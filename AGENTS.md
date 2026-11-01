# AGENTS.md

This file provides guidance to LLM Agents when working with code in this repository.

## Documentation

Detailed technical documentation for each subsystem is in [`docs/`](docs/README.md). It is written in French, and most claims carry a `File.cpp:line` reference: line numbers drift, re-read the code before trusting one.

| Doc | Subsystem |
|---|---|
| [docs/00_overview.md](docs/00_overview.md) | Global architecture, startup flow, dependencies |
| [docs/01_core_data_model.md](docs/01_core_data_model.md) | IOBase, Room, ListeRoom, IOFactory, Params, Device |
| [docs/02_io_drivers.md](docs/02_io_drivers.md) | All IO drivers: Wago, KNX, MQTT, GPIO, Hue… |
| [docs/03_rules_engine.md](docs/03_rules_engine.md) | Rules, Conditions, Actions |
| [docs/04_scenarios.md](docs/04_scenarios.md) | AutoScenario, time ranges, timers |
| [docs/05_audio.md](docs/05_audio.md) | AudioPlayer (Squeezebox, Roon), AVReceiver (Denon, Yamaha…) |
| [docs/06_ipcam.md](docs/06_ipcam.md) | IP cameras (IPCam, Axis, Foscam, Synology…) |
| [docs/07_remoteui.md](docs/07_remoteui.md) | Embedded RemoteUI devices, WebSocket, HMAC auth, OTA |
| [docs/08_http_api.md](docs/08_http_api.md) | JSON HTTP/WebSocket API, real-time events, admin messages |
| [docs/09_lua_scripting.md](docs/09_lua_scripting.md) | Lua engine, calaos:* API, script examples |
| [docs/10_events_notifications.md](docs/10_events_notifications.md) | EventManager, push notifications, email, HistLogger |
| [docs/11_config_persistence.md](docs/11_config_persistence.md) | local_config.xml, rules.xml, state cache |
| [docs/12_extern_proc.md](docs/12_extern_proc.md) | ExternProc IPC framework (C++ subprocess drivers) |
| [docs/13_utility_lib.md](docs/13_utility_lib.md) | Utils, Params, Timer, Logger, bundled libraries |
| [docs/14_python_extern_proc.md](docs/14_python_extern_proc.md) | Python drivers: calaos_extern_proc lib, Reolink, Roon |
| [docs/15_mcp_server.md](docs/15_mcp_server.md) | MCP sidecar: calaos_mcp Python process, reverse proxy, tools, auth |
| [docs/16_config_options.md](docs/16_config_options.md) | Reference of every `local_config.xml` option, **generated** by `calaos_config options --markdown`: never edit by hand |
| [docs/17_device_templates.md](docs/17_device_templates.md) | Device templates for the installer wizard, `calaos_server --gentemplates` |
| [docs/18_stack_notes.md](docs/18_stack_notes.md) | Cross-repo stack notes: IODoc chain, translations, wire format, rules, ESPHome/MQTT, workflow pitfalls |

Other material under `docs/`, not part of the numbered reference:

- Design plans, **not implemented** unless the code says otherwise (each states its status on its first lines): `docs/matter/PLAN.md`, `docs/connections/PLAN.md`, `docs/autoscenario/PLAN.md`, `docs/climate/PROGRAM_PLAN.md`, `docs/remote/MLSH_STUDY.md`. `docs/admin/PLAN.md` still says "not started" but the admin messages exist in the code (`JsonApiAdmin.cpp`, `AdminState.cpp`, `DaemonClient.cpp`, documented in docs/08): trust the code.
- `docs/devices/` (board, per-ticket files, `UI_CLIMATE.md`) tracks the devices/climate work; the design is `CALAOS_DEVICES_FEAT.md` at the repository root (its "not yet implemented" status line is outdated: `IO/Device.cpp`, `IO/InternalThermostat.cpp`, `IO/ProxyClimate.cpp` exist).
- `docs/refactoring/` is a pile of dated records (one file per ticket, `BOARD.md`, `DECISIONS.md`, a 1 MB `ORCHESTRATION.md`). They describe the tree as it stood at their own commit and are never rewritten: grep them, do not read them whole and do not edit them.

