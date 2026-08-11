/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#ifndef S_ACTIONMAIL_H
#define S_ACTIONMAIL_H

#include "Calaos.h"
#include "Action.h"

namespace Calaos
{

class ActionCameraDownload;

class ActionMail: public Action
{
private:
    string mail_sender;
    string mail_recipients;
    string mail_subject;
    string mail_attachment;
    string mail_message;
    string mail_attachment_tfile;

    /* Owns the completion slot of a running camera attachment download.
     * Destroying the action destroys it, which disconnects the slot: a download
     * still in flight can never call back into a freed action.
     * See ActionCameraDownload.h
     */
    std::unique_ptr<ActionCameraDownload> camDownload;

    void sendMail();

public:
    ActionMail();
    ~ActionMail();

    bool Execute();

    bool LoadFromXml(TiXmlElement *node);
    bool SaveToXml(TiXmlElement *node);
};

}
#endif
