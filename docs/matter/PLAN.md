# Support de Matter et Thread dans Calaos

État : plan figé, **non lancé**. Rien ne s'implémente sans décision de Raoul.

Cette analyse s'appuie, le 2026-10-18, sur :
- deux états des lieux : l'écosystème Matter et les points d'intégration dans Calaos ;
- trois conceptions : A, intégration typée native ; B, bridge vers MQTT ; C, aspects transverses ;
- un arbitre contradicteur.

Elle couvre deux usages :
- piloter des appareils Matter et Thread comme des IOs Calaos ;
- exposer Calaos à Apple Home, Google et Alexa en tant que bridge Matter.

## État de l'art (octobre 2026)
- **Spec.** La version courante est Matter 1.6 (juin 2026), avec environ 82 types d'appareils.
  - Lumières, prises, interrupteurs, volets, thermostats, capteurs et serrures sont supportés depuis la 1.0.
  - La 1.5 a ajouté les caméras et le cluster Closures.
  - La 1.6 apporte le commissioning par NFC et le Joint Fabric.
- **Multi-admin.** Chaque contrôleur installe son propre NOC sur l'appareil. Calaos peut donc être le second contrôleur d'un appareil déjà appairé dans Apple Home ou Google Home, à partir du code de « mode appairage », valable 15 minutes.
- **Attestation.** La chaîne est PAA → PAI → DAC, vérifiée contre le DCL main-net. Un **contrôleur non certifié peut commissionner des appareils du commerce** : il génère sa propre autorité de fabric (RCAC et NOC), et les appareils n'attestent pas le contrôleur.
- **Contrôleurs sous Linux.**

  | Projet | État | Remarques |
  |---|---|---|
  | **matterjs-server** | Apache-2, Node 22+, image `ghcr.io/matter-js/matterjs-server:stable` | API WebSocket sur le port 5580, compatible avec python-matter-server. Contrôleur par défaut de Home Assistant depuis juin 2026. OTA via le DCL. Encore en bêta, re-certification prévue. Environ 200 à 300 Mo de RAM. |
  | python-matter-server | archivé en juin 2026 | — |
  | SDK C++ connectedhomeip | — | Trop lourd et sans ABI stable. |
  | rs-matter | — | Pas encore éprouvé en tant que contrôleur. |

  Précédents :
  - openHAB et ioBroker embarquent matter.js ;
  - Indigo lance matterjs-server en sidecar ;
  - Homey est certifié et embarque son propre TBR.
- **Thread.**
  - Il faut un border router, soit l'OTBR `ot-br-posix` (image `openthread/border-router`, RCP en spinel/HDLC), soit celui de l'utilisateur (Apple TV, HomePod, Google).
  - Le dongle doit être **dédié** : le MultiPAN Zigbee+Thread sur une seule radio est abandonné.
  - IPv6 est obligatoire, avec mDNS et SRP.
  - Thread 1.4 permet le partage des identifiants entre border routers (ePSKc).
- **Bridge.** On utilise un Aggregator avec des Bridged Nodes, ce que matter.js sait faire.
  - Sans certification, Apple affiche « accessoire non certifié » et propose « Ajouter quand même ».
  - Limites : environ 150 accessoires pour HomeKit, 50 pour Alexa.
- **Pièges.**
  - `--network=host` est obligatoire, et il faut fixer l'interface (`--primary-interface`, `OT_INFRA_IF`).
  - **Ne jamais faire tourner deux répondeurs mDNS.**
  - Le BLE depuis un conteneur demande dbus et bluez.
  - Les appareils sur batterie (ICD) remontent leurs valeurs plus lentement.
  - Il existe des bizarreries propres à chaque fabricant.

## État côté Calaos
- **Patterns réutilisables.**
  - ExternProc : JSON tramé sur socket unix. Le modèle Python de Reolink (`IO/Reolink/ReolinkCtrl.cpp:30-114`, `ExternProcReolink_main.py`) gère la reconnexion et le ré-enregistrement.
  - `ThinIo<Base>` (`IO/ThinIo.h`).
  - Devices et `ProxyClimate` (`IO/ProxyClimate.cpp:51-139`).
  - `DeviceTemplates` (`IO/DeviceTemplates.cpp`).
  - La chaîne IODoc.
  - Le `DialogZigbee2mqtt` de l'installer.
  - `status_info` pour la batterie et la connexion.
