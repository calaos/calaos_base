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
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputShutter)

static const char *TAG = "mqtt_shutter";

MqttOutputShutter::MqttOutputShutter(Params &p):
    MqttIOBase(p, "MqttOutputShutter", _("Control shutters through mqtt broker"))
{
    ioDoc->paramAdd("topic_pub", _("Topic to publish commands (open/close/stop)"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("topic_sub", _("Topic to subscribe to get shutter status (optional). If not set, state is managed by Calaos timing logic."), IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("payload_open", _("Payload to send when opening"), IODoc::TYPE_STRING, true, "OPEN");
    ioDoc->paramAdd("payload_close", _("Payload to send when closing"), IODoc::TYPE_STRING, true, "CLOSE");
    ioDoc->paramAdd("payload_stop", _("Payload to send when stopping"), IODoc::TYPE_STRING, true, "STOP");

    ioDoc->paramAdd("state_open", _("Value received for open state (when topic_sub is set)"), IODoc::TYPE_STRING, false, "open");
    ioDoc->paramAdd("state_close", _("Value received for closed state (when topic_sub is set)"), IODoc::TYPE_STRING, false, "closed");

    // Subscribe to status topic if provided
    if (get_params().Exists("topic_sub"))
    {
        useExternalState = true;
        subscribeTopicSub([this]() { readValue(); });
    }
    else
        subscribeStatusTopics();

    cInfoDom(TAG) << "MqttOutputShutter::MqttOutputShutter()";
}

void MqttOutputShutter::applyExternalState(bool open)
{
    cDebugDom(TAG) << "Shutter is " << (open ? "OPEN" : "CLOSED");

    // Update internal state
    sens = SHUTTER_STOP;
    old_sens = open ? SHUTTER_UP : SHUTTER_DOWN;
    state_volet = open ? "true" : "false";
    cmd_state = open ? "up" : "down";

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
        applyExternalState(true);
    else if (val == state_close)
        applyExternalState(false);
}

void MqttOutputShutter::publishCommand(const string &payloadParam, const string &defaultPayload)
{
    string payload = get_param(payloadParam);
    if (payload.empty())
        payload = defaultPayload;

    ctrl->publishTopic(get_param("topic_pub"), payload);
}

void MqttOutputShutter::setOutputUp(bool enable)
{
    if (!enable)
        return;

    cDebugDom(TAG) << "Opening shutter via MQTT";
    publishCommand("payload_open", "OPEN");
}

void MqttOutputShutter::setOutputDown(bool enable)
{
    if (!enable)
        return;

    cDebugDom(TAG) << "Closing shutter via MQTT";
    publishCommand("payload_close", "CLOSE");
}

void MqttOutputShutter::Stop()
{
    cDebugDom(TAG) << "Stopping shutter via MQTT";
    publishCommand("payload_stop", "STOP");

    // Call base class Stop to update internal state
    OutputShutter::Stop();
}
