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

    int idx;
    json_t *value;

//    json_array_foreach(jroot, idx, value)
//    {
//        Params p;
//        jansson_decode_object(value, p);

//        if (p.Exists("channel") && p.Exists("value"))
//        {
//            unsigned int channel;
//            unsigned int val;
//            Utils::from_string(p["channel"], channel);
//            Utils::from_string(p["value"], val);

//            cDebugDom("knx") << "Set channel " << channel << " with value: " << val;
//        }
//    }
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
    run(1000);

    return 0;
}

EXTERN_PROC_CLIENT_MAIN(KNXProcess)
