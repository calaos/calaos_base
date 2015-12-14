/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "Utils.h"

#include <Ecore_File.h>
#include <tcpsocket.h>

using namespace Utils;

bool Utils::file_copy(std::string source, std::string dest)
{
    FILE *f1, *f2;
    char buf[16384];
    char realpath1[PATH_MAX];
    char realpath2[PATH_MAX];
    size_t num;
    size_t res;

    if (!realpath(source.c_str(), realpath1)) return false;
    if (realpath(dest.c_str(), realpath2) && !strcmp(realpath1, realpath2)) return false;

    f1 = fopen(source.c_str(), "rb");
    if (!f1) return false;
    f2 = fopen(dest.c_str(), "wb");
    if (!f2)
    {
        fclose(f1);
        return false;
    }

    while ((num = fread(buf, 1, sizeof(buf), f1)) > 0)
    {
        res = fwrite(buf, 1, num, f2);
        if (res <= 0) cCritical() <<  "Failed to fwrite !";
    }

    fclose(f1);
    fclose(f2);

    return true;
}

bool Utils::fileExists(std::string filename)
{
    struct stat buf;
    if (stat(filename.c_str(), &buf) != -1)
        return true;

    return false;
}

string Utils::url_encode(string str)
{
    string ret = "";
    char tmp[10];

    for (uint i = 0;i < str.length();i++)
    {
        if ((str[i] >= 'a' && str[i] <= 'z') ||
            (str[i] >= 'A' && str[i] <= 'Z') ||
            (str[i] >= '0' && str[i] <= '9') ||
            str[i] == '_')
            ret += str[i];
        else
        {
            memset(tmp, '\0', 5);
            sprintf(tmp, "%%%02X", (unsigned char)str[i]);
            ret += tmp;
        }
    }

    return ret;
}

string Utils::url_decode(string str)
{
    string ret = "";

    for (uint i = 0;i < str.length();i++)
    {
        if (str[i] == '%' && isxdigit((int)str[i + 1]) && isxdigit((int)str[i + 2]))
        {
            ret += (char) htoi((char *)str.c_str() + i + 1);
            i += 2;
        }
        else
            ret += str[i];
    }

    return ret;
}

std::string Utils::url_decode2(std::string str)
{
    return url_decode(url_decode(str));
}

std::string Utils::Base64_decode(std::string &str)
{
    std::string ret;
    ret = base64_decode(str);

    return ret;
}

std::string Utils::Base64_decode_data(std::string &str)
{
    string ret = base64_decode(str);

    return ret;
}

std::string Utils::Base64_encode(std::string &str)
{
    std::string ret;
    ret = base64_encode(reinterpret_cast<const unsigned char*>(str.c_str()), str.length());

    return ret;
}

std::string Utils::Base64_encode(void *data, int size)
{
    std::string ret;
    ret = base64_encode(reinterpret_cast<const unsigned char*>(data), size);

    return ret;
}

int Utils::htoi(char *s)
{
    int value;
    int c;

    c = ((unsigned char *)s)[0];
    if (isupper(c))
        c = tolower(c);
    value = (c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10) * 16;

    c = ((unsigned char *)s)[1];
    if (isupper(c))
        c = tolower(c);
    value += c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10;

    return (value);
}

string Utils::time2string(long s, long ms)
{
    double sec = s;
    int hours = (int)(sec / 3600.0);
    sec -= hours * 3600;
    int min = (int)(sec / 60.0);
    sec -= min * 60;

    stringstream str;

    if (hours == 1)
        str << hours << " " << "heure" << " ";
    if (hours > 1)
        str << hours << " " << "heures" << " ";
    if (min == 1)
        str << min << " " << "minute" << " ";
    if (min > 1)
        str << min << " " << "minutes" << " ";
    if (sec == 1)
    {
        str << sec << " " << "seconde";
        if (ms > 0) str << " ";
    }
    if (sec > 1)
    {
        str << sec << " " << "secondes";
        if (ms > 0) str << " ";
    }
    if (ms > 0)
        str << ms << " " << "ms";

    return str.str();
}

string Utils::time2string_digit(long s, long ms)
{
    double sec = s;
    int hours = (int)(sec / 3600.0);
    sec -= hours * 3600;
    int min = (int)(sec / 60.0);
    sec -= min * 60;

    stringstream str;

    if (hours > 0)
        str << setw(2) << setfill('0') << hours << ":";

    str << setw(2) << setfill('0') << min << ":";
    str << setw(2) << setfill('0') << sec;

    if (ms > 0)
        str << "." << setw(4) << setfill('0') << ms;

    return str.str();
}

