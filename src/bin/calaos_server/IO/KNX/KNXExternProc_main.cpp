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
#include "KNXExternProc_main.h"

void KNXProcess::readTimeout()
{
    connectKnxd(); //try reconnecting to eibnetmux

    if (monitorMode && isConnected())
        while (monitorWait()) ;
}

void KNXProcess::messageReceived(const string &msg)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cWarningDom("knx") << "Error parsing json from sub process: " << jerr.text;
        if (jroot)
            json_decref(jroot);
        return;
    }

    Params jsonData;
    jansson_decode_object(jroot, jsonData);

    if (jsonData["type"] == "write")
    {
        KNXValue v = KNXValue::fromJson(json_object_get(jroot, "value"));

        writeKnxValue(jsonData["group_addr"], v);
    }
    else if (jsonData["type"] == "read")
    {
        sendReadKnxCommand(jsonData["group_addr"]);
    }
    json_decref(jroot);
}

bool KNXProcess::setup(int &argc, char **&argv)
{
    if (argvOptionCheck(argv, argv + argc, "--help") ||
            argvOptionCheck(argv, argv + argc, "-h"))
    {
        cout << "This tool is for calaos internal use only. However it may be useful " <<
                "for debugging purpose. It's designed to connect to a running eibnetmux server " <<
                "and handle KNX devices. You can also do some query and request directly from this tool." << endl << endl;
        cout << "    Usage : " << argv[0] << " [--monitor-bus] [--write] [--read] <--server 127.0.0.1> <parameters>" << endl;
        cout << "         --monitor-bus    : Monitor the KNX bus and display what happens." << endl;
        cout << "         --write          : Writes a value to a device. Parameters are: <group_address> <eis_type> <value>" << endl;
        cout << "         --read           : Read a value from a device. Parameters are: <group_address> [<eis_type>] (eis_type is 0 and is autodetection)" << endl;
        cout << "         --server <host>  : Allows to choose to which server to connect (default, auto search on LAN)" << endl;
        cout << endl;
        cout << "    Parameters:" << endl;
        cout << "         <group address> is knx group address either as x/y/z or 16-bit integer" << endl;
        cout << "         <eis type> is an integer in the range 1-15, 0 means autodetection (only for reading)" << endl;
        cout << "         <value> depends on eis type:" << endl;
        cout << "                 1,2,6,7,8,13,14 - byte" << endl;
        cout << "                 3               - time in seconds" << endl;
        cout << "                 4               - date in seconds" << endl;
        cout << "                 5,9             - float (5 not implemented yet)" << endl;
        cout << "                 10              - integer 16-bits" << endl;
        cout << "                 11              - integer 32-bits" << endl;
        cout << "                 13              - 4 bytes" << endl;
        cout << "                 15              - 14 bytes" << endl;
        return false;
    }

    char *srv = argvOptionParam(argv, argv + argc, "--server");
    if (srv) eibserver = srv;

    if (argvOptionCheck(argv, argv + argc, "--read"))
    {
        doRead(argc, argv);
        return false;
    }
    else if (argvOptionCheck(argv, argv + argc, "--write"))
    {
        doWrite(argc, argv);
        return false;
    }
    else if (argvOptionCheck(argv, argv + argc, "--monitor-bus"))
    {
        doMonitorBus(argc, argv);
        return false;
    }

    if (argvOptionCheck(argv, argv + argc, "--internal-monitor-bus"))
        monitorMode = true;

    if (!connectSocket())
    {
        cError() << "process cannot connect to calaos_server";
        return false;
    }

    cDebug() << "Knxd host: " << eibserver;

    return true;
}

string KNXProcess::knxPhysicalAddr(eibaddr_t addr)
{
    stringstream s;
    s << ((addr >> 12) & 0x0F) << "."
      << ((addr >> 8) & 0x0F) << "."
      << (addr & 0xFF);

    return s.str();
}

string KNXProcess::knxGroupAddr(eibaddr_t addr)
{
    stringstream s;
    s << ((addr >> 11) & 0x1F) << "/"
      << ((addr >> 8) & 0x07) << "/"
      << (addr & 0xFF);

    return s.str();
}

eibaddr_t KNXProcess::eKnxPhysicalAddr(const string &addr)
{
    vector<string> tokens;
    Utils::split(addr, tokens, ".", 3);
    int a, b, c;
    Utils::from_string(tokens[0], a);
    Utils::from_string(tokens[1], b);
    Utils::from_string(tokens[2], c);
    return ((a & 0x0F) << 12) |
           ((b & 0x0F) << 8) |
           (c & 0xFF);
}