- **Python `websockets`** est déjà présent dans l'image runner (`calaos_mcp/pyproject.toml:20`, `Dockerfile:126-127`).
- **Calaos OS.**
  - Les services sont des units systemd `podman run --network=host`, packagés sur le modèle de pkgdebs zigbee2mqtt.
  - Il n'y a ni avahi ni bluez, et `/run/dbus` n'est monté dans aucun conteneur.
  - IPv6 SLAAC est actif.
  - L'image fait 4 Go et a peu de marge.
- **Exposition vers d'autres écosystèmes.** `calaos-homekit` est un PoC en Go, instable et non packagé.

## Décision : intégration typée native (option A allégée)
L'option B, un bridge Matter → MQTT, est écartée pour quatre raisons :
- **Sécurité.** mosquitto est anonyme et écoute sur tout le réseau (`mosquitto_pre:24-25`). Tout le LAN pourrait commissionner un appareil dans le fabric Calaos ou commander les appareils.
- **Faux gain.** La webapp ne parle pas MQTT, il faut donc de toute façon une API dans calaos_server.
- **Fragilité.** Les topics par nom cassent quand on renomme un appareil. Les appuis de boutons deviennent des IOs string bricolés : un interrupteur 4 touches donne environ 12 IOs.
- **Maintenance.** Un sidecar Node avec des conventions de normalisation à maintenir à vie.

### Architecture
- **`calaos-matter`.** Un paquet pkgdebs qui contient un conteneur matterjs-server.
  - Il s'**installe à la demande via apt** et n'est pas embarqué dans l'image.
  - Son unit est `--no-enable`.
  - Il tourne en `--network=host`, écoute sur `LISTEN_ADDRESS=127.0.0.1`, avec le dashboard désactivé.
  - Ses données sont dans `/mnt/calaos/matter`.
  - Il est lancé avec `--primary-interface`.
- **ExternProc Python `calaos_matter`.** C'est le client du WebSocket de matterjs-server, sur le modèle de Reolink.
  - Il appelle `start_listening`, filtre les chemins enregistrés et remonte les événements.
  - Il se reconnecte avec backoff : il refait `start_listening`, recharge les nœuds et repousse l'état complet.
  - Pas de miroir MQTT en v1. Si on en veut un un jour, ce sera une option d'environ 200 lignes dans le même process, jamais le chemin de commande.
- **`MatterCtrl` dans calaos_server.** Un singleton qui possède l'`ExternProcServer`, sur le modèle de `ReolinkCtrl.h`, avec un `MatterWire.h` pour le protocole.
- **IOs typés sur `ThinIo`**, avec un mixin `MatterIoBase` :

  | IO Calaos | Cluster Matter |
  |---|---|
  | `MatterOutputLight` | OnOff |
  | `MatterOutputLightDimmer` | LevelControl (0-254 ↔ 0-100) |
  | `MatterOutputLightRGB` | ColorControl |
  | `MatterShutterSmart` | WindowCovering, avec la position réelle |
  | `MatterInputTemp` | TemperatureMeasurement |
  | `MatterInputAnalog` | humidité, pression, luminosité, puissance, choisi par `io_style` |
  | `MatterOutputString` / `MatterOutputAnalog` | — |
  | `MatterInputSwitch` | BooleanState, Occupancy |
  | `MatterInputSwitchTriple` | événements du cluster Switch : appui simple, long, multiple, avec le reset de 250 ms habituel |
- **Thermostat.** Un template `matter_thermostat` sur **ProxyClimate**, dont les rôles mode, consigne, plage, action et ventilation correspondent au cluster Thermostat et à FanControl. **Pas de classe `MatterClimate`.**
- **Identité.** Params `node_id` et `endpoint`, plus `cluster` et `attribute` pour les types génériques. La clé interne est `node/endpoint/cluster/attribute`.
- **Joignabilité et batterie.** `node_updated.available` alimente `StatusType::Connected`, et `PowerSource.BatPercentRemaining` (divisé par 2) alimente `BatteryLevel`. Les deux émettent `EventIOStatusChanged`. La webapp (`SensorStatusBadge.vue`) et le mobile (`status_info.battery_level`) les affichent déjà.
- **API.**
  - Verbes WS et HTTP :
    - `matter_commission {code, req_id}` : QR `MT:` ou code manuel à 11 ou 21 chiffres, réseau IP uniquement ;
    - `matter_nodes` : endpoints, types d'appareil, fabricant et produit, disponibilité, type Calaos suggéré ;
    - `matter_open_commissioning_window {node_id}` : renvoie un code et un QR pour partager l'appareil vers Apple ou Google ;
    - `matter_remove_node`, avec une suppression forcée si l'appareil est hors ligne ;
    - `matter_interview`.
  - Progression : événement `matter_commission`, avec les étapes `sent`, `node_added`, `interviewed`, `done` et `error`. matterjs-server ne fournit pas un suivi plus fin. Les clients HTTP interrogent `matter_commission_status`.
  - Le scope service MCP refuse ces verbes.
