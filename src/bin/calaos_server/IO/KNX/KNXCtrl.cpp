/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#include "KNXCtrl.h"
#include "Prefix.h"

namespace Calaos {

KNXCtrl::KNXCtrl(const std::string host)
{
    cDebugDom("knx") << "new KNXCtrl: " << host;
    process = new ExternProcServer("knx");
    processMonitor = new ExternProcServer("knx_monitor");

    //Command process
    std::string exe = Prefix::Instance().binDirectoryGet() + "/calaos_knx";

    //There should not be any message from command process
    //process->messageReceived.connect(sigc::mem_fun(*this, &KNXCtrl::processNewMessage));

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "process exited, restarting...";
        process->startProcess(exe, "knx", std::string("--server ") + host);
    });

    process->startProcess(exe, "knx", std::string("--server ") + host);

    //Monitor process
    exe = Prefix::Instance().binDirectoryGet() + "/calaos_knx";

    process->messageReceived.connect(sigc::mem_fun(*this, &KNXCtrl::processNewMessage));

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "monitor process exited, restarting...";
        process->startProcess(exe, "knx", std::string("--internal-monitor-bus --server ") + host);
    });

    process->startProcess(exe, "knx", std::string("--internal-monitor-bus --server ") + host);
}

KNXCtrl::~KNXCtrl()
{
    delete process;
}

std::shared_ptr<KNXCtrl> KNXCtrl::Instance(const std::string &host)
{
    static std::map<std::string, std::shared_ptr<KNXCtrl>> mapInst;
    auto it = mapInst.find(host);
    if (it != mapInst.end())
        return it->second;

    std::shared_ptr<KNXCtrl> inst(new KNXCtrl(host));
    mapInst[host] = std::move(inst);
    return mapInst[host];
}

json_t *KNXValue::toJson() const
{
    Params p = {{"type", Utils::to_string(type) },
                {"eis", Utils::to_string(eis)},
                {"value_int", Utils::to_string(value_int)},
                {"value_float", Utils::to_string(value_float)},
                {"value_char", Utils::to_string(value_char)},
                {"value_string", value_string}};
    return p.toJson();
}

KNXValue KNXValue::fromJson(json_t *jval)
{
    Params p;
    jansson_decode_object(jval, p);

    KNXValue v;
    Utils::from_string(p["type"], v.type);
    Utils::from_string(p["eis"], v.eis);
    Utils::from_string(p["value_int"], v.value_int);
    Utils::from_string(p["value_float"], v.value_float);
    Utils::from_string(p["value_char"], v.value_char);
    v.value_string = p["value_string"];

    return v;
}

std::string KNXValue::toString() const
{
    switch (type)
    {
    case KNXError:
        cError() << "No data";
        return std::string();
    case KNXInteger:
    {
        switch (eis)
        {
        case 1: return value_int == 0?"false":"true";
        case 3:
        {
            struct tm *gtime = gmtime((time_t *)&value_int);
            return Utils::time2string_digit(gtime->tm_hour * 3600 + gtime->tm_min * 60 + gtime->tm_sec);
        }
        case 4:
        {
            struct tm *ltime = localtime( (time_t *)&value_int);
            std::stringstream s;
            s << ltime->tm_year + 1900 << "/" << ltime->tm_mon + 1 << "/" << ltime->tm_mday;
            return s.str();
        }
        default: return Utils::to_string(value_int);
            break;
        }
        break;
    }
    case KNXFloat: return Utils::to_string(value_float);
    case KNXChar: return Utils::to_string((char)value_char);
    case KNXString: return value_string;
    }

    return std::string();
}

bool KNXValue::toBool() const
{
    switch (type)
    {
    case KNXError: cError() << "No data"; break;
    case KNXInteger: return value_int != 0;
    default: cError() << "Value is not integer"; break;
    }

    return false;
}

float KNXValue::toFloat() const
{
    switch (type)
    {
    case KNXError: cError() << "No data"; break;
    case KNXFloat: return value_float;
    default: cError() << "Value is not float"; break;
    }

    return 0.0;
}

int KNXValue::toInt() const
{
    switch (type)
    {
    case KNXError: cError() << "No data"; break;
    case KNXInteger: return value_int;
    default: cError() << "Value is not integer"; break;
    }

    return 0;
}

char KNXValue::toChar() const
{
    switch (type)
    {
    case KNXError: cError() << "No data"; break;
    case KNXChar: return value_char;
    default: cError() << "Value is not char"; break;
    }

    return 0;
}

KNXValue KNXValue::fromBool(bool val, int eis)
{
    KNXValue v;
    v.type = KNXInteger;
    v.eis = eis;
    v.value_int = val?1:0;
    v.value_float = v.value_int;
    v.value_char = v.value_int;
    return v;
}

KNXValue KNXValue::fromChar(char val, int eis)
{
    KNXValue v;
    v.type = KNXChar;
    v.eis = eis;
    v.value_char = val;
    v.value_float = val;
    v.value_int = val;
    return v;
}

KNXValue KNXValue::fromFloat(float val, int eis)
{
    KNXValue v;
    v.type = KNXFloat;
    v.eis = eis;
    v.value_float = val;
    v.value_int = val;
    v.value_char = val;
    return v;
}

KNXValue KNXValue::fromInt(int val, int eis)
{
    KNXValue v;
    v.type = KNXInteger;
    v.eis = eis;
    v.value_int = val;
    v.value_float = val;
    v.value_char = val;
    return v;
}

KNXValue KNXValue::fromString(const std::string &val, int eis)
{
    KNXValue v;
    v.type = KNXString;
    v.eis = eis;
    v.value_string = val;
    return v;
}

void KNXCtrl::processNewMessage(const std::string &msg)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cWarningDom("knx") << "Error parsing json from sub process: " << jerr.text << " Raw message: " << msg;
        if (jroot)
            json_decref(jroot);
        return;
    }

    std::string mtype = jansson_string_get(jroot, "type");

    if (mtype == "event")
    {
        std::string group_addr = jansson_string_get(jroot, "group_addr");
        KNXValue val = KNXValue::fromJson(json_object_get(jroot, "value"));

        knxCache[group_addr] = val;
        valueChanged.emit(group_addr, val);
    }
    else if (mtype == "disconnected")
    {
        cDebugDom("knx") << "Disconnected from eibnetmux, restarting command process...";
        process->terminate();
    }

    json_decref(jroot);
}

KNXValue KNXCtrl::getValue(const std::string &group_addr)
{
    return knxCache[group_addr];
}

void KNXCtrl::writeValue(const std::string &group_addr, const KNXValue &value)
{
    Params p = {{"type", "write"},
                {"group_addr", group_addr}};

    json_t *jroot = p.toJson();
    json_object_set_new(jroot, "value", value.toJson());

    std::string res = jansson_to_string(jroot);

    if (!res.empty())
        process->sendMessage(res);

    cDebugDom("knx") << "Sending: " << res;
}

void KNXCtrl::readValue(const std::string &group_addr, int eis)
{
    Params p = {{"type", "read"},
                {"group_addr", group_addr},
                {"eis", Utils::to_string(eis)}};

    std::string res = jansson_to_string(p.toJson());

    if (!res.empty())
        process->sendMessage(res);

    cDebugDom("knx") << "Sending: " << res;
}

}
