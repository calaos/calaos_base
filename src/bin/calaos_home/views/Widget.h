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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef CWidget_H
#define CWidget_H

#include <Utils.h>
#include <EdjeObject.h>
#include <EcoreTimer.h>
#include <Modules.h>
#include <BaseView.h>

class ActivityWidgetsView;

class Widget: public EdjeObject
{
protected:
    Evas_Object *parent;
    ActivityWidgetsView *view;

    double x, y, start_time;
    string id, mtype;

    ModuleDef mdef;

    EcoreTimer *timer;

    Evas_Object *popup;

    int offset_x, offset_y;
    int old_x, old_y, old_w, old_h;
    int start_x, start_y, start_w, start_h;
    bool moving, resizing;

    Evas_Object *move_box, *resize_box;

    int posx, posy, width, height;

    sigc::connection sig_widgetmanager;

    void _ResetAnim();

    void deleteValid(void *data);
    void deleteCancel(void *data);

public:
    Widget(string &_theme, Evas *_evas, ModuleDef &_mtype, string _id, Evas_Object *parent, ActivityWidgetsView *view);
    ~Widget();

    void Callback(Evas_Object *edje, std::string emission, std::string source);

    virtual void Show();
    virtual void Hide();
    virtual void Move(int x, int y);
    virtual void Resize(int w, int h);

    virtual void EditMode();
    virtual void NormalMode();

    //Reset position of widget
    virtual void Reset();

    //Save config & position to xml
    virtual void Save(TiXmlElement *node);

    virtual string getId() { return id; }
    virtual string getType() { return mtype; }
    virtual string getStringInfo();
};

#endif