- **IODoc.** Tous les params sont déclarés (`paramAddInt`/`List`, `linkRoleAdd`), avec la vérification `--gendoc` en CI.
- **OTA.** En phase 2 ou plus tard, via `check_node_update` / `update_node`. Le conteneur a alors besoin d'un accès HTTPS sortant vers le DCL.

### Réseau et OS
- **mDNS.**
  - **Ni avahi, ni `MulticastDNS` dans resolved.** C'est une contrainte dure, à écrire noir sur blanc.
  - matter.js embarque son propre mDNS, et OTBR est compilé avec `OTBR_MDNS=openthread`.
- **Sysctls** (`/etc/sysctl.d/60-calaos-thread.conf`) :
  - `accept_ra=2` et `accept_ra_rt_info_max_plen=64` sur l'interface. Ils sont nécessaires même sans OTBR, pour joindre un réseau Thread via le TBR Apple ou Google.
  - Avec OTBR, en plus : `forwarding=1`.
- **Interface.** Elle est déterminée une seule fois par le script `_pre` (première interface `en*` qui a une route par défaut), puis écrite dans `/run/calaos/primary_if`. Elle sert à la fois de `PRIMARY_INTERFACE` et d'`OT_INFRA_IF`.
- **Fabric.** `/mnt/calaos/matter` doit être **inclus dans les sauvegardes**. Le perdre oblige à recommissionner tous les appareils. Deux serveurs ne doivent jamais utiliser le même répertoire.

### Parcours v1 : ajouter un appareil
1. Dans l'installer, Devices → « Ajouter un appareil Matter », ou depuis la page Matter de la webapp.
2. Saisir le code. Dans la webapp, on peut aussi scanner le QR via `getUserMedia` en HTTPS, ce qui suppose d'avoir accepté le certificat une fois.
3. Une barre de progression suit les étapes.
4. La liste des endpoints s'affiche avec un type suggéré, et l'utilisateur coche ceux qu'il veut.
5. Les IOs typés ou les templates sont créés.

Pour un appareil déjà présent dans Apple Home : dans Apple Home, « Activer le mode d'appairage », puis copier le code dans Calaos.

**Couverture v1** : appareils déjà sur le réseau IP (Wi-Fi, Ethernet) et Thread en multi-admin via le TBR de l'utilisateur. Un appareil Wi-Fi neuf, qui n'a jamais été appairé, doit d'abord être appairé avec l'app du fabricant ou Apple/Google Home, puis partagé avec Calaos.

### Calaos comme bridge Matter (phase 3)
- Un second process matter.js dans le conteneur `calaos-matter`, avec son propre nœud, son propre stockage et le **port 5541**. Le contrôleur n'écoute pas sur 5540, donc pas de conflit.
- Il consomme `get_home` et les événements d'IO via le WS, avec un **token `login_service`** généré comme `mcp_service_token`.
- Il expose les IOs visibles : lumières (on/off, variateur, RGB), volets, température, interrupteurs, scénarios (comme interrupteurs). Il ne fait rien pour les strings ni l'analogique générique.
- Plafond de 150 endpoints, avec un avertissement au-delà de 50 (Alexa). La documentation explique l'avertissement « non certifié ».
- **Il remplace calaos-homekit**, qui n'était qu'un PoC instable (décision de Raoul).

### Thread natif et BLE (phase 4)
- **Paquet `calaos-otbr`** : `openthread/border-router`, `--cap-add=NET_ADMIN`, `--device` pour le RCP et `/dev/net/tun`.
- **Arbitrage du dongle.** On exclut le port `serial.port` de zigbee2mqtt et on sélectionne par `/dev/serial/by-id` (VID:PID des ZBT-1/2, nRF52840, ESP32-H2). Si le seul candidat est le dongle Zigbee, on refuse.
- **Réseau Thread.** Créer un réseau par défaut. Pour rejoindre un réseau Apple ou Google, il faut le dataset du téléphone (Android Thread API, iOS ThreadNetwork) ou l'ePSKc (Thread 1.4).
- **BLE.** Un proxy BLE côté téléphone (`--ble-proxy` de matterjs-server), sans bluez sur la box. Il faut aussi le scan de QR dans calaos_mobile (QtMultimedia). Le commissioning BLE de matterjs-server ne couvre aujourd'hui que le Wi-Fi.

