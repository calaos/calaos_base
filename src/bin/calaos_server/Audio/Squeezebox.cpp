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
#include <Squeezebox.h>
#include <SqueezeboxDB.h>
#include <AudioManager.h>
#include <FileDownloader.h>
#include <IOFactory.h>

// The JSON parser
#include <jansson.h>

#define SQ_TIMEOUT      40.0
#define SQ_RECONNECT    3.0

using namespace Calaos;

REGISTER_AUDIO_USERTYPE(slim, Squeezebox)
REGISTER_AUDIO(Squeezebox)

static Eina_Bool _con_server_add(void *data, int type, Ecore_Con_Event_Server_Add *ev)
{
    Squeezebox *o = reinterpret_cast<Squeezebox *>(data);

    if (ev && ev->server && (o != ecore_con_server_data_get(ev->server)))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (o)
    {
        o->addConnection(ev->server);
    }
    else
    {
        cCriticalDom("squeezebox") << "failed to get object !";
    }

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_del(void *data, int type, Ecore_Con_Event_Server_Del *ev)
{
    Squeezebox *o = reinterpret_cast<Squeezebox *>(data);

    if (ev && ev->server && (o != ecore_con_server_data_get(ev->server)))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (o)
    {
        o->delConnection(ev->server);
    }
    else
    {
        cCriticalDom("squeezebox") << "failed to get object !";
    }

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_data(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
    Squeezebox *o = reinterpret_cast<Squeezebox *>(data);

    if (ev && ev->server && (o != ecore_con_server_data_get(ev->server)))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (o)
    {
        o->dataGet(ev->server, ev->data, ev->size);
    }
    else
    {
        cCriticalDom("squeezebox") << "failed to get object !";
    }

    return ECORE_CALLBACK_RENEW;
}

Squeezebox::Squeezebox(Params &p):
    AudioPlayer(p),
    enotif(NULL),
    econ(NULL),
    econ_udp(NULL),
    ehandler_add(NULL),
    ehandler_del(NULL),
    ehandler_data(NULL),
    timer_notification(NULL),
    timer_con(NULL),
    timer_timeout(NULL),
    isConnected(false)
{
    host = param["host"];
    if (param.Exists("port_cli"))
    {
        Utils::from_string(param["port_web"], port_web);
        Utils::from_string(param["port_cli"], port_cli);
    }
    else
    {
        Utils::from_string(param["port"], port_cli);
        port_web = 9000;
        param.Add("port_cli", Utils::to_string(port_cli));
        param.Add("port_web", "9000");
    }
    if (param.Exists("port"))
        param.Delete("port");
    id = param["id"];

    cDebugDom("squeezebox") <<  "new device (" << id << ") at " << host << ":" << port_cli;

    //Create DB
    database = new SqueezeboxDB(this, param);

    ehandler_add = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, (Ecore_Event_Handler_Cb)_con_server_add, this);
    ehandler_del = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, (Ecore_Event_Handler_Cb)_con_server_del, this);
    ehandler_data = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)_con_server_data, this);

    econ_udp = ecore_con_server_connect(ECORE_CON_REMOTE_BROADCAST, "255.255.255.255", BCAST_UDP_PORT, NULL);

    timerConnReconnect();
    timerNotificationReconnect();

    timer_notification = new EcoreTimer(SQ_RECONNECT,
                                        (sigc::slot<void>)sigc::mem_fun(*this, &Squeezebox::timerNotificationReconnect));
    timer_con = new EcoreTimer(SQ_RECONNECT,
                               (sigc::slot<void>)sigc::mem_fun(*this, &Squeezebox::timerConnReconnect));
}

Squeezebox::~Squeezebox()
{
    if (timer_notification)
    {
        delete timer_notification;
        timer_notification = NULL;
    }
    if (timer_con)
    {
        delete timer_con;
        timer_con = NULL;
    }

    ecore_con_server_del(enotif);
    ecore_con_server_del(econ);
    ecore_con_server_del(econ_udp);

    ecore_event_handler_del(ehandler_add);
    ecore_event_handler_del(ehandler_del);
    ecore_event_handler_del(ehandler_data);

    cDebugDom("squeezebox");
}

void Squeezebox::timerNotificationReconnect()
{
    cDebugDom("squeezebox") <<  "Connecting to " << host << ":" << port_cli;

    if (enotif) ecore_con_server_del(enotif);
    enotif = ecore_con_server_connect(ECORE_CON_REMOTE_TCP, host.c_str(), port_cli, this);
    ecore_con_server_data_set(enotif, this);

    cDebugDom("squeezebox") <<  "econ == " << econ << " and enotif == " << enotif;
}

void Squeezebox::timerConnReconnect()
{
    cDebugDom("squeezebox") <<  "Connecting to " << host << ":" << port_cli;

    if (econ) ecore_con_server_del(econ);
    econ = ecore_con_server_connect(ECORE_CON_REMOTE_TCP, host.c_str(), port_cli, this);
    ecore_con_server_data_set(econ, this);

    cDebugDom("squeezebox") <<  "econ == " << econ << " and enotif == " << enotif;
}

