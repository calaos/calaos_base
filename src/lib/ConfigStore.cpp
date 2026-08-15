/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include "ConfigStore.h"

#include "Constants.h"
#include "FileUtils.h"
#include "LogSetup.h"

#include <TinyXML/tinyxml.h>

#include <fstream>
#include <iostream>
#include <list>
#include <mutex>
#include <stdexcept>

#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace Utils;
using namespace std;

static const char* ENV_CONFIG = "CALAOS_CONFIG";

static string _configBase;
static string _cacheBase;

string Utils::getConfigPath()
{
    string result;
    string home;

    if (getenv("HOME"))
    {
        home = getenv("HOME");
    }
    else
    {
        //getpwuid() returns NULL if the uid has no passwd entry (e.g.
        //containers/chroots with a bare /etc/passwd); fall back to /tmp
        //rather than dereferencing a null pointer.
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }

    list<string> confDirs;
    confDirs.push_back(home + "/" + HOME_CONFIG_PATH);
    confDirs.push_back(ETC_CONFIG_PATH);
    confDirs.push_back(PREFIX_CONFIG_PATH);

    //Check config in that order:
    // - $HOME/.config/calaos/
    // - /etc/calaos
    // - pkg_prefix/etc/calaos
    // - create $HOME/.config/calaos/ if nothing found

    list<string>::iterator it = confDirs.begin();
    for (;it != confDirs.end();it++)
    {
        string conf = *it;
        conf += "/" IO_CONFIG;

        if (FileUtils::exists(conf))
        {
          result = *it;
          break;
        }
    }

    if (result.empty())
    {
        //no config dir found, create $HOME/.config/calaos
        mkdir(string(home + "/.config").c_str(), S_IRWXU);
        mkdir(string(home + "/.config/calaos").c_str(), S_IRWXU);
        result = home + "/" + HOME_CONFIG_PATH;
    }

    return result;
}

string Utils::getCachePath()
{
    if (_cacheBase.empty())
    {
        string home;
        if (getenv("HOME"))
        {
            home = getenv("HOME");
        }
        else
        {
            //See getConfigPath() above: getpwuid() can return NULL.
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }

        //force the creation of .cache/calaos
        mkdir(string(home + "/.cache").c_str(), S_IRWXU);
        mkdir(string(home + "/.cache/calaos").c_str(), S_IRWXU);

        _cacheBase = home + "/.cache/calaos";
    }

    return _cacheBase;
}

string Utils::getConfigFile(const char *configType)
{
    if (_configBase.empty())
    {
        const char* envConfig = getenv(ENV_CONFIG);

        if (envConfig) {
            _configBase = envConfig;
        }
        else
        {
            _configBase = getConfigPath();
        }
    }

    return _configBase + "/" + configType;
}

string Utils::getCacheFile(const char *cacheFile)
{
    if (_cacheBase.empty())
    {
        string home;
        if (getenv("HOME"))
        {
            home = getenv("HOME");
        }
        else
        {
            //See getConfigPath() above: getpwuid() can return NULL.
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }

        //force the creation of .cache/calaos
        mkdir(string(home + "/.cache").c_str(), S_IRWXU);
        mkdir(string(home + "/.cache/calaos").c_str(), S_IRWXU);

        _cacheBase = home + "/.cache/calaos";
    }

    return _cacheBase + "/" + cacheFile;
}

