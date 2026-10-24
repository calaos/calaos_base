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
#include "ConsoleTunnel.h"
#include "AdminState.h"
#include "DaemonClient.h"
#include "HttpClient.h"
#include "JsonField.h"
#include "McpServerManager.h"
#include "Timer.h"
#include "WebSocketFrame.h"
#include "libuvw.h"

#include <cmath>
#include <sstream>

ConsoleTunnel::Timings ConsoleTunnel::timings;

namespace
{

//The terms of the daemon's POST admin/console
bool isTerm(const string &term)
{
    return term == "xterm-256color" || term == "xterm" || term == "vt100";
}

//An integer member inside 1..MaxDimension, 0 otherwise
int dimension(const Json &j, const char *key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer())
        return 0;
    const long long v = it->get<long long>();
    return v >= 1 && v <= ConsoleTunnel::MaxDimension ? (int)v : 0;
}

//The daemon closes with 1000 or 1001: any other code, on either side, reads
//as a console that went away
uint16_t relayedCode(uint16_t code)
{
    return code == WebSocketFrame::CloseCodeNormal ? code : (uint16_t)WebSocketFrame::CloseCodeGoingAway;
}

const char *const OpenExpected = "The first message must be the open message";

} //namespace

ConsoleTunnel::ConsoleTunnel(HttpClient *owner, const std::shared_ptr<uvw::TcpHandle> &clientHandle):
    JsonApi(owner),
    client(clientHandle),
    frame(new WebSocketFrame())
{
    arm(timings.open, "invalid_request");

    //The handle outlives this object: the callback checks the token first
    std::weak_ptr<bool> alive = apiAlive;
    client->on<uvw::WriteEvent>([this, alive](const uvw::WriteEvent &, uvw::TcpHandle &h)
                                {
        if (alive.expired() || !daemonPaused || !daemon || h.writeQueueSize() > LowWater)
            return;
        daemonPaused = false;
        daemon->read();
    });
}

ConsoleTunnel::~ConsoleTunnel()
{
    //Closing the socket can publish an error at once: nothing may reach the
    //WebSocket, which is already destroyed
    state = State::Closed;
    delete timer;
    closeDaemon(WebSocketFrame::CloseCodeGoingAway);
}

bool ConsoleTunnel::offersProtocol(const string &header)
{
    std::istringstream in(header);
    string item;
    while (std::getline(in, item, ','))
    {
        if (Utils::trim(item) == Protocol)
            return true;
    }
    return false;
}

string ConsoleTunnel::expectedSocket(const string &dir, const string &consoleId)
{
    if (dir.empty() || !isDaemonId(consoleId))
        return string();
    return dir + "/console-" + consoleId + ".sock";
}

void ConsoleTunnel::arm(double delay, const string &code)
{
    delete timer;
    //refuse() deletes the timer, which Timer allows from its own callback
    timer = new Timer(delay, [this, code]()
                      {
        refuse(adminError(code, string()));
    });
}

void ConsoleTunnel::clientText(const string &data)
{
    if (state == State::WaitOpen)
    {
        open(data);
        return;
    }
    if (state != State::Open || tooBig(data))
        return;

    const Json j = requestNestingWithinLimit(data) ? Json::parse(data, nullptr, false) : Json();
    const string type = JsonField::string(j, "type");
    if (type == "input")
    {
        writeInput(JsonField::string(j, "data"));
    }
    else if (type == "resize")
    {
        const int c = dimension(j, "columns");
        const int r = dimension(j, "rows");
        if (c && r)
            sendDaemon(WebSocketFrame::OpCodeText, Json{ { "type", "resize" }, { "columns", c }, { "rows", r } }.dump());
    }
}

void ConsoleTunnel::clientBinary(const string &data)
{
    if (state == State::WaitOpen)
        refuse(adminError("invalid_request", OpenExpected));
    else if (state == State::Open && !tooBig(data))
        writeInput(data);
}

void ConsoleTunnel::clientClosing(uint16_t code)
{
    if (state == State::Closed)
        return;
    state = State::Closed;
    DELETE_NULL(timer);
    closeDaemon(relayedCode(code));
}

