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
#include "KNXExternProc_main.h"

void KNXProcess::readTimeout()
{
    connectEibNetMux(); //try reconnecting to eibnetmux
}

void KNXProcess::messageReceived(const string &msg)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_array(jroot))
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
        cInfo() << "TODO: read not implemented yet.";
    }
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

    if (!connectSocket())
    {
        cError() << "process cannot connect to calaos_server";
        return false;
    }

    int enmx_version;
    if ((enmx_version = enmx_init()) != ENMX_VERSION_API)
    {
        cError() << "Incompatible eibnetmux API version (" << enmx_version << ", expected " << ENMX_VERSION_API << ")";
        throw std::runtime_error("Incompatible eibnetmux API version");
    }

    cDebug() << "EIBNetMux host: " << eibserver;

    return true;
}

string KNXProcess::knxPhysicalAddr(uint16_t addr)
{
    addr = ntohs(addr);

    stringstream s;
    s << ((addr & 0xf000) >> 12) << "."
      << ((addr & 0x0f00) >> 8) << "."
      << (addr & 0x00ff);

    return s.str();
}

string KNXProcess::knxGroupAddr(uint16_t addr)
{
    addr = ntohs(addr);

    stringstream s;
    s << ((addr & 0x7800) >> 11) << "/"
      << ((addr & 0x0700) >> 8) << "/"
      << (addr & 0x00ff);

    return s.str();
}

int KNXProcess::procMain()
{
    connectEibNetMux();

    run(1000);

    return 0;
}

void KNXProcess::connectEibNetMux()
{
    if (isConnected())
        return;

    if (eibserver.empty())
        eibsock = enmx_open(nullptr, (char *)"calaos");
    else
        eibsock = enmx_open((char *)eibserver.c_str(), (char *)"calaos");
    if (eibsock < 0)
    {
        cError() << "Connect to eibnetmux failed: " << enmx_errormessage(eibsock);
        return;
    }

    //add enmx socket to the main loop
    appendFd(eibsock);
}

bool KNXProcess::handleFdSet(int fd)
{
    if (fd != eibsock)
        return true; //not our fd

    unsigned char *data = (unsigned char *)malloc(10);
    uint16_t datalen = 10;
    uint16_t value_size;
    data = enmx_monitor(eibsock, 0xffff, data, &datalen, &value_size);
    if (!data)
    {
        cError() << "EIB Read error.";

        switch (enmx_geterror(eibsock))
        {
        case ENMX_E_COMMUNICATION:
        case ENMX_E_NO_CONNECTION:
        case ENMX_E_WRONG_USAGE:
        case ENMX_E_NO_MEMORY:
            cError() << "Error on write: " << enmx_errormessage(eibsock);
            enmx_close(eibsock);
            removeFd(eibsock);
            eibsock = -1;
            break;
        case ENMX_E_INTERNAL:
            cError() << "Bad status returned";
            break;
        case ENMX_E_SERVER_ABORTED:
            cError() << "Server aborted: " << enmx_errormessage(eibsock);
            enmx_close(eibsock);
            removeFd(eibsock);
            eibsock = -1;
            break;
        case ENMX_E_TIMEOUT:
            cDebug() << "Timeout monitor.";
            break;
        }

        return true;
    }

    CEMIFRAME *cemiframe = (CEMIFRAME *)data;

    string addr = (cemiframe->ntwrk & EIB_DAF_GROUP)? knxGroupAddr(cemiframe->daddr):knxPhysicalAddr(cemiframe->daddr);
    cDebug() << "Received: " << knxPhysicalAddr(cemiframe->saddr) << "  " << addr;

    if (cemiframe->apci & (A_WRITE_VALUE_REQ | A_RESPONSE_VALUE_REQ))
    {
        KNXValue v;
        if (v.setValue(0, cemiframe, cemiframe->length, true))
            cDebug() << "Value : " << v.toString();

        Params p = {{"type", "monitor"},
                    {"group_addr", addr}};
        json_t *j = p.toJson();
        json_object_set_new(j, "value", v.toJson());

        string res = jansson_to_string(j);
        if (!res.empty())
            sendMessage(res);
    }

    return true;
}

void KNXProcess::writeKnxValue(const string &group_addr, const KNXValue &value)
{
    unsigned char *p_val = nullptr;
    uint32_t val_int32;
    uint16_t knx_addr = enmx_getaddress(group_addr.c_str());

    switch (value.eis)
    {
    default: p_val = (unsigned char *)&value.value_int; break;
    case 11: val_int32 = value.value_int; p_val = (unsigned char *)&val_int32; break;
    case 5:
    case 9: p_val = (unsigned char *)&value.value_float; break;
    case 13: p_val = (unsigned char *)&value.value_char; break;
    case 15: p_val = (unsigned char *)value.value_string.c_str(); break;
    case 12: break;
    }

    unsigned char *data = (unsigned char *)malloc(enmx_EISsizeKNX[value.eis]);
    if (enmx_value2eis(value.eis, (void *)p_val, data) != 0)
    {
        free(data);
        cError() << "Error in value conversion";
        return;
    }

    int len = (value.eis != 15)? enmx_EISsizeKNX[value.eis] : value.value_string.length();
    if (enmx_write(eibsock, knx_addr, len, data) != 0)
    {
        cError() << "Unable to send command: " << enmx_errormessage(eibsock);
        free(data);
        return;
    }

    free(data);
}

EXTERN_PROC_CLIENT_MAIN(KNXProcess)
