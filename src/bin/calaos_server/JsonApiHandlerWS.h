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
#ifndef JSONAPIV3_H
#define JSONAPIV3_H

#include "JsonApi.h"
#include "EventManager.h"

class JsonApiHandlerWS: public JsonApi
{
public:
    JsonApiHandlerWS(HttpClient *client);
    virtual ~JsonApiHandlerWS();

    virtual void processApi(const string &data, const Params &paramsGET);

private:

    sigc::connection evcon;

    sigc::signal<void, string, string, void*, void*> sig_events;

    void handleEvents(const CalaosEvent &event);

    bool loggedin = false;

    void sendJson(const string &msg_type, json_t *data, const string &client_id = string());
    void sendJson(const string &msg_type, const Params &p, const string &client_id = string());

    void processGetHome(const Params &jsonReq, const string &client_id = string());
    void processGetState(json_t *jdata, const string &client_id = string());
    void processGetStates(const Params &jsonReq, const string &client_id = string());
    void processQuery(const Params &jsonReq, const string &client_id = string());
    void processGetParam(const Params &jsonReq, const string &client_id = string());
    void processSetParam(const Params &jsonReq, const string &client_id = string());
    void processDelParam(const Params &jsonReq, const string &client_id = string());
    void processSetState(Params &jsonReq, const string &client_id = string());
    void processGetPlaylist(Params &jsonReq, const string &client_id = string());
    void processGetIO(json_t *jdata, const string &client_id = string());
    void processGetTimerange(const Params &jsonReq, const string &client_id = string());
    void processSetTimerange(json_t *jdata, const string &client_id = string());

    void processAudio(json_t *jdata, const string &client_id = string());
    void processAudioDb(json_t *jdata, const string &client_id = string());

    void processAutoscenario(json_t *jdata, const string &client_id = string());
};

#endif // JSONAPIV3_H
