/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef CALAOSMODEL_H
#define CALAOSMODEL_H

#include <Utils.h>

#include "CalaosDiscover.h"
#include "CalaosConnection.h"

#include "RoomModel.h"
#include "CameraModel.h"
#include "AudioModel.h"
#include "ScenarioModel.h"

using namespace Utils;

class CalaosModel
{
private:
    CalaosModel();

    CalaosDiscover *discover;
    CalaosConnection *connection;
    string server_address;

    RoomModel *room_model;
    CameraModel *camera_model;
    AudioModel *audio_model;
    ScenarioModel *scenario_model;

    void discover_found(string address);
    void discover_error_login(string address);
    void connection_ok();
    void lost_connection();

    int load_count;
    void load_home_done();
    void load_done();
    bool loaded;

    void StartDiscover();

public:
    static CalaosModel &Instance()
    {
        static CalaosModel inst;
        return inst;
    }

    ~CalaosModel();

    string toString();

    RoomModel *getHome() { return room_model; }
    CameraModel *getCamera() { return camera_model; }
    AudioModel *getAudio() { return audio_model; }
    ScenarioModel *getScenario() { return scenario_model; }

    bool isLoaded() { return loaded; }

    sigc::signal<void, string> login_failed;
    sigc::signal<void> home_loaded;
};

#endif // CALAOSMODEL_H
