# HTTP / WebSocket API — JsonApi

## Vue d'ensemble

Le serveur expose une **API JSON** sur HTTP et WebSocket (port par défaut 5454). Les clients UI (application mobile, web) utilisent cette API pour :
- Lire l'état des IOs
- Envoyer des commandes
- S'abonner aux événements en temps réel (via WebSocket)
- Gérer la configuration

---

## Architecture réseau

```
HttpServer (port 5454)
  └── accepte connexions TCP (via uvw::TcpHandle)
      └── WebSocket (upgrade HTTP → WS)
          ├── JsonApiHandlerHttp   (requêtes HTTP classiques)
          ├── JsonApiHandlerWS     (connexion persistante WS)
          └── McpProxyHandler      (chemin /mcp → sidecar calaos_mcp, voir doc/15)
```

`WebSocket::ProcessData` renifle la première ligne de requête : si elle cible
`/mcp` (ou `/mcp/...`), la connexion bascule en mode reverse-proxy brut vers le
socket Unix du sidecar `calaos_mcp` au lieu d'être traitée comme une requête
JsonApi. Tout passe donc par le **même port 5454** (un seul port à exposer
derrière le reverse proxy HTTPS). Voir [15_mcp_server.md](15_mcp_server.md).

---

## HttpServer

**Fichier :** [src/bin/calaos_server/HttpServer.h](../src/bin/calaos_server/HttpServer.h)

Singleton. Lance le serveur TCP et crée un objet `WebSocket` pour chaque connexion entrante.

```cpp
HttpServer::Instance(5454);  // démarrage
HttpServer::Instance().disconnectAll();  // ferme toutes les connexions
```

---

## WebSocket / HttpClient

**Fichier :** [src/bin/calaos_server/WebSocket.h](../src/bin/calaos_server/WebSocket.h)

Gère le handshake WebSocket et le parsing HTTP/WebSocket sur une connexion TCP. Crée le handler approprié (`JsonApiHandlerHttp` ou `JsonApiHandlerWS`) selon le type de requête.

**Fichier :** [src/bin/calaos_server/HttpClient.h](../src/bin/calaos_server/HttpClient.h)

Représente une connexion client HTTP. Délègue au `JsonApi`.

---

## JsonApi

**Fichier :** [src/bin/calaos_server/JsonApi.h](../src/bin/calaos_server/JsonApi.h)

Classe principale de l'API. Contient tous les builders de réponses JSON.

### Méthodes de construction JSON

#### Home / IOs

```cpp
json_t *buildJsonHome();        // liste complète rooms + IOs
json_t *buildFlatIOList();      // liste plate de tous les IOs
json_t *buildJsonRoomIO(Room*); // IOs d'une pièce
void buildJsonIO(IOBase*, json_t*); // sérialise un IO
```

#### États

```cpp
void buildJsonState(vector<string> iolist, lambda); // état d'une liste d'IOs
void buildJsonStates(Params, lambda);               // états multiples
void buildQuery(Params, lambda);                    // requête générale
bool decodeSetState(Params&);                       // applique un set_value
```

#### Paramètres

```cpp
json_t *buildJsonGetParam(Params);   // lire un paramètre IO
json_t *buildJsonSetParam(Params);   // modifier un paramètre IO
json_t *buildJsonDelParam(Params);   // supprimer un paramètre IO
```

#### Plages horaires

```cpp
json_t *buildJsonGetTimerange(Params);
json_t *buildJsonSetTimerange(json_t*);
```

#### Scénarios

```cpp
json_t *buildAutoscenarioList(json_t*);
json_t *buildAutoscenarioGet(json_t*);
json_t *buildAutoscenarioCreate(json_t*);
json_t *buildAutoscenarioDelete(json_t*);
json_t *buildAutoscenarioModify(json_t*);
json_t *buildAutoscenarioAddSchedule(json_t*);
json_t *buildAutoscenarioDelSchedule(json_t*);
```

#### Audio

```cpp
void audioGetDbStats(json_t*, lambda);
void audioDbGetAlbums(json_t*, lambda);
void audioDbGetArtists(json_t*, lambda);
void audioDbGetRadios(json_t*, lambda);
void audioDbGetSearch(json_t*, lambda);
void decodeGetPlaylist(Params, lambda);
```

#### Caméras

```cpp
json_t *buildJsonCameras();
```

#### Logs / Notifications

```cpp
void buildJsonEventLog(Params, lambda);
bool registerPushToken(Params);
```

#### Status IO

```cpp
json_t *buildJsonStatusInfo(IOBase*);  // batterie, connexion, signal, etc.
```

---

## Format de l'API (exemples)

### GET home (liste des pièces et IOs)

**Requête :**
```json
{"action": "get_home"}
```