void Squeezebox::addConnection(Ecore_Con_Server *srv)
{
    if (srv == enotif)
    {
        if (timer_notification)
        {
            delete timer_notification;
            timer_notification = NULL;
        }

        //we need to subscribe to these commands to watch for status changes
        //string cmd = "subscribe playlist,mixer,pause,stop\n\r";
        string cmd = "listen 1\n\r";

        cDebugDom("squeezebox") <<  "trying to subscribe to events";

        ecore_con_server_send(enotif, cmd.c_str(), cmd.length());
    }
    else if (srv == econ)
    {
        if (timer_con)
        {
            delete timer_con;
            timer_con = NULL;
        }

        cDebugDom("squeezebox") <<  "Main connection established";
    }
    else
    {
        cWarningDom("squeezebox") << "Wrong Ecore_Con_Server object";
    }
}

void Squeezebox::delConnection(Ecore_Con_Server *srv)
{
    if (srv == enotif)
    {
        if (timer_notification)
        {
            delete timer_notification;
            timer_notification = NULL;
        }

        cWarningDom("squeezebox") <<  "Notification Connection closed !";
        cWarningDom("squeezebox") <<  "Trying to reconnect...";

        timer_notification = new EcoreTimer(SQ_RECONNECT,
                                            (sigc::slot<void>)sigc::mem_fun(*this, &Squeezebox::timerNotificationReconnect));

        isConnected = false;
    }
    else if (srv == econ)
    {
        if (timer_con)
        {
            delete timer_con;
            timer_con = NULL;
        }

        cWarningDom("squeezebox") <<  "Main Connection closed !";
        cWarningDom("squeezebox") <<  "Trying to reconnect...";

        timer_con = new EcoreTimer(SQ_RECONNECT,
                                   (sigc::slot<void>)sigc::mem_fun(*this, &Squeezebox::timerConnReconnect));

        isConnected = false;
    }
    else
    {
        cWarningDom("squeezebox")
                << "Wrong Ecore_Con_Server object (" << srv << ") where econ == " << econ
                << " and enotif == " << enotif;
    }
}

void Squeezebox::dataGet(Ecore_Con_Server *srv, void *data, int size)
{
    string msg((char *)data, size);

    if (srv == enotif)
    {
        if (msg.find('\n') == string::npos &&
            msg.find('\r') == string::npos)
        {
            //We have not a complete paquet yet, buffurize it.
            buffer_notif += msg;

            cDebugDom("squeezebox") <<  "Bufferize data.";

            return;
        }

        if (!buffer_notif.empty())
        {
            msg = buffer_notif;
            buffer_notif.clear();
        }

        //Clean data string
        int i = msg.length() - 1;
        while ((msg[i] == '\n' || msg[i] == '\r' || msg[i] == '\0') && i >= 0) i--;

        replace_str(msg, "\r\n", "\n");
        replace_str(msg, "\r", "\n");

        vector<string> tokens;
        split(msg, tokens, "\n");

        isConnected = true;
        cDebugDom("squeezebox") <<  "Got " << tokens.size() << " messages.";

        for(uint j = 0; j < tokens.size(); j++)
            processNotificationMessage(tokens[j]);
    }
    else if (srv == econ)
    {
        if (msg.find('\n') == string::npos &&
            msg.find('\r') == string::npos)
        {
            //We have not a complete paquet yet, buffurize it.
            buffer_main += msg;

            cDebugDom("squeezebox") <<  "Bufferize data.";

            return;
        }

        if (!buffer_main.empty())
        {
            msg = buffer_main;
            buffer_main.clear();
        }

        //Clean data string
        int i = msg.length() - 1;
        while ((msg[i] == '\n' || msg[i] == '\r' || msg[i] == '\0') && i >= 0) i--;

        replace_str(msg, "\r\n", "\n");
        replace_str(msg, "\r", "\n");

        vector<string> tokens;
        split(msg, tokens, "\n");

        cDebugDom("squeezebox") <<  "Got " << tokens.size() << " messages.";

        for(uint j = 0; j < tokens.size(); j++)
            processMessage(true, tokens[j]);
    }
    else
    {
        cWarningDom("squeezebox") << "Wrong Ecore_Con_Server object";
    }
}

