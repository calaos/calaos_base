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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ActivityIntl.h"
#include "ActivityHomeView.h"
#include "ApplicationMain.h"
#include "IOView.h"
#include "IO/IOGenlistRoomGroup.h"

ActivityHomeView::ActivityHomeView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/home"),
    mode_detail(false),
    pageStatus(NULL)
{
    setPartText("header.label", _("My House"));
    setPartText("text", _("Loading..."));
    rooms.reserve(6);
    chauffages.reserve(6);
    chauff_change_con.reserve(6);
    chauff_del_con.reserve(6);
    for (int i = 0;i < 6;i++)
    {
        rooms.push_back(new EdjeObject(ApplicationMain::getTheme(), evas));
        chauffages.push_back(NULL);
        chauff_change_con.push_back(sigc::connection());
        chauff_del_con.push_back(sigc::connection());
    }

    list_top = elm_list_add(parent);
    Swallow(list_top, "list.top");
    elm_object_style_set(list_top, "home/room_list");
    evas_object_show(list_top);

    list_left = elm_genlist_add(parent);
    Swallow(list_left, "list.left");
    elm_object_style_set(list_left, "calaos");
    elm_genlist_select_mode_set(list_left, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_show(list_left);

    list_right = elm_genlist_add(parent);
    Swallow(list_right, "list.right");
    elm_object_style_set(list_right, "calaos");
    elm_genlist_select_mode_set(list_right, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_show(list_right);

    page_view = new PagingView(evas, parent);
    Swallow(page_view, "home.pager");

    page_view->drag_start.connect(sigc::mem_fun(*this, &ActivityHomeView::pagerDragStart));
    page_view->drag_stop.connect(sigc::mem_fun(*this, &ActivityHomeView::pagerDragStop));

    addCallback("home", "*", sigc::mem_fun(*this, &ActivityHomeView::EdjeCallback));
    addCallback("button.back", "pressed", sigc::mem_fun(*this, &ActivityHomeView::BackToMainViewCb));
}

ActivityHomeView::~ActivityHomeView()
{
    for_each(rooms.begin(), rooms.end(), Delete());

    clearLists();

    DELETE_NULL_FUNC(evas_object_del, list_top)
            DELETE_NULL_FUNC(evas_object_del, list_left)
            DELETE_NULL_FUNC(evas_object_del, list_right)

            DELETE_NULL(page_view)
}

void ActivityHomeView::objectShown()
{
    EmitSignal("show,right", "calaos");
    EmitSignal("show,left", "calaos");
}

void ActivityHomeView::objectHidden()
{
    EmitSignal("hide,right", "calaos");
    EmitSignal("hide,left", "calaos");
}

void ActivityHomeView::setRoom(string type, int position, IOBase *chauffage)
{
    if (rooms[position])
        delete rooms[position];

    EmitSignal("reset,rooms", "calaos");

    string t = type;

    if (type == "salon") t = "lounge";
    if (type == "chambre") t = "bedroom";
    if (type == "cuisine") t = "kitchen";
    if (type == "bureau") t = "office";
    if (type == "sam") t = "diningroom";
    if (type == "cave") t = "cellar";
    if (type == "divers") t = "various";
    if (type == "misc") t = "various";
    if (type == "exterieur") t = "outside";
    if (type == "sdb") t = "bathroom";
    if (type == "hall") t = "corridor";
    if (type == "couloir") t = "corridor";

    rooms[position] = new EdjeObject(ApplicationMain::getTheme(), evas);
    string group = "calaos/room/";

    if (!rooms[position]->LoadEdje(group + t))
    {
        //room not found, load default
        if (!rooms[position]->LoadEdje("calaos/room/various"))
        {
            throw(runtime_error("ActivityHomeView::setRoom(): Can't load edje !"));
        }
    }

    rooms[position]->Show();

    if (position > 2)
        rooms[position]->EmitSignal("size,small", "calaos");
    else
        rooms[position]->EmitSignal("size,normal", "calaos");

    Swallow(rooms[position], "room." + Utils::to_string(position + 1));

    chauffages[position] = chauffage;
    chauff_change_con[position].disconnect();
    chauff_del_con[position].disconnect();

    if (chauffage)
    {
        rooms[position]->EmitSignal("chauffage,show", "calaos");
        chauff_change_con[position] = chauffage->io_changed.connect(sigc::bind(sigc::mem_fun(*this, &ActivityHomeView::updateChauffage), position));
        chauff_del_con[position] = chauffage->io_deleted.connect(sigc::bind(sigc::mem_fun(*this, &ActivityHomeView::delChauffage), position));

        updateChauffage(position);
    }

    EmitSignal(string("enable,room,") + Utils::to_string(position + 1), "calaos"); //this enable mouse click on unused rooms
}

void ActivityHomeView::updateChauffage(int pos)
{
    if (pos < 0 || pos >= 6 || !rooms[pos])
        return;

    string t = chauffages[pos]->params["state"];
    rooms[pos]->setPartText("chauffage.temp.small", t + " °C");

    IOBase *consigne = CalaosModel::Instance().getHome()->getConsigneFromTemp(chauffages[pos]);
    if (consigne)
        t += "<consigne> / " + consigne->params["state"] + " °C</consigne>";
    rooms[pos]->setPartText("chauffage.temp", t);
}

void ActivityHomeView::delChauffage(int pos)
{
    if (pos < 0 || pos >= 6 || !rooms[pos])
        return;

    chauffages[pos] = NULL;
    chauff_change_con[pos].disconnect();
    chauff_del_con[pos].disconnect();
}

void ActivityHomeView::hideRoom(int position)
{
    rooms[position]->EmitSignal("hide", "calaos");
    EmitSignal(string("disable,room,") + Utils::to_string(position + 1), "calaos"); //this disable mouse click on unused rooms
}

void ActivityHomeView::resetRooms()
{
    rooms[0]->EmitSignal("size,normal", "calaos");
    rooms[1]->EmitSignal("size,normal", "calaos");
    rooms[2]->EmitSignal("size,normal", "calaos");
    rooms[3]->EmitSignal("size,small", "calaos");
    rooms[4]->EmitSignal("size,small", "calaos");
    rooms[5]->EmitSignal("size,small", "calaos");

    for (int i = 0;i < 6;i++)
        rooms[i]->EmitSignal("unselect", "calaos");

    mode_detail = false;
}

void ActivityHomeView::EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (emission.substr(0, 12) == "select,room,")
    {
        emission.erase(0, 12);
        int pos;
        from_string(emission, pos);
        pos--;

        if (pos < 0 || pos >= 6) return;

        if (pos > 2)
        {
            rooms[0]->EmitSignal("size,small", "calaos");
            rooms[1]->EmitSignal("size,small", "calaos");
            rooms[2]->EmitSignal("size,small", "calaos");
            rooms[3]->EmitSignal("size,normal", "calaos");
            rooms[4]->EmitSignal("size,normal", "calaos");
            rooms[5]->EmitSignal("size,normal", "calaos");
        }
        else
        {
            rooms[0]->EmitSignal("size,normal", "calaos");
            rooms[1]->EmitSignal("size,normal", "calaos");
            rooms[2]->EmitSignal("size,normal", "calaos");
            rooms[3]->EmitSignal("size,small", "calaos");
            rooms[4]->EmitSignal("size,small", "calaos");
            rooms[5]->EmitSignal("size,small", "calaos");
        }

        for (int i = 0;i < 6;i++)
            rooms[i]->EmitSignal("unselect", "calaos");

        rooms[pos]->EmitSignal("size,big", "calaos");

        room_selected = pos;
        mode_detail = true;

        room_click.emit(room_selected);
    }
    else if (emission == "room,right")
    {
        room_right_click.emit();
    }
    else if (emission == "room,left")
    {
        room_left_click.emit();
    }
    else if (emission == "mode,normal")
    {
        resetRooms();
    }
}

void ActivityHomeView::BackToMainViewCb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
}

