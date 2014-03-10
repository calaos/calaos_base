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
#ifndef S_UTILS_CALAOS_H
#define S_UTILS_CALAOS_H

#include <sigc++/sigc++.h>
#include <Utils.h>
#include <config.h>

#include <Ecore_File.h>

using namespace Utils;

namespace Calaos
{

typedef struct _BlinkInfo
{
    bool state;
    int duration;
    int next;
} BlinkInfo;

void CallUrl(string url, string post_data = "");
#ifndef UTILS
std::string get_new_id(std::string prefix);
std::string get_new_scenario_id();
#endif

//This class only counts all IO at start and wait for them
//to read their values. After all values are read, it calls ExecuteStartRules
class StartReadRules
{
private:
    int count_io;

    StartReadRules();

public:
    static StartReadRules &Instance()
    {
        static StartReadRules st;
        return st;
    }

    void addIO();
    void ioRead();
};
}

namespace Utils
{
typedef sigc::signal<void, std::string, int, bool, std::string> type_signal_wago;
extern type_signal_wago signal_wago;
}

#endif
