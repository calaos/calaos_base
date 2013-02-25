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
#include "ActivityHomeController.h"

ActivityHomeController::ActivityHomeController(Evas *e, Evas_Object *p):
        ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_HOME)
{
        CalaosModel::Instance();
}

ActivityHomeController::~ActivityHomeController()
{
}

void ActivityHomeController::createView()
{
        if (view) return;

        ActivityController::createView();

        ActivityHomeView *homeView = dynamic_cast<ActivityHomeView *>(view);

        homeView->room_left_click.connect(sigc::mem_fun(*this, &ActivityHomeController::clickRoomLeft));
        homeView->room_right_click.connect(sigc::mem_fun(*this, &ActivityHomeController::clickRoomRight));
        homeView->room_click.connect(sigc::mem_fun(*this, &ActivityHomeController::clickRoom));

        if (!CalaosModel::Instance().isLoaded())
        {
                homeView->ShowLoading();

                CalaosModel::Instance().home_loaded.connect(sigc::mem_fun(*this, &ActivityHomeController::load_done));

                return;
        }

        CalaosModel::Instance().getHome()->lights_on_changed.connect(sigc::mem_fun(*this, &ActivityHomeController::lights_changed));
        CalaosModel::Instance().getHome()->shutters_up_changed.connect(sigc::mem_fun(*this, &ActivityHomeController::shutter_changed));

        CalaosModel::Instance().getScenario()->scenario_new.connect(sigc::mem_fun(*this, &ActivityHomeController::scenarioReload));
        CalaosModel::Instance().getScenario()->scenario_change.connect(sigc::mem_fun(*this, &ActivityHomeController::scenarioReload));
        CalaosModel::Instance().getScenario()->scenario_del.connect(sigc::mem_fun(*this, &ActivityHomeController::scenarioReload));

        page = 0;
        updatePageView();
        updateScenarios();

        lights_changed(CalaosModel::Instance().getHome()->getCacheLightsOn().size());
        shutter_changed(CalaosModel::Instance().getHome()->getCacheShuttersUp().size());
}

void ActivityHomeController::clickRoomLeft()
{
        page--;
        if (page < 0) page = 0;

        updatePageView();
}

void ActivityHomeController::clickRoomRight()
{
        page++;
        if (page > (int)(CalaosModel::Instance().getHome()->rooms_type.size() / 6))
                page = (CalaosModel::Instance().getHome()->rooms_type.size() / 6);

        updatePageView();
}

void ActivityHomeController::updatePageView()
{
        ActivityHomeView *homeView = dynamic_cast<ActivityHomeView *>(view);
        list<Room *>::iterator it = CalaosModel::Instance().getHome()->rooms_type.begin();
        int i = 0;

        for (int j = 0;j < page * 6;j++)
                it++;

        for (;it != CalaosModel::Instance().getHome()->rooms_type.end() && i < 6;
             it++, i++)
        {
                Room *room = *it;

                IOBase *chauffage = CalaosModel::Instance().getHome()->getChauffageForType(room->type);

                homeView->setRoom(room->type, i, chauffage);
        }

        for (;i < 6;i++)
        {
                //hide unused rooms
                homeView->hideRoom(i);
        }

        if (page == 0)
                homeView->DisableLeftButton();
        else
                homeView->EnableLeftButton();

        int page_count = (CalaosModel::Instance().getHome()->rooms_type.size() / 6) - 1;
        if (CalaosModel::Instance().getHome()->rooms_type.size() % 6 > 0)
                page_count++;

        if (page < page_count &&
                        (CalaosModel::Instance().getHome()->rooms_type.size() > 6))
                homeView->EnableRightButton();
        else
                homeView->DisableRightButton();
}

