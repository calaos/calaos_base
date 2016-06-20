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
#ifndef CALAOSDISCOVER_H
#define CALAOSDISCOVER_H

#include <Utils.h>

#include <Eina.h>
#include <Ecore.h>
#include <Ecore_Con.h>

#include <EcoreTimer.h>
#include <CalaosConnection.h>


class CalaosDiscover: public sigc::trackable
{
public:
    //Ecore Internal use only
    void dataGet(Ecore_Con_Server *server, void *data, int size);

private:
    std::string address;
    Ecore_Con_Server *econ;
    Ecore_Con_Server *econ_sender;

    Ecore_Event_Handler *event_handler_data_get;

    EcoreTimer *timer;

    CalaosConnection *connection;

    void loginSuccess();
    void loginFailed();
    void timerDiscover();
    void delayDel();

public:
    CalaosDiscover();
    ~CalaosDiscover();

    sigc::signal<void, std::string> server_found;
    sigc::signal<void, std::string> login_error;
};

#endif // CALAOSDISCOVER_H
