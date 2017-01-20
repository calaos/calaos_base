/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "ListeRoom.h"
#include "IPCam.h"
#include "Prefix.h"
#include "uvw/src/uvw.hpp"
#include "UrlDownloader.h"

using namespace Calaos;

ActionMail::ActionMail():
    Action(ACTION_MAIL)
{
    cDebugDom("rule.action.mail") <<  "New Mail action";
}

ActionMail::~ActionMail()
{
}

bool ActionMail::Execute()
{
    IPCam *camera = NULL;

    if (mail_attachment != "")
        camera = dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(mail_attachment));

    if (camera)
    {
        cInfoDom("rule.action.mail") <<  "Need to download camera ("
                                      << camera->get_param("name")
                                      << ") attachment";

        //Get a temporary filename
        mail_attachment_tfile = Utils::getTmpFilename("tmp", "_mail_attachment");

        cDebug() << "DL URL: " << camera->getPictureUrl();

        UrlDownloader *dl = new UrlDownloader(camera->getPictureUrl(), true);
        dl->m_signalComplete.connect([this](int status)
        {
            if (status < 20 || status >= 300)
                mail_attachment_tfile.clear();
            this->sendMail();
        });
        dl->httpGet(mail_attachment_tfile);
    }
    else
    {
        sendMail();

        cInfoDom("rule.action.mail") <<  "Ok, mail is in queue";
    }

    return true;
}

void ActionMail::sendMail()
{
    //Get a temporary filename
    string tmpFile = Utils::getTmpFilename("tmp", "_mail_body");

    //Write body message to a temp file
    std::ofstream ofs;
    ofs.open(tmpFile.c_str(), std::ofstream::trunc);
    ofs << mail_message;
    ofs.close();

    vector<string> cmd;
    cmd.push_back(Prefix::Instance().binDirectoryGet() + "/calaos_mail");
    cmd.push_back("--delete"); //force temp file deletion after mail is sent
    if (Utils::get_config_option("smtp_debug") == "true")
        cmd.push_back("--verbose");
    cmd.push_back("--from");
    cmd.push_back(mail_sender);
    cmd.push_back("--to");
    cmd.push_back(mail_recipients);
    cmd.push_back("--subject");
    cmd.push_back(mail_subject);
    cmd.push_back("--body");
    cmd.push_back(tmpFile);

    if (!mail_attachment_tfile.empty())
    {
        cmd.push_back("--attach");
        cmd.push_back(mail_attachment_tfile);
    }

    auto exe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    exe->once<uvw::ExitEvent>([this, exe](const uvw::ExitEvent &ev, auto &) { exe->close(); });
    exe->once<uvw::ErrorEvent>([this, exe](const uvw::ErrorEvent &ev, auto &)
    {
        cDebugDom("rule.action.mail") << "Process error: " << ev.what();
        exe->close();
    });

    Utils::CStrArray arr(cmd);
    cInfoDom("rule.action.mail") << "Executing command: " << arr.toString();
    exe->spawn(arr.at(0), arr.data());
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
