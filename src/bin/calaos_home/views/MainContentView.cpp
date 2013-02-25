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
#include "MainContentView.h"
#include "ApplicationMain.h"

MainContentView::MainContentView(Evas *e, Evas_Object *p):
        BaseView(e, p),
        EvasSmart(e, "PageContentView"),
        evas(e)
{
        clip = evas_object_rectangle_add(evas);
        evas_object_color_set(clip, 255, 255, 255, 255);
        AddMemberObject(clip);
}

MainContentView::~MainContentView()
{
        list<ViewAnimation *>::iterator it = views.begin();
        for (;it != views.end();it++)
        {
                ViewAnimation *va = (*it);

                DelMemberObject(va->animation->getEvasObject());
                delete va->animation;

                if (va->view)
                        delete va->view;

                delete va;
        }

        DelMemberObject(clip);
        evas_object_del(clip);
}

void MainContentView::addView(BaseView *view)
{
        if (!view)
                throw(runtime_error("MainContentView::addView(): view is NULL !"));

        int x, y, w, h;
        evas_object_geometry_get(clip, &x, &y, &w, &h);
        evas_object_show(clip);

        ViewAnimation *va = new ViewAnimation(view);
        va->view->EmitSignal("show", "calaos");
        va->view->Show();
        va->animation = new EdjeObject(theme, evas);
        va->animation->LoadEdje("elm/pager/base/calaos/fade_invisible");
        va->animation->Swallow(va->view, "elm.swallow.content");
        va->pop_me = false;
        va->animation->Move(x, y);
        va->animation->Resize(w, h);
        evas_object_clip_set(va->animation->getEvasObject(), clip);
        va->animation->addCallback("*", "elm,action,hide,finished", sigc::mem_fun(*this, &MainContentView::hideFinished), va);

        AddMemberObject(va->animation->getEvasObject());

        if (views.size() > 0)
        {
                ViewAnimation *old_va = views.back();
                old_va->animation->EmitSignal("elm,action,hide", "elm");
        }

        views.push_back(va);

        va->animation->Show();
        va->animation->EmitSignal("elm,action,push", "elm");
}

void MainContentView::removeTopView()
{
        if (views.size() <= 0)
                return;

        ViewAnimation *va = views.back();
        views.pop_back();

        va->animation->EmitSignal("elm,action,pop", "elm");
        va->pop_me = true;

        if (views.size() > 0)
        {
                ViewAnimation *last_va = views.back();
                last_va->view->Show();
                last_va->animation->Show();
                last_va->animation->EmitSignal("elm,action,show", "elm");
        }
}

void MainContentView::showView(BaseView *view)
{
        Utils::logger("root") << Priority::CRIT << "MainMenuView:showView() not implemented !" << log4cpp::eol;

        if (!view)
                throw(runtime_error("MainContentView::showView(): view is NULL !"));
}

void MainContentView::hideFinished(void *data, Evas_Object *edje_object, string emission, string source)
{
        ViewAnimation *va = reinterpret_cast<ViewAnimation *>(data);
        if (!va) return;

        if (va->pop_me)
        {
                DelMemberObject(va->animation->getEvasObject());

                if (member_count <= 0)
                        evas_object_hide(clip);

                delete va->animation;

                //Don't delete view if it has been pushed again to MainContentView
                list<ViewAnimation *>::iterator it = views.begin();
                bool found = false;
                for (;it != views.end();it++)
                {
                        ViewAnimation *_va = *it;
                        if (_va->view == va->view)
                        {
                                found = true;
                                break;
                        }
                }
                if (!found && va->view)
                        delete va->view;

                delete va;
        }
        else
        {
                va->animation->Hide();
                va->view->Hide();
                va->animation->EmitSignal("elm,action,reset", "elm");
                edje_object_message_signal_process(va->animation->getEvasObject());
        }
}

BaseView *MainContentView::getTopView()
{
        if (views.size() <= 0)
                return NULL;

        ViewAnimation *va = views.back();

        return va->view;
}

class SmartMoveFunctor
{
        private:
                int x, y;
        public:
                SmartMoveFunctor(int _x, int _y): x(_x), y(_y) {}

                void operator()(ViewAnimation *&va) const
                {
                        va->animation->Move(x, y);
                }
};
void MainContentView::SmartMove(int x, int y)
{
        evas_object_move(clip, x, y);

        for_each(views.begin(), views.end(), SmartMoveFunctor(x, y));
}

class SmartResizeFunctor
{
        private:
                int w, h;
        public:
                SmartResizeFunctor(int _w, int _h): w(_w), h(_h) {}

                void operator()(ViewAnimation *&va) const
                {
                        va->animation->Resize(w, h);
                }
};
void MainContentView::SmartResize(int w, int h)
{
        evas_object_resize(clip, w, h);

        for_each(views.begin(), views.end(), SmartResizeFunctor(w, h));
}

void MainContentView::SmartShow()
{
        evas_object_show(clip);
}

void MainContentView::SmartHide()
{
        evas_object_hide(clip);
}

void MainContentView::SmartColorSet(int r, int g, int b, int a)
{
        evas_object_color_set(clip, r, g, b, a);
}

void MainContentView::SmartClipSet(Evas_Object *_clip)
{
        evas_object_clip_set(clip, _clip);
}

void MainContentView::SmartClipUnset()
{
        evas_object_clip_unset(clip);
}