void ConsoleTunnel::open(const string &data)
{
    state = State::Opening;
    DELETE_NULL(timer);

    const bool parsable = data.size() <= MaxClientMessage && requestNestingWithinLimit(data);
    const Json request = parsable ? Json::parse(data, nullptr, false) : Json();
    if (!request.is_object() || JsonField::string(request, "type") != "open")
    {
        refuse(adminError("invalid_request", OpenExpected));
        return;
    }

    const AdminAnswer refused = [this](const Json &j)
    {
        refuse(j);
    };
    if (!authenticate(request) || refuseServiceScope("console", refused))
        return;

    const string codeId = JsonField::string(request, "code_id");
    columns = dimension(request, "columns");
    rows = dimension(request, "rows");
    const bool hasTerm = request.contains("term");
    const string term = JsonField::string(request, "term");

    string invalid;
    if (!isDaemonId(codeId))
        invalid = "Invalid code_id";
    else if (!columns || !rows)
        invalid = "columns and rows must be integers from 1 to 1000";
    else if (hasTerm && !isTerm(term))
        invalid = "Invalid term";
    if (!invalid.empty())
    {
        refuse(adminError("invalid_request", invalid));
        return;
    }

    //Built from the validated fields only, whatever else the message holds
    Json body = { { "code_id", codeId } };
    if (hasTerm)
        body["term"] = term;
    const string session = JsonField::string(request, "admin_session");

    withAdminMode([this, session, body]()
                  {
        const Json refusal = adminModeRefusal();
        if (!refusal.is_null())
            refuse(refusal);
        //Without the header the bearer alone authorizes: never call the daemon so
        else if (!isValidAdminSession(session))
            refuse(adminError("bad_session", string()));
        else if (state == State::Opening)
            openConsole(session, body);
    });
}

bool ConsoleTunnel::authenticate(const Json &request)
{
    //The login_service credential opens a service scoped session, which
    //refuseServiceScope() then refuses
    const bool service = request.contains("token");
    const string bucket = loginThrottleKey(service ? "service:" : "");
    const double now = Utils::getMainLoopTime();

    if (LoginThrottle::isBlocked(bucket, now))
    {
        Json j = adminError("rate_limited", string());
        j["retry_after"] = std::to_string((long long)std::ceil(LoginThrottle::retryAfter(bucket, now)));
        refuse(j);
        return false;
    }

    const string &expected = McpServerManager::Instance().getServiceToken();
    const bool ok = service ? !expected.empty() && secureCompare(expected, JsonField::string(request, "token"))
                            : checkCredentials(JsonField::string(request, "cn_user"), JsonField::string(request, "cn_pass"));
    if (!ok)
    {
        LoginThrottle::registerFailure(bucket, now);
        cWarningDom("admin") << "Console refused to " << loginClientForLog(bucket) << ": wrong credentials";
        refuse(adminError("login_failed", string()));
        return false;
    }

    LoginThrottle::registerSuccess(bucket);
    serviceScope = service;
    return true;
}

void ConsoleTunnel::openConsole(const string &session, const Json &body)
{
    DaemonClient::Request req;
    req.method = "POST";
    req.path = "admin/console";
    req.body = body;
    req.session = session;
    req.clientIp = adminClientIp();

    std::weak_ptr<bool> alive = apiAlive;
    DaemonClient::Instance().call(req, [this, alive](const DaemonReply &reply)
                                  {
        if (!alive.expired())
            opened(reply);
    });
}

void ConsoleTunnel::opened(const DaemonReply &reply)
{
    //The browser left while the daemon answered
    if (state != State::Opening)
        return;

    if (reply.kind != DaemonReply::Kind::Ok)
    {
        refuse(adminRelayFailure(reply));
        return;
    }

    //Only the socket rebuilt from a well formed id under the mounted directory
    //is ever connected, whatever path the daemon names
    const string id = JsonField::string(reply.data, "console_id");
    const string socket = expectedSocket(DaemonClient::Instance().consoleDir(), id);
    if (socket.empty() || JsonField::string(reply.data, "socket") != socket ||
        JsonField::string(reply.data, "ws_path") != "/ws" || JsonField::string(reply.data, "ws_protocol") != Protocol)
    {
        cErrorDom("admin") << "admin/console answered a console outside the console directory or protocol";
        refuse(adminError("internal", string()));
        return;
    }

    ready = Json{ { "type", "ready" }, { "console_id", id } };
    for (const char *k: { "expires_in", "idle_timeout" })
    {
        if (reply.data.contains(k) && reply.data[k].is_number_integer())
            ready[k] = reply.data[k];
    }

    cInfoDom("admin") << "Console " << id << " opened by " << adminClientIp();
    attach(socket);
}

