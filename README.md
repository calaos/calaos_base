# calaos_base

[![License](https://img.shields.io/badge/license-GPLv3%2B-blue.svg)](COPYING)
[![Build and Test](https://github.com/calaos/calaos_base/actions/workflows/ci.yml/badge.svg)](https://github.com/calaos/calaos_base/actions/workflows/ci.yml)

`calaos_base` is the server side of [Calaos](http://www.calaos.fr), an open-source
home automation suite. It builds `calaos_server`, a C++20 daemon that talks to your
devices (lights, shutters, sensors, thermostats, audio, cameras), runs the
automation rules and exposes everything to the user interfaces through a
JSON API over HTTP and WebSocket (port 5454).

## Where it sits in the Calaos suite

| Repository | Role |
|---|---|
| **calaos_base** (this one) | `calaos_server`, its driver subprocesses, `calaos_config`, `calaos_mail`, the `calaos_mcp` sidecar. Shipped as the container image `ghcr.io/calaos/calaos_base` |
| calaos-container | Go system daemon of Calaos OS; `calaos_server` asks it for the administration features (admin password, see [docs/08](docs/08_http_api.md)) |
| calaos_mobile | Calaos Home: Qt/QML mobile app and touchscreen kiosk |
| calaos_remote_ui | Firmware of the ESP32 wall panels (they connect to the server, see [docs/07](docs/07_remoteui.md)) |
| calaos_installer | Desktop tool to describe the installation (`io.xml`, `rules.xml`) |
| calaos-build | Calaos OS image builder |
| calaos_ddns | Dynamic DNS service |

## Features

Implemented in this repository:

- **Devices**: lights (on/off, dimmer, RGB), shutters (with estimated position),
  switches (single, long press, triple click), temperature and analog sensors,
  string and boolean/number variables, thermostats and climate devices
  (an internal thermostat, and a proxy to an external climate such as an ESPHome unit).
- **Protocols and hardware**: Wago 750 PLCs (Modbus TCP), KNX (through knxd),
  MQTT, 1-Wire (owfs or the kernel w1 interface), DMX512 (OLA), Linux GPIO (sysfs),
  Philips Hue (RGB lights), generic HTTP/web inputs and outputs, ping and Wake-on-LAN,
  Reolink cameras (events), ESP32 wall panels (RemoteUI, with OTA firmware updates).
- **Audio**: Squeezebox / Logitech Media Server, Roon, and A/V receivers
  (Denon, Marantz, Onkyo, Pioneer, Rose, Yamaha).
- **IP cameras**: Axis, Foscam, Planet, generic MJPEG, Synology Surveillance Station.
- **Automation**: rules (conditions and actions), auto-scenarios with time ranges and
  sunrise/sunset, timers, Lua scripts (LuaJIT, run in a sandboxed subprocess).
- **Notifications and history**: e-mail, mobile push, event log (SQLite), optional
  InfluxDB export.
- **MCP sidecar**: `calaos_mcp` exposes rooms, IOs, scenarios and audio to LLM clients
  through the Model Context Protocol, behind the `/mcp` path of the API port.
- **Tools**: `calaos_config` (command line and terminal UI for `local_config.xml`),
  `calaos_mail`, `calaos_picture`.

Designed but not implemented yet (plans, not features): Matter/Thread support, shared
connection definitions in `io.xml`, an auto-scenario editor in the UIs, climate time
programs, remote access through MLSH. See the plans under [docs/](docs/README.md).

Hardware drivers are optional at build time: KNX, MQTT, OLA and the Python integrations
(Roon, Reolink, MCP) are compiled or installed only when their dependencies are found.

## Building

The build system is GNU Autotools. A C++20 compiler is required (GCC 12 on Debian 12
is what CI uses).

Debian 12 packages for a build with the tests:

```sh
apt-get install build-essential pkg-config autoconf automake libtool autopoint gettext \
    libsigc++-2.0-dev libcurl4-openssl-dev libluajit2-5.1-dev libsqlite3-dev \
    libssl-dev libuv1-dev libpugixml-dev libgtest-dev python3 python3-pip
```

Required libraries (checked by `configure.ac`): libuv >= 1.19, libcurl >= 7.20,
sigc++-2.0 >= 2.4.1, luajit, sqlite3, openssl >= 1.1. pugixml >= 1.10 is used when
found, otherwise the bundled copy is built. Other libraries (uvw, llhttp, exprtk,
nlohmann/json...) are bundled in `src/lib/`.

Optional, autodetected by `./configure` (see the "Hardware support" and "Python support"
lines of its summary):

| Package | Enables |
|---|---|
| `libow-dev` (owfs) | 1-Wire through owfs |
| `knxd-dev` (`eibclient.h`) | `calaos_knx` |
| `libmosquitto-dev`, `libmosquittopp-dev` | `calaos_mqtt` |
| `libola-dev` | `calaos_ola` |
| `imagemagick` | picture resizing of camera snapshots (runtime, `convert`) |
| Python modules `roonapi`, `reolink_aio` | `calaos_roon`, `calaos_reolink` |
| the set of `src/bin/calaos_mcp/pyproject.toml` | the `calaos_mcp` sidecar (Python >= 3.11) |

```sh
./autogen.sh
./configure                 # --without-mcp to skip the sidecar, --enable-asan for a debug build
make -j"$(nproc)"
sudo make install
```

The MCP sidecar dependencies are pinned in `src/bin/calaos_mcp/pyproject.toml`; install
them in a single pip run, never package by package:

```sh
scripts/pyproject-requirements.py src/bin/calaos_mcp/pyproject.toml > req.txt
pip3 install -r req.txt --break-system-packages
```

### Docker

The `Dockerfile` has three stages: `dev` (Debian 12 with every build dependency and the
Python packages), `builder` (builds and installs into `/opt`) and `runner` (the shipped
image: runtime libraries plus `/opt`). `.devcontainer/` describes a Debian 12 dev container
(VS Code) that mounts a ccache directory and forwards port 5454.

```sh
docker build --target dev -t calaos_base:dev .          # build environment
docker build --build-arg APP_VERSION=dev -t calaos_base .  # runtime image
```

The runtime image has no entrypoint: the binaries are in `/opt/bin`. The web application is
not embedded; it is provided by the host (on Calaos OS, the `calaos-web-app` package is
bind-mounted on `/opt/share/calaos/app`).

## Running

```sh
calaos_server [--config <dir>] [--cache <dir>] [-noudp]
calaos_server --gendoc <dir>         # write the IO documentation (io_doc.json / io_doc.md) and exit
calaos_server --gentemplates <dir>   # write the device templates and exit
```

Configuration is three XML files in the configuration directory:

| File | Content |
|---|---|
| `io.xml` | rooms and IOs (devices) |
| `rules.xml` | automation rules |
| `local_config.xml` | server settings (API credentials, SMTP, InfluxDB, ports...), reference in [docs/16](docs/16_config_options.md) |

The directory is `--config`, or `$CALAOS_CONFIG`, or the first of `~/.config/calaos`,
`/etc/calaos`, `<prefix>/etc/calaos` that holds an `io.xml` (`~/.config/calaos` is created
if none does). The cache directory (last IO states, event database) defaults to
`~/.cache/calaos`. The files are normally written by `calaos_installer`; `calaos_config`
edits `local_config.xml` (`calaos_config list`, `get`, `set`, `describe`, or the
interactive browser when run without arguments).

Network ports:

| Port | Protocol | Use |
|---|---|---|
| 5454/tcp | HTTP and WebSocket | JSON API (`/api`), RemoteUI panels, `/mcp` reverse proxy to the sidecar; configurable with `port_api`, bind address with `listen_address` |
| 4545/udp | UDP | discovery of the server by clients on the LAN |
| 4646/udp | UDP | input events pushed by Wago and KNX gateways |

`calaos_server` serves plain HTTP; TLS is expected to be terminated by a reverse proxy
(as on Calaos OS). The default API credentials are `user` / `pass` (options `cn_user` and
`cn_pass`): change them before exposing the server.

## Tests

```sh
make check
```

`make check` builds and runs about 200 GoogleTest programs (`tests/`, `tests/core/`)
when gtest is installed, plus shell and Python checks (config option registry, generated
docs, translations, dist tarball coverage, the `tests/python/` suites for the sidecar and
the Python drivers). The Python suites need the `test` extra of the pyproject
(`scripts/pyproject-requirements.py --extra test ...`); without it they are reported as
skipped. `./configure --enable-asan` builds with AddressSanitizer (run with
`ASAN_OPTIONS=detect_leaks=0 make check`).

## Documentation

[docs/README.md](docs/README.md) indexes the technical documentation, one numbered file per
subsystem (architecture, data model, IO drivers, rules, API, Lua, ExternProc, MCP, every
`local_config.xml` option...). It is written in French. Design plans for features that are
not implemented are in `docs/admin`, `docs/matter`, `docs/connections`, `docs/autoscenario`,
`docs/climate` and `docs/remote`; `docs/refactoring/` holds dated work records.
[AGENTS.md](AGENTS.md) is the guide for coding agents and contributors working on the code.

## Continuous integration and images

- `.github/workflows/ci.yml` (Build and Test) runs on every branch push, pull request, and
  monthly: build and `make check` on Debian 12, the MCP dependency checks, and on pull
  requests a clang-format check of the changed lines and a diff-coverage gate (every
  executable line added under `src/` must be run by `make check`). It also publishes the
  `io-doc` artifact (IO documentation and device templates) that `calaos_installer` compares
  against.
- `.github/workflows/publish.yml` builds and pushes `ghcr.io/calaos/calaos_base` and
  triggers the Debian package build. It is run by hand only, from `master`, and requires a
  green Build and Test on the commit. A push does not publish anything.
  Channel `prerelease` pushes the image tag `dev`, `patch`/`minor`/`major` push `latest`;
  a versioned image tag and a git tag are created as well.

## Contributing

Issues and pull requests are welcome at <https://github.com/calaos/calaos_base>. Code style
is described in AGENTS.md and enforced on changed lines by `.clang-format`.

## License

GNU General Public License v3 or later, see [COPYING](COPYING).