`python3 -I scripts/check-docs.py .` (also `make check-docs`) verifies that the `File.cpp:line` references of `docs/*.md` still designate the lines pinned in `scripts/check-docs.pins`. It is deliberately non-blocking and not part of `make check`; the tree already reports hundreds of drifted references, so compare its summary before and after your change instead of expecting zero. After a reviewed documentation change, `make check-docs-pins` rewrites the pin file: the diff is the review, do not regenerate it blindly.

---

## Project Overview

Calaos Server is a home automation daemon that controls lights, shutters, sensors, thermostats, music, cameras, and more through a JSON/WebSocket API. It reads configuration from three XML files and runs an event-driven loop based on libuv. It is the server of the Calaos suite; its clients (Calaos Home / calaos_mobile, calaos_installer, calaos_remote_ui panels, the web app) and the Calaos OS daemon it talks to (calaos-container) live in other repositories.

The main binary is `calaos_server`. Several drivers run as **separate subprocesses** (C++ or Python) that communicate with the server over Unix sockets using the `ExternProc` IPC framework. Subprocess drivers: Wago (`calaos_wago`), KNX (`calaos_knx`), MQTT (`calaos_mqtt`), OneWire (`calaos_1wire`), OLA (`calaos_ola`), Lua scripts (`calaos_script`), Reolink (`calaos_reolink`, Python), Roon (`calaos_roon`, Python). KNX, MQTT and OLA are built only when their library is found, Reolink and Roon only when their Python module imports (see Key Dependencies).

An additional Python subprocess, `calaos_mcp`, exposes the home to LLM clients (Claude Desktop, etc.) via the **Model Context Protocol**. Unlike the ExternProc drivers it does *not* use the binary IPC framing: it runs an MCPServer/uvicorn HTTP server on a Unix domain socket and is reverse-proxied through the `/mcp` path of the main HTTP port (5454). See [docs/15_mcp_server.md](docs/15_mcp_server.md).

Other binaries of this repository (`src/bin/tools/`): `calaos_config` (CLI and terminal UI over `local_config.xml`, also the generator of docs/16), `calaos_mail` (sends one notification mail, spawned by the server), `wago_test`, and the `calaos_picture` script (ImageMagick resize for `get_cover` and `get_camera_pic`; `get_picture` with `width` resizes in-process with libjpeg-turbo).

Ports (`src/lib/Constants.h`): 5454/tcp JSON API (`JSONAPI_PORT`, overridable with `port_api`), 4545/udp discovery (`BCAST_UDP_PORT`), 4646/udp Wago/KNX input events (`WAGO_LISTEN_PORT`). `-noudp` disables both UDP servers. The server speaks plain HTTP: TLS is the job of a reverse proxy.

---

## Build Commands

```bash
# Bootstrap the build system (first time or after configure.ac changes).
# This only runs libtoolize + autoreconf: it does not run ./configure.
./autogen.sh

# Configure and build
./configure && make -j$(nproc)

# Install
sudo make install

# Run tests (requires gtest to be installed)
make check

# Optional hardware support (OneWire/KNX/MQTT/OLA) is autodetected:
# install the dev packages (owfs, knxd/eibclient, libmosquitto, libola)
# before running ./configure
```

The project uses GNU Autotools. Optional hardware drivers are enabled by autodetection at configure time (`AC_CHECK_HEADERS`/`PKG_CHECK_MODULES`), not by `--with-*` flags — check the "Hardware support" and "Python support" lines in the configure summary. The only switches are `--without-mcp` and `--enable-asan`.

An out-of-tree build directory named `_build/` at the repository root is recognised by the `tests/check-*.sh` scripts when run by hand. The `version` file contains `git`, so a local build reports that version; the Docker build replaces it with the release number.

How CI builds (`.github/workflows/ci.yml`), worth copying when you need a full run: `make -j$(nproc)`, then `make -j$(nproc) check TESTS=` (compiles every test program without running it), then a serial `make check`. Do not run `make -j all check` in one call: both recursions race on `src/`.

To work on one test, build only its program (`make -C tests core/CoreSmoke_test`) and run the binary with `--gtest_filter=…`. `make check TESTS=<name>` still compiles every `check_PROGRAMS` entry first.

### AddressSanitizer build (`--enable-asan`)

```bash
./autogen.sh
./configure --enable-asan
make -j$(nproc)
ASAN_OPTIONS=detect_leaks=0 make check
```

