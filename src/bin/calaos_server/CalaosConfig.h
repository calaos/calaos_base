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
#ifndef S_CONFIG_H
#define S_CONFIG_H

#include "Calaos.h"
#include "Timer.h"
#include "Room.h"
#include "ListeRoom.h"
#include "IOFactory.h"
#include "ListeRule.h"
#include "RulesFactory.h"

namespace Calaos
{

class Config
{
private:
    Config();

    unordered_map<string, string> cache_states;
    unordered_map<string, Params> cache_params;
    std::shared_ptr<Timer> saveCacheTimer;

    //Corrupt-config alerts: messages queued at load time by LoadConfigIO()/
    //LoadConfigRule() when a corrupt file was recovered (or not), sent later
    //by mail+push (NotifManager) once the server is up and the event loop
    //runs (Timer::singleShot).
    vector<string> configAlerts;
    bool configAlertScheduled = false;
    void scheduleConfigAlert(const string &message);
    void sendConfigAlerts();

public:
    static Config &Instance()
    {
        static Config inst;
        return inst;
    }
    ~Config();

    void LoadConfigIO();
    void LoadConfigRule();

    void SaveConfigIO();
    void SaveConfigRule();

    //Reload/flush the IO state cache from/to disk. loadStateCache() is
    //called by the constructor, saveStateCache() by a 60s timer, by the
    //destructor and by SaveValue*(..., save = true). Public so the shutdown
    //path can force a flush (and for tests).
    void loadStateCache();
    void saveStateCache();

    void SaveValueIO(string id, string value, bool save = true);
    void SaveValueParams(string id, Params value, bool save = true);
    bool ReadValueIO(string id, string &value);
    bool ReadValueParams(string id, Params &value);

    void BackupFiles();

    /* Queue a message on the same deferred mail/push channel LoadConfigIO()/
     * LoadConfigRule() use, for a problem detected outside them. E4.6h needs
     * it for the scenarios a configuration upload took away, which only the
     * startup that follows the upload can see.
     */
    void reportConfigAlert(const string &message) { scheduleConfigAlert(message); }

    //Corruption alert messages queued for the deferred mail/push
    //notification (visible for tests; cleared once sent)
    const vector<string> &getConfigAlerts() const { return configAlerts; }
};

}
#endif