/* Config options storage (local_config.xml)
 * =========================================
 *
 * local_config.xml is shared by several processes (calaos_server, calaos_config,
 * calaos_mail, and Calaos Home outside of this repository) and it holds secrets
 * (smtp_password, influxdb_token, mcp_token, cn_pass...). Accesses are therefore
 * serialised twice:
 *   - configMutex serialises the threads of the current process,
 *   - an flock() on a companion local_config.xml.lock file serialises the
 *     processes between them.
 *
 * The lock is taken on a separate file, never on local_config.xml itself:
 * flock() locks an inode, and the atomic write below replaces the config file
 * inode with rename(). A lock held on the config file would be silently dropped
 * at exactly the wrong moment.
 *
 * DEADLOCK RULE
 * -------------
 * flock() locks are attached to the open file description, so a second
 * open() + flock() from the same process blocks against the first one, and
 * std::mutex is not recursive either. Therefore no function taking those locks
 * may call another function that takes them. All the real work lives in the
 * unlocked static helpers below (doGetConfigOption, doGetConfigOptions,
 * doSetConfigOptions, saveConfigDocument, loadConfigDocument); each public entry
 * point takes the locks exactly once and delegates to those helpers, and no
 * helper ever calls a public entry point.
 *
 * Error messages are queued and logged only once every lock has been released:
 * the logger reads its own level through get_config_option() on first use, so
 * logging from inside the locked section would re-enter it and deadlock.
 */

//A global mutex for get/set config options threadsafely
static std::mutex configMutex;

namespace
{

struct ConfigLogMessage
{
    bool warning;
    string message;
};

typedef std::vector<ConfigLogMessage> ConfigErrors;

void addError(ConfigErrors &errors, const string &msg)
{
    errors.push_back({ false, msg });
}

void addWarning(ConfigErrors &errors, const string &msg)
{
    errors.push_back({ true, msg });
}

//Log the messages collected while the locks were held.
//Must never be called with a config lock held, see the deadlock rule above.
void flushConfigErrors(const ConfigErrors &errors, bool noLoggerOut)
{
    for (const ConfigLogMessage &m: errors)
    {
        if (noLoggerOut)
            cout << m.message << endl;
        else if (m.warning)
            cWarningDom("config") << m.message;
        else
            cErrorDom("config") << m.message;
    }
}

//Warning about a missing lock is emitted only once per process, it would
//otherwise be repeated on every single config read.
bool configLockWarned = false;

/* RAII inter-process lock on <configdir>/local_config.xml.lock.
 * operation is LOCK_EX for the mutating functions, LOCK_SH for the readers.
 */
class ConfigFileLock
{
public:
    ConfigFileLock(int operation, ConfigErrors &errors):
        fd(-1),
        proceed(false)
    {
        const bool shared = (operation == LOCK_SH);
        string lockFile = Utils::getConfigFile(LOCAL_CONFIG) + ".lock";

        //The lock file sits next to the secrets, keep it private.
        int f;
        do
        {
            f = open(lockFile.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR);
        }
        while (f < 0 && errno == EINTR);

        //A read-only rootfs cannot host the lock file. Readers must keep
        //working there: writes are atomic (rename), so an unlocked read can
        //never observe a partial file anyway.
        if (f < 0 && shared)
        {
            do
            {
                f = open(lockFile.c_str(), O_RDONLY | O_CLOEXEC);
            }
            while (f < 0 && errno == EINTR);
        }

        if (f < 0)
        {
            int err = errno;
            reportFailure(shared, errors, "Unable to open config lock file " + lockFile +
                                          ": " + strerror(err));
            return;
        }

        int ret;
        do
        {
            ret = ::flock(f, operation);
        }
        while (ret < 0 && errno == EINTR);

        if (ret < 0)
        {
            int err = errno;
            reportFailure(shared, errors, "Unable to lock config file " + lockFile +
                                          ": " + strerror(err));
            close(f);
            return;
        }

        fd = f;
        proceed = true;
    }

    ~ConfigFileLock()
    {
        if (fd < 0)
            return;

        //Closing the descriptor releases the flock()
        close(fd);
    }

    ConfigFileLock(const ConfigFileLock &) = delete;
    ConfigFileLock &operator=(const ConfigFileLock &) = delete;