`--enable-asan` appends `-fsanitize=address -fno-omit-frame-pointer -g -O1` to `CFLAGS`, `CXXFLAGS` **and** `LDFLAGS`. It is off by default and a plain `./configure` is bit-for-bit unaffected. The configure summary reports its state on the "AddressSanitizer" line.

`ASAN_OPTIONS=detect_leaks=0` disables LeakSanitizer: the server intentionally never frees a number of process-lifetime singletons, so the leak checker is pure noise here. The memory-error detection (use-after-free, buffer overflow, use-after-scope) — the part that validates the ownership refactoring — stays on. Use a dedicated build tree (`--enable-asan` is not meant to be mixed with a release build) and never ship an ASan binary.

---

## Code Architecture

### Entry Point
`src/bin/calaos_server/main.cpp` initializes the system, loads XML config, and starts the libuv event loop.

Startup sequence:
1. Parse the command line (`--config`, `--cache`, `--gendoc <dir>`, `--gentemplates <dir>`, `-noudp`, `-v`, `-h`), then `Utils::initConfigOptions()`, which throws if the config or cache directory is not writable, and `ConfigOptions::purgeObsolete()`. `--gendoc` / `--gentemplates` write their output and exit here.
2. Singletons are touched in a fixed order so that destructors run in the reverse one: `HistLogger`, `AVRManager`, `EventManager`, `ListeRule`, `ListeRoom`, `RemoteUIManager`, `OtaFirmwareManager`.
3. `Config::LoadConfigIO()` — parses `io.xml`, instantiates IOs via `IOFactory`, fills `ListeRoom`; `Config::LoadConfigRule()` — parses `rules.xml`, instantiates `Rule`/`Condition`/`Action` objects; `DataLogger::Instance()`
4. `UDPServer(BCAST_UDP_PORT)` (discovery), unless `-noudp`
5. `HttpServer::Instance(port)` — starts the HTTP/WebSocket server (`JSONAPI_PORT` = **5454** by default, overridable via the `port_api` config option, bind address via `listen_address`)
6. `McpServerManager::Instance().start()` — spawns the `calaos_mcp` sidecar (no-op if the wrapper is not installed)
7. `DaemonClient::Instance().configure()` and `AdminState::Instance().start()` — poll calaos-container (`http://127.0.0.1:8000`) for the admin password state; only meaningful on Calaos OS
8. `UDPServer(WAGO_LISTEN_PORT)`, unless `-noudp`
9. `StartReadRules` counts the IOs still doing their first read; when the count reaches zero it calls `ListeRule::ExecuteStartRules()`, which fires rules with `ConditionStart` once
10. libuv event loop runs until SIGTERM / SIGINT / SIGHUP; a 0.1 s timer runs `ListeRule::RunEventLoop`, a 5 s timer feeds the watchdog

### IO System (`src/bin/calaos_server/IO/`)
Every controllable device is an `IOBase` subclass. IOs self-register via a factory macro placed at file scope in the `.cpp`. The first argument is a bare token (not a string): it becomes the `type` name used in `io.xml`.

```cpp
REGISTER_IO(WagoOutputLight)                       // XML type == class name
REGISTER_IO_USERTYPE(WagoOutputLight, WODigital)   // XML type WagoOutputLight, class WODigital
```

`IOFactory` reads the `type` attribute from `io.xml` and instantiates the matching class. Subdirectories group IOs by protocol:

| Directory | Protocol |
|---|---|
| `IO/Wago/` | Modbus TCP (Wago 750 PLCs) — ExternProc |
| `IO/KNX/` | KNX bus via knxd — ExternProc |
| `IO/Mqtt/` | MQTT — ExternProc |
| `IO/Gpio/` | Linux GPIO sysfs |
| `IO/Hue/` | Philips Hue REST API (RGB light only) |
| `IO/LAN/` | Ping / Wake-On-LAN |
| `IO/OLA/` | DMX512 via OLA — ExternProc |
| `IO/OneWire/` | 1-Wire via owfs or the kernel w1 interface — ExternProc |
| `IO/RemoteUI/` | IOs hosted on embedded RemoteUI devices |
| `IO/Reolink/` | Reolink cameras — Python ExternProc |
| `IO/Web/` | HTTP-based IOs |

