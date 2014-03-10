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
#include "EcoreFdHandler.h"

static Eina_Bool _calaos_fdhandler_event(void *data, Ecore_Fd_Handler *fd_handler)
{
    CalaosEcoreFdHandler *efdhandler = reinterpret_cast<CalaosEcoreFdHandler *>(data);
    efdhandler->Tick();

    return EINA_TRUE;
}

CalaosEcoreFdHandler::CalaosEcoreFdHandler(int _fd, sigc::slot<void, void *> slot, void *d):
    fdhandler(NULL), fd(_fd), fdhandler_data(true), data(d)
{
    Ecore_Fd_Handler_Flags flags;

    flags = static_cast<Ecore_Fd_Handler_Flags>(ECORE_FD_READ |  ECORE_FD_WRITE |  ECORE_FD_ERROR);
    //connect the sigc slot
    connection_data = event_signal_data.connect(slot);

    //create the ecore fdhandler
    fdhandler = ecore_main_fd_handler_add(fd, flags,
                                          _calaos_fdhandler_event, this, NULL, NULL);
}

CalaosEcoreFdHandler::CalaosEcoreFdHandler(int _fd, sigc::slot<void> slot):
    fdhandler(NULL), fd(_fd), fdhandler_data(false)
{
    Ecore_Fd_Handler_Flags flags;

    flags = static_cast<Ecore_Fd_Handler_Flags>(ECORE_FD_READ |  ECORE_FD_WRITE |  ECORE_FD_ERROR);

    //connect the sigc slot
    connection = event_signal.connect(slot);

    //create the ecore fdhandler
    fdhandler = ecore_main_fd_handler_add(fd,  flags,
                                          _calaos_fdhandler_event, this, NULL, NULL);
}

CalaosEcoreFdHandler::~CalaosEcoreFdHandler()
{
    //disconnect the sigc slot
    if (!fdhandler_data)
        connection.disconnect();
    else
        connection_data.disconnect();

    //delete the ecore fdhandler
    if (fdhandler) ecore_main_fd_handler_del(fdhandler);
}

void CalaosEcoreFdHandler::Tick()
{
    if (fdhandler_data)
        event_signal_data.emit(data);
    else
        event_signal.emit();
}

