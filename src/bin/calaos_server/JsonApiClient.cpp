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
#include "JsonApiClient.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "PollListenner.h"
#include "TCPConnection.h"
#include "hef_uri_syntax.h"
#include "Prefix.h"
#include "CalaosConfig.h"
#include <Ecore.h>

using namespace Calaos;

#ifndef json_array_foreach
#define json_array_foreach(array, index, value) \
    for(index = 0; \
    index < json_array_size(array) && (value = json_array_get(array, index)); \
    index++)
#endif

#define HTTP_400 "HTTP/1.0 400 Bad Request"
#define HTTP_404 "HTTP/1.0 404 Not Found"
#define HTTP_500 "HTTP/1.0 500 Internal Server Error"
#define HTTP_200 "HTTP/1.0 200 OK"

#define HTTP_400_BODY "<html><head>" \
    "<title>400 Bad Request</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Bad Request</h1>" \
    "<p>The server received a request it could not understand.</p>" \
    "</body>" \
    "</html>"

#define HTTP_404_BODY "<html><head>" \
    "<title>404 Not Found</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Page not found</h1>" \
    "<p>Document or file requested by the client was not found.</p>" \
    "</body>" \
    "</html>"

#define HTTP_500_BODY "<html><head>" \
    "<title>500 Internal Server Error</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Internal Server Error</h1>" \
    "<p>The server encountered an unexpected condition which prevented it from fulfilling the request.</p>" \
    "</body>" \
    "</html>"

int _parser_begin(http_parser *parser)
{
    JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);

    //reset status flags to parse another request on the same connection
    client->parse_done = false;
    client->has_field = false;
    client->has_value = false;
    client->hfield.clear();
    client->hvalue.clear();
    client->bodymessage.clear();
    client->parse_url.clear();

    return 0;
}

int _parser_header_field(http_parser *parser, const char *at, size_t length)
{
    JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);

    if (client->has_field && client->has_value)
    {
        client->request_headers[client->hfield] = client->hvalue;
        client->has_field = false;
        client->has_value = false;
        client->hfield.clear();
        client->hvalue.clear();
    }

    if (!client->has_field)
        client->has_field = true;

    client->hfield.append(at, length);

    return 0;
}

int _parser_header_value(http_parser *parser, const char *at, size_t length)
{
    JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);

    if (!client->has_value)
        client->has_value = true;

    client->hvalue.append(at, length);

    return 0;
}

int _parser_headers_complete(http_parser *parser)
{
    JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);

    if (client->has_field && client->has_value)
    {
        client->request_headers[client->hfield] = client->hvalue;
        client->has_field = false;
        client->has_value = false;
        client->hfield.clear();
        client->hvalue.clear();
    }

    return 0;
}

int _parser_url(http_parser *parser, const char *at, size_t length)
{
    JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);

    client->parse_url.append(at, length);

    return 0;
}

int _parser_message_complete(http_parser *parser)
{
    JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);

    client->parse_done = true;
    client->request_method = parser->method;

    return 0;
}

int _parser_body_complete(http_parser* parser, const char *at, size_t length)
{
    JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);

    client->bodymessage.append(at, length);

    return 0;
}

Eina_Bool _ecore_exe_finished(void *data, int type, void *event)
{
    JsonApiClient *client = reinterpret_cast<JsonApiClient *>(data);
    Ecore_Exe_Event_Del *ev = reinterpret_cast<Ecore_Exe_Event_Del *>(event);

    if (ev->exe != client->exe_thumb)
        return ECORE_CALLBACK_PASS_ON;

    client->exeFinished(ev->exe, ev->exit_code);

    return ECORE_CALLBACK_CANCEL;
}

JsonApiClient::JsonApiClient(Ecore_Con_Client *cl):
    client_conn(cl)
{
    //set up callbacks for the parser
    parser_settings.on_message_begin = _parser_begin;
    parser_settings.on_url = _parser_url;
    parser_settings.on_header_field = _parser_header_field;
    parser_settings.on_header_value = _parser_header_value;
    parser_settings.on_headers_complete = _parser_headers_complete;
    parser_settings.on_body = _parser_body_complete;
    parser_settings.on_message_complete = _parser_message_complete;

    parser = (http_parser *)calloc(1, sizeof(http_parser));
    http_parser_init(parser, HTTP_REQUEST);
    parser->data = this;

    cDebugDom("network") << this;

    exe_handler = ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _ecore_exe_finished, this);

    int cpt = rand();
    do
    {
        tempfname = "/tmp/calaos_json_temp_" + Utils::to_string(cpt) + ".jpg";
        cpt++;
    }
    while (ecore_file_exists(tempfname.c_str()));
}

