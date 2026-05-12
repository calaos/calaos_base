# AGENTS.md

This file provides guidance to LLM Agents when working with code in this repository.

## Documentation

Detailed technical documentation for each subsystem is in [`doc/`](doc/README.md):

| Doc | Subsystem |
|---|---|
| [doc/00_overview.md](doc/00_overview.md) | Global architecture, startup flow, dependencies |
| [doc/01_core_data_model.md](doc/01_core_data_model.md) | IOBase, Room, ListeRoom, IOFactory, Params |
| [doc/02_io_drivers.md](doc/02_io_drivers.md) | All IO drivers: Wago, KNX, MQTT, GPIO, Hue, MySensors… |
| [doc/03_rules_engine.md](doc/03_rules_engine.md) | Rules, Conditions, Actions |
| [doc/04_scenarios.md](doc/04_scenarios.md) | AutoScenario, time ranges, timers |
| [doc/05_audio.md](doc/05_audio.md) | AudioPlayer (Squeezebox, Roon), AVReceiver (Denon, Yamaha…) |
| [doc/06_ipcam.md](doc/06_ipcam.md) | IP cameras (IPCam, Axis, Foscam, Synology…) |
| [doc/07_remoteui.md](doc/07_remoteui.md) | Embedded RemoteUI devices, WebSocket, HMAC auth, OTA |
| [doc/08_http_api.md](doc/08_http_api.md) | JSON HTTP/WebSocket API, real-time events |
| [doc/09_lua_scripting.md](doc/09_lua_scripting.md) | Lua engine, calaos:* API, script examples |
| [doc/10_events_notifications.md](doc/10_events_notifications.md) | EventManager, push notifications, email, HistLogger |
| [doc/11_config_persistence.md](doc/11_config_persistence.md) | local_config.xml, rules.xml, state cache |
| [doc/12_extern_proc.md](doc/12_extern_proc.md) | ExternProc IPC framework (C++ subprocess drivers) |
| [doc/13_utility_lib.md](doc/13_utility_lib.md) | Utils, Params, Timer, Logger, bundled libraries |
| [doc/14_python_extern_proc.md](doc/14_python_extern_proc.md) | Python drivers: calaos_extern_proc lib, Reolink, Roon |

---

## Project Overview

Calaos Server is a home automation daemon that controls lights, shutters, sensors, music, cameras, and more through a JSON/WebSocket API. It reads configuration from three XML files and runs an event-driven loop based on libuv.

The main binary is `calaos_server`. Several drivers run as **separate subprocesses** (C++ or Python) that communicate with the server over Unix sockets using the `ExternProc` IPC framework. Subprocess drivers: Wago, KNX, MQTT, OneWire, OLA, Reolink (Python), Roon (Python), Lua scripts.

---

## Build Commands

```bash
# Bootstrap the build system (first time or after configure.ac changes)
./autogen.sh

# Configure and build
./configure && make

# Install
sudo make install

# Run tests (requires gtest to be installed)
make check

# Build with optional hardware support
./configure --with-owfs --with-knx --with-mqtt --with-ola
```

The project uses GNU Autotools. If dependencies are missing, check `configure.ac` for the optional `--with-*` flags.

---

## Code Architecture

### Entry Point
`src/bin/calaos_server/main.cpp` initializes the system, loads XML config, and starts the libuv event loop.

Startup sequence:
1. `Config::LoadConfigIO()` — parses `io.xml`, instantiates IOs via `IOFactory`, fills `ListeRoom`
2. `Config::LoadConfigRule()` — parses `rules.xml`, instantiates `Rule`/`Condition`/`Action` objects
3. `HttpServer::Instance(4444)` — starts the HTTP/WebSocket server
4. `ListeRule::ExecuteStartRules()` — fires rules with `ConditionStart` once
5. libuv event loop runs indefinitely

