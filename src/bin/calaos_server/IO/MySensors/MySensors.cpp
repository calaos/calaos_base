/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "MySensors.h"

string MySensors::DataType2String(int dataType)
{
    switch (dataType)
    {
    default: break;
    case V_TEMP: return "V_TEMP";
    case V_HUM: return "V_HUM";
    case V_LIGHT: return "V_LIGHT";
    case V_DIMMER: return "V_DIMMER";
    case V_PRESSURE: return "V_PRESSURE";
    case V_FORECAST: return "V_FORECAST";
    case V_RAIN: return "V_RAIN";
    case V_RAINRATE: return "V_RAINRATE";
    case V_WIND: return "V_WIND";
    case V_GUST: return "V_GUST";
    case V_DIRECTION: return "V_DIRECTION";
    case V_UV: return "V_UV";
    case V_WEIGHT: return "V_WEIGHT";
    case V_DISTANCE: return "V_DISTANCE";
    case V_IMPEDANCE: return "V_IMPEDANCE";
    case V_ARMED: return "V_ARMED";
    case V_TRIPPED: return "V_TRIPPED";
    case V_WATT: return "V_WATT";
    case V_KWH: return "V_KWH";
    case V_SCENE_ON: return "V_SCENE_ON";
    case V_SCENE_OFF: return "V_SCENE_OFF";
    case V_HEATER: return "V_HEATER";
    case V_HEATER_SW: return "V_HEATER_SW";
    case V_LIGHT_LEVEL: return "V_LIGHT_LEVEL";
    case V_VAR1: return "V_VAR1";
    case V_VAR2: return "V_VAR2";
    case V_VAR3: return "V_VAR3";
    case V_VAR4: return "V_VAR4";
    case V_VAR5: return "V_VAR5";
    case V_UP: return "V_UP";
    case V_DOWN: return "V_DOWN";
    case V_STOP: return "V_STOP";
    case V_IR_SEND: return "V_IR_SEND";
    case V_IR_RECEIVE: return "V_IR_RECEIVE";
    case V_FLOW: return "V_FLOW";
    case V_VOLUME: return "V_VOLUME";
    case V_LOCK_STATUS: return "V_LOCK_STATUS";
    case V_DUST_LEVEL: return "V_DUST_LEVEL";
    case V_VOLTAGE: return "V_VOLTAGE";
    case V_CURRENT: return "V_CURRENT";
    case V_CALAOS: return "V_CALAOS";
    }

    return string();
}

int MySensors::String2DataType(string dataType)
{
    if (dataType == "V_TEMP") return V_TEMP;
    else if (dataType == "V_HUM") return V_HUM;
    else if (dataType == "V_LIGHT") return V_LIGHT;
    else if (dataType == "V_DIMMER") return V_DIMMER;
    else if (dataType == "V_PRESSURE") return V_PRESSURE;
    else if (dataType == "V_FORECAST") return V_FORECAST;
    else if (dataType == "V_RAIN") return V_RAIN;
    else if (dataType == "V_RAINRATE") return V_RAINRATE;
    else if (dataType == "V_WIND") return V_WIND;
    else if (dataType == "V_GUST") return V_GUST;
    else if (dataType == "V_DIRECTION") return V_DIRECTION;
    else if (dataType == "V_UV") return V_UV;
    else if (dataType == "V_WEIGHT") return V_WEIGHT;
    else if (dataType == "V_DISTANCE") return V_DISTANCE;
    else if (dataType == "V_IMPEDANCE") return V_IMPEDANCE;
    else if (dataType == "V_ARMED") return V_ARMED;
    else if (dataType == "V_TRIPPED") return V_TRIPPED;
    else if (dataType == "V_WATT") return V_WATT;
    else if (dataType == "V_KWH") return V_KWH;
    else if (dataType == "V_SCENE_ON") return V_SCENE_ON;
    else if (dataType == "V_SCENE_OFF") return V_SCENE_OFF;
    else if (dataType == "V_HEATER") return V_HEATER;
    else if (dataType == "V_HEATER_SW") return V_HEATER_SW;
    else if (dataType == "V_LIGHT_LEVEL") return V_LIGHT_LEVEL;
    else if (dataType == "V_VAR1") return V_VAR1;
    else if (dataType == "V_VAR2") return V_VAR2;
    else if (dataType == "V_VAR3") return V_VAR3;
    else if (dataType == "V_VAR4") return V_VAR4;
    else if (dataType == "V_VAR5") return V_VAR5;
    else if (dataType == "V_UP") return V_UP;
    else if (dataType == "V_DOWN") return V_DOWN;
    else if (dataType == "V_STOP") return V_STOP;
    else if (dataType == "V_IR_SEND") return V_IR_SEND;
    else if (dataType == "V_IR_RECEIVE") return V_IR_RECEIVE;
    else if (dataType == "V_FLOW") return V_FLOW;
    else if (dataType == "V_VOLUME") return V_VOLUME;
    else if (dataType == "V_LOCK_STATUS") return V_LOCK_STATUS;
    else if (dataType == "V_DUST_LEVEL") return V_DUST_LEVEL;
    else if (dataType == "V_VOLTAGE") return V_VOLTAGE;
    else if (dataType == "V_CURRENT") return V_CURRENT;
    else if (dataType == "V_CALAOS") return V_CALAOS;

    return V_ERROR;
}

void MySensors::commonDoc(IODoc *ioDoc)
{
    ioDoc->linkAdd("MySensors", _("http://mysensors.org"));
    Params gwlist = {{ "serial", _("Serial") },
                     { "tcp", _("TCP") }};
    ioDoc->paramAddList("gateway", _("Gateway type used, tcp or serial are supported"), true, gwlist, "serial");
    ioDoc->paramAdd("port",
                    _("If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway."),
                    IODoc::TYPE_STRING, true, "/dev/ttyUSB0");
    ioDoc->paramAdd("host", _("IP address of the tcp gateway if relevant"), IODoc::TYPE_STRING, true);
}
