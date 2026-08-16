# Calaos Server — Refactoring Board

Tracking board for distributing refactoring tickets to autonomous dev sub-agents.
Each ticket is a file `docs/refactoring/<ID>.md`.

> **Ce qui change pour les utilisateurs** : [`RELEASE_NOTES.md`](RELEASE_NOTES.md).
>
> **Phase 4 ?** Lire [`PHASE4.md`](PHASE4.md) (cartographie + découpage en sous-tickets).
>
> **Reprendre le travail ?** Commencer par [`ORCHESTRATION.md`](ORCHESTRATION.md) (état + prochaine
> action + protocoles), puis [`DECISIONS.md`](DECISIONS.md) (choix utilisateur) et
> [`FINDINGS.md`](FINDINGS.md) (backlog de découvertes hors-périmètre).

**Status:** `📋 Backlog` · `🔨 In Progress` · `👀 Review` · `✅ Done` · `⛔ Blocked`

**Anti-conflict rule:** within a single wave, every ticket owns an **exclusive** set of files. Two tickets in the same wave never modify the same file. Cross-phase edits to the same file are allowed only when serialized into different waves (see the graph).

---

## Context

`calaos_base` is the Calaos home-automation server: ~75k lines of C++14 (libuv/uvw, sigc++, **two** JSON libraries — jansson **and** nlohmann/json — luajit, SQLite) plus Python drivers (MCP sidecar, Reolink, Roon) over the `ExternProc` Unix-socket IPC. This is production code.

A three-part audit (core/rules, network/IPC/security, drivers/lib/tests/build), re-verified against the current tree and enriched by a 10-subsystem sweep, revealed homogeneous technical debt:

- **Root cause:** ownership by raw pointers with multiple non-owning aliases of the same `IOBase*`/`Rule*` → lifecycle bugs (dangling / use-after-free at runtime). Confirmed criticals: `RemoteUIWebSocketHandler` post-auth raw-`this` timer, `HueOutputLightRGB` polling timer with an empty destructor, `ActionMail`/`ActionPush` dangling-this download callbacks.
- **Network security:** path traversal, weak-PRNG tokens, no size/connection caps (pre-auth DoS), non-constant-time comparisons, secrets logged before auth.
- **Correctness / edge cases:** off-by-one heap overflow in ping, iterator-invalidation UB in Onkyo reassembly, absent Lua sandbox, `exit(-1)` on malformed XML, many unvalidated inputs.
- **Massive duplication** and a **dual JSON library**.
- **Almost no safety net:** 3 unit tests (none on the core), CI without `make check`.

### Decisions taken
- **Scope:** the whole repository.
- **C++:** C++14 → **C++20** (official Arch build + `debian:12`/GCC 12 support it).
- **JSON:** converge on nlohmann/json (already vendored).
- **Crypto:** reuse OpenSSL (already a hard dependency) instead of bundled SHA1 / weak PRNG.
- **Backlog language:** English (this board replaces the earlier French draft; git history retains it).

---

## Board

| Wave 0a / 0b | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|---|---|---|---|---|
| T0.1 T0.4 T0.6 · T0.2 T0.3 T0.5 | T1.1 … T1.19 | T2.1 … T2.7 | T3.1 T3.2a-f T3.3 T3.4 T3.5 | E4.1 E4.2 E4.3 E4.4 |

---

## Tracking