void Utils::split(const string &str, vector<string> &tokens, const string &delimiters, int max /* = 0 */)
{
    // Skip delimiters at beginning.
    string::size_type lastPos = str.find_first_not_of(delimiters, 0);
    // Find first "non-delimiter".
    string::size_type pos     = str.find_first_of(delimiters, lastPos);

    int counter = 0;

    while (string::npos != pos || string::npos != lastPos)
    {
        if (counter + 1 >= max && max > 0)
        {
            tokens.push_back(str.substr(lastPos, string::npos));
            break;
        }

        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos));
        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of(delimiters, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delimiters, lastPos);

        counter++;
    }

    while (tokens.size() < (uint)max) tokens.push_back("");
}

void Utils::remove_tag(string &html, const string begin_tag, const string end_tag)
{
    string::size_type start_pos = html.find(begin_tag, 0);

    while (start_pos != string::npos)
    {
        string::size_type end_pos = html.find(end_tag, start_pos);
        if (end_pos == string::npos)
        {
            break;
        }
        else
        {
            end_pos += end_tag.length();
            html.erase(start_pos, end_pos - start_pos);
            start_pos = html.find(begin_tag, 0);
        }
    }
}

void Utils::replace_str(string &source, const string searchstr, const string replacestr)
{
    string::size_type pos = 0;
    while((pos = source.find(searchstr, pos)) != string::npos)
    {
        source.erase(pos, searchstr.length());
        source.insert(pos, replacestr);
        pos += replacestr.length();
    }
}

void Utils::trim_right(std::string &source, const std::string &t)
{
    source.erase(source.find_last_not_of(t) + 1);
}

void Utils::trim_left(std::string &source, const std::string &t)
{
    source.erase(0, source.find_first_not_of(t));
}

double Utils::roundValue(double value, int precision)
{
    if (value == 0.)
        return value;

    int ex = floor(log10(abs(value))) - precision + 1;
    double div = pow(10, ex);

    return floor(value / div + 0.5) * div;
}

bool Utils::strContains(const string &str, const string &needle, CaseSensitivity cs)
{
    if (needle.empty())
        return true;

    if (needle.length() > str.length())
        return false;

    if (cs == Utils::CaseSensitive)
        return str.find(needle) != std::string::npos;

    string s = str;
    return str_to_lower(s).find(str_to_lower(needle)) != std::string::npos;
}

bool Utils::strStartsWith(const string &str, const string &needle, Utils::CaseSensitivity cs)
{
    if (needle.empty())
        return true;

    if (needle.length() > str.length())
        return false;

    if (cs == Utils::CaseSensitive)
        return memcmp(str.c_str(), needle.c_str(), needle.length()) == 0;

    for (uint i = 0;i < needle.length();i++)
    {
        if (tolower(str[i]) != tolower(needle[i]))
            return false;
    }

    return true;
}

void Utils::parseParamsItemList(string l, vector<Params> &res, int start_at)
{
    vector<string> tokens;
    split(l, tokens);
    Params item;

    for (unsigned int i = start_at;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;
        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (item.Exists(tk[0]))
        {
            res.push_back(item);
            item.clear();
        }

        item.Add(tk[0], tk[1]);
    }

    if (item.size() > 0)
        res.push_back(item);
}

int CURL_write_callback(void *buffer, size_t size, size_t nmemb, void *stream)
{
    File_CURL *out = (File_CURL *)stream;

    if(out && !out->fp)
    {
        /* open file for writing */
        out->fp = fopen(out->fname, "wb+");
        if(!out->fp)
            return -1; /* failure, can't open file to write */
    }

    return fwrite(buffer, size, nmemb, out->fp);
}

int CURL_writebuf_callback(void *buffer, size_t size, size_t nmemb, void *stream)
{
    Buffer_CURL *cbuffer = (Buffer_CURL *)stream;

    cbuffer->buffer = realloc(cbuffer->buffer, cbuffer->bufsize + size * nmemb);
    memcpy((char *)cbuffer->buffer + cbuffer->bufsize, buffer, size * nmemb);

    cbuffer->bufsize = cbuffer->bufsize + size * nmemb;

    return nmemb;
}

