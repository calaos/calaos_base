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
#include "NotifManager.h"
#include "UrlDownloader.h"
#include "HistLogger.h"
#include "Prefix.h"
#include "libuvw.h"

using namespace Calaos;

static const char *TAG = "notif";

NotifManager::NotifManager()
{
    cDebugDom(TAG) << "NotifManager initialized";
}

void NotifManager::sendMailNotification(const string &subject, const string &messageBody,
                                        const string &to, const string &from, const string &attachmentFile)
{
    cDebugDom(TAG) << "Sending mail notification with subject: " << subject;

    //Get a temporary filename
    string tmpFile = Utils::getTmpFilename("tmp", "_mail_body");

    //Write body message to a temp file
    std::ofstream ofs;
    ofs.open(tmpFile.c_str(), std::ofstream::trunc);
    ofs << messageBody;
    ofs.close();

    string mail_sender, mail_recipients;

    if (from.empty())
    {
        // Get default sender email from config
        mail_sender = Utils::get_config_option("notif/mail_sender");
        if (mail_sender.empty())
        {
            cWarningDom(TAG) << "No sender email configured, using default 'calaos@localhost'";
            mail_sender = "calaos@localhost";
        }
    }
    else
    {
        mail_sender = from;
    }

    if (to.empty())
    {
        // Get default recipient email from config
        mail_recipients = Utils::get_config_option("notif/mail_recipients");
        if (mail_recipients.empty())
        {
            cCriticalDom(TAG) << "No recipient email configured, cannot send mail !";
            return; // Cannot send mail without recipient
        }
    }
    else
    {
        mail_recipients = to;
    }

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
    cmd.push_back(subject);
    cmd.push_back("--body");
    cmd.push_back(tmpFile);

    if (!attachmentFile.empty())
    {
        cmd.push_back("--attach");
        cmd.push_back(attachmentFile);
    }

    auto exe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    exe->once<uvw::ExitEvent>([exe](const uvw::ExitEvent &ev, auto &) { exe->close(); });
    exe->once<uvw::ErrorEvent>([exe](const uvw::ErrorEvent &ev, auto &)
    {
        cWarningDom(TAG) << "Process error: " << ev.what();
        exe->close();
    });

    Utils::CStrArray arr(cmd);
    cDebugDom(TAG) << "Executing command: " << arr.toString();
    exe->spawn(arr.at(0), arr.data());
}

void NotifManager::sendPushNotification(const string &message, const string &notifPicUuid, std::function<void(void)> callbackSent)
{
    cDebugDom(TAG) << "Sending push notification with message: " << message;

    auto nmsg = message;
    if (nmsg == "")
        nmsg = "Calaos Notification";

    HistLogger::Instance().getPushTokens(
            [=](bool success, string errorMsg, const vector<PushToken> &tokens)
    {
        if (!success)
        {
            cCriticalDom(TAG) << "Unable to send push notification: " << errorMsg;
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
        // Apple iOS -----------------------
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

            if (!notifPicUuid.empty())
            {
                notif["mutable-content"] = true;
                notif["mutable_content"] = true;
                notif["data"] = {{ "event_uuid", notifPicUuid }};
            }

            if (Utils::get_config_option("notif_development") == "true")
                notif["development"] = true;

            jarr.push_back(notif);
        }

        // Google Android -----------------------
        if (!androidTokens.empty())
        {
            Json tokArray = Json::array();
            for (const auto &t: androidTokens)
                tokArray.push_back(t.token);

            Json notif = {
                { "tokens", tokArray },
                { "platform", 2 },
                { "message", nmsg },
                { "title", "Calaos" },
                { "priority", "high" },
                { "sound", "default" }
            };

            //force priority to high to make sure the notification is displayed
            notif["notification"] = {
                { "title", "Calaos" },
                { "body", nmsg },
                { "sound", "default" }
            };

            if (!notifPicUuid.empty())
            {
                notif["data"] = {
                    { "event_uuid", notifPicUuid },
                    { "body", nmsg },
                    { "message", nmsg },
                    { "title", "Calaos" }
                };
            }
            else
            {
                notif["data"] = {
                    { "body", nmsg },
                    { "message", nmsg },
                    { "title", "Calaos" }
                };
            }

            jarr.push_back(notif);
        }

        Json jnotif = {{ "notifications", jarr }};

        if (jarr.empty())
        {
            cInfoDom(TAG) << "No token registered. Cannot send push.";
            return;
        }

        cDebugDom(TAG) << "Sending notif to service:";
        cDebugDom(TAG) << jnotif.dump();

        UrlDownloader *u = new UrlDownloader("https://push.calaos.fr/api/push", true);
        u->m_signalComplete.connect([this, callbackSent](int statusCode)
        {
            cDebugDom(TAG) << "Push notif sent with code: " << statusCode;
            callbackSent();
        });
        u->httpPost(string(), jnotif.dump());
    });
}
