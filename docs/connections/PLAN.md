# Connexions partagées dans io.xml, MQTT via WebSocket et durcissement de mosquitto

État : plan, **non lancé**. Rien ne s'implémente sans la décision de Raoul. Analyse faite le 2026-10-21.

## Contexte
- **A.** Aujourd'hui, chaque IO porte sa propre config de transport (MQTT, Wago, etc.), ce qui la répète partout. Le but est d'avoir dans io.xml un arbre de configs globales, appelées « connexions ». Les IOs les réutilisent et peuvent en surcharger certains champs. On peut définir plusieurs connexions d'un même type, et chaque IO choisit la sienne.
- **B.** L'installer se connecte au mosquitto de calaos-server à travers un WebSocket vers calaos-server, au lieu d'ouvrir mosquitto au LAN.

Plans liés :
- `docs/admin/PLAN.md` : contrôle d'Origin (CB1), gate admin, `config get` réservé à l'admin.
- `docs/remote/MLSH_STUDY.md` : l'accès distant passe par le port 443.
- Client MQTT maison de l'installer : branche `feat/inhouse-mqtt`.

## Partie A : les connexions

### Inventaire

| Famille | Params répétés par IO | Partage actuel |
|---|---|---|
| MQTT | `host` (127.0.0.1 par défaut), `port` (1883), `keepalive`, `user`, `password` (`MqttCtrl.cpp:456-461`, résolus dans `MqttWire.h:209-222`) | `MqttBrokersList::get_ctrl` (`MqttBrokerslist.cpp:29-47`) |
| Wago | `host`, `port` (502), `wago_841` (`WagoIOBase.h:84-102, 270`) | `WagoMap::Instance(host,port)` (`WagoMap.cpp:141-157`) |
| KNX | `host` (`KNXBase.cpp:38`) | `KNXCtrl::Instance(host)` |
| OLA | `universe` | une instance par univers |
| OneWire | `ow_args` | une instance par jeu d'arguments |
| Roon | `host`, `port` | singleton : le premier hôte lu gagne, sans message (`RoonPlayer.cpp:93-96`) |
| Squeezebox | `host`, `port_cli`, `port_web` | une connexion par lecteur |
| AVR | `host` | `AVRManager::getReceiver(host)` |
| Hue | `host`, `api` | aucun, HTTP par IO |
| Reolink | `hostname`, `username`, `password` | sidecar singleton, un enregistrement par caméra |
| IPCam | Synology : `url`, `username`, `password` ; Foscam : `host`, `port`, identifiants | aucun |
| Web IO | `url` | c'est une ressource, pas une connexion : hors périmètre |
| Device, ProxyClimate | rien en propre : ce sont leurs IOs MQTT possédés qui portent le transport | le template ESPHome recopie `mqtt_host`, `mqtt_port`, `mqtt_user` et `mqtt_password` dans chaque IO (`DeviceTemplates.cpp:203-219`) |

Les `*_status_topic` (batterie, connecté, signal…) dépendent du nœud, pas du broker. Ils restent dans l'IO.

### Format actuel
- Les params d'un IO sont les attributs de sa balise (`IOFactory.cpp:30-36`). Les devices utilisent `<calaos:param>` et `<calaos:link>` (`Device.cpp:81-98`).
- `format="2"` n'est écrit que s'il existe un device. Le serveur **ne lit pas** `format` et ignore les enfants inconnus de `<calaos:home>` (`CalaosConfig.cpp:285-289`).
- L'installer conserve les enfants inconnus (`projectmanager.cpp:656-675`) et ouvre en lecture seule un format plus récent (`:1194-1207`).
- Décision 16 : `io_format` est déclaré dans `config put` et annoncé par `config get` (`JsonApiHandlerHttp.cpp:48-98, 826, 856`).