| ID | Phase | Title | Type | Depends on | Status |
|---|---|---|---|---|---|
| [T0.1](T0.1.md) | 0 | CI build + `make check` | infra | — | ✅ |
| [T0.2](T0.2.md) | 0 | Build & artifact cleanup (tests) | infra | T0.1 | ✅ |
| [T0.3](T0.3.md) | 0 | Core test scaffolding | infra | T0.1 | ✅ |
| [T0.4](T0.4.md) | 0 | C++20 switch + configure cleanup | infra | — | ✅ |
| [T0.5](T0.5.md) | 0 | clang-format + CI check (diff only) | infra | T0.2 | ✅ |
| [T0.6](T0.6.md) | 0 | Docker/build hardening + dead scripts | infra | — | ✅ |
| [T1.1](T1.1.md) | 1 | Rule/IO lifecycle (C1, C2 + scenario) | fix | Phase 0 | ✅ |
| [T1.2](T1.2.md) | 1 | Condition semantics (M1, M7) | fix | Phase 0 | ✅ |
| [T1.3](T1.3.md) | 1 | ListeRoom robustness (M6, m9, m10) | fix | Phase 0 | ✅ |
| [T1.4](T1.4.md) | 1 | JsonApi hardening (F6, F7, F3, F4, F2, F11 + cover) | fix (sec) | Phase 0 | ✅ |
| [T1.5](T1.5.md) | 1 | Transport & WS framing limits (F9, F10, F12 +) | fix (sec) | Phase 0 | ✅ |
| [T1.6](T1.6.md) | 1 | RemoteUI HMAC constant-time + dedup | fix (sec) | Phase 0 | ✅ |
| [T1.7](T1.7.md) | 1 | MCP token CSPRNG (F1) | fix (sec) | Phase 0 | ✅ |
| [T1.8](T1.8.md) | 1 | Python sidecar auth & quality (F5) | fix (sec) | Phase 0 | ✅ |
| [T1.9](T1.9.md) | 1 | ExternProc framing (F13 + sockfd) | fix | Phase 0 | ✅ |
| [T1.10](T1.10.md) | 1 | RemoteUI WebSocket/OTA lifecycle | fix | Phase 0 | ✅ |
| [T1.11](T1.11.md) | 1 | IOBase/IOFactory id integrity | fix | Phase 0 | ✅ |
| [T1.12](T1.12.md) | 1 | Utils CSPRNG/safety + tcpsocket | fix (sec) | Phase 0 | ✅ |
| [T1.13](T1.13.md) | 1 | LAN & Hue memory safety | fix | Phase 0 | ✅ |
| [T1.14](T1.14.md) | 1 | Reolink driver lifecycle & log hygiene | fix | Phase 0 | ✅ |
| [T1.15](T1.15.md) | 1 | Lua sandbox + exec watchdog | fix (sec) | Phase 0 | ✅ |
| [T1.16](T1.16.md) | 1 | MCP client + Roon Python robustness | fix | Phase 0 | ✅ |
| [T1.17](T1.17.md) | 1 | Extern-proc driver mains (Wago/OLA/OneWire/Mqtt) | fix | Phase 0 | ✅ |
| [T1.18](T1.18.md) | 1 | ActionMail/ActionPush dangling-this | fix | Phase 0 | ✅ |
| [T1.19](T1.19.md) | 1 | IO controllers (MySensors/Gpio/Web) | fix | Phase 0 | ✅ |
| [T2.1](T2.1.md) | 2 | Timer lifetime | refactor | Phase 1 | ✅ |
| [T2.2](T2.2.md) | 2 | Utils god-object split | refactor | Phase 1, T1.12 | ✅ |
| [T2.3](T2.3.md) | 2 | SHA1 → OpenSSL | refactor | Phase 1 | ✅ |
| [T2.4](T2.4.md) | 2 | Config robustness (m1–m6 + cache) | fix | Phase 1 | ✅ |
| [T2.5](T2.5.md) | 2 | UrlDownloader → libcurl | refactor | Phase 1 | ✅ |
| [T2.6](T2.6.md) | 2 | Common-lib correctness (ThreadedQueue/ColorUtils/Calendar/Params) | fix | Phase 1 | ✅ |
| [T2.7](T2.7.md) | 2 | ~~NTPClock timer lifetime~~ (resolved by deletion, see plan step 2) | fix | Phase 1, T2.1 | ✅ |
| [T3.1](T3.1.md) | 3 | AVReceiver correctness & dedup | fix+refactor | Phase 2 | ✅ |
| [T3.2a](T3.2a.md) | 3 | Thin IO subclasses — KNX | refactor | Phase 2 | ✅ |
| [T2.8](T2.8.md) | 2 | exprtk stack-use-after-scope (ASan, préexistant) | fix | Phase 1 | ✅ |
| [T2.9](T2.9.md) | 2 | ListeRule dead code updateAllRulesTo* | refactor | T1.1 | ✅ |
| [T2.10](T2.10.md) | 2 | UrlDownloader lifecycle (cancel, pipe, buffering) | fix (sec) | T1.4 | ✅ |
| [T2.11](T2.11.md) | 2 | Transport hardening compléments (431, cap/IP, config) | fix (sec) | T1.5 | ✅ |
| [T2.12](T2.12.md) | 2 | Remove MySensors entirely (dead code, user decision) | removal | Phase 1 | ✅ |
| [T2.13](T2.13.md) | 2 | GPIO: honor `debounce_time` from config | fix | T1.19 | ✅ |
| [T2.14](T2.14.md) | 2 | Wire pytest suites into `make check` | infra | T0.3 | ✅ |
| [T2.15](T2.15.md) | 2 | JsonApi audio-state UAF/leak + RemoteUI token constant-time | fix (sec) | T1.4 | ✅ |
| [T2.16](T2.16.md) | 2 | HttpClient keep-alive request_headers leak | fix (sec) | T2.11 | ✅ |
| [T2.17](T2.17.md) | 2 | UrlDownloader TLS verification by default | fix (sec) | T2.5 | ✅ |
| [T2.18](T2.18.md) | 2 | createIO null-guard audit (JsonApi/AutoScenario) | fix | T1.11 | ✅ |
| [T2.19](T2.19.md) | 2 | Per-IO `insecure` option for Web/Lua (user decision) | feature | T2.17 | ✅ |
| [T3.2b](T3.2b.md) | 3 | ~~Thin IO subclasses — MySensors~~ (obsolete: removal via T2.12) | refactor | T2.12 | ⛔ |
| [T3.2c](T3.2c.md) | 3 | Thin IO subclasses — Mqtt | refactor | Phase 2 | ✅ |
| [T3.2d](T3.2d.md) | 3 | Thin IO subclasses — Web | refactor | Phase 2 | ✅ |
| [T3.2e](T3.2e.md) | 3 | Thin IO subclasses — Wago | refactor | Phase 2 | ✅ |
| [T3.2f](T3.2f.md) | 3 | Thin IO subclasses — Gpio | refactor | Phase 2 | ✅ |
| [T3.3](T3.3.md) | 3 | IPCam cleanup + Synology snapshot fix | fix+refactor | Phase 2 | ✅ |
| [T3.4](T3.4.md) | 3 | base64 hardening | fix | Phase 2, T2.2 | ✅ |
| [T3.5](T3.5.md) | 3 | Squeezebox correctness & lifetime | fix | Phase 2 | ✅ |
| [T3.6](T3.6.md) | 3 | Remove Gadspot driver (dead code, user decision) | removal | T3.3 | ✅ |
| [T3.7](T3.7.md) | 3 | ExternProc pid-0 group-kill guard | fix | — | ✅ |
| [T3.8](T3.8.md) | 3 | AVRRose/NotifServer async lifetimes | fix | T3.1 | ✅ |
| [T3.9](T3.9.md) | 3 | pid-0 kill guard: remaining sites | fix | T3.7 | ✅ |
| [T3.10](T3.10.md) | 3 | Promote ThinIo + harmonize driver mixins | refactor | T3.2a-f | ✅ |
| [T3.11](T3.11.md) | 3 | Hygiene: SHA1/curl residue, npm lockfile | cleanup | T2.3, T2.5 | ✅ |
| [E4.0](E4.0.md) | 4 | Caractérisation de l'API JSON (préalable dur à E4.1) | epic | — | 📋 |
| [E4.0a](E4.0.md) | 4 | Fondation du filet de caractérisation (harnais in-process, oracle sémantique, goldens) | tests | — | ✅ |
| [E4.1](E4.1.md) | 4 | Single JSON library | epic | **E4.0** | 📋 |
| [E4.2](E4.2.md) | 4 | Ownership model (smart pointers / by-id) | epic | Phase 3 | 🔨 |
| [E4.2a](E4.2a.md) | 4 | ListeRoom by-id resolution + lookup hardening | refactor | — | ✅ |
| [E4.2b](E4.2b.md) | 4 | Room owns unique_ptr<IOBase>, caches non-owning | refactor | E4.2a | ✅ |
| [E4.2c](E4.2c.md) | 4 | Conditions/Actions reference IOs by id (not IOBase*) | refactor | E4.2b | ✅ |
| [E4.2d](E4.2d.md) | 4 | Rule/ListeRule own their objects (unique_ptr) | refactor | E4.2c | ✅ |
| [E4.3](E4.3.md) | 4 | ~~Test coverage~~ (atteint à 99%: 40 binaires/411 cas — voir PHASE4.md) | epic | T0.3 | ✅ |
| [E4.3ab](E4.3ab.md) | 4 | Coverage gaps: TimeRange/InPlageHoraire + XML round-trip | tests | — | ✅ |
| [E4.3cd](E4.3cd.md) | 4 | `--enable-asan` option + CI coverage report | infra | — | ✅ |
| [T3.13](T3.13.md) | 3 | TimeRange: include guard, parse UB, midnight-wrap (user decision) | fix | E4.3ab | ✅ |
| [T3.14](T3.14.md) | 3 | Shutdown dangling read (static destruction order) | fix | — | ✅ |
| [T3.15](T3.15.md) | 3 | RemoteUI device_info does not round-trip (user decision) | fix | E4.4cd | ✅ |
| [T3.16](T3.16.md) | 3 | Fix CI format-check (pugixml exclude, git before checkout) | infra | — | ✅ |
| [E4.4](E4.4.md) | 4 | ~~TinyXML 2.5.3 → TinyXML2~~ → **pugixml** (user decision) | epic | Phase 3 | ✅ |
| [T3.12](T3.12.md) | 3 | ~~Patch 2 TinyXML CVEs~~ (abandoned: superseded by pugixml migration) | fix (sec) | — | ⛔ |
| [E4.4a](E4.4a.md) | 4 | Introduce pugixml (Debian pkg, XPath on) | infra | — | ✅ |
| [E4.4b](E4.4b.md) | 4 | WebCtrl: TinyXPath → pugixml XPath | refactor | E4.4a | ✅ |
| [E4.2e](E4.2e.md) | 4 | Rule with a missing dependency is disabled (user decision) | fix | E4.2c | ✅ |
| [E4.4cd](E4.4cd.md) | 4 | XML port on pugixml (sweep + core, atomic) | refactor | E4.4b | ✅ |
| [E4.4dbis](E4.4dbis.md) | 4 | Port ConfigStore.cpp (last TinyXML consumer) | refactor | E4.4cd | ✅ |
| [E4.4e](E4.4e.md) | 4 | Delete vendored TinyXML + TinyXPath | removal | E4.4d | ✅ |
| [E4.2f](E4.2f.md) | 4 | Scenario/AutoScenario back-pointers → ids or weak_ptr | refactor | E4.2d | ✅ |
| [E4.2g](E4.2g.md) | 4 | ~~JsonApi `IOBase*` sites~~ (re-scopé : 0/21 dangereux, abandonné) | refactor | E4.2c | ⛔ |
| [T3.17](T3.17.md) | 3 | Généraliser la garde alive aux ~42 sites async audio/caméra | fix (sec) | T2.15 | 📋 |
| [T3.17a](T3.17.md) | 3 | Garder la chaîne récursive `get_playlist` (double mort : `apiAlive` + `AudioPlayer*`) | fix (sec) | T2.15 | ✅ |
| [T3.18](T3.18.md) | 3 | Scénario amputé : désactiver le scénario entier (user decision) | fix | E4.2f, **T3.17** | 📋 |
| ~~T3.18b~~ | 3 | ~~Commande d'API dédiée de réactivation~~ (annulé : réintégré dans T3.18) | feature | — | ⛔ |