JsonApiClient::~JsonApiClient()
{
    ecore_event_handler_del(exe_handler);
    free(parser);
    CloseConnection();
    ecore_file_unlink(tempfname.c_str());

    cDebugDom("network") << this;
}

void JsonApiClient::ProcessData(string request)
{
    size_t nparsed;

    nparsed = http_parser_execute(parser, &parser_settings, request.c_str(), request.size());

    if (parser->upgrade)
    {
        /* handle new protocol */
        cDebugDom("network") << "Protocol Upgrade not supported, closing connection.";
        CloseConnection();

        return;
    }
    else if (nparsed != request.size())
    {
        /* Handle error. Usually just close the connection. */
        CloseConnection();

        return;
    }

    if (parse_done)
    {
        //Finally parsing of request is done, we can search for
        //a response for the requested path


        cDebugDom("network") << "Client headers: HTTP/" << Utils::to_string(parser->http_major) << "." << Utils::to_string(parser->http_minor);
        for (auto it = request_headers.begin();it!= request_headers.end();++it)
            cDebugDom("network") << it->first << ": " << it->second;

        //init parser again
        http_parser_init(parser, HTTP_REQUEST);
        parser->data = this;

        hef::HfURISyntax req_url("http://0.0.0.0" + parse_url);

        if (req_url.getPath() == "/" ||
            req_url.getPath() == "/index.html" ||
            req_url.getPath() == "/debug.html")
        {
            Params headers;
            headers.Add("Connection", "Close");
            headers.Add("Content-Type", "text/html");
            string fileName = Prefix::Instance().dataDirectoryGet() + "/debug.html";
            cDebug() << "send file " << fileName << "to Client";
            string res = buildHttpResponseFromFile(HTTP_200, headers, fileName);
            sendToClient(res);

            return;
        }

        if (req_url.getPath() != "/api" &&
            req_url.getPath() != "/api.php" &&
            req_url.getPath() != "/api/v1" /*&&
                        req_url.getPath() != "/api/v1.5" &&
                        req_url.getPath() != "/api/v2"*/)
        {
            Params headers;
            headers.Add("Connection", "close");
            headers.Add("Content-Type", "text/html");
            string res = buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
            sendToClient(res);

            return;
        }


        //get protocol version, if nothing is set default to v1
        proto_ver = APIV1;
        if (req_url.getPath() == "/api/v1.5") proto_ver = APIV1_5;
        if (req_url.getPath() == "/api/v2") proto_ver = APIV2;

        //TODO: get url parameters here?
        //for example get username/password as a url parameter

        //Handle CORS here
        if (jsonParam.Exists("Origin"))
        {
            resHeaders.Add("Access-Control-Allow-Origin", jsonParam["Origin"]);
            resHeaders.Add("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
        }

        if (request_method == HTTP_OPTIONS)
        {
            if (jsonParam.Exists("Access-Control-Request-Method"))
                resHeaders.Add("Access-Control-Allow-Methods", "GET, POST, OPTIONS");

            if (jsonParam.Exists("Access-Control-Request-Headers"))
                resHeaders.Add("Access-Control-Allow-Headers", "{" + jsonParam["Access-Control-Request-Headers"] + "}");

            Params headers;
            headers.Add("Connection", "Close");
            headers.Add("Cache-Control", "no-cache, must-revalidate");
            headers.Add("Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
            headers.Add("Content-Type", "text/html");
            string res = buildHttpResponse(HTTP_200, headers, "");
            sendToClient(res);

            return;
        }

        //handle request now
        handleRequest();
    }
}

void JsonApiClient::DataWritten(int size)
{
    data_size -= size;

    cDebugDom("network") << size << " bytes has been written, " << data_size << " bytes remaining";

    if (data_size <= 0 && need_restart)
    {
        cDebugDom("network")
                << "All config files written, restarting calaos_server";
        ecore_app_restart();
    }

    if (conn_close && data_size <= 0)
    {
        cDebugDom("network")
                << "All data sent, close connection";
        //force all remaining data to be written before closing
        ecore_con_client_flush(client_conn);

        //Close connection in 500ms if not closed by client. This forces the closing and
        //has to be done because lighttpd mod_proxy keeps connection open regardless of the Connection: close header
        if (!closeTimer)
            closeTimer = new EcoreTimer(0.5, sigc::mem_fun(this, &JsonApiClient::CloseConnection));
        else
            closeTimer->Reset(0.5);
    }
}

void JsonApiClient::CloseConnection()
{
    DELETE_NULL(closeTimer);

    cDebugDom("network") << "Closing connection";
    DELETE_NULL_FUNC(ecore_con_client_del, client_conn);
}

string JsonApiClient::buildHttpResponseFromFile(string code, Params &headers, string fileName)
{
    ifstream file(fileName);
    string body((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());

    return buildHttpResponse(code, headers, body);
}

string JsonApiClient::buildHttpResponse(string code, Params &headers, string body)
{
    stringstream res;

    //HTTP code
    res << code << "\r\n";

    if (!headers.Exists("Content-Length"))
        headers.Add("Content-Length", Utils::to_string(body.length()));

    if (request_headers["Connection"] == "close" ||
        request_headers["Connection"] == "Close")
    {
        headers.Add("Connection", "Close");
        cDebugDom("network")
                << "Client requested Connection: Close";
    }

    if (headers["Connection"] == "Close" || headers["Connection"] == "close")
        conn_close = true;
    else
        conn_close = false;

    //headers
    for (int i = 0;i < headers.size();i++)
    {
        string key, value;
        headers.get_item(i, key, value);
        res << key << ": " << value << "\r\n";
    }

    res << "\r\n";

    //body
    res << body;

    return res.str();
}

void JsonApiClient::sendToClient(string res)
{
    data_size += res.length();

    cDebugDom("network") << res;
    cDebugDom("network") << "Sending " << res.length() << " bytes, data_size = " << data_size;

    if (!client_conn || ecore_con_client_send(client_conn, res.c_str(), res.length()) == 0)
    {
        cCriticalDom("network")
                << "Error sending data ! Closing connection.";

        CloseConnection();
    }
}

void JsonApiClient::handleRequest()
{
    jsonParam.clear();

    //parse the json data
    json_error_t jerr;
    json_t *jroot = json_loads(bodymessage.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cDebugDom("network") << "Error loading json : " << jerr.text;

        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
        sendToClient(res);
        CloseConnection();

        return;
    }

    cDebug() << json_dumps(jroot, JSON_INDENT(4));

    const char *key;
    json_t *value;

    //decode the json root object into jsonParam
    json_object_foreach(jroot, key, value)
    {
        string svalue;

        if (json_is_string(value))
            svalue = json_string_value(value);
        else if (json_is_boolean(value))
            svalue = json_is_true(value)?"true":"false";
        else if (json_is_number(value))
            svalue = Utils::to_string(json_number_value(value));

        jsonParam.Add(key, svalue);
    }

    //check for if username/password matches
    string user = Utils::get_config_option("calaos_user");
    string pass = Utils::get_config_option("calaos_password");

    if (Utils::get_config_option("cn_user") != "" &&
        Utils::get_config_option("cn_pass") != "")
    {
        user = Utils::get_config_option("cn_user");
        pass = Utils::get_config_option("cn_pass");
    }

    if (user != jsonParam["cn_user"] || pass != jsonParam["cn_pass"])
    {
        cDebugDom("network") << "Login failed!";

        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
        sendToClient(res);
        CloseConnection();

        return;
    }

    //check action now
    if (jsonParam["action"] == "get_home")
        processGetHome();
    else if (jsonParam["action"] == "get_state")
        processGetState(jroot);
    else if (jsonParam["action"] == "set_state")
        processSetState();
    else if (jsonParam["action"] == "get_playlist")
        processGetPlaylist();
    else if (jsonParam["action"] == "poll_listen")
        processPolling();
    else if (jsonParam["action"] == "get_cover")
        processGetCover();
    else if (jsonParam["action"] == "get_camera_pic")
        processGetCameraPic();
    else if (jsonParam["action"] == "config")
        processConfig(jroot);

    json_decref(jroot);
}

template<typename T>
json_t *JsonApiClient::buildJsonRoomIO(Room *room)
{
    json_t *jdata = json_array();

    for (int i = 0;i < room->get_size<T *>();i++)
    {
        json_t *jinput = json_object();
        T *io = room->get_io<T *>(i);

        vector<string> params =
        { "id", "name", "type", "hits", "var_type", "visible",
          "chauffage_id", "rw", "unit", "gui_type", "state",
          "auto_scenario", "step" };

        for (string &param: params)
        {
            string value;

            if (param == "state")
            {
                if (io->get_type() == TINT)
                    value = Utils::to_string(io->get_value_double());
                else if (io->get_type() == TBOOL)
                    value = io->get_value_bool()?"true":"false";
                else if (io->get_type() == TSTRING)
                    value = io->get_value_string();
            }
            else if (param == "var_type")
            {
                if (io->get_type() == TINT) value = "float";
                else if (io->get_type() == TBOOL) value = "bool";
                else if (io->get_type() == TSTRING) value = "string";
            }
            else
            {
                if (!io->get_params().Exists(param))
                    continue;
                value = io->get_param(param);
            }

            json_object_set_new(jinput, param.c_str(),
                                json_string(value.c_str()));
        }

        json_array_append_new(jdata, jinput);
    }

    return jdata;
}

json_t *JsonApiClient::buildJsonHome()
{
    json_t *jdata = json_array();

    for (int iroom = 0;iroom < ListeRoom::Instance().size();iroom++)
    {
        Room *room = ListeRoom::Instance().get_room(iroom);
        json_t *jroom = json_object();

        json_t *jitems = json_pack("{s:o, s:o}",
                                   "inputs", buildJsonRoomIO<Input>(room),
                                   "outputs", buildJsonRoomIO<Output>(room));

        json_object_set_new(jroom, "type", json_string(room->get_type().c_str()));
        json_object_set_new(jroom, "name", json_string(room->get_name().c_str()));
        json_object_set_new(jroom, "hits", json_string(Utils::to_string(room->get_hits()).c_str()));
        json_object_set_new(jroom, "items", jitems);

        json_array_append_new(jdata, jroom);
    }

    return jdata;
}

json_t *JsonApiClient::buildJsonCameras()
{
    json_t *jdata = json_array();

    int cpt = 0;
    for (int i = 0;i < ListeRoom::Instance().get_nb_input();i++)
    {
        Input *in = ListeRoom::Instance().get_input(i);
        CamInput *ipcam = dynamic_cast<CamInput *>(in);
        if (ipcam)
        {
            IPCam *camera = ipcam->get_cam();

            json_t *jcam = json_object();
            json_object_set_new(jcam, "id", json_string(Utils::to_string(cpt).c_str()));
            json_object_set_new(jcam, "input_id", json_string(camera->get_param("iid").c_str()));
            json_object_set_new(jcam, "output_id", json_string(camera->get_param("oid").c_str()));
            json_object_set_new(jcam, "name", json_string(camera->get_param("name").c_str()));
            json_object_set_new(jcam, "type", json_string(camera->get_param("type").c_str()));
            json_object_set_new(jcam, "url_jpeg", json_string(camera->get_picture().c_str()));
            json_object_set_new(jcam, "url_mjpeg", json_string(camera->get_mjpeg_stream().c_str()));
            Params caps = camera->getCapabilities();
            if (caps["ptz"] == "true")
                json_object_set_new(jcam, "ptz", json_string("true"));
            else
                json_object_set_new(jcam, "ptz", json_string("false"));

            cpt++;

            json_array_append_new(jdata, jcam);
        }
    }

    return jdata;
}

json_t *JsonApiClient::buildJsonAudio()
{
    json_t *jdata = json_array();

    for (int i = 0;i < AudioManager::Instance().get_size();i++)
    {
        AudioPlayer *player = AudioManager::Instance().get_player(i);

        json_t *jaudio = json_object();
        json_object_set_new(jaudio, "id", json_string(Utils::to_string(i).c_str()));
        json_object_set_new(jaudio, "input_id", json_string(player->get_param("iid").c_str()));
        json_object_set_new(jaudio, "output_id", json_string(player->get_param("oid").c_str()));
        json_object_set_new(jaudio, "name", json_string(player->get_param("name").c_str()));
        json_object_set_new(jaudio, "type", json_string(player->get_param("type").c_str()));

        json_object_set_new(jaudio, "playlist", json_string(player->canPlaylist()?"true":"false"));
        json_object_set_new(jaudio, "database", json_string(player->canDatabase()?"true":"false"));

        if (player->get_params().Exists("amp"))
            json_object_set_new(jaudio, "avr", json_string(player->get_param("amp").c_str()));

        json_array_append_new(jdata, jaudio);

        //don't query detailed player infos here, other informations need to be queried to the squeezecenter
        //so the get_home request will be delayed by all the squeezecenter's requests.
        //To be faster, only return the basic infos here, and call get_state for each players to get detailed infos
    }

    return jdata;
}

void JsonApiClient::sendJson(json_t *json)
{
    char *d = json_dumps(json, JSON_COMPACT | JSON_ENSURE_ASCII /*| JSON_ESCAPE_SLASH*/);
    if (!d)
    {
        cDebugDom("network") << "json_dumps failed!";

        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = buildHttpResponse(HTTP_500, headers, HTTP_500_BODY);
        sendToClient(res);
        CloseConnection();

        return;
    }
    json_decref(json);

    string data(d);

    Params headers;
    headers.Add("Connection", "Close");
    headers.Add("Cache-Control", "no-cache, must-revalidate");
    headers.Add("Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
    headers.Add("Content-Type", "application/json");
    headers.Add("Content-Length", Utils::to_string(data.size()));
    string res = buildHttpResponse(HTTP_200, headers, data);
    sendToClient(res);
}

void JsonApiClient::processGetHome()
{
    json_t *jret = nullptr;

    jret = json_pack("{s:o, s:o, s:o}",
                     "home", buildJsonHome(),
                     "cameras", buildJsonCameras(),
                     "audio", buildJsonAudio());

    sendJson(jret);
}

void JsonApiClient::processGetState(json_t *jroot)
{
    json_incref(jroot);
    json_t *jinputs = json_object();
    json_t *joutputs = json_object();
    json_t *jaudio = json_array();

    json_t *jin = json_object_get(jroot, "inputs");
    if (jin && json_is_array(jin))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jin, idx, value)
        {
            string svalue;

            if (!json_is_string(value)) continue;

            svalue = json_string_value(value);
            Input *input = ListeRoom::Instance().get_input(svalue);
            if (input)
            {
                if (input->get_type() == TBOOL)
                    json_object_set_new(jinputs, svalue.c_str(), json_string(input->get_value_bool()?"true":"false"));
                else if (input->get_type() == TINT)
                    json_object_set_new(jinputs, svalue.c_str(), json_string(Utils::to_string(input->get_value_double()).c_str()));
                else if (input->get_type() == TSTRING)
                    json_object_set_new(jinputs, svalue.c_str(), json_string(input->get_value_string().c_str()));
            }
        }
    }

    json_t *jout = json_object_get(jroot, "outputs");
    if (jout && json_is_array(jout))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jout, idx, value)
        {
            string svalue;

            if (!json_is_string(value)) continue;

            svalue = json_string_value(value);
            Output *output = ListeRoom::Instance().get_output(svalue);
            if (output)
            {
                if (output->get_type() == TBOOL)
                    json_object_set_new(joutputs, svalue.c_str(), json_string(output->get_value_bool()?"true":"false"));
                else if (output->get_type() == TINT)
                    json_object_set_new(joutputs, svalue.c_str(), json_string(Utils::to_string(output->get_value_double()).c_str()));
                else if (output->get_type() == TSTRING)
                    json_object_set_new(joutputs, svalue.c_str(), json_string(output->get_value_string().c_str()));
            }
        }
    }

    player_count = 0;

    json_t *jpl = json_object_get(jroot, "audio_players");
    if (jpl && json_is_array(jpl))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jpl, idx, value)
        {
            string svalue;

            if (!json_is_string(value)) continue;
            svalue = json_string_value(value);

            int pid;
            Utils::from_string(svalue, pid);
            if (pid < 0 || pid >= AudioManager::Instance().get_size())
                continue;

            player_count++;

            json_t *jplayer = json_object();
            json_object_set_new(jplayer, "player_id", json_string(Utils::to_string(pid).c_str()));

            AudioPlayer *player = AudioManager::Instance().get_player(pid);
            player->get_playlist_current([=](AudioPlayerData data1)
            {
                json_object_set_new(jplayer,
                                    "playlist_current_track",
                                    json_string(Utils::to_string(data1.ivalue).c_str()));

                player->get_volume([=](AudioPlayerData data2)
                {
                    json_object_set_new(jplayer,
                                        "volume",
                                        json_string(Utils::to_string(data2.ivalue).c_str()));

                    player->get_playlist_size([=](AudioPlayerData data3)
                    {
                        json_object_set_new(jplayer,
                                            "playlist_size",
                                            json_string(Utils::to_string(data3.ivalue).c_str()));

                        player->get_current_time([=](AudioPlayerData data4)
                        {
                            json_object_set_new(jplayer,
                                                "time_elapsed",
                                                json_string(Utils::to_string(data4.dvalue).c_str()));

                            player->get_status([=](AudioPlayerData data5)
                            {
                                string status;
                                switch (data5.ivalue)
                                {
                                case AudioPlay: status = "playing"; break;
                                case AudioPause: status = "pause"; break;
                                case AudioStop: status = "stop"; break;
                                default:
                                case AudioError: status = "error"; break;
                                case AudioSongChange: status = "song_change"; break;
                                }

                                json_object_set_new(jplayer,
                                                    "status",
                                                    json_string(status.c_str()));

                                json_t *jtrack = json_object();
                                player->get_songinfo([=](AudioPlayerData data6)
                                {
                                    Params &infos = data6.params;
                                    for (int i = 0;i < infos.size();i++)
                                    {
                                        string inf_key, inf_value;
                                        infos.get_item(i, inf_key, inf_value);

                                        json_object_set_new(jtrack,
                                                            inf_key.c_str(),
                                                            json_string(inf_value.c_str()));
                                    }

                                    json_object_set_new(jplayer,
                                                        "current_track",
                                                        jtrack);

                                    //Add player to array, and send data back if all players requests are done.
                                    json_array_append_new(jaudio, jplayer);
                                    player_count--;

                                    if (player_count <= 0)
                                    {
                                        json_decref(jroot);
                                        json_t *jret = json_object();
                                        jret = json_pack("{s:o, s:o, s:o}",
                                                         "inputs", jinputs,
                                                         "outputs", joutputs,
                                                         "audio_players", jaudio);

                                        sendJson(jret);
                                    }
                                });
                            });
                        });
                    });
                });
            });
        }
    }

    //only send data if there is not audio players
    if (player_count == 0)
    {
        json_decref(jroot);
        json_t *jret = json_object();
        jret = json_pack("{s:o, s:o, s:o}",
                         "inputs", jinputs,
                         "outputs", joutputs,
                         "audio_players", jaudio);

        sendJson(jret);
    }
}

