/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
#ifndef ROOMMODEL_H
#define ROOMMODEL_H

#include <Utils.h>
#include "CalaosConnection.h"
#include <TimeRange.h>

using namespace Utils;

class IOBase;
class Room;

bool IOHitsCompare(const IOBase *lhs, const IOBase *rhs);
bool RoomHitsCompare(const Room *lhs, const Room *rhs);

class IOBase;
class IOActionList
{
public:
    IOActionList(string a, string t, string t2, int ty): //title_computed must be computed with value first
        action(a), title(t), title_computed(t2), dvalue(0.0), red(0), green(0), blue(0), type(ty)
    {}
    IOActionList(string a, string t, int ty): //same title and title_computed
        action(a), title(t), title_computed(t), dvalue(0.0), red(0), green(0), blue(0), type(ty)
    {}
    IOActionList():
        dvalue(0.0), red(0), green(0), blue(0), type(ACTION_NONE)
    {}

    string action;
    string title;
    string title_computed;

    double dvalue;
    string svalue;

    //for color type
    int red, green, blue;

    enum { ACTION_NONE = 0, ACTION_SIMPLE, ACTION_SLIDER, ACTION_COLOR, ACTION_TEXT, ACTION_NUMBER, ACTION_TIME_MS };
    int type;

    string getComputedAction(IOBase *io);
    string getComputedTitle(IOBase *io);

    void copyValueFrom(IOActionList &ac)
    { dvalue = ac.dvalue; svalue = ac.svalue; red = ac.red; green = ac.green; blue = ac.blue; }
};

class TimeRangeInfos
{
public:
    TimeRangeInfos()
    {}

    vector<TimeRange> range_monday;
    vector<TimeRange> range_tuesday;
    vector<TimeRange> range_wednesday;
    vector<TimeRange> range_thursday;
    vector<TimeRange> range_friday;
    vector<TimeRange> range_saturday;
    vector<TimeRange> range_sunday;

    //months where InPlageHoraire is activated
    enum { JANUARY = 0, FEBRUARY, MARCH, APRIL, MAY, JUNE, JULY, AUGUST, SEPTEMBER, OCTOBER, NOVEMBER, DECEMBER };
    bitset<12> range_months;
};

class Room;
class IOBase: public sigc::trackable
{
private:
    friend class Room;
    CalaosConnection* connection;

    Room *room;

    void sendAction_cb(bool success, vector<string> result, void *data);
    void notifyChange(string notif);

    void checkCacheChange();

    void loadPlage_cb(bool success, vector<string> result, void *data);
    void loadPlageMonths_cb(bool success, vector<string> result, void *data);

public:
    IOBase(CalaosConnection* con, Room *r, int iotype):
        connection(con),
        room(r),
        io_type(iotype)
    {
        connection->getListener()->notify_io_change.connect(
                    sigc::mem_fun(*this, &IOBase::notifyChange));
    }
    ~IOBase()
    {
        io_deleted.emit();
    }

    Params params;

    enum { IO_INPUT, IO_OUTPUT };
    int io_type;

    void new_io_cb(bool success, vector<string> result, void *data);

    void sendAction(string command);
    void sendUserCommand(string command, CommandDone_cb callback, void *data = NULL);
    Room *getRoom() { return room; }

    //Some utility functions
    void getRGBValueFromState(int &r, int &g, int &b);
    int computeStateFromRGBValue(int r, int g, int b);
    double getDaliValueFromState();
    int getPercentVoletSmart();
    string getStatusVoletSmart();

    string getIconForIO();
    vector<IOActionList> getActionList();
    IOActionList getActionFromState();
    IOActionList getActionListFromAction(string action); //action is from the scenario action

    //InPlageHoraire functions
    void loadPlage(); //force a reload of the plage data

    TimeRangeInfos range_infos;

    //signals
    sigc::signal<void, IOBase *> load_done;
    sigc::signal<void> io_changed;
    sigc::signal<void> io_deleted;
};

class RoomModel;
class Room: public sigc::trackable
{
private:
    friend class IOBase;

