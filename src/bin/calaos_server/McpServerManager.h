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
#ifndef S_McpServerManager_H
#define S_McpServerManager_H

#include "Calaos.h"
#include "Timer.h"

namespace uvw {
class ProcessHandle;
class PipeHandle;
}

namespace Calaos
{

// Singleton responsible for spawning and supervising the calaos_mcp Python
// sidecar that exposes the Model Context Protocol on path /mcp.
//
// Responsibilities:
//  - Auto-generate the MCP bearer token (mcp_token) and the service-account
//    token (mcp_service_token) on first boot, persist them in local_config.xml.
//  - Spawn $bindir/calaos_mcp via uvw::ProcessHandle, passing the Unix domain
//    socket path through the CALAOS_MCP_SOCKET environment variable.
//  - Re-spawn on unexpected exit with exponential backoff.
//  - Forward stdout/stderr to the calaos_server log under the "mcp" domain.
//  - Send SIGTERM at shutdown and wait for clean exit.
class McpServerManager
{
private:
    std::shared_ptr<uvw::ProcessHandle> processExe;
    std::shared_ptr<uvw::PipeHandle> pipeStdout;
    std::shared_ptr<uvw::PipeHandle> pipeStderr;

    std::string sockPath;
    std::string mcpToken;
    std::string serviceToken;

    std::string stdoutBuf;
    std::string stderrBuf;

    bool started = false;
    bool shuttingDown = false;
    int restartAttempts = 0;
    Timer *restartTimer = nullptr;

    McpServerManager();

    void ensureTokens();
    void spawnSidecar();
    void scheduleRestart();
    void flushStreamBuffer(std::string &buf, bool toStderr);

public:
    static McpServerManager &Instance();

    ~McpServerManager();

    void start();
    void stop();

    // Path of the Unix domain socket the sidecar listens on. Used by the
    // McpProxyHandler to forward /mcp/* HTTP requests.
    const std::string &socketPath() const { return sockPath; }

    // MCP bearer token shared with clients (Claude Desktop etc.). Returned by
    // the get_mcp_info JSON API action.
    const std::string &bearerToken() const { return mcpToken; }

    // Service token used by the Python sidecar to authenticate to JsonApi
    // as a scoped service account. Never sent to external clients.
    const std::string &getServiceToken() const { return serviceToken; }
};

}

#endif
