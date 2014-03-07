/******************************************************************************
 **  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos Home.
 **
 **  Calaos Home is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos Home is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include <TCPConnection.h>
#include <CalaosConfig.h>
#include <NTPClock.h>
#include <PollListenner.h>

using namespace Calaos;

void TCPConnection::BaseCommand(Params &request, ProcessDone_cb callback)
{
        Params result = request;
        if (request["0"] == "version")
        {
                cDebugDom("network") << "TCPConnection::BaseCommand(version)" << log4cpp::eol;
                if (request["1"] == "?")
                        result.Add("1", Utils::get_config_option("fw_version"));
        }
        else if (request["0"] == "save")
        {
                cDebugDom("network") << "TCPConnection::BaseCommand(save)" << log4cpp::eol;

                Config::Instance().SaveConfigIO();
                Config::Instance().SaveConfigRule();

                if (request["1"] == "default")
                {
                        //copy the default config files
                        Utils::file_copy(ETC_DIR"io.xml", "/mnt/ext3/calaos/io.default");
                        Utils::file_copy(ETC_DIR"rules.xml", "/mnt/ext3/calaos/rules.default");
                }
        }
        else if (request["0"] == "system")
        {
                cDebugDom("network") << "TCPConnection::BaseCommand(system)" << log4cpp::eol;

                if (request["1"] == "reboot")
                {
                        if (request["2"] == "calaos_gui")
                        {
                                int unused = system("killall -9 calaos_gui");
                                unused = system("killall -9 calaos_thumb");
                                (void)unused;
                        }
                        else if (request["2"] == "calaosd")
                        {
                                int unused = system("killall -9 calaosd");
                                (void)unused;
                        }
                        else if (request["2"] == "all")
                        {
                                int unused = system("reboot");
                                (void)unused;
                        }
                }
                else if(request["1"] == "date")
                {
                        vector<string> vcmd;
                        for(int i=0;i<request.size();i++)
                        {
                                string s = Utils::to_string(i);
                                vcmd.push_back(request[s]);

                        }
                        NTPClock::Instance().setNetworkCmdCalendarApply(vcmd);
                        NTPClock::Instance().setRestartWhenApply(false);
                        IPC::Instance().SendEvent("CalaosCommon::NTPClock","applyCalendar",NULL);

                        result.Add("2", "ok");

                        string cmd = "";
                        for(int i=0;i<9;i++)
                                cmd+=request[Utils::to_string(i)]+" ";
                        //envoie de la commandes aux clients
                        TCPSocket *sock;
                        sock = new TCPSocket;
                        sock->Create(UDP);
                        sock->Broadcast(cmd, BCAST_UDP_PORT);
                        sock->Close();
                        delete sock;
                }
        }
        else if (request["0"] == "firmware")
        {
                cDebugDom("network") << "TCPConnection::BaseCommand(firmware)" << log4cpp::eol;

                if (request["1"] == "webupdate")
                {
                        //try to update firmware from /tmp/image.tar.bz2
                        cDebugDom("network") << "TCPConnection::BaseCommand(save): Firmware update requested by web." << log4cpp::eol;
                        int unused = system("fw_update.sh");
                        (void)unused;
                }
        }
        else if (request["0"] == "poll_listen")
        {
                cDebugDom("network") << "TCPConnection::BaseCommand(poll_listen)" << log4cpp::eol;

                if (request["1"] == "register")
                {
                        string uuid = PollListenner::Instance().Register();
                        result.Add("2", uuid);
                }
                else if (request["1"] == "unregister")
                {
                        if (PollListenner::Instance().Unregister(request["2"]))
                                result.Add("2", "true");
                        else
                                result.Add("2", "false");
                }
                else if (request["1"] == "get")
                {
                        Params events;

                        bool res = PollListenner::Instance().GetEvents(request["2"], events);

                        result.Add("2", "");

                        if (!res)
                        {
                                result.Add("2", "error");
                        }
                        else
                        {
                                int c = 2;

                                for (int i = 0;i < events.size();i++)
                                {
                                        string key, value;
                                        events.get_item(i, key, value);

                                        result.Add(Utils::to_string(c), key + ":" + url_encode(value));

                                        c++;
                                }
                        }
                }
        }

        ProcessDone_signal sig;
        sig.connect(callback);
        sig.emit(result);
}
