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
#include "ActivityKeyboardController.h"

ActivityKeyboardController::ActivityKeyboardController(Evas *e, Evas_Object *p, string _subtitle, ActivityKeyboardCb _cb, bool _multiline, int t):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_KEYBOARD),
    multiline(_multiline),
    subtitle(_subtitle),
    callback(_cb),
    type(t)
{
}

ActivityKeyboardController::~ActivityKeyboardController()
{
}

void ActivityKeyboardController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityKeyboardView *keyboardView = dynamic_cast<ActivityKeyboardView *>(view);
    keyboardView->setMultiline(multiline);
    keyboardView->setSubtitle(subtitle);
    keyboardView->validPressed.connect(sigc::mem_fun(*this, &ActivityKeyboardController::validButtonPressed));
}

void ActivityKeyboardController::validButtonPressed(string text)
{
    ActivityKeyboardSig sig;
    sig.connect(callback);

    if (type == 0)
    {
        char *s = elm_entry_markup_to_utf8(text.c_str());
        text = s;
        free(s);
    }
    sig.emit(text);

    //Closes the keyboard
    wants_quit.emit();
}

void ActivityKeyboardController::setText(string t)
{
    if (!view) return;

    ActivityKeyboardView *keyboardView = dynamic_cast<ActivityKeyboardView *>(view);
    keyboardView->setText(t);
}