void ConsoleTunnel::attach(const string &socket)
{
    state = State::Attaching;
    arm(timings.attach, "daemon_unreachable");
    key = DaemonClient::webSocketKey();

    daemon = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
    std::weak_ptr<bool> alive = apiAlive;

    daemon->once<uvw::ConnectEvent>([this, alive](const uvw::ConnectEvent &, uvw::PipeHandle &h)
                                    {
        if (!alive.expired() && state == State::Attaching)
            upgradeDaemon(h);
    });
    daemon->on<uvw::DataEvent>([this, alive](const uvw::DataEvent &ev, uvw::PipeHandle &)
                               {
        if (!alive.expired())
            daemonData(ev.data.get(), ev.length);
    });
    daemon->on<uvw::EndEvent>([this, alive](const uvw::EndEvent &, uvw::PipeHandle &)
                              {
        if (!alive.expired())
            daemonGone();
    });
    daemon->on<uvw::ErrorEvent>([this, alive](const uvw::ErrorEvent &ev, uvw::PipeHandle &)
                                {
        if (!alive.expired())
        {
            cDebugDom("admin") << "Console socket: " << ev.what();
            daemonGone();
        }
    });
    daemon->on<uvw::WriteEvent>([this, alive](const uvw::WriteEvent &, uvw::PipeHandle &h)
                                {
        if (alive.expired() || !clientPaused || h.writeQueueSize() > LowWater)
            return;
        clientPaused = false;
        client->read();
    });

    daemon->connect(socket);
}

void ConsoleTunnel::upgradeDaemon(uvw::PipeHandle &h)
{
    h.read();
    //Nothing but the upgrade: the socket's mode is the daemon's gate
    std::ostringstream req;
    req << "GET /ws HTTP/1.1\r\n"
        << "Host: localhost\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "Sec-WebSocket-Protocol: " << Protocol << "\r\n"
        << "\r\n";
    writeDaemon(req.str());
}

void ConsoleTunnel::daemonData(const char *data, size_t len)
{
    //Nothing is read from a socket being shut down
    const bool live = state == State::Attaching || state == State::Open;
    if (live)
        inbuf.append(data, len);
    if (live && (state == State::Open || daemonHandshake()))
        daemonFrames();
}

bool ConsoleTunnel::daemonHandshake()
{
    const string::size_type end = inbuf.find("\r\n\r\n");
    if (end == string::npos)
    {
        if (inbuf.size() > MaxHandshake)
        {
            cErrorDom("admin") << "Console socket: handshake too long";
            refuse(adminError("internal", string()));
        }
        return false;
    }

    map<string, string> headers;
    const int status = DaemonClient::parseHttpHead(inbuf.substr(0, end), headers);
    inbuf.erase(0, end + 4);

    if (status != 101 || Utils::str_to_lower(headers["upgrade"]) != "websocket" ||
        headers["sec-websocket-accept"] != DaemonClient::webSocketAccept(key) ||
        headers["sec-websocket-protocol"] != Protocol)
    {
        cErrorDom("admin") << "Console socket refused the upgrade (HTTP " << status << ")";
        refuse(adminError("internal", string()));
        return false;
    }

    DELETE_NULL(timer);
    state = State::Open;
    daemonUpgraded = true;
    sendDaemon(WebSocketFrame::OpCodeText, Json{ { "columns", columns }, { "rows", rows } }.dump());
    sendData.emit(ready.dump());
    return true;
}