void ActivityHomeView::EnableLeftButton()
{
    EmitSignal("enable,left", "calaos");
}

void ActivityHomeView::DisableLeftButton()
{
    EmitSignal("disable,left", "calaos");
}

void ActivityHomeView::EnableRightButton()
{
    EmitSignal("enable,right", "calaos");
}

void ActivityHomeView::DisableRightButton()
{
    EmitSignal("disable,right", "calaos");
}

void ActivityHomeView::ShowLoading()
{
    EmitSignal("show,loading", "calaos");
}

void ActivityHomeView::HideLoading()
{
    EmitSignal("hide,loading", "calaos");
}

void ActivityHomeView::selectPage_cb()
{
    page_view->bringPage(pageTimer);
}

void ActivityHomeView::selectPage(int page, double delay)
{
    if (page < 0 || page >= page_view->getPageCount())
        return;

    if (delay > 0.0)
    {
        pageTimer = page;
        EcoreTimer::singleShot(delay, sigc::mem_fun(*this, &ActivityHomeView::selectPage_cb));
    }
    else
    {
        page_view->bringPage(page);
    }
}

void ActivityHomeView::ButtonLightsOffCb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    const RoomIOCache &lights = CalaosModel::Instance().getHome()->getCacheLightsOn();
    RoomIOCache::const_iterator it = lights.begin();
    for (int i = 0;it != lights.end();it++, i++)
    {
        const RoomIO &lo = (*it).second;

        lo.io->sendAction("false");
    }
}

