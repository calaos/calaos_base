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
        action(a), title(t), title_computed(t2), dvalue(0.0), type(ty)
    {}
    IOActionList(string a, string t, int ty): //same title and title_computed
        action(a), title(t), title_computed(t), dvalue(0.0), type(ty)
    {}
    IOActionList():
        dvalue(0.0), type(ACTION_NONE)
    {}

    string action;
    string title;
    string title_computed;

    double dvalue;
    string svalue;

    //for color type
    ColorValue colorval;

    enum { ACTION_NONE = 0, ACTION_SIMPLE, ACTION_SLIDER, ACTION_COLOR, ACTION_TEXT, ACTION_NUMBER, ACTION_TIME_MS };
    int type;

    string getComputedAction(IOBase *io);
    string getComputedTitle(IOBase *io);

    void copyValueFrom(IOActionList &ac)
    { dvalue = ac.dvalue; svalue = ac.svalue; colorval = ac.colorval; }
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

    vector<TimeRange> getRange(int day);

    //months where InPlageHoraire is activated
    enum { JANUARY = 0, FEBRUARY, MARCH, APRIL, MAY, JUNE, JULY, AUGUST, SEPTEMBER, OCTOBER, NOVEMBER, DECEMBER };
    bitset<12> range_months;

    vector<int> isScheduledDate(tm scDate);

    string toString();
};

class Room;
class IOBase: public sigc::trackable
{
private:
    friend class Room;
    CalaosConnection* connection;

    Room *room;

    void sendAction_cb(bool success, vector<string> result, void *data);
    void notifyChange(const string &msgtype, const Params &evdata);

    void checkCacheChange();

    void loadPlage_cb(bool success, vector<string> result, void *data);
    void loadPlageMonths_cb(bool success, vector<string> result, void *data);

public:
    IOBase(CalaosConnection* con, Room *r, int iotype):
        connection(con),
        room(r),
        io_type(iotype)
    {
        connection->notify_io_change.connect(
                    sigc::mem_fun(*this, &IOBase::notifyChange));
    }
    ~IOBase()
    {
        io_deleted.emit();
    }

    Params params;

    enum { IO_INPUT, IO_OUTPUT };
    int io_type;

    void sendAction(string command);
    void sendUserCommand(string command, CommandDone_cb callback, void *data = NULL);
    Room *getRoom() { return room; }

    //Some utility functions
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

    //AVReceiver specifiv functions
    map<int, string> amplifier_inputs;

    //signals
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

    void notifyChange(const string &msgtype, const Params &evdata);
    void notifyIOAdd(const string &msgtype, const Params &evdata);
    void notifyIODel(const string &msgtype, const Params &evdata);

    void updateVisibleIO();

public:
    Room(CalaosConnection* con, RoomModel *_model):
        connection(con),
        model(_model)
    {
        connection->notify_io_delete.connect(
                    sigc::mem_fun(*this, &Room::notifyIODel));
        connection->notify_io_new.connect(
                    sigc::mem_fun(*this, &Room::notifyIOAdd));
        connection->notify_room_change.connect(
                    sigc::mem_fun(*this, &Room::notifyChange));
    }
    ~Room()
    {
        room_deleted.emit();
        for_each(ios.begin(), ios.end(), Delete());
    }

    void load(json_t *data);

    string name;
    string type;
    int hits;

    list<IOBase *> ios;
    list<IOBase *> visible_ios; //Contains only visible IO for a GUI
    list<IOBase *> scenario_ios; //Contains all IO controlable in a scenario

    void load_io_done(IOBase *io);
    void load_io_notif_done(IOBase *io);

    void loadNewIO(json_t *data, int io_type);
    void loadNewIOFromNotif(const Params &ioparam, int io_type);

    IOBase *getChauffage();

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

    void home_get_cb(json_t *result, void *data);

    void updateRoomType();

    void updateChauffageIO(); //update association of chauffage IO

    void notifyRoomAdd(const string &msgtype, const Params &evdata);
    void notifyRoomDel(const string &msgtype, const Params &evdata);
    void notifyRoomChange(); //notify hits change and sort rooms again if change happens

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

    json_t *jsonHome = nullptr;

public:
    RoomModel(CalaosConnection *connection);
    ~RoomModel();

    list<Room *> rooms; //All rooms sorted
    list<Room *> rooms_type; //Only types sorted by hits

    void load();
    json_t *getJsonHome() { return jsonHome; }

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
