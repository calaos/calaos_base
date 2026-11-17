/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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
#ifndef S_Klf200OutputShutterSmart_H
#define S_Klf200OutputShutterSmart_H

#include "OutputShutterSmart.h"
#include "Klf200Ctrl.h"

namespace Calaos
{

/* A VELUX window or roller shutter behind a KLF 200: the gateway owns the
 * position (0 = open, 100 = closed); time_up/time_down are pinned to 100 so the
 * base class position IS the percentage. */
class Klf200OutputShutterSmart: public OutputShutterSmart
{
private:
    Klf200Ctrl *ctrl = nullptr;
    Klf200Ctrl::RegistrationId registrationId = Klf200Registry::INVALID_ID;
    string host;
    int nodeId = -1;

    void nodeEventReceived(const Params &event);
    //false when the order could not be handed to the gateway link
    bool sendPosition(int percent);
    bool sendStop();

protected:
    virtual void readConfig() override;

public:
    Klf200OutputShutterSmart(Params &p);
    virtual ~Klf200OutputShutterSmart();

    virtual bool set_value(std::string val) override;
};

} //namespace Calaos

#endif
