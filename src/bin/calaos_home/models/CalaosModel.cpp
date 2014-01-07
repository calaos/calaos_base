/******************************************************************************
**  Copyright (c) 2006-2011, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Calaos; if not, write to the Free Software
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

void CalaosModel::discover_found(string address)
{
        server_address = address;

        DELETE_NULL(discover)

        Utils::logger("network") << Priority::INFO << "CalaosModel: found server: " << server_address << log4cpp::eol;

        connection = new CalaosConnection(server_address);
        connection->connection_ok.connect(sigc::mem_fun(*this, &CalaosModel::connection_ok));
        connection->lost_connection.connect(sigc::mem_fun(*this, &CalaosModel::lost_connection));

        load_count = 0;

        DELETE_NULL(room_model)
        room_model = new RoomModel(connection);
        room_model->load_done.connect(sigc::mem_fun(*this, &CalaosModel::load_home_done));

        DELETE_NULL(camera_model)
        camera_model = new CameraModel(connection);
        camera_model->load_done.connect(sigc::mem_fun(*this, &CalaosModel::load_done));

        DELETE_NULL(audio_model)
        audio_model = new AudioModel(connection);
        audio_model->load_done.connect(sigc::mem_fun(*this, &CalaosModel::load_done));

        DELETE_NULL(scenario_model)
        scenario_model = new ScenarioModel(connection);
        scenario_model->load_done.connect(sigc::mem_fun(*this, &CalaosModel::load_done));
}

void CalaosModel::discover_error_login(string address)
{
        /* this has to be redispatched to the gui and ask user for username/password */
        server_address = address;

        DELETE_NULL(discover)

        Utils::logger("network") << Priority::ERROR << "CalaosModel: Failed to login to server: " << server_address << log4cpp::eol;

        login_failed.emit(server_address);
}

void CalaosModel::connection_ok()
{
        Utils::logger("network") << Priority::INFO << "CalaosModel: Connection success, loading home" << log4cpp::eol;

        //First load home and wait it finishes loading all IO (they are needed by AudioModel/CameraModel)
        room_model->load();
}

void CalaosModel::lost_connection()
{
        Utils::logger("network") << Priority::ERROR << "CalaosModel: Lost Connection !" << log4cpp::eol;

        DELETE_NULL(room_model)
        DELETE_NULL(camera_model)
        DELETE_NULL(audio_model)

        DELETE_NULL(discover);
        DELETE_NULL(connection);
        discover = new CalaosDiscover();
        discover->server_found.connect(sigc::mem_fun(*this, &CalaosModel::discover_found));
        discover->login_error.connect(sigc::mem_fun(*this, &CalaosModel::discover_error_login));

        loaded = false;
}

void CalaosModel::load_done()
{
        load_count--;

        if (load_count <= 0)
        {
                Utils::logger("network") << Priority::INFO << "CalaosModel: Home loaded" << log4cpp::eol;

                home_loaded.emit();

                loaded = true;
        }
}

void CalaosModel::load_home_done()
{
        camera_model->load();
        load_count++;
        audio_model->load();
        load_count++;
        scenario_model->load();
        load_count++;
}

string CalaosModel::toString()
{
        stringstream s;

        if (room_model)
        {
                for (list<Room *>::iterator i = room_model->rooms.begin();i != room_model->rooms.end();i++)
                {
                        Room *room = *i;
                        s << "[" << room->type << " - " << room->name << "] - " << room->hits << endl;

                        for (list<IOBase *>::iterator j = room->ios.begin();j != room->ios.end();j++)
                        {
                                IOBase *io = *j;

                                s << "\t" << io->params["type"] << " - " << io->params["id"] << " - " << io->params["name"] << " - " << io->params["hits"] << endl;
                        }
                }
        }

        if (camera_model)
        {
                for (list<Camera *>::iterator i = camera_model->cameras.begin();i != camera_model->cameras.end();i++)
                {
                        Camera *camera = *i;

                        s << "[Camera]" << endl << "\t";
                        s << camera->params["name"] << " - " << camera->params["jpeg_url"] << endl;
                }
        }

        if (audio_model)
        {
                for (list<AudioPlayer *>::iterator i = audio_model->players.begin();i != audio_model->players.end();i++)
                {
                        AudioPlayer *audio = *i;

                        s << "[Audio]" << endl << "\t";
                        s << audio->params["name"] << " - " << audio->params["id"] << endl;
                }
        }

        if (scenario_model)
        {
                for (list<Scenario *>::iterator i = scenario_model->scenarios.begin();i != scenario_model->scenarios.end();i++)
                {
                        Scenario *sc = *i;

                        s << "[Scenario]" << endl << "\t";
                        s << sc->ioScenario->params["name"] << " - " << sc->ioScenario->params["id"] << endl;
                }
        }

        return s.str();
}