static bool einaLogShuttingDown = false;
static string default_domain;
static std::unordered_map<std::string, EinaLog *> logger_hash;
static EinaLog defaultCoutLogger(true);

EinaLog *Utils::einaLogger(const char *domain)
{
    if (einaLogShuttingDown)
        return &defaultCoutLogger;

    string d = default_domain;
    
    if (domain)
      d = domain;

    // Prefix all domains with calaos_. We can so filter log domains
    // with export EINA_LOG_LEVELS_GLOB="calaos*:5"

    //add domain prefix to ease eina logs filtering
    if (!Utils::strStartsWith(d, "calaos_"))
        d.insert(0, "calaos_");

    EinaLog *logger = nullptr;
    auto it = logger_hash.find(d);
    if (it == logger_hash.end())
    {
        logger = new EinaLog(d);
        logger_hash[d] = logger;
    }
    else
        logger = it->second;

    return logger;
}

void Utils::InitEinaLog(const char *d)
{
    //We are actually shutting down everything, do not allocate memory
    if (einaLogShuttingDown)
        return;

    //try to get env variable EINA_LOG_LEVELS_GLOB
    //and if not defined set it to print INF messages by default
    if (!getenv("EINA_LOG_LEVELS_GLOB"))
        setenv("EINA_LOG_LEVELS_GLOB", "calaos*:3", true);

    eina_init();
    default_domain = d;
    logger_hash[default_domain] = new EinaLog(default_domain);
}

void Utils::FreeEinaLogs()
{
    for (auto &kv: logger_hash)
    {
        delete kv.second;
    }
    logger_hash.clear();

    eina_shutdown();

    einaLogShuttingDown = true;
}

static string _configBase;
static string _cacheBase;

string Utils::getConfigFile(const char *configType)
{
    if (_configBase.empty())
    {
        string home;
        if (getenv("HOME"))
        {
            home = getenv("HOME");
        }
        else
        {
            struct passwd *pw = getpwuid(getuid());
            home = pw->pw_dir;
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
            if (ecore_file_exists(conf.c_str()))
            {
                _configBase = *it;
                break;
            }
        }

        if (_configBase.empty())
        {
            //no config dir found, create $HOME/.config/calaos
            ecore_file_mkdir(string(home + "/.config").c_str());
            ecore_file_mkdir(string(home + "/.config/calaos").c_str());
            _configBase = home + "/" + HOME_CONFIG_PATH;
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
            struct passwd *pw = getpwuid(getuid());
            home = pw->pw_dir;
        }

        //force the creation of .cache/calaos
        ecore_file_mkdir(string(home + "/.cache").c_str());
        ecore_file_mkdir(string(home + "/.cache/calaos").c_str());

        _cacheBase = home + "/.cache/calaos";
    }

    return _cacheBase + "/" + cacheFile;
}

void Utils::initConfigOptions(char *configdir, char *cachedir, bool quiet)
{
    if (configdir) _configBase = configdir;
    if (cachedir) _cacheBase = cachedir;

    string file = getConfigFile(LOCAL_CONFIG);

    if (!quiet)
    {
        cInfo() << "Using config path: " << getConfigFile("");
        cInfo() << "Using cache path: " << getCacheFile("");
    }

    if (!ecore_file_can_write(getConfigFile("").c_str()))
        throw (runtime_error("config path is not writable"));
    if (!ecore_file_can_write(getCacheFile("").c_str()))
        throw (runtime_error("cache path is not writable"));

    if (!fileExists(file))
    {
        //create a defaut config
        std::ofstream conf(file.c_str(), std::ofstream::out);
        conf << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" << endl;
        conf << "<calaos:config xmlns:calaos=\"http://www.calaos.fr\">" << endl;
        conf << "<calaos:option name=\"fw_version\" value=\"0\" />" << endl;
        conf << "</calaos:config>" << endl;
        conf.close();

        set_config_option("fw_target", "calaos_tss");
        set_config_option("fw_version", "0");
        set_config_option("show_cursor", "true");
        set_config_option("use_ntp", "true");
        set_config_option("device_type", "calaos_server");
        set_config_option("dpms_enable", "false");
        set_config_option("smtp_server", "");
        set_config_option("cn_user", "user");
        set_config_option("cn_pass", "pass");
        set_config_option("longitude", "2.322235");
        set_config_option("latitude", "48.864715");

        if (!quiet)
            cout << "WARNING: no local_config.xml found, generating default config with username: \"user\" and password: \"pass\"" << endl;
    }
}

