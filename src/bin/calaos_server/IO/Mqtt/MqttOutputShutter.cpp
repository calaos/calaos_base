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
#include "MqttOutputShutter.h"
#include "MqttBrokersList.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputShutter)

const char *TAG = "mqtt_shutter";

MqttOutputShutter::MqttOutputShutter(Params &p):
    OutputShutter(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MqttOutputShutter");
    ioDoc->descriptionSet(_("Control shutters through mqtt broker"));
    MqttCtrl::commonDoc(ioDoc);

    ioDoc->paramAdd("topic_pub", _("Topic to publish commands (open/close/stop)"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("topic_sub", _("Topic to subscribe to get shutter status (optional). If not set, state is managed by Calaos timing logic."), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("payload_open", _("Payload to send when opening"), IODoc::TYPE_STRING, true, "OPEN");
    ioDoc->paramAdd("payload_close", _("Payload to send when closing"), IODoc::TYPE_STRING, true, "CLOSE");
    ioDoc->paramAdd("payload_stop", _("Payload to send when stopping"), IODoc::TYPE_STRING, true, "STOP");

    ioDoc->paramAdd("state_open", _("Value received for open state (when topic_sub is set)"), IODoc::TYPE_STRING, false, "open");
    ioDoc->paramAdd("state_close", _("Value received for closed state (when topic_sub is set)"), IODoc::TYPE_STRING, false, "closed");

    // Get MQTT controller
    ctrl = MqttBrokersList::Instance().get_ctrl(get_params());

    // Subscribe to status topic if provided
    if (get_params().Exists("topic_sub"))
    {
        useExternalState = true;
        ctrl->subscribeTopic(get_param("topic_sub"), [=](string, string)
        {
            readValue();
        });
    }

    ctrl->subscribeStatusTopics(this);

    cInfoDom(TAG) << "MqttOutputShutter::MqttOutputShutter()";
}

void MqttOutputShutter::readValue()
{
    if (!useExternalState)
        return;

    bool err;
    auto val = ctrl->getValue(get_params(), err, "topic_sub");

    if (err)
        return;

    cDebugDom(TAG) << "Read shutter status value: " << val;

    string state_open = get_param("state_open");
    if (state_open.empty())
        state_open = "open";

    string state_close = get_param("state_close");
    if (state_close.empty())
        state_close = "closed";

    if (val == state_open)
    {
        cDebugDom(TAG) << "Shutter is OPEN";

        // Update internal state
        sens = SHUTTER_STOP;
        old_sens = SHUTTER_UP;
        state_volet = "true";
        cmd_state = "up";

        // Stop any running timers
        if (timer_end)
        {
            delete timer_end;
            timer_end = NULL;
        }
        if (timer_up)
        {
            delete timer_up;
            timer_up = NULL;
        }
        if (timer_down)
        {
            delete timer_down;
            timer_down = NULL;
        }

        updateCache();
        EmitSignalIO();
        EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } },
                         true);
    }
    else if (val == state_close)
    {
        cDebugDom(TAG) << "Shutter is CLOSED";

        // Update internal state
        sens = SHUTTER_STOP;
        old_sens = SHUTTER_DOWN;
        state_volet = "false";
        cmd_state = "down";

        // Stop any running timers
        if (timer_end)
        {
            delete timer_end;
            timer_end = NULL;
        }
        if (timer_up)
        {
            delete timer_up;
            timer_up = NULL;
        }
        if (timer_down)
        {
            delete timer_down;
            timer_down = NULL;
        }

        updateCache();
        EmitSignalIO();
        EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } },
                         true);
    }
}

void MqttOutputShutter::setOutputUp(bool enable)
{
    if (!enable)
        return;

    cDebugDom(TAG) << "Opening shutter via MQTT";

    string topic = get_param("topic_pub");
    string payload = get_param("payload_open");
    if (payload.empty())
        payload = "OPEN";

    ctrl->publishTopic(topic, payload);
}

void MqttOutputShutter::setOutputDown(bool enable)
{
    if (!enable)
        return;

    cDebugDom(TAG) << "Closing shutter via MQTT";

    string topic = get_param("topic_pub");
    string payload = get_param("payload_close");
    if (payload.empty())
        payload = "CLOSE";

    ctrl->publishTopic(topic, payload);
}

void MqttOutputShutter::Stop()
{
    cDebugDom(TAG) << "Stopping shutter via MQTT";

    string topic = get_param("topic_pub");
    string payload = get_param("payload_stop");
    if (payload.empty())
        payload = "STOP";

    ctrl->publishTopic(topic, payload);

    // Call base class Stop to update internal state
    OutputShutter::Stop();
}