void Squeezebox::processNotificationMessage(string msg)
{
    cDebugDom("squeezebox") << "Message: \"" << msg << "\"";

    Params p;
    p.Parse(msg);

    if (param["id"] != url_decode2(p["0"]))
        return;

    int pid;
    from_string(param["pid"], pid);

    if (p["1"] == "playlist")
    {
        if (p["2"] == "open" || p["2"] == "newsong") //current song changes !
        {
            string notify = "songchanged player " + Utils::to_string(pid);

            ecore_con_server_send(econ_udp, notify.c_str(), notify.size());

            //notify the player's input
            AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
            if (ain) ain->set_status(AudioSongChange);

            string sig = "audio ";
            sig += get_param("pid") + " songchanged";
            IPC::Instance().SendEvent("events", sig);
        }
        else if (p["2"] == "move")
        {
            string notify = "playlist move " + p["3"] + " " + p["4"] + " " + Utils::to_string(pid);;

            ecore_con_server_send(econ_udp, notify.c_str(), notify.size());

            //notify the player's input
            AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
            if (ain) ain->set_status(AudioPlaylistChange);

            string sig = "audio_playlist ";
            sig += get_param("pid") + " playlist move " + p["3"] + " " + p["4"];
            IPC::Instance().SendEvent("events", sig);
        }
        else if (p["2"] == "delete")
        {
            string notify = "playlist delete " + p["3"] + " " + Utils::to_string(pid);;

            ecore_con_server_send(econ_udp, notify.c_str(), notify.size());

            //notify the player's input
            AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
            if (ain) ain->set_status(AudioPlaylistChange);

            string sig = "audio_playlist ";
            sig += get_param("pid") + " playlist delete " + p["3"];
            IPC::Instance().SendEvent("events", sig);
        }
        else if (p["2"] == "loadtracks" || p["2"] == "clear" || p["2"] == "play" || p["2"] == "load")
        {
            string notify = "playlist reload " + Utils::to_string(pid);;

            ecore_con_server_send(econ_udp, notify.c_str(), notify.size());

            //notify the player's input
            AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
            if (ain) ain->set_status(AudioPlaylistChange);

            string sig = "audio_playlist ";
            sig += get_param("pid") + " playlist reload";
            IPC::Instance().SendEvent("events", sig);
        }
        else if (p["2"] == "addtracks" || p["2"] == "add")
        {
            string notify = "playlist tracksadded " + Utils::to_string(pid);;

            ecore_con_server_send(econ_udp, notify.c_str(), notify.size());

            //notify the player's input
            AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
            if (ain) ain->set_status(AudioPlaylistChange);

            string sig = "audio_playlist ";
            sig += get_param("pid") + " playlist tracksadded";
            IPC::Instance().SendEvent("events", sig);
        }
        else if (p["2"] == "clear")
        {
            string notify = "playlist cleared " + Utils::to_string(pid);;

            ecore_con_server_send(econ_udp, notify.c_str(), notify.size());

            //notify the player's input
            AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
            if (ain) ain->set_status(AudioPlaylistChange);

            string sig = "audio_playlist ";
            sig += get_param("pid") + " playlist cleared";
            IPC::Instance().SendEvent("events", sig);
        }
        else if (p["2"] == "pause")
        {
            //notify the player's input
            AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
            if (ain)
            {
                if (p["3"] == "" || p["3"] == "0")
                    ain->set_status(AudioPlay);
                else
                    ain->set_status(AudioPause);
            }

            string sig = "audio_status ";
            sig += get_param("pid") + " ";

            if (p["3"] == "" || p["3"] == "0")
                sig += "play";
            else
                sig += "pause";

            IPC::Instance().SendEvent("events", sig);
        }
        else if (p["2"] == "stop")
        {
            //notify the player's input
            AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
            if (ain) ain->set_status(AudioStop);

            string sig = "audio_status ";
            sig += get_param("pid") + " stop";
            IPC::Instance().SendEvent("events", sig);
        }
    }

    //volume change
    if (p["1"] == "mixer" && p["2"] == "volume")
    {
        string notify = "volumechanged " + Utils::to_string(pid) + " " + p["3"];

        ecore_con_server_send(econ_udp, notify.c_str(), notify.size());

        //notify the player's input
        AudioInput *ain = dynamic_cast<AudioInput *>(ainput);
        if (ain) ain->set_status(AudioVolumeChange);

        string sig = "audio_volume ";
        sig += get_param("pid") + " change " + p["3"];
        IPC::Instance().SendEvent("events", sig);
    }
}

void Squeezebox::processMessage(bool status, string msg)
{
    if (status)
    {
        cDebugDom("squeezebox") <<  "Message: \"" << msg << "\"";
    }
    else
    {
        cDebugDom("squeezebox") <<  "sending failed !";

        //Try reconnecting to avoid unwanted data from old command to alter current command
        if (ecore_con_server_connected_get(econ))
        {
            delConnection(econ);
            delConnection(enotif);
        }
    }

    if (timer_timeout)
    {
        delete timer_timeout;
        timer_timeout = NULL;
    }

    if (squeeze_commands.size() > 0)
    {
        SqueezeboxCommand &cmd = squeeze_commands.front();

        cmd.status = status;
        cmd.result = msg;

        if (!cmd.noCallback)
        {
            SqueezeRequest_signal sig;
            sig.connect(cmd.callback);
            sig.emit(status, cmd.request, cmd.result, cmd.user_data);
        }

        squeeze_commands.pop();
    }

    //continue to send request until we have no more
    _sendRequest();
}

void Squeezebox::sendRequest(string command, SqueezeRequest_cb callback, AudioPlayerData user_data)
{
    SqueezeboxCommand cmd;
    cmd.request = command;
    cmd.callback = callback;
    cmd.user_data = user_data;

    squeeze_commands.push(cmd);

    _sendRequest();
}

