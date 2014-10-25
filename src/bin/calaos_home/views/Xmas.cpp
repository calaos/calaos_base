/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include "Xmas.h"

#define MAX_FLAKE 60

static Eina_Bool _snow_cb_animator (void *data)
{
        XmasWidget *w = reinterpret_cast<XmasWidget *>(data);
        if (w) w->_Animator();

        return EINA_TRUE;
}

XmasWidget::XmasWidget(string &_theme, Evas *_evas, ModuleDef &_mdef, string _id, Evas_Object *_parent, ActivityWidgetsView *_view):
                        Widget(_theme, _evas, _mdef, _id, _parent, _view),
                        animator(NULL), clip(NULL)
{
        LoadWidget("xmas", 0.0, 0.0, _id);
}

XmasWidget::~XmasWidget()
{
        if (animator) ecore_animator_del(animator);
        animator = NULL;

        for_each(flakes.begin(), flakes.end(), Delete());

        evas_object_del(clip);
}

void XmasWidget::Show()
{
        edje_object_signal_emit(edje, "enable", "calaos");
        EdjeObject::Show();

        if (animator) ecore_animator_del(animator);
        animator = ecore_animator_add(_snow_cb_animator, this);
}

void XmasWidget::Hide()
{
        edje_object_signal_emit(edje, "disable", "calaos");

        if (animator) ecore_animator_del(animator);
        animator = NULL;
}

bool XmasWidget::LoadWidget(string, double _x, double _y, string _id)
{
        string witem = "calaos/widget/xmas";
        if (!LoadEdje(witem))
        {
                return false;
        }

        setLayer(500);
        EdjeObject::Show();

        clip = evas_object_rectangle_add(evas);
        evas_object_show(clip);
        edje_object_part_swallow(edje, "widget.swallow", clip);

        Resize(1024, 768);

        //create some flakes
        for (int i = 0;i < MAX_FLAKE;i++)
        {
                string type;
                if (i < MAX_FLAKE / 3) type = "flake_small";
                else if (i >= MAX_FLAKE / 3 && i < (MAX_FLAKE / 3) * 2) type = "flake_medium";
                else type = "flake_large";

                EdjeObject *o = new EdjeObject(theme, evas);
                if (!o->LoadEdje("calaos/widget/xmas/" + type))
                    cError() << "Error loading edje group calaos/widget/xmas/" + type;
                o->setLayer(500);
                evas_object_clip_set(o->getEvasObject(), clip);

                Flake *flake = new Flake(o);

                int tx = random() % clipw;
                int ty = random() % cliph;

                flake->Move(tx + clipx, ty + clipy);
                flake->Show();
                flake->setStart(ecore_time_get() + (double)(random() % (flake->getHeight() * 10)) / (double)flake->getHeight());

                if (type == "flake_small") flake->setSpeed(1);
                if (type == "flake_medium") flake->setSpeed(2);
                if (type == "flake_large") flake->setSpeed(3);

                flakes.push_back(flake);
        }

        return true;
}

void XmasWidget::_Animator()
{
        double d;

        for (uint i = 0;i < flakes.size();i++)
        {
                Flake *flake = flakes[i];
                Evas_Coord _y;

                d = ecore_time_get() - flake->getStart();
                _y = 30 * d * flake->getSpeed();
                if (_y > cliph)
                        flake->setStart(ecore_time_get() + (double)(random() % 100) / (double)100);
                flake->Move(flake->getX(), _y + clipy);
        }
}

void XmasWidget::Move(int _x, int _y)
{
    EdjeObject::Move(_x, _y);
    edje_object_part_geometry_get(edje, "widget.swallow", &clipx, &clipy, &clipw, &cliph);
    for (uint i = 0;i < flakes.size();i++)
    {
        Flake *flake = flakes[i];
        if (clipw == 0) return;
        int tx = random() % clipw;
        flake->Move(tx + clipx, flake->getY());
    }
}

void XmasWidget::Resize(int _w, int _h)
{
    EdjeObject::Resize(_w, _h);
    edje_object_part_geometry_get(edje, "widget.swallow", &clipx, &clipy, &clipw, &cliph);
    for (uint i = 0;i < flakes.size();i++)
    {
        Flake *flake = flakes[i];
        if (clipw == 0) return;
        int tx = random() % clipw;
        flake->Move(tx + clipx, flake->getY());
    }
}

void XmasWidget::EditMode()
{
}

void XmasWidget::NormalMode()
{
}

void XmasWidget::Reset()
{
}

void XmasWidget::Save(TiXmlElement *node)
{
}

string XmasWidget::getStringInfo()
{
        return "Xmas";
}