`IO/` itself holds the protocol-independent types (inputs, outputs, shutters, timers, internal variables, `Scenario`, `InPlageHoraire`).

**Devices and climate.** A `<calaos:device>` element in `io.xml` groups the IOs of one appliance (`IO/Device.{h,cpp}`); implementations register with `REGISTER_DEVICE(impl, Class)` (`InternalThermostat`, `ProxyClimate`, both on `ClimateBase`). `IOChangeHub` gives devices synchronous per-IO change subscriptions. `DeviceTemplates.cpp` produces the templates used by the installer wizard. See docs/01 and docs/17.

**IODoc.** Each IO class documents its parameters and commands through `IODoc` (strings marked with `_()`); `calaos_server --gendoc` turns them into `io_doc.json` / `io_doc.md`, consumed by calaos_installer, by the MCP sidecar, and checked by CI.

### Rules Engine (`src/bin/calaos_server/Rules/`)
Each `Rule` holds a list of `Condition` objects (all must be true) and a list of `Action` objects (executed when conditions pass).

Condition types: `ConditionStd` (compare IO value), `ConditionOutput` (compare output state), `ConditionStart` (once at boot), `ConditionScript` (Lua).

Action types: `ActionStd` (set IO value), `ActionMail`, `ActionPush`, `ActionScript` (Lua), `ActionTouchscreen`.

A rule referencing a deleted IO is kept in `rules.xml` but disabled (never evaluated).

### ExternProc IPC (`src/bin/calaos_server/IO/ExternProc.h`)
Framework for subprocess drivers. The server side uses `ExternProcServer`; subprocess drivers (C++ or Python) use `ExternProcClient`. Communication is via Unix socket (the server starts the child with `--socket <path> --namespace <name>`) with a simple binary framing: 1 opcode byte (`0x21`), a 4-byte big-endian length (capped at 4 MiB), then a JSON payload. Child binaries are looked up in the directory given by `Prefix` (`CALAOS_BIN_PREFIX` overrides the compiled-in bin dir, `CALAOS_LIB_PREFIX` and `CALAOS_DATA_PREFIX` likewise). Python drivers use the `calaos_extern_proc` package (`src/lib/calaos-python/`).

### Event System (`src/bin/calaos_server/EventManager.cpp`)
Central pub/sub using sigc++ signals. When an IO changes state it calls `EmitSignalIO()` → `ListeRule::ExecuteRuleSignal(id)` (triggers rules and `IOChangeHub` subscribers) and `DataLogger::log()`; the IO subclasses themselves publish `EventManager::create(EventIOChanged, ...)` (notifies WebSocket clients). See `docs/10_events_notifications.md`.

### HTTP / WebSocket API (`src/bin/calaos_server/HttpServer.cpp`, `JsonApi.cpp`)
Singleton `HttpServer` listens on port 5454 (`JSONAPI_PORT`). Each connection becomes a `WebSocket` which delegates to `JsonApiHandlerHttp` (single request) or `JsonApiHandlerWS` (persistent connection with real-time event push). `WebSocket::ProcessData` also sniffs the first request line: requests targeting `/mcp` are handed to `McpProxyHandler` instead (see MCP section below), and `/api/v3/remote_ui/ws` goes to the RemoteUI handler. `JsonApiAdmin.cpp` implements the `admin_unlock` / `admin_set` / `admin` messages by forwarding to calaos-container. HTTP and WebSocket do not use the same request shape (arguments at the root vs under `msg.data`) and every scalar on the wire is a JSON string: read docs/08 before touching it. The characterization tests in `tests/core/JsonApi*` pin the wire bytes.

### MCP Sidecar (`src/bin/calaos_server/McpServerManager.{h,cpp}`, `McpProxyHandler.{h,cpp}`, `src/bin/calaos_mcp/`)
`McpServerManager` (singleton) spawns the `calaos_mcp` Python sidecar at boot via `uvw::ProcessHandle`, auto-generates the `mcp_token` and `mcp_service_token` in `local_config.xml`, and supervises it (restart with backoff, SIGTERM on shutdown). The sidecar listens on a Unix domain socket (`$CALAOS_CACHE_PATH/mcp.sock`, mode 0660). `McpProxyHandler` splices `/mcp/*` TCP connections to that socket as raw bytes, so the existing HTTPS-terminating reverse proxy (haproxy on Calaos OS) covers MCP without any extra port. The sidecar authenticates back to the JsonApi over WebSocket using `login_service` (a scope-restricted session, `serviceScope=true`) rather than admin credentials. Its tools (`src/bin/calaos_mcp/python/calaos_mcp/tools/`): `list_ios`, `find_io`, `get_io_state`, `set_io_state`, `list_rooms`, `get_room`, `list_scenarios`, `run_scenario`, `audio_control`. See `docs/15_mcp_server.md`.

