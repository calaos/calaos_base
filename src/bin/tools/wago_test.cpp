/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <iostream>
#include <fstream>
#include <sstream>
#include <Utils.h>
#include <tcpsocket.h>
#include <Params.h>


int main(int argc, char **argv)
{
    TCPSocket *socket = new TCPSocket();
    socket->Create(UDP);
    char buffer[256];
    std::string cmd;
    std::string host, action;
    int address = 0;
    std::string type;
    int addr1 = 0, addr2 = 0, sameas = 0;
    std::stringstream s;
    std::string file, ip, boolean;
    int dali_line = 0, dali_group = 0, dali_addr = 0, dali_value = 0, dali_fade = 0, dali_time = 0;
    int dali_faderate = 0, dali_fadetime = 0, dali_maxlevel = 0, dali_minlevel = 0, dali_systemfailurelevel = 0, dali_poweronlevel = 0;

    if (argc < 4)
    {
        std::cout << "config_wago Usage:" << std::endl;
        std::cout << "\t" << argv[0] << " host <ip address> action <action> [Action options]" << std::endl;
        std::cout << std::endl;
        std::cout << "\tWhere action:" << std::endl;
        std::cout << "\t\tset_outtype: <address> <type> (type=NONE, TELERUPTEUR, DIRECT, VOLET, VOLET_IMPULSE, TELERUPTEUR_DALI, TELERUPTEUR_DALI_GROUP, TELERUPTEUR_KNX_OUTPUT, DIRECT_KNX_OUTPUT)" << std::endl;
        std::cout << "\t\tget_outtype: <address>" << std::endl;
        std::cout << "\t\tset_outaddr: <address> <out1> <out2> <SameAs>" << std::endl;
        std::cout << "\t\tget_outaddr: <address>" << std::endl;
        std::cout << "\t\tget_version" << std::endl;
        std::cout << "\t\tget_info" << std::endl;
        std::cout << "\t\tget_info_module: <module>" << std::endl;
        std::cout << "\t\tget_volet_positions: <var_save>" << std::endl;
        std::cout << "\t\tset_server_ip: <ip>" << std::endl;
        std::cout << "\t\tset_output: <out> <boolean>" << std::endl;
        std::cout << "\t\tset_knx_output: <out> <boolean>" << std::endl;
        std::cout << "\t\theartbeat (send heartbeat to the PLC)" << std::endl;
        std::cout << "\t\tset_dali: <dali_line> <group?> <address> <value> <fade_time>" << std::endl;
        std::cout << "\t\tget_dali: <dali_line> <address>" << std::endl;
        std::cout << "\t\tdali_search_devices: <dali_line>" << std::endl;
        std::cout << "\t\tdali_get_device_info: <dali_line> <address>" << std::endl;
        std::cout << "\t\tdali_get_device_group: <dali_line> <group>" << std::endl;
        std::cout << "\t\tdali_device_add_group: <dali_line> <address> <group>" << std::endl;
        std::cout << "\t\tdali_device_del_group: <dali_line> <address> <group>" << std::endl;
        std::cout << "\t\tdali_central: <dali_line> <boolean> (switch on/off all DALI lamps)" << std::endl;
        std::cout << "\t\tdali_blink: <dali_line> <address> <group?> <blink_time>" << std::endl;
        std::cout << "\t\tdali_blink_stop: <dali_line>" << std::endl;
        std::cout << "\t\tdali_set_device_config: <dali_line> <address> <fade_rate> <fade_time> <max_level> <min_level> <system_failure_level> <power_on_level>" << std::endl;
        std::cout << "\t\tdali_addressing_new: <dali_line> <reset?>" << std::endl;
        std::cout << "\t\tdali_addressing_add: <dali_line>" << std::endl;
        std::cout << "\t\tdali_addressing_status: <dali_line>" << std::endl;
        std::cout << "\t\tdali_addressing_change: <dali_line> <old_address> <new_address>" << std::endl;
        std::cout << "\t\tdali_addressing_del: <dali_line> <address_to_delete>" << std::endl;

        std::cout << std::endl;
        std::cout << "\t\tsave_config: <file>" << std::endl;
        std::cout << "\t\tload_config: <file>" << std::endl;
        std::cout << std::endl;
    }
    else
    {
        for (int i = 0;i < argc;i++)
        {
            std::string arg = argv[i];
            if (arg == "host")
                host = argv[i + 1];
            else if (arg == "action")
            {
                action = argv[i + 1];
                i++;

                if (action == "set_outtype")
                {
                    address = atoi(argv[i + 1]);
                    type = argv[i + 2];
                }
                else if (action == "get_outtype")
                {
                    address = atoi(argv[i + 1]);
                }
                else if (action == "set_outaddr")
                {
                    address = atoi(argv[i + 1]);
                    addr1 = atoi(argv[i + 2]);
                    addr2 = atoi(argv[i + 3]);
                    sameas = atoi(argv[i + 4]);
                }
                else if (action == "get_outaddr")
                {
                    address = atoi(argv[i + 1]);
                }
                else if (action == "get_info_module")
                {
                    address = atoi(argv[i + 1]);
                }
                else if (action == "save_config" || action == "load_config")
                {
                    file = argv[i + 1];
                }
                else if (action == "set_server_ip")
                {
                    ip = argv[i + 1];
                }
                else if (action == "set_output")
                {
                    addr1 = atoi(argv[i + 1]);
                    boolean = argv[i + 2];
                }
                else if (action == "set_knx_output")
                {
                    addr1 = atoi(argv[i + 1]);
                    boolean = argv[i + 2];
                }
                else if (action == "set_dali")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_group = atoi(argv[i + 2]);
                    dali_addr = atoi(argv[i + 3]);
                    dali_value = atoi(argv[i + 4]);
                    dali_fade = atoi(argv[i + 5]);
                }
                else if (action == "get_dali")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                }
                else if (action == "dali_search_devices")
                {
                    dali_line = atoi(argv[i + 1]);
                }
                else if (action == "dali_get_device_info")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                }
                else if (action == "dali_get_device_group")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                }
                else if (action == "dali_central")
                {
                    dali_line = atoi(argv[i + 1]);
                    boolean = argv[i + 2];
                }
                else if (action == "dali_blink")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                    dali_group = atoi(argv[i + 3]);
                    dali_time = atoi(argv[i + 4]);
                }
                else if (action == "dali_blink_stop")
                {
                    dali_line = atoi(argv[i + 1]);
                }
                else if (action == "dali_device_add_group")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                    dali_group = atoi(argv[i + 3]);
                }
                else if (action == "dali_device_del_group")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                    dali_group = atoi(argv[i + 3]);
                }
                else if (action == "dali_set_device_config")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                    dali_faderate = atoi(argv[i + 3]);
                    dali_fadetime = atoi(argv[i + 4]);
                    dali_maxlevel = atoi(argv[i + 5]);
                    dali_minlevel = atoi(argv[i + 6]);
                    dali_systemfailurelevel = atoi(argv[i + 7]);
                    dali_poweronlevel = atoi(argv[i + 8]);
                }
                else if (action == "dali_addressing_new")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                }
                else if (action == "dali_addressing_add")
                {
                    dali_line = atoi(argv[i + 1]);
                }
                else if (action == "dali_addressing_status")
                {
                    dali_line = atoi(argv[i + 1]);
                }
                else if (action == "dali_addressing_change")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                    dali_value = atoi(argv[i + 3]);
                }
                else if (action == "dali_addressing_del")
                {
                    dali_line = atoi(argv[i + 1]);
                    dali_addr = atoi(argv[i + 2]);
                }
                else if (action == "get_info" || action == "get_version")
                {
                }
                else if (action == "get_volet_positions")
                {
                    address = atoi(argv[i + 1]);
                }
                else
                {
                    std::cout << "action not found: " << action << std::endl;
                    exit(1);
                }
            }
        }
    }

    if (action == "set_outaddr")
    {
        s << "WAGO_SET_OUTADDR " << address << " " << addr1 << " " << addr2 << " " << sameas;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "heartbeat")
    {
        s << "WAGO_HEARTBEAT";
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "set_outtype")
    {
        int t = 0;
        if (type == "NONE") t = 0;
        if (type == "TELERUPTEUR") t = 1;
        if (type == "DIRECT") t = 2;
        if (type == "VOLET") t = 3;
        if (type == "VOLET_IMPULSE") t = 4;
        if (type == "TELERUPTEUR_DALI") t = 5;
        if (type == "TELERUPTEUR_DALI_GROUP") t = 6;
        if (type == "TELERUPTEUR_KNX_OUTPUT") t = 7;
        if (type == "DIRECT_KNX_OUTPUT") t = 8;
        s << "WAGO_SET_OUTTYPE " << address << " " << t;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "get_outtype")
    {
        s << "WAGO_GET_OUTTYPE " << address;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;
        Params p;
        p.Parse(cmd);
        std::cout << "Type of input " << p["1"] << ":" << std::endl;
        if (p["2"] == "0") std::cout << "\tNONE" << std::endl;
        if (p["2"] == "1") std::cout << "\tTELERUPTEUR" << std::endl;
        if (p["2"] == "2") std::cout << "\tDIRECT" << std::endl;
        if (p["2"] == "3") std::cout << "\tVOLET" << std::endl;
        if (p["2"] == "4") std::cout << "\tVOLET_IMPULSE" << std::endl;
    }
    else if (action == "get_outaddr")
    {
        s << "WAGO_GET_OUTADDR " << address;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;
        Params p;
        p.Parse(cmd);
        std::cout << "Address for input " << p["1"] << ":" << std::endl;
        std::cout << "\tAddress 1: " << p["2"] << std::endl;
        std::cout << "\tAddress 2: " << p["3"] << std::endl;
        std::cout << "\tSameAs: " << p["4"] << std::endl;
    }
    else if (action == "get_info")
    {
        s << "WAGO_GET_INFO";
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;
        Params p;
        p.Parse(cmd);
        std::cout << "WAGO Infos :" << std::endl;
        std::cout << "\tTotal nb modules: " << p["1"] << std::endl;
        std::cout << "\tnb input modules: " << p["2"] << std::endl;
        std::cout << "\tnb output modules: " << p["3"] << std::endl;
        std::cout << "\tnb input digital: " << p["4"] << std::endl;
        std::cout << "\tnb output digital: " << p["5"] << std::endl;
        std::cout << "\tnb input analog: " << p["6"] << std::endl;
        std::cout << "\tnb output analog: " << p["7"] << std::endl;
    }
    else if (action == "get_version")
    {
        s << "WAGO_GET_VERSION";
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;
        Params p;
        p.Parse(cmd);
        std::cout << "WAGO type: " << p["2"] << std::endl << "Calaos version: " << p["1"] << std::endl;
    }
    else if (action == "get_volet_positions")
    {
        s << "WAGO_INFO_VOLET_GET " << address;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;
        Params p;
        p.Parse(cmd);
        std::cout << "WAGO volet " << p["1"] << " position: " << p["2"] << std::endl;
    }
    else if (action == "get_info_module")
    {
        s << "WAGO_GET_INFO_MODULE " << address;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;
        Params p;
        p.Parse(cmd);
        std::cout << "WAGO module " << p["1"] << " :" << std::endl;
        std::cout << "\tmodule type: " << p["2"] << std::endl;
        std::cout << "\tphysical position: " << p["3"] << std::endl;
        std::cout << "\tinput size: " << p["4"] << std::endl;
        std::cout << "\toutput size: " << p["5"] << std::endl;
    }
    else if (action == "save_config")
    {
        std::ofstream f(file.c_str());
        for (int i = 0;i < 256;i++)
        {
            {
                std::stringstream sstr;
                sstr << "WAGO_GET_OUTTYPE " << i;
                cmd = sstr.str();
                std::cout << "Sending \"" << cmd << "\"..." << std::endl;
                socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
                socket->RecvFrom(buffer, 256);
                cmd = buffer;
                std::cout << "Receiving \"" << cmd << "\"..." << std::endl;

                Params p;
                p.Parse(cmd);
                f << "WAGO_SET_OUTTYPE " << i << " " << p["2"] << std::endl;
            }

            {
                std::stringstream sstr;
                sstr << "WAGO_GET_OUTADDR " << i;
                cmd = sstr.str();
                std::cout << "Sending \"" << cmd << "\"..." << std::endl;
                socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
                socket->RecvFrom(buffer, 256);
                cmd = buffer;
                std::cout << "Receiving \"" << cmd << "\"..." << std::endl;

                Params p;
                p.Parse(cmd);
                f << "WAGO_SET_OUTADDR " << i << " " << p["2"] << " " << p["3"] << " " << p["4"] << std::endl;
            }
        }
        f.close();
    }
    else if (action == "load_config")
    {
        std::ifstream f(file.c_str());
        while (!f.eof())
        {
            getline (f, cmd);
            std::cout << "Sending \"" << cmd << "\"..." << std::endl;
            socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);

            //little pause
            struct timespec t;
            t.tv_sec = 0;
            t.tv_nsec = 800 * 1000000; //200ms
            nanosleep(&t, NULL);
        }
        f.close();
    }
    else if (action == "set_server_ip")
    {
        s << "WAGO_SET_SERVER_IP " << ip;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "set_output")
    {
        s << "WAGO_SET_OUTPUT " << addr1 << " ";
        if (boolean == "true") s << "1"; else s << "0";
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "set_knx_output")
    {
        s << "WAGO_SET_KNX_OUTPUT " << addr1 << " ";
        if (boolean == "true") s << "1"; else s << "0";
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "set_dali")
    {
        s << "WAGO_DALI_SET ";
        s << dali_line << " ";
        s << dali_group << " ";
        s << dali_addr << " ";
        s << dali_value << " ";
        s << dali_fade << " ";
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "get_dali")
    {
        s << "WAGO_DALI_GET " << dali_line << " " << dali_addr;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;
        Params p;
        p.Parse(cmd);
        std::cout << "DALI slave, short address: " << dali_addr << " :" << std::endl;
        std::cout << "\tlight is on: " << p["1"] << std::endl;
        std::cout << "\tdimm value: " << p["2"] << std::endl;
    }
    else if (action == "dali_search_devices")
    {
        s << "WAGO_DALI_GET_ADDR";
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;

        std::cout << cmd << std::endl;
        /*Params p;
                p.Parse(cmd);
                std::cout << "DALI slave, short address: " << dali_addr << " :" << std::endl;
                std::cout << "\tlight is on: " << p["1"] << std::endl;
                std::cout << "\tdimm value: " << p["2"] << std::endl;*/
    }
    else if (action == "dali_get_device_info")
    {
        s << "WAGO_DALI_GET_DEVICE_INFO " << dali_line << " " << dali_addr;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;

        std::cout << cmd << std::endl;
        /*Params p;
                p.Parse(cmd);
                std::cout << "DALI slave, short address: " << dali_addr << " :" << std::endl;
                std::cout << "\tlight is on: " << p["1"] << std::endl;
                std::cout << "\tdimm value: " << p["2"] << std::endl;*/
    }
    else if (action == "dali_get_device_group")
    {
        s << "WAGO_DALI_GET_DEVICE_GROUP " << dali_line << " " << dali_addr;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;

        std::cout << cmd << std::endl;
        /*Params p;
                p.Parse(cmd);
                std::cout << "DALI slave, short address: " << dali_addr << " :" << std::endl;
                std::cout << "\tlight is on: " << p["1"] << std::endl;
                std::cout << "\tdimm value: " << p["2"] << std::endl;*/
    }
    else if (action == "dali_central")
    {
        s << "WAGO_DALI_CENTRAL " << dali_line << " " << ((boolean == "true")?"1":"0");
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "dali_blink")
    {
        s << "WAGO_DALI_BLINK " << dali_line << " " << dali_addr << " " << dali_group << " " << dali_time;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "dali_blink_stop")
    {
        s << "WAGO_DALI_BLINK_STOP " << dali_line;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "dali_device_add_group")
    {
        s << "WAGO_DALI_DEVICE_ADD_GROUP " << dali_line << " " << dali_addr << " " << dali_group;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);

        socket->RecvFrom(buffer, 256);
        cmd = buffer;

        std::cout << cmd << std::endl;
    }
    else if (action == "dali_device_del_group")
    {
        s << "WAGO_DALI_DEVICE_DEL_GROUP " << dali_line << " " << dali_addr << " " << dali_group;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);

        socket->RecvFrom(buffer, 256);
        cmd = buffer;

        std::cout << cmd << std::endl;
    }
    else if (action == "dali_set_device_config")
    {
        s << "WAGO_DALI_SET_DEVICE_CONFIG " << dali_line << " " << dali_addr << " "
          << dali_faderate << " " << dali_fadetime << " "
          << dali_maxlevel << " " << dali_minlevel << " "
          << dali_systemfailurelevel << " " << dali_poweronlevel;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "dali_addressing_new")
    {
        s << "WAGO_DALI_ADDRESSING_NEW " << dali_line << " " << dali_addr;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "dali_addressing_add")
    {
        s << "WAGO_DALI_ADDRESSING_ADD " << dali_line;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "dali_addressing_status")
    {
        s << "WAGO_DALI_ADDRESSING_STATUS " << dali_line;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
        socket->RecvFrom(buffer, 256);
        cmd = buffer;

        std::cout << cmd << std::endl;
    }
    else if (action == "dali_addressing_change")
    {
        s << "WAGO_DALI_ADDRESSING_CHANGE " << dali_line << " " << dali_addr << " " << dali_value;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }
    else if (action == "dali_addressing_del")
    {
        s << "WAGO_DALI_ADDRESSING_DEL " << dali_line << " " << dali_addr;
        cmd = s.str();
        std::cout << "Sending \"" << cmd << "\"..." << std::endl;
        socket->SendTo(cmd.c_str(), cmd.size(), WAGO_LISTEN_PORT, host);
    }

    socket->Close();
    delete socket;

    return 0;
}
