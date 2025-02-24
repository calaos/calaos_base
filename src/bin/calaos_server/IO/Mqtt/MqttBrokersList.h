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
#ifndef __MQTT_BROKERS_LIST_H__
#define __MQTT_BROKERS_LIST_H__

#include "MqttCtrl.h"
#include <unordered_map>
#include "Params.h"

namespace Calaos
{

class MqttBrokersList
{
private:
    MqttBrokersList();

    std::unordered_map<string, MqttCtrl *> hashControllers;

public:
    MqttCtrl *get_ctrl(const Params &p);

    static MqttBrokersList &Instance()
    {
        static MqttBrokersList list;
        return list;
    }
};

}
#endif // __MQTT_BROKERS_LIST_H__
