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

#ifndef GENLISTITEMSIMPLEHEADER_H
#define GENLISTITEMSIMPLEHEADER_H

#include <Utils.h>
#include <GenlistItemBase.h>


class GenlistItemSimpleHeader: public GenlistItemBase
{
private:
    std::string label;

    Params bt_labels;

public:
    GenlistItemSimpleHeader(Evas *evas, Evas_Object *parent, std::string label, std::string style_addition = "");
    virtual ~GenlistItemSimpleHeader();

    virtual std::string getLabelItem(Evas_Object *obj, std::string part);
    virtual Evas_Object *getPartItem(Evas_Object *obj, std::string part);

    void setButtonLabel(std::string _button, std::string _label) { bt_labels.Add(_button, _label); }

    void buttonClickBack();
    void buttonClickValid();

    sigc::signal<void, std::string> button_click; //special headers can have buttons
};

#endif // GENLISTITEMSIMPLEHEADER_H