eibaddr_t KNXProcess::eKnxGroupAddr(const string &group_addr)
{
    vector<string> tokens;
    Utils::split(group_addr, tokens, "/", 3);
    int a, b, c;
    Utils::from_string(tokens[0], a);
    Utils::from_string(tokens[1], b);
    Utils::from_string(tokens[2], c);
    return ((a & 0x01F) << 11) |
           ((b & 0x07) << 8) |
           (c & 0xFF);
}

int KNXProcess::procMain()
{
    connectKnxd();

    run(500);

    return 0;
}

void KNXProcess::connectKnxd()
{
    if (isConnected())
        return;

    cDebug() << "Trying to connect to eibnetmux " << eibserver;

    if (!eibserver.empty())
        eibsock = EIBSocketURL(eibserver.c_str());
    else
        eibsock = EIBSocketURL("ip:localhost"); //by default try localhost
    if (!isConnected())
    {
        cError() << "Connect to knxd failed.";
        return;
    }

    if (EIBOpen_GroupSocket(eibsock, 0) == -1)
    {
        cError() << "Failed to open group socket.";
        return;
    }

    cDebug() << "Connected.";
}

bool KNXProcess::monitorWait()
{
    cDebug() << "Waiting monitor data...";

    eibaddr_t dest;
    eibaddr_t src;
    vector<uint8_t> buf(200, 0);

    int len = EIBGetGroup_Src (eibsock, buf.size(), buf.data(), &src, &dest);

    if (len < 0)
    {
        cWarning() << "Error waiting for monitor data, stopping.";
        EIBClose(eibsock);
        eibsock = nullptr;
        Params p = {{"type", "disconnected"}};

        string res = jansson_to_string(p.toJson());
        if (!res.empty())
            sendMessage(res);
        return false;
    }
    else if (len < 2)
    {
        cWarning() << "Packet too short";
        return true; //try again
    }

    buf.resize(len);

    if (buf[0] & 0x3 || (buf[1] & 0xC0) == 0xC0)
    {
        cWarning() << "Unkown APDU from " << knxPhysicalAddr(src) << " to " << knxGroupAddr(dest);
        return true;
    }

    string t, type;
    bool printValue = true;
    switch (buf[1] & 0xC0)
    {
    case 0x00:
        t = "Read";
        type = "read";
        printValue = false;
        break;
    case 0x40:
        t = "Response";
        type = "response";
        break;
    case 0x80:
        t = "Write";
        type = "write";
        break;
    }

    KNXValue v;
    if (printValue && v.setValue(0, buf))
        cDebug() << t << " Value : " << v.toString();

    Params p = {{"type", "event"},
                {"group_addr", knxGroupAddr(dest)},
                {"knx_type", type}};
    json_t *j = p.toJson();
    if (printValue)
        json_object_set_new(j, "value", v.toJson());

    string res = jansson_to_string(j);
    if (!res.empty())
        sendMessage(res);

    return isConnected();
}

void KNXProcess::writeKnxValue(const string &group_addr, const KNXValue &value)
{
    cDebug() << "Writing KNX value to " << group_addr;

    eibaddr_t knx_addr = eKnxGroupAddr(group_addr);
    vector<uint8_t> data;

    if (!value.toKnxData(data))
    {
        cError() << "Failed to get KNX formated data";
        return;
    }

    int len = EIBSendGroup(eibsock, knx_addr, data.size(), data.data());
    if (len == -1)
        cError() << "Failed to write data to KNX address " << group_addr;
}

void KNXProcess::sendReadKnxCommand(const string &group_addr)
{
    cDebug() << "Sending Read command for KNX group " << group_addr;
    std::unique_ptr<KnxdObj> eibobj (new KnxdObj());

    if (!eibobj->open(eibserver))
    {
        cError() << "Failed to connect to " << eibserver;
        return;
    }

    eibaddr_t knx_addr = eKnxGroupAddr(group_addr);

    if (EIBOpenT_Group(eibobj->sock, knx_addr, 1) == -1)
    {
        cError() << "Failed to open T_Group";
        return;
    }

    uint8_t buf[2] = { 0, 0 };
    int len = EIBSendAPDU(eibobj->sock, 2, buf);

    if (len < 0)
        cError() << "Failed to send READ command";
}

EXTERN_PROC_CLIENT_MAIN(KNXProcess)