void Squeezebox::sendRequest(string command)
{
    SqueezeboxCommand cmd;
    cmd.request = command;
    cmd.noCallback = true;

    squeeze_commands.push(cmd);

    _sendRequest();
}

Eina_Bool _execute_request_idler_cb(void *data)
{
    Squeezebox *sq = reinterpret_cast<Squeezebox *>(data);
    if (!sq) return ECORE_CALLBACK_CANCEL;

    sq->processMessage(false, "");

    //delete the ecore_idler
    return ECORE_CALLBACK_CANCEL;
}

void Squeezebox::_sendRequest()
{
    //return if a request is already in progress
    if (!squeeze_commands.empty() && squeeze_commands.front().inProgress)
        return;

    //return if there is no more command to process
    if (squeeze_commands.empty())
        return;

    //Do the real work here, get the next available request and process it
    SqueezeboxCommand &cmd = squeeze_commands.front();

    cDebugDom("squeezebox") <<  "sending command: \"" << cmd.request << "\"";

    cmd.inProgress = true;

    if (!timer_timeout)
        timer_timeout = new EcoreTimer(SQ_TIMEOUT, (sigc::slot<void>)sigc::mem_fun(*this, &Squeezebox::requestTimeout_cb));

    if (isConnected)
    {
        cmd.request += "\n\r";
        ecore_con_server_send(econ, cmd.request.c_str(), cmd.request.length());
    }
    else
    {
        ecore_idler_add(_execute_request_idler_cb, this);
    }
}

void Squeezebox::requestTimeout_cb()
{
    cDebugDom("squeezebox") <<  "Request: Timeout ! ";

    processMessage(false, "");
}

void Squeezebox::Play()
{
    string cmd = id;
    cmd += " play";

    sendRequest(cmd);
}

void Squeezebox::Pause()
{
    string cmd = id;
    cmd += " pause";

    sendRequest(cmd);
}

void Squeezebox::Stop()
{
    string cmd = id;
    cmd += " stop";

    sendRequest(cmd);
}

void Squeezebox::Next()
{
    string cmd = id;
    cmd += " playlist index +1";

    sendRequest(cmd);
}

void Squeezebox::Previous()
{
    string cmd = id;
    cmd += " playlist index -1";

    sendRequest(cmd);
}

void Squeezebox::Power(bool on)
{
    string cmd = id;

    if (on)
        cmd += " power 1";
    else
        cmd += " power 0";

    sendRequest(cmd);
}

void Squeezebox::Sleep(int seconds)
{
    string cmd = id;
    cmd += " sleep " + Utils::to_string(seconds);

    sendRequest(cmd);
}

void Squeezebox::Synchronize(string playerid, bool sync)
{
    string cmd = id;
    if (sync)
        cmd += " sync " + playerid;
    else
        cmd += " sync -";

    sendRequest(cmd);
}

void Squeezebox::get_songinfo(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " path ?";

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_songinfo_cb), data);
}

void Squeezebox::get_songinfo_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    string path = p["2"];

    string cmd = "songinfo 0 100 tags:algjdro url:";
    cmd += path;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_songinfo_cb2), data);
}

void Squeezebox::get_songinfo_cb2(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    Params infos;

    //It's probably a remote stream, get infos with specific commands
    if (p.size() <= 5)
    {
        get_artist(sigc::mem_fun(*this, &Squeezebox::get_songinfo_artist_cb), data);

        return;
    }

    for (int i = 5;i < p.size();i++)
    {
        string value = p[Utils::to_string(i)];
        vector<string> attr;
        Utils::split(Utils::url_decode2(value), attr, ":", 2);

        if (attr.size() == 2)
            infos.Add(Utils::url_decode2(attr[0]), Utils::url_decode2(attr[1]));
    }
    data.get_chain_data().params = infos;

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_songinfo_artist_cb(AudioPlayerData data)
{
    data.get_chain_data().params.Add("artist", data.svalue);
    get_album(sigc::mem_fun(*this, &Squeezebox::get_songinfo_album_cb), data);
}
void Squeezebox::get_songinfo_album_cb(AudioPlayerData data)
{
    data.get_chain_data().params.Add("album", data.svalue);
    get_title(sigc::mem_fun(*this, &Squeezebox::get_songinfo_title_cb), data);
}
void Squeezebox::get_songinfo_title_cb(AudioPlayerData data)
{
    data.get_chain_data().params.Add("title", data.svalue);
    get_duration(sigc::mem_fun(*this, &Squeezebox::get_songinfo_duration_cb), data);
}
void Squeezebox::get_songinfo_duration_cb(AudioPlayerData data)
{
    data.get_chain_data().params.Add("duration", Utils::to_string(data.dvalue));

    //force getting album cover for remote stream (radio, music services)
    data.get_chain_data().params.Add("coverart", "1");

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}


void Squeezebox::get_title(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " remote ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_title_cb), data);
}
void Squeezebox::get_title_cb(bool status, string request, string result, AudioPlayerData data)
{
    string cmd;
    Params p;
    p.Parse(result);

    if (p["2"] == "1")
        cmd = id + " title ?";
    else
        cmd = id + " current_title ?";

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_title2_cb), data);
}
void Squeezebox::get_title2_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    data.get_chain_data().svalue = url_decode2(p["2"]);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}


