/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
#include <IntValue.h>
#include <IPC.h>
#include <Config.h>

using namespace Calaos;

Internal::Internal(Params &p):
                Input(p),
                Output(p),
                bvalue(false),
                dvalue(0.0),
                svalue("")
{
        Utils::logger("output") << Priority::INFO << "Internal::Internal(" << get_param("id") << "): Ok" << log4cpp::eol;

        if (!Input::get_params().Exists("visible")) set_param("visible", "false");
        if (!Input::get_params().Exists("rw")) set_param("rw", "false");
        if (!Input::get_params().Exists("save")) set_param("save", "false");

        LoadFromConfig();
}

Internal::~Internal()
{
        Utils::logger("output") << Priority::INFO << "Internal::~Internal(): Ok" << log4cpp::eol;
}

void Internal::force_input_bool(bool v)
{
        bvalue = v;
        EmitSignalInput();

        Save();

        string sig = "input ";
        sig += Input::get_param("id") + " ";
        if (v)
                sig += Utils::url_encode(string("state:true"));
        else
                sig += Utils::url_encode(string("state:false"));
        IPC::Instance().SendEvent("events", sig);
}

bool Internal::set_value(bool val)
{
        Utils::logger("output") << Priority::INFO << "InternalBool(" << get_param("id") << "): got action, " << ((val)?"True":"False") << log4cpp::eol;

        force_input_bool(val);

        string sig = "output ";
        sig += Input::get_param("id") + " ";
        if (val)
                sig += Utils::url_encode(string("state:true"));
        else
                sig += Utils::url_encode(string("state:false"));
        IPC::Instance().SendEvent("events", sig);

        return true;
}

void Internal::force_input_double(double v)
{
        dvalue = v;
        EmitSignalInput();

        Save();

        string sig = "input ";
        sig += Input::get_param("id") + " ";
        sig += url_encode("state:" + to_string(dvalue));
        IPC::Instance().SendEvent("events", sig);
}

bool Internal::set_value(double val)
{
        Utils::logger("output") << Priority::INFO << "InternalInt(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        force_input_double(val);

        string sig = "output ";
        sig += Input::get_param("id") + " ";
        sig += url_encode("state:" + to_string(dvalue));
        IPC::Instance().SendEvent("events", sig);

        return true;
}

void Internal::force_input_string(string v)
{
        svalue = v;
        EmitSignalInput();

        Save();

        string sig = "input ";
        sig += Input::get_param("id") + " ";
        sig += url_encode(string("state:") + to_string(svalue));
        IPC::Instance().SendEvent("events", sig);
}

bool Internal::set_value(string val)
{
        Utils::logger("output") << Priority::INFO << "InternalString(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        force_input_string(val);

        string sig = "output ";
        sig += Input::get_param("id") + " ";
        sig += url_encode(string("state:") + to_string(svalue));
        IPC::Instance().SendEvent("events", sig);

        return true;
}

void Internal::Save()
{
        if (Input::get_param("save") != "true") return;

        switch (get_type())
        {
          case TBOOL:
                  if (bvalue)
                          set_param("value", "true");
                  else
                          set_param("value", "false");
                  break;
          case TINT: set_param("value", to_string(dvalue)); break;
          case TSTRING: set_param("value", svalue); break;
          default: break;
        }

        //save value if needed
        Config::Instance().SaveValueIO(Input::get_param("id"), Input::get_param("value"));
}

void Internal::LoadFromConfig()
{
        if (Input::get_param("save") != "true") return;

        string saved_value;
        if (!Config::Instance().ReadValueIO(Input::get_param("id"), saved_value))
        {
                saved_value = Input::get_param("value");
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
