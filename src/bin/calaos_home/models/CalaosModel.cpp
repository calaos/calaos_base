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
#include "CalaosModel.h"

CalaosModel::CalaosModel():
    discover(NULL),
    connection(NULL),
    room_model(NULL),
    camera_model(NULL),
    audio_model(NULL),
    scenario_model(NULL),
    loaded(false)
{
    discover = new CalaosDiscover();
    discover->server_found.connect(sigc::mem_fun(*this, &CalaosModel::discover_found));
    discover->login_error.connect(sigc::mem_fun(*this, &CalaosModel::discover_error_login));
}

CalaosModel::~CalaosModel()
{
    DELETE_NULL(room_model);
    DELETE_NULL(camera_model);
    DELETE_NULL(audio_model);
    DELETE_NULL(scenario_model);

    DELETE_NULL(discover);
    DELETE_NULL(connection);
}

void CalaosModel::discover_found(std::string address)
{
    server_address = address;

    EcoreTimer::singleShot(0.0, [=]()
    {
        DELETE_NULL(discover);
    });

    cInfoDom("network") << "CalaosModel: found server: " << server_address;

    connection = new CalaosConnection(server_address);
    connection->connection_ok.connect(sigc::mem_fun(*this, &CalaosModel::connection_ok));
    connection->lost_connection.connect(sigc::mem_fun(*this, &CalaosModel::lost_connection));

    load_count = 0;

    DELETE_NULL(room_model);
    room_model = new RoomModel(connection);
    room_model->load_done.connect(sigc::mem_fun(*this, &CalaosModel::load_home_done));

    DELETE_NULL(camera_model);
    camera_model = new CameraModel(connection);
    camera_model->load_done.connect(sigc::mem_fun(*this, &CalaosModel::load_done));

    DELETE_NULL(audio_model);
    audio_model = new AudioModel(connection);
    audio_model->load_done.connect(sigc::mem_fun(*this, &CalaosModel::load_done));

    DELETE_NULL(scenario_model);
    scenario_model = new ScenarioModel(connection);
    scenario_model->load_done.connect(sigc::mem_fun(*this, &CalaosModel::load_done));
}

void CalaosModel::discover_error_login(std::string address)
{
    /* this has to be redispatched to the gui and ask user for username/password */
    server_address = address;

    EcoreTimer::singleShot(0.0, [=]()
    {
        DELETE_NULL(discover);
    });

    cErrorDom("network") << "CalaosModel: Failed to login to server: " << server_address;

    login_failed.emit(server_address);
}

void CalaosModel::connection_ok()
{
    cInfoDom("network") << "CalaosModel: Connection success, loading home";

    //First load home and wait it finishes loading all IO (they are needed by AudioModel/CameraModel)
    room_model->load();
}

void CalaosModel::lost_connection()
{
    cErrorDom("network") << "CalaosModel: Lost Connection !";

    DELETE_NULL(room_model);
    DELETE_NULL(camera_model);
    DELETE_NULL(audio_model);

    EcoreTimer::singleShot(0.0, [=]()
    {
        DELETE_NULL(discover);
        DELETE_NULL(connection);

        discover = new CalaosDiscover();
        discover->server_found.connect(sigc::mem_fun(*this, &CalaosModel::discover_found));
        discover->login_error.connect(sigc::mem_fun(*this, &CalaosModel::discover_error_login));
    });

    loaded = false;
}

void CalaosModel::load_done()
{
    load_count--;

    if (load_count <= 0)
    {
        cInfoDom("network") << "CalaosModel: Home loaded";

        home_loaded.emit();

        loaded = true;
    }
}

void CalaosModel::load_home_done()
{
    load_count += 3;
    camera_model->load(room_model->getJsonHome());
    audio_model->load(room_model->getJsonHome());
    scenario_model->load(room_model->getJsonHome());
}

std::string CalaosModel::toString()
{
    std::stringstream s;

    if (room_model)
    {
        for (std::list<Room *>::iterator i = room_model->rooms.begin();i != room_model->rooms.end();i++)
        {
            Room *room = *i;
            s << "[" << room->type << " - " << room->name << "] - " << room->hits << std::endl;

            for (std::list<IOBase *>::iterator j = room->ios.begin();j != room->ios.end();j++)
            {
                IOBase *io = *j;

                s << "\t" << io->params["type"] << " - " << io->params["id"] << " - " << io->params["name"] << " - " << io->params["hits"] << std::endl;
            }
        }
    }

    if (camera_model)
    {
        for (std::list<Camera *>::iterator i = camera_model->cameras.begin();i != camera_model->cameras.end();i++)
        {
            Camera *camera = *i;

            s << "[Camera]" << std::endl << "\t";
            s << camera->params["name"] << " - " << camera->params["jpeg_url"] << std::endl;
        }
    }

    if (audio_model)
    {
        for (std::list<AudioPlayer *>::iterator i = audio_model->players.begin();i != audio_model->players.end();i++)
        {
            AudioPlayer *audio = *i;

            s << "[Audio]" << std::endl << "\t";
            s << audio->params["name"] << " - " << audio->params["id"] << std::endl;
        }
    }

    if (scenario_model)
    {
        for (std::list<Scenario *>::iterator i = scenario_model->scenarios.begin();i != scenario_model->scenarios.end();i++)
        {
            Scenario *sc = *i;

            s << "[Scenario]" << std::endl << "\t";
            s << sc->ioScenario->params["name"] << " - " << sc->ioScenario->params["id"] << std::endl;
        }
    }

    return s.str();
}
