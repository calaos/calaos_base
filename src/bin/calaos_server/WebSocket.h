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
#ifndef S_WebSocketServer_H
#define S_WebSocketServer_H

#include "Calaos.h"
#include <Ecore_Con.h>
#include <unordered_map>
#include "HttpClient.h"
#include "WebSocketFrame.h"

namespace Calaos {

class WebSocket: public HttpClient
{
public:
    WebSocket(Ecore_Con_Client *cl);
    virtual ~WebSocket();

    sigc::signal<void, const std::string &> textMessageReceived;
    sigc::signal<void, const std::string &> binaryMessageReceived;
    sigc::signal<void> websocketDisconnected;

    /* Called by JsonApiServer whenever data comes in */
    virtual void ProcessData(std::string data);

    void sendPing(const std::string &data);
    void sendCloseFrame(uint16_t code = WebSocketFrame::CloseCodeNormal, const std::string &reason = std::string(), bool forceClose = false);

    void sendTextMessage(const std::string &data);
    void sendBinaryMessage(const std::string &data);

private:

    bool echoMode = false;

    enum { WSConnecting, WSOpened, WSClosing, WSClosed };
    int status = WSConnecting;

    std::string recv_buffer;

    WebSocketFrame currentFrame;
    std::string currentData;
    int currentOpcode;

    bool isfragmented = false;

    double ping_time = 0.0;
    EcoreTimer *closeTimeout = nullptr;
    bool closeReceived = false;

    void reset(); //reset state machine

    bool checkHandshakeRequest();
    void processHandshake();
    void processFrame(const std::string &data);
    void processControlFrame();

    bool checkCloseStatusCode(uint16_t code);

    void sendFrameData(const std::string &data, bool isbinary);

    EcoreTimer *timerPing = nullptr;
};

}

#endif
