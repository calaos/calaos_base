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
#ifndef ActivityWebView_H
#define ActivityWebView_H

#include <Utils.h>

#include "ActivityView.h"
#include "KeyboardView.h"


class ActivityWebView: public ActivityView
{
private:
    KeyboardView *keyboard;
    Evas_Object *web;

    void buttonCallback(void *data, Evas_Object *edje_object, std::string emission, std::string source);
    void goToCallback(std::string url);

public:
    ActivityWebView(Evas *evas, Evas_Object *parent);
    ~ActivityWebView();

    virtual void resetView();


    //Used by C-Callbacks
    void _webLoadStarted();
    void _webLoadProgress();
    void _webLoadFinished(Elm_Web_Frame_Load_Error *error);
    void _webTitleChanged();
    void _webInputMethodChanged(bool en);
};

#endif // ActivityWebView_H
