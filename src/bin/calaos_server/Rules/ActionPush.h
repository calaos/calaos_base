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
#ifndef S_ACTIONPUSH_H
#define S_ACTIONPUSH_H

#include "Calaos.h"
#include "Action.h"

namespace Calaos
{

class ActionCameraDownload;
class IPCam;

class ActionPush: public Action
{
private:
    string notif_attachment;
    string notif_message;

    /* Owns the completion slots of the camera picture downloads still in
     * flight. Destroying the action destroys them, which disconnects the slots
     * and drops the files they were writing to: a transfer still running can
     * never call back into a freed action.
     * See ActionCameraDownload.h
     */
    std::unique_ptr<ActionCameraDownload> camDownload;

    /* Lifetime token for the asynchronous "push sent" callback, which is a
     * plain std::function owned by NotifManager and offers no connection to
     * disconnect. The callback holds a weak_ptr on it: when the action dies the
     * token expires and the callback becomes a no-op.
     */
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    /* Starts the download of the camera snapshot to attach. Returns false when
     * the notification has to be sent right away, without any picture.
     */
    bool startPictureDownload(IPCam *camera);

    //picUid is empty when the notification carries no picture
    void sendNotif(const string &picUid);

public:
    ActionPush();
    ActionPush(const string &message, const string &attachement);
    ~ActionPush();

    bool Execute();

    bool LoadFromXml(TiXmlElement *node) override;
    bool SaveToXml(TiXmlElement *node) override;

    sigc::signal<void> notifSent;
};

}
#endif
