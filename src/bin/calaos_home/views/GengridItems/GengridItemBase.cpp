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

#include "GengridItemBase.h"

static Evas_Object *_item_part_get(void *data, Evas_Object *obj, const char *part);
static void _item_delete(void *data, Evas_Object *obj);
static Eina_Bool _item_state(void *data, Evas_Object *obj, const char *part);
static char *_item_label(void *data, Evas_Object *obj, const char *part);
static void _item_sel_cb(void *data, Evas_Object *obj, void *event_info);

GengridItemBase::GengridItemBase(Evas *_evas, Evas_Object *_parent, string _style, void *select_user_data):
        evas(_evas),
        parent(_parent),
        gengrid(NULL),
        item(NULL),
        style(_style),
        user_data(select_user_data),
        autodel_userdata(NULL)
{
        item_class.item_style = style.c_str();
        item_class.func.text_get = _item_label;
        item_class.func.content_get = _item_part_get;
        item_class.func.state_get = _item_state;
        item_class.func.del = _item_delete;
}

GengridItemBase::~GengridItemBase()
{
        if (autodel_userdata)
        {
                (*autodel_userdata)(user_data);
                delete autodel_userdata;
        }

        cDebug() <<  "GengridItemBase: item deleted";
}

void GengridItemBase::Append(Evas_Object *_gengrid)
{
        gengrid = _gengrid;
        item = elm_gengrid_item_append(gengrid,
                                       &item_class,
                                       this,
                                       _item_sel_cb, /* select function */
                                       this); /* select function data */
        elm_object_item_data_set(item, this);
        itemAdded();
}

void GengridItemBase::Prepend(Evas_Object *_gengrid)
{
        gengrid = _gengrid;
        item = elm_gengrid_item_prepend(gengrid,
                                        &item_class,
                                        this,
                                        _item_sel_cb, /* select function */
                                        this); /* select function data */
        elm_object_item_data_set(item, this);
        itemAdded();
}

void GengridItemBase::InsertBefore(Evas_Object *_gengrid, GengridItemBase *before)
{
        gengrid = _gengrid;
        item = elm_gengrid_item_insert_before(gengrid,
                                              &item_class,
                                              this,
                                              before->item,
                                              _item_sel_cb, /* select function */
                                              this); /* select function data */
        elm_object_item_data_set(item, this);
        itemAdded();
}

void GengridItemBase::InsertAfter(Evas_Object *_gengrid, GengridItemBase *after)
{
        gengrid = _gengrid;
        item = elm_gengrid_item_insert_after(gengrid,
                                             &item_class,
                                             this,
                                             after->item,
                                             _item_sel_cb, /* select function */
                                             this); /* select function data */
        elm_object_item_data_set(item, this);
        itemAdded();
}

void GengridItemBase::ShowItem(Elm_Gengrid_Item_Scrollto_Type type)
{
        elm_gengrid_item_show(item, type);
}

void GengridItemBase::BringInItem(Elm_Gengrid_Item_Scrollto_Type type)
{
        elm_gengrid_item_bring_in(item, type);
}

Evas_Object *GengridItemBase::getPartItem(Evas_Object *obj, string part)
{
        return NULL;
}

string GengridItemBase::getLabelItem(Evas_Object *obj, string part)
{
        return "none";
}

bool GengridItemBase::getStateItem(Evas_Object *obj, string part)
{
        return false;
}

void GengridItemBase::itemEmitSignal(string signal, string source)
{
        if (!item) return;

        elm_object_item_signal_emit(item, signal.c_str(), source.c_str());
}

void GengridItemBase::emitSelectedSignal()
{
        item_selected.emit(user_data);
}

GengridItemBase * GengridItemBase::getPreviousItem()
{
        Elm_Object_Item *it = elm_gengrid_item_prev_get(item);
        if (!it) return NULL;
        GengridItemBase *prev = reinterpret_cast<GengridItemBase *>(elm_object_item_data_get(it));
        return prev;
}

GengridItemBase * GengridItemBase::getNextItem()
{
        Elm_Object_Item *it = elm_gengrid_item_next_get(item);
        if (!it) return NULL;
        GengridItemBase *next = reinterpret_cast<GengridItemBase *>(elm_object_item_data_get(it));
        return next;
}

Evas_Object *_item_part_get(void *data, Evas_Object *obj, const char *part)
{
        GengridItemBase *item = reinterpret_cast<GengridItemBase *>(data);
        if (!item)
        {
                cErrorDom("home") << "GengridItemBase : _item_part_get(): Can't cast data !";
                return NULL;
        }

        return item->getPartItem(obj, part);
}

void _item_delete(void *data, Evas_Object *obj)
{
        GengridItemBase *item = reinterpret_cast<GengridItemBase *>(data);
        if (!item)
        {
                cErrorDom("home") << "GengridItemBase : _item_delete(): Can't cast data !";
                return;
        }

        delete item;
}

Eina_Bool _item_state(void *data, Evas_Object *obj, const char *part)
{
        GengridItemBase *item = reinterpret_cast<GengridItemBase *>(data);
        if (!item)
        {
                cErrorDom("home") << "GengridItemBase : _item_state(): Can't cast data !";
                return false;
        }

        return item->getStateItem(obj, part);
}

char *_item_label(void *data, Evas_Object *obj, const char *part)
{
        GengridItemBase *item = reinterpret_cast<GengridItemBase *>(data);
        if (!item)
        {
                cErrorDom("home") << "GengridItemBase : _item_label(): Can't cast data !";
                return NULL;
        }

        return strdup(item->getLabelItem(obj, part).c_str());
}

void _item_sel_cb(void *data, Evas_Object *obj, void *event_info)
{
        GengridItemBase *item = reinterpret_cast<GengridItemBase *>(data);
        if (!item)
        {
                cErrorDom("home") << "GengridItemBase : _item_sel_cb(): Can't cast data !";
                return;
        }
        item->emitSelectedSignal();
}
