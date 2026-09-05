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
//restored over it (or before a later save overwrites it when the server had
//to start with an empty config). Returns the preserved path, empty on error.
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

    if (!FileUtils::copyFile(file, dest))
    {
        cError() << "Unable to copy corrupt " << file << " to " << dest;
        return {};
    }

    cWarning() << "Corrupt " << configName << " preserved at " << dest << " for inspection";
    return dest;
}

//Load file into document. On a parse error, log it, preserve the corrupt
//file, then walk the backups newest to oldest and restore the first one
//that parses. loaded stays false only when no backup is usable (the server
//then starts with an empty config). Never exits: a corrupt config must not
//kill the daemon.
XmlLoadResult loadXmlDocument(pugi::xml_document &document, const string &file, const string &configName)
{
    XmlLoadResult res;

    pugi::xml_parse_result parsed = document.load_file(file.c_str());
    if (parsed)
    {
        res.loaded = true;
        return res;
    }

    res.wasCorrupt = true;
    cError() << "There was a parse error in " << file;
    cError() << parsed.description();
    //pugixml reports a byte offset where the old TinyXML 1 reader (removed in
    //E4.4e) reported a row; the offset is what it has, and it points at the
    //same place in the file.
    cError() << "In file " << file << " At offset " << parsed.offset;

    res.corruptCopy = preserveCorruptFile(file, configName);

    for (const string &backup: findBackupsNewestFirst(configName))
    {
        //Parse the candidate in place first: a corrupt backup must not be
        //copied over the live file
        pugi::xml_document candidate;
        pugi::xml_parse_result candidateParsed = candidate.load_file(backup.c_str());
        if (!candidateParsed)
        {
            cWarning() << "Backup " << backup << " has a parse error too ("
                       << candidateParsed.description() << "), trying an older one";
            continue;
        }

        if (!FileUtils::copyFile(backup, file))
        {
            cError() << "Unable to restore backup " << backup << ", trying an older one";
            continue;
        }

        if (!document.load_file(file.c_str()))
        {
            //candidate parsed above, so this should never happen
            cError() << "Restored " << backup << " but reloading " << file
                     << " failed, trying an older one";
            continue;
        }

        cInfo() << configName << " restored from backup " << backup;
        res.loaded = true;
        res.restoredFrom = backup;
        return res;
    }

    cError() << "No usable backup found for " << configName << ", config not loaded";
    return res;
}

//Human readable report of one recovery, used for the mail/push notification
string configAlertMessage(const string &configName, const XmlLoadResult &res)
{
    string msg = "The configuration file " + configName + " was corrupt and could not be parsed.";

    if (!res.corruptCopy.empty())
        msg += "\nThe corrupt file was preserved at: " + res.corruptCopy;
    else
        msg += "\nThe corrupt file could not be preserved (see server logs).";

    if (!res.restoredFrom.empty())
        msg += "\nThe configuration was automatically restored from backup: " + res.restoredFrom;
    else
        msg += "\nNo usable backup was found: the server started with an EMPTY " + configName + " configuration.";

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

    if (!FileUtils::exists(file))
    {
        std::ofstream conf(file.c_str(), std::ofstream::out);
        conf << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" << std::endl;
        conf << "<calaos:ioconfig xmlns:calaos=\"http://www.calaos.fr\">" << std::endl;
        conf << "<calaos:home></calaos:home>" << std::endl;
        conf << "</calaos:ioconfig>" << std::endl;
        conf.close();
    }

    pugi::xml_document document;

    XmlLoadResult xmlres = loadXmlDocument(document, file, IO_CONFIG);
    if (xmlres.wasCorrupt)
        scheduleConfigAlert(configAlertMessage(IO_CONFIG, xmlres));
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

    cInfo() <<  "Done. ";
}