void JsonApiClient::processSetState()
{
    bool success = true;

    if (jsonParam["type"] == "input")
    {
        Input *input = ListeRoom::Instance().get_input(jsonParam["id"]);
        if (!input)
            success = false;
        else
        {
            if (input->get_type() == TBOOL)
                input->force_input_bool(jsonParam["value"] == "true");
            else if (input->get_type() == TINT)
            {
                double dv;
                Utils::from_string(jsonParam["value"], dv);
                input->force_input_double(dv);
            }
            else if (input->get_type() == TSTRING)
                input->force_input_string(jsonParam["value"]);
        }
    }
    else if (jsonParam["type"] == "output")
    {
        Output *output = ListeRoom::Instance().get_output(jsonParam["id"]);
        if (!output)
            success = false;
        else
        {
            if (output->get_type() == TBOOL)
                output->set_value(jsonParam["value"] == "true");
            else if (output->get_type() == TINT)
            {
                if (!output->set_value(jsonParam["value"]))
                  {
                    double dv;
                    Utils::from_string(jsonParam["value"], dv);
                    output->set_value(dv);
                  }
            }
            else if (output->get_type() == TSTRING)
                output->set_value(jsonParam["value"]);
        }
    }
    else if (jsonParam["type"] == "audio")
    {
        int pid;
        Utils::from_string(jsonParam["player_id"], pid);
        if (pid < 0 || pid >= AudioManager::Instance().get_size())
            success = false;
        else
        {
            AudioPlayer *player = AudioManager::Instance().get_player(pid);

            Params cmd;
            cmd.Parse(jsonParam["value"]);

            if (cmd["0"] == "play") player->Play();
            else if (cmd["0"] == "pause") player->Pause();
            else if (cmd["0"] == "stop") player->Stop();
            else if (cmd["0"] == "next") player->Next();
            else if (cmd["0"] == "previous") player->Previous();
            else if (cmd["0"] == "off") player->Power(false);
            else if (cmd["0"] == "on") player->Power(true);
            else if (cmd["0"] == "volume")
            {
                int vol = 0;
                Utils::from_string(cmd["1"], vol);
                player->set_volume(vol);
            }
            else if (cmd["0"] == "time")
            {
                int _t = 0;
                Utils::from_string(cmd["1"], _t);
                player->set_current_time(_t);
            }
            else if (cmd["0"] == "playlist")
            {
                if (cmd["1"] == "clear") player->playlist_clear();
                else if (cmd["1"] == "save") player->playlist_save(cmd["2"]);
                else if (cmd["1"] == "add") player->playlist_add_items(cmd["2"]);
                else if (cmd["1"] == "play") player->playlist_play_items(cmd["2"]);
                else if (Utils::is_of_type<int>(cmd["1"]))
                {
                    int item = 0;
                    Utils::from_string(cmd["1"], item);

                    if (cmd["2"] == "moveup") player->playlist_moveup(item);
                    else if (cmd["2"] == "movedown") player->playlist_movedown(item);
                    else if (cmd["2"] == "delete") player->playlist_delete(item);
                    else if (cmd["2"] == "play") player->playlist_play(item);
                }
            }
            else if (cmd["0"] == "random")
            {
                vector<string> tk;
                split(cmd["1"], tk, ":", 2);
                if (tk.size() == 2 && tk[0] == "random_id")
                    player->get_database()->setRandomsType(tk[1]);
            }
            else if (cmd["0"] == "options")
            {
                if (cmd["1"] == "sync" && cmd["2"] == "off")
                    player->Synchronize("", false);
                else if (cmd["1"] == "sync")
                {
                    vector<string> tk;
                    split(cmd["2"], tk, ":", 2);
                    if (tk.size() == 2 && tk[0] == "id")
                        player->Synchronize(tk[1], true);
                }
            }
            else if (cmd["0"] == "database")
            {
                if (cmd["1"] == "playlist" && cmd["2"] == "delete")
                {
                    vector<string> tk;
                    split(cmd["3"], tk, ":", 2);
                    if (tk.size() == 2 && tk[0] == "playlist_id")
                        player->playlist_delete(tk[1]);
                }
            }
        }
    }
    else if (jsonParam["type"] == "camera")
    {
        int pid;
        Utils::from_string(jsonParam["camera_id"], pid);
        if (pid < 0 || pid >= CamManager::Instance().get_size())
            success = false;
        else
        {
            IPCam *camera = CamManager::Instance().get_camera(pid);
            if (camera)
            {
                if (jsonParam["camera_action"] == "move")
                {
                    int action = -1;

                    if (jsonParam["value"] == "left") action = 1;
                    if (jsonParam["value"] == "right") action = 1;
                    if (jsonParam["value"] == "up") action = 1;
                    if (jsonParam["value"] == "down") action = 1;
                    if (jsonParam["value"] == "home") action = 1;
                    if (jsonParam["value"] == "zoomin") action = 1;
                    if (jsonParam["value"] == "zoomout") action = 1;

                    if (action == 1)
                        camera->activateCapabilities("ptz", "move", jsonParam["camera_action"]);

                    //move to a preset position
                    if (Utils::is_of_type<int>(jsonParam["camera_action"]))
                        camera->activateCapabilities("position", "recall", jsonParam["camera_action"]);
                }
                else if (jsonParam["camera_action"] == "save")
                {
                    if (Utils::is_of_type<int>(jsonParam["value"]))
                        camera->activateCapabilities("position", "save", jsonParam["value"]);
                }
            }
        }
    }

    json_t *jret = json_object();
    json_object_set_new(jret, "success", json_string(success?"true":"false"));
    sendJson(jret);
}

