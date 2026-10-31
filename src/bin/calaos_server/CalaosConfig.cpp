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
#include "CalaosConfig.h"
#include <iomanip>
#include <ctime>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <utility>
#include <filesystem>
#include <fstream>
#include "FileUtils.h"
#include "NotifManager.h"
#include "AutoScenarioDef.h"

using namespace Calaos;

namespace
{

//Wait for the server to be fully up (event loop running, network and
//notification infrastructure usable) before sending a corruption alert
constexpr double CONFIG_ALERT_DELAY_SEC = 30.0;

//Bound on the suffix search in BackupFiles(); reaching it means something
//other than a burst of uploads is writing there.
constexpr int MAX_BACKUPS_PER_SECOND = 1000;

//Outcome of loading one XML config file, also feeds the corruption alert
struct XmlLoadResult
{
    bool loaded = false;      //document is usable
    bool wasCorrupt = false;  //the file on disk failed to parse
    bool readFailed = false;  //not read, its content may well be valid
    bool absent = false;      //no file, and the skeleton could not be created
    string corruptCopy;       //where the corrupt file was preserved
    string restoredFrom;      //backup restored over file, empty if none
};

//All backup copies of configName under <config>/backups (as written by
//Config::BackupFiles()), sorted newest first by file modification time.
//The preserved corrupt copies live under backups/corrupt/ with a timestamp
//suffix in the file name, so they never match configName here.
vector<string> findBackupsNewestFirst(const string &configName)
{
    namespace fs = std::filesystem;

    string backupRoot = Utils::getConfigFile("backups");
    vector<std::pair<fs::file_time_type, string>> found;

    std::error_code ec;
    fs::recursive_directory_iterator it(backupRoot,
                                        fs::directory_options::skip_permission_denied,
                                        ec), end;
    for (;!ec && it != end;it.increment(ec))
    {
        std::error_code fec;
        if (!it->is_regular_file(fec) || fec)
            continue;
        if (it->path().filename() != configName)
            continue;

        auto t = fs::last_write_time(it->path(), fec);
        if (fec)
            continue;

        found.emplace_back(t, it->path().string());
    }

    std::sort(found.begin(), found.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

    vector<string> result;
    result.reserve(found.size());
    for (auto &f: found)
        result.push_back(std::move(f.second));
    return result;
}

//Preserve a corrupt config file for later inspection, before a backup gets
//restored over it or a later save replaces it. Returns the preserved path,
//empty on error.
string preserveCorruptFile(const string &file, const string &configName)
{
    string dir = Utils::getConfigFile("backups") + "/corrupt";
    if (!FileUtils::mkpath(dir))
    {
        cError() << "Unable to create " << dir << ", corrupt " << configName << " not preserved";
        return {};
    }

    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    string dest = dir + "/" + configName + "." + ss.str();

    //Still the tokens and camera passwords of the file it was meant to be
    if (!FileUtils::copyFile(file, dest, true))
    {
        cError() << "Unable to copy corrupt " << file << " to " << dest;
        return {};
    }

    cWarning() << "Corrupt " << configName << " preserved at " << dest << " for inspection";
    return dest;
}

//Only a parse error proves the content bad. pugixml reports every fopen
//failure as status_file_not_found, so look at the file again: EACCES or
//EMFILE on a valid file must not send it to the backups.
bool isReadFailure(const pugi::xml_parse_result &parsed, const string &file)
{
    switch (parsed.status)
    {
    case pugi::status_file_not_found:
    {
        std::error_code ec;
        return std::filesystem::status(file, ec).type() != std::filesystem::file_type::not_found;
    }
    case pugi::status_io_error:
    case pugi::status_out_of_memory:
    case pugi::status_internal_error: //the directory guard of loadXmlDocument
        return true;
    default:
        return false;
    }
}

//Load file into document. On a parse error, log it, preserve the corrupt
//file, then walk the backups newest to oldest and restore the first one
//that parses. A file that could not be read is left alone: its content is
//unknown, and so is that of a valid XML file with another root than rootName,
//from which nothing gets loaded. loaded stays false in those cases and when no
//backup is usable. Never exits: a corrupt config must not kill the daemon.
XmlLoadResult loadXmlDocument(pugi::xml_document &document, const string &file,
                              const string &configName, const char *rootName)
{
    XmlLoadResult res;

    //pugixml sizes its buffer from ftell, which is LONG_MAX on a directory
    pugi::xml_parse_result parsed;
    const bool isDirectory = FileUtils::isDir(file);
    if (!isDirectory)
        parsed = document.load_file(file.c_str());
    if (parsed && document.child(rootName))
    {
        res.loaded = true;
        return res;
    }

    if (parsed || isReadFailure(parsed, file))
    {
        res.readFailed = true;
        //parsed.description() says "not found" for a file that is there
        const char *reason = parsed.description();
        if (isDirectory)
            reason = "it is a directory";
        else if (parsed)
            reason = "it has another root element";
        else if (parsed.status == pugi::status_file_not_found)
            reason = "it could not be opened";
        cError() << "Unable to read " << file << " (" << reason
                 << "), config not loaded, no backup restored, file left as it was";
        return res;
    }

    //Nothing valid to lose: a dangling link, the skeleton could not go through it
    const bool absent = parsed.status == pugi::status_file_not_found;

    res.wasCorrupt = true;
    res.absent = absent;
    if (absent)
        cError() << file << " does not exist and could not be created";
    else
    {
        cError() << "There was a parse error in " << file;
        cError() << parsed.description();
        //pugixml reports a byte offset where the old TinyXML 1 reader (removed in
        //E4.4e) reported a row; the offset is what it has, and it points at the
        //same place in the file.
        cError() << "In file " << file << " At offset " << parsed.offset;

        res.corruptCopy = preserveCorruptFile(file, configName);
    }

    for (const string &backup: findBackupsNewestFirst(configName))
    {
        //Read once: the bytes written over the live file are the ones parsed here
        std::ifstream in(backup, std::ios::binary);
        std::stringstream content;
        content << in.rdbuf();
        const string bytes = content.str();

        pugi::xml_document candidate;
        pugi::xml_parse_result candidateParsed = candidate.load_buffer(bytes.data(), bytes.size());
        if (!candidateParsed || !candidate.child(rootName))
        {
            cWarning() << "Backup " << backup << " is unreadable or has a parse error too ("
                       << (candidateParsed ? "another root element" : candidateParsed.description())
                       << "), trying an older one";
            continue;
        }

        //Written aside then renamed: a full disk must not cost the live file,
        //its mode, or the chance to try an older backup
        if (!Utils::restore_config_file(file, bytes))
        {
            cError() << "Unable to restore backup " << backup << ", trying an older one";
            continue;
        }

        document.reset(candidate);
        cInfo() << configName << " restored from backup " << backup;
        if (res.corruptCopy.empty() && !absent)
            cWarning() << "The corrupt bytes of " << file << " were not preserved and are lost";
        res.loaded = true;
        res.restoredFrom = backup;
        return res;
    }

    cError() << "No usable backup found for " << configName << ", config not loaded, "
             << file << " left as it was until a config put or a config_restore replaces it";
    return res;
}

//First start: the file truly is not there. FileUtils::exists() is false as
//well for a file it could not look at, which is not a first start.
void createSkeleton(const string &file, const char *content)
{
    std::error_code ec;
    if (std::filesystem::status(file, ec).type() != std::filesystem::file_type::not_found)
        return;

    cWarning() << file << " does not exist, first start: creating an empty one";

    //Through a dangling link too: its target may be a fresh persistent volume
    std::ofstream conf(file.c_str(), std::ofstream::out);
    conf << content;
    conf.close();

    //Saves keep the mode of the file they replace, so it is decided here
    ::chmod(file.c_str(), 0600);
}

//Goes where a restore goes: mode and owner survive, a failure costs nothing
void saveDocument(const pugi::xml_document &document, const string &file)
{
    cInfo() << "Saving " << file << "...";

    std::ostringstream content;
    document.save(content, XmlUtils::CONFIG_INDENT);

    if (Utils::restore_config_file(file, content.str()))
        cInfo() << "Done.";
    else
        cError() << "Unable to save " << file;
}

//Human readable report of one recovery, used for the mail/push notification
string configAlertMessage(const string &configName, const XmlLoadResult &res)
{
    const string untilReplaced = "Changes to it are refused, and no save writes the file, until a "
                                 "configuration upload or a restore replaces it.";

    if (res.readFailed)
        return "The configuration file " + configName +
               " could not be read (see server logs).\nIt was left as it was and no backup was "
               "restored, but the server started with an EMPTY " +
               configName + " configuration. " + untilReplaced;

    string msg;
    if (res.absent)
        msg = "The configuration file " + configName + " does not exist and could not be created.";
    else
    {
        msg = "The configuration file " + configName + " was corrupt and could not be parsed.";

        if (!res.corruptCopy.empty())
            msg += "\nThe corrupt file was preserved at: " + res.corruptCopy;
        else
            msg += "\nThe corrupt file could not be preserved (see server logs).";
    }

    if (!res.restoredFrom.empty())
        msg += "\nThe configuration was automatically restored from backup: " + res.restoredFrom;
    else
        msg += "\nNo usable backup was found: the server started with an EMPTY " + configName +
               " configuration. " + untilReplaced;

    return msg;
}

}

Config::Config()
{
    loadStateCache();

    saveCacheTimer = std::make_shared<Timer>(60.0, [this]() { saveStateCache(); });
}

Config::~Config()
{
    //Flush state changes recorded with save=false that the 60s timer did not
    //persist yet: without this, up to 60s of IO states are lost on shutdown.
    saveCacheTimer.reset();
    saveStateCache();
}

void Config::scheduleConfigAlert(const string &message)
{
    configAlerts.push_back(message);

    if (configAlertScheduled)
        return;
    configAlertScheduled = true;

    //LoadConfigIO/LoadConfigRule run from main() before the event loop is
    //started: defer the notification until the server is fully up (loop
    //running, network and notification infrastructure usable). Config is an
    //eternal singleton, capturing this is safe.
    //T3.40: rechecked against the sweep - Config::Instance() is a
    //function-local static (CalaosConfig.h) and nothing deletes it, so this
    //`this` cannot dangle. Left unguarded on that measured ground.
    Timer::singleShot(CONFIG_ALERT_DELAY_SEC, [this]() { sendConfigAlerts(); });
}

void Config::sendConfigAlerts()
{
    if (configAlerts.empty())
    {
        configAlertScheduled = false;
        return;
    }

    //Wording is generic since E4.2e: this channel now carries both the corrupt
    //file recovery and the rules disabled for a missing IO.
    string body = "The Calaos server detected configuration problems at startup:\n\n";
    for (const string &m: configAlerts)
        body += m + "\n\n";

    cWarning() << "Sending config problem notification (mail + push)";

    //Same mechanism/settings as the IO alerts (battery, connected status):
    //NotifManager reads the mail/push configuration from local_config.xml
    NotifManager::Instance().sendMailNotification("Calaos: configuration problem detected", body);
    NotifManager::Instance().sendPushNotification(
        "Calaos: a configuration problem was detected at startup. "
        "Check your mailbox or the server logs for details.");

    configAlerts.clear();
    configAlertScheduled = false;
}

void Config::LoadConfigIO()
{
    std::string file = Utils::getConfigFile(IO_CONFIG);

    createSkeleton(file, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                         "<calaos:ioconfig xmlns:calaos=\"http://www.calaos.fr\">\n"
                         "<calaos:home></calaos:home>\n"
                         "</calaos:ioconfig>\n");

    pugi::xml_document document;

    XmlLoadResult xmlres = loadXmlDocument(document, file, IO_CONFIG, "calaos:ioconfig");
    if (xmlres.wasCorrupt || xmlres.readFailed)
        scheduleConfigAlert(configAlertMessage(IO_CONFIG, xmlres));
    ioLoaded = xmlres.loaded;
    if (!xmlres.loaded)
        return;

    pugi::xml_node room_node = XmlUtils::firstChildElement(
                document.child("calaos:ioconfig").child("calaos:home"));
    for(; room_node; room_node = XmlUtils::nextSiblingElement(room_node))
    {
        if (string(room_node.name()) == "calaos:room" &&
            room_node.attribute("name") &&
            room_node.attribute("type"))
        {
            string name, type;

            name = room_node.attribute("name").as_string();
            type = room_node.attribute("type").as_string();

            //E4.4cd: the only out-parameter read of the whole tree.
            //TiXmlElement::Attribute("hits", &hits) left hits UNTOUCHED both
            //when the attribute was absent and when its value did not parse;
            //hits starts at 0, so as_int(0) reproduces "present but empty" and
            //"present but garbage" landing on 0, and the guard keeps "absent"
            //a distinct, equally-0 case rather than an implicit one.
            pugi::xml_attribute hitsAttr = room_node.attribute("hits");
            int hits = hitsAttr ? hitsAttr.as_int(0) : 0;

            Room *room = new Room(name, type, hits);
            ListeRoom::Instance().Add(room);

            room->LoadFromXml(room_node);
        }
    }

    //Before the rules load, which may act on what the links expose.
    ListeRoom::Instance().resolveDeviceLinks();

    cInfo() <<  "Done. ";
}

void Config::SaveConfigIO()
{
    if (Utils::config_frozen())
        return;

    if (!ioLoaded)
    {
        cError() << "Skipping the save of " << IO_CONFIG << ": it was not loaded";
        return;
    }

    pugi::xml_document document;
    XmlUtils::appendDeclaration(document);
    pugi::xml_node ionode = document.append_child("calaos:ioconfig");
    XmlUtils::setAttribute(ionode, "xmlns:calaos", "http://www.calaos.fr");
    pugi::xml_node node = ionode.append_child("calaos:home");
    if (ListeRoom::Instance().deviceCount() > 0)
        XmlUtils::setAttribute(node, "format", IO_FORMAT_DEVICES);

    for (int i = 0;i < ListeRoom::Instance().size();i++)
    {
        Room *room = ListeRoom::Instance().get_room(i);
        room->SaveToXml(node);
    }

    saveDocument(document, Utils::getConfigFile(IO_CONFIG));
}

void Config::LoadConfigRule()
{
    std::string file = Utils::getConfigFile(RULES_CONFIG);

    createSkeleton(file, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                         "<calaos:rules xmlns:calaos=\"http://www.calaos.fr\">\n"
                         "</calaos:rules>\n");

    pugi::xml_document document;

    XmlLoadResult xmlres = loadXmlDocument(document, file, RULES_CONFIG, "calaos:rules");
    if (xmlres.wasCorrupt || xmlres.readFailed)
        scheduleConfigAlert(configAlertMessage(RULES_CONFIG, xmlres));
    rulesLoaded = xmlres.loaded;
    if (!xmlres.loaded)
        return;

    pugi::xml_node rule_node = XmlUtils::firstChildElement(document.child("calaos:rules"));

    if (!rule_node)
    {
        cError() <<  "Error, <calaos:rules> node not found in file " << file;
    }

    for(; rule_node; rule_node = XmlUtils::nextSiblingElement(rule_node))
    {
        if (string(rule_node.name()) == "calaos:rule" &&
            rule_node.attribute("name") &&
            rule_node.attribute("type"))
        {
            string name, type;

            name = rule_node.attribute("name").as_string();
            type = rule_node.attribute("type").as_string();

            Rule *rule = new Rule(type, name);
            rule->LoadFromXml(rule_node);

            ListeRule::Instance().Add(rule);
        }
    }

    cInfo() <<  "Done. " << ListeRule::Instance().size() << " rules loaded.";

    /* E4.2e. Rules kept but disabled because they reference an IO that is not
     * in io.xml (deleted, renamed, driver removed). Each one already logged
     * itself with its missing ids; this is the summary, plus the same alert
     * channel the corrupt-config recovery uses, so the user does not have to
     * read the log to learn that one of their rules stopped running.
     */
    vector<Rule *> disabled = ListeRule::Instance().getDisabledRules();
    if (!disabled.empty())
    {
        string report = "The following rules were loaded but are DISABLED: they "
                        "reference IOs that do not exist in " IO_CONFIG ", and a "
                        "rule missing part of its criteria would run on incomplete "
                        "conditions.\n";

        bool anyScenario = false;

        for (Rule *rule: disabled)
        {
            /* T3.18. A scenario step rule is named "<scenario_id>_step", which
             * says nothing to anybody: name the SCENARIO instead. Without this
             * the alert exists but does not tell the user that one of their
             * scenarios is dead - and a scenario, unlike a plain rule, will NOT
             * come back on its own once the IO is restored.
             *
             * The uid and not `auto_scenario`: the legacy key also sits on
             * rules no scenario claims any more, and telling the user those are
             * steps of a scenario sends them looking for one that does not
             * exist. Only a rule the projection wrote carries the uid.
             */
            if (rule->param_exists(AutoScenarioDef::KEY_UID))
            {
                anyScenario = true;
                report += "\n- step of scenario '" +
                          rule->get_param(AutoScenarioDef::KEY_UID) +
                          "' (rule '" + rule->get_name() + "'): missing IO(s) " +
                          rule->getMissingIoDescription();
            }
            else
            {
                report += "\n- rule '" + rule->get_name() + "': missing IO(s) " +
                          rule->getMissingIoDescription();
            }
        }

        report += "\n\nThey are kept in your configuration untouched and will run "
                  "again as soon as the missing IOs are back.";

        if (anyScenario)
        {
            report += "\n\nA SCENARIO is among them: a scenario disabled this way "
                      "stays disabled even once the missing IOs are back, and has to "
                      "be re-enabled explicitly (autoscenario reenable).";
        }

        cError() << "" << disabled.size() << " rule(s) DISABLED because of missing IOs";
        scheduleConfigAlert(report);
    }
}

void Config::SaveConfigRule()
{
    if (Utils::config_frozen())
        return;

    if (!rulesLoaded)
    {
        cError() << "Skipping the save of " << RULES_CONFIG << ": it was not loaded";
        return;
    }

    pugi::xml_document document;
    XmlUtils::appendDeclaration(document);
    pugi::xml_node rulesnode = document.append_child("calaos:rules");
    XmlUtils::setAttribute(rulesnode, "xmlns:calaos", "http://www.calaos.fr");

    for (int i = 0;i < ListeRule::Instance().size();i++)
    {
        Rule *rule = ListeRule::Instance().get_rule(i);
        rule->SaveToXml(rulesnode);
    }

    saveDocument(document, Utils::getConfigFile(RULES_CONFIG));
}

void Config::loadStateCache()
{
    string file = Utils::getCacheFile("iostates.cache");
    cache_states.clear();
    cache_params.clear();

    std::ifstream cacheStream;
    cacheStream.open(file);
    if (!cacheStream.is_open())
    {
        cWarning() <<  "Could not open iostates.cache for read !";
        return;
    }

    //The whole deserialization runs inside the try: a cache that parses as
    //JSON but does not have the expected shape (e.g. non string state values)
    //throws too, and must not abort the daemon at startup.
    try
    {
        Json jcache = Json::parse(cacheStream);
        if (!jcache.is_object())
            throw (invalid_argument(string("Json cache is not an object")));

        Json jstates = jcache["iostates"];
        Json jparams = jcache["ioparams"];

        for (Json::iterator it = jstates.begin(); it != jstates.end(); ++it)
        {
            cache_states[it.key()] = it.value();
        }

        for (Json::iterator it = jparams.begin(); it != jparams.end(); ++it)
        {
            cache_params[it.key()] = Params::fromJson(it.value());
        }
    }
    catch (const std::exception &e)
    {
        cWarning() << "Error parsing " << file << ": "
                   << Utils::jsonErrorForLog(e, cacheStream)
                   << " - starting with an empty state cache";
        cache_states.clear();
        cache_params.clear();
        return;
    }

    cInfo() <<  "States cache read successfully.";
}

void Config::saveStateCache()
{
    string file = Utils::getCacheFile("iostates.cache");
    string tmp = file + ".tmp";

    Json jparams;
    for (auto it = cache_params.begin();it != cache_params.end(); it++)
    {
        jparams[it->first] = it->second.toJson();
    }

    Json jcache({{ "iostates", cache_states },
                 { "ioparams", jparams } });
    std::ofstream fout;
    fout.open(tmp, std::ofstream::out | std::ofstream::trunc);
    if (!fout.is_open())
    {
        cError() <<  "Could not open " << tmp << " for write !";
        return;
    }

    //error_handler_t::replace: a non UTF-8 state value coming from hardware
    //must not make dump() throw and lose the whole cache
    fout << jcache.dump(4, ' ', false, Json::error_handler_t::replace);
    fout.close();
    if (!fout)
    {
        cError() << "Failed to write state cache to " << tmp;
        FileUtils::unlink(tmp);
        return;
    }

    //rename() alone is atomic, an unlink() first would open a window where
    //no cache file exists at all
    if (!FileUtils::rename(tmp, file))
    {
        cError() << "Unable to move " << tmp << " to " << file;
        return;
    }

    cInfo() <<  "State cache file written successfully (" << file << ")";
}

void Config::SaveValueIO(string id, string value, bool save)
{
    cache_states[id] = value;
    if (save)
        saveStateCache();
}

bool Config::ReadValueIO(string id, string &value)
{
    if (cache_states.find(id) != cache_states.end())
    {
        value = cache_states[id];
        return true;
    }

    return false;
}

void Config::SaveValueParams(string id, Params value, bool save)
{
    cache_params[id] = value;
    if (save)
        saveStateCache();
}

bool Config::ReadValueParams(string id, Params &value)
{
    if (cache_params.find(id) != cache_params.end())
    {
        value = cache_params[id];
        return true;
    }

    return false;
}

string Config::BackupFiles()
{
    //getConfigFile() so backups land next to the config files actually in
    //use (getConfigPath() ignores the CALAOS_CONFIG override)
    string backFolder = Utils::getConfigFile("backups");

    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    std::cout.imbue(std::locale("C"));

    std::stringstream ss;
    ss << std::put_time(&tm, "%Y");
    string year = ss.str();

    ss.str({});
    ss << std::put_time(&tm, "%m");
    string month = ss.str();

    ss.str({});
    ss << std::put_time(&tm, "%d-%m-%Y_%H-%M-%S");
    string dateTime = ss.str();

    const string base = backFolder + "/" + year + "/" + month + "/" + dateTime;

    //A second is not fine enough: scripted uploads collide inside one, and
    //reusing the folder would overwrite the copy of the state before the
    //first of them - the only copy that still exists anywhere. Suffix rather
    //than a finer stamp so the usual folder name stays the plain date-time.
    string folder = base;
    for (int i = 2;i <= MAX_BACKUPS_PER_SECOND && FileUtils::exists(folder);i++)
        folder = base + "-" + std::to_string(i);

    if (FileUtils::exists(folder))
    {
        cError() << "Unable to pick a free backup folder next to " << base << ", skipping backup...";
        return string();
    }

    if (!FileUtils::mkpath(folder))
    {
        cError() << "Unable to create backup folder (" << folder << "), skipping backup...";
        return string();
    }

    const std::pair<const char *, bool> sources[] = {
        { IO_CONFIG, ioLoaded }, { RULES_CONFIG, rulesLoaded }, { LOCAL_CONFIG, true }
    };

    bool complete = true;
    for (const auto &[name, wasLoaded]: sources)
    {
        const string source = Utils::getConfigFile(name);
        //local_config.xml holds tokens and io.xml camera passwords
        if (!FileUtils::exists(source) || FileUtils::copyFile(source, folder + "/" + name, true))
            continue;

        //Failing here would forbid the only uploads that can replace this file
        if (!wasLoaded)
        {
            cWarning() << "Unable to backup file " << name << ", it was not loaded either";
            continue;
        }

        cError() << "Unable to backup file " << name;
        complete = false;
    }

    return complete ? folder.substr(backFolder.size() + 1) : string();
}
