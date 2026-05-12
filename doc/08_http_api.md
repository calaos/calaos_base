# HTTP / WebSocket API — JsonApi

## Vue d'ensemble

Le serveur expose une **API JSON** sur HTTP et WebSocket (port par défaut 4444). Les clients UI (application mobile, web) utilisent cette API pour :
- Lire l'état des IOs
- Envoyer des commandes
- S'abonner aux événements en temps réel (via WebSocket)
- Gérer la configuration

---

## Architecture réseau

```
HttpServer (port 4444)
  └── accepte connexions TCP (via uvw::TcpHandle)
      └── WebSocket (upgrade HTTP → WS)
          ├── JsonApiHandlerHttp   (requêtes HTTP classiques)
          └── JsonApiHandlerWS     (connexion persistante WS)
```

---

## HttpServer

**Fichier :** [src/bin/calaos_server/HttpServer.h](../src/bin/calaos_server/HttpServer.h)

Singleton. Lance le serveur TCP et crée un objet `WebSocket` pour chaque connexion entrante.

```cpp
HttpServer::Instance(4444);  // démarrage
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

**Réponse :**
```json
{
  "home": [
    {
      "name": "Salon",
      "type": "living",
      "items": [
        {
          "id": "id-abc",
          "name": "Lumière principale",
          "type": "WagoOutputLight",
          "gui_type": "light",
          "var_type": "bool",
          "val": "true",
          "enabled": "true"
        }
      ]
    }
  ]
}
```

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

**Requête :**
```json
{"action": "get_state", "items": ["id-abc", "id-def"]}
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

## Authentification HTTP

L'API HTTP utilise une authentification basique (username/password) configurable via `local_config.xml`. Les credentials peuvent être changés via `JsonApi::changeCredentials()`.

---

## HttpClient (utilitaire)

**Fichier :** [src/lib/UrlDownloader.h](../src/lib/UrlDownloader.h)

Classe utilitaire pour télécharger des ressources HTTP depuis le serveur vers des URLs externes (utilisée par les drivers Web, les capteurs Hue, etc.). Basée sur libcurl avec l'event loop libuv.
