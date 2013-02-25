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

#ifndef GENLISTITEMBASE_H
#define GENLISTITEMBASE_H

#include <Utils.h>
#include <IOView.h>

using namespace Utils;

//This macro is used to add a C callback for elementary buttons on items
#define ITEM_BUTTON_CALLBACK(_class, it_name) \
static void _item_button_##it_name(void *data, Evas_Object *obj, void *event_info) \
{ _class *c = reinterpret_cast<_class *>(data); if (data) c->buttonClick##it_name(); }

class GenlistItemBase: public sigc::trackable
{
        protected:
                Evas *evas;
                Evas_Object *parent;
                Evas_Object *genlist;

                Elm_Object_Item *item;

                Elm_Genlist_Item_Class item_class;
                string style;

                void *user_data;
                Elm_Genlist_Item_Type flags;

                DeletorBase *autodel_userdata;

                virtual void itemAdded() {} //item was added to a genlist

        public:
                GenlistItemBase(Evas *evas, Evas_Object *parent, string style, Elm_Genlist_Item_Type flags = ELM_GENLIST_ITEM_NONE, void *select_user_data = NULL);
                virtual ~GenlistItemBase();

                //Add item to genlist
                void Append(Evas_Object *genlist, GenlistItemBase *gparent = NULL);
                void Prepend(Evas_Object *genlist, GenlistItemBase *gparent = NULL);
                void InsertBefore(Evas_Object *genlist, GenlistItemBase *before, GenlistItemBase *gparent = NULL);
                void InsertAfter(Evas_Object *genlist, GenlistItemBase *after, GenlistItemBase *gparent = NULL);

                void RemoveItem() { elm_object_item_del(item); }
                GenlistItemBase *getPreviousItem();
                GenlistItemBase *getNextItem();

                void ShowItem(Elm_Genlist_Item_Scrollto_Type type);
                void BringInItem(Elm_Genlist_Item_Scrollto_Type type);

                virtual Evas_Object *getPartItem(Evas_Object *obj, string part);
                virtual string getLabelItem(Evas_Object *obj, string part);
                virtual bool getStateItem(Evas_Object *obj, string part);

                void setSelected(bool sel) { elm_genlist_item_selected_set(item, sel); }
                bool isSelected() { return elm_genlist_item_selected_get(item); }

                void updateField(string part, Elm_Genlist_Item_Field_Type type) { elm_genlist_item_fields_update(item, part.c_str(),  type); }

                sigc::signal<void, void *> item_selected;

                void setUserData(void *data) { user_data = data; }
                void *getUserData() { return user_data; }
                void setAutoDeleteUserData(DeletorBase *how_to_delete_user_data) { autodel_userdata = how_to_delete_user_data; }

                void itemEmitSignal(string signal, string source);

                //Used by C callback
                void emitSelectedSignal();
};

#endif // GENLISTITEMBASE_H