void ConsoleTunnel::daemonFrames()
{
    while (state == State::Open && !inbuf.empty())
    {
        if (!frame->processFrameData(inbuf))
            break;

        //A zero length frame is flagged as an error by the parser yet stays valid
        if (!frame->isValid() || frame->hasMask())
        {
            finish(WebSocketFrame::CloseCodeGoingAway, "console closed", WebSocketFrame::CloseCodeProtocolError);
            return;
        }

        const int opcode = frame->getOpcode();
        const string payload = frame->hasError() ? string() : frame->getPayload();
        const bool final = frame->isFinalFrame();
        frame->clear();

        if (opcode == WebSocketFrame::OpCodePing)
        {
            sendDaemon(WebSocketFrame::OpCodePong, payload);
        }
        else if (opcode == WebSocketFrame::OpCodeClose)
        {
            const uint16_t code = payload.size() >= 2 ? (uint16_t)(((uint8_t)payload[0] << 8) | (uint8_t)payload[1]) : 0;
            finish(relayedCode(code), "console closed", code ? code : (uint16_t)WebSocketFrame::CloseCodeNormal);
            return;
        }
        else if (opcode != WebSocketFrame::OpCodePong)
        {
            if ((opcode == WebSocketFrame::OpCodeContinue) != inMessage)
            {
                finish(WebSocketFrame::CloseCodeGoingAway, "console closed", WebSocketFrame::CloseCodeProtocolError);
                return;
            }
            if (opcode != WebSocketFrame::OpCodeContinue)
            {
                message.clear();
                binaryMessage = opcode == WebSocketFrame::OpCodeBinary;
            }
            message += payload;
            inMessage = !final;
            if (message.size() > MaxDaemonMessage)
            {
                finish(WebSocketFrame::CloseCodeGoingAway, "console closed", WebSocketFrame::CloseCodeTooMuchData);
                return;
            }
            //The daemon sends the pty output as binary and nothing as text
            if (final && binaryMessage)
                sendBinary.emit(message);
        }
    }

    if (state == State::Open && !daemonPaused && client->writeQueueSize() > HighWater)
    {
        daemonPaused = true;
        daemon->stop();
    }
}

void ConsoleTunnel::daemonGone()
{
    if (state == State::Attaching)
        refuse(adminError("daemon_unreachable", string()));
    else if (state == State::Open)
        finish(WebSocketFrame::CloseCodeGoingAway, "console closed", 0);
}

bool ConsoleTunnel::tooBig(const string &data)
{
    if (data.size() <= MaxClientMessage)
        return false;
    finish(WebSocketFrame::CloseCodeTooMuchData, "message too big", WebSocketFrame::CloseCodeNormal);
    return true;
}

void ConsoleTunnel::writeInput(const string &bytes)
{
    //A write can fail at once and close the socket
    for (size_t at = 0; at < bytes.size() && state == State::Open; at += InputChunk)
        sendDaemon(WebSocketFrame::OpCodeBinary, bytes.substr(at, InputChunk));

    if (state == State::Open && !clientPaused && daemon->writeQueueSize() > HighWater)
    {
        clientPaused = true;
        client->stop();
    }
}

void ConsoleTunnel::sendDaemon(int opcode, const string &payload)
{
    writeDaemon(WebSocketFrame::makeFrame(opcode, payload, true, DaemonClient::webSocketMask()));
}

void ConsoleTunnel::writeDaemon(const string &bytes)
{
    auto buf = std::unique_ptr<char[]>(new char[bytes.size()]);
    std::copy(bytes.begin(), bytes.end(), buf.get());
    if (daemon)
        daemon->write(std::move(buf), (unsigned int)bytes.size());
}

void ConsoleTunnel::refuse(const Json &error)
{
    //The browser may have left while an answer was on its way
    if (state != State::Closed)
    {
        state = State::Closed;
        DELETE_NULL(timer);

        Json j = error;
        j["type"] = "error";
        sendData.emit(j.dump());
        closeConnection.emit(WebSocketFrame::CloseCodePolicyViolated, "console refused");
        closeDaemon(WebSocketFrame::CloseCodeNormal);
    }
}

void ConsoleTunnel::finish(uint16_t clientCode, const string &reason, uint16_t daemonCode)
{
    state = State::Closed;
    closeConnection.emit(clientCode, reason);
    closeDaemon(daemonCode);
}

void ConsoleTunnel::closeDaemon(uint16_t code)
{
    if (!daemon)
        return;

    if (daemonUpgraded && code)
    {
        string payload;
        payload.push_back((char)(code >> 8));
        payload.push_back((char)(code & 0xff));
        sendDaemon(WebSocketFrame::OpCodeClose, payload);
    }

    //Shut down rather than closed: the close frame still in the queue goes out
    std::shared_ptr<uvw::PipeHandle> h;
    h.swap(daemon);
    h->once<uvw::ShutdownEvent>([](const uvw::ShutdownEvent &, uvw::PipeHandle &p)
                                {
        p.close();
    });
    h->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &, uvw::PipeHandle &p)
                             {
        p.close();
    });
    h->shutdown();
}
