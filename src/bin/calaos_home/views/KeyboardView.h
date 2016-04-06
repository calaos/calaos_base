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
#ifndef KeyboardView_H
#define KeyboardView_H

#include <Utils.h>

#include "BaseView.h"


class KeyboardView: public BaseView
{
private:
    void onKeyboardCallback(void *data, Evas_Object *edje_object, std::string emission, std::string source);

    bool keys_upper;
    bool keys_other;

public:
    KeyboardView(Evas *evas, Evas_Object *parent);
    ~KeyboardView();

    //simulate a pressed key
    void pressKey(std::string key);

    enum { KEY_ALPHA, KEY_SHIFT, KEY_SPACE, KEY_ENTER };
    sigc::signal<void, std::string, int> key_pressed;
};

#endif // KeyboardView_H
