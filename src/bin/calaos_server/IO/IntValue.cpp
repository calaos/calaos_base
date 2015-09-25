/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "IntValue.h"
#include "CalaosConfig.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO_USERTYPE(InternalInt, Internal)
REGISTER_IO_USERTYPE(InternalBool, Internal)
REGISTER_IO_USERTYPE(InternalString, Internal)

Internal::Internal(Params &p):
    IOBase(p, IOBase::IO_INOUT),
    bvalue(false),
    dvalue(0.0),
    svalue("")
{
    cInfoDom("output") << get_param("id") << ": Ok";

    // Define IO documentation
    ioDoc->friendlyNameSet(p["type"]);
    if (p["type"] == "InternalInt")
    {
        ioDoc->descriptionSet(_("Internal number object. This object is useful for doing internal programing in rules, like counters, of displaying values."));
        ioDoc->paramAdd("step", _("Set a step for increment/decrement value. Default is 1.0"), IODoc::TYPE_FLOAT, false, "1");
        ioDoc->conditionAdd("0", _("Event on a specific number value"));
        ioDoc->actionAdd("0", _("Set a specific number value"));
        ioDoc->actionAdd("inc", _("Increment value with configured step"));
        ioDoc->actionAdd("dec", _("Decrement value with configured step"));
        ioDoc->actionAdd("inc 1", _("Increment value by value"));
        ioDoc->actionAdd("dec 1", _("Decrement value by value"));
    }
    else if (p["type"] == "InternalBool")
    {
        ioDoc->descriptionSet(_("Internal boolean object. This object is useful for doing internal programing in rules, like keeping boolean states, or displaying boolean values"));
        ioDoc->conditionAdd("true", _("Event when value is true"));
        ioDoc->conditionAdd("false", _("Event when value is false"));
        ioDoc->actionAdd("true", _("Set a value to true"));
        ioDoc->actionAdd("false", _("Set a value to false"));
        ioDoc->actionAdd("toggle", _("Invert boolean value"));
        ioDoc->actionAdd("impulse 200", _("Do an impulse on boolean value. Set to true for X ms then reset to false"));
        ioDoc->actionAdd("impulse 500 200 500 200", _("Do an impulse on boolean value with a pattern.<br>"
                                                             "Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>"
                                                             "Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>"
                                                             "Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts)"));
    }
    else if (p["type"] == "InternalString")
    {
        ioDoc->descriptionSet(_("Internal string object. This object is useful for doing internal programing in rules or displaying text values on user interfaces."));
        ioDoc->conditionAdd("value", _("Event on a specific string value"));
        ioDoc->actionAdd("value", _("Set a specific string value"));
    }

    ioDoc->paramAdd("rw", _("Enable edit mode for this object. It allows user to modify the value on interfaces. Default to false"), IODoc::TYPE_BOOL, false, "false");
    ioDoc->paramAdd("save", _("Automatically save the value in cache. The value will be reloaded when restarting calaos is true. Default to false"), IODoc::TYPE_BOOL, false, "false");

    ioDoc->conditionAdd("changed", _("Event on any change of value"));

    if (!get_params().Exists("visible")) set_param("visible", "false");
    if (!get_params().Exists("rw")) set_param("rw", "false");
    if (!get_params().Exists("save")) set_param("save", "false");
    if (get_param("type") == "InternalBool") set_param("gui_type", "var_bool");
    if (get_param("type") == "InternalInt") set_param("gui_type", "var_int");
    if (get_param("type") == "InternalString") set_param("gui_type", "var_string");

    LoadFromConfig();
}

Internal::~Internal()
{
    cInfoDom("output");
}

bool Internal::set_value(bool val)
{
    DELETE_NULL(timer);
    if (!isEnabled()) return true;

    cInfoDom("output") << get_param("id") << ": got action, " << ((val)?"True":"False");

    bvalue = val;
    EmitSignalIO();

    Save();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", bvalue?"true":"false" } });

    return true;
}

bool Internal::set_value(double val)
{
    if (!isEnabled()) return true;

    cInfoDom("output") << get_param("id") << ": got action, " << val;

    dvalue = val;
    EmitSignalIO();

    Save();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", Utils::to_string(dvalue) } });

    return true;
}

