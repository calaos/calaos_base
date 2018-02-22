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
    string tempfname;

    Params jsonParam;

    UrlDownloader *cameraDl = nullptr;
    bool camHeaderSent = false;

    void sendJson(json_t *json);
    void sendJson(const Json &json);

    //processing functions
    void processGetHome();
    void processGetState(json_t *jroot);
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
    void processConfig(json_t *jroot);
    void processGetIO(json_t *jroot);
    void processGetTimerange();
    void processSetTimerange(json_t *jroot);
    void processAutoscenario(json_t *jroot);
    void processCamera();
    void processEventLog();
    void processEventPicture();

    void processAudio(json_t *jroot);
    void processAudioDb(json_t *jroot);

    void getNextPlaylistItem(AudioPlayer *player, json_t *jplayer, json_t *jplaylist, int it_current, int it_count);

    void exeFinished(int exit_code);

    void downloadCameraPicture(IPCam *camera);
};

#endif // JSONAPIV2_H