void Config::SaveConfigIO()
{
    string file = Utils::getConfigFile(IO_CONFIG);
    string tmp = file + "_tmp";

    cInfo() <<  "Saving " << file << "...";

    pugi::xml_document document;
    XmlUtils::appendDeclaration(document);
    pugi::xml_node ionode = document.append_child("calaos:ioconfig");
    XmlUtils::setAttribute(ionode, "xmlns:calaos", "http://www.calaos.fr");
    pugi::xml_node node = ionode.append_child("calaos:home");

    for (int i = 0;i < ListeRoom::Instance().size();i++)
    {
        Room *room = ListeRoom::Instance().get_room(i);
        room->SaveToXml(node);
    }

    if (!document.save_file(tmp.c_str(), XmlUtils::CONFIG_INDENT))
    {
        cError() << "Unable to save " << file << ": writing " << tmp << " failed";
        FileUtils::unlink(tmp); //do not leave a partial tmp file behind
        return;
    }

    //rename() alone is atomic, an unlink() first would open a window where
    //no config file exists at all
    if (::rename(tmp.c_str(), file.c_str()) != 0)
    {
        cError() << "Unable to move " << tmp << " to " << file << ": " << strerror(errno);
        return;
    }

    cInfo() <<  "Done.";
}

void Config::LoadConfigRule()
{
    std::string file = Utils::getConfigFile(RULES_CONFIG);

    if (!FileUtils::exists(file))
    {
        std::ofstream conf(file.c_str(), std::ofstream::out);
        conf << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" << std::endl;
        conf << "<calaos:rules xmlns:calaos=\"http://www.calaos.fr\">" << std::endl;
        conf << "</calaos:rules>" << std::endl;
        conf.close();
    }

    pugi::xml_document document;

    XmlLoadResult xmlres = loadXmlDocument(document, file, RULES_CONFIG);
    if (xmlres.wasCorrupt)
        scheduleConfigAlert(configAlertMessage(RULES_CONFIG, xmlres));
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
    string file = Utils::getConfigFile(RULES_CONFIG);
    string tmp = file + "_tmp";

    cInfo() <<  "Saving " << file << "...";

    pugi::xml_document document;
    XmlUtils::appendDeclaration(document);
    pugi::xml_node rulesnode = document.append_child("calaos:rules");
    XmlUtils::setAttribute(rulesnode, "xmlns:calaos", "http://www.calaos.fr");

    for (int i = 0;i < ListeRule::Instance().size();i++)
    {
        Rule *rule = ListeRule::Instance().get_rule(i);
        rule->SaveToXml(rulesnode);
    }

    if (!document.save_file(tmp.c_str(), XmlUtils::CONFIG_INDENT))
    {
        cError() << "Unable to save " << file << ": writing " << tmp << " failed";
        FileUtils::unlink(tmp); //do not leave a partial tmp file behind
        return;
    }

    //rename() alone is atomic, an unlink() first would open a window where
    //no config file exists at all
    if (::rename(tmp.c_str(), file.c_str()) != 0)
    {
        cError() << "Unable to move " << tmp << " to " << file << ": " << strerror(errno);
        return;
    }

    cInfo() <<  "Done.";
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

void Config::BackupFiles()
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
        return;
    }

    if (!FileUtils::mkpath(folder))
    {
        cError() << "Unable to create backup folder (" << folder << "), skipping backup...";
        return;
    }

    if (!FileUtils::copyFile(Utils::getConfigFile(IO_CONFIG), folder + "/" + IO_CONFIG))
    {
        cError() << "Unable to backup file " << IO_CONFIG;
    }

    if (!FileUtils::copyFile(Utils::getConfigFile(RULES_CONFIG), folder + "/" + RULES_CONFIG))
    {
        cError() << "Unable to backup file " << RULES_CONFIG;
    }

    if (!FileUtils::copyFile(Utils::getConfigFile(LOCAL_CONFIG), folder + "/" + LOCAL_CONFIG))
    {
        cError() << "Unable to backup file " << LOCAL_CONFIG;
    }
}