void Squeezebox::get_artist(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " remote ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_title_cb), data);
}
void Squeezebox::get_artist_cb(bool status, string request, string result, AudioPlayerData data)
{
    string cmd;
    Params p;
    p.Parse(result);

    if (p["2"] == "1")
        cmd = id + " artist ?";
    else
        cmd = id + " title ?";

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_title2_cb), data);
}
void Squeezebox::get_artist2_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    data.get_chain_data().svalue = url_decode2(p["2"]);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}


void Squeezebox::get_album(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " album ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_album_cb), data);
}
void Squeezebox::get_album_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    data.get_chain_data().svalue = url_decode2(p["2"]);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}


void Squeezebox::get_album_cover(AudioRequest_cb callback, AudioPlayerData user_data)
{
    //Try using a JSON call to get artwork_url if any

    string url = "http://";
    url += param["host"] + ":9000";
    url += "/jsonrpc.js";

    string postData = "{\"id\":1,\"method\":\"slim.request\",\"params\":[\"";
    postData += id;
    postData += "\",[\"status\",\"-\",1,\"tags:gABbehldiqtyrSuoKLN\"]]}";

    AudioPlayerData *data = new AudioPlayerData();
    data->set_chain_data(new AudioPlayerData(user_data));
    data->callback = callback;

    FileDownloader *downloader = new FileDownloader(url, postData, "application/json", true);
    downloader->addCallback(sigc::mem_fun(*this, &Squeezebox::get_album_cover_json_cb), data);

    downloader->Start();
}
void Squeezebox::get_album_cover_json_cb(string result, void *data, void *user_data)
{
    if (result == "failed" || result == "aborted")
    {
        AudioPlayerData *_data = reinterpret_cast<AudioPlayerData *>(user_data);
        AudioPlayerData adata(*_data);
        delete _data;

        get_album_cover_std(adata);
    }
    else if (result == "done")
    {
        AudioPlayerData *_data = reinterpret_cast<AudioPlayerData *>(user_data);
        AudioPlayerData adata(*_data);
        delete _data;

        Buffer_CURL *buff = reinterpret_cast<Buffer_CURL *>(data);
        string res((const char *)buff->buffer, buff->bufsize);

        json_error_t jerr;
        json_t *json = json_loads(res.c_str(), 0, &jerr);

        cDebug() << json_dumps(json, JSON_INDENT(4));

        if (!json)
        {
            cDebugDom("squeezebox") <<  "JSON - Error loading json : " << jerr.text;

            get_album_cover_std(adata);

            return;
        }

        json_t *remoteMeta = NULL, *artwork_url = NULL, *jresult = NULL;

        if (json_is_object(json))
        {
            jresult = json_object_get(json, "result");

            if (json_is_object(jresult))
            {
                remoteMeta = json_object_get(jresult, "remoteMeta");

                if (json_is_object(remoteMeta))
                {
                    artwork_url = json_object_get(remoteMeta, "artwork_url");
                    if (json_is_string(artwork_url))
                    {
                        string aurl;

                        aurl = json_string_value(artwork_url);

                        if (artwork_url) json_decref(artwork_url);
                        if (remoteMeta) json_decref(remoteMeta);
                        if (jresult) json_decref(jresult);
                        if (json) json_decref(json);

                        if (aurl.compare(0, 4, "http") == 0)
                        {
                            adata.get_chain_data().svalue = aurl;

                            AudioRequest_signal sig;
                            sig.connect(adata.callback);
                            sig.emit(adata.get_chain_data());

                            return;
                        }
                        else
                        {
                            string s = "http://";
                            s += host + ":9000/";
                            s += aurl;

                            adata.get_chain_data().svalue = s;

                            AudioRequest_signal sig;
                            sig.connect(adata.callback);
                            sig.emit(adata.get_chain_data());

                            return;
                        }
                    }

                    cDebugDom("squeezebox") <<  "JSON - artwork_url not found in remoteMeta!";
                }
                else
                {
                    cDebugDom("squeezebox") <<  "JSON - remoteMeta not found!";
                }
            }
        }

        if (remoteMeta) json_decref(remoteMeta);
        if (artwork_url) json_decref(artwork_url);
        if (json) json_decref(json);

        get_album_cover_std(adata);
    }
}
void Squeezebox::get_album_cover_std(AudioPlayerData data)
{
    cDebugDom("squeezebox") <<  "trying with standard CLI way...";

    string cmd = id;
    cmd += " path ?";

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_album_cover_std_cb), data);
}
void Squeezebox::get_album_cover_std_cb(bool status, string request, string result, AudioPlayerData data)
{
    string cmd;
    Params p;
    p.Parse(result);
    string path = p["2"];

    cmd = "songinfo 0 100 url:";
    cmd += path;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_album_cover_std2_cb), data);
}
void Squeezebox::get_album_cover_std2_cb(bool status, string request, string result, AudioPlayerData data)
{
    vector<string> tokens;
    split(result, tokens);
    for_each(tokens.begin(), tokens.end(), UrlDecode());

    string aid = "";
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;
        split(tmp, tk, ":", 2);
        if (tk.size() != 2) continue;

        if (tk[0] == "artwork_track_id") aid = tk[1];
        if (tk[0] == "id" && aid == "") aid = tk[1];
    }

    stringstream aurl;
    if (aid == "") aid = "current";
    aurl << "http://" << host << ":" << port_web << "/music/" << aid << "/cover.jpg";
    if (aid == "") aurl << "?playerid=" << id;

    data.get_chain_data().svalue = aurl.str();

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_album_cover_id(string track_id, AudioRequest_cb callback, AudioPlayerData user_data)
{
    stringstream aurl;
    if (track_id == "") track_id = "0";
    aurl << "http://" << host << ":" << port_web << "/music/" << track_id << "/cover.jpg";

    cDebugDom("squeezebox") <<  "\"" << aurl.str() << "\"";

    user_data.svalue = aurl.str();

    AudioRequest_signal sig;
    sig.connect(callback);
    sig.emit(user_data);
}