**Réponse :** `home` est un **tableau** de pièces ; chaque pièce porte ses IOs
sous la clé `items` (pas `ios`). Toutes les valeurs scalaires (dont `state` et
`rw`) sont des **chaînes** JSON.
```json
{
  "home": [
    {
      "type": "salon",
      "name": "Salon",
      "hits": "0",
      "items": [
        {
          "id": "id-abc",
          "name": "Lumière principale",
          "type": "WagoOutputLight",
          "var_type": "bool",
          "visible": "true",
          "rw": "true",
          "gui_type": "light",
          "state": "false",
          "io_type": "inout"
        }
      ]
    }
  ],
  "cameras": [],
  "audio": []
}
```

Champs IO renvoyés par `buildJsonIO` (présents seulement s'ils existent sur
l'IO) : `id`, `name`, `type`, `hits`, `var_type` (`bool`/`float`/`string`),
`visible`, `chauffage_id`, `rw`, `unit`, `gui_type`, `state`, `auto_scenario`,
`step`, `io_type` (`input`/`output`/`inout`), `io_style`, `value_warning`, plus
un objet `status_info` (batterie, connectivité…) le cas échéant.

### SET state (commande IO)

**Requête :**
```json
{"action": "set_state", "id": "id-abc", "value": "true"}
```

**Réponse :**
```json
{"success": "true"}
```

### GET state

**Requête :** `items` est un tableau d'**identifiants** (chaînes).
```json
{"action": "get_state", "items": ["id-abc", "id-def"]}
```

**Réponse :** un dictionnaire plat `{id: valeur}` (et non une liste d'objets).
```json
{"id-abc": "true", "id-def": "21.5"}
```

### Événements WebSocket

Le serveur pousse des événements aux clients WebSocket connectés :

```json
{
  "type": "io_changed",
  "id": "id-abc",
  "value": "true"
}
```

Types d'événements (voir `EventManager`) :
- `io_changed` — changement d'état d'un IO
- `io_added` / `io_deleted`
- `room_added` / `room_deleted` / `room_changed`
- `timerange_changed`
- `scenario_added` / `scenario_deleted` / `scenario_changed`
- `audio_song_changed` / `audio_status_changed` / `audio_volume_changed`
- `push_notification`
- `io_status_changed` — changement de statut (batterie, etc.)

---

## PollListenner

**Fichier :** [src/bin/calaos_server/PollListenner.h](../src/bin/calaos_server/PollListenner.h)

Mécanisme de polling pour les clients HTTP classiques (non-WebSocket). Permet d'attendre des événements sans polling actif (long polling).

---

## UDPServer

**Fichier :** [src/bin/calaos_server/UDPServer.h](../src/bin/calaos_server/UDPServer.h)

Serveur UDP pour la découverte du serveur Calaos sur le réseau local (broadcast).

---

## Authentification

### HTTP (requête unique)
Chaque requête HTTP porte les champs `cn_user` / `cn_pass` dans le corps JSON.
Les credentials sont stockés dans `local_config.xml` (`calaos_user` /
`calaos_password`) et modifiables via `JsonApi::changeCredentials()`.

### WebSocket — `login` (session admin)
Premier message attendu sur une connexion `/api` :
```json
{"msg": "login", "msg_id": "1", "data": {"cn_user": "user", "cn_pass": "pass"}}
```
Réponse `{"msg": "login", "data": {"success": "true"}}`. La session reste
authentifiée pour toute la durée de la connexion ; en cas d'échec la connexion
est fermée.

### WebSocket — `login_service` (session restreinte, sidecar MCP)
Le sidecar `calaos_mcp` ne se connecte pas en admin : il utilise un compte de
service à portée restreinte (`serviceScope`). Voir
[15_mcp_server.md](15_mcp_server.md).
```json
{"msg": "login_service", "msg_id": "1", "data": {"token": "<mcp_service_token>"}}
```
Le token est comparé à l'option `mcp_service_token` de `local_config.xml`
(`McpServerManager::getServiceToken()`). En session `serviceScope`, les actions
de configuration/sensibles sont refusées (`scopeDenied`) : `set_param`,
`del_param`, `audio_db`, `set_timerange`, `eventlog`, `register_push`,
`settings`.

### `get_mcp_info` (découverte du token MCP)
Action JsonApi (HTTP ou WS, session admin) qui renvoie l'URL et le Bearer token
à donner à un client MCP (Claude Desktop, etc.) :
```json
// Requête
{"action": "get_mcp_info", "cn_user": "user", "cn_pass": "pass"}
// Réponse
{"url_path": "/mcp",
 "token": "<mcp_token>",
 "hint": "Use token as Bearer in Authorization header. Append /mcp to your Calaos HTTPS base URL."}
```

---

## HttpClient (utilitaire)

**Fichier :** [src/lib/UrlDownloader.h](../src/lib/UrlDownloader.h)

Classe utilitaire pour télécharger des ressources HTTP depuis le serveur vers des URLs externes (utilisée par les drivers Web, les capteurs Hue, etc.). Basée sur libcurl avec l'event loop libuv.
