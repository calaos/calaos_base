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
#include "KNXCtrl.h"
#include "Prefix.h"

namespace
{

/* E4.1e - the two jansson readers this file used, kept identical in
 * behaviour on top of nlohmann::json.
 *
 * knxDecodeObject() mirrors jansson_decode_object() (src/lib/Jansson_Addition.h):
 * a string stays a string, a boolean becomes "true"/"false", a number goes
 * through Utils::to_string(double), anything else is recorded as an empty
 * string, every key present is recorded, and NOTHING EVER THROWS. The natural
 * nlohmann shortcut - assigning a Json straight into a std::string, the way
 * Params::fromNJson() does - throws type_error.302 on a non-string value, from
 * inside processNewMessage(), which has no handler above it: a subprocess
 * sending {"value":{"eis":13}} would take the server down.
 *
 * knxStringGet() mirrors jansson_string_get(): string values only, the default
 * for anything else, including a missing key.
 */
void knxDecodeObject(const Json &jroot, Params &params)
{
    if (!jroot.is_object())
        return;

    for (Json::const_iterator it = jroot.cbegin();it != jroot.cend();it++)
    {
        string svalue;

        if (it->is_string())
            svalue = it->get<string>();
        else if (it->is_boolean())
            svalue = it->get<bool>()?"true":"false";
        else if (it->is_number())
            svalue = Utils::to_string(it->get<double>());

        params.Add(it.key(), svalue);
    }
}

string knxStringGet(const Json &jroot, const string &key, const string &default_value = string())
{
    if (!jroot.is_object())
        return default_value;

    Json::const_iterator it = jroot.find(key);
    if (it == jroot.cend())
        return default_value;

    if (!it->is_string())
        return default_value;

    return it->get<string>();
}

/* KNXCtrl::processNewMessage() hands fromJson() whatever sits under "value",
 * and hands it nothing at all when the key is absent - which is exactly what a
 * "read" event looks like. An absent key must stay absent, not become null.
 */
Json jsonChild(const Json &jroot, const string &key)
{
    Json::const_iterator it = jroot.find(key);
    if (it == jroot.cend())
        return Json();
    return *it;
}

}

KNXCtrl::KNXCtrl(const string host)
{
    cDebugDom("knx") << "new KNXCtrl: " << host;
    process = new ExternProcServer("knx");
    processMonitor = new ExternProcServer("knx_monitor");

    //Command process
    string exe = Prefix::Instance().binDirectoryGet() + "/calaos_knx";

    //There should not be any message from command process
    //process->messageReceived.connect(sigc::mem_fun(*this, &KNXCtrl::processNewMessage));

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "process exited, restarting...";
        process->startProcess(exe, "knx", string("--server ip:") + host);
    });

    process->startProcess(exe, "knx", string("--server ip:") + host);

    //Monitor process
    processMonitor->messageReceived.connect(sigc::mem_fun(*this, &KNXCtrl::processNewMessage));

    processMonitor->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "monitor process exited, restarting...";
        processMonitor->startProcess(exe, "knx", string("--internal-monitor-bus --server ip:") + host);
    });

    processMonitor->startProcess(exe, "knx", string("--internal-monitor-bus --server ip:") + host);
}

KNXCtrl::~KNXCtrl()
{
    delete process;
    delete processMonitor;
}

shared_ptr<KNXCtrl> KNXCtrl::Instance(const string &host)
{
    static map<string, shared_ptr<KNXCtrl>> mapInst;
    auto it = mapInst.find(host);
    if (it != mapInst.end())
        return it->second;

    shared_ptr<KNXCtrl> inst(new KNXCtrl(host));
    mapInst[host] = std::move(inst);
    return mapInst[host];
}

Json KNXValue::toJson() const
{
    Params p = {{"type", Utils::to_string(type) },
                {"eis", Utils::to_string(eis)},
                {"value_int", Utils::to_string(value_int)},
                {"value_float", Utils::to_string(value_float)},
                {"value_char", Utils::to_string(value_char)},
                {"value_string", value_string}};
    return p.toNJson();
}

KNXValue KNXValue::fromJson(const Json &jval)
{
    Params p;
    knxDecodeObject(jval, p);

    KNXValue v;
    Utils::from_string(p["type"], v.type);
    Utils::from_string(p["eis"], v.eis);
    Utils::from_string(p["value_int"], v.value_int);
    Utils::from_string(p["value_float"], v.value_float);
    Utils::from_string(p["value_char"], v.value_char);
    v.value_string = p["value_string"];

    return v;
}

string KNXValue::toString() const
{
    switch (type)
    {
    case KNXError:
        cError() << "No data";
        return string();
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
            stringstream s;
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

    return string();
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

KNXValue KNXValue::fromString(const string &val, int eis)
{
    KNXValue v;
    v.type = KNXString;
    v.eis = eis;
    v.value_string = val;
    return v;
}

void KNXCtrl::processNewMessage(const string &msg)
{
    //E4.1e: json_loads() answered NULL on malformed input, Json::parse()
    //throws. The non-throwing form plus is_discarded() keeps the same shape.
    Json jroot = Json::parse(msg, nullptr, false);

    if (jroot.is_discarded() || !jroot.is_object())
    {
        cWarningDom("knx") << "Error parsing json from sub process. Raw message: " << msg;
        return;
    }

    string mtype = knxStringGet(jroot, "type");

    if (mtype == "event")
    {
        cDebugDom("knx") << "Received event: " << msg;
        string knxtype = knxStringGet(jroot, "knx_type");
        if (knxtype != "read") //Do not emit signal for read commands
        {
            string group_addr = knxStringGet(jroot, "group_addr");
            KNXValue val = KNXValue::fromJson(jsonChild(jroot, "value"));

            knxCache[group_addr] = val;
            valueChanged.emit(group_addr, val);
        }
    }
    else if (mtype == "disconnected")
    {
        cDebugDom("knx") << "Disconnected from knxd, restarting command process...";
        process->terminate();
    }
}

KNXValue KNXCtrl::getValue(const string &group_addr)
{
    return knxCache[group_addr];
}

void KNXCtrl::writeValue(const string &group_addr, const KNXValue &value)
{
    Params p = {{"type", "write"},
                {"group_addr", group_addr}};

    Json jroot = p.toNJson();
    jroot["value"] = value.toJson();

    string res = jroot.dump(-1, ' ', true, Json::error_handler_t::replace);

    if (!res.empty())
        process->sendMessage(res);

    cDebugDom("knx") << "Sending: " << res;
}

void KNXCtrl::readValue(const string &group_addr, int eis)
{
    Params p = {{"type", "read"},
                {"group_addr", group_addr},
                {"eis", Utils::to_string(eis)}};

    string res = p.toNJson().dump(-1, ' ', true, Json::error_handler_t::replace);

    if (!res.empty())
        process->sendMessage(res);

    cDebugDom("knx") << "Sending: " << res;
}

