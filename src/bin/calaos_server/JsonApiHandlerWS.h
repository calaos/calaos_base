/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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

protected:
    // Allow subclasses to set authentication state (e.g., for HMAC auth)
    void setAuthenticated(bool auth) { loggedin = auth; }
    bool isAuthenticated() const { return loggedin; }

    /* E4.1s: the jansson overload is GONE. Its ONE behaviour that the
     * nlohmann overload does not have - OMITTING the "data" member when the
     * pointer was null, where `jroot["data"] = json` would write "data":null -
     * lives on in sendJsonNoData() below. Golden
     * e40e_ws_get_state_without_data pins that omission.
     */
    void sendJson(const string &msg_type, const Json &json, const string &client_id = string());
    void sendJsonNoData(const string &msg_type, const string &client_id = string());

    bool loggedin = false;

private:
    //Source address of the client, "unknown" when there is no connection
    string clientIp() const;

    void processLoginService(const Params &jsonData, const string &client_id = string());
    sigc::connection evcon;

    sigc::signal<void, string, string, void*, void*> sig_events;

    void handleEvents(const CalaosEvent &event);

    void processGetHome(const Params &jsonReq, const string &client_id = string());
    /* E4.1s: a POINTER, and deliberately so. `json_object_get(jroot, "data")`
     * answered NULL for an ABSENT member and a non null json_null for a
     * member spelled `"data": null`, and these two functions branch on exactly
     * that difference. A `const Json &` cannot carry it - a null Json would
     * merge the two cases and turn `"data": null` into the no-data answer.
     */
    void processGetState(const Json *jdata, const string &client_id = string());
    void processGetStates(const Params &jsonReq, const string &client_id = string());
    void processQuery(const Params &jsonReq, const string &client_id = string());
    void processGetParam(const Params &jsonReq, const string &client_id = string());
    void processSetParam(const Params &jsonReq, const string &client_id = string());
    void processDelParam(const Params &jsonReq, const string &client_id = string());
    void processSetState(Params &jsonReq, const string &client_id = string());
    void processGetPlaylist(Params &jsonReq, const string &client_id = string());
    void processGetIO(const Json *jdata, const string &client_id = string());
    void processGetTimerange(const Params &jsonReq, const string &client_id = string());
    void processSetTimerange(const Json &jdata, const string &client_id = string());
    void processEventLog(const Params &jsonReq, const string &client_id = string());
    void processRegisterPush(const Params &jsonReq, const string &client_id = string());
    void processSettings(const Params &jsonReq, const string &client_id = string());

    //E4.1s: ONE document. The json_t* twin that carried the dispatch is gone
    //with the request parse it came from.
    void processAudio(const Json &jdataDoc, const string &client_id = string());
    void processAudioDb(const Json &jdataDoc, const string &client_id = string());

    //E4.1r: the "data" member as a document, not a second parse.
    void processAutoscenario(const Json &jdata, const string &client_id = string());
};

#endif // JSONAPIV3_H