string Utils::get_config_option(string _key)
{
    string value = "";
    TiXmlDocument document(getConfigFile(LOCAL_CONFIG).c_str());

    if (!document.LoadFile())
    {
        cError() <<  "There was an exception in XML parsing.";
        cError() <<  "Parse error: " << document.ErrorDesc();
        cError() <<  " At line " << document.ErrorRow();

        return value;
    }

    TiXmlHandle docHandle(&document);

    TiXmlElement *key_node = docHandle.FirstChildElement("calaos:config").FirstChildElement().ToElement();
    if (key_node)
    {
        for(; key_node; key_node = key_node->NextSiblingElement())
        {
            if (key_node->ValueStr() == "calaos:option" &&
                key_node->Attribute("name") &&
                key_node->Attribute("name") == _key &&
                key_node->Attribute("value"))
            {
                value = key_node->Attribute("value");
                break;
            }
        }
    }

    return value;
}

bool Utils::set_config_option(string key, string value)
{
    TiXmlDocument document(getConfigFile(LOCAL_CONFIG).c_str());

    if (!document.LoadFile())
    {
        cError() <<  "There was an exception in XML parsing.";
        cError() <<  "Parse error: " << document.ErrorDesc();
        cError() <<  "In file " << getConfigFile(LOCAL_CONFIG) << " At line " << document.ErrorRow();

        return false;
    }

    TiXmlHandle docHandle(&document);
    bool found = false;

    TiXmlElement *key_node = docHandle.FirstChildElement("calaos:config").FirstChildElement().ToElement();
    if (key_node)
    {
        for(; key_node; key_node = key_node->NextSiblingElement())
        {
            if (key_node->ValueStr() == "calaos:option" &&
                key_node->Attribute("name") &&
                key_node->Attribute("name") == key)
            {
                key_node->SetAttribute("value", value);
                found = true;
                break;
            }
        }

        //the option was not found, we create it
        if (!found)
        {
            TiXmlElement *element = new TiXmlElement("calaos:option");
            element->SetAttribute("name", key);
            element->SetAttribute("value", value);
            docHandle.FirstChild("calaos:config").ToElement()->LinkEndChild(element);
        }

        document.SaveFile();
    }

    return true;
}

bool Utils::del_config_option(string key)
{
    TiXmlDocument document(getConfigFile(LOCAL_CONFIG).c_str());

    if (!document.LoadFile())
    {
        cError() <<  "There was an exception in XML parsing.";
        cError() <<  "Parse error: " << document.ErrorDesc();
        cError() <<  "In file " << getConfigFile(LOCAL_CONFIG) << " At line " << document.ErrorRow();

        return false;
    }

    TiXmlHandle docHandle(&document);

    TiXmlElement *key_node = docHandle.FirstChildElement("calaos:config").FirstChildElement().ToElement();
    if (key_node)
    {
        for(; key_node; key_node = key_node->NextSiblingElement())
        {
            if (key_node->ValueStr() == "calaos:option" &&
                key_node->Attribute("name") &&
                key_node->Attribute("name") == key)
            {
                docHandle.FirstChild("calaos:config").Element()->RemoveChild(key_node);
                break;
            }
        }

        document.SaveFile();
    }

    return true;
}

bool Utils::get_config_options(Params &options)
{
    TiXmlDocument document(getConfigFile(LOCAL_CONFIG).c_str());

    if (!document.LoadFile())
    {
        cError() <<  "There was an exception in XML parsing.";
        cError() <<  "Parse error: " << document.ErrorDesc();
        cError() <<  "In file " << getConfigFile(LOCAL_CONFIG) << " At line " << document.ErrorRow();

        return false;
    }

    TiXmlHandle docHandle(&document);

    TiXmlElement *key_node = docHandle.FirstChildElement("calaos:config").FirstChildElement().ToElement();
    if (key_node)
    {
        for(; key_node; key_node = key_node->NextSiblingElement())
        {
            if (key_node->ValueStr() == "calaos:option" &&
                key_node->Attribute("name") &&
                key_node->Attribute("value"))
            {
                options.Add(key_node->Attribute("name"), key_node->Attribute("value"));
            }
        }
    }

    return true;
}

