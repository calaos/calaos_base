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
#pragma once

#include "Utils.h"
#include <functional>

class PushToken;

namespace Calaos
{

class NotifManager
{
public:
    static NotifManager &Instance()
    {
        static NotifManager instance;
        return instance;
    }
    ~NotifManager() = default;

    //Note: attachmentFile is optional and can be used to attach a file to the email. It must be a valid file path.
    //to and from parameters are optional and can be used to specify the recipient and sender email addresses.
    //If not provided, the default email addresses from the local_config.xml will be used or a default one
    void sendMailNotification(const string &subject, const string &messageBody,
                              const string &to = "", const string &from = "", const string &attachmentFile = "");

    // Note: The `notifPicUuid` parameter is optional and can be used to attach a picture to the push notification.
    // If provided it must be a valid UUID from an event in the history log (HistLogger::Instance().appendEvent(e))
    // `callbackDone` gets true once the push service took the push, or when no token is registered, false
    // when it could not be sent.
    void sendPushNotification(const string &message, const string &notifPicUuid = "", std::function<void(void)> callbackSent = {},
                              std::function<void(bool)> callbackDone = {});

    //A code as GET admin/push/{code_id} hands it over; action and code are
    //empty when the daemon predates them
    struct AdminCode
    {
        string message;
        string codeId;
        string action;
        string code;
        //RFC 3339 UTC, to the second
        string expires;
    };

    /* Pushes the code to that one registered token only, never to the others:
     * encrypted with its push key when it has one and the daemon gave the
     * code, the daemon's message otherwise. callbackDone(false) when the token
     * is not registered (any more) or the push failed; sealedKey is the key
     * the code went out sealed with, empty for the daemon's message. Nothing
     * of the token, the key or the code is logged.
     */
    void sendAdminCodeTo(const string &token, const AdminCode &code,
                         std::function<void(bool sent, const string &sealedKey)> callbackDone = {});

    //Request body for push.calaos.fr (gorush), empty when no token can be served.
    static string buildPushBody(const vector<PushToken> &tokens, const string &message,
                                const string &notifPicUuid, bool development);

    //The keyed form of a code push, nonce being 16 raw bytes; empty when the
    //token has no usable key or the action is not in the closed table
    static string buildAdminCodeBody(const PushToken &token, const AdminCode &code, const string &nonce, bool development);

    //What a code of this action allows, empty outside the closed table
    static string adminCodePurpose(const string &action);

    //ct and tag as lowercase hex; false when the key is not 32 bytes or the code not 6 digits
    static bool sealAdminCode(const string &keyHex, const AdminCode &code, const string &nonce, string &ctHex, string &tagHex);

    //Tests point the push at a local peer.
    static void setPushServiceUrl(const string &url);

private:
    NotifManager();

    void post(const string &body, std::function<void(void)> callbackSent, std::function<void(bool)> callbackDone);

    static string pushServiceUrl;
};

}
