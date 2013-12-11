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
#include <JsonApiClient.h>
#include <NTPClock.h>
#include <TCPServer.h>

extern NTPClock *ntpclock;

static const char *responseData = "HTTP/1.1 200 OK\r\n"
                                  "Content-Length: %ld\r\n"
                                  "Content-Type: %s\r\n"
                                  "Date: %s\r\n\r\n"
                                  "%s\r\n";

int
_parser_header_field(http_parser *parser, const char *at, size_t length)
{
        //   char *field;
        //   Ems_Stream_Client *client = parser->data;

        //   if (client->header_value && client->header_field)
        //     {
        //        eina_hash_add(client->request_headers,
        //                      eina_strbuf_string_get(client->header_field),
        //                      eina_stringshare_add(eina_strbuf_string_get(client->header_value)));

        //        eina_strbuf_free(client->header_field);
        //        eina_strbuf_free(client->header_value);
        //        client->header_field = NULL;
        //        client->header_value = NULL;
        //     }

        //   if (!client->header_field)
        //     client->header_field = eina_strbuf_new();

        //   eina_strbuf_append_length(client->header_field, at, length);
        //   field = eina_strbuf_string_steal(client->header_field);
        //   eina_str_tolower(&field);
        //   eina_strbuf_append(client->header_field, field);
        //   free(field);

        return 0;
}

int
_parser_header_value(http_parser *parser, const char *at, size_t length)
{
        //   Ems_Stream_Client *client = parser->data;

        //   if (!client->header_value)
        //     client->header_value = eina_strbuf_new();

        //   eina_strbuf_append_length(client->header_value, at, length);

        return 0;
}

int
_parser_headers_complete(http_parser *parser)
{
        //   Ems_Stream_Client *client = parser->data;
        //   client->headers_done = EINA_TRUE;

        //   if (client->header_value && client->header_field)
        //     {
        //        eina_hash_add(client->request_headers,
        //                      eina_strbuf_string_get(client->header_field),
        //                      eina_stringshare_add(eina_strbuf_string_get(client->header_value)));

        //        eina_strbuf_free(client->header_field);
        //        eina_strbuf_free(client->header_value);
        //        client->header_field = NULL;
        //        client->header_value = NULL;
        //     }

        //   DBG("parse headers done:");
        //   Eina_Iterator *it = eina_hash_iterator_tuple_new(client->request_headers);
        //   void *data;
        //   while (eina_iterator_next(it, &data))
        //     {
        //        Eina_Hash_Tuple *t = data;
        //        DBG("HEADER: %s : %s", (const char *)t->key, (const char *)t->data);
        //     }
        //   eina_iterator_free(it);

        return 0;
}

int
_parser_url(http_parser *parser, const char *at, size_t length)
{
        JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);


        //   Ems_Stream_Client *client = parser->data;
        //   char **arr;
        //   int i;
        //   Eina_Strbuf *path = eina_strbuf_new();

        //   eina_strbuf_append_length(path, at, length);
        //   arr = eina_str_split(eina_strbuf_string_get(path), "/", 0);

        //   for (i = 0; arr[i]; i++)
        //     {
        //        if (strlen(arr[i]) > 0)
        //          client->request_path = eina_list_append(client->request_path,
        //                                                  eina_stringshare_add(arr[i]));
        //     }

        //   free(arr[0]);
        //   free(arr);

        //   //TODO: parse query parameters like:
        //   // [/path/requested][?query]&[key=value]
        //   // This needs to be parsed from GET or POST requests

        //   DBG("URL Path requested: %s", eina_strbuf_string_get(path));
        //   eina_strbuf_free(path);

        //   Eina_List *l;
        //   const char *data;
        //   EINA_LIST_FOREACH(client->request_path, l, data)
        //      DBG("PATH: %s", data);

        return 0;
}

int
_parser_message_complete(http_parser *parser)
{
        JsonApiClient *client = reinterpret_cast<JsonApiClient *>(parser->data);

        client->parse_done = true;
        client->request_method = parser->method;

        return 0;
}

int
_parser_body_complete(http_parser* parser, const char *at, size_t length)
{


        return 0;
}

JsonApiClient::JsonApiClient(Ecore_Con_Client *cl):
        client_conn(cl)
{
        //set up callbacks for the parser
        parser_settings.on_message_begin = NULL;
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

        }
}

void JsonApiClient::CloseConnection()
{
        DELETE_NULL_FUNC(ecore_con_client_del, client_conn);
}