void JsonApiClient::processGetPlaylist()
{
    int pid;
    Utils::from_string(jsonParam["player_id"], pid);
    if (pid < 0 || pid >= AudioManager::Instance().get_size())
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        sendJson(jret);
        return;
    }

    AudioPlayer *player = AudioManager::Instance().get_player(pid);

    json_t *jplayer = json_object();

    player->get_playlist_current([=](AudioPlayerData data)
    {
        json_object_set_new(jplayer,
                            "current_track",
                            json_string(Utils::to_string(data.ivalue).c_str()));

        player->get_playlist_size([=](AudioPlayerData data1)
        {
            json_object_set_new(jplayer,
                                "count",
                                json_string(Utils::to_string(data1.ivalue).c_str()));

            int it_count = data1.ivalue;
            if (it_count <= 0)
            {
                json_object_set_new(jplayer, "items", json_array());
                sendJson(jplayer);
            }
            else
                getNextPlaylistItem(player, jplayer, json_array(), 0, it_count);

        });
    });
}

void JsonApiClient::getNextPlaylistItem(AudioPlayer *player, json_t *jplayer, json_t *jplaylist, int it_current, int it_count)
{
    player->get_playlist_item(it_current, [=](AudioPlayerData data)
    {
        json_t *jtrack = json_object();
        Params &infos = data.params;
        for (int i = 0;i < infos.size();i++)
        {
            string inf_key, inf_value;
            infos.get_item(i, inf_key, inf_value);

            json_object_set_new(jtrack,
                                inf_key.c_str(),
                                json_string(inf_value.c_str()));
        }

        json_array_append_new(jplaylist, jtrack);

        int idx = it_current + 1;
        if (idx >= it_count)
        {
            //all track are queried, send back data
            json_object_set_new(jplayer,
                                "items",
                                jplaylist);
            sendJson(jplayer);
        }
        else
        {
            getNextPlaylistItem(player, jplayer, jplaylist, idx, it_count);
        }
    });
}