### Design
```xml
<calaos:home format="3">
  <calaos:connections>
    <calaos:connection id="mqtt_main" type="mqtt" name="Broker box" default="true">
      <calaos:param name="host" value="127.0.0.1"/>
      <calaos:param name="password" value="…"/>
    </calaos:connection>
  </calaos:connections>
  <calaos:room …>
    <calaos:input type="MqttInputSwitch" id="…" connection="mqtt_main" topic_sub="…"/>
```
- **Forme XML.** Attributs `id`, `type`, `name` et `default`, avec des réglages en `<calaos:param>`, comme pour les devices. Pas de collision avec les attributs réservés, et l'écriture des params de `DeviceEdit` est réutilisable. La section est placée avant les rooms : `LoadConfigIO` fait une première passe, car les constructeurs d'IO lisent leurs params immédiatement.
- **Résolution** dans `IOFactory::CreateIO`, avant la construction. L'ordre est : valeur de l'IO, puis valeur de la connexion, puis défaut du `connectionDoc`. L'IO retient la liste des clés héritées. `SaveToXml` ne les réécrit pas, et un `set_param` sur une clé héritée la transforme en surcharge.
- **Choix de la connexion.** L'IO utilise `connection=` s'il est présent, sinon la connexion `default="true"` de son type. Il n'y a **pas de défaut implicite** quand une seule connexion existe : en ajouter une deuxième changerait en silence le comportement des IOs. Une IO au format 2 qui porte encore `host` garde son comportement actuel.
- **Surcharges.**
  - Les topics et les `*_path` restent propres à chaque IO.
  - Les champs d'identité du contrôleur partagé (`host`, `port`, `user`, `password`, `keepalive`, et pour Wago `host`, `port` et `wago_841`) ne se surchargent pas : en changer un revient à décrire une autre connexion. Une IO qui porte à la fois `connection=` et `host=` provoque une erreur de chargement.
- **Validation.** Un id inconnu, un type incompatible ou un champ requis manquant déclenchent `cErrorDom("connection")`. L'IO est alors gardée opaque et inactive, comme un device inconnu. Elle n'est pas perdue à la sauvegarde et ne se connecte jamais à 127.0.0.1 par accident.
- **IODoc.**
  - Nouvelle clé `connections` dans `io_doc.json`, qui décrit les params de chaque type avec un drapeau `secret`, sur le modèle de la clé `devices`.
  - `MqttCtrl::commonDoc` est scindé en deux : `connectionDoc` pour le transport, et les params propres à l'IO.
  - Chaque doc d'IO déclare son `connection_type`.
- **Secrets.**
  - Le mot de passe sort des `Params` de l'IO : `get_ctrl(Connection)` le lit dans le registre.
  - `config get` contient toujours les secrets, puisque l'installer doit les renvoyer, et il sera réservé à l'admin (plan admin). Un masquage avec restauration au `put` est possible, mais pas en v1.
- **Exécution.**
  - MQTT : la clé devient `conn:<id>`.
  - Wago : `WagoMap` reste indexé par host:port, car il n'y a qu'un seul automate.
  - Deux connexions sur le même host:port donnent un avertissement.
  - Un `config put` redémarre le serveur, donc rien n'est à reconnecter à chaud.
- **Format 3.**
  - Il n'est écrit que s'il existe des connexions. `config get` annonce alors `io_format: 3`, et un client qui déclare moins de 3 est refusé.
  - ⚠️ Un vieux serveur ne refuse rien : il ignore la section, et ses IOs MQTT retombent sur 127.0.0.1 sans identifiants. Le vrai garde-fou est la sonde de l'installer (INST-3).
- **Migration dans l'installer, jamais dans le serveur.**
  - Le serveur lit les formats 2 et 3 sans migrer. Sinon, sa première sauvegarde passerait la box en format 3 et mettrait un vieil installer en lecture seule.
  - L'installer propose « Factoriser les connexions ». Il regroupe par n-uplet complet, en considérant `""` égal à `127.0.0.1`.
  - Les ids sont déterministes : `mqtt_<host>_<port>`, avec un suffixe `_2` quand les identifiants diffèrent. Le groupe le plus peuplé devient la connexion par défaut.
  - Un diff est montré avant d'écrire.
