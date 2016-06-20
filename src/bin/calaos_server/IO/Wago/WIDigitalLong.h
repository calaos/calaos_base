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
#ifndef S_WIDigitalLong_H
#define S_WIDigitalLong_H

#include <InputSwitchLongPress.h>
#include <WagoMap.h>

namespace Calaos
{

class WIDigitalLong : public InputSwitchLongPress, public sigc::trackable
{
protected:
    type_signal_wago::iterator iter;

    int address;
    std::string host;
    int port;

    bool udp_value;

    void WagoReadCallback(bool status, UWord address, int count, std::vector<bool> &values);

    virtual bool readValue();

public:
    WIDigitalLong(Params &p);
    ~WIDigitalLong();

    virtual void ReceiveFromWago(std::string ip, int addr, bool val, std::string intype);
};

}

#endif