void JsonApiClient::processPolling()
{
    json_t *jret = json_object();

    if (jsonParam["type"] == "register")
    {
        string uuid = PollListenner::Instance().Register();
        json_object_set_new(jret, "uuid", json_string(uuid.c_str()));
    }
    else if (jsonParam["type"] == "unregister")
    {
        string uuid = jsonParam["uuid"];
        bool success = PollListenner::Instance().Unregister(uuid);
        json_object_set_new(jret, "success", json_string(success?"true":"false"));
    }
    else if (jsonParam["type"] == "get")
    {
        string uuid = jsonParam["uuid"];
        Params events;

        bool res = PollListenner::Instance().GetEvents(uuid, events);
        if (!res)
            json_object_set_new(jret, "success", json_string("false"));
        else
        {
            json_t *jev = json_array();

            for (int i = 0;i < events.size();i++)
            {
                string key, value;
                events.get_item(i, key, value);

                string ev = key + ":" + url_encode(value);
                vector<string> spl;
                Utils::split(ev, spl, ":");

                ev.clear();
                for (uint j = 0;j < spl.size();j++)
                {
                    ev += spl[j];
                    if (j < spl.size() - 1)
                        ev += " ";
                }

                json_array_append_new(jev, json_string(ev.c_str()));
            }

            json_object_set_new(jret, "success", json_string("true"));
            json_object_set_new(jret, "events", jev);
        }

    }

    sendJson(jret);
}