## Phasage

| Phase | Contenu | Effort |
|---|---|---|
| 1. Socle OS | pkgdeb `calaos-matter` à la demande, sysctls, `primary_if`, ajout dans `init_calaosfs`, fabric dans les sauvegardes, 5580 en loopback, doc | ≈ 1 sem. |
| 2. Intégration et commissioning v1 | côté calaos_base : ExternProc, `MatterCtrl`, IOs typés, template `matter_thermostat`, verbes, événements, tests, docs (≈ 4,5 sem.) ; `DialogMatter` dans l'installer (≈ 1,5 sem.) ; page Matter de la webapp avec code et QR (≈ 1 sem.) | ≈ 7 sem. |
| 3. Bridge Matter | sidecar matter.js sur 5541, token `login_service`, remplacement de calaos-homekit | ≈ 3 sem. |
| 4. Thread natif et BLE | `calaos-otbr`, arbitrage du dongle, proxy BLE, QR mobile, import du dataset | 2 à 3 sem. |

## Ce qu'on ne fait pas
- Réexporter Matter en MQTT, ou tout sidecar Node pour l'ingestion.
- Une classe `MatterClimate`.
- BLE et Thread natif en v1.
- Remonter les événements Matter vers Lua.
- Une interface d'administration multi-fabric.
- La certification CSA.
- Le scan de QR sur mobile en v1.
- Créer automatiquement des IOs sans que l'utilisateur choisisse la pièce et le type.
- Installer avahi.

## Décisions de Raoul (2026-10-18)
1. **mosquitto anonyme sur 0.0.0.0 et zigbee2mqtt `permit_join: true`** (`pkgdebs/mosquitto/mosquitto_pre:24-25`, `pkgdebs/zigbee2mqtt/zigbee2mqtt_pre:19`) : **accepté pour le moment.** Ce n'est pas lié à Matter, puisque Matter ne passe pas par MQTT. À reprendre dans un chantier sécurité séparé.
2. **Version de matterjs-server et suivi de son API WebSocket (bêta)** : **à définir.** Il faut au minimum :
   - figer un tag précis dans le `.source` du paquet, pas `stable` ;
   - lancer un test d'intégration en CI contre ce tag (commissioning d'un appareil simulé `chip-all-clusters-app` ou matter.js, puis lecture et écriture d'attributs) ;
   - désigner quelqu'un qui relit le changelog avant chaque montée de version.
3. **Le bridge Matter remplace calaos-homekit** une fois livré. calaos-homekit n'était qu'un PoC instable.
4. **Source de vérité du nom des appareils** (NodeLabel Matter ou nom de l'IO Calaos) : **à trancher à l'implémentation.**
5. **Fabric sur une box neuve** (restaurer `/mnt/calaos/matter` ou recommissionner tout) : **à trancher à l'implémentation.**

## Risques
- matterjs-server est en bêta, et son API WebSocket peut changer (voir la décision 2).
- La mémoire : 200 à 300 Mo pour le contrôleur, plus le bridge.
- IPv6 et mDNS avec `--network=host` : interfaces multiples, réseaux Wi-Fi maillés qui filtrent le multicast.
- Le suivi de commissioning reste grossier.
- Sans BLE, l'onboarding d'un appareil neuf dépend d'une app tierce.
- La perte du fabric.
- Les bizarreries des fabricants.

## Vérification (à l'implémentation)
- **CI.** Un appareil Matter simulé (`chip-all-clusters-app` ou un appareil matter.js) dans un conteneur, sur le tag figé : commissioning, `matter_nodes`, lecture et écriture OnOff/Level/WindowCovering, événements Switch, passage en indisponible puis retour.
- **calaos_base.** Tests de `MatterCtrl` avec une ExternProc simulée : reconnexion, ré-enregistrement, `status_info`, et refus du scope service.
- **Webapp.** Page Matter sur le mock-server, avec la progression de commissioning en e2e.
- **Recette sur la box.** Un appareil Wi-Fi et un appareil Thread ajoutés en multi-admin depuis Apple Home. Puis, en phase 3, Calaos ajouté dans Apple Home comme bridge.
