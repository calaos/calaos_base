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
#include "AVRRoseNotifServer.h"
#include "AVRRose.h"
#include "libuvw.h"

using namespace Calaos;

AVRRoseNotifServer::AVRRoseNotifServer()
{
    auto loop = uvw::Loop::getDefault();
    listenHandle = loop->resource<uvw::TcpHandle>();
    listenHandle->bind("0.0.0.0", NOTIF_PORT);
    listenHandle->listen();

    listenHandle->on<uvw::ListenEvent>([this](const uvw::ListenEvent &, uvw::TcpHandle &srv)
    {
        auto client = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
        srv.accept(*client);

        string remoteIP = client->peer().ip;
        cDebugDom("hifirose") << "Notification connection from " << remoteIP;

        // Accumulate data in a shared buffer
        auto buffer = std::make_shared<string>();

        client->on<uvw::DataEvent>([this, remoteIP, buffer](const uvw::DataEvent &ev, uvw::TcpHandle &h)
        {
            buffer->append(ev.data.get(), ev.length);

            // Check if we have a complete HTTP request (headers + body)
            // Look for the end of headers
            auto headerEnd = buffer->find("\r\n\r\n");
            if (headerEnd == string::npos)
                return; // Not enough data yet

            // Extract Content-Length to know when the body is complete
            string headers = buffer->substr(0, headerEnd);
            size_t contentLength = 0;

            string clHeader = "Content-Length:";
            auto clPos = headers.find(clHeader);
            if (clPos == string::npos)
            {
                // Try lowercase
                clHeader = "content-length:";
                clPos = headers.find(clHeader);
            }
            if (clPos != string::npos)
            {
                auto clValueStart = clPos + clHeader.length();
                auto clValueEnd = headers.find("\r\n", clValueStart);
                if (clValueEnd == string::npos)
                    clValueEnd = headers.length();
                string clValue = headers.substr(clValueStart, clValueEnd - clValueStart);
                // Trim whitespace
                clValue.erase(0, clValue.find_first_not_of(" \t"));
                clValue.erase(clValue.find_last_not_of(" \t") + 1);
                try { contentLength = std::stoul(clValue); }
                catch (...) { contentLength = 0; }
            }

            size_t bodyStart = headerEnd + 4;
            size_t bodyReceived = buffer->length() - bodyStart;

            if (bodyReceived < contentLength)
                return; // Body not complete yet

            // We have a complete request — process it
            string fullRequest = *buffer;
            buffer->clear();

            handleClientData(remoteIP, fullRequest);

            // Send HTTP 200 response
            string resp = buildHttpResponse("Data received");
            auto respData = std::make_unique<char[]>(resp.size());
            std::copy(resp.begin(), resp.end(), respData.get());
            h.write(std::move(respData), resp.size());

            // Close after response
            h.once<uvw::WriteEvent>([](const uvw::WriteEvent &, uvw::TcpHandle &handle)
            {
                handle.close();
            });
        });

        client->once<uvw::EndEvent>([](const uvw::EndEvent &, uvw::TcpHandle &h)
        {
            h.close();
        });

        client->once<uvw::ErrorEvent>([remoteIP](const uvw::ErrorEvent &ev, uvw::TcpHandle &h)
        {
            cWarningDom("hifirose") << "Notification client error from " << remoteIP << ": " << ev.what();
            h.close();
        });

        client->read();
    });

    listenHandle->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &ev, uvw::TcpHandle &)
    {
        cErrorDom("hifirose") << "Notification server error: " << ev.what();
    });

    cInfoDom("hifirose") << "Push notification server listening on port " << NOTIF_PORT;
}

AVRRoseNotifServer::~AVRRoseNotifServer()
{
    if (listenHandle)
    {
        listenHandle->stop();
        listenHandle->close();
    }
    cInfoDom("hifirose") << "Push notification server stopped";
}

void AVRRoseNotifServer::registerReceiver(AVRRose *receiver)
{
    receivers.push_back(receiver);
    cDebugDom("hifirose") << "Registered receiver for " << receiver->getHost();
}

void AVRRoseNotifServer::unregisterReceiver(AVRRose *receiver)
{
    receivers.remove(receiver);
    cDebugDom("hifirose") << "Unregistered receiver for " << receiver->getHost();
}

void AVRRoseNotifServer::handleClientData(const string &remoteIP, const string &rawData)
{
    // Parse minimal HTTP request: first line for method + path, body after \r\n\r\n
    auto firstLineEnd = rawData.find("\r\n");
    if (firstLineEnd == string::npos)
    {
        cWarningDom("hifirose") << "Malformed HTTP request from " << remoteIP;
        return;
    }

    string firstLine = rawData.substr(0, firstLineEnd);

    // Parse "POST /device_state_noti HTTP/1.1"
    string method, path;
    auto sp1 = firstLine.find(' ');
    if (sp1 != string::npos)
    {
        method = firstLine.substr(0, sp1);
        auto sp2 = firstLine.find(' ', sp1 + 1);
        if (sp2 != string::npos)
            path = firstLine.substr(sp1 + 1, sp2 - sp1 - 1);
    }

    // Extract body
    string body;
    auto bodyStart = rawData.find("\r\n\r\n");
    if (bodyStart != string::npos)
        body = rawData.substr(bodyStart + 4);

    cDebugDom("hifirose") << "Notification: " << method << " " << path << " from " << remoteIP;

    processHttpRequest(remoteIP, method, path, body);
}

void AVRRoseNotifServer::processHttpRequest(const string &remoteIP, const string &method,
                                             const string &path, const string &body)
{
    if (path == "/test")
    {
        cDebugDom("hifirose") << "Health check from " << remoteIP;
        return;
    }

    if (path != "/device_state_noti")
    {
        cDebugDom("hifirose") << "Unknown notification path: " << path << " from " << remoteIP;
        return;
    }

    // Parse JSON body
    Json jmsg;
    try
    {
        jmsg = Json::parse(body);
    }
    catch (const std::exception &e)
    {
        cWarningDom("hifirose") << "Failed to parse notification JSON from " << remoteIP << ": " << e.what();
        return;
    }

    // Find the receiver for this IP
    AVRRose *receiver = findReceiverForIP(remoteIP);
    if (!receiver)
    {
        // If we only have one receiver, dispatch to it regardless of IP
        // (the device may use a different source IP for notifications)
        if (receivers.size() == 1)
            receiver = receivers.front();
        else
        {
            cWarningDom("hifirose") << "No receiver found for notification from " << remoteIP;
            return;
        }
    }

    receiver->handleNotification(jmsg);
}

AVRRose *AVRRoseNotifServer::findReceiverForIP(const string &remoteIP)
{
    for (auto *r : receivers)
    {
        if (r->getHost() == remoteIP)
            return r;
    }
    return nullptr;
}

string AVRRoseNotifServer::buildHttpResponse(const string &body)
{
    string resp;
    resp += "HTTP/1.1 200 OK\r\n";
    resp += "Content-Type: text/plain\r\n";
    resp += "Content-Length: " + Utils::to_string(body.size()) + "\r\n";
    resp += "Connection: close\r\n";
    resp += "\r\n";
    resp += body;
    return resp;
}
