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
#ifndef S_McpProxyHandler_H
#define S_McpProxyHandler_H

#include "Calaos.h"

namespace uvw {
class TcpHandle;
class PipeHandle;
}

namespace Calaos
{

// Reverse proxy that splices a TCP connection to /mcp/* on the calaos_server
// HTTP port (5454) with the calaos_mcp Python sidecar listening on a local
// Unix domain socket. All bytes are forwarded raw in both directions; this
// process never inspects the HTTP framing beyond the initial request line
// (used for path validation, see McpProxyHandler::sniffRequest).
//
// One instance is created per TCP connection that targets /mcp; it is
// destroyed when either end of the splice closes.
class McpProxyHandler
{
public:
    enum class SniffResult
    {
        NotEnoughData,    // not enough bytes yet to make a decision
        NotMcp,           // request line does not target /mcp
        Mcp,              // request targets /mcp — caller should create the proxy
        InvalidPath,      // request line targets /mcp but path is malformed (S5)
        Smuggling,        // suspicious Content-Length / Transfer-Encoding combo (S8)
    };

    // Inspect a buffer of raw bytes (the first chunk(s) of an HTTP request)
    // and decide whether it targets the /mcp/* path. Returns Mcp if the
    // caller should switch the connection to proxy mode. The status line is
    // expected within the first 8 KiB; beyond that we conservatively bail
    // out as NotMcp.
    static SniffResult sniffRequest(const std::string &buf);

    McpProxyHandler(std::shared_ptr<uvw::TcpHandle> client,
                    const std::string &initialBytes);
    ~McpProxyHandler();

    // Push more bytes received from the client TCP connection to the sidecar.
    void onClientData(const std::string &data);

    // Invoked when the client TCP connection closes (either side).
    void onClientClose();

    // Send an in-band HTTP error to the client before tearing down. Used by
    // the routing layer to reply 400/502 without bringing the sidecar in.
    static void sendError(std::shared_ptr<uvw::TcpHandle> client,
                          int status,
                          const std::string &message);

    bool isClosed() const { return closed; }

private:
    void connectToSidecar(const std::string &path);
    void onSidecarConnected();
    void writeToSidecar(const std::string &data);
    void writeToClient(const std::string &data);
    void teardown();

    std::shared_ptr<uvw::TcpHandle> client;
    std::shared_ptr<uvw::PipeHandle> sidecar;

    std::string pendingToSidecar;
    bool sidecarReady = false;
    bool closed = false;
};

}

#endif