bool Internal::set_value(string val)
{
    if (!isEnabled()) return true;

    if (get_type() == TINT)
    {
        if (val == "inc")
        {
            double step = 1.0;
            if (Utils::is_of_type<double>(get_param("step")))
                Utils::from_string(get_param("step"), step);

            set_value(dvalue + step);
        }
        else if (val == "dec")
        {
            double step = 1.0;
            if (Utils::is_of_type<double>(get_param("step")))
                Utils::from_string(get_param("step"), step);

            set_value(dvalue - step);
        }
        else if (val.compare(0, 4, "inc ") == 0)
        {
            string t = val;
            t.erase(0, 4);

            double step = 1.0;
            if (Utils::is_of_type<double>(t))
                Utils::from_string(t, step);

            set_value(dvalue + step);
        }
        else if (val.compare(0, 4, "dec ") == 0)
        {
            string t = val;
            t.erase(0, 4);

            double step = 1.0;
            if (Utils::is_of_type<double>(t))
                Utils::from_string(t, step);

            set_value(dvalue - step);
        }
        else if (Utils::is_of_type<double>(val))
        {
            double dval;
            Utils::from_string(val, dval);

            set_value(dval);
        }
        else
        {
            return false;
        }
    }
    else if (get_type() == TSTRING)
    {
        cInfoDom("output") << get_param("id") << ": got action, " << val;

        svalue = val;
        EmitSignalIO();

        Save();

        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", get_param("id") },
                               { "state", svalue } });
    }
    else if (get_type() == TBOOL)
    {
        if (val.compare(0, 8, "impulse ") == 0)
        {
            string tmp = val;
            tmp.erase(0, 8);
            // classic impulse, output goes false after <time> miliseconds
            if (Utils::is_of_type<int>(tmp))
            {
                int t;
                Utils::from_string(tmp, t);

                cInfoDom("output") << get_param("id")
                                   << ": got impulse action, staying true for "
                                   << t << "ms";

                set_value(true);

                timer = new EcoreTimer((double)t / 1000., [=]()
                {
                    set_value(false);
                });
            }
            else
            {
                // extended impulse using pattern
                impulse_extended(tmp);
            }

            return true;
        }
        else if (val == "toggle")
        {
            return set_value(!bvalue);
        }
        else if (val == "true" || val == "on")
        {
            return set_value(true);
        }
        else if (val == "false" || val == "off")
        {
            return set_value(false);
        }
        else
        {
            return false;
        }
    }

    return true;
}

void Internal::impulse_extended(string pattern)
{
    /* Extended impulse to do blinking.
         * It uses a pattern like this one:
         * - "<on_time> <off_time>"
         * - "loop <on_time> <off_time>"
         * - "old" (switch to the old value)
         * they can be combined together to create different blinking effects
         */

    DELETE_NULL(timer);
    blinks.clear();

    cInfoDom("output") << get_param("id") << ": got extended impulse action, parsing blinking pattern...";

    //Parse the string
    vector<string> tokens;
    split(pattern, tokens);

    bool state = true;
    int loop = -1;
    for (uint i = 0;i < tokens.size();i++)
    {
        if (is_of_type<int>(tokens[i]))
        {
            int blinktime;
            from_string(tokens[i], blinktime);

            BlinkInfo binfo;
            binfo.state = state;
            binfo.duration = blinktime;
            binfo.next = blinks.size() + 1;

            blinks.push_back(binfo);

            cDebugDom("output") << get_param("id")
                                << ": Add blink step " << ((binfo.state)?"True":"False")
                                << " for " << binfo.duration << "ms";

            state = !state;
        }
        else if (tokens[i] == "loop" && loop < 0)
        {
            //set loop mode to the next item
            loop = blinks.size();

            cDebugDom("output") << get_param("id") << ": Loop all next steps.";
        }
        else if (tokens[i] == "old")
        {
            BlinkInfo binfo;
            binfo.state = get_value_bool();
            binfo.duration = 0;
            binfo.next = blinks.size() + 1;

            blinks.push_back(binfo);

            cDebugDom("output") << get_param("id")
                                << ": Add blink step " << ((binfo.state)?"True":"False");
        }
    }

    if (loop >= 0)
    {
        //tell the last item to loop
        if (blinks.size() > (uint)loop)
            blinks[blinks.size() - 1].next = loop;
    }

    current_blink = 0;

    if (blinks.size() > 0)
    {
        set_value(blinks[current_blink].state);

        timer = new EcoreTimer((double)blinks[current_blink].duration / 1000.,
                               (sigc::slot<void>)sigc::mem_fun(*this, &Internal::TimerImpulseExtended) );
    }
}

void Internal::TimerImpulseExtended()
{
    //Stop timer
    DELETE_NULL(timer);

    //safety checks
    if (current_blink < 0 || current_blink >= (int)blinks.size())
        return; //Stops blinking

    current_blink = blinks[current_blink].next;

    //safety checks for new value
    if (current_blink < 0 || current_blink >= (int)blinks.size())
        return; //Stops blinking

    //Set new output state
    set_value(blinks[current_blink].state);

    //restart timer
    timer = new EcoreTimer((double)blinks[current_blink].duration / 1000.,
                           (sigc::slot<void>)sigc::mem_fun(*this, &Internal::TimerImpulseExtended) );
}

void Internal::Save()
{
    if (get_param("save") != "true") return;

    switch (get_type())
    {
    case TBOOL:
        if (bvalue)
            set_param("value", "true");
        else
            set_param("value", "false");
        break;
    case TINT: set_param("value", Utils::to_string(dvalue)); break;
    case TSTRING: set_param("value", svalue); break;
    default: break;
    }

    //save value if needed
    Config::Instance().SaveValueIO(get_param("id"), get_param("value"));
}

void Internal::LoadFromConfig()
{
    if (get_param("save") != "true") return;

    string saved_value;
    if (!Config::Instance().ReadValueIO(get_param("id"), saved_value))
    {
        saved_value = get_param("value");
        if (saved_value == "")
            return;
    }

    switch (get_type())
    {
    case TBOOL:
        if (saved_value == "true")
            bvalue = true;
        else
            bvalue = false;
        break;
    case TINT:
        from_string(saved_value, dvalue);
        break;
    case TSTRING:
        svalue = saved_value;
        break;
    default: break;
    }
}

bool Internal::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:internal");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        if (key == "value" && get_param("save") != "true") continue;
        cnode->SetAttribute(key, value);
    }

    return true;
}