### RemoteUI (`src/bin/calaos_server/RemoteUI/`)
Manages embedded devices (ESP32-S3 wall panels, etc.) that connect via WebSocket with HMAC-SHA256 authentication. Handles IO state synchronization, command dispatch, and OTA firmware updates. See `docs/07_remoteui.md`.

### Audio (`src/bin/calaos_server/Audio/`)
`AudioPlayer` subclasses: `Squeezebox` (Logitech Media Server), `RoonPlayer` (Python ExternProc). AV receivers (`AVReceiver` subclasses): Denon, Marantz, Onkyo, Pioneer, Rose, Yamaha. See `docs/05_audio.md`.

### IP Cameras (`src/bin/calaos_server/IPCam/`)
`IPCam` base class with implementations for Axis, Foscam, Planet, StandardMjpeg, SynoSurveillanceStation. See `docs/06_ipcam.md`.

### Lua Scripting (`src/bin/calaos_server/LuaScript/`)
Scripts never run inside `calaos_server`: `ScriptExec::ExecuteScriptDetached()` starts the `calaos_script` subprocess (`ScriptExtern_main.cpp`, via ExternProc), which runs the script in a whitelisted LuaJIT environment with a 5 s execution guard. `Lua_Calaos` (via Lunar, in `ScriptBindings.cpp`) exposes `calaos:getIOValue()`, `calaos:setIOValue()`, `calaos:getIOParam()`, `calaos:setIOParam()`, `calaos:waitForIO()`, `calaos:requestUrl()`, `calaos:sendPushNotif()`, `calaos:getEnv()` (plus the aliases `getInputValue` / `getOutputValue` / `setOutputValue`). There is no `calaos:get_io()`. See `docs/09_lua_scripting.md`.

### Config & Persistence (`src/bin/calaos_server/CalaosConfig.h`, `src/lib/ConfigStore.cpp`)
`Config` singleton loads/saves `io.xml` (IOs) and `rules.xml` (rules). `local_config.xml` is *not*
owned by `Config`: it is read/written through free functions in namespace `Utils` (`get_config_option[s]`,
`set_config_option[s]`, `del_config_option`, in `ConfigStore.cpp`), shared by `calaos_server`, `calaos_config`,
`calaos_mail` and Calaos Home — see `docs/16_config_options.md` for the option reference; the registry of known
options is `src/lib/ConfigOptions.cpp`. Last known IO values are persisted in `<cache>/iostates.cache` (a **JSON**
file written by `Config`, not a database). The event log and the push tokens live in `<cache>/events.db`
(**SQLite**, `HistLogger`, via `sqlite_modern_cpp`). See `docs/11_config_persistence.md`.

### Common Library (`src/lib/`)
`Utils` (split into `Constants.h`, `MemMacros.h`, `LogSetup`, `StringUtils`, `ConfigStore`, `SystemInfo`), `Logger`, `Params` (string→string map), `Timer` (libuv-based), `ColorUtils`, `FileUtils`, `UrlDownloader` (libcurl async), `ExpressionEvaluator` (exprtk), `TimeRange`/`sunset` (time ranges, sunrise/sunset). Bundled third-party libs: nlohmann/json (`json.hpp`), pugixml (XML DOM + XPath 1.0, used only when the distro package is absent), sole (UUID), sqlite_modern_cpp, llhttp, exprtk, libquickmail, uvw, uri_parser, cpptui. See `docs/13_utility_lib.md`.

---

## Configuration Files

| File | Constant | Content |
|---|---|---|
| `io.xml` | `IO_CONFIG` | Rooms and IO declarations (type, id, name, protocol params) |
| `rules.xml` | `RULES_CONFIG` | Automation rules (conditions + actions) |
| `local_config.xml` | `LOCAL_CONFIG` | Server settings: credentials, SMTP, InfluxDB, misc options — see `docs/16_config_options.md` |

