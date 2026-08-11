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
#include "ActionMail.h"
#include "ActionCameraDownload.h"
#include "NotifManager.h"

using namespace Calaos;

static const char *TAG = "rule.action.mail";

ActionMail::ActionMail():
    Action(ACTION_MAIL),
    camDownload(std::make_unique<ActionCameraDownload>())
{
    cDebugDom(TAG) <<  "New Mail action";
}

ActionMail::~ActionMail()
{
}

bool ActionMail::Execute()
{
    IPCam *camera = ActionCameraDownload::findCamera(mail_attachment);

    if (camera)
    {
        //Get a temporary filename
        mail_attachment_tfile = Utils::getTmpFilename("tmp", "_mail_attachment");

        //The completion slot is owned by camDownload, destroying this action
        //disconnects it before the captured `this` can go stale.
        if (camDownload->start(camera, mail_attachment_tfile,
                               [this](bool success)
                               {
                                   if (!success)
                                       mail_attachment_tfile.clear();
                                   sendMail();
                               }))
            return true;

        //Download could not be started, send the mail without attachment
        mail_attachment_tfile.clear();
    }

    sendMail();

    return true;
}

void ActionMail::sendMail()
{
    NotifManager::Instance().sendMailNotification(mail_subject,
                                                  mail_message,
                                                  mail_recipients,
                                                  mail_sender,
                                                  mail_attachment_tfile);
}

bool ActionMail::LoadFromXml(TiXmlElement *pnode)
{
    TiXmlElement *mail_node = pnode->FirstChildElement("calaos:mail");
    if (!mail_node) return false;

    if (mail_node->Attribute("sender")) mail_sender = mail_node->Attribute("sender");
    if (mail_node->Attribute("recipients")) mail_recipients = mail_node->Attribute("recipients");
    if (mail_node->Attribute("subject")) mail_subject = mail_node->Attribute("subject");
    if (mail_node->Attribute("attachment")) mail_attachment = mail_node->Attribute("attachment");

    //remove spaces
    replace_str(mail_recipients, " ", "");

    TiXmlText *tnode = dynamic_cast<TiXmlText *>(mail_node->FirstChild());

    if (tnode)
        mail_message = tnode->ValueStr();

    return true;
}

bool ActionMail::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *action_node = new TiXmlElement("calaos:action");
    action_node->SetAttribute("type", "mail");
    node->LinkEndChild(action_node);

    TiXmlElement *mail_node = new TiXmlElement("calaos:mail");
    mail_node->SetAttribute("sender", mail_sender);
    mail_node->SetAttribute("recipients", mail_recipients);
    mail_node->SetAttribute("subject", mail_subject);
    mail_node->SetAttribute("attachment", mail_attachment);
    action_node->LinkEndChild(mail_node);

    TiXmlText *txt_node = new TiXmlText(mail_message);
    txt_node->SetCDATA(true);
    mail_node->LinkEndChild(txt_node);

    return true;
}
