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
#include "McpProxyHandler.h"
#include "McpServerManager.h"
#include "libuvw.h"

#include <regex>
#include <sstream>
#include <cctype>

namespace Calaos
{

namespace {

constexpr std::size_t SNIFF_LIMIT = 8 * 1024; // bail out if no request line in 8 KiB

// S5: strict path regex. /mcp itself or one or more "/<segment>" where each
// segment is a conservative subset of safe URL characters. Query string is
// not allowed at sniff time — uvicorn validates it later.
bool isValidMcpPath(const std::string &path)
{
    static const std::regex re(R"(^/mcp(?:/[A-Za-z0-9._~-]+)*/?$)");
    return std::regex_match(path, re);
}

// Lowercase ASCII compare.
bool iequals(const std::string &a, const char *b)
{
    std::size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// S8: scan headers in the request prefix for HTTP smuggling indicators.
// Returns true if smuggling is suspected.
bool detectSmuggling(const std::string &headerBlock)
{
    int contentLengthCount = 0;
    bool hasTransferEncoding = false;
    bool transferEncodingOk = true;

    std::istringstream iss(headerBlock);
    std::string line;
    // skip request line
    if (!std::getline(iss, line)) return false;

    while (std::getline(iss, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break; // end of headers

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        // strip leading spaces from value
        std::size_t v0 = value.find_first_not_of(" \t");
        if (v0 != std::string::npos) value = value.substr(v0);

        if (iequals(name, "content-length"))
        {
            contentLengthCount++;
        }
        else if (iequals(name, "transfer-encoding"))
        {
            hasTransferEncoding = true;
            // Lowercase value for comparison
            std::string lv;
            lv.reserve(value.size());
            for (char c : value)
                lv.push_back(std::tolower(static_cast<unsigned char>(c)));
            if (lv != "chunked" && lv != "identity")
                transferEncodingOk = false;
        }
    }

    if (contentLengthCount > 1) return true;
    if (hasTransferEncoding && contentLengthCount > 0) return true;
    if (hasTransferEncoding && !transferEncodingOk) return true;
    return false;
}

} // namespace

McpProxyHandler::SniffResult McpProxyHandler::sniffRequest(const std::string &buf)
{
    auto endOfLine = buf.find("\r\n");
    if (endOfLine == std::string::npos)
    {
        if (buf.size() > SNIFF_LIMIT) return SniffResult::NotMcp;
        return SniffResult::NotEnoughData;
    }

    std::string requestLine = buf.substr(0, endOfLine);
    // expect: METHOD SP PATH SP HTTP/x.y
    auto firstSpace = requestLine.find(' ');
    if (firstSpace == std::string::npos) return SniffResult::NotMcp;
    auto secondSpace = requestLine.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) return SniffResult::NotMcp;

    std::string target = requestLine.substr(firstSpace + 1,
                                            secondSpace - firstSpace - 1);
    // strip query string for path validation
    std::string path = target;
    auto q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);

    if (path != "/mcp" && path.compare(0, 5, "/mcp/") != 0
        && path.compare(0, 5, "/mcp?") != 0)
        return SniffResult::NotMcp;

    if (!isValidMcpPath(path))
        return SniffResult::InvalidPath;

    // Wait for the end of the header block before validating smuggling, but
    // only up to SNIFF_LIMIT. If we never see "\r\n\r\n", proceed without
    // the smuggling check rather than stalling the connection.
    auto headersEnd = buf.find("\r\n\r\n");
    if (headersEnd == std::string::npos)
    {
        if (buf.size() < SNIFF_LIMIT) return SniffResult::NotEnoughData;
    }
    else
    {
        std::string headerBlock = buf.substr(0, headersEnd + 2);
        if (detectSmuggling(headerBlock))
            return SniffResult::Smuggling;
    }