### IO System (`src/bin/calaos_server/IO/`)
Every controllable device is an `IOBase` subclass. IOs self-register via a factory macro placed at file scope in the `.cpp`:

```cpp
REGISTER_IO(WagoOutputLight)
REGISTER_IO_USERTYPE("WagoOutputLight", WODigital)  // XML type name differs from class name
```

`IOFactory` reads the `type` attribute from `local_config.xml` and instantiates the matching class. Subdirectories group IOs by protocol:

| Directory | Protocol |
|---|---|
| `IO/Wago/` | Modbus TCP (Wago 750 PLCs) — ExternProc |
| `IO/KNX/` | KNX bus via knxd — ExternProc |
| `IO/Mqtt/` | MQTT — ExternProc |
| `IO/Gpio/` | Linux GPIO sysfs |
| `IO/Hue/` | Philips Hue REST API |
| `IO/LAN/` | Ping / Wake-On-LAN |
| `IO/MySensors/` | MySensors RF network |
| `IO/OLA/` | DMX512 via OLA — ExternProc |
| `IO/OneWire/` | 1-Wire via owfs — ExternProc |
| `IO/RemoteUI/` | IOs hosted on embedded RemoteUI devices |
| `IO/Reolink/` | Reolink cameras — Python ExternProc |
| `IO/Web/` | HTTP-based IOs |

### Rules Engine (`src/bin/calaos_server/Rules/`)
Each `Rule` holds a list of `Condition` objects (all must be true) and a list of `Action` objects (executed when conditions pass).

Condition types: `ConditionStd` (compare IO value), `ConditionOutput` (compare output state), `ConditionStart` (once at boot), `ConditionScript` (Lua).

Action types: `ActionStd` (set IO value), `ActionMail`, `ActionPush`, `ActionScript` (Lua), `ActionTouchscreen`.

### ExternProc IPC (`src/bin/calaos_server/IO/ExternProc.h`)
Framework for subprocess drivers. The server side uses `ExternProcServer`; subprocess drivers (C++ or Python) use `ExternProcClient`. Communication is via Unix socket with a simple binary framing (type + size + JSON payload). Python drivers use the `calaos_extern_proc` package (`src/lib/calaos-python/`).

### Event System (`src/bin/calaos_server/EventManager.cpp`)
Central pub/sub using sigc++ signals. When an IO changes state it calls `EmitSignalIO()` → `ListeRule::ExecuteRuleSignal(id)` (triggers rules) and `EventManager::create(EventIOChanged, ...)` (notifies WebSocket clients). See `doc/10_events_notifications.md`.

### HTTP / WebSocket API (`src/bin/calaos_server/HttpServer.cpp`, `JsonApi.cpp`)
Singleton `HttpServer` listens on port 4444. Each connection becomes a `WebSocket` which delegates to `JsonApiHandlerHttp` (single request) or `JsonApiHandlerWS` (persistent connection with real-time event push). See `doc/08_http_api.md`.

### RemoteUI (`src/bin/calaos_server/RemoteUI/`)
Manages embedded devices (ESP32-S3 wall panels, etc.) that connect via WebSocket with HMAC-SHA256 authentication. Handles IO state synchronization, command dispatch, and OTA firmware updates. See `doc/07_remoteui.md`.

### Audio (`src/bin/calaos_server/Audio/`)
`AudioPlayer` subclasses: `Squeezebox` (Logitech Media Server), `RoonPlayer` (Python ExternProc). AV receivers (`AVReceiver` subclasses): Denon, Marantz, Onkyo, Pioneer, Rose, Yamaha. See `doc/05_audio.md`.

### IP Cameras (`src/bin/calaos_server/IPCam/`)
`IPCam` base class with implementations for Axis, Foscam, Gadspot, Planet, StandardMjpeg, SynoSurveillanceStation. See `doc/06_ipcam.md`.

