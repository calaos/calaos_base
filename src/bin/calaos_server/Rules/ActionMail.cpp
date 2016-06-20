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
#include "ActionMail.h"
#include "ListeRoom.h"
#include "IPCam.h"
#include "FileDownloader.h"
#include "Prefix.h"


namespace Calaos {
ActionMail::ActionMail():
    Action(ACTION_MAIL)
{
    cDebugDom("rule.action.mail") <<  "New Mail action";
}

ActionMail::~ActionMail()
{
    cDebugDom("rule.action.mail");
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

        std::string tmpFile;
        int cpt = 0;

        //Get a temporary filename
        do
        {
            tmpFile = "/tmp/calaos_mail_attachment_";
            tmpFile += Utils::to_string(cpt);
            cpt++;
        }
        while (ecore_file_exists(tmpFile.c_str()));

        // Autodestroy file downloader
        cDebug() << "DL URL: " << camera->getPictureUrl();
        FileDownloader* downloader = new FileDownloader(camera->getPictureUrl(), tmpFile, true);
        downloader->addCallback([=](std::string signal, void *sender_data)
        {
            if (signal == "done")
            {
                mail_attachment_tfile = *(reinterpret_cast<std::string *>(sender_data));
                sendMail();
            }
            else if (signal == "failed" || signal == "aborted")
            {
                mail_attachment_tfile.clear();
                sendMail();
            }
        });
        downloader->Start();
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
    std::string tmpFile;
    int cpt = 0;
    //Get a temporary filename
    do
    {
        tmpFile = "/tmp/calaos_mail_body_";
        tmpFile += Utils::to_string(cpt);
        cpt++;
    }
    while (ecore_file_exists(tmpFile.c_str()));

    //Write body message to a temp file
    std::ofstream ofs;
    ofs.open(tmpFile.c_str(), std::ofstream::trunc);
    ofs << mail_message;
    ofs.close();

    std::stringstream cmd;

    cmd << Prefix::Instance().binDirectoryGet();
    cmd << "/calaos_mail";

    if (ecore_file_exists(cmd.str().c_str()))
    {
        cmd << " ";
        cmd << "--delete "; //force temp file deletion after mail is sent
        if (Utils::get_config_option("smtp_debug") == "true")
            cmd << "--verbose ";
        cmd << "--from \"" << mail_sender << "\" ";
        cmd << "--to \"" << mail_recipients << "\" ";
        cmd << "--subject \"" << mail_subject << "\" ";
        cmd << "--body " << tmpFile << " ";

        if (!mail_attachment_tfile.empty())
            cmd << "--attach " << mail_attachment_tfile;

        cInfo() << "Executing command : " << cmd.str();
        ecore_exe_run(cmd.str().c_str(), NULL);
    }
    else
    {
        cError() << "Command " << cmd.str() << " not found";
    }
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

}