void ActivityHomeView::ButtonLightsInfoCb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    Evas_Object *table = createPaddingTable(evas, parent, 330, 300);

    Evas_Object *glist = elm_genlist_add(parent);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    map<Room *, list<IOBase *> > lights = CalaosModel::Instance().getHome()->getLightsOnForRooms();
    map<Room *, list<IOBase *> >::iterator it = lights.begin();
    for (;it != lights.end();it++)
    {
        Room *room = (*it).first;
        list<IOBase *> &ios = (*it).second;
        list<IOBase *>::iterator it_io;

        //Create group header
        GenlistItemBase *group_item = new IOGenlistRoomGroup(evas, parent, room, "");
        group_item->Append(glist);

        for (it_io = ios.begin();it_io != ios.end();it_io++)
        {
            IOBase *io = (*it_io);
            IOViewFactory::CreateIOBaseElement(evas, glist, io, glist, "left", group_item);
        }
    }

    elm_table_pack(table, glist, 1, 1, 1, 1);

    Evas_Object *popup = elm_ctxpopup_add(parent);
    elm_object_content_set(popup, table);
    elm_object_style_set(popup, "calaos");

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(popup, x, y);
    evas_object_show(popup);
}

void ActivityHomeView::ButtonShuttersDownCb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    const RoomIOCache &shutters = CalaosModel::Instance().getHome()->getCacheShuttersUp();
    RoomIOCache::const_iterator it = shutters.begin();
    for (int i = 0;it != shutters.end();it++, i++)
    {
        const RoomIO &lo = (*it).second;

        lo.io->sendAction("down");
    }
}

void ActivityHomeView::ButtonShuttersInfoCb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    Evas_Object *table = createPaddingTable(evas, parent, 360, 300);

    Evas_Object *glist = elm_genlist_add(parent);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    map<Room *, list<IOBase *> > shutters = CalaosModel::Instance().getHome()->getShuttersUpForRooms();
    map<Room *, list<IOBase *> >::iterator it = shutters.begin();
    for (;it != shutters.end();it++)
    {
        Room *room = (*it).first;
        list<IOBase *> &ios = (*it).second;
        list<IOBase *>::iterator it_io;

        //Create group header
        GenlistItemBase *group_item = new IOGenlistRoomGroup(evas, parent, room, "");
        group_item->Append(glist);

        for (it_io = ios.begin();it_io != ios.end();it_io++)
        {
            IOBase *io = (*it_io);
            IOViewFactory::CreateIOBaseElement(evas, glist, io, glist, "left", group_item);
        }
    }

    elm_table_pack(table, glist, 1, 1, 1, 1);

    Evas_Object *popup = elm_ctxpopup_add(parent);
    elm_object_content_set(popup, table);
    elm_object_style_set(popup, "calaos");

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(popup, x, y);
    evas_object_show(popup);
}

void ActivityHomeView::setLightsOnText(string txt)
{
    pageStatus->setPartText("light.text", txt);
}

void ActivityHomeView::setShuttersUpText(string txt)
{
    pageStatus->setPartText("shutter.text", txt);
}

