/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#ifndef WOLOutputBool_H
#define WOLOutputBool_H

#include "IOBase.h"
#include <Ecore.h>
#include <Ecore_Con.h>
#include "EcoreTimer.h"

using namespace Calaos;

class WOLOutputBool: public IOBase
{
protected:
    bool value = false;

    Ecore_Con_Server *udp_con = nullptr;
    Ecore_Event_Handler *hwritten = nullptr;
    Ecore_Event_Handler *herr = nullptr;
    int data_size = 0;

    EcoreTimer *timerState = nullptr;

    friend Eina_Bool WOLOutputBool_con_data_written(void *data, int type, void *event);
    friend Eina_Bool WOLOutputBool_con_data_error(void *data, int type, void *event);

    void timerTimeout();

    void doWakeOnLan();

public:
    WOLOutputBool(Params &p);
    virtual ~WOLOutputBool();

    DATA_TYPE get_type() { return TBOOL; }

    virtual bool set_value(bool val);
    virtual bool get_value_bool() { return value; }
    virtual bool set_value(string val);
};

#endif // PINGINPUTSWITCH_H
