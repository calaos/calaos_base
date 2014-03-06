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
#include <NTPClock.h>
#include <TCPServer.h>

extern NTPClock *ntpclock;

TCPConnection::TCPConnection(Ecore_Con_Client *cl):
                client_conn(cl),
                login(false)
{
        Utils::logger("network") << Priority::DEBUG << "TCPConnection::TCPConnection("
                                 << this << "): Ok" << log4cpp::eol;
}

TCPConnection::~TCPConnection()
{
        CloseConnection();

        Utils::logger("network") << Priority::DEBUG << "TCPConnection::~TCPConnection("
                                 << this << "): Ok" << log4cpp::eol;
}

void TCPConnection::ProcessRequest(Params &request, ProcessDone_cb callback)
{
        Params result = request;

        /* login <user> <password> */
        if (!login && request["0"] == "login")
        {
                Utils::logger("network") << Priority::DEBUG << "TCPConnection::ProcessRequest(login)" << log4cpp::eol;

                std::string user = Utils::get_config_option("calaos_user");
                std::string pass = Utils::get_config_option("calaos_password");

                if (Utils::get_config_option("cn_user") != "" &&
                    Utils::get_config_option("cn_pass") != "")
                {
                        user = Utils::get_config_option("cn_user");
                        pass = Utils::get_config_option("cn_pass");
                }

                if (user == request["1"] && pass == request["2"])
                        login = true;
                else
                {
                        CloseConnection();

                        Utils::logger("network") << Priority::WARN
                                        << "TCPConnection: Wrong username/password got (" << request["1"]
                                << "/" << request["2"] << ")" << log4cpp::eol;
                }

                //changes the password for the response (security)
                result.Add("2", "ok");

                ProcessDone_signal sig;
                sig.connect(callback);
                sig.emit(result);

                return;
        }

        if (!login)
        {
                ProcessDone_signal sig;
                sig.connect(callback);
                sig.emit(result);

                return;
        }

        //"listen" is a special command that never returns unless it fails.
        //Clients can listen to all events that happens.
        if (request["0"] == "listen") ListenCommand();

        if (listen_mode) return; //do not treat any other command if in listen mode

        bool request_handled = true;

        if (request["0"] == "version") BaseCommand(request, callback);
        else if (request["0"] == "save") BaseCommand(request, callback);
        else if (request["0"] == "system") BaseCommand(request, callback);
        else if (request["0"] == "firmware") BaseCommand(request, callback);
        else if (request["0"] == "camera") CameraCommand(request, callback);
        else if (request["0"] == "home") HomeCommand(request, callback);
        else if (request["0"] == "room") HomeCommand(request, callback);
        else if (request["0"] == "input") IOCommand(request, callback);
        else if (request["0"] == "output") IOCommand(request, callback);
        else if (request["0"] == "io") IOCommand(request, callback);
        else if (request["0"] == "rules") RulesCommand(request, callback);
        else if (request["0"] == "audio") AudioCommand(request, callback);
        else if (request["0"] == "poll_listen") BaseCommand(request, callback);
        else if (request["0"] == "scenario") ScenarioCommand(request, callback);
        else request_handled = false;

        if (!request_handled)
        {
                ProcessDone_signal sig;
                sig.connect(callback);
                sig.emit(result);
        }
}

void TCPConnection::ProcessData(string request)
{
        if (request.find('\n') == string::npos &&
            request.find('\r') == string::npos)
        {
                //We have not a complete paquet yet, buffurize it.
                buffer += request;
                return;
        }

        if (!buffer.empty())
        {
                //Put last data in buffer
                buffer += request;
                request = buffer;
                buffer.clear();
        }

        //Clean data string
        int i = request.length() - 1;
        while ((request[i] == '\n' || request[i] == '\r' || request[i] == '\0') && i >= 0) i--;

        terminator = request.substr(i + 1, request.length() - i + 1);
        request.erase(i + 1, request.length() - i + 1);

        Utils::logger("network") << Priority::DEBUG
                        << "TCPConnection::ProcessData(): New request: \"" << request << "\"" << log4cpp::eol;

        Params p;
        p.Parse(request);

        //url decode all
        for (i = 0;i < p.size();i++)
        {
                p.Add(Utils::to_string(i), Utils::url_decode(p[Utils::to_string(i)]));
        }

        /* exit */
        if (p["0"] == "exit")
        {
                CloseConnection();
        }
        else if (p["0"] == "listen")
        {
                Utils::logger("network") << Priority::DEBUG
                                << "TCPConnection::ProcessData(): Entering listen mode for client " << ecore_con_client_ip_get(client_conn) << log4cpp::eol;

                listen_mode = true;

                ProcessRequest(p, sigc::mem_fun(*this, &TCPConnection::ProcessingDataDone));
                //Don't bother with the responde for "listen" command,
                //it's a special command that echos all events
        }
        else
        {
                ProcessRequest(p, sigc::mem_fun(*this, &TCPConnection::ProcessingDataDone));
        }
}

void TCPConnection::ProcessingDataDone(Params &response)
{
        std::string res = "";

        //url encode
        for (int i = 0;i < response.size();i++)
        {
                response.Add(Utils::to_string(i), Utils::url_encode(response[Utils::to_string(i)]));
                res += response[Utils::to_string(i)];
                if (i < response.size() - 1)
                        res += " ";
        }

        Utils::logger("network") << Priority::DEBUG
                        << "TCPConnection::ProcessData(): We send: \"" << res << "\"" << log4cpp::eol;
        res += terminator;

        if (!client_conn || ecore_con_client_send(client_conn, res.c_str(), res.length()) == 0)
        {
                Utils::logger("network") << Priority::CRIT
                                << "TCPConnection::ProcessData(): Error sending data ! Closing connection." << log4cpp::eol;

                CloseConnection();
        }
}

void TCPConnection::CloseConnection()
{
        if (client_conn)
        {
                ecore_con_client_del(client_conn);
                client_conn = NULL;
        }

        //Remove IPC handler if any
        IPC::Instance().DeleteHandler(sig_events);
}
