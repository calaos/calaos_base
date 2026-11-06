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
#include "ActionPush.h"
#include "ActionCameraDownload.h"
#include "Prefix.h"
#include "libuvw.h"
#include "sole.hpp"
#include "HistLogger.h"
#include "EventManager.h"
#include "NotifManager.h"

using namespace Calaos;

static const char *TAG = "rule.action.push";

ActionPush::ActionPush():
    Action(ACTION_PUSH),
    camDownload(std::make_unique<ActionCameraDownload>())
{
    cDebugDom(TAG) <<  "New Push Notification action";
}

ActionPush::ActionPush(const string &message, const string &attachement):
    Action(ACTION_PUSH),
    camDownload(std::make_unique<ActionCameraDownload>())
{
    cDebugDom(TAG) <<  "New Push Notification action with message/attachment";
    notif_message = message;
    notif_attachment = attachement;
}

ActionPush::~ActionPush()
{
}

bool ActionPush::Execute()
{
    IPCam *camera = ActionCameraDownload::findCamera(notif_attachment);

    if (camera && startPictureDownload(camera))
        return true;

    sendNotif("");

    cInfoDom(TAG) <<  "Ok, Push Notif sent";

    return true;
}

bool ActionPush::startPictureDownload(IPCam *camera)
{
    //The picture is served to the mobile clients by its uid, which is also what
    //names it on disk
    string picUid = sole::uuid4().str();

    string dir = Utils::getCacheFile("push_pictures");
    mkdir(dir.c_str(), S_IRWXU);

    /* The completion slot is owned by camDownload: destroying this action
     * disconnects it before the captured `this` can go stale, and unlinks the
     * picture of a transfer nobody is waiting for anymore.
     * Every call gets its own uid, its own file and its own slot, so a rule
     * triggering again while a first snapshot is still downloading sends both
     * notifications, each with its own picture.
     */
    return camDownload->start(camera, dir + "/" + picUid + ".jpg",
                              [this, picUid](bool success, const string &file)
    {
        if (!success)
        {
            //Whatever curl left in there is unusable, drop it and send the
            //notification without a picture
            FileUtils::unlink(file);
            sendNotif("");
            return;
        }

        sendNotif(picUid);
    });
}

void ActionPush::sendNotif(const string &picUid)
{
    //Append history event
    HistEvent e = HistEvent::create();
    e.pic_uid = picUid;
    e.event_type = CalaosEvent::EventPushNotification;

    auto nmsg = notif_message;
    if (nmsg == "")
        nmsg = "Calaos Notification";

    Json data = {
        { "message", nmsg},
        { "pic_uid", picUid }
    };
    //E4.1b: this string is PERSISTED in the history database and read back by
    //HistEvent::toJson(), which Json::parse()s it - so escaping here is
    //invisible on the API, while a throw here would kill the process on a rule
    //firing with a non UTF-8 message.
    e.event_raw = data.dump(-1, ' ', true, Json::error_handler_t::replace);

    auto notif_pic_uuid = picUid.empty() ? "" : e.uuid;

    HistLogger::Instance().appendEvent(e);

    //The callback is called back asynchronously (push tokens are read from the
    //history database first), and this action may well be destroyed before that
    //happens. Guard it with the lifetime token instead of a raw `this`.
    NotifManager::Instance().sendPushNotification(
        nmsg, notif_pic_uuid,
        [this, token = std::weak_ptr<bool>(alive)]()
        {
            if (token.expired())
                return; //action destroyed while the push was in flight

            cDebugDom(TAG) << "Push notif sent";
            notifSent.emit();
        }
    );
}

bool ActionPush::LoadFromXml(pugi::xml_node pnode)
{
    pugi::xml_node notif_node = pnode.child("calaos:push");
    if (!notif_node) return false;

    if (notif_node.attribute("attachment")) notif_attachment = notif_node.attribute("attachment").as_string();

    if (XmlUtils::hasText(notif_node))
        notif_message = XmlUtils::text(notif_node);

    return true;
}

bool ActionPush::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node action_node = node.append_child("calaos:action");
    XmlUtils::setAttribute(action_node, "type", "push");

    pugi::xml_node notif_node = action_node.append_child("calaos:push");
    XmlUtils::setAttribute(notif_node, "attachment", notif_attachment);

    XmlUtils::appendCData(notif_node, notif_message);

    return true;
}