    //True when the caller may access the config file
    bool canProceed() const { return proceed; }

private:
    /* A writer that could not take the lock must fail: writing unlocked is
     * exactly the key losing race this lock exists for. A reader keeps going,
     * it is the only way to still read the config on a read-only rootfs where
     * the lock file cannot be created, and writes are atomic so an unlocked
     * read never observes a partial file. The warning is emitted once per
     * process, it would otherwise be repeated on every single read.
     */
    void reportFailure(bool shared, ConfigErrors &errors, const string &msg)
    {
        if (!shared)
        {
            addError(errors, msg);
            return;
        }

        proceed = true;

        if (!configLockWarned)
        {
            configLockWarned = true;
            addWarning(errors, msg + ", reading config unlocked");
        }
    }

    int fd;
    bool proceed;
};

//Split a path into its directory and its last component
void splitPath(const string &path, string &dir, string &base)
{
    size_t pos = path.rfind('/');

    if (pos == string::npos)
    {
        dir = ".";
        base = path;
    }
    else
    {
        dir = (pos == 0)? "/": path.substr(0, pos);
        base = path.substr(pos + 1);
    }
}

/* Write document to path, atomically and durably.
 * Unlocked: the caller must hold the config locks.
 * On any failure the original file is left strictly untouched.
 */
bool saveConfigDocument(const TiXmlDocument &document, const string &path, ConfigErrors &errors)
{
    char resolved[PATH_MAX];
    string target;

    //local_config.xml is often a symlink to persistent storage on an overlay
    //or read-only rootfs. Resolve it first, renaming onto the symlink would
    //replace it by a regular file in the wrong place.
    if (realpath(path.c_str(), resolved))
    {
        target = resolved;
    }
    else if (errno == ENOENT)
    {
        //File not created yet, resolve the directory holding it
        string dirPart, basePart;
        splitPath(path, dirPart, basePart);

        if (!realpath(dirPart.c_str(), resolved))
        {
            int err = errno;
            addError(errors, "Unable to resolve config directory " + dirPart + ": " + strerror(err));
            return false;
        }

        target = string(resolved) + "/" + basePart;
    }
    else
    {
        int err = errno;
        addError(errors, "Unable to resolve config file " + path + ": " + strerror(err));
        return false;
    }

    string dir, base;
    splitPath(target, dir, base);

    //The temporary file must live in the same directory as the target,
    //rename() fails with EXDEV across filesystems. mkstemp() gives it a unique
    //name (two writers never collide) and creates it with mode 0600.
    string tmpl = dir + "/." + base + ".XXXXXX";
    std::vector<char> tmpPath(tmpl.begin(), tmpl.end());
    tmpPath.push_back('\0');

    int fd = mkstemp(tmpPath.data());
    if (fd < 0)
    {
        int err = errno;
        addError(errors, "Unable to create temporary config file in " + dir + ": " + strerror(err));
        return false;
    }

    struct stat st;
    bool ok = true;

    if (stat(target.c_str(), &st) == 0)
    {
        //Keep the mode and the owner of the original file: the umask would
        //otherwise widen the rights of a file holding smtp_password/mcp_token.
        if (fchmod(fd, st.st_mode & 07777) != 0)
        {
            int err = errno;
            addError(errors, "Unable to set mode on temporary config file: " + string(strerror(err)));
            ok = false;
        }

        //fchown() fails for a non root user, this is expected and must not
        //fail the write. Only attempt it when the owner would really change.
        if (ok && (st.st_uid != geteuid() || st.st_gid != getegid()))
        {
            if (fchown(fd, st.st_uid, st.st_gid) != 0)
            {
                int err = errno;
                addWarning(errors, "Unable to preserve owner of " + target + ": " + strerror(err) +
                                   ", the file now belongs to the current user");
            }
        }
    }
    else if (fchmod(fd, S_IRUSR | S_IWUSR) != 0)
    {
        //Brand new file: 0600 rather than whatever the umask allows
        int err = errno;
        addError(errors, "Unable to set mode on temporary config file: " + string(strerror(err)));
        ok = false;
    }

    FILE *fp = nullptr;

    if (ok)
    {
        fp = fdopen(fd, "w");
        if (!fp)
        {
            int err = errno;
            addError(errors, "Unable to open temporary config file: " + string(strerror(err)));
            close(fd);
            ok = false;
        }
    }
    else
    {
        close(fd);
    }

    if (ok && !document.SaveFile(fp))
    {
        addError(errors, "Unable to write temporary config file " + string(tmpPath.data()));
        ok = false;
    }

    //fsync() the data before the rename: without it the rename can be made
    //durable before the content and a power cut leaves an empty config.
    if (ok && (fflush(fp) != 0 || fsync(fileno(fp)) != 0))
    {
        int err = errno;
        addError(errors, "Unable to flush temporary config file: " + string(strerror(err)));
        ok = false;
    }

    if (fp && fclose(fp) != 0 && ok)
    {
        int err = errno;
        addError(errors, "Unable to close temporary config file: " + string(strerror(err)));
        ok = false;
    }

    if (ok && rename(tmpPath.data(), target.c_str()) != 0)
    {
        int err = errno;
        addError(errors, "Unable to replace config file " + target + ": " + strerror(err));
        ok = false;
    }

    if (!ok)
    {
        //Leave the original untouched, never a partial state
        unlink(tmpPath.data());
        return false;
    }

    //fsync() the directory so that the rename itself survives a power cut
    int dirFd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirFd < 0)
    {
        int err = errno;
        addWarning(errors, "Unable to open " + dir + " to sync it: " + strerror(err));
    }
    else
    {
        if (fsync(dirFd) != 0)
        {
            int err = errno;
            addWarning(errors, "Unable to sync directory " + dir + ": " + strerror(err));
        }
        close(dirFd);
    }

