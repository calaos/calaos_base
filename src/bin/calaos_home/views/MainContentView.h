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
#ifndef MAINCONTENTVIEW_H
#define MAINCONTENTVIEW_H

#include <Utils.h>

#include "BaseView.h"
#include "EvasSmart.h"

using namespace Utils;

class ViewAnimation
{
private:
    void viewDeleted()
    {
        view = NULL;
    }

public:
    ViewAnimation(BaseView *v):
        view(v)
    {
        view->object_deleted.connect(sigc::mem_fun<void>(*this, &ViewAnimation::viewDeleted));
    }

    BaseView *view;
    EdjeObject *animation;
    bool pop_me;
};

class MainContentView: public BaseView, public EvasSmart
{
private:
    list<ViewAnimation *> views;
    BaseView *top_item;

    Evas_Object *clip;

    void hideFinished(void *data, Evas_Object *edje_object, string emission, string source);

public:
    MainContentView(Evas *evas, Evas_Object *parent);
    ~MainContentView();

    void addView(BaseView *view);
    void removeTopView(bool showLastView = true);
    void showView(BaseView *view);

    BaseView *getTopView();


    virtual void SmartMove(int x, int y);
    virtual void SmartResize(int w, int h);
    virtual void SmartShow();
    virtual void SmartHide();
    virtual void SmartColorSet(int r, int g, int b, int a);
    virtual void SmartClipSet(Evas_Object *clip);
    virtual void SmartClipUnset();
};

#endif // MAINCONTENTVIEW_H
