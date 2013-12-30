/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
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
#include "JsonApiClient.h"
#include <vmime/utility/url.hpp>
#include "ListeRoom.h"
#include "ListeRule.h"

#define HTTP_400 "HTTP/1.1 400 Bad Request"
#define HTTP_404 "HTTP/1.1 404 Not Found"
#define HTTP_500 "HTTP/1.1 500 Internal Server Error"
#define HTTP_200 "HTTP/1.1 200 OK"

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

        Utils::logger("network") << Priority::DEBUG << "JsonApiClient::JsonApiClient("
                                 << this << "): Ok" << log4cpp::eol;
}

JsonApiClient::~JsonApiClient()
{
        free(parser);
        CloseConnection();

        Utils::logger("network") << Priority::DEBUG << "JsonApiClient::~JsonApiClient("
                                 << this << "): Ok" << log4cpp::eol;
}

void JsonApiClient::ProcessData(string request)
{
        size_t nparsed;

        nparsed = http_parser_execute(parser, &parser_settings, request.c_str(), request.size());

        if (parser->upgrade)
        {
                /* handle new protocol */
                Utils::logger("network") << Priority::DEBUG << "Protocol Upgrade not supported, closing connection." << log4cpp::eol;
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

                //init parser again
                http_parser_init(parser, HTTP_REQUEST);
                parser->data = this;

                vmime::utility::url req_url("http://0.0.0.0" + parse_url);

                if (req_url.getPath() != "/api" &&
                    req_url.getPath() != "/api.php")
                {
                        Params headers;
                        headers.Add("Connection", "close");
                        headers.Add("Content-Type", "text/html");
                        string res = buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
                        sendToClient(res);

                        return;
                }

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
                        headers.Add("Connection", "Keep-Alive");
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

void JsonApiClient::CloseConnection()
{
        DELETE_NULL_FUNC(ecore_con_client_del, client_conn);
}

string JsonApiClient::buildHttpResponse(string code, Params &headers, string body)
{
        stringstream res;

        //HTTP code
        res << code << "\r\n";

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
        if (!client_conn || ecore_con_client_send(client_conn, res.c_str(), res.length()) == 0)
        {
                Utils::logger("network") << Priority::CRIT
                                         << "JsonApiClient::handleRequest(): Error sending data ! Closing connection." << log4cpp::eol;

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
                Utils::logger("network") << Priority::DEBUG << "JsonApiClient: JSON - Error loading json : " << jerr.text << log4cpp::eol;

                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "text/html");
                string res = buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
                sendToClient(res);
                CloseConnection();

                return;
        }

        cout << json_dumps(jroot, JSON_INDENT(4)) << endl;

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

                json_decref(value);
        }

        json_decref(jroot);

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
                Utils::logger("network") << Priority::DEBUG << "JsonApiClient: Login failed!" << log4cpp::eol;

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
                processGetState();
        else if (jsonParam["action"] == "set_state")
                processSetState();
        else if (jsonParam["action"] == "get_playlist")
                processGetPlaylist();
        else if (jsonParam["action"] == "poll_listen")
                processPolling();
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
                //To be faster, only return the basic infos here, and expand the api with more audio commands
        }

        return jdata;
}

void JsonApiClient::processGetHome()
{
        json_t *jret = nullptr;

        jret = json_pack("{s:o, s:o, s:o}",
                         "home", buildJsonHome(),
                         "cameras", buildJsonCameras(),
                         "audio", buildJsonAudio());

        char *d = json_dumps(jret, JSON_COMPACT | JSON_ENSURE_ASCII /*| JSON_ESCAPE_SLASH*/);
        if (!d)
        {
                Utils::logger("network") << Priority::DEBUG << "JsonApiClient: json_dumps failed!" << log4cpp::eol;

                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "text/html");
                string res = buildHttpResponse(HTTP_500, headers, HTTP_500_BODY);
                sendToClient(res);
                CloseConnection();

                return;
        }

        string data(d);

        Params headers;
        headers.Add("Connection", "Keep-Alive");
        headers.Add("Cache-Control", "no-cache, must-revalidate");
        headers.Add("Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
        headers.Add("Content-Type", "application/json");
        headers.Add("Content-Length", Utils::to_string(data.size()));
        string res = buildHttpResponse(HTTP_200, headers, data);
        sendToClient(res);
}

void JsonApiClient::processGetState()
{

}

void JsonApiClient::processSetState()
{

}

void JsonApiClient::processGetPlaylist()
{

}

void JsonApiClient::processPolling()
{

}