    return true;
}

//Load local_config.xml. Unlocked: the caller must hold the config locks.
bool loadConfigDocument(TiXmlDocument &document, ConfigErrors &errors)
{
    if (document.LoadFile())
        return true;

    addError(errors, "There was an exception in XML parsing.");
    addError(errors, string("Parse error: ") + document.ErrorDesc());
    addError(errors, "In file " + Utils::getConfigFile(LOCAL_CONFIG) +
                     " At line " + std::to_string(document.ErrorRow()));

    return false;
}

//Unlocked: the caller must hold the config locks.
string doGetConfigOption(const string &key, ConfigErrors &errors)
{
    string value;
    TiXmlDocument document(Utils::getConfigFile(LOCAL_CONFIG).c_str());

    if (!loadConfigDocument(document, errors))
        return value;

    TiXmlHandle docHandle(&document);

    TiXmlElement *keyNode = docHandle.FirstChildElement("calaos:config").FirstChildElement().ToElement();
    for (;keyNode; keyNode = keyNode->NextSiblingElement())
    {
        if (keyNode->ValueStr() == "calaos:option" &&
            keyNode->Attribute("name") &&
            keyNode->Attribute("name") == key &&
            keyNode->Attribute("value"))
        {
            value = keyNode->Attribute("value");
            break;
        }
    }

    return value;
}

//Unlocked: the caller must hold the config locks.
bool doGetConfigOptions(Params &options, ConfigErrors &errors)
{
    TiXmlDocument document(Utils::getConfigFile(LOCAL_CONFIG).c_str());

    if (!loadConfigDocument(document, errors))
        return false;

    TiXmlHandle docHandle(&document);

    TiXmlElement *keyNode = docHandle.FirstChildElement("calaos:config").FirstChildElement().ToElement();
    for (;keyNode; keyNode = keyNode->NextSiblingElement())
    {
        if (keyNode->ValueStr() == "calaos:option" &&
            keyNode->Attribute("name") &&
            keyNode->Attribute("value"))
        {
            options.Add(keyNode->Attribute("name"), keyNode->Attribute("value"));
        }
    }

    return true;
}

