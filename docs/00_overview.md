# Calaos Base — Vue d'ensemble de l'architecture

> **Convention de traçabilité** (établie par E4.0f, appliquée ici par E4.5a). Chaque
> affirmation vérifiable de ce document porte sa provenance : **(capturé, `tests/…`)** =
> octets produits par le code et figés en golden/test ; **(dérivé, `Fichier.cpp:L`)** =
> forme lue dans le source. Rien d'autre n'est autorisé.

## Qu'est-ce que Calaos ?

Calaos est un serveur de domotique open-source écrit en **C++20**
(dérivé, [configure.ac:44](../configure.ac), `AX_CXX_COMPILE_STDCXX_20([noext],[mandatory])`).
Il expose un système d'automatisation basé sur des **règles** (conditions → actions), gère des
**entrées/sorties** (IO) sur de nombreux protocoles, et fournit une **API JSON** (HTTP +
WebSocket) à ses clients UI.

Le binaire principal est `calaos_server`. Des sous-processus sont lancés pour de nombreux drivers
via le framework `ExternProc` — MQTT, KNX, Wago, OneWire, OLA (C++), Reolink et Roon (Python),
scripts Lua (dérivé, [src/bin/calaos_server/Makefile.am:358-466](../src/bin/calaos_server/Makefile.am)).
Un sous-processus Python supplémentaire, `calaos_mcp`, expose l'installation aux LLM via le Model
Context Protocol (voir [15_mcp_server.md](15_mcp_server.md)).

---

## Arborescence des sources

(dérivé, arborescence de `src/`)

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
        OLA/               # Open Lighting Architecture (DMX)
        OneWire/           # Bus 1-Wire (via owfs)
        RemoteUI/          # IOs pour appareils embarqués RemoteUI
        Reolink/           # Caméras Reolink
        Wago/              # Automates Wago (Modbus TCP), + libmbus/ vendorisé
        Web/               # IOs HTTP/Web
      IPCam/               # Caméras IP
      LuaScript/           # Moteur de scripts Lua
      RemoteUI/            # Gestionnaire de connexions RemoteUI
      Rules/               # Conditions et Actions
      Scenario/            # Auto-scénarios
    calaos_mcp/            # Sidecar MCP (Python)
    tools/                 # calaos_config, calaos_mail…
  lib/                     # Bibliothèque utilitaire partagée
                           # + copies vendorisées : pugixml/, uvw/, llhttp/,
                           #   sqlite_modern_cpp/, exprtk/, sole/, uri_parser/,
                           #   libquickmail/, cpptui/, calaos-python/
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
| ExternProc Python | Côté Python du framework (Reolink, Roon) | [14_python_extern_proc.md](14_python_extern_proc.md) |
| MCP Server | Sidecar calaos_mcp (contrôle par LLM) | [15_mcp_server.md](15_mcp_server.md) |
| Options de config | Registre des options de `local_config.xml` | [16_config_options.md](16_config_options.md) |

---

## Flux de démarrage

(dérivé, [src/bin/calaos_server/main.cpp:110-198](../src/bin/calaos_server/main.cpp))

```
main()
  → Utils::initConfigOptions(--config, --cache)  // fixe les chemins config/cache, crée
                                                 // local_config.xml s'il manque, sème les défauts
  → ConfigOptions::purgeObsolete()               // retire les clés qui n'ont plus d'effet
  → [--gendoc <dir>] IOFactory::genDoc(dir); exit(0)
  → HistLogger / AVRManager / EventManager / ListeRule / ListeRoom
                                                 // construits dans CET ordre, et c'est
                                                 // load-bearing : il fixe l'ordre inverse des
                                                 // destructeurs (~Room appelle ListeRule)
  → RemoteUIManager / OtaFirmwareManager::init()
  → Config::LoadConfigIO()      // charge io.xml → construit Room/IO
  → Config::LoadConfigRule()    // charge rules.xml → construit Rule/Condition/Action
  → UDPServer(BCAST_UDP_PORT)   // sauf -noudp
  → HttpServer::Instance(port)  // port = local_config "port_api", défaut JSONAPI_PORT = 5454
  → McpServerManager::start()   // lance le sidecar MCP calaos_mcp (no-op si absent)
  → UDPServer(WAGO_LISTEN_PORT) // sauf -noudp
  → StartReadRules::addIO() + ioRead()
                                // compteur de valeurs initiales : chaque driver qui doit
                                // lire une valeur au démarrage s'y ajoute et se décompte ;
                                // quand le compteur retombe à 0, StartReadRules::ioRead()
                                // appelle ListeRule::ExecuteStartRules()
                                // (dérivé, Calaos.cpp:81-91)
  → Timer(0.1, ListeRule::RunEventLoop)  // scrutation des IOs à événement, 10 Hz
  → Timer::singleShot(0.1, ListeRoom::checkAutoScenario)
  → uvw event loop              // boucle libuv principale
```

