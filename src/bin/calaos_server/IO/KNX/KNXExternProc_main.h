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
#ifndef KNXEXTERNPROC_MAIN_H
#define KNXEXTERNPROC_MAIN_H

#include "ExternProc.h"
#include "Params.h"

extern "C" {
#include <eibclient.h>
}

/* E4.1e - mirror of the flattening decoder it replaces, on top
 * of nlohmann::json, shared by the two translation units of calaos_knx
 * (KNXExternProc_main.cpp for the message envelope, KNXExternProc_cli.cpp for
 * KNXValue::fromJson). A string stays a string, a boolean becomes
 * "true"/"false", a number goes through Utils::to_string(double), anything else
 * is recorded as an empty string, and NOTHING EVER THROWS - assigning a Json
 * straight into a std::string, the way Params::fromJson() does, throws
 * type_error.302 on a non-string, from inside messageReceived(), which has no
 * handler above it.
 */
inline void knxDecodeObject(const Json &jroot, Params &params)
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

/* An absent "value" key must stay absent, not become null: fromJson() used to
 * be handed the NULL that json_object_get() answered.
 */
inline Json knxJsonChild(const Json &jroot, const string &key)
{
    Json::const_iterator it = jroot.find(key);
    if (it == jroot.cend())
        return Json();
    return *it;
}

class KNXValue
{
public:
    KNXValue() {}

    enum
    {
        KNXError,
        KNXInteger,
        KNXFloat,
        KNXChar,
        KNXString,
    };

    int eis = -1;
    int type = KNXError;

    int64_t value_int = 0;
    float value_float = 0.0;
    unsigned char value_char = 0;
    string value_string;

    string toString();
    bool setValue(int eis, vector<uint8_t> data);
    bool toKnxData(vector<uint8_t> &data) const;

    Json toJson() const;
    static KNXValue fromJson(const Json &jval);
    static KNXValue fromString(int eis, const string &s);
};

/* E4.1e - the exact bytes calaos_knx puts on the wire, built in ONE place so
 * that production and tests cannot drift apart. KNXProcess::monitorWait()
 * blocks in EIBGetGroup_Src() on a live knxd socket, so a test can never reach
 * it; freezing a copy of the assembly froze what the TEST did, not what the
 * PRODUCT did. Both sides call these now.
 */
inline string knxEventMessage(const string &group_addr, const string &knx_type,
                              const KNXValue &value, bool printValue)
{
    Params p = {{"type", "event"},
                {"group_addr", group_addr},
                {"knx_type", knx_type}};

    Json j = p.toJson();
    if (printValue)
        j["value"] = value.toJson();

    return j.dump(-1, ' ', true, Json::error_handler_t::replace);
}

inline string knxDisconnectedMessage()
{
    Params p = {{"type", "disconnected"}};

    return p.toJson().dump(-1, ' ', true, Json::error_handler_t::replace);
}

/*******************************************************************************
 * T3.33 - THE TWO ADDRESS DECODERS, WHICH CAN NOW SAY NO
 *
 * Whatever these answer is what EIBSendGroup() writes to. They used to answer
 * an address unconditionally: Utils::split() PADS its token list up to `max`
 * (StringUtils.cpp:210), so "1" yields {"1", "", ""} - no out of bounds read,
 * but two components that nobody gave were invented and the telegram went to
 * ANOTHER GROUP.
 *
 * ⛔ The & 0x0F / & 0xFF of the old bodies were never a guard, only a mask:
 * "1/2/300" wrote to 1/2/44. A component outside its layout is REFUSED here,
 * not truncated, and `out` is left alone so a caller that ignores the answer
 * cannot pick up a fabricated address.
 *
 * They live in this header, and not in KNXExternProc_main.cpp, because that
 * translation unit ends on EXTERN_PROC_CLIENT_MAIN and no test can link it.
 ******************************************************************************/

//Group address, 5/3/8 layout: main/middle/sub.
inline bool knxGroupAddrFromString(const string &group_addr, eibaddr_t &out)
{
    vector<string> tokens;
    Utils::split(group_addr, tokens, "/", 3);

    int main_group = 0, middle_group = 0, sub_group = 0;
    if (!Utils::from_string(tokens[0], main_group) ||
        !Utils::from_string(tokens[1], middle_group) ||
        !Utils::from_string(tokens[2], sub_group))
        return false;

    if (main_group < 0 || main_group > 31 ||
        middle_group < 0 || middle_group > 7 ||
        sub_group < 0 || sub_group > 255)
        return false;

    out = (eibaddr_t)((main_group << 11) | (middle_group << 8) | sub_group);

    return true;
}

//Physical address, 4/4/8 layout: area.line.device.
inline bool knxPhysicalAddrFromString(const string &addr, eibaddr_t &out)
{
    vector<string> tokens;
    Utils::split(addr, tokens, ".", 3);

    int area = 0, line = 0, device = 0;
    if (!Utils::from_string(tokens[0], area) ||
        !Utils::from_string(tokens[1], line) ||
        !Utils::from_string(tokens[2], device))
        return false;

    if (area < 0 || area > 15 ||
        line < 0 || line > 15 ||
        device < 0 || device > 255)
        return false;

    out = (eibaddr_t)((area << 12) | (line << 8) | device);

    return true;
}

class KnxdObj
{
public:
    KnxdObj() {}
    ~KnxdObj() { if (sock != 0) EIBClose(sock); }

    bool open(const string &server);

    EIBConnection * sock = nullptr;
};

class KNXProcess: public ExternProcClient
{
public:

    //Those are called from command line by user
    void doRead(int argc, char **argv);
    void doWrite(int argc, char **argv);
    void doMonitorBus(int argc, char **argv);

    //needs to be reimplemented
    virtual bool setup(int &argc, char **&argv);
    virtual int procMain();

    EXTERN_PROC_CLIENT_CTOR(KNXProcess)

protected:

    bool monitorWait();

    //needs to be reimplemented
    virtual void readTimeout();
    virtual void messageReceived(const string &msg);

    void connectKnxd();
    void writeKnxValue(const string &group_addr, const KNXValue &value);
    void sendReadKnxCommand(const string &group_addr);

    string knxPhysicalAddr(eibaddr_t addr);
    string knxGroupAddr(eibaddr_t addr);

    string eibserver;
    EIBConnection *eibsock = nullptr;

    bool monitorMode = false;

    bool isConnected() { return eibsock; }
};

#endif // KNXEXTERNPROC_MAIN_H

