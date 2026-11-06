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

    if (camera && startAttachmentDownload(camera))
        return true;

    sendMail("");

    return true;
}

bool ActionMail::startAttachmentDownload(IPCam *camera)
{
    //Get a temporary filename
    string tfile = Utils::getTmpFilename("tmp", "_mail_attachment");

    if (tfile.empty())
    {
        //getTmpFilename() already logged why. The mail is worth more than its
        //attachment, so it goes out without one instead of being dropped.
        cWarningDom(TAG) << "No temporary file available, sending the mail without its attachment";
        return false;
    }

    /* The completion slot is owned by camDownload: destroying this action
     * disconnects it before the captured `this` can go stale, and unlinks the
     * temp file of a transfer nobody is waiting for anymore.
     * Every call gets its own file and its own slot, so a rule triggering
     * again while a first snapshot is downloading sends both mails.
     */
    return camDownload->start(camera, tfile, [this](bool success, const string &file)
    {
        if (!success)
        {
            //Whatever curl left in there is unusable, drop it
            FileUtils::unlink(file);
            sendMail("");
            return;
        }

        //calaos_mail is spawned with --delete: it unlinks the attachment once
        //the mail has been sent
        sendMail(file);
    });
}

void ActionMail::sendMail(const string &attachmentFile)
{
    NotifManager::Instance().sendMailNotification(mail_subject,
                                                  mail_message,
                                                  mail_recipients,
                                                  mail_sender,
                                                  attachmentFile);
}

bool ActionMail::LoadFromXml(pugi::xml_node pnode)
{
    pugi::xml_node mail_node = pnode.child("calaos:mail");
    if (!mail_node) return false;

    if (mail_node.attribute("sender")) mail_sender = mail_node.attribute("sender").as_string();
    if (mail_node.attribute("recipients")) mail_recipients = mail_node.attribute("recipients").as_string();
    if (mail_node.attribute("subject")) mail_subject = mail_node.attribute("subject").as_string();
    if (mail_node.attribute("attachment")) mail_attachment = mail_node.attribute("attachment").as_string();

    //remove spaces
    replace_str(mail_recipients, " ", "");

    if (XmlUtils::hasText(mail_node))
        mail_message = XmlUtils::text(mail_node);

    return true;
}

bool ActionMail::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node action_node = node.append_child("calaos:action");
    XmlUtils::setAttribute(action_node, "type", "mail");

    pugi::xml_node mail_node = action_node.append_child("calaos:mail");
    XmlUtils::setAttribute(mail_node, "sender", mail_sender);
    XmlUtils::setAttribute(mail_node, "recipients", mail_recipients);
    XmlUtils::setAttribute(mail_node, "subject", mail_subject);
    XmlUtils::setAttribute(mail_node, "attachment", mail_attachment);

    XmlUtils::appendCData(mail_node, mail_message);

    return true;
}