/* Apply all the modifications in one load/modify/write cycle.
 * Unlocked: the caller must hold the config locks.
 * The document is loaded here, under the lock, so every key that is neither in
 * toSet nor in toDelete keeps the value another process may have given it.
 */
bool doSetConfigOptions(const Params &toSet, const std::vector<string> &toDelete, ConfigErrors &errors)
{
    string file = Utils::getConfigFile(LOCAL_CONFIG);
    TiXmlDocument document(file.c_str());

    if (!loadConfigDocument(document, errors))
        return false;

    TiXmlHandle docHandle(&document);

    TiXmlElement *root = docHandle.FirstChildElement("calaos:config").ToElement();
    if (!root)
    {
        addError(errors, "No calaos:config root element in " + file);
        return false;
    }

    bool changed = false;

    for (const string &key: toDelete)
    {
        TiXmlElement *next = nullptr;
        for (TiXmlElement *keyNode = root->FirstChildElement(); keyNode; keyNode = next)
        {
            //RemoveChild() deletes the node, get the next one first
            next = keyNode->NextSiblingElement();

            if (keyNode->ValueStr() == "calaos:option" &&
                keyNode->Attribute("name") &&
                keyNode->Attribute("name") == key)
            {
                root->RemoveChild(keyNode);
                changed = true;
            }
        }
    }

    for (int i = 0;i < toSet.size();i++)
    {
        string key, value;
        toSet.get_item(i, key, value);

        bool found = false;
        for (TiXmlElement *keyNode = root->FirstChildElement(); keyNode; keyNode = keyNode->NextSiblingElement())
        {
            if (keyNode->ValueStr() == "calaos:option" &&
                keyNode->Attribute("name") &&
                keyNode->Attribute("name") == key)
            {
                if (!keyNode->Attribute("value") || keyNode->Attribute("value") != value)
                {
                    keyNode->SetAttribute("value", value);
                    changed = true;
                }
                found = true;
                break;
            }
        }

        if (!found)
        {
            TiXmlElement *element = new TiXmlElement("calaos:option");
            element->SetAttribute("name", key);
            element->SetAttribute("value", value);
            root->LinkEndChild(element);
            changed = true;
        }
    }

    //Nothing to write, do not rewrite the file for nothing, this runs on flash
    if (!changed)
        return true;

    return saveConfigDocument(document, file, errors);
}

}

