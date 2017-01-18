/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include <X10Output.h>
#include <IOFactory.h>
#include "uvw/src/uvw.hpp"

using namespace Calaos;

REGISTER_IO(X10Output)

X10Output::X10Output(Params &p):
    OutputLightDimmer(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("X10Output");
    ioDoc->descriptionSet(_("Light dimmer using X10 and heyu."));
    ioDoc->paramAdd("code", _("House code of the X10 light device"), IODoc::TYPE_STRING, true);

    housecode = get_param("code");
    state_value = X10Command("onstate");
    int _val;
    X10Command("dimstate", &_val);
    _val = 22 - _val + 1;
    if (state_value)
    {
        value = (int)((double)(_val * 101.) / 22.);
        value--;
    }
    else
    {
        old_value = (int)((double)(_val * 101.) / 22.);
        old_value--;
    }

    cDebugDom("output") << get_param("id") << ": Ok";
}

X10Output::~X10Output()
{
}

bool X10Output::set_on_real()
{
    return X10Command("on");
}

bool X10Output::set_off_real()
{
    return X10Command("off");
}

bool X10Output::set_dim_up_real(int percent)
{
    int v = (int)(((double)(percent + 1.) * 22.) / 101.);
    v = 22 - v + 1;
    if (v < 1) v = 1;
    if (v > 22) v = 22;
    return X10Command("bright", &v);
}

bool X10Output::set_dim_down_real(int percent)
{
    int v = (int)(((double)(percent + 1.) * 22.) / 101.);
    v = 22 - v + 1;
    if (v < 1) v = 1;
    if (v > 22) v = 22;
    return X10Command("dim", &v);
}

bool X10Output::set_value_real(int val)
{
    int v = (int)(((double)(val + 1.) * 22.) / 101.);
    v = 22 - v + 1;
    if (v < 1) v = 1;
    if (v > 22) v = 22;
    return X10Command("dimb", &v);
}

void X10Output::execCmd(const string &cmd)
{
    exe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    exe->once<uvw::ExitEvent>([](const uvw::ExitEvent &, auto &h) { h.close(); });
    exe->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &ev, auto &h)
    {
        cDebugDom("process") << "Process error: " << ev.what();
        h.close();
    });

    Utils::CStrArray arr(cmd);
    exe->spawn(arr.at(0), arr.data());
}

bool X10Output::X10Command(std::string cmd, int *dval)
{
    if (cmd == "on")
    {
        string cmd_line = "heyu on " + housecode;
        execCmd(cmd_line);
    }
    else if (cmd == "off")
    {
        string cmd_line = "heyu off " + housecode;
        execCmd(cmd_line);
    }
    else if (cmd == "dimb")
    {
        string cmd_line = "heyu dimb " + housecode + " " + Utils::to_string(*dval);
        execCmd(cmd_line);
    }
    else if (cmd == "bright")
    {
        string cmd_line = "heyu bright " + housecode + " " + Utils::to_string(*dval);
        execCmd(cmd_line);
    }
    else if (cmd == "dim")
    {
        string cmd_line = "heyu dim " + housecode + " " + Utils::to_string(*dval);
        execCmd(cmd_line);
    }
//    else if (cmd == "onstate")
//    {
//        //setup params for heyu
//        string _cmd = "heyu onstate " + housecode;
//        string std_out;
//        int _ret = -1;

//        //TO FIX: Need to fix that using ecore_exe...
//        //Glib::spawn_command_line_sync(_cmd, &std_out, NULL, &_ret);

//        if (_ret != 0)
//        {
//            //reload the heyu engine
//            string cmd_line = "heyu engine";
//            ecore_exe_run(cmd_line.c_str(), NULL);

//            //then try again
//            //      Glib::spawn_command_line_sync(_cmd, &std_out, NULL, &_ret);
//        }

//        if (std_out.empty()) return false;
//        if (std_out.compare(0, 1, "1") == 0)
//            return true;
//        else
//            return false;
//    }
//    else if (cmd == "dimstate")
//    {
//        //setup params for heyu
//        string _cmd = "heyu dimstate " + housecode;
//        string std_out;
//        int _ret = -1;

//        //TO FIX: Need to fix that using ecore_exe...
//        //Glib::spawn_command_line_sync(_cmd, &std_out, NULL, &_ret);

//        if (_ret != 0)
//        {
//            //reload the heyu engine
//            string cmd_line = "heyu engine";
//            ecore_exe_run(cmd_line.c_str(), NULL);

//            //then try again
//            //Glib::spawn_command_line_sync(_cmd, &std_out, NULL, &_ret);
//        }

//        *dval = 22;
//        if (std_out.empty()) return false;
//        *dval = atoi(std_out.c_str());
//    }

    return true;
}