### Lua Scripting (`src/bin/calaos_server/LuaScript/`)
Scripts run in an isolated subprocess (`ScriptExtern_main.cpp` via ExternProc). `ScriptManager` sends the script text to the subprocess; `Lua_Calaos` (via Lunar) exposes `calaos:get_io()`, `calaos:set_io()`, etc. See `doc/09_lua_scripting.md`.

### Config & Persistence (`src/bin/calaos_server/CalaosConfig.h`)
`Config` singleton loads/saves `local_config.xml` (IOs) and `rules.xml` (rules). A state cache (SQLite via `sqlite_modern_cpp`) persists last known IO values across restarts. See `doc/11_config_persistence.md`.

### Common Library (`src/lib/`)
`Utils`, `Logger`, `Params` (string→string map), `Timer` (libuv-based), `ColorUtils`, `FileUtils`, `UrlDownloader` (libcurl async), `ExpressionEvaluator` (exprtk), `NTPClock`. Bundled third-party libs: nlohmann/json, TinyXML+XPath, sole (UUID), sqlite_modern_cpp, llhttp, exprtk, libquickmail, uvw. See `doc/13_utility_lib.md`.

---

## Configuration Files

| File | Constant | Content |
|---|---|---|
| `io.xml` | `IO_CONFIG` | Rooms and IO declarations (type, id, name, protocol params) |
| `rules.xml` | `RULES_CONFIG` | Automation rules (conditions + actions) |
| `local_config.xml` | `LOCAL_CONFIG` | Server settings: credentials, SMTP, InfluxDB, NTP, push tokens, misc options |

Default path: `/etc/calaos/`. Override with env var `CALAOS_HOME` or `~/.config/calaos/` in development. Constants are defined in `src/lib/Utils.h`.

**Important:** the `type` attribute in `io.xml` must exactly match a registered `IOFactory` key (case-insensitive). Unknown types are silently skipped at load time.

---

## Coding Conventions

- **C++**: camelCase for variables/functions/methods, PascalCase for classes
- **C**: snake_case for variables/functions/methods, ALL_CAPS for `#define` constants
- **Braces**: opening `{` on a new line; single-line bodies omit braces; multi-line bodies always use braces
- **Constructor initialization lists**: `:` at end of the declaration line, each initializer on its own line with `,` at the end:

```cpp
MyClass::MyClass():
    MyBaseClass(a, b),
    member_1(3),
    member_2(nullptr)
{
}
```

- **Namespace**: all server-side business logic is in namespace `Calaos`
- **Singletons**: expose a static `Instance()` method
- **IO IDs**: unique string, format `id-<uuid4>`, generated by `get_new_id(prefix)`
- **Comments**: English only, clear and concise
- **Error handling**: always log errors with contextual information using `cErrorDom("domain") << "message"`
- **Python** (subprocess drivers): follow the same domain-based logging via `calaos_extern_proc.logger`

---

## Tests

Tests live in `tests/` and use Google Test. Built and run with `make check` (only if gtest is detected at configure time). Current coverage: `ColorValue`, `ExpressionEvaluator`, `WebSocketClient`.

---

## Key Dependencies

**Required:** `gcc > 5` (C++14), `libuv > 1.10`, `jansson > 2.5`, `libcurl > 7.20`, `luajit`, `sigc++ > 2.4`, `sqlite3`

**Optional** (enabled via `./configure --with-<name>`):

| Flag | Library | Enables |
|---|---|---|
| `--with-owfs` | owfs | 1-Wire sensors (OneWire driver) |
| `--with-knx` | knxd | KNX bus (KNX driver) |
| `--with-mqtt` | libmosquitto | MQTT broker (MQTT driver) |
| `--with-ola` | OLA | DMX512 lighting (OLA driver) |

**Python drivers** (installed separately): `reolink_aio` (Reolink), `roonapi` (Roon), `colorama` (logging)

---

## Docker

A multi-stage `Dockerfile` is provided with `dev`, `builder`, and `runner` targets. Images are published to `ghcr.io/calaos/calaos_base`.