---

## Dépendances clés

Dépendances **système**, exigées par `configure`
(dérivé, [configure.ac:51-56, 87, 119-156](../configure.ac)) :

| Lib | Usage |
|---|---|
| libuv (via uvw) | Boucle événementielle async (TCP, timers, pipes) — `uvw` est vendorisé |
| nlohmann-json (`json.hpp`) | Toute la sérialisation JSON : API, cache d'état, `HistLogger`/`eventlog`, wires des extern-procs — vendorisé, pas une dépendance système |
| sigc++-2.0 | Signaux/slots (connexions entre objets) |
| libcurl | Téléchargements HTTP (UrlDownloader) |
| sqlite3 | Base d'historique et tokens push (`HistLogger`) |
| OpenSSL | TLS et empreintes SHA1 (depuis T2.3) |
| LuaJIT | Moteur de scripts |
| pugixml ≥ 1.10 | Parsing/écriture XML + XPath 1.0 (config, IOs Web). Copie vendorisée dans `src/lib/pugixml` si le paquet système est absent |
| owfs (`libowcapi`) | 1-Wire (OneWire) |
| eibclient (knxd) | Bus KNX (ExternProc) |
| libmosquitto | Client MQTT (ExternProc) |
| libola | Open Lighting Architecture / DMX — **optionnel** |

Dépendance **vendorisée** notable : `libmbus`
([src/bin/calaos_server/IO/Wago/libmbus](../src/bin/calaos_server/IO/Wago/libmbus)) fournit le
Modbus TCP du driver Wago (port 502 par défaut, dérivé,
[IO/Wago/WagoConfigParse.h:43](../src/bin/calaos_server/IO/Wago/WagoConfigParse.h)).

> **Une seule bibliothèque JSON.** `nlohmann::json`, vendorisée en `src/lib/json.hpp` et aliasée
> `Json`. `jansson` a été retirée de `configure.ac` à la clôture de la série E4.1 : ce n'est plus
> une dépendance de compilation.

> **TinyXML 2.5.3 + TinyXPath ont été entièrement retirés** (série E4.4, commit `93537ae4`) :
> ~14 300 lignes de bibliothèque tierce supprimées du dépôt, au profit de pugixml. Les rares
> comportements de l'ancien lecteur que pugixml n'a pas nativement sont isolés dans
> [src/lib/XmlUtils.h](../src/lib/XmlUtils.h) — tout le reste du portage est un renommage.

---

## Conventions de code

- Le code du serveur est dans le namespace `Calaos`. `src/lib` est moins homogène : les
  fonctions libres sont dans `Utils` (`StringUtils.h`, `ConfigStore.h`, `Utils.h`),
  `XmlUtils` est dans `Calaos`, et `Params`/`FileUtils` sont au namespace global.
- Les classes singleton utilisent `Instance()` statique.
- Les IOs sont identifiés par un `id` unique (string), **opaque** : n'importe quelle chaîne
  non vide fait un id valide, et les ids réels viennent de `calaos_installer`. Le serveur
  n'en génère lui-même que dans un seul cas — un IO créé par l'API JSON sans id reçoit
  `io_<n>`, le premier entier libre (dérivé,
  [ListeRoom.cpp:493](../src/bin/calaos_server/ListeRoom.cpp) et
  [Calaos.cpp:30-43](../src/bin/calaos_server/Calaos.cpp)).
- L'id est **immuable** après construction : `set_param("id")` refuse et journalise, seul
  `IOBase::renameId()` change l'id *et* la clé de la table de hachage
  (dérivé, [IOBase.cpp:82-107](../src/bin/calaos_server/IOBase.cpp)).
- Les paramètres d'un IO sont stockés dans un objet `Params` (map string→string).
- La configuration est cherchée dans cet ordre : `$HOME/.config/calaos/`, puis `/etc/calaos`,
  puis `<prefix>/etc/calaos` ; si aucun n'a d'`io.xml`, `$HOME/.config/calaos/` est créé.
  La variable d'environnement `CALAOS_CONFIG` (ou l'option `--config`) court-circuite cette
  recherche (dérivé, [src/lib/ConfigStore.cpp:58, 86-137, 165-181](../src/lib/ConfigStore.cpp)).
- Les fichiers de config : `io.xml` (IOs), `rules.xml` (règles), `local_config.xml` (paramètres
  serveur). Le cache d'état et la base d'historique vivent à part, dans `$HOME/.cache/calaos/`.
  Voir [11_config_persistence.md](11_config_persistence.md).
