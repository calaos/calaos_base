# Documentation Calaos Base — Index

Documentation technique du projet `calaos_base` pour agents LLM.

## Fichiers

| Fichier | Contenu |
|---|---|
| [00_overview.md](00_overview.md) | Architecture globale, arborescence, dépendances, conventions |
| [01_core_data_model.md](01_core_data_model.md) | IOBase, Room, ListeRoom, IOFactory, Params |
| [02_io_drivers.md](02_io_drivers.md) | Tous les drivers IO : Wago, KNX, MQTT, GPIO, Hue, MySensors… |
| [03_rules_engine.md](03_rules_engine.md) | Règles, Conditions, Actions — moteur d'automatisation |
| [04_scenarios.md](04_scenarios.md) | AutoScenario, plages horaires, InputTimer |
| [05_audio.md](05_audio.md) | AudioPlayer (Squeezebox, Roon), AVReceiver (Denon, Yamaha…) |
| [06_ipcam.md](06_ipcam.md) | Caméras IP (IPCam, Axis, Foscam, Synology…) |
| [07_remoteui.md](07_remoteui.md) | Appareils embarqués RemoteUI, WebSocket, HMAC auth, OTA |
| [08_http_api.md](08_http_api.md) | API JSON HTTP/WebSocket, événements temps réel |
| [09_lua_scripting.md](09_lua_scripting.md) | Moteur Lua, API calaos:*, exemples de scripts |
| [10_events_notifications.md](10_events_notifications.md) | EventManager, HistLogger, push mobile, e-mail |
| [11_config_persistence.md](11_config_persistence.md) | local_config.xml, rules.xml, cache d'état |
| [12_extern_proc.md](12_extern_proc.md) | Framework IPC sous-processus (MQTT, KNX) |
| [13_utility_lib.md](13_utility_lib.md) | Utils, Params, Timer, Logger, ColorUtils et libs embarquées |
| [14_python_extern_proc.md](14_python_extern_proc.md) | Drivers Python (calaos_extern_proc, Reolink, Roon) |
