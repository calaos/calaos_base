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

#ifndef GENLISTITEMSIMPLE_H
#define GENLISTITEMSIMPLE_H

#include <Utils.h>
#include <GenlistItemBase.h>


class GenlistItemSimple: public GenlistItemBase
{
private:
    std::string label;
    std::string icon;
    std::string button_icon;
    bool multiline;

public:
    GenlistItemSimple(Evas *evas, Evas_Object *parent, std::string label, bool can_select, bool multiline = false, void *data = NULL, std::string style_addition = "");
    virtual ~GenlistItemSimple();

    virtual std::string getLabelItem(Evas_Object *obj, std::string part);
    virtual Evas_Object *getPartItem(Evas_Object *obj, std::string part);

    void setIcon(std::string ic) { icon = ic; }
    void setButtonIcon(std::string ic) { button_icon = ic; }
    void setLabelText(std::string t) { label = t; updateField("text", ELM_GENLIST_ITEM_FIELD_TEXT); }

    //private, from c callback
    void buttonClickPressed();

    //public signal
    sigc::signal<void> button_pressed;
};

class GenlistItemSimpleKeyValue: public GenlistItemBase
{
private:
    std::string label_key, label_value;
    std::string icon;

public:
    GenlistItemSimpleKeyValue(Evas *evas, Evas_Object *parent, std::string label_key, std::string label_value, void *data = NULL);
    virtual ~GenlistItemSimpleKeyValue();

    virtual std::string getLabelItem(Evas_Object *obj, std::string part);
    virtual Evas_Object *getPartItem(Evas_Object *obj, std::string part);

    void setIcon(std::string ic) { icon = ic; }
};

#endif // GENLISTITEMSIMPLE_H