Config directory resolution (`Utils::getConfigPath()`, `src/lib/ConfigStore.cpp`): `--config <dir>` (which sets `_configBase` and the `CALAOS_CONFIG` env var), else `$CALAOS_CONFIG`, else the first of `~/.config/calaos`, `/etc/calaos`, `<sysconfdir>/calaos` that contains an `io.xml`; if none does, `~/.config/calaos` is created and used. Cache path: `~/.cache/calaos`, overridable with `--cache <dir>`. Constants are defined in `src/lib/Constants.h`.

**Important:** the `type` attribute in `io.xml` must exactly match a registered `IOFactory` key (case-insensitive). Unknown types are silently skipped at load time.

**Config path gotchas (learned the hard way):**
- `Utils::getConfigFile(name)` honours `--config` / `CALAOS_CONFIG` (it reads `_configBase`); `Utils::getConfigPath()` does **not** — it re-derives the directory from `$HOME` every time. When you need the *active* config directory (e.g. to pass to a subprocess), use `getConfigFile("")` and strip the trailing slash, not `getConfigPath()`. `getCachePath()` does respect `--cache`.
- `local_config.xml` options are namespaced elements: `<calaos:option name="..." value="..."/>` with `xmlns:calaos="http://www.calaos.fr"`. A Python `xml.etree` parser sees the tag as `{http://www.calaos.fr}option`, so `root.iter("option")` matches nothing — match on the local name after the `}` instead.
- Every key read with `get_config_option("x")` must be declared in `src/lib/ConfigOptions.cpp`, then `docs/16_config_options.md` regenerated (`calaos_config options --markdown > docs/16_config_options.md`). `tests/check-config-options.sh` and `tests/check-config-docs.sh` fail otherwise.
- MCP keys in `local_config.xml`: `mcp_token` (Bearer token for MCP clients) and `mcp_service_token` (sidecar→JsonApi service login). Both are auto-generated (64 hex chars, OpenSSL `RAND_bytes`) on first boot if absent.
- `local_config.xml` is shared: `calaos_server`, `calaos_config`, `calaos_mail` (spawned per notification mail) and Calaos Home (outside this repo) all read/write it. Writes from the three processes in this repo go through `Utils`, which serialises them with an `flock()` on a companion `local_config.xml.lock` and writes atomically (temp file + `fsync` + `rename`, see `docs/11_config_persistence.md`). Calaos Home does not go through `Utils` and does not take that lock.
- Push notification tokens (`push_tokens`) live in the SQLite history database (`src/bin/calaos_server/HistLogger.cpp`), **not** in `local_config.xml`.
- The default API credentials are `cn_user=user` / `cn_pass=pass`.

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

- **Formatting**: `.clang-format` encodes the rules above. CI runs `clang-format-diff` on the lines a pull request changes (not whole files) and excludes the bundled trees of `src/lib`; do not reformat code you are not otherwise touching.
- **Namespace**: all server-side business logic is in namespace `Calaos`
- **Singletons**: expose a static `Instance()` method
- **IO IDs**: unique string, used as the `id` param in `io.xml`. When none is given, `Calaos::get_new_id(prefix)` picks `prefix` plus the first free counter (`io_0`, `io_1`…). Not a UUID.
- **Comments**: English only, clear and concise; explain the why, not the what
- **Error handling**: always log errors with contextual information using `cErrorDom("domain") << "message"` (`cInfoDom`, `cWarningDom`, `cDebugDom`… in `src/lib/LogSetup.h`)
- **Python** (subprocess drivers): follow the same domain-based logging via `calaos_extern_proc.logger`
- **Translations**: user-visible and IODoc strings use `_()` (gettext, catalogues in `po/`). A driver source under `src/bin/calaos_server/IO` that marks strings must be listed in `po/POTFILES.in` (`tests/check-potfiles-devices.sh`).
- **New files**: every file under `src/` or `tests/` must be named in the matching `Makefile.am` (`_SOURCES`, `EXTRA_DIST`…), or `tests/check-dist-coverage.sh` fails.

---

## Tests