    CalaosConnection* connection;
    RoomModel *model;

    int io_loaded;

    void notifyChange(string notif);
    void notifyIOAdd(string notif);
    void notifyIODel(string notif);

    void updateVisibleIO();

public:
    Room(CalaosConnection* con, RoomModel *_model):
        connection(con),
        model(_model)
    {
        connection->getListener()->notify_io_delete.connect(
                    sigc::mem_fun(*this, &Room::notifyIODel));
        connection->getListener()->notify_io_new.connect(
                    sigc::mem_fun(*this, &Room::notifyIOAdd));
        connection->getListener()->notify_room_change.connect(
                    sigc::mem_fun(*this, &Room::notifyChange));
    }
    ~Room()
    {
        room_deleted.emit();
        for_each(ios.begin(), ios.end(), Delete());
    }

    string name;
    string type;
    int hits;

    list<IOBase *> ios;
    list<IOBase *> visible_ios; //Contains only visible IO for a GUI
    list<IOBase *> scenario_ios; //Contains all IO controlable in a scenario

    void new_room_cb(bool success, vector<string> result, void *data);
    void load_io_done(IOBase *io);
    void load_io_notif_done(IOBase *io);

    void loadNewIO(string id, int io_type);
    void loadNewIOFromNotif(string id, int io_type);

    IOBase *getChauffage();

    sigc::signal<void, Room *> load_done;
    sigc::signal<void, IOBase *> io_deleted;
    sigc::signal<void, IOBase *> io_added;
    sigc::signal<void> room_changed;
    sigc::signal<void> room_deleted;
};

typedef struct _RoomLightsOn
{
    Room *room;
    IOBase *io;
} RoomIO;
typedef map<IOBase *, RoomIO> RoomIOCache;

class RoomModel: public sigc::trackable
{
private:
    CalaosConnection *connection;

    int room_loaded;

    void home_get_cb(bool success, vector<string> result, void *data);

    void load_room_done(Room *room);

    void updateRoomType();

    void updateChauffageIO(); //update association of chauffage IO

    void notifyRoomAdd(string notif);
    void notifyRoomDel(string notif);
    void notifyRoomChange(string notif); //monitor hits change and sort rooms again if change happens

    /* Caches */
    friend class Room;
    friend class IOBase;

    list<IOBase *> cacheScenarios;
    list<IOBase *> cacheScenariosPref;

    RoomIOCache cacheLightsOn;
    RoomIOCache cacheShuttersUp;

    map<string, IOBase *> cacheInputs;
    map<string, IOBase *> cacheOutputs;

    list<IOBase *> chauffageList;

public:
    RoomModel(CalaosConnection *connection);
    ~RoomModel();

    list<Room *> rooms; //All rooms sorted
    list<Room *> rooms_type; //Only types sorted by hits

    void load();

    const list<IOBase *> &getCacheScenarios() { return cacheScenarios; }
    const list<IOBase *> &getCacheScenariosPref() { return cacheScenariosPref; }
    const RoomIOCache &getCacheLightsOn() { return cacheLightsOn; }
    const RoomIOCache &getCacheShuttersUp() { return cacheShuttersUp; }
    const map<string, IOBase *> &getCacheInputs() { return cacheInputs; }
    const map<string, IOBase *> &getCacheOutputs() { return cacheOutputs; }

    map<Room *, list<IOBase *> > getLightsOnForRooms();
    map<Room *, list<IOBase *> > getShuttersUpForRooms();

    list<Room *> getRoomsForType(string type);

    IOBase *getConsigneFromTemp(IOBase *temp);
    IOBase *getTempFromConsigne(IOBase *temp);

    IOBase *getChauffageForType(string type);

    sigc::signal<void> load_done;

    sigc::signal<void, Room *> room_deleted;
    sigc::signal<void, Room *> room_added;

    sigc::signal<void, int> lights_on_changed;
    sigc::signal<void, int> shutters_up_changed;
};

#endif // ROOMMODEL_H
