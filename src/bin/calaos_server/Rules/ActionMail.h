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
class IPCam;

class ActionMail: public Action
{
private:
    string mail_sender;
    string mail_recipients;
    string mail_subject;
    string mail_attachment;
    string mail_message;

    /* Owns the completion slots of the camera attachment downloads still in
     * flight. Destroying the action destroys them, which disconnects the slots
     * and drops the files they were writing to: a transfer still running can
     * never call back into a freed action.
     * See ActionCameraDownload.h
     */
    std::unique_ptr<ActionCameraDownload> camDownload;

    /* Starts the download of the camera snapshot to attach. Returns false when
     * the mail has to be sent right away, without any attachment.
     */
    bool startAttachmentDownload(IPCam *camera);

    //attachmentFile is empty when there is nothing to attach
    void sendMail(const string &attachmentFile);

public:
    ActionMail();
    ~ActionMail();

    bool Execute();

    bool LoadFromXml(TiXmlElement *node) override;
    bool SaveToXml(TiXmlElement *node) override;
};

}
#endif
