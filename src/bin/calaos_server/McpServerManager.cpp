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
#include "McpServerManager.h"
#include "Prefix.h"
#include "libuvw.h"
#include "Timer.h"

#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace Calaos
{

namespace {

constexpr const char *MCP_TOKEN_KEY = "mcp_token";
constexpr const char *MCP_SERVICE_TOKEN_KEY = "mcp_service_token";
constexpr const char *MCP_LISTEN_PORT_KEY = "port_api";
constexpr unsigned short DEFAULT_JSONAPI_PORT = 5454;

// Generate a 64-hex-char token (256 bits) by concatenating two random
// UUIDs and stripping the dashes. Utils::createRandomUuid relies on
// gettimeofday + rand which is not cryptographically strong but is
// the same source already used elsewhere in calaos_server. For a
// stronger source we would have to plumb a /dev/urandom reader; this
// is good enough for an MVP token that is rotatable.
std::string generateToken()
{
    auto strip = [](std::string s)
    {
        s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
        return s;
    };
    return strip(Utils::createRandomUuid()) + strip(Utils::createRandomUuid());
}

unsigned short jsonApiPort()
{
    unsigned short port = DEFAULT_JSONAPI_PORT;
    std::string opt = Utils::get_config_option(MCP_LISTEN_PORT_KEY);
    if (!opt.empty())
        from_string(opt, port);
    return port;
}

} // namespace

McpServerManager &McpServerManager::Instance()
{
    static McpServerManager inst;
    return inst;
}

McpServerManager::McpServerManager()
{
    sockPath = Utils::getCachePath();
    if (sockPath.empty() || sockPath.back() != '/')
        sockPath += "/";
    sockPath += "mcp.sock";
}

McpServerManager::~McpServerManager()
{
    stop();
    if (restartTimer)
    {
        delete restartTimer;
        restartTimer = nullptr;
    }
}

void McpServerManager::ensureTokens()
{
    mcpToken = Utils::get_config_option(MCP_TOKEN_KEY, true);
    if (mcpToken.empty())
    {
        mcpToken = generateToken();
        Utils::set_config_option(MCP_TOKEN_KEY, mcpToken);
        cInfoDom("mcp") << "generated new mcp_token, persisted to local_config.xml";
    }

    serviceToken = Utils::get_config_option(MCP_SERVICE_TOKEN_KEY, true);
    if (serviceToken.empty())
    {
        serviceToken = generateToken();
        Utils::set_config_option(MCP_SERVICE_TOKEN_KEY, serviceToken);
        cInfoDom("mcp") << "generated new mcp_service_token, persisted to local_config.xml";
    }
}

void McpServerManager::start()
{
    if (started)
        return;

    ensureTokens();

    // Remove any leftover socket from a previous instance. The Python
    // sidecar also does this on its own startup but doing it here as well
    // avoids a confusing log line when the sidecar inherits a stale file.
    ::unlink(sockPath.c_str());

    cInfoDom("mcp") << "starting MCP sidecar manager (uds=" << sockPath << ")";
    spawnSidecar();
    started = true;
}

void McpServerManager::stop()
{
    if (!started)
        return;
    shuttingDown = true;
    started = false;

    if (processExe && processExe->referenced())
    {
        cInfoDom("mcp") << "stopping MCP sidecar";
        processExe->kill(SIGTERM);
        processExe->close();
    }
    ::unlink(sockPath.c_str());
}

void McpServerManager::spawnSidecar()
{
    std::string exe = Prefix::Instance().binDirectoryGet() + "/calaos_mcp";

    // Skip silently if the wrapper is not installed (e.g. ./configure
    // --without-mcp or Python deps missing at build time).
    struct stat st;
    if (::stat(exe.c_str(), &st) != 0)
    {
        cWarningDom("mcp")
            << "calaos_mcp wrapper not found at " << exe
            << " (MCP sidecar disabled)";
        return;
    }

    auto loop = uvw::Loop::getDefault();
    processExe = loop->resource<uvw::ProcessHandle>();
    pipeStdout = loop->resource<uvw::PipeHandle>();
    pipeStderr = loop->resource<uvw::PipeHandle>();

    processExe->stdio(static_cast<uvw::FileHandle>(0),
                      uvw::ProcessHandle::StdIO::IGNORE_STREAM);
    uv_stdio_flags f = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    uvw::Flags<uvw::ProcessHandle::StdIO> ff(f);
    processExe->stdio(*pipeStdout, ff);
    processExe->stdio(*pipeStderr, ff);

    pipeStdout->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
    {
        stdoutBuf.append(ev.data.get(), ev.length);
        flushStreamBuffer(stdoutBuf, false);
    });
    pipeStderr->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
    {
        stderrBuf.append(ev.data.get(), ev.length);
        flushStreamBuffer(stderrBuf, true);
    });
    auto cleanup_pipe = [](const auto &, auto &cl) { cl.close(); };
    pipeStdout->once<uvw::EndEvent>(cleanup_pipe);
    pipeStderr->once<uvw::EndEvent>(cleanup_pipe);
    pipeStdout->once<uvw::ErrorEvent>([](const auto &, auto &cl) { cl.stop(); });
    pipeStderr->once<uvw::ErrorEvent>([](const auto &, auto &cl) { cl.stop(); });

    processExe->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &)
    {
        cInfoDom("mcp") << "sidecar exited (status=" << ev.status << ")";
        processExe->close();
        if (!shuttingDown)
            scheduleRestart();
    });
    processExe->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &)
    {
        cErrorDom("mcp") << "sidecar spawn error: " << ev.what();
        processExe->close();
        if (!shuttingDown)
            scheduleRestart();
    });

    auto inheritEnv = [](const std::string &var)
    {
        const char *v = std::getenv(var.c_str());
        return var + "=" + (v ? std::string(v) : std::string());
    };

    // Resolve the *actual* config directory in use. Utils::getConfigPath()
    // re-derives the path from $HOME and ignores a --config override, whereas
    // getConfigFile() honours the _configBase set by initConfigOptions(). We
    // strip the trailing component to obtain the directory the sidecar must
    // read local_config.xml from.
    std::string configDir = Utils::getConfigFile("");
    if (!configDir.empty() && configDir.back() == '/')
        configDir.pop_back();

    std::vector<std::string> envVars = {
        "CALAOS_CACHE_PATH=" + Utils::getCachePath(),
        "CALAOS_CONFIG_PATH=" + configDir,
        "CALAOS_LOG_LEVEL=" + Utils::get_config_option("debug_level"),
        "CALAOS_LOG_DOMAINS=" + Utils::get_config_option("debug_domains"),
        "CALAOS_MCP_SOCKET=" + sockPath,
        "CALAOS_API_URL=ws://127.0.0.1:" + Utils::to_string(jsonApiPort()) + "/api",
        inheritEnv("PATH"),
        inheritEnv("HOME"),
        inheritEnv("LANG"),
        inheritEnv("LC_ALL"),
        inheritEnv("LANGUAGE"),
        inheritEnv("LD_LIBRARY_PATH"),
        inheritEnv("PWD"),
    };

    std::vector<std::string> argVec = { exe };
    Utils::CStrArray env(envVars);
    Utils::CStrArray args(argVec);

    cInfoDom("mcp") << "spawning sidecar: " << exe;
    processExe->spawn(args.at(0), args.data(), env.data());

    pipeStdout->read();
    pipeStderr->read();

    // Reset the restart back-off counter once we successfully spawned —
    // libuv will deliver an ErrorEvent here synchronously if spawn() fails,
    // so reaching this line means the process started.
    restartAttempts = 0;
}

