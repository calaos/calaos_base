/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "ListeRoom.h"
#include "IPCam.h"
#include "Prefix.h"
#include "libuvw.h"
#include "UrlDownloader.h"
#include "sole.hpp"
#include "HistLogger.h"
#include "EventManager.h"

using namespace Calaos;

ActionPush::ActionPush():
    Action(ACTION_PUSH)
{
    cDebugDom("rule.action.push") <<  "New Push Notification action";
}

ActionPush::ActionPush(const string &message, const string &attachement):
    Action(ACTION_PUSH)
{
    cDebugDom("rule.action.push") <<  "New Push Notification action with message/attachment";
    notif_message = message;
    notif_attachment = attachement;
}

ActionPush::~ActionPush()
{
}

bool ActionPush::Execute()
{
    IPCam *camera = NULL;

    if (notif_attachment != "")
        camera = dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(notif_attachment));

    if (camera)
    {
        cInfoDom("rule.action.push") <<  "Need to download camera ("
                                      << camera->get_param("name")
                                      << ") attachment";

        sole::uuid u4 = sole::uuid4();
        notif_pic_uid = u4.str();

        //Get a filename
        notif_attachment_tfile = Utils::getCacheFile("push_pictures");
        mkdir(notif_attachment_tfile.c_str(), S_IRWXU);
        notif_attachment_tfile = notif_attachment_tfile + "/" + notif_pic_uid + ".jpg";

        cDebug() << "DL URL: " << camera->getPictureUrl();

        UrlDownloader *dl = new UrlDownloader(camera->getPictureUrl(), true);
        dl->m_signalComplete.connect([this](int status)
        {
            if (status < 20 || status >= 300)
                notif_attachment_tfile.clear();
            this->sendNotif();
        });
        dl->httpGet(notif_attachment_tfile);
    }
    else
    {
        sendNotif();

        cInfoDom("rule.action.push") <<  "Ok, Push Notif sent";
    }

    return true;
}

void ActionPush::sendNotif()
{
    //Append history event
    HistEvent e = HistEvent::create();
    e.pic_uid = notif_pic_uid;
    e.event_type = CalaosEvent::EventPushNotification;

    string eventUuid = e.uuid;

    auto nmsg = notif_message;
    if (nmsg == "")
        nmsg = "Calaos Notification";

    Json data = {
        { "message", nmsg},
        { "pic_uid", notif_pic_uid }
    };
    e.event_raw = data.dump();

    HistLogger::Instance().appendEvent(e);

    HistLogger::Instance().getPushTokens(
            [=](bool success, string errorMsg, const vector<PushToken> &tokens)
    {
        if (!success)
        {
            cCriticalDom("rule.action.push") << "Unable to send push notification: " << errorMsg;
            return;
        }

        vector<PushToken> androidTokens;
        vector<PushToken> iosTokens;

        for (const auto &t: tokens)
        {
            if (t.hw_type == HistLogger::PUSH_HW_IOS)
                iosTokens.push_back(t);
            else if (t.hw_type == HistLogger::PUSH_HW_ANDROID)
                androidTokens.push_back(t);
        }

        Json jarr = Json::array();
        if (!iosTokens.empty())
        {
            Json tokArray = Json::array();
            for (const auto &t: iosTokens)
                tokArray.push_back(t.token);

            Json notif = {
                { "tokens", tokArray },
                { "platform", 1 },
                { "message", nmsg },
                { "topic", "fr.calaos.CalaosMobile" }
            };

            if (!notif_pic_uid.empty())
            {
                notif["mutable-content"] = true;
                notif["mutable_content"] = true;
                notif["data"] = {{ "event_uuid", eventUuid }};
            }

            if (Utils::get_config_option("notif_development") == "true")
                notif["development"] = true;

            jarr.push_back(notif);
        }

        if (!androidTokens.empty())
        {
            Json tokArray = Json::array();
            for (const auto &t: androidTokens)
                tokArray.push_back(t.token);

            Json notif = {
                { "tokens", tokArray },
                { "platform", 2 },
                { "message", nmsg },
                { "title", "Calaos" }
            };

            if (!notif_pic_uid.empty())
            {
                notif["data"] = {{ "event_uuid", eventUuid }};
            }

            jarr.push_back(notif);
        }

        Json jnotif = {{ "notifications", jarr }};

        if (jarr.empty())
        {
            cInfoDom("rule.action.push") << "No token registered. Cannot send push.";
            return;
        }

        cDebugDom("rule.action.push") << "Sending notif to service:";
        cDebugDom("rule.action.push") << jnotif.dump();

        UrlDownloader *u = new UrlDownloader("https://push.calaos.fr/api/push", true);
        u->m_signalComplete.connect([this](int statusCode)
        {
            cDebugDom("rule.action.push") << "Push notif sent with code: " << statusCode;
            notifSent.emit();
        });
        u->httpPost(string(), jnotif.dump());
    });
}

bool ActionPush::LoadFromXml(TiXmlElement *pnode)
{
    TiXmlElement *notif_node = pnode->FirstChildElement("calaos:push");
    if (!notif_node) return false;

    if (notif_node->Attribute("attachment")) notif_attachment = notif_node->Attribute("attachment");

    TiXmlText *tnode = dynamic_cast<TiXmlText *>(notif_node->FirstChild());

    if (tnode)
        notif_message = tnode->ValueStr();

    return true;
}

bool ActionPush::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *action_node = new TiXmlElement("calaos:action");
    action_node->SetAttribute("type", "push");
    node->LinkEndChild(action_node);

    TiXmlElement *notif_node = new TiXmlElement("calaos:push");
    notif_node->SetAttribute("attachment", notif_attachment);
    action_node->LinkEndChild(notif_node);

    TiXmlText *txt_node = new TiXmlText(notif_message);
    txt_node->SetCDATA(true);
    notif_node->LinkEndChild(txt_node);

    return true;
}