void Squeezebox::get_playlist_album_cover(int item, AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " playlist path " + Utils::to_string(item) + " ?";

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_album_cover_cb), data);
}
void Squeezebox::get_playlist_album_cover_cb(bool status, string request, string result, AudioPlayerData data)
{
    string cmd;
    Params p;
    p.Parse(result);
    string path = p["4"];

    cmd = "songinfo 0 100 url:";
    cmd += path;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_album_cover2_cb), data);
}
void Squeezebox::get_playlist_album_cover2_cb(bool status, string request, string result, AudioPlayerData data)
{
    vector<string> tokens;
    split(result, tokens);
    for_each(tokens.begin(), tokens.end(), UrlDecode());

    string aid = "";
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;
        split(tmp, tk, ":", 2);
        if (tk.size() != 2) continue;

        if (tk[0] == "artwork_track_id") aid = tk[1];
        if (tk[0] == "id" && aid == "") aid = tk[1];
    }

    stringstream aurl;
    if (aid == "") aid = "0";
    aurl << "http://" << host << ":" << port_web << "/music/" << aid << "/cover.jpg";

    cDebugDom("squeezebox") <<  "\"" << aurl.str() << "\"";

    data.get_chain_data().svalue = aurl.str();

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_genre(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " genre ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_genre_cb), data);
}
void Squeezebox::get_genre_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    data.get_chain_data().svalue = url_decode2(p["2"]);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_current_time(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " time ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_current_time_cb), data);
}

void Squeezebox::get_current_time_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    if (is_of_type<double>(url_decode2(p["2"])))
        from_string(url_decode2(p["2"]), data.get_chain_data().dvalue);
    else
        data.get_chain_data().dvalue = 0.0;

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::set_current_time(double seconds)
{
    string cmd = id;
    cmd += " time " + Utils::to_string(seconds);

    sendRequest(cmd);
}

void Squeezebox::get_duration(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " duration ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;
    data.dvalue = 0.0;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_duration_cb), data);
}

void Squeezebox::get_duration_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    if (is_of_type<double>(url_decode2(p["2"])))
        from_string(url_decode2(p["2"]), data.get_chain_data().dvalue);
    else
        data.get_chain_data().dvalue = 0.0;

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_sleep(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " sleep ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_sleep_cb), data);
}

void Squeezebox::get_sleep_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    if (is_of_type<int>(url_decode2(p["2"])))
        from_string(url_decode2(p["2"]), data.get_chain_data().ivalue);
    else
        data.get_chain_data().ivalue = 0;

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_status(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " mode ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_status_cb), data);
}

void Squeezebox::get_status_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    data.get_chain_data().svalue = url_decode2(p["2"]);

    if (data.get_chain_data().svalue == "play")
        data.get_chain_data().ivalue = AudioPlay;
    else if (data.get_chain_data().svalue == "pause")
        data.get_chain_data().ivalue = AudioPause;
    else if (data.get_chain_data().svalue == "stop")
        data.get_chain_data().ivalue = AudioStop;
    else
        data.get_chain_data().ivalue = AudioStop;

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_sync_status(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " sync ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_sync_status_cb), data);
}

void Squeezebox::get_sync_status_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    vector<Params> &results = data.get_chain_data().vparams;

    vector<string> splitter;
    Utils::split(url_decode2(p["2"]), splitter, ",");

    for (uint j = 0;j < splitter.size();j++)
    {
        string tmp = splitter[j];

        if (tmp == "-") break;

        for (int i = 0;i < AudioManager::Instance().get_size();i++)
        {
            AudioPlayer *ap = AudioManager::Instance().get_player(i);
            if (ap->get_param("id") == tmp)
            {
                Params res;

                //player found in calaosd, add it
                res.Add("id", ap->get_param("id"));
                res.Add("name", ap->get_param("name"));

                results.push_back(res);

                break;
            }
        }
    }

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_playlist_size(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " playlist tracks ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_size_cb), data);
}