---

## Wave / dependency graph

```
Phase 0 (infra):
  Wave 0a:  T0.1, T0.4, T0.6          (disjoint: ci.yml / configure.ac / Dockerfile+libquickmail+scripts)
  Wave 0b:  T0.2 ─▶ T0.3 ─▶ T0.5      (serialized: share tests/Makefile.am and/or ci.yml)
  E4.3 may begin after T0.3 and run continuously.

Phase 1 (critical fixes / security) — depends on Phase 0. Files fully disjoint → all parallel:
  T1.1  ListeRule/AutoScenario   T1.2  ConditionStd/Output   T1.3  ListeRoom
  T1.4  JsonApi*                 T1.5  Http/WebSocket/McpProxy/WebSocketFrame
  T1.6  RemoteUI/RemoteUI+HMAC+Manager                        T1.7  McpServerManager
  T1.8  calaos_mcp auth.py + calaos-python extern_proc/logger T1.9  IO/ExternProc
  T1.10 RemoteUI WS/OTA handlers T1.11 IOBase/IOFactory       T1.12 Utils(RNG/paths)+tcpsocket ⮕ blocks T2.2
  T1.13 IO/LAN + IO/Hue          T1.14 IO/Reolink              T1.15 LuaScript
  T1.16 calaos_mcp client/tools + Roon main                   T1.17 extern-proc *_main.cpp
  T1.18 ActionMail/ActionPush    T1.19 MySensors/Gpio/Web controllers

Phase 2 (structural) — depends on Phase 1:
  Wave 2a (SOLO):  T2.2  Utils split          (depends T1.12; run alone — Utils.h is included almost everywhere)
  Wave 2b:         T2.1, T2.3, T2.4, T2.5, T2.6, T2.7   (disjoint files; T2.7 after T2.1)

Phase 3 (driver dedup / fix) — depends on Phase 2. Disjoint files → parallel:
  T3.1 AVR*/AVReceiver   T3.2a KNX  T3.2b MySensors(sub)  T3.2c Mqtt(sub)
  T3.2d Web(sub)         T3.2e Wago(sub)  T3.2f Gpio(sub)
  T3.3 IPCam             T3.4 base64 (after T2.2)          T3.5 Squeezebox

Phase 4 (epics) — depends on Phase 3 + E4.3:
  E4.3 (ongoing from Phase 0) is the prerequisite net.
  E4.1 (JSON), E4.2 (ownership), E4.4 (TinyXML2) are SERIALIZED / split into per-module
  sub-tickets before execution — they all touch ListeRoom / ListeRule / CalaosConfig / JSON files.
```

