# Calaos Base — Vue d'ensemble de l'architecture

## Qu'est-ce que Calaos ?

Calaos est un serveur de domotique open-source écrit en C++14. Il expose un système d'automatisation basé sur des **règles** (conditions → actions), gère des **entrées/sorties** (IO) sur de nombreux protocoles, et fournit une **API JSON** (HTTP + WebSocket) à ses clients UI.

Le binaire principal est `calaos_server`. Des sous-processus sont lancés pour de nombreux drivers (MQTT, KNX, Wago, OneWire, OLA, Reolink, scripts Lua, Roon) via le framework `ExternProc`.

---

## Arborescence des sources

```
src/
  bin/
    calaos_server/         # Serveur principal
      Audio/               # Lecteurs audio et amplis AV
      IO/                  # Drivers d'entrées/sorties
        Gpio/              # GPIO Linux
        Hue/               # Philips Hue
        KNX/               # Bus KNX (via knxd)
        LAN/               # Ping / Wake-On-LAN
        Mqtt/              # MQTT (via process externe)
        MySensors/         # Réseau MySensors
        OLA/               # Open Lighting Architecture (DMX)
        OneWire/           # Bus 1-Wire (via owfs)
        RemoteUI/          # IOs pour appareils embarqués RemoteUI
        Reolink/           # Caméras Reolink
        Wago/              # Automates Wago (Modbus TCP)
        Web/               # IOs HTTP/Web
      IPCam/               # Caméras IP
      LuaScript/           # Moteur de scripts Lua
      RemoteUI/            # Gestionnaire de connexions RemoteUI
      Rules/               # Conditions et Actions
      Scenario/            # Auto-scénarios
  lib/                     # Bibliothèque utilitaire partagée
```

---

## Sous-systèmes principaux

| Sous-système | Rôle | Doc |
|---|---|---|
| Core Data Model | IOBase, Room, ListeRoom, IOFactory | [01_core_data_model.md](01_core_data_model.md) |
| IO Drivers | Drivers pour chaque protocole (Wago, KNX, MQTT…) | [02_io_drivers.md](02_io_drivers.md) |
| Rules Engine | Règles : Conditions + Actions | [03_rules_engine.md](03_rules_engine.md) |
| Scénarios | AutoScenario, plages horaires | [04_scenarios.md](04_scenarios.md) |
| Audio | Lecteurs (Squeezebox, Roon) + Amplis AV | [05_audio.md](05_audio.md) |
| IP Cameras | IPCam et ses pilotes | [06_ipcam.md](06_ipcam.md) |
| RemoteUI | Appareils embarqués connectés en WebSocket | [07_remoteui.md](07_remoteui.md) |
| HTTP / JSON API | Serveur HTTP, WebSocket, API JSON | [08_http_api.md](08_http_api.md) |
| Lua Scripting | Exécution de scripts Lua dans les règles | [09_lua_scripting.md](09_lua_scripting.md) |
| Events & Notifications | EventManager, push, e-mail | [10_events_notifications.md](10_events_notifications.md) |
| Config & Persistence | Chargement/sauvegarde XML, cache d'état | [11_config_persistence.md](11_config_persistence.md) |
| ExternProc IPC | Framework sous-processus (MQTT, KNX) | [12_extern_proc.md](12_extern_proc.md) |
| Utility Library | Utils, Params, Timer, Logger… | [13_utility_lib.md](13_utility_lib.md) |

---

## Flux de démarrage

```
main()
  → Config::LoadConfigIO()      // charge io.xml → construit Room/IO
  → Config::LoadConfigRule()    // charge rules.xml → construit Rule/Condition/Action
  → HttpServer::Instance()      // ouvre port HTTP/WS (défaut 4444)
  → ListeRule::ExecuteStartRules() // évalue les règles ConditionStart
  → uvw event loop              // boucle libuv principale
```

---

## Dépendances clés

| Lib | Usage |
|---|---|
| libuv (via uvw) | Boucle événementielle async (TCP, timers, pipes) |
| jansson / nlohmann-json | Sérialisation JSON |
| sigc++ | Signaux/slots (connexions entre objets) |
| libcurl | Téléchargements HTTP (UrlDownloader) |
| TinyXML | Parsing/écriture XML (config) |
| LuaJIT | Moteur de scripts |
| libmbus | Protocole Modbus TCP (Wago) |
| libmosquitto | Broker MQTT (ExternProc) |
| knxd client | Bus KNX (ExternProc) |
| owfs | 1-Wire (OneWire) |

---

## Conventions de code

- Tout le code métier est dans le namespace `Calaos`.
- Les classes singleton utilisent `Instance()` statique.
- Les IOs sont identifiés par un `id` unique (string). Le format est `id-<uuid>`.
- Les paramètres d'un IO sont stockés dans un objet `Params` (map string→string).
- La configuration est persistée dans `/etc/calaos/` (ou `~/.config/calaos/`).
- Les fichiers de config : `io.xml` (IOs), `rules.xml` (règles), `local_config.xml` (paramètres serveur).
