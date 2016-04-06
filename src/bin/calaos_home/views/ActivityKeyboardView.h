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
#ifndef ActivityKeyboardView_H
#define ActivityKeyboardView_H

#include <Utils.h>

#include "ActivityView.h"
#include "KeyboardView.h"


class ActivityKeyboardView: public ActivityView
{
private:
    KeyboardView *keyboard;
    Evas_Object *textblock;

    void clearTextCb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void validInputCb(void *data, Evas_Object *_edje, std::string emission, std::string source);

public:
    ActivityKeyboardView(Evas *evas, Evas_Object *parent);
    ~ActivityKeyboardView();

    virtual void resetView();

    void setMultiline(bool multiline);
    void setSubtitle(std::string subtitle);
    void setText(std::string t);

    sigc::signal<void> clearText;
    sigc::signal<void, std::string> validPressed;
};

#endif // ActivityKeyboardView_H