void Squeezebox::get_playlist_size_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    if (is_of_type<int>(url_decode2(p["3"])))
        from_string(url_decode2(p["3"]), data.get_chain_data().ivalue);
    else
        data.get_chain_data().ivalue = 0;

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_playlist_current(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " playlist index ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_current_cb), data);
}

void Squeezebox::get_playlist_current_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    if (is_of_type<int>(url_decode2(p["3"])))
        from_string(url_decode2(p["3"]), data.get_chain_data().ivalue);
    else
        data.get_chain_data().ivalue = 0;

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_playlist_item(int index, AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " playlist path " + Utils::to_string(index) + " ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    data.ivalue = index; //store index for later use

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_item_cb), data);
}

void Squeezebox::get_playlist_item_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    string path = url_decode2(p["4"]);

    string cmd = "songinfo 0 100 tags:algjdro url:" + url_encode(path);

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_item2_cb), data);
}
void Squeezebox::get_playlist_item2_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    //It's probably a remote stream, get infos with specific commands
    if (p.size() <= 5)
    {
        string cmd = id;
        cmd += " playlist title " + Utils::to_string(data.ivalue) + " ?";

        sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_item3_cb), data);

        return;
    }

    for (int i = 5;i < p.size();i++)
    {
        string value = p[Utils::to_string(i)];
        vector<string> attr;
        split(url_decode2(value), attr, ":", 2);

        if (attr.size() == 2)
            data.get_chain_data().params.Add(url_decode2(attr[0]), url_decode2(attr[1]));
    }

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}
void Squeezebox::get_playlist_item3_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    data.get_chain_data().params.Add("artist", url_decode2(p["4"]));

    data.ivalue2 = data.ivalue; //hack here, ivalue will be erased by get_playlist_current call
    get_playlist_current(sigc::mem_fun(*this, &Squeezebox::get_playlist_item4_cb), data);
}
void Squeezebox::get_playlist_item4_cb(AudioPlayerData data)
{
    int index = (int)data.ivalue2;

    if (index == data.ivalue)
    {
        string cmd = id;
        cmd += " current_title ?";

        sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_item5_cb), data);
    }
    else
    {
        AudioRequest_signal sig;
        sig.connect(data.callback);
        sig.emit(data.get_chain_data());
    }
}
void Squeezebox::get_playlist_item5_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    data.get_chain_data().params.Add("title", url_decode2(p["2"]));

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::get_playlist_basic_info(int index, AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " playlist artist " + Utils::to_string(index) + " ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    data.ivalue = index; //store index for later use

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_info_cb), data);
}

void Squeezebox::get_playlist_info_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    string key = url_decode2(p["2"]);
    string val = url_decode2(p["4"]);

    data.get_chain_data().params.Add(key, val);

    if (key == "artist")
    {
        string cmd = id;
        cmd += " playlist album " + Utils::to_string(data.ivalue) + " ?";
        sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_info_cb), data);
    }
    else if (key == "album")
    {
        string cmd = id;
        cmd += " playlist title " + Utils::to_string(data.ivalue) + " ?";
        sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_playlist_info_cb), data);
    }
    else
    {
        AudioRequest_signal sig;
        sig.connect(data.callback);
        sig.emit(data.get_chain_data());
    }
}

void Squeezebox::playlist_moveup(int item)
{
    stringstream cmd;
    cmd << id << " playlist move " << item << " " << (item - 1);

    sendRequest(cmd.str());
}

void Squeezebox::playlist_movedown(int item)
{
    stringstream cmd;
    cmd << id << " playlist move " << item << " " << (item + 1);

    sendRequest(cmd.str());
}

void Squeezebox::playlist_delete(int item)
{
    stringstream cmd;
    cmd << id << " playlist delete " << item;

    sendRequest(cmd.str());
}

void Squeezebox::playlist_play(int item)
{
    stringstream cmd;
    cmd << id << " playlist index " << item;

    sendRequest(cmd.str());
}

void Squeezebox::playlist_clear()
{
    string cmd = id;
    cmd += " playlist clear";

    sendRequest(cmd);
}

void Squeezebox::playlist_save(string name)
{
    string cmd = id;
    cmd += " playlist save " + Utils::url_encode(name);

    sendRequest(cmd);
}

void Squeezebox::playlist_delete(string pid)
{
    string cmd = "playlists delete playlist_id:" + pid;

    sendRequest(cmd);
}

void Squeezebox::playlist_play_artist(string item)
{
    string cmd = id;
    cmd += " playlistcontrol cmd:load artist_id:";
    cmd += item;

    sendRequest(cmd);
}

