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

        cInfoDom("rule.action.push") <<  "Ok, mail is in queue";
    }

    return true;
}

void ActionPush::sendNotif()
{
    //Append history event
    HistEvent e = HistEvent::create();
    e.pic_uid = notif_pic_uid;
    e.event_type = CalaosEvent::EventPushNotification;

    Json data = {
        { "message", notif_message},
        { "pic_uid", notif_pic_uid }
    };
    e.event_raw = data.dump();

    HistLogger::Instance().appendEvent(e);

    //TODO: send push to our service
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
