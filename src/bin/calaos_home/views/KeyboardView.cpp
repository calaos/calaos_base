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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "KeyboardView.h"

#ifdef HAVE_ECORE_X
#include "Ecore_X.h"
#endif

//Some XKeysims
#define XK_Return                        0xff0d
#define XK_BackSpace                     0xff08

KeyboardView::KeyboardView(Evas *_e, Evas_Object *_parent):
        BaseView(_e, _parent),
        keys_upper(false),
        keys_other(false)
{
        try
        {
                LoadEdje("calaos/keyboard");
        }
        catch (exception const &e)
        {
                cCriticalDom("root") <<  "KeyboardView: Can't load edje";
                throw;
        }

        addCallback("keyboard", "*", sigc::mem_fun(*this, &KeyboardView::onKeyboardCallback));
}

KeyboardView::~KeyboardView()
{
}

void KeyboardView::pressKey(string k)
{
#ifdef HAVE_ECORE_X
        /* Code from enlightenment/Illume */
        const char *key = NULL;
        int glyph;

        /* utf8 -> glyph id (unicode - ucs4) */
        glyph = 0;
        evas_string_char_next_get(k.c_str(), 0, &glyph);
        if (glyph <= 0) return;
        /* glyph id -> keysym */
        if (glyph > 0xff) glyph |= 0x1000000;

        key = ecore_x_keysym_string_get(glyph);

        if (!key) return;

        ecore_x_test_fake_key_press(key);
#endif
}

void KeyboardView::onKeyboardCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
#ifdef HAVE_ECORE_X
        if (source == "keyboard" && emission == "key,key_maj")
        {
                keys_upper = !keys_upper;

                if (keys_upper)
                        EmitSignal("keyboard,upper", "calaos");
                else
                        EmitSignal("keyboard,lower", "calaos");
        }
        else if (source == "keyboard" && emission == "key,key_other")
        {
                keys_other = !keys_other;

                if (keys_other)
                        EmitSignal("keyboard,other", "calaos");
                else
                        EmitSignal("keyboard,normal", "calaos");
        }
        else if (source == "keyboard" && emission == "key,key_space")
        {
                pressKey(" ");
        }
        else if (source == "keyboard" && emission == "key,key_del")
        {
                ecore_x_test_fake_key_press(ecore_x_keysym_string_get(XK_BackSpace));
        }
        else if (source == "keyboard" && emission == "key,key_enter")
        {
                ecore_x_test_fake_key_press(ecore_x_keysym_string_get(XK_Return));

                EmitSignal("hide,keyboard", "calaos");
        }
        else if (source == "keyboard" && emission == "key,key_multiply")
        {
                pressKey("*");
        }
        else if (source == "keyboard" && emission == "key,key_question")
        {
                pressKey("?");
        }
        else if (source == "keyboard" && emission == "key,key_quote")
        {
                pressKey("\"");
        }
        else if (source == "keyboard" && emission == "key,key_backslash")
        {
                pressKey("\\");
        }
        else if (source == "keyboard" && emission == "key,key_bracket_open")
        {
                pressKey("[");
        }
        else if (source == "keyboard" && emission == "key,key_bracket_close")
        {
                pressKey("]");
        }
        else if (source == "keyboard" && emission == "key,key_double_point")
        {
                pressKey(":");
        }
        else if (source == "keyboard" && emission.substr(0, 4) == "key,")
        {
                emission.erase(0, 4);
                if (keys_upper)
                        std::transform (emission.begin(), emission.end(), emission.begin(), to_upper());

                pressKey(emission);
        }
#endif
}
