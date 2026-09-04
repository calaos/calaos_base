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
#include "McpRequestFilter.h"

namespace uvw {
class TcpHandle;
class PipeHandle;
}

namespace Calaos
{

// Reverse proxy that joins a TCP connection to /mcp/* on the calaos_server
// HTTP port (5454) with the calaos_mcp Python sidecar listening on a local
// Unix domain socket. Sidecar to client is a raw splice; client to sidecar
// goes through McpRequestFilter, which frames the stream request by request so
// that every head carries the client identity THIS process measured and none
// the client wrote (see the note in McpRequestFilter.h).
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

    // peerIp is the TCP peer of `client`; credential is the secret the
    // sidecar checks before believing the identity we write.
    McpProxyHandler(std::shared_ptr<uvw::TcpHandle> client,
                    const std::string &initialBytes,
                    const std::string &peerIp,
                    const std::string &credential);
    ~McpProxyHandler();

    // Push more bytes received from the client TCP connection to the sidecar.
    void onClientData(const std::string &data);

    // Invoked when the client TCP connection closes (either side).
    void onClientClose();

    // Send an in-band HTTP error to a raw client connection before tearing it
    // down. Used by the routing layer to reply 400/502 without bringing the
    // sidecar in, and by HttpServer to reply 503 when the connection limit is
    // reached. Does not close the handle, the caller decides when to.
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

    McpRequestFilter::Filter filter;
    std::string pendingToSidecar;
    bool sidecarReady = false;
    bool closed = false;

    /* The sidecar pipe callbacks below outlive this object: closing the client
     * connection makes HttpServer delete the WebSocket, which deletes this
     * handler synchronously, while the pipe may still have events queued in
     * the loop. Every callback holds a weak reference on this token and gives
     * up when it has expired, which happens when the handler is destroyed.
     */
    std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);
};

}

#endif