Cross-phase sequential edges (same file, different wave — never same wave):
- `src/lib/Utils.cpp`: T1.12 (P1, RNG+paths) → T2.2 (P2, split).
- `WebSocket.cpp`: T1.5 (P1) → T2.3 (P2, SHA1 removal).
- `CalaosConfig.cpp`: T2.4 (P2) → E4.4 (P4, parser migration).
- IO controller dirs: T1.13/T1.19 (P1, base/ctrl files) vs T3.2b/d/f (P3, subclass files) — disjoint files even inside the same directory.
- `IO/Wago/`, `IO/Mqtt/`: T1.17 (P1, extern-proc mains) vs T3.2e/c (P3, subclasses) — disjoint files.

---

## Quick wins (highest leverage first)

1. **T0.1** — CI `make check` (today nothing guards merges).
2. **T1.4** (path traversal `event_picture`) + **T1.1** (rule/IO UAF).
3. **T1.13** (ping heap overflow + Hue polling-timer UAF — two confirmed criticals).
4. **T1.7** (MCP token CSPRNG) + **T1.5** (pre-auth DoS caps).
5. **T3.1** (Onkyo reassembly iterator-invalidation UB — memory corruption on real traffic).

---

## Verification (per ticket & per wave)

1. **Build:** `./autogen.sh && ./configure && make` with no new warnings (+ `--with-mqtt --with-knx --with-owfs --with-ola` for drivers).
2. **Tests:** `make check` green; every "fix" ticket adds a regression test.
3. **Security (Phase 1):** path traversal rejected, login throttled, MCP token 256-bit CSPRNG, oversized WS frame rejected without OOM, ping/WOL/Onkyo inputs validated.
4. **Functional:** server started with real `io.xml`/`rules.xml`, IO state + rule execution verified via the JSON API (port 5454); `ExternProc` driver (MQTT/KNX) + MCP sidecar OK.
5. **Wave review:** `/code-review` on the cumulative diff before merge; ASan/valgrind on all lifecycle-fix tickets.
