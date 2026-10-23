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

string NotifManager::pushServiceUrl = "https://push.calaos.fr/api/push";

void NotifManager::setPushServiceUrl(const string &url)
{
    pushServiceUrl = url;
}

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

    /* calaos_mail is spawned with --delete below, so it is the one unlinking
     * the body file and the attachment once the mail is out: this function
     * takes ownership of both. Every path leaving without spawning it has to
     * drop them, or they stay in /tmp forever (a tmpfs on the box).
     */
    auto dropTempFiles = [&tmpFile, &attachmentFile]()
    {
        if (!tmpFile.empty())
            FileUtils::unlink(tmpFile);
        if (!attachmentFile.empty())
            FileUtils::unlink(attachmentFile);
    };

    if (tmpFile.empty())
    {
        //getTmpFilename() already logged why. calaos_mail reads the body from a
        //file, there is nothing to send without it.
        cCriticalDom(TAG) << "No temporary file for the mail body, cannot send mail !";
        dropTempFiles();
        return;
    }

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
            dropTempFiles();
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
    exe->once<uvw::ErrorEvent>([exe, tmpFile, attachmentFile = string(attachmentFile)](const uvw::ErrorEvent &ev, auto &)
    {
        cWarningDom(TAG) << "Process error: " << ev.what();

        //calaos_mail never ran, nobody else will delete its files
        FileUtils::unlink(tmpFile);
        if (!attachmentFile.empty())
            FileUtils::unlink(attachmentFile);

        exe->close();
    });

    Utils::CStrArray arr(cmd);
    cDebugDom(TAG) << "Executing command: " << arr.toString();
    exe->spawn(arr.at(0), arr.data());
}

string NotifManager::buildPushBody(const vector<PushToken> &tokens, const string &message,
                                   const string &notifPicUuid, bool development)
{
    auto nmsg = message;
    if (nmsg == "")
        nmsg = "Calaos Notification";

    vector<PushToken> androidTokens;
    vector<PushToken> androidDataTokens;
    vector<PushToken> iosTokens;

    for (const auto &t: tokens)
    {
        if (t.hw_type == HistLogger::PUSH_HW_IOS)
            iosTokens.push_back(t);
        else if (t.hw_type == HistLogger::PUSH_HW_ANDROID && t.format == HistLogger::PUSH_FORMAT_DATA)
            androidDataTokens.push_back(t);
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
            { "message", nmsg },
            { "topic", "fr.calaos.CalaosMobile" }
        };
        notif["platform"] = 1;

        //gorush reads only the snake_case key. Always set: the iOS service
        //extension (app badge) is run by iOS only for mutable-content.
        notif["mutable_content"] = true;

        if (!notifPicUuid.empty())
        {
            notif["data"] = { { "event_uuid", notifPicUuid } };
        }

        if (development)
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
            { "message", nmsg },
            { "title", "Calaos" },
            { "priority", "high" },
            { "sound", "default" }
        };
        notif["platform"] = 2;

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

    // Google Android, data-only -----------------------
    //No title/message/notification/sound: gorush would turn any of them into a
    //notification that Android displays itself while the app is in the
    //background, without calling onMessageReceived(). android.priority is the
    //FCM delivery priority (the top-level one is not mapped for Android), a
    //normal data message is deferred while the phone dozes.
    if (!androidDataTokens.empty())
    {
        Json tokArray = Json::array();
        for (const auto &t: androidDataTokens)
            tokArray.push_back(t.token);

        Json notif = {
            { "tokens", tokArray },
            { "android", { { "priority", "high" } } },
            { "data", { { "body", nmsg }, { "message", nmsg }, { "title", "Calaos" } } }
        };
        notif["platform"] = 2;

        if (!notifPicUuid.empty())
            notif["data"]["event_uuid"] = notifPicUuid;

        jarr.push_back(notif);
    }

    if (jarr.empty())
        return string();

    //The message comes from rules.xml and never went through a JSON parser:
    //a bare dump() throws type_error.316 on invalid UTF-8.
    Json jnotif = { { "notifications", jarr } };
    return jnotif.dump(-1, ' ', true, Json::error_handler_t::replace);
}

void NotifManager::sendPushNotification(const string &message, const string &notifPicUuid, std::function<void(void)> callbackSent,
                                        std::function<void(bool)> callbackDone)
{
    cDebugDom(TAG) << "Sending push notification with message: " << message;

    HistLogger::Instance().getPushTokens(
        [=](bool success, string errorMsg, const vector<PushToken> &tokens)
        {
        if (!success)
        {
            cCriticalDom(TAG) << "Unable to send push notification: " << errorMsg;
            if (callbackDone)
                callbackDone(false);
            return;
        }

        string body = buildPushBody(tokens, message, notifPicUuid,
                                    Utils::get_config_option("notif_development") == "true");
        if (body.empty())
        {
            cInfoDom(TAG) << "No token registered. Cannot send push.";
            if (callbackDone)
                callbackDone(true);
            return;
        }

        //The body carries the device tokens, it is never logged.
        cDebugDom(TAG) << "Sending notif to service for " << tokens.size() << " token(s)";

        UrlDownloader *u = new UrlDownloader(pushServiceUrl, true);
        u->m_signalComplete.connect([this, callbackSent, callbackDone](int statusCode)
                                    {
            cDebugDom(TAG) << "Push notif sent with code: " << statusCode;
            //Optional: calling an empty one throws std::bad_function_call
            if (callbackSent)
                callbackSent();
            if (callbackDone)
                callbackDone(statusCode >= 200 && statusCode < 300);
        });
        if (!u->httpPost(string(), body))
        {
            //LCOV_EXCL_START only reached when libcurl cannot create a handle
            u->cancel();
            if (callbackDone)
                callbackDone(false);
            //LCOV_EXCL_STOP
        }
    });
}
