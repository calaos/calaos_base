/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef __MQTT_IO_BASE_H__
#define __MQTT_IO_BASE_H__

#include <functional>

#include "MqttCtrl.h"
#include "MqttBrokersList.h"

namespace Calaos
{

//T3.2c: shared constructor plumbing for the thin Mqtt IO subclasses.
//Every Mqtt IO documents itself the same way (friendly name, description,
//MqttCtrl::commonDoc) and resolves its MqttCtrl from the brokers list.
//
//The subclass constructor must finish its setup by calling
//subscribeTopicSub() (or subscribeStatusTopics() when the topic_sub
//subscription is conditional, see MqttOutputShutter): the callback is only
//invoked later from the event loop, never re-entrantly, so capturing `this`
//from the constructor body is safe once the object members are initialized.
//
//This is a local dedup inside IO/Mqtt only; a later ticket harmonizes the
//per-family IO abstractions across protocols.
template<typename IoBaseT>
class MqttIOBase : public IoBaseT
{
protected:
    MqttCtrl *ctrl;

    MqttIOBase(Params &p, const char *friendlyName, const string &description):
        IoBaseT(p)
    {
        this->ioDoc->friendlyNameSet(friendlyName);
        this->ioDoc->descriptionSet(description);
        MqttCtrl::commonDoc(this->ioDoc);

        ctrl = MqttBrokersList::Instance().get_ctrl(this->get_params());
    }

    //Subscribe to topic_sub and to the special status topics
    //(battery, online, ...)
    void subscribeTopicSub(std::function<void()> onMessage)
    {
        ctrl->subscribeTopic(this->get_param("topic_sub"),
                             [onMessage](string, string) { onMessage(); });
        subscribeStatusTopics();
    }

    void subscribeStatusTopics()
    {
        ctrl->subscribeStatusTopics(this);
    }
};

}

#endif // __MQTT_IO_BASE_H__
