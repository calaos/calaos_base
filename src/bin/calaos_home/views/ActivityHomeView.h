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
#ifndef ACTIVITYHOMEVIEW_H
#define ACTIVITYHOMEVIEW_H

#include <Utils.h>

#include "ActivityView.h"
#include <CalaosModel.h>

#include <IOView.h>
#include <PagingView.h>


class ActivityHomeView;
class HomeRoomClickData
{
public:
    ActivityHomeView *view;
    Room *room;
};

class ActivityHomeView: public ActivityView
{
protected:
    virtual void objectShown();
    virtual void objectHidden();

    std::vector<HomeRoomClickData *> dataRoomCallbacks;

    std::vector<IOBase *> chauffages;
    std::vector<sigc::connection> chauff_change_con;
    std::vector<sigc::connection> chauff_del_con;
    std::vector<EdjeObject *> rooms;

    Evas_Object *list_top;
    Evas_Object *list_left;
    Evas_Object *list_right;

    PagingView *page_view;

    int room_selected;
    bool mode_detail;

    int pageTimer;

    EdjeObject *pageStatus;

    void resetRooms();

    void EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void BackToMainViewCb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void ButtonLightsOffCb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void ButtonLightsInfoCb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void ButtonShuttersDownCb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void ButtonShuttersInfoCb(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void selectPage_cb();

    void updateChauffage(int pos);
    void delChauffage(int pos);

    void pagerDragStart();
    void pagerDragStop();

public:
    ActivityHomeView(Evas *evas, Evas_Object *parent);
    ~ActivityHomeView();

    void setRoom(std::string type, int position, IOBase *chauffage);
    void hideRoom(int position);

    void addStatusPage();
    void addScenarioPage(std::list<IOBase *> &ios);
    void removePage(int p);
    int getPageCount();
    int getCurrentPage();

    void selectPage(int page, double delay = 0.0);

    void EnableLeftButton();
    void DisableLeftButton();
    void EnableRightButton();
    void DisableRightButton();

    void ShowLoading();
    void HideLoading();

    virtual void resetView();

    void setLightsOnText(std::string txt);
    void setShuttersUpText(std::string txt);

    void clearLists();
    void setCurrentRoomDetail(Room *room);
    void changeCurrentRoomDetail(Room *room);

    sigc::signal<void> room_left_click;
    sigc::signal<void> room_right_click;
    sigc::signal<void, int> room_click;
};

#endif // ACTIVITYHOMEVIEW_H