void JsonApiClient::processGetCover()
{
    int pid;
    Utils::from_string(jsonParam["player_id"], pid);
    if (pid < 0 || pid >= AudioManager::Instance().get_size())
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("player_id not set"));
        sendJson(jret);
        return;
    }

    AudioPlayer *player = AudioManager::Instance().get_player(pid);

    string w, h;
    if (jsonParam.Exists("width"))
        w = jsonParam["width"];
    if (jsonParam.Exists("height"))
        h = jsonParam["height"];

    player->get_album_cover([=](AudioPlayerData data)
    {
        //do not start another exe if one is running already
        if (data.svalue == "" || exe_thumb)
        {
            json_t *jret = json_object();
            json_object_set_new(jret, "success", json_string("false"));
            json_object_set_new(jret, "error_str", json_string("unable to get url"));
            sendJson(jret);
            return;
        }

        string cmd = "calaos_thumb " + data.svalue + " " + tempfname;
        if (w.empty() || h.empty())
            cmd += " " + w + "x" + h;
        exe_thumb = ecore_exe_run(cmd.c_str(), nullptr);
    });
}

void JsonApiClient::processGetCameraPic()
{
    int pid;
    Utils::from_string(jsonParam["camera_id"], pid);
    if (pid < 0 || pid >= CamManager::Instance().get_size())
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("camera_id not set"));
        sendJson(jret);
        return;
    }

    IPCam *camera = CamManager::Instance().get_camera(pid);

    string w, h;
    if (jsonParam.Exists("width"))
        w = jsonParam["width"];
    if (jsonParam.Exists("height"))
        h = jsonParam["height"];

    string cmd = "calaos_thumb " + camera->get_picture() + " " + tempfname;
    if (w.empty() || h.empty())
        cmd += " " + w + "x" + h;
    exe_thumb = ecore_exe_run(cmd.c_str(), nullptr);
}

