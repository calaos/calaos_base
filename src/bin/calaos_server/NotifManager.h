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
    void sendPushNotification(const string &message, const string &notifPicUuid = "", std::function<void(void)> callbackSent = {});

private:
    NotifManager();
};

}
