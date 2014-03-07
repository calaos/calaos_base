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
#include <EvasSmart.h>

static void _smart_add(Evas_Object *o);
static void _smart_del(Evas_Object *o);
static void _smart_move(Evas_Object *o, Evas_Coord x, Evas_Coord y);
static void _smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h);
static void _smart_show(Evas_Object *o);
static void _smart_hide(Evas_Object *o);
static void _smart_color_set(Evas_Object * o, int r, int g, int b, int a);
static void _smart_clip_set(Evas_Object * o, Evas_Object * clip);
static void _smart_clip_unset(Evas_Object * o);
static void _smart_calculate(Evas_Object * o);
static void _smart_member_add(Evas_Object * o, Evas_Object *child);
static void _smart_member_del(Evas_Object * o, Evas_Object *child);

EvasSmart::EvasSmart(Evas *evas, string evas_smart_type):
    evas_object(NULL),
    smart_object(NULL),
    member_count(0)
{
    smart_object = EvasSmartClassCreate(evas_smart_type);

    evas_object = evas_object_smart_add(evas, smart_object);
    evas_object_smart_data_set(evas_object, this);
}

Evas_Smart *EvasSmart::EvasSmartClassCreate(string evas_smart_type)
{
    static std::map<string, Evas_Smart *> SmartObjects;
    static std::map<string, Evas_Smart_Class> SmartClasses;

    //try to find the smart class
    //if not found create a new one
    map<string, Evas_Smart *>::iterator fter = SmartObjects.find(evas_smart_type);
    if (fter != SmartObjects.end())
        return SmartObjects[evas_smart_type];

    Evas_Smart_Class sc =
    {
        NULL,
        EVAS_SMART_CLASS_VERSION,
        _smart_add,
        _smart_del,
        _smart_move,
        _smart_resize,
        _smart_show,
        _smart_hide,
        _smart_color_set,
        _smart_clip_set,
        _smart_clip_unset,
        _smart_calculate,
        _smart_member_add,
        _smart_member_del,
        NULL
    };

    sc.name = evas_smart_type.c_str();

    SmartClasses[evas_smart_type] = sc;

    return (evas_smart_class_new(&SmartClasses[evas_smart_type]));
}

EvasSmart::~EvasSmart()
{
    if (evas_object)
    {
        evas_object_del(evas_object);
        evas_object = NULL;
    }

    evas_smart_free(smart_object);
}

//C smart objects callbacks
static void _smart_add(Evas_Object *o)
{
}
static void _smart_del(Evas_Object *o)
{
}
static void _smart_move(Evas_Object *o, Evas_Coord x, Evas_Coord y)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartMove(x, y);
}
static void _smart_resize(Evas_Object *o, Evas_Coord w, Evas_Coord h)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartResize(w, h);
}
static void _smart_show(Evas_Object *o)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartShow();
}
static void _smart_hide(Evas_Object *o)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartHide();
}
static void _smart_color_set(Evas_Object * o, int r, int g, int b, int a)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartColorSet(r, g, b, a);
}
static void _smart_clip_set(Evas_Object *o, Evas_Object * clip)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartClipSet(clip);
}
static void _smart_clip_unset(Evas_Object *o)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartClipUnset();
}
static void _smart_calculate(Evas_Object * o)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartCalculate();
}

static void _smart_member_add(Evas_Object * o, Evas_Object *child)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartMemberAdd(child);
}

static void _smart_member_del(Evas_Object * o, Evas_Object *child)
{
    EvasSmart *_esmart = reinterpret_cast<EvasSmart *>(evas_object_smart_data_get(o));
    if (_esmart)
        _esmart->SmartMemberDel(child);
}