- **Installer.**
  - Modèle `Connection`, avec lecture et écriture, et passage à `kPutIoFormat` 3.
  - Éditeur de connexions.
  - Dans `WidgetIOProperties` : une combo « Connexion » et les seuls champs surchargeables.
  - `MqttBroker::ofProject()` est remplacé par la connexion par défaut, dans les dialogues ESPHome, Home Assistant et Zigbee2MQTT.
  - Nouveau type de champ `connection` dans les templates, avec un bump de leur `FORMAT`.
- **Web app et mobile** : aucun changement.
- **Tests.**
  - Golden `tests/core/golden` : une config au format 2 et sa migration attendue, plus un aller-retour au format 3.
  - Précédence des valeurs, connexion cassée qui reste opaque, clés héritées non réécrites.
  - `get_param` qui refuse un secret.
  - `MqttConfigTransport_test` étendu.

### Défauts trouvés en passant (lot A0, à faire en premier)
1. **Clé MQTT** (`MqttBrokerslist.cpp:39`) : elle est construite avec `p["host"]` au lieu de la variable `broker` déjà calculée. `host=""` et `host="127.0.0.1"` lancent donc deux sidecars pour le même broker. Comme la clé ignore aussi `user` et `password`, deux IOs qui partagent host:port avec des identifiants différents héritent en silence de ceux du premier.
2. **`get_param`** (`JsonApi.cpp:1221-1236`) renvoie n'importe quel param, **mot de passe compris**, à tout client connecté et à Lua. `get_home` n'est pas concerné : il ne projette qu'une liste blanche.
3. **pkgdebs** : le postinst du paquet `calaos-mosquitto` s'appelle `calaos-grafana.postinst`. Il ne s'exécute probablement jamais.

### Lots A
| Lot | Dépôt | Effort |
|---|---|---|
| A0 : clé MQTT, `get_param` et secrets, postinst mosquitto | base, pkgdebs | 0,5 j |
| A1 : registre, XML, résolution, opaque, IODoc, io_format 3 | base | 4 à 6 j |
| A2 : MQTT et Wago par connexion, puis KNX, AVR, Roon, Squeezebox, Hue, Synology | base | 1 à 2 j, puis 2 j |
| A3 : modèle, éditeur, formulaire, migration, découvertes | installer | 6 à 9 j |
| A4 : templates avec champ `connection` | base et installer | 1 à 2 j |

