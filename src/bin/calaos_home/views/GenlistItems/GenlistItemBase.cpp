/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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

#include "GenlistItemBase.h"

static Evas_Object *_item_part_get(void *data, Evas_Object *obj, const char *part);
static void _item_delete(void *data, Evas_Object *obj);
static Eina_Bool _item_state(void *data, Evas_Object *obj, const char *part);
static char *_item_label(void *data, Evas_Object *obj, const char *part);
static void _item_sel_cb(void *data, Evas_Object *obj, void *event_info);
static void _realized_cb(void *data, Evas_Object *, void *);

GenlistItemBase::GenlistItemBase(Evas *_evas, Evas_Object *_parent, string _style, Elm_Genlist_Item_Type _flags, void *select_user_data):
    evas(_evas),
    parent(_parent),
    genlist(NULL),
    item(NULL),
    style(_style),
    user_data(select_user_data),
    flags(_flags),
    autodel_userdata(NULL)
{
    item_class.item_style = style.c_str();
    item_class.func.text_get = _item_label;
    item_class.func.content_get = _item_part_get;
    item_class.func.state_get = _item_state;
    item_class.func.del = _item_delete;
}

GenlistItemBase::~GenlistItemBase()
{
    evas_object_smart_callback_del(genlist, "realized", _realized_cb);

    if (autodel_userdata)
    {
        (*autodel_userdata)(user_data);
        delete autodel_userdata;
    }

    cDebug() <<  "GenlistItemBase: item deleted";
}

static void _realized_cb(void *data, Evas_Object *, void *)
{
    GenlistItemBase *item = reinterpret_cast<GenlistItemBase *>(data);
    if (!item)
    {
        cErrorDom("home") << "GenlistItemBase : _item_part_get(): Can't cast data !";
        return;
    }
    item->itemRealized();
}

void GenlistItemBase::itemRealized()
{
    cDebugDom("genlist") << "item is realized, force update its content";
    elm_genlist_item_update(item);
    elm_genlist_item_fields_update(item, "*", ELM_GENLIST_ITEM_FIELD_CONTENT);
}

void GenlistItemBase::Append(Evas_Object *_genlist, GenlistItemBase *gparent)
{
    genlist = _genlist;
    evas_object_smart_callback_add(genlist, "realized", _realized_cb, this);
    item = elm_genlist_item_append(genlist,
                                   &item_class,
                                   this,
                                   (gparent)?gparent->item:NULL, /* parent item */
                                   flags,
                                   _item_sel_cb, /* select function */
                                   this); /* select function data */
    elm_object_item_data_set(item, this);
    itemAdded();
}

void GenlistItemBase::Prepend(Evas_Object *_genlist, GenlistItemBase *gparent)
{
    genlist = _genlist;
    evas_object_smart_callback_add(genlist, "realized", _realized_cb, this);
    item = elm_genlist_item_prepend(genlist,
                                    &item_class,
                                    this,
                                    (gparent)?gparent->item:NULL, /* parent item */
                                    flags,
                                    _item_sel_cb, /* select function */
                                    this); /* select function data */
    elm_object_item_data_set(item, this);
    itemAdded();
}

void GenlistItemBase::InsertBefore(Evas_Object *_genlist, GenlistItemBase *before, GenlistItemBase *gparent)
{
    genlist = _genlist;
    evas_object_smart_callback_add(genlist, "realized", _realized_cb, this);
    item = elm_genlist_item_insert_before(genlist,
                                          &item_class,
                                          this,
                                          (gparent)?gparent->item:NULL, /* parent item */
                                          before->item,
                                          flags,
                                          _item_sel_cb, /* select function */
                                          this); /* select function data */
    elm_object_item_data_set(item, this);
    itemAdded();
}

void GenlistItemBase::InsertAfter(Evas_Object *_genlist, GenlistItemBase *after, GenlistItemBase *gparent)
{
    genlist = _genlist;
    evas_object_smart_callback_add(genlist, "realized", _realized_cb, this);
    item = elm_genlist_item_insert_after(genlist,
                                         &item_class,
                                         this,
                                         (gparent)?gparent->item:NULL, /* parent item */
                                         after->item,
                                         flags,
                                         _item_sel_cb, /* select function */
                                         this); /* select function data */
    elm_object_item_data_set(item, this);
    itemAdded();
}

void GenlistItemBase::ShowItem(Elm_Genlist_Item_Scrollto_Type type)
{
    elm_genlist_item_show(item, type);
}

void GenlistItemBase::BringInItem(Elm_Genlist_Item_Scrollto_Type type)
{
    elm_genlist_item_bring_in(item, type);
}

Evas_Object *GenlistItemBase::getPartItem(Evas_Object *obj, string part)
{
    return NULL;
}

string GenlistItemBase::getLabelItem(Evas_Object *obj, string part)
{
    return "none";
}

bool GenlistItemBase::getStateItem(Evas_Object *obj, string part)
{
    return false;
}

void GenlistItemBase::itemEmitSignal(string signal, string source)
{
    if (!item) return;

    elm_object_item_signal_emit(item, signal.c_str(), source.c_str());
}

void GenlistItemBase::emitSelectedSignal()
{
    item_selected.emit(user_data);
}

GenlistItemBase * GenlistItemBase::getPreviousItem()
{
    Elm_Object_Item *it = elm_genlist_item_prev_get(item);
    if (!it) return NULL;
    GenlistItemBase *prev = reinterpret_cast<GenlistItemBase *>(elm_object_item_data_get(it));
    return prev;
}

GenlistItemBase * GenlistItemBase::getNextItem()
{
    Elm_Object_Item *it = elm_genlist_item_next_get(item);
    if (!it) return NULL;
    GenlistItemBase *next = reinterpret_cast<GenlistItemBase *>(elm_object_item_data_get(it));
    return next;
}

Evas_Object *_item_part_get(void *data, Evas_Object *obj, const char *part)
{
    GenlistItemBase *item = reinterpret_cast<GenlistItemBase *>(data);
    if (!item)
    {
        cErrorDom("home") << "GenlistItemBase : _item_part_get(): Can't cast data !";
        return NULL;
    }

    return item->getPartItem(obj, part);
}

void _item_delete(void *data, Evas_Object *obj)
{
    GenlistItemBase *item = reinterpret_cast<GenlistItemBase *>(data);
    if (!item)
    {
        cErrorDom("home") << "GenlistItemBase : _item_delete(): Can't cast data !";
        return;
    }

    delete item;
}

Eina_Bool _item_state(void *data, Evas_Object *obj, const char *part)
{
    GenlistItemBase *item = reinterpret_cast<GenlistItemBase *>(data);
    if (!item)
    {
        cErrorDom("home") << "GenlistItemBase : _item_state(): Can't cast data !";
        return false;
    }

    return item->getStateItem(obj, part);
}

char *_item_label(void *data, Evas_Object *obj, const char *part)
{
    GenlistItemBase *item = reinterpret_cast<GenlistItemBase *>(data);
    if (!item)
    {
        cErrorDom("home") << "GenlistItemBase : _item_label(): Can't cast data !";
        return NULL;
    }

    return strdup(item->getLabelItem(obj, part).c_str());
}

void _item_sel_cb(void *data, Evas_Object *obj, void *event_info)
{
    GenlistItemBase *item = reinterpret_cast<GenlistItemBase *>(data);
    if (!item)
    {
        cErrorDom("home") << "GenlistItemBase : _item_sel_cb(): Can't cast data !";
        return;
    }
    item->emitSelectedSignal();
}
