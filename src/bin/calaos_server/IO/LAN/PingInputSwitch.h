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
#ifndef PINGINPUTSWITCH_H
#define PINGINPUTSWITCH_H

#include "InputSwitch.h"
#include <Ecore.h>

namespace Calaos {

class PingInputSwitch: public InputSwitch
{
protected:
    virtual bool readValue();

    bool lastStatus = false;
    Ecore_Exe *ping_exe = nullptr;
    Ecore_Event_Handler *hProcDel;

    void doPing();

    friend Eina_Bool PingInputSwitch_proc_del(void *data, int type, void *event);

public:
    PingInputSwitch(Params &p);
    virtual ~PingInputSwitch();
};

}
  
#endif // PINGINPUTSWITCH_H
