/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef S_ConsoleTunnel_H
#define S_ConsoleTunnel_H

#include "JsonApi.h"

#include <memory>

class Timer;
class WebSocketFrame;

namespace uvw
{
class TcpHandle;
class PipeHandle;
} //namespace uvw

/* /api/console (docs/08_http_api.md, Console): the browser's WebSocket,
 * relayed message by message to the console socket calaos-container opens for
 * it (docs/API.md of calaos-container, Console). The first browser message
 * authenticates and names the verified push code, and POST admin/console runs
 * from here: a console is only attached by the connection that opened it, and
 * no console id or socket path is ever taken from a client.
 */
class ConsoleTunnel: public JsonApi
{
public:
    static constexpr const char *Path = "/api/console";
    static constexpr const char *Protocol = "calaos-console.v1";
    static constexpr int MaxDimension = 1000;
    //The daemon reads at most 64 KiB per message
    static constexpr size_t MaxClientMessage = 64 * 1024;
    static constexpr size_t InputChunk = 16 * 1024;
    static constexpr size_t MaxDaemonMessage = 1024 * 1024;
    static constexpr size_t MaxHandshake = 16 * 1024;
    //A write queue above HighWater stops reading the other side, until it is
    //back under LowWater: neither side is buffered without bound
    static constexpr size_t HighWater = 256 * 1024;
    static constexpr size_t LowWater = 64 * 1024;

    struct Timings
    {
        double open = 30.0;   //for the open message
        double attach = 10.0; //for the console socket to answer the upgrade
    };
    static void setTimings(const Timings &t) { timings = t; }

    ConsoleTunnel(HttpClient *client, const std::shared_ptr<uvw::TcpHandle> &clientHandle);
    ~ConsoleTunnel() override;

    //Whether a Sec-WebSocket-Protocol header offers Protocol
    static bool offersProtocol(const string &header);

    //The socket the daemon must name for consoleId, empty when consoleId is
    //not one of its ids or dir is empty
    static string expectedSocket(const string &dir, const string &consoleId);

    void clientText(const string &data);
    void clientBinary(const string &data);
    //A close frame from the browser, which the WebSocket answers itself
    void clientClosing(uint16_t code);

    sigc::signal<void, const string &> sendBinary;

private:
    enum class State
    {
        WaitOpen,
        Opening,   //credentials, admin state, POST admin/console
        Attaching, //console socket connection and upgrade
        Open,
        Closed,
    };

    static Timings timings;

    State state = State::WaitOpen;
    std::shared_ptr<uvw::TcpHandle> client;
    std::shared_ptr<uvw::PipeHandle> daemon;
    bool daemonUpgraded = false;
    Timer *timer = nullptr;

    int columns = 0;
    int rows = 0;
    Json ready;

    string key;
    string inbuf;
    string message;
    bool inMessage = false;
    bool binaryMessage = false;
    std::unique_ptr<WebSocketFrame> frame;

    bool clientPaused = false;
    bool daemonPaused = false;

    void arm(double delay, const string &code);
    void open(const string &data);
    bool authenticate(const Json &request);
    void openConsole(const string &session, const Json &body);
    void opened(const DaemonReply &reply);
    void attach(const string &socket);
    void upgradeDaemon(uvw::PipeHandle &h);

    void daemonData(const char *data, size_t len);
    bool daemonHandshake();
    void daemonFrames();
    void daemonGone();

    bool tooBig(const string &data);
    void writeInput(const string &bytes);
    void sendDaemon(int opcode, const string &payload);
    void writeDaemon(const string &bytes);

    //Before the console is attached: an admin error, then 1008
    void refuse(const Json &error);
    //Once attached: closes the browser with clientCode, the socket with daemonCode
    void finish(uint16_t clientCode, const string &reason, uint16_t daemonCode);
    void resumeClient();
    //0 closes the socket without a close frame
    void closeDaemon(uint16_t code);
};

#endif