    return SniffResult::Mcp;
}

void McpProxyHandler::sendError(std::shared_ptr<uvw::TcpHandle> client,
                                int status,
                                const std::string &message)
{
    if (!client || client->closing())
        return;

    std::string reason;
    switch (status)
    {
    case 400: reason = "Bad Request"; break;
    case 502: reason = "Bad Gateway"; break;
    case 503: reason = "Service Unavailable"; break;
    default: reason = "Error"; break;
    }

    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << " " << reason << "\r\n"
         << "Content-Type: text/plain; charset=utf-8\r\n"
         << "Content-Length: " << message.size() << "\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << message;

    std::string out = resp.str();
    auto buf = std::unique_ptr<char[]>(new char[out.size()]);
    std::copy(out.begin(), out.end(), buf.get());
    client->write(std::move(buf), out.size());
}

McpProxyHandler::McpProxyHandler(std::shared_ptr<uvw::TcpHandle> c,
                                 const std::string &initialBytes)
    : client(std::move(c)),
      pendingToSidecar(initialBytes)
{
    const std::string &path = McpServerManager::Instance().socketPath();
    if (path.empty())
    {
        cErrorDom("mcp") << "MCP sidecar socket path is empty";
        sendError(client, 503, "MCP sidecar not configured\n");
        teardown();
        return;
    }
    connectToSidecar(path);
}

McpProxyHandler::~McpProxyHandler()
{
    teardown();
}

void McpProxyHandler::connectToSidecar(const std::string &path)
{
    auto loop = uvw::Loop::getDefault();
    sidecar = loop->resource<uvw::PipeHandle>();

    sidecar->once<uvw::ConnectEvent>([this](const auto &, auto &)
    {
        onSidecarConnected();
    });
    sidecar->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
    {
        writeToClient(std::string(ev.data.get(), ev.length));
    });
    sidecar->once<uvw::EndEvent>([this](const auto &, auto &)
    {
        cDebugDom("mcp") << "sidecar pipe EOF";
        teardown();
    });
    sidecar->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &)
    {
        cWarningDom("mcp") << "sidecar pipe error: " << ev.what();
        if (!sidecarReady)
            sendError(client, 502, "MCP sidecar unavailable\n");
        teardown();
    });

    sidecar->connect(path);
}

void McpProxyHandler::onSidecarConnected()
{
    sidecarReady = true;
    sidecar->read();
    if (!pendingToSidecar.empty())
    {
        std::string data;
        data.swap(pendingToSidecar);
        writeToSidecar(data);
    }
}

void McpProxyHandler::onClientData(const std::string &data)
{
    if (closed) return;
    if (!sidecarReady)
    {
        pendingToSidecar.append(data);
        // Tiny safety bound: refuse to buffer megabytes before the sidecar is
        // even up (would be a DoS vector if the sidecar is wedged).
        if (pendingToSidecar.size() > 1024 * 1024)
        {
            cWarningDom("mcp")
                << "dropping client connection: too much data buffered before sidecar ready";
            teardown();
        }
        return;
    }
    writeToSidecar(data);
}

void McpProxyHandler::onClientClose()
{
    teardown();
}

void McpProxyHandler::writeToSidecar(const std::string &data)
{
    if (!sidecar || sidecar->closing()) return;
    auto buf = std::unique_ptr<char[]>(new char[data.size()]);
    std::copy(data.begin(), data.end(), buf.get());
    sidecar->write(std::move(buf), data.size());
}

void McpProxyHandler::writeToClient(const std::string &data)
{
    if (!client || client->closing()) return;
    auto buf = std::unique_ptr<char[]>(new char[data.size()]);
    std::copy(data.begin(), data.end(), buf.get());
    client->write(std::move(buf), data.size());
}

void McpProxyHandler::teardown()
{
    if (closed) return;
    closed = true;

    if (sidecar)
    {
        if (!sidecar->closing()) sidecar->close();
        sidecar.reset();
    }
    if (client)
    {
        if (!client->closing()) client->close();
        client.reset();
    }
}

}