void JsonApiClient::exeFinished(Ecore_Exe *exe, int exit_code)
{
    if (exit_code != 0)
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("unable to load data from url"));
        sendJson(jret);
        return;
    }

    ecore_exe_free(exe);

    json_t *jret = json_object();
    json_object_set_new(jret, "success", json_string("true"));
    json_object_set_new(jret, "contenttype", json_string("image/jpeg"));
    json_object_set_new(jret, "encoding", json_string("base64"));
    json_object_set_new(jret, "data", json_string(Utils::getFileContentBase64(tempfname.c_str()).c_str()));
    sendJson(jret);
}

void JsonApiClient::processConfig(json_t *jroot)
{
    json_t *jret = json_object();

    if (jsonParam["type"] == "get")
    {
        Config::Instance().SaveConfigIO();
        Config::Instance().SaveConfigRule();

        json_t *jfiles = json_object();
        json_object_set_new(jfiles, "io.xml",
                            json_string(Utils::getFileContent(Utils::getConfigFile(IO_CONFIG).c_str()).c_str()));
        json_object_set_new(jfiles, "rules.xml",
                            json_string(Utils::getFileContent(Utils::getConfigFile(RULES_CONFIG).c_str()).c_str()));
        json_object_set_new(jfiles, "local_config.xml",
                            json_string(Utils::getFileContent(Utils::getConfigFile(LOCAL_CONFIG).c_str()).c_str()));

        json_object_set_new(jret, "config_files", jfiles);
        json_object_set_new(jret, "success", json_string("true"));
    }
    else if (jsonParam["type"] == "put")
    {
        bool ret = true;
        json_t *jfiles = json_object_get(jroot, "config_files");
        if (jfiles && json_is_object(jfiles))
        {
            const char *key;
            json_t *value;

            json_object_foreach(jfiles, key, value)
            {
                if (key && json_is_string(value))
                {
                    string skey = key;
                    if (skey != IO_CONFIG &&
                        skey != RULES_CONFIG &&
                        skey != LOCAL_CONFIG)
                    {
                        cErrorDom("network") << "Error, file " << skey << " is not a valid config filename";
                        ret = false;
                        continue;
                    }

                    string filecontent = json_string_value(value);

                    ofstream ofs(Utils::getConfigFile(key), ios::out | ios::trunc);

                    if (ofs.is_open())
                    {
                        ofs << filecontent;
                        ofs.close();
                    }
                    else
                    {
                        cErrorDom("network") << "Error, key " << key << " is not a string";
                        ret = false;
                    }
                }
                else
                {
                    cErrorDom("network") << "Error, key " << key << " is not a string";
                    ret = false;
                }
            }
        }
        else
        {
            ret = false;
            cErrorDom("network") << "Error, wrong query";
        }

        json_object_set_new(jret, "success", json_string(ret?"true":"false"));

        if (ret)
            need_restart = true;
    }
    else
    {
        json_object_set_new(jret, "success", json_string("false"));
    }

    sendJson(jret);
}