void McpServerManager::scheduleRestart()
{
    if (shuttingDown)
        return;

    static const double delays[] = { 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 };
    int idx = restartAttempts;
    if (idx >= static_cast<int>(sizeof(delays) / sizeof(delays[0])))
        idx = sizeof(delays) / sizeof(delays[0]) - 1;
    double delay = delays[idx];
    restartAttempts++;

    cWarningDom("mcp") << "scheduling sidecar restart in " << delay << "s";
    Timer::singleShot(delay, [this]() { spawnSidecar(); });
}

void McpServerManager::flushStreamBuffer(std::string &buf, bool toStderr)
{
    auto pos = buf.find_first_of('\n');
    while (pos != std::string::npos)
    {
        std::string line = buf.substr(0, pos);
        buf.erase(0, pos + 1);
        // Avoid echoing 64-hex blocks (potential token leak if the sidecar
        // ever logs one by accident). Mitigation S1.
        for (std::size_t i = 0; i + 64 <= line.size(); ++i)
        {
            bool allHex = true;
            for (std::size_t j = 0; j < 64; ++j)
            {
                char c = line[i + j];
                if (!((c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F')))
                {
                    allHex = false;
                    break;
                }
            }
            if (allHex)
            {
                line.replace(i, 64, "[redacted-hex64]");
                break;
            }
        }
        if (toStderr)
            cErrorDom("mcp") << line;
        else
            cInfoDom("mcp") << line;
        pos = buf.find_first_of('\n');
    }
}

}