void ActivityHomeController::load_done()
{
        ActivityHomeView *homeView = dynamic_cast<ActivityHomeView *>(view);
        homeView->HideLoading();

        CalaosModel::Instance().getHome()->lights_on_changed.connect(sigc::mem_fun(*this, &ActivityHomeController::lights_changed));
        CalaosModel::Instance().getHome()->shutters_up_changed.connect(sigc::mem_fun(*this, &ActivityHomeController::shutter_changed));

        page = 0;
        updatePageView();
        updateScenarios();

        lights_changed(CalaosModel::Instance().getHome()->getCacheLightsOn().size());
        shutter_changed(CalaosModel::Instance().getHome()->getCacheShuttersUp().size());
}

void ActivityHomeController::updateScenarios()
{
        ActivityHomeView *homeView = dynamic_cast<ActivityHomeView *>(view);

        const list<IOBase *> &scenarios = CalaosModel::Instance().getHome()->getCacheScenariosPref();
        list<IOBase *> page;

        //Add the status page first
        homeView->addStatusPage();

        list<IOBase *>::const_iterator it = scenarios.begin();
        for (int i = 0;it != scenarios.end();it++, i++)
        {
                IOBase *io = *it;
                page.push_back(io);

                if (page.size() >= 6)
                {
                        homeView->addScenarioPage(page);
                        page.clear();
                }
        }

        if (page.size() > 0)
        {
                while (page.size() < 6) page.push_back(NULL);
                homeView->addScenarioPage(page);
        }

        homeView->selectPage(1, 0.35);

        cout << "PageView count: " << homeView->getCurrentPage() << endl;
}

void ActivityHomeController::scenarioReload(Scenario *sc)
{
        ActivityHomeView *homeView = dynamic_cast<ActivityHomeView *>(view);
        if (!homeView) return;

        const list<IOBase *> &scenarios = CalaosModel::Instance().getHome()->getCacheScenariosPref();
        list<IOBase *> page;

        int currentpage = homeView->getCurrentPage();

        int count = homeView->getPageCount();
        cout << "PageView count: " << count << endl;
        for (int i = 1;i < count;i++)
        {
                cout << "PageView i: " << i << endl;
                homeView->removePage(1);
        }

        list<IOBase *>::const_iterator it = scenarios.begin();
        for (int i = 0;it != scenarios.end();it++, i++)
        {
                IOBase *io = *it;
                page.push_back(io);

                if (page.size() >= 6)
                {
                        homeView->addScenarioPage(page);
                        page.clear();
                }
        }

        if (page.size() > 0)
        {
                while (page.size() < 6) page.push_back(NULL);
                homeView->addScenarioPage(page);
        }

        if (currentpage >= homeView->getPageCount())
                currentpage = homeView->getPageCount();

        homeView->selectPage(currentpage);
}

void ActivityHomeController::clickRoom(int selected_room)
{
        ActivityHomeView *homeView = dynamic_cast<ActivityHomeView *>(view);

        list<Room *>::iterator it = CalaosModel::Instance().getHome()->rooms_type.begin();

        for (int j = 0;j < page * 6 + selected_room &&
             it != CalaosModel::Instance().getHome()->rooms_type.end();j++)
                it++;

        if (it == CalaosModel::Instance().getHome()->rooms_type.end())
        {
                Utils::logger("home") << Priority::ERROR << "ActivityHomeController::clickRoom(): Can't get room !" << log4cpp::eol;

                return;
        }

        homeView->clearLists();
        homeView->setCurrentRoomDetail(*it);
}

void ActivityHomeController::lights_changed(int count)
{
        ActivityHomeView *homeView = dynamic_cast<ActivityHomeView *>(view);
        string t;

        if (count <= 0)
                t = "Aucune lumière allumée";
        else if (count == 1)
                t = "1 lumière est allumée";
        else
                t = to_string(count) + " lumières sont allumées";

        homeView->setLightsOnText(t);
}

void ActivityHomeController::shutter_changed(int count)
{
        ActivityHomeView *homeView = dynamic_cast<ActivityHomeView *>(view);
        string t;

        if (count <= 0)
                t = "Aucune volet ouvert";
        else if (count == 1)
                t = "1 volet est ouvert";
        else
                t = to_string(count) + " volets sont ouverts";

        homeView->setShuttersUpText(t);
}
