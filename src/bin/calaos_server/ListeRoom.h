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
#ifndef S_ListeRoom_H
#define S_ListeRoom_H

#include <Calaos.h>
#include <Room.h>
#include <ListeRule.h>
#include <IPCam.h>
#include <CamInput.h>
#include <CamManager.h>
#include <CamOutput.h>
#include <AudioInput.h>
#include <AudioOutput.h>
#include <AudioPlayer.h>
#include <AudioManager.h>
#include <IntValue.h>
#include <InPlageHoraire.h>
#include <InputTimer.h>
#include <Scenario.h>
#include <IOFactory.h>
#include <Eina.h>

namespace Calaos
{

typedef enum { PLAGE_HORAIRE, CONSIGNE, ACTIVE } ChauffType;

class ListeRoom
{
protected:
    std::vector<Room *> rooms;
    Eina_Hash *input_table;
    Eina_Hash *output_table;

    list<Scenario *> auto_scenario_cache;

    ListeRoom();

public:
    //singleton
    static ListeRoom &Instance();

    ~ListeRoom();

    void Add(Room *p);
    void Remove(int i);
    Room *get_room(int i);
    Room *operator[] (int i) const;

    Input *get_input(std::string i);
    Output *get_output(std::string i);

    Input *get_input(int i);
    Output *get_output(int i);
    bool delete_input(Input *in, bool del = true);
    bool delete_output(Output *out, bool del = true);

    int get_nb_input();
    int get_nb_output();

    int size() { return rooms.size(); }

    Input *get_chauffage_var(std::string &chauff_id, ChauffType type);

    //Auto scenarios

    void addScenarioCache(Scenario *sc);
    void delScenarioCache(Scenario *sc);
    list<Scenario *> getAutoScenarios();
    void checkAutoScenario();

    Room * searchRoomByNameAndType(string name,string type);

    Room *getRoomByInput(Input *o);
    Room *getRoomByOutput(Output *o);

    bool deleteIO(Input *input, bool modify = false);
    bool deleteIO(Output *output, bool modify = false);

    Input* createInput(Params param, Room *room);
    Output* createOutput(Params param, Room *room);

    void addInputHash(Input *input);
    void delInputHash(Input *input);
    void addOutputHash(Output *output);
    void delOutputHash(Output *output);
};

}

#endif
