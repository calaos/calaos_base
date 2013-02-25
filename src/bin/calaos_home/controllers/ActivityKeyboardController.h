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
#ifndef ActivityKeyboardController_H
#define ActivityKeyboardController_H

#include <Utils.h>

#include "ActivityController.h"
#include "ActivityKeyboardView.h"
#include "CalaosModel.h"

using namespace Utils;

typedef sigc::slot<void, string> ActivityKeyboardCb;
typedef sigc::signal<void, string> ActivityKeyboardSig;

class ActivityKeyboardController: public ActivityController
{
        private:
                bool multiline;
                string subtitle;
                ActivityKeyboardCb callback;
                int type; //0 to get a UTF-8 string and 1 to keep evas textblock markup instead

                virtual void createView();

                void validButtonPressed(string text);

        public:
                ActivityKeyboardController(Evas *evas, Evas_Object *parent, string subtitle, ActivityKeyboardCb callback, bool multiline, int type);
                ~ActivityKeyboardController();

                void setText(string t);
};

#endif // ActivityKeyboardController_H