void ActivityHomeView::addStatusPage()
{
    pageStatus = new EdjeObject(theme, evas);
    pageStatus->LoadEdje("calaos/page/home/status");
    pageStatus->setPartText("title", _("State of the House : <light_blue>Normal</light_blue><br><small>Informations concerning your House</small>"));
    pageStatus->setAutoDelete(true);

    pageStatus->addCallback("button.lights_off", "pressed", sigc::mem_fun(*this, &ActivityHomeView::ButtonLightsOffCb));
    pageStatus->addCallback("button.lights_info", "pressed", sigc::mem_fun(*this, &ActivityHomeView::ButtonLightsInfoCb));
    pageStatus->addCallback("button.shutters_down", "pressed", sigc::mem_fun(*this, &ActivityHomeView::ButtonShuttersDownCb));
    pageStatus->addCallback("button.shutters_info", "pressed", sigc::mem_fun(*this, &ActivityHomeView::ButtonShuttersInfoCb));

    page_view->addPage(pageStatus->getEvasObject());
}

void ActivityHomeView::addScenarioPage(list<IOBase *> &scenarios_io)
{
    EdjeObject *container = new EdjeObject(theme, evas);
    container->LoadEdje("calaos/page/home/scenario");
    container->setAutoDelete(true);

    list<IOBase *>::iterator it = scenarios_io.begin();
    for (int i = 0;it != scenarios_io.end() && i < 6;it++, i++)
    {
        IOView *ioView = IOViewFactory::CreateIOView(evas, getEvasObject(), IOView::IO_SCENARIO_HOME);
        ioView->setIO(*it);
        ioView->Show();
        ioView->initView();

        string _t = "element." + Utils::to_string(i + 1);
        container->Swallow(ioView, _t, true);
    }

    page_view->addPage(container->getEvasObject());
}

void ActivityHomeView::removePage(int p)
{
    page_view->delPage(p);
}

int ActivityHomeView::getPageCount()
{
    return page_view->getPageCount();
}

int ActivityHomeView::getCurrentPage()
{
    return page_view->getCurrentPage();
}

void ActivityHomeView::resetView()
{
    if (!mode_detail) return;

    //Simulate back button press to get back to initial state
    EmitSignal("pressed", "button.back");
}

void ActivityHomeView::clearLists()
{
    elm_genlist_clear(list_left);
    elm_genlist_clear(list_right);
    elm_list_clear(list_top);

    for_each(dataRoomCallbacks.begin(), dataRoomCallbacks.end(), Delete());
}

static void _room_list_click(void *data, Evas_Object *obj, void *event_info)
{
    HomeRoomClickData *hdata = reinterpret_cast<HomeRoomClickData *>(data);
    if (!hdata) return;

    hdata->view->changeCurrentRoomDetail(hdata->room);
}

void ActivityHomeView::changeCurrentRoomDetail(Room *room)
{
    //Disable this for now. need to find a better way to go back to
    //initial room title
    //rooms[room_selected]->setPartText("room.title", room->name);

    elm_genlist_clear(list_left);
    elm_genlist_clear(list_right);

    list<IOBase *>::iterator it = room->visible_ios.begin();
    for (int i = 0;it != room->visible_ios.end();it++, i++)
    {
        if (i % 2)
            IOViewFactory::CreateIOBaseElement(evas, list_left, *it, list_left, "left");
        else
            IOViewFactory::CreateIOBaseElement(evas, list_right, *it, list_right, "right");
    }

    elm_genlist_realized_items_update(list_left);
    elm_genlist_realized_items_update(list_right);
}

static void _del_list_data_cb(void *data, Evas_Object *obj, void *item)
{
    HomeRoomClickData *d = reinterpret_cast<HomeRoomClickData *>(data);
    DELETE_NULL(d);
}

void ActivityHomeView::setCurrentRoomDetail(Room *room)
{
    //Update top room list from current room type
    list<Room *> list_room_type = CalaosModel::Instance().getHome()->getRoomsForType(room->type);

    Elm_Object_Item *item = NULL;

    list<Room *>::iterator it = list_room_type.begin();
    for (;it != list_room_type.end();it++)
    {
        Room *r = *it;

        HomeRoomClickData *data = new HomeRoomClickData;
        data->view = this;
        data->room = r;

        Elm_Object_Item *_item = elm_list_item_append(list_top, r->name.c_str(), NULL, NULL, _room_list_click, data);
        elm_object_item_del_cb_set(_item, _del_list_data_cb);
        if (room == r) item = _item;
    }

    elm_list_go(list_top);

    if (item)
        elm_list_item_selected_set(item, true);
}

void ActivityHomeView::pagerDragStart()
{
    EmitSignal("show,edge", "calaos");
}

void ActivityHomeView::pagerDragStop()
{
    EmitSignal("hide,edge", "calaos");
}
