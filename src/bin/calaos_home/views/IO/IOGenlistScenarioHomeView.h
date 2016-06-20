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
#ifndef IOGENLISTSCENARIOHOMEVIEW_H
#define IOGENLISTSCENARIOHOMEVIEW_H

#include <Utils.h>
#include <GenlistItemBase.h>


class IOGenlistScenarioHomeView: public GenlistItemBase, public IOBaseElement
{
private:
    Evas_Object *object_button;

    std::string state;

    virtual void ioDeleted();
    void clickFlashButton_cb();

public:
    IOGenlistScenarioHomeView(Evas *evas, Evas_Object *parent, IOBase *io, std::string style_addition, Elm_Genlist_Item_Type flags = ELM_GENLIST_ITEM_NONE);
    virtual ~IOGenlistScenarioHomeView();

    virtual Evas_Object *getPartItem(Evas_Object *obj, std::string part);
    virtual std::string getLabelItem(Evas_Object *obj, std::string part);

    //Called when the real IO changed
    virtual void initView();
    virtual void updateView();

    void buttonClickGo();
};

#endif // IOGENLISTSCENARIOHOMEVIEW_H
