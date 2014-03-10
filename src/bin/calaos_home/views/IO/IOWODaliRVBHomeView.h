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
#ifndef IOWODALIRVBHOMEVIEW_H
#define IOWODALIRVBHOMEVIEW_H

#include <Utils.h>
#include <GenlistItemBase.h>

using namespace Utils;

class IOWODaliRVBHomeView: public GenlistItemBase, public IOBaseElement
{
private:
    virtual void ioDeleted();

    EdjeObject *slider_red;
    EdjeObject *slider_green;
    EdjeObject *slider_blue;
    Evas_Object *color_preview;

    void sliderRedObjectDeleted();
    void sliderGreenObjectDeleted();
    void sliderBlueObjectDeleted();

    void sliderSignalCallback(void *data, Evas_Object *edje_object, string emission, string source);

public:
    IOWODaliRVBHomeView(Evas *evas, Evas_Object *parent, IOBase *io, string style_addition, Elm_Genlist_Item_Type flags = ELM_GENLIST_ITEM_NONE);
    virtual ~IOWODaliRVBHomeView();

    virtual Evas_Object *getPartItem(Evas_Object *obj, string part);
    virtual string getLabelItem(Evas_Object *obj, string part);

    //Called when the real IO changed
    virtual void initView();
    virtual void updateView();

    void buttonClickOn();
    void buttonClickOff();

    void buttonClickRedMore();
    void buttonClickRedLess();
    void buttonClickGreenMore();
    void buttonClickGreenLess();
    void buttonClickBlueMore();
    void buttonClickBlueLess();
};

#endif // IOWODALIRVBHOMEVIEW_H
