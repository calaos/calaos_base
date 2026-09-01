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
#ifndef JSONAPIV2_H
#define JSONAPIV2_H

#include "JsonApi.h"
#include "Room.h"
#include "AudioPlayer.h"
#include "UrlDownloader.h"
#include "IPCam.h"
#include "json.hpp"

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class ProcessHandle;
}

using namespace Calaos;

class JsonApiHandlerHttp: public JsonApi
{
public:
    JsonApiHandlerHttp(HttpClient *client);
    virtual ~JsonApiHandlerHttp();

    virtual void processApi(const string &data, const Params &paramsGET);

private:

    std::shared_ptr<uvw::ProcessHandle> exe_thumb;
    bool exe_thumb_running = false;
    string tempfname;

    Params jsonParam;

    UrlDownloader *cameraDl = nullptr;
    sigc::connection camConnData;
    sigc::connection camConnComplete;
    bool camHeaderSent = false;

    /* Alive token for the asynchronous callbacks kept outside of this object
     * (camera snapshots, timers, album cover): they capture a weak_ptr on it
     * and bail out when the handler has been destroyed in the meantime.
     */
    std::shared_ptr<bool> handlerAlive { std::make_shared<bool>(true) };

    /* E4.1s: the jansson overload is GONE. It was the last emitter of this
     * transport that dumped with JSON_COMPACT | JSON_ENSURE_ASCII, and with it
     * goes the last form-1 bytestream of the HTTP API. Everything now leaves
     * through the nlohmann overload and its three emission invariants
     * (E4.1b): sorted keys, ensure_ascii = true, error_handler_t::replace.
     */
    void sendJson(const Json &json);
    void sendLoginFailed();

    //Source address of the client, "unknown" when there is no connection
    string clientIp() const;

    //Rejects width/rotate when they are not plain integers of a sane range
    static bool checkPictureParams(const string &width, const string &rotate);
    //argv of calaos_picture, built argument by argument
    vector<string> buildPictureCommand(const string &url, const string &width, const string &rotate);

    void releaseCameraDl();

    //processing functions
    void processGetHome();
    //E4.1s: the request document itself, not a second parse. A body that did
    //not parse as an object is a NULL Json here, which is what a null json_t*
    //meant to these three: fall back to the GET parameters.
    void processGetState(const Json &jroot);
    void processGetStates();
    void processQuery();
    void processGetParam();
    void processSetParam();
    void processDelParam();
    void processSetState();
    void processGetPlaylist();
    void processPolling();
    void processGetCover();
    void processGetCameraPic();
    void processConfig(const Json &jroot);
    void processGetIO(const Json &jroot);
    void processGetTimerange();
    void processSetTimerange(const Json &jroot);
    //E4.1r: the document the dispatch already parsed, not a second parse.
    void processAutoscenario(const Json &jroot);
    void processCamera();
    void processEventLog();
    void processEventPicture();
    void processRegisterPush();

    //E4.1s: ONE document now. The json_t* twin that carried the dispatch is
    //gone with the request parse it came from.
    void processAudio(const Json &jrootDoc);
    void processAudioDb(const Json &jrootDoc);

    void exeFinished(int exit_code);

    void downloadCameraPicture(const string &cameraId);
};

#endif // JSONAPIV2_H
