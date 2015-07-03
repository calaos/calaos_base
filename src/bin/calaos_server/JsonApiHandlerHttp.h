/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#include <Ecore.h>
#include "Room.h"
#include "AudioPlayer.h"

using namespace Calaos;

class JsonApiHandlerHttp: public JsonApi
{
public:
    JsonApiHandlerHttp(HttpClient *client);
    virtual ~JsonApiHandlerHttp();

    virtual void processApi(const string &data);

private:

    Ecore_Event_Handler *exe_handler;
    Ecore_Exe *exe_thumb;
    string tempfname;

    Params jsonParam;

    void sendJson(json_t *json);
    void sendJson(const Params &p);

    //processing functions
    void processGetHome();
    void processGetState(json_t *jroot);
    void processSetState();
    void processGetPlaylist();
    void processPolling();
    void processGetCover();
    void processGetCameraPic();
    void processConfig(json_t *jroot);
    void processGetIO(json_t *jroot);

    void processAudio(json_t *jroot);

    void getNextPlaylistItem(AudioPlayer *player, json_t *jplayer, json_t *jplaylist, int it_current, int it_count);

    void exeFinished(Ecore_Exe *exe, int exit_code);
    friend Eina_Bool _ecore_exe_finished(void *data, int type, void *event);
};

#endif // JSONAPIV2_H
