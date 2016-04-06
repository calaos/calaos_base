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
#ifndef S_OWCTRL_H
#define S_OWCTRL_H

#include "Calaos.h"
#include "ExternProc.h"

namespace Calaos {

class OwCtrl: public sigc::trackable
{
private:    
    OwCtrl(const std::string &args);

    ExternProcServer *process;
    std::unordered_map<std::string, std::string> mapValues;
    std::unordered_map<std::string, std::string> mapTypes;
    std::string exe;

    void processNewMessage(const std::string &msg);

public:
    static std::shared_ptr<OwCtrl> Instance(const std::string &args);
    ~OwCtrl();

    std::string getValue(std::string owid);
    std::string getType(std::string owid);

    sigc::signal<void> valueChanged;
};

}

#endif