Tests live in `tests/` and use Google Test. Built and run with `make check`. The GoogleTest programs are built only if gtest is detected at configure time; the shell and Python checks below run regardless.

- `tests/*_test.cpp`: unit tests of the common library, drivers' wire formats and IO classes (about 55 programs: `ColorValue`, `ExpressionEvaluator`, `UrlDownloader*`, `TimeRangeCalendar`, `Wago*`, `Mqtt*`, `KNX*`, `OLAWire`, `Squeezebox*`, `Reolink*`, `RemoteUIHmac`, `WebSocketFrame`, `ConfigModel`/`ConfigOptions` (the calaos_config TUI), …).
- `tests/core/*_test.cpp`: tests of the server core on top of the shared fixture `CalaosCoreFixture.h` (private config/cache directory, `loadConfig(ioXml, rulesXml)`, singleton reset): IO/Room/Rule lifecycle, ListeRoom, auto-scenarios, climate and thermostats, ExternProc supervision, the JSON API (many suites pin exact wire bytes against `tests/core/golden/`), RemoteUI, HistLogger, push, UDP server. The whole `tests/` tree holds about 200 GoogleTest programs and 3000 cases.
- `tests/check-*.sh` (automake simple tests, exit 77 = SKIP): config option registry and generated docs, translation file lists, `--gentemplates` / `--gendoc` generation, `make dist` file coverage (`check-extra-dist`, `check-dist-coverage`), test dependency declarations (`check-test-deps`), the diff-coverage script self-test, and a few meta-checks that keep other test families honest (order sentinels, echo ceilings, ccache honesty, Python dependency conformance).
- `tests/python/` (pytest): the MCP sidecar, `calaos_extern_proc`, the Reolink and Roon drivers, run through `tests/run-python-tests.sh`, which counts declared against executed cases and prints `run-python-tests: suites=… cases=…`.
- `tests/websockets/`: Autobahn fuzzing scripts, run by hand, not by `make check`.

Adding a GoogleTest program: copy a recent block of `tests/Makefile.am` (`TESTS +=`, `check_PROGRAMS +=`, `_SOURCES`, `_LDADD`, and a `_DEPENDENCIES` that lists every `calaos_server` object the program links — `tests/check-test-deps.sh` refuses a stale-prone block), inside the `if HAVE_GTEST` conditional. Write the test so that it fails without your fix.

---

## Key Dependencies

**Required** (checked by `configure.ac`): C++20 compiler (`AX_CXX_COMPILE_STDCXX_20`, GCC 12 on Debian 12 in CI and the images), `libuv >= 1.19.0`, `libcurl >= 7.20.0`, `sigc++-2.0 >= 2.4.1`, `luajit`, `sqlite3`, `openssl >= 1.1.0`, `pthread`. `pugixml >= 1.10` is used when pkg-config finds it, otherwise the copy in `src/lib/pugixml` is built. `jansson` is no longer used. For the build tools: `autoconf automake libtool autopoint gettext pkg-config`.

**Optional** (autodetected at configure time if the dev package is present):

| Library | Enables |
|---|---|
| owfs (`owcapi.h`) | 1-Wire through owfs (`calaos_1wire` is always built; without owfs it only reads the kernel w1 interface) |
| knxd (`eibclient.h`) | KNX bus (`calaos_knx`) |
| libmosquitto (`mosquitto.h`) | MQTT broker (`calaos_mqtt`; the code also links `libmosquittopp`) |
| libola (pkg-config) | DMX512 lighting (`calaos_ola`) |
| libjpeg-turbo (`jpeglib.h`) | camera thumbnails of `get_picture`; without it, full-resolution frames and one warning |
| gtest (`gtest/gtest.h`) | the GoogleTest programs of `make check` |
| `convert` (ImageMagick) | runtime tool of `calaos_picture` |

**Python drivers** (installed separately; `configure` imports the module to decide): `reolink_aio` (Reolink), `roonapi` (Roon); `colorama` (logging of `calaos_extern_proc`)

**MCP sidecar** (enabled by default, disable with `./configure --without-mcp`; needs Python >= 3.11): its Python dependencies are declared in **`src/bin/calaos_mcp/pyproject.toml`, which is the single source of truth** — the Dockerfile, the devcontainer and CI all install from it, never from a hand-written list. Install them with:

```
scripts/pyproject-requirements.py src/bin/calaos_mcp/pyproject.toml > req.txt
pip3 install -r req.txt --break-system-packages
```

Install the whole set in **one** pip invocation: `mcp`, `fastapi` and `starlette` are coupled, and resolving them package by package silently picks incompatible combinations.

Add `--extra test` to that command to also get what `tests/python/` needs (`pytest`, `httpx`, `colorama`). Without it a part of the declared Python cases cannot run and `make check` reports a SKIP; with `CALAOS_PYTHON_TESTS_REQUIRED=1` — what CI sets — that SKIP is a build failure instead. `CALAOS_PYDEPS_STRICT=1` does the same for `tests/check-pydeps-conformance.sh`, which compares the installed set with the pyproject.

`configure` then probes the **API the sidecar actually imports** (not just `import mcp`, which passes whatever the major version: mcp 2 renamed `mcp.server.fastmcp.FastMCP` to `mcp.server.mcpserver.MCPServer` and moved `transport_security` to `streamable_http_app()`) and sets `HAVE_PYTHON_MCP`; if the probe fails it prints the failing import and skips building the sidecar. `calaos_mcp --help` exercises the same import chain with no socket and no config — it is the quickest way to tell whether a built image can start the sidecar at all. See [T3.23](docs/refactoring/T3.23.md).

---

## Docker

A multi-stage `Dockerfile` is provided with `dev` (Debian 12 with every build dependency and the Python set), `builder` (`./autogen.sh && ./configure --prefix=/opt && make install-strip`) and `runner` (the shipped image: runtime libraries and `/opt`; no entrypoint, binaries in `/opt/bin`) targets. Images are published to `ghcr.io/calaos/calaos_base`. The web app is not in the image: on Calaos OS a host package is bind-mounted on `/opt/share/calaos/app`. `.devcontainer/` is a separate Debian 12 dev image (adds ccache, gdb, Claude Code, rtk; bind-mounts `~/.claude`, `~/.agents`, a shared ccache directory at `~/.cache/calaos-ccache`; runs privileged with host networking; forwards 5454). The `ccache` shims are only active when `/usr/lib/ccache` is first in `PATH`; the ccache directory is shared mutable state between agents and worktrees, do not reconfigure or zero it.

## CI and publishing

`.github/workflows/ci.yml` ("Build and Test") runs on pushes to any branch, on pull requests, monthly (cron) and by hand. Jobs: `build-and-test` (Debian 12, `make check` with `CALAOS_PYTHON_TESTS_REQUIRED=1`, `CALAOS_PYDEPS_STRICT=1`, then generates and uploads the `io-doc` artifact consumed by calaos_installer's CI — not from pull requests), `coverage` (pull request / schedule / dispatch only; on a pull request, `scripts/check-diff-coverage.py` requires every executable line the diff adds under `src/` to be run by `make check`, vendored trees excepted; excuse a line only with `LCOV_EXCL_LINE` / `LCOV_EXCL_START…STOP` and a reason), `format-check` (pull requests only), `mcp-sidecar-deps`, `workflow-gating`, `dependabot-config`.

Publishing (image, git tag, `build_deb` dispatch to calaos/pkgdebs) is manual only: Actions → **Publish** → Run workflow, input `channel` = `prerelease` | `patch` | `minor` | `major` (`prerelease` pushes the image tag `dev`, the others `latest`). Its `verify-ci` job refuses a ref other than `master` and a commit whose Build and Test run is not green. A push publishes nothing. `.github/check-workflow-gating.py` fails CI if a publishing workflow loses either property.

---

## Before you call a change done

- Build and run `make check` (a full run with the Python extras installed, so nothing is silently skipped); for code under `src/`, make sure the lines you add are exercised by a test, because the pull request gate counts them.
- A behaviour change comes with a test that fails without it. Prefer asserting exact observable output (wire bytes, file content) to asserting that something happened.
- Touching the option registry, an IO's IODoc, a driver's `_()` strings or the device templates: regenerate/update docs/16, `po/POTFILES.in` and the checks above.
- Touching documented behaviour: update the matching `docs/NN_*.md` and run `python3 -I scripts/check-docs.py .` to see which references you moved.
- Do not push, tag or run the Publish workflow without being asked: a push costs CI minutes, and publishing releases an image and a Debian package.