void Squeezebox::playlist_play_album(string item)
{
    string cmd = id;
    cmd += " playlistcontrol cmd:load album_id:";
    cmd += item;

    sendRequest(cmd);
}

void Squeezebox::playlist_play_title(string item)
{
    string cmd = id;
    cmd += " playlistcontrol cmd:load track_id:";
    cmd += item;

    sendRequest(cmd);
}

void Squeezebox::playlist_add_artist(string item)
{
    string cmd = id;
    cmd += " playlistcontrol cmd:add artist_id:";
    cmd += item;

    sendRequest(cmd);
}

void Squeezebox::playlist_add_album(string item)
{
    string cmd = id;
    cmd += " playlistcontrol cmd:add album_id:";
    cmd += item;

    sendRequest(cmd);
}

void Squeezebox::playlist_add_title(string item)
{
    string cmd = id;
    cmd += " playlistcontrol cmd:add track_id:";
    cmd += item;

    sendRequest(cmd);
}

void Squeezebox::playlist_add_items(string item)
{
    string cmd;

    vector<string> tokens;
    Utils::split(item, tokens, ":", 2);
    if (tokens.size() != 2) return;

    if (tokens[0] == "track_id" ||
        tokens[0] == "album_id" ||
        tokens[0] == "artist_id" ||
        tokens[0] == "genre_id" ||
        tokens[0] == "year" ||
        tokens[0] == "playlist_id" ||
        tokens[0] == "folder_id")
    {
        cmd = id;
        cmd += " playlistcontrol cmd:add ";
        cmd += item;
    }
    else if (tokens[0] == "radio_id")
    {
        vector<string> tok;
        Utils::split(tokens[1], tok, ":", 2);
        if (tok.size() != 2) return;

        cmd = id;
        cmd += " " + tok[1] + " "; //radio type
        cmd += " playlist add item_id:";
        cmd += tok[0]; //radio id
    }
    else //add as direct song url
    {
        cmd = id;
        cmd += " playlist add ";
        cmd += url_encode(item);
    }

    sendRequest(cmd);
}

void Squeezebox::playlist_play_items(string item)
{
    string cmd;

    vector<string> tokens;
    Utils::split(item, tokens, ":", 2);
    if (tokens.size() != 2) return;

    if (tokens[0] == "track_id" ||
        tokens[0] == "album_id" ||
        tokens[0] == "artist_id" ||
        tokens[0] == "genre_id" ||
        tokens[0] == "year" ||
        tokens[0] == "playlist_id" ||
        tokens[0] == "folder_id" ||
        tokens[0] == "playlist_name")
    {
        cmd = id;
        cmd += " playlistcontrol cmd:load ";
        cmd += url_encode(item);
    }
    else if (tokens[0] == "radio_id")
    {
        vector<string> tok;
        Utils::split(tokens[1], tok, ":", 2);
        if (tok.size() != 2) return;

        cmd = id;
        cmd += " " + tok[1] + " "; //radio type
        cmd += " playlist play item_id:";
        cmd += tok[0]; //radio id
    }
    else //play as direct song url
    {
        cmd = id;
        cmd += " playlist play ";
        cmd += url_encode(item);
    }

    sendRequest(cmd);
}

void Squeezebox::get_volume(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = id;
    cmd += " mixer volume ?";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_volume_cb), data);
}

void Squeezebox::get_volume_cb(bool status, string request, string result, AudioPlayerData data)
{
    Params p;
    p.Parse(result);

    if (is_of_type<int>(url_decode2(p["3"])))
        from_string(url_decode2(p["3"]), data.get_chain_data().ivalue);
    else
        data.get_chain_data().ivalue = 0;

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void Squeezebox::set_volume(int vol)
{
    string cmd = id;
    cmd += " mixer volume ";
    cmd += Utils::to_string(vol);

    sendRequest(cmd);
}

void Squeezebox::getSynchronizeList(AudioRequest_cb callback, AudioPlayerData user_data)
{
    string cmd = "players 0 20";

    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    sendRequest(cmd, sigc::mem_fun(*this, &Squeezebox::get_sync_list_cb), data);
}

void Squeezebox::get_sync_list_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;

    vector<string> tokens;
    split(res, tokens);

    if (tokens.size() > 0)
    {
        for_each(tokens.begin(), tokens.end(), UrlDecode());

        Params item;
        for (uint i = 0;i < tokens.size();i++)
        {
            string tmp = tokens[i];
            vector<string> tk;

            split(tmp, tk, ":", 2);

            if (tk.size() != 2) continue;

            if (tk[0] == "playerid")
            {
                string pid = url_decode2(tk[1]);

                for (int j = 0;j < AudioManager::Instance().get_size();j++)
                {
                    AudioPlayer *ap = AudioManager::Instance().get_player(j);
                    if (ap->get_param("id") == pid && pid != id)
                    {
                        //player found in calaosd, add it
                        item.Add("id", pid);
                        item.Add("name", ap->get_param("name"));
                        result.push_back(item);

                        break;
                    }
                }
            }
        }
    }

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}