void Utils::initConfigOptions(char *configdir, char *cachedir, bool quiet)
{
    if (configdir)
    {
        _configBase = configdir;
        setenv(ENV_CONFIG, configdir, 1);
    }

    if (cachedir) _cacheBase = cachedir;

    string file = getConfigFile(LOCAL_CONFIG);

    if (!quiet)
    {
        cout << "Using config path: " << getConfigFile("") << endl;
        cout << "Using cache path: " << getCacheFile("") << endl;
    }

    if (!FileUtils::isWritable(getConfigFile("")))
        throw (runtime_error("config path is not writable"));
    if (!FileUtils::isWritable(getCacheFile("")))
        throw (runtime_error("cache path is not writable"));

    ConfigErrors errors;
    bool created = false;

    {
        //The locks are taken once here and the unlocked helpers are used:
        //calling the public functions would take them a second time and
        //deadlock, see the deadlock rule above.
        std::lock_guard<std::mutex> lock{configMutex};
        ConfigFileLock fileLock(LOCK_EX, errors);

        if (fileLock.canProceed())
        {
            if (!FileUtils::exists(file))
            {
                //create a defaut config
                std::ofstream conf(file.c_str(), std::ofstream::out);
                conf << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" << endl;
                conf << "<calaos:config xmlns:calaos=\"http://www.calaos.fr\">" << endl;
                conf << "</calaos:config>" << endl;
                conf.close();

                //The file holds secrets, do not let the umask decide
                if (chmod(file.c_str(), S_IRUSR | S_IWUSR) != 0)
                {
                    int err = errno;
                    addWarning(errors, "Unable to restrict mode of " + file + ": " + strerror(err));
                }

                //All the defaults in a single write instead of one rewrite
                //of the whole file per option
                Params defaults = {
                    { "show_cursor", "true" },
                    { "dpms_enable", "false" },
                    { "smtp_server", "" },
                    { "cn_user", "user" },
                    { "cn_pass", "pass" },
                    { "longitude", "2.322235" },
                    { "latitude", "48.864715" },
                    { "notif/battery_mail_enabled", "true" },
                    { "notif/battery_push_enabled", "true" },
                    { "notif/io_connected_mail_enabled", "true" },
                    { "notif/io_connected_push_enabled", "true" },
                };

                doSetConfigOptions(defaults, {}, errors);
                created = true;
            }
            else
            {
                Params paramsConf;

                if (doGetConfigOptions(paramsConf, errors))
                {
                    // Set default values if not set
                    Params missing;

                    if (!paramsConf.Exists("notif/battery_mail_enabled"))
                        missing.Add("notif/battery_mail_enabled", "true");
                    if (!paramsConf.Exists("notif/battery_push_enabled"))
                        missing.Add("notif/battery_push_enabled", "true");
                    if (!paramsConf.Exists("notif/io_connected_mail_enabled"))
                        missing.Add("notif/io_connected_mail_enabled", "true");
                    if (!paramsConf.Exists("notif/io_connected_push_enabled"))
                        missing.Add("notif/io_connected_push_enabled", "true");

                    //Only write when something is really missing
                    if (missing.size() > 0)
                        doSetConfigOptions(missing, {}, errors);
                }
            }
        }
    }

    flushConfigErrors(errors, false);

    if (created && !quiet)
        cout << "WARNING: no local_config.xml found, generating default config with username: \"user\" and password: \"pass\"" << endl;
}

string Utils::get_config_option(string _key, bool no_logger_out)
{
    string value;
    ConfigErrors errors;

    {
        std::lock_guard<std::mutex> lock{configMutex};
        ConfigFileLock fileLock(LOCK_SH, errors);

        if (fileLock.canProceed())
            value = doGetConfigOption(_key, errors);
    }

    flushConfigErrors(errors, no_logger_out);

    return value;
}

bool Utils::get_config_options(Params &options)
{
    bool res = false;
    ConfigErrors errors;

    {
        std::lock_guard<std::mutex> lock{configMutex};
        ConfigFileLock fileLock(LOCK_SH, errors);

        if (fileLock.canProceed())
            res = doGetConfigOptions(options, errors);
    }

    flushConfigErrors(errors, false);

    return res;
}

bool Utils::set_config_option(string key, string value)
{
    //Not implemented with set_config_options(): that would take the locks a
    //second time and deadlock, see the deadlock rule above.
    Params toSet;
    toSet.Add(key, value);

    bool res = false;
    ConfigErrors errors;

    {
        std::lock_guard<std::mutex> lock{configMutex};
        ConfigFileLock fileLock(LOCK_EX, errors);

        if (fileLock.canProceed())
            res = doSetConfigOptions(toSet, {}, errors);
    }

    flushConfigErrors(errors, false);

    return res;
}

bool Utils::del_config_option(string key)
{
    bool res = false;
    ConfigErrors errors;

    {
        std::lock_guard<std::mutex> lock{configMutex};
        ConfigFileLock fileLock(LOCK_EX, errors);

        if (fileLock.canProceed())
            res = doSetConfigOptions(Params(), { key }, errors);
    }

    flushConfigErrors(errors, false);

    return res;
}

bool Utils::set_config_options(const Params &toSet, const std::vector<string> &toDelete)
{
    bool res = false;
    ConfigErrors errors;

    {
        std::lock_guard<std::mutex> lock{configMutex};
        ConfigFileLock fileLock(LOCK_EX, errors);

        if (fileLock.canProceed())
            res = doSetConfigOptions(toSet, toDelete, errors);
    }

    flushConfigErrors(errors, false);

    return res;
}