void Utils::Watchdog(std::string fname)
{
    std::string file = "/tmp/wd_" + fname;

    std::ifstream f(file.c_str());

    if (f.fail())
    {
        std::ofstream of(file.c_str());

        of << "wd_" << fname;
        of.close();
    }

    f.close();
}

bool Utils::argvOptionCheck(char **begin, char **end, const std::string &option)
{
    return std::find(begin, end, option) != end;
}

char *Utils::argvOptionParam(char **begin, char **end, const std::string &option)
{
    char ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end)
        return *itr;
    return NULL;
}

#define HWETH1 "eth0"
#define HWETH2 "eth1"

string Utils::getHardwareID()
{
    static string hwID;

    if (!hwID.empty())
        return hwID;

    stringstream ss;

    unsigned char hwmac1[6], hwmac2[6];
    if (!TCPSocket::GetMacAddr(HWETH1, hwmac1))
        return "";

    if (!TCPSocket::GetMacAddr(HWETH2, hwmac2))
        return "";

    for (int i = 0;i < 6;i++)
    {
        ss << setiosflags(ios_base::uppercase) << setfill('0')
           << setw(2) << hex << (unsigned int)hwmac1[i];
        ss << setiosflags(ios_base::uppercase) << setfill('0')
           << setw(2) << hex << (unsigned int)hwmac2[i];
    }
    hwID = ss.str();

    return hwID;
}

string Utils::getFileContent(const char *filename)
{
    ifstream ifs(filename, ios::in | ios::binary | ios::ate);
    if (!ifs) return "";

    ifstream::pos_type filesize = ifs.tellg();
    ifs.seekg(0, ios::beg);

    vector<char> buff(filesize);
    ifs.read(&buff[0], filesize);

    return string(&buff[0], filesize);
}

string Utils::getFileContentBase64(const char *filename)
{
    ifstream ifs(filename, ios::in | ios::binary | ios::ate);
    if (!ifs) return "";

    ifstream::pos_type filesize = ifs.tellg();
    ifs.seekg(0, ios::beg);

    vector<char> buff(filesize);
    ifs.read(&buff[0], filesize);

    return Utils::Base64_encode(&buff[0], filesize);
}

unsigned int Utils::getUptime()
{
#if defined(__linux__) || defined(__linux) || defined(linux)
    struct sysinfo info;
    if (sysinfo(&info) != 0)
        return -1;
    return info.uptime;
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &boottime, &len, NULL, 0) < 0)
        return -1;
    return time(NULL) - boottime.tv_sec;
#elif (defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)) && defined(CLOCK_UPTIME)
    struct timespec ts;
    if (clock_gettime(CLOCK_UPTIME, &ts) != 0)
        return -1;
    return ts.tv_sec;
#else
    return 0;
#endif
}

string Utils::createRandomUuid()
{
    struct timeval t1;
    gettimeofday(&t1, NULL);
    srand(t1.tv_usec * t1.tv_sec); //use a more accurate seed for srand.
    stringstream ssUuid;

    ssUuid << std::hex << std::setfill('0') ;
    ssUuid << std::setw(4) << (rand() & 0xffff) << std::setw(4) << (rand() & 0xffff) << "-";
    ssUuid << std::setw(4) << (rand() & 0xffff) << "-";
    ssUuid << std::setw(4) << (rand() & 0xffff) << "-";
    ssUuid << std::setw(4) << (rand() & 0xffff) << "-";
    ssUuid << std::setw(4) << (rand() & 0xffff) << std::setw(4) << (rand() & 0xffff)<< std::setw(4) << (rand() & 0xffff);

    return ssUuid.str();
}

string Utils::str_to_lower(std::string s)
{
     std::transform(s.begin(), s.end(), s.begin(), Utils::to_lower());
     return s;
}

string Utils::str_to_upper(std::string s)
{
     std::transform(s.begin(), s.end(), s.begin(), Utils::to_upper());
     return s;
}

string Utils::trim(const string &str)
{
    if (str.size() == 0)
        return str;
    if (!::isspace(str[0]) && !::isspace(str[str.length() - 1]))
        return str;

    uint start = 0;
    uint end = str.length() - 1;
    uint len;

    while (start <= end && ::isspace(str[start]))
        start++;

    if (start <= end)
    {
        while (end && ::isspace(str[end]))
            end--;
    }

    len = end - start + 1;
    if (len <= 0)
        return string();

    return string(str, start, len);
}