Risques :
- une migration qui perd un mot de passe (parade : regroupement par n-uplet complet et diff montré avant d'écrire) ;
- deux connexions sur le même host:port ;
- une IO opaque encore référencée par des règles ;
- une config au format 3 poussée sur un vieux serveur par un autre outil que l'installer.

## Partie B : MQTT via WebSocket et durcissement de mosquitto

### B1. Endpoint `/api/mqtt` dans calaos_server
- **Point d'entrée.** On ne réutilise pas `McpProxyHandler`, qui détourne le TCP brut avant le parsing HTTP. On suit la branche RemoteUI, qui dispatche dans `checkHandshakeRequest` (`WebSocket.cpp:262-300`). calaos_server termine lui-même le WebSocket et relaie les trames binaires vers un `uvw::TcpHandle`, avec une borne de pré-connexion et du backpressure. N'importe quel broker joignable en TCP devient ainsi accessible.
- **Sous-protocole.** Le serveur répond `Sec-WebSocket-Protocol: mqtt`, ce qui n'est pas géré aujourd'hui.
- **Authentification dans le CONNECT MQTT.**
  - Le client met son login Calaos dans `username` et `password`. Le serveur le vérifie avec `LoginThrottle`.
  - Le serveur **réécrit** ensuite le CONNECT avec les identifiants de la connexion. Les secrets du broker ne quittent jamais la box, et MQTT Explorer ou MQTT.js fonctionnent tels quels.
  - Plus tard, le gate admin passera par une session admin utilisée comme mot de passe. B1 n'en dépend pas.
- **Choix du broker** : `?connection=<id>` dans l'URL, sinon la connexion par défaut.
- **Origin.** Tant que CB1 n'existe pas, tout handshake qui porte un `Origin` est refusé, ce qui ne laisse passer que les clients natifs. Ensuite, on applique la liste d'identités de CB1.
- **Lecture seule, en option.** Comme le CONNECT est déjà lu, découper les paquets du client vers le broker représente environ 150 lignes plus les tests. Un PUBLISH est rejeté par un DISCONNECT, pour ne pas laisser un QoS 1 en attente. On se limite à MQTT 3.1.1 en v1.
- **Accès distant gratuit**, puisque haproxy envoie déjà tout le port 443 vers calaos (DDNS aujourd'hui, MLSH demain). Ping WebSocket toutes les 45 s au plus.

### B2. Installer
- On extrait une interface `MqttTransport` du client maison, à partir du point d'extension `openTransport()`/`onTransportReady()`, avec deux implémentations : TCP et `QWebSocket`.
- Le module Qt WebSockets est ajouté au `.pro`, au flatpak et aux scripts Windows.
- Les dialogues de découverte proposent « via le serveur Calaos » par défaut, et « broker direct » en repli.

### B3. Mosquitto (`pkgdebs/mosquitto/mosquitto_pre:17-26`)
- **Aujourd'hui** : `listener 1883 0.0.0.0` avec `allow_anonymous true`. zigbee2mqtt, hue2mqtt et calaos_server visent 127.0.0.1:1883.
- **Proposition** :
  - `per_listener_settings true` ;
  - un listener anonyme sur 127.0.0.1:1883 ;
  - un listener LAN authentifié par `password_file`.
- **Piège Linux** : `0.0.0.0:1883` et `127.0.0.1:1883` ne cohabitent pas. Le listener LAN doit donc être lié à l'IP de l'interface, que `mosquitto_pre` régénère à chaque démarrage, ou passer par `bind_interface`. **À valider sur une box.**
- **Mot de passe des appareils.**
  - Il est généré par `mosquitto_pre` et stocké en 0600 dans `/mnt/calaos/mosquitto/devices.secret`, et sous forme hachée dans le `passwd`.
  - Il est affiché par l'installer et la webapp, à l'admin seulement.
  - Il est injecté en `!secret` dans le YAML ESPHome des templates.
- **Migration.**
  - Une nouvelle installation est fermée d'emblée.
  - Une installation existante est en opt-in. On commence par une phase d'audit, avec `password_file` actif et `allow_anonymous true` : les logs montrent quels appareils se connectent encore sans identifiants. On bascule quand il n'en reste plus.
- **Lien avec A** : calaos_server reste anonyme en loopback. Une connexion qui vise le LAN porte les identifiants des appareils.

### Lots B
| Lot | Dépôt | Effort |
|---|---|---|
| B1 : endpoint `/api/mqtt` | base | 3 à 5 j |
| B2 : transport WebSocket | installer | 2 à 3 j |
| B3 : mosquitto (pkgdebs, interface, doc) | pkgdebs, installer, webapp, doc | 2 à 3 j |

B1 fonctionne sans CB1, puisqu'il refuse tout Origin, et sans le gate admin. B3 suppose que l'installer n'a plus besoin des identifiants LAN, c'est-à-dire que B2 est fait.

## Ordre recommandé
1. A0
2. A1 et A2 (base)
3. B1, qui s'appuie sur le registre de connexions
4. A3, A4 et B2 ensemble, car ils touchent les mêmes dialogues de découverte
5. B3

## Questions ouvertes pour Raoul
1. Un défaut implicite quand une seule connexion existe ? Recommandation : non.
2. La migration se fait automatiquement à l'ouverture du projet, ou par une action explicite ?
3. Les status topics : des connexions chaînées (`parent=`) pour un nœud, ou laissés aux devices ?
4. Reolink et IPCam : une connexion « compte » sans hôte, ou rien ?
5. Masquer les secrets dans `config get` dès la v1 ?
6. Listener LAN de mosquitto : lié à l'IP régénérée à chaque démarrage, ou `bind_interface` ?
7. Le mode lecture seule du tunnel `/api/mqtt` en v1 ?
8. Roon multi-cœurs : corriger le singleton ?
