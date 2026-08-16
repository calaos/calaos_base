# HTTP / WebSocket API — JsonApi

## Vue d'ensemble

Le serveur expose une **API JSON** sur HTTP et WebSocket (port par défaut 5454).
Les clients UI (application mobile, web) utilisent cette API pour lire l'état des
IOs, envoyer des commandes, gérer la configuration et recevoir les événements
temps réel.

**Les deux transports ne parlent pas exactement la même langue.** Ce document
décrit la forme réelle du fil, telle qu'elle sort du code. Deux règles
gouvernent tout le reste :

1. **HTTP** met ses arguments **à la racine** du corps de requête, sous la clé
   `action`. **WebSocket** met ses arguments **sous `data`**, sous la clé `msg`.
   Se tromper de transport ne provoque **aucune erreur** : on reçoit `{}`, une
   enveloppe vide, ou rien du tout.
2. **Tout scalaire sur le fil est une chaîne JSON.** Une seule exception dans
   toute l'API (`eventlog`), documentée plus bas.

Les événements poussés (WebSocket) et leur équivalent HTTP (`poll_listen`) sont
documentés dans [10_events_notifications.md](10_events_notifications.md).

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

### Points d'entrée exacts

| Transport | Chemin | Méthode | Handler |
|---|---|---|---|
| HTTP | `/api` ou `/api.php` | POST (corps JSON) ou GET (paramètres d'URL) | `JsonApiHandlerHttp` |
| WebSocket | `/api` | GET + upgrade | `JsonApiHandlerWS` |

Tout autre chemin est rejeté en 404 avant d'atteindre le JsonApi
([HttpClient.cpp:458-470](../src/bin/calaos_server/HttpClient.cpp)).
`/api/v2` et `/api/v3…` passent le filtre de chemin mais n'ont **pas** de
handler JsonApi : le serveur logge `API version not implemented` et **ne répond
rien du tout** ([HttpClient.cpp:650-659](../src/bin/calaos_server/HttpClient.cpp)).
Avant d'arriver au JsonApi, `HttpClient::handleJsonRequest()` propose l'URL au
handler RemoteUI puis au handler OTA ; le JsonApi n'est que le fallback
([HttpClient.cpp:611-673](../src/bin/calaos_server/HttpClient.cpp)).

Côté WebSocket, seuls `/api`, `/api/v3/remote_ui/ws` et `/echo` sont acceptés ;
seul `/api` crée un `JsonApiHandlerWS` — les deux autres chemins sont routés
ailleurs par le `else` de
([WebSocket.cpp:258-279 et :335-339](../src/bin/calaos_server/WebSocket.cpp)).

---

## L'asymétrie fondamentale : où vivent les arguments

Le dispatch HTTP décode le corps JSON **à plat** dans `jsonParam` et chaque
`process*()` y lit ses arguments
([JsonApiHandlerHttp.cpp:96-99, 140-215](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

Le dispatch WebSocket décode `jsonRoot` (pour `msg` / `msg_id`) **et**
séparément l'objet `data` dans `jsonData` ; chaque `process*()` reçoit
`jsonData` ou le `json_t *jdata` brut
([JsonApiHandlerWS.cpp:108-112, 116-229](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).

Même opération, deux formes de requête (dérivé des deux dispatchs) :

```json
// HTTP — POST /api ; arguments à la RACINE, avec les credentials
{"action": "set_state", "cn_user": "user", "cn_pass": "pass",
 "id": "io_0", "value": "true"}
```

```json
// WebSocket — arguments sous "data", après un "login" réussi
{"msg": "set_state", "msg_id": "7",
 "data": {"id": "io_0", "value": "true"}}
```

### Se tromper de forme ne produit pas d'erreur

C'est le piège principal de cette API : aucun des deux dispatchs ne valide la
forme. Les conséquences observables (dérivées du code, pas capturées) :

| Erreur du client | Ce que fait le serveur | Ce que reçoit le client |
|---|---|---|
| Requête WS avec les arguments à la racine (pas de `data`) | `jsonData` reste vide, `jdata` est `nullptr` | `get_state` / `get_io` : enveloppe nue `{"msg":"get_state","msg_id":"7"}` sans clé `data` ([JsonApiHandlerWS.cpp:248-252, 307-311](../src/bin/calaos_server/JsonApiHandlerWS.cpp)) |
| idem, sur `get_param` / `set_param` / `del_param` | `id` vide → IO introuvable | `{"error":"wrong io/param"}` |
| idem, sur `set_state` | `id` vide → échec | `{"success":"false"}` |
| idem, sur `audio` / `audio_db` | `jansson_string_get(nullptr, …)` renvoie `""` | `{"error":"unkown audio_action"}` |
| idem, sur `autoscenario` | `type` vide, aucune branche ne correspond | **rien du tout** ([JsonApiHandlerWS.cpp:474-491](../src/bin/calaos_server/JsonApiHandlerWS.cpp)) |
| idem, sur `query` / `get_states` | `Exists("id")` faux | `{}` |
| Requête HTTP avec les arguments sous `data` | `jansson_decode_object()` n'aplatit **que les scalaires** : un objet imbriqué devient une chaîne vide ([Jansson_Addition.h:93-111](../src/lib/Jansson_Addition.h)) | `jsonParam["data"] == ""` et **aucun** argument utile → mêmes réponses vides que ci-dessus |

Aucun de ces cas ne renvoie de message d'erreur exploitable. Un client qui
reçoit `{}` ou une enveloppe nue doit d'abord vérifier qu'il n'a pas inversé les
deux formes.

**Le cas `audio` est le plus trompeur, et il est capturé.** Une requête bien
formée mais dans la forme de l'autre transport reçoit **exactement le même
document** qu'une sous-action mal orthographiée — il n'existe aucun moyen, à la
lecture de la réponse, de distinguer « vous avez imbriqué vos arguments » de
« cette `audio_action` n'existe pas » :

```json
// WS ayant reçu une requête de forme HTTP
// (capturé, tests/core/golden/e40f_ws_audio_http_shaped_request.json)
{"msg": "audio", "msg_id": "1", "data": {"error": "unkown audio_action"}}
```

```json
// HTTP ayant reçu une requête de forme WS
// (capturé, tests/core/golden/e40f_http_audio_ws_shaped_request.json)
{"error": "unkown audio_action"}
```

Sur `audio_db` la dégradation est plus subtile encore : le nom de la
sous-action est à la racine et **est** trouvé, donc le dispatch réussit ; seuls
`from`/`count` sont imbriqués, et le serveur reproche au client des `from`/`count`
qu'il n'a jamais mis là où le serveur les cherche (capturé,
`tests/core/golden/e40f_http_audio_db_ws_shaped_arguments.json` →
`{"error":"wrong from/count"}`).

### Deux particularités WebSocket supplémentaires

- **`msg_id` est optionnel, et son absence peut supprimer la réponse.**
  `sendJson()` n'ajoute `msg_id` à la réponse que si le client en avait fourni
  un ([JsonApiHandlerWS.cpp:63-73](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
  Pire : `set_state` et `register_push` **ne répondent rien** si `msg_id` est
  absent ou vide, alors que leurs équivalents HTTP répondent toujours
  ([JsonApiHandlerWS.cpp:334-339, 505-509](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
- **Tant que la session n'est pas authentifiée, tout message autre que `login`
  et `login_service` est ignoré en silence** — pas d'erreur, pas de fermeture
  ([JsonApiHandlerWS.cpp:156](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).

### Corps HTTP non-JSON : repli sur les paramètres GET

Si le corps HTTP n'est pas un objet JSON valide, le handler se rabat sur les
paramètres d'URL (`jsonParam = paramsGET`)
([JsonApiHandlerHttp.cpp:81-92](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).
Dans ce mode :

- les listes se passent en chaîne séparée par des virgules
  (`items=io_0,io_1`), et non en tableau JSON
  ([JsonApiHandlerHttp.cpp:300-303](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) ;
- les cinq actions qui ont besoin de l'arbre JSON brut — `config`, `audio`,
  `audio_db`, `set_timerange`, `autoscenario` — sont **inaccessibles** : le
  serveur répond HTTP 400 et ferme la connexion
  ([JsonApiHandlerHttp.cpp:189-201](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

---

## Typage sur le fil : tout est une chaîne

Toutes les réponses construites via `Params::toJson()` sérialisent leurs valeurs
avec `json_string()` sans exception
([Params.cpp:134-147](../src/lib/Params.cpp)), et les builders qui écrivent
directement du jansson utilisent eux aussi `json_string()` partout
(`buildJsonIO`, `buildJsonHome`, `buildJsonState`, `buildJsonCameras`,
`buildJsonAudio`, `buildJsonStatusInfo`…). Un client ne doit donc **jamais**
attendre un `true`, un `false` ou un nombre JSON : il reçoit `"true"`,
`"false"`, `"0"`, `"21.5"`.

**L'unique exception de toute l'API** est `buildJsonEventLog()`, réécrit en
nlohmann : `total_page`, `total_count`, `page` et `per_page` y sont de vrais
**entiers JSON**
([JsonApi.cpp:2050-2056](../src/bin/calaos_server/JsonApi.cpp)). C'est le seul
endroit où un client doit attendre un nombre. Toujours dans `eventlog`, le champ
`event_raw` de chaque entrée est un **objet JSON imbriqué** (le JSON de
l'événement est reparsé), et non une chaîne
([HistLogger.cpp:93-100](../src/bin/calaos_server/HistLogger.cpp)).

Deux conséquences de forme, dérivées du code :

- **Formatage des flottants — perte d'information sur le fil** : les valeurs
  numériques passent par `Utils::to_string()`, qui est un `ostringstream` par
  défaut ([StringUtils.h:112-118](../src/lib/StringUtils.h)) — **6 chiffres
  significatifs**, et notation scientifique au-delà. Le seul flottant que
  l'API émet est `time_elapsed` (`audio` / `get_time`), et il est tronqué
  (capturé, `tests/core/golden/e40f_ws_audio_time_six_significant_digits.json`
  et `…_time_large_value_goes_scientific.json`) :

  | Le player répond | Le client reçoit |
  |---|---|
  | `1234.56789` | `"1234.57"` — trois décimales perdues |
  | `123456789.0` | `"1.23457e+08"` — un `parseInt()` naïf renvoie `1` |
  | `0.0` | `"0"` — pas de point décimal |

  Un client qui a besoin de la position exacte dans un morceau ne peut pas
  s'appuyer sur `time_elapsed` au-delà de six chiffres.
- **Ordre des clés** : `Params` est une `map<string,string>`, donc tout objet
  issu de `Params::toJson()` sort avec ses clés **triées alphabétiquement** ;
  les objets construits à la main en jansson sortent dans l'ordre d'insertion.

En entrée, en revanche, les booléens et les nombres JSON **sont** acceptés et
convertis en chaînes par `jansson_decode_object()`
([Jansson_Addition.h:93-111](../src/lib/Jansson_Addition.h)) : envoyer
`"value": true` ou `"value": 42` fonctionne.

---

## Authentification

### HTTP — credentials à chaque requête
Chaque requête HTTP porte `cn_user` / `cn_pass` **à la racine** du corps JSON
(ou en paramètres GET). En cas d'échec : HTTP 400 + fermeture immédiate de la
connexion ([JsonApiHandlerHttp.cpp:63-71, 120-135](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

Les credentials sont lus dans `local_config.xml` : `cn_user`/`cn_pass` s'ils
existent tous les deux, sinon repli sur les anciens `calaos_user` /
`calaos_password` ([JsonApi.cpp:124-141](../src/bin/calaos_server/JsonApi.cpp)).
La comparaison passe par un SHA-256 + `CRYPTO_memcmp` à temps constant
(`secureCompare()`, [JsonApi.cpp:111-122](../src/bin/calaos_server/JsonApi.cpp)).

### Limitation de débit (les deux transports)
`LoginThrottle` indexe les échecs par IP avec un délai exponentiel : 1 s au
premier échec, doublé à chaque suivant, plafonné à 60 s ; une entrée est oubliée
après 900 s sans activité, la table est bornée à 1024 IPs
([JsonApi.h:54-60](../src/bin/calaos_server/JsonApi.h),
[JsonApi.cpp:34-99](../src/bin/calaos_server/JsonApi.cpp)). Un login réussi
efface l'entrée. Pendant le blocage, HTTP répond 400 et WS répond
`{"msg":"login","data":{"success":"false"}}` puis ferme.

### WebSocket — `login` (session admin)
Premier message attendu sur `/api` (forme dérivée de
[JsonApiHandlerWS.cpp:116-151](../src/bin/calaos_server/JsonApiHandlerWS.cpp)) :
```json
{"msg": "login", "msg_id": "1", "data": {"cn_user": "user", "cn_pass": "pass"}}
```
Réponse `{"msg":"login","msg_id":"1","data":{"success":"true"}}`. La session
reste authentifiée pour toute la durée de la connexion ; en cas d'échec la
réponse porte `"success":"false"` et la connexion est fermée.

### WebSocket — `login_service` (session restreinte, sidecar MCP)
Le sidecar `calaos_mcp` ne se connecte pas en admin : il utilise un compte de
service à portée restreinte (`serviceScope`). Voir
[15_mcp_server.md](15_mcp_server.md).
```json
{"msg": "login_service", "msg_id": "1", "data": {"token": "<mcp_service_token>"}}
```
Le token est comparé à l'option `mcp_service_token` de `local_config.xml`
(`McpServerManager::getServiceToken()`). Un token non configuré n'accorde jamais
l'accès ([JsonApiHandlerWS.cpp:526-562](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
En session `serviceScope`, 7 messages sont refusés avec
`{"msg":"<msg>","data":{"error":"scope denied"}}` : `set_param`, `del_param`,
`audio_db`, `set_timerange`, `eventlog`, `register_push`, `settings`
([JsonApiHandlerWS.cpp:159-221](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).

---

## Table complète des commandes de premier niveau

Dérivée intégralement des deux tables de dispatch
([JsonApiHandlerHttp.cpp:140-215](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)
et [JsonApiHandlerWS.cpp:116-229](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
**24 commandes en HTTP, 20 en WebSocket, 27 au total ; 17 sont communes.**

| Commande | HTTP (`action`) | WS (`msg`) | Notes |
|---|:---:|:---:|---|
| `get_home` | ✅ | ✅ | pièces + caméras + players |
| `get_state` | ✅ | ✅ | dictionnaire plat `{id: valeur}` |
| `get_io` | ✅ | ✅ | dictionnaire `{id: objet IO}` |
| `get_states` | ✅ | ✅ | toutes les valeurs d'un IO |
| `query` | ✅ | ✅ | ⚠️ voir « pièges » |
| `get_param` | ✅ | ✅ | |
| `set_param` | ✅ | ✅ | WS : `scope denied` en session service |
| `del_param` | ✅ | ✅ | WS : `scope denied` en session service |
| `set_state` | ✅ | ✅ | WS : muet sans `msg_id` |
| `get_playlist` | ✅ | ✅ | |
| `get_timerange` | ✅ | ✅ | |
| `set_timerange` | ✅ | ✅ | HTTP : corps JSON obligatoire |
| `audio` | ✅ | ✅ | sous-actions divergentes (5 vs 4) |
| `audio_db` | ✅ | ✅ | ⚠️ `get_albums` vs `get_album` |
| `autoscenario` | ✅ | ✅ | silence sur `type` inconnu |
| `eventlog` | ✅ | ✅ | seule réponse à entiers JSON |
| `register_push` | ✅ | ✅ | WS : muet sans `msg_id` |
| `poll_listen` | ✅ | ❌ | équivalent HTTP des événements WS |
| `get_cover` | ✅ | ❌ | JSON + JPEG en base64 |
| `get_camera_pic` | ✅ | ❌ | JSON + JPEG en base64 |
| `camera` | ✅ | ❌ | octets `image/jpeg` bruts |
| `event_picture` | ✅ | ❌ | octets `image/jpeg` bruts |
| `get_mcp_info` | ✅ | ❌ | **HTTP uniquement** |
| `config` | ✅ | ❌ | code WS présent mais commenté ([JsonApiHandlerWS.cpp:223-228](../src/bin/calaos_server/JsonApiHandlerWS.cpp)) |
| `login` | ❌ | ✅ | |
| `login_service` | ❌ | ✅ | |
| `settings` | ❌ | ✅ | |

Action HTTP inconnue → `{"error":"unknown action"}` **si le corps était un JSON
valide**, sinon HTTP 400 + fermeture
([JsonApiHandlerHttp.cpp:189-214](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).
Message WS inconnu (session authentifiée) → **aucune réponse**, il n'y a pas de
branche `else` ([JsonApiHandlerWS.cpp:156-229](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).

---

## Sous-actions

Sept commandes redispatchent sur une sous-clé. **36 sous-actions au total**, ce
qui porte l'API à **56 opérations distinctes** (20 commandes feuilles + 36
sous-actions).

### `audio` — clé `audio_action` (5 en HTTP, 4 en WS)

| `audio_action` | HTTP | WS | Arguments | Réponse |
|---|:---:|:---:|---|---|
| `get_playlist_size` | ✅ | ✅ | `id` | `{"playlist_size":"N"}` |
| `get_time` | ✅ | ✅ | `id` | `{"time_elapsed":"12.5"}` |
| `get_playlist_item` | ✅ | ✅ | `id`, `item` (entier) | les tags de la piste |
| `get_cover_url` | ✅ | ✅ | `id` | `{"cover":"<url>"}` |
| `get_cover` | ✅ | ❌ | `id` | **octets `image/jpeg`** ([JsonApiHandlerHttp.cpp:711-773](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) |

Sous-action inconnue (ou absente) → `{"error":"unkown audio_action"}`.

### `audio_db` — clé `audio_action` (16 des deux côtés)

Tous prennent `id` (le player) ; sauf mention contraire, `from` et `count`
(entiers, sous forme de chaînes) sont **obligatoires** et une valeur absente ou
non numérique donne `{"error":"wrong from/count"}`.

| `audio_action` | Nom HTTP | Nom WS | Arguments supplémentaires |
|---|---|---|---|
| albums | **`get_albums`** | **`get_album`** | — |
| `get_stats` | ✅ | ✅ | aucun (`from`/`count` non requis) |
| `get_artist_album` | ✅ | ✅ | `artist_id` |
| `get_year_albums` | ✅ | ✅ | `year` |
| `get_genre_artists` | ✅ | ✅ | `genre` |
| `get_album_titles` | ✅ | ✅ | `album_id` |
| `get_playlist_titles` | ✅ | ✅ | `playlist_id` |
| `get_artists` | ✅ | ✅ | — |
| `get_years` | ✅ | ✅ | — |
| `get_genres` | ✅ | ✅ | — |
| `get_playlists` | ✅ | ✅ | — |
| `get_music_folder` | ✅ | ✅ | `folder_id` |
| `get_search` | ✅ | ✅ | `search` |
| `get_radios` | ✅ | ✅ | — |
| `get_radio_items` | ✅ | ✅ | `radio_id`, `item_id`, `search` |
| `get_track_infos` | ✅ | ✅ | `track_id` (pas de `from`/`count`) |

⚠️ **Divergence de nom sur les albums.** Le même builder `audioDbGetAlbums()`
est câblé sous deux orthographes : HTTP attend `get_albums`
([JsonApiHandlerHttp.cpp:781](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)),
WebSocket attend `get_album` (singulier,
[JsonApiHandlerWS.cpp:380](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
Chaque transport **rejette** l'orthographe de l'autre avec
`{"error":"unkown audio_action"}`. Un client multi-transport doit donc écrire
les deux.

### `autoscenario` — clé `type` (7 des deux côtés)

| `type` | Arguments | Réponse |
|---|---|---|
| `list` | — | `{"scenarios":[…]}` |
| `get` | `id` | l'objet scénario, ou `{"error":"wrong input"}` |
| `create` | `name`, `visible`, `cycle`, `disabled`, `room_name`, `room_type`, `steps` | `{"id":"…"}` ou `{"error":"scenario creation failed"}` |
| `delete` | `id` | `{"success":"true"}` |
| `modify` | idem `create` + `id` | `{"success":"true"}` ou `{"error":"scenario modification failed"}` |
| `add_schedule` | `id` | `{"id":"<id de la plage horaire>"}` |
| `del_schedule` | `id` | `{"success":"true"}` |

⚠️ Un `type` inconnu (ou absent) **ne produit aucune réponse** sur les deux
transports : il n'y a pas de branche `else`
([JsonApiHandlerHttp.cpp:880-897](../src/bin/calaos_server/JsonApiHandlerHttp.cpp),
[JsonApiHandlerWS.cpp:474-491](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
Le client reste en attente jusqu'à son propre timeout. Ce n'est pas une erreur,
c'est un silence.

### `poll_listen` — clé `type` (HTTP uniquement, 3)

Voir [10_events_notifications.md](10_events_notifications.md) pour le détail.

| `type` | Arguments | Réponse |
|---|---|---|
| `register` | — | `{"uuid":"<uuid v4>"}` |
| `get` | `uuid` | `{"success":"true","events":[…]}` ou `{"success":"false"}` |
| `unregister` | `uuid` | `{"success":"true"\|"false"}` |

`type` inconnu ou absent → `{}` (objet vide,
[JsonApiHandlerHttp.cpp:388-427](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

### `config` — clé `type` (HTTP uniquement, 2)

| `type` | Effet | Réponse |
|---|---|---|
| `get` | sauvegarde puis relit `io.xml`, `rules.xml`, `local_config.xml` | `{"config_files":{…},"success":"true"}` |
| `put` | écrit les fichiers fournis dans `config_files` après backup, puis **redémarre le serveur** une fois la réponse écrite | `{"success":"true"\|"false"}` |

`type` inconnu → `{"success":"false"}`. En `put`, seuls les trois noms de
fichiers connus sont acceptés et le contenu doit commencer par `<?xml`
([JsonApiHandlerHttp.cpp:594-686](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

### `camera` — clé `type` (HTTP uniquement, 2)

| `type` | Réponse |
|---|---|
| `get_picture` | un JPEG (`Content-Type: image/jpeg`) ; `camfail.jpg` si la caméra ne répond pas |
| `get_video` | flux MJPEG relayé, ou flux construit image par image si la caméra n'a pas d'URL vidéo |

`id` inconnu → `{"error":"unkown camera id"}`. `type` inconnu → **rien**, pas de
branche `else` ([JsonApiHandlerHttp.cpp:899-1025](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

### `settings` — clé `action` (WS uniquement, 1)

| `action` | Arguments | Réponse |
|---|---|---|
| `change_cred` | `old_user`, `old_pw`, `new_user`, `new_pw` | `{"action":"change_cred","success":"true"\|"false"}` |

En cas de succès, la session est **déconnectée** (`loggedin = false`) : il faut
se relogguer avec les nouveaux identifiants
([JsonApiHandlerWS.cpp:512-524](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
Toute autre valeur d'`action` → aucune réponse.

---

## Réponses non-JSON

Six opérations ne renvoient pas du JSON applicatif — mais pas toutes de la même
façon, et c'est une source classique de confusion :

| Opération | Ce qui sort réellement |
|---|---|
| `action: get_cover` | **JSON**, avec le JPEG encodé : `{"success":"true","contenttype":"image/jpeg","encoding":"base64","data":"<base64>"}` ([JsonApiHandlerHttp.cpp:575-592](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) |
| `action: get_camera_pic` | **JSON base64**, même forme (même fonction `exeFinished()`) |
| `audio` / `audio_action: get_cover` | **octets JPEG bruts**, `Content-Type: image/jpeg` ([JsonApiHandlerHttp.cpp:753-757](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) |
| `camera` / `get_picture` | octets JPEG bruts |
| `camera` / `get_video` | flux MJPEG |
| `action: event_picture` | octets JPEG bruts, ou HTTP 404 si `pic_uid` est introuvable ([JsonApiHandlerHttp.cpp:1083-1104](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) |

`get_cover` et `get_camera_pic` acceptent `width` (1…10000) et `rotate`
(-360…360) ; toute autre valeur donne
`{"success":"false","error_str":"invalid width or rotate parameter"}`.

---

## Exemples de requêtes et de réponses

> **Convention de traçabilité.** Chaque exemple porte sa provenance :
> **(capturé, `tests/core/golden/…`)** = octets produits par le code et figés en
> golden ; **(dérivé, `Fichier.cpp:L-L`)** = forme reconstituée en lisant le
> builder. Aucun exemple de ce document n'a d'autre origine. Les goldens ne
> capturent que des **réponses** : toutes les requêtes ci-dessous sont dérivées
> du dispatch. Les goldens sont ré-indentés et à clés triées par le
> comparateur de tests ; sur le fil, le JSON est compact
> (`JSON_COMPACT`, [JsonApiHandlerHttp.cpp:223](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

### `get_home`

Requête HTTP (dérivée, [JsonApiHandlerHttp.cpp:140-141](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) :
```json
{"action": "get_home", "cn_user": "user", "cn_pass": "pass"}
```

Réponse (capturé, `tests/core/golden/http_get_home.json`, extrait) :
```json
{
  "audio": [
    {"avr": "e40_avr", "database": "false", "id": "e40_player",
     "name": "Player", "playlist": "false", "type": "Roon"}
  ],
  "cameras": [
    {"id": "e40_cam_ptz", "name": "Camera PTZ", "ptz": "true", "type": "StandardMjpeg"}
  ],
  "home": [
    {
      "hits": "3",
      "name": "Salon",
      "type": "salon",
      "items": [
        {"gui_type": "var_bool", "id": "e40_bool_in", "io_type": "inout",
         "name": "Bool input", "rw": "false", "state": "false",
         "type": "InternalBool", "var_type": "bool", "visible": "true"}
      ]
    }
  ]
}
```

`home` est un **tableau** de pièces ; chaque pièce porte ses IOs sous `items`
(pas `ios`). La même réponse en WebSocket est enveloppée (capturé,
`tests/core/golden/ws_get_home.json`) :
```json
{"msg": "get_home", "msg_id": "42", "data": { … même contenu … }}
```

Champs produits par `buildJsonIO()`, présents seulement s'ils existent sur l'IO
([JsonApi.cpp:256-297](../src/bin/calaos_server/JsonApi.cpp)) : `id`, `name`,
`type`, `hits`, `var_type` (`bool`/`float`/`string`), `visible`,
`chauffage_id`, `rw`, `unit`, `gui_type`, `state`, `auto_scenario`, `step`,
`io_type`, `io_style`, `value_warning`, plus un objet `status_info` le cas
échéant. `state` et `var_type` sont toujours présents (ils sont calculés, pas
lus dans les params) — un IO string vide donne `"state": ""`.

### `get_io`

Requête WS (dérivée, [JsonApiHandlerWS.cpp:189-190](../src/bin/calaos_server/JsonApiHandlerWS.cpp)) :
```json
{"msg": "get_io", "msg_id": "e40b-2", "data": {"items": ["e40_int", "e40_string"]}}
```

Réponse (capturé, `tests/core/golden/e40b_ws_get_io.json`, extrait) :
```json
{
  "msg": "get_io",
  "msg_id": "e40b-2",
  "data": {
    "e40_int": {"gui_type": "var_int", "id": "e40_int", "io_type": "inout",
                "name": "Int value", "rw": "false", "state": "0",
                "type": "InternalInt", "var_type": "float", "visible": "true"}
  }
}
```

Les identifiants inconnus sont **silencieusement omis** de la réponse
([JsonApi.cpp:740-757](../src/bin/calaos_server/JsonApi.cpp)) : la taille de la
réponse peut être inférieure à celle de la demande.

### `get_state`

Requête HTTP (dérivée) — `items` est un tableau d'identifiants :
```json
{"action": "get_state", "cn_user": "user", "cn_pass": "pass",
 "items": ["e40_int", "e40_bool_in"]}
```

Réponse : un dictionnaire plat `{id: valeur}` (capturé,
`tests/core/golden/e40b_http_get_state.json`) :
```json
{"e40_accented": "", "e40_bool_in": "false", "e40_bool_out": "false",
 "e40_cam_plain": "", "e40_cam_ptz": "", "e40_int": "0", "e40_string": ""}
```

Cas particulier : un IO de `gui_type` `audio_player` n'est pas rendu comme un
scalaire mais comme un **objet** (`playlist_current_track`, `volume`,
`playlist_size`, `time_elapsed`, `status`, `current_track`) et la réponse est
alors asynchrone ([JsonApi.cpp:421-598](../src/bin/calaos_server/JsonApi.cpp)).
`status` vaut `playing`, `pause`, `stop`, `error` ou `song_change`.

### `set_state`

Requête HTTP (dérivée, [JsonApiHandlerHttp.cpp:373-378](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) :
```json
{"action": "set_state", "cn_user": "user", "cn_pass": "pass",
 "id": "e40_bool_out", "value": "true"}
```
Réponse (dérivée) : `{"success":"true"}` ou `{"success":"false"}`. `false`
couvre indistinctement « IO inconnu » et « la commande a échoué »
([JsonApi.cpp:759-781](../src/bin/calaos_server/JsonApi.cpp)) — il n'y a pas de
message d'erreur distinct.

### `get_playlist`

Réponse HTTP (capturé, `tests/core/golden/t317a_http_get_playlist.json`,
**extrait** — le golden porte les 3 items annoncés par `count`) :
```json
{
  "count": "3",
  "current_track": "1",
  "items": [
    {"album": "Album 0", "artist": "Artist 0", "duration": "100",
     "id": "track_0", "title": "Title 0"}
  ]
}
```
Ici `count` **est** bien la longueur de `items` — contrairement au `steps_count`
d'`autoscenario` (voir plus bas) et au `total_count` d'`audio_db`, qui comptent
tous deux autre chose que le tableau qui les accompagne.

Playlist vide (capturé, `tests/core/golden/t317a_ws_get_playlist_empty.json`,
**intégral**, enveloppe WS comprise) :
```json
{"msg": "get_playlist", "msg_id": "1",
 "data": {"count": "0", "current_track": "0", "items": []}}
```
Player inconnu (capturé,
`tests/core/golden/t317a_http_get_playlist_unknown_player.json`, **intégral**) :
`{"success":"false"}` — noter que ce chemin-là ne renvoie **pas** `error`.

### `audio`

Requête WS (dérivée, [JsonApiHandlerWS.cpp:350-375](../src/bin/calaos_server/JsonApiHandlerWS.cpp)) :
```json
{"msg": "audio", "msg_id": "1",
 "data": {"audio_action": "get_time", "id": "e40_player"}}
```
Réponse (capturé, `tests/core/golden/t317b_ws_audio_get_time.json`,
**intégral**) :
```json
{"msg": "audio", "msg_id": "1", "data": {"time_elapsed": "12.5"}}
```

Autres réponses capturées, **intégrales** — noter que les goldens HTTP sont le
corps nu et les goldens WS portent l'enveloppe `msg`/`msg_id` :

| Golden | Contenu exact |
|---|---|
| `t317b_http_audio_get_playlist_size.json` (HTTP) | `{"playlist_size":"12"}` |
| `t317b_ws_audio_get_cover_url.json` (WS) | `{"msg":"audio","msg_id":"1","data":{"cover":"http://calaos.fr/cover_t317b.jpg"}}` |
| `t317b_ws_audio_get_playlist_item.json` (WS) | `{"msg":"audio","msg_id":"1","data":{"album":"Album 3","artist":"Artist 3","duration":"103","id":"track_3","title":"Title 3"}}` |

### `audio_db` / `get_stats`

Réponse (capturé, `tests/core/golden/t317b_http_audio_db_get_stats.json`) :
```json
{"albums": "12", "artists": "7", "audio_action": "get_stats", "songs": "134"}
```

### `get_timerange`

Réponse (capturé, `tests/core/golden/e40c_http_get_timerange.json`, extrait) :
```json
{
  "months": "110000000001",
  "ranges": [
    {"day": "1", "start_hour": "8", "start_min": "30", "start_sec": "15",
     "start_type": "0", "start_offset": "1",
     "end_hour": "12", "end_min": "45", "end_sec": "5",
     "end_type": "0", "end_offset": "1"}
  ]
}
```
`day` va de 1 (lundi) à 7 (dimanche) ; `months` est un bitset de 12 caractères
**inversé** à l'émission pour se lire de gauche à droite
([JsonApi.cpp:1610-1614](../src/bin/calaos_server/JsonApi.cpp)). IO qui n'est
pas une plage horaire → `{"error":"wrong input"}`.

### `autoscenario` / `get` et `list`

Réponse `get` (capturé, `tests/core/golden/e40c_ws_autoscenario_get.json`,
**intégral**) :
```json
{
  "msg": "autoscenario",
  "msg_id": "e40c-get",
  "data": {
    "id": "io_0", "category": "other", "cycle": "false", "enabled": "false",
    "schedule": "false", "steps_count": "2",
    "steps": [
      {"step_type": "standard", "step_pause": "1.5",
       "actions": [{"action": "true", "id": "e40c_bool"}]},
      {"step_type": "standard", "step_pause": "0",
       "actions": [{"action": "true", "id": "e40c_target"},
                   {"action": "42",   "id": "e40c_int"}]},
      {"step_type": "end",
       "actions": [{"action": "done", "id": "e40c_string"}]}
    ]
  }
}
```

> ⚠️ **`steps_count` ne compte PAS les éléments du tableau `steps`.**
> Ci-dessus : `steps_count` vaut `"2"` et `steps` contient **3** éléments. Ce
> n'est pas une incohérence du golden, c'est le contrat :
> `steps_count` est la taille de `getRuleSteps()`, c'est-à-dire le nombre
> d'étapes **réelles** ([IO/Scenario.cpp:95](../src/bin/calaos_server/IO/Scenario.cpp)),
> puis la boucle émet ces étapes ([:99-123](../src/bin/calaos_server/IO/Scenario.cpp))
> et une étape terminale **synthétique** `step_type: "end"` est ajoutée
> **hors de la boucle** ([:125-142](../src/bin/calaos_server/IO/Scenario.cpp)).
>
> **L'invariant est donc `len(steps) == steps_count + 1`, toujours.** Un client
> qui dimensionne son tableau sur `steps_count` **tronque l'étape de fin** —
> c'est-à-dire précisément les actions exécutées à la sortie du scénario. Pour
> itérer sur les seules étapes réelles, s'arrêter à `steps_count`, ou filtrer
> sur `step_type != "end"` ; ne jamais utiliser `steps_count` comme longueur du
> tableau.

L'étape `end` est reconnaissable à deux choses : `step_type: "end"` et
**l'absence** de `step_pause` (elle n'est pas temporisée). Les étapes réelles
portent toujours un `step_pause`, éventuellement `"0"`.

`list` renvoie la même structure sous `{"scenarios":[…]}` (capturé,
`tests/core/golden/e40c_ws_autoscenario_list.json`). `schedule` vaut `"false"`
ou l'identifiant de la plage horaire associée.

### `eventlog`

Requête (dérivée, [JsonApi.cpp:2007-2060](../src/bin/calaos_server/JsonApi.cpp)) :
```json
{"action": "eventlog", "cn_user": "user", "cn_pass": "pass",
 "page": "0", "per_page": "100"}
```
Réponse (dérivée du builder, **aucun golden n'existe**) — c'est la seule
réponse de l'API à contenir de vrais entiers JSON :
```json
{
  "total_page": 3,
  "total_count": 271,
  "page": 0,
  "per_page": 100,
  "events": [
    {
      "id": "<uuid>",
      "event_type": "3",
      "io_id": "io_0",
      "io_state": "true",
      "pic_uid": "",
      "created_at": "2026-01-01 12:00:00",
      "event_raw": {"event_raw": "io_changed id:io_0 state:true",
                    "type": "3", "type_str": "io_changed",
                    "data": {"id": "io_0", "state": "true"}}
    }
  ]
}
```
Avec `uuid` en argument, la réponse est **l'objet événement seul**, sans
pagination ; `uuid` introuvable → `{"error":"uuid not found"}`
([HistLogger.cpp:337-341](../src/bin/calaos_server/HistLogger.cpp)).

### `get_mcp_info` — **HTTP uniquement**

Cette action n'existe **pas** dans le dispatch WebSocket : elle n'est câblée que
dans `JsonApiHandlerHttp`
([JsonApiHandlerHttp.cpp:176-188](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).
```json
// Requête (dérivée)
{"action": "get_mcp_info", "cn_user": "user", "cn_pass": "pass"}
// Réponse (dérivée de JsonApiHandlerHttp.cpp:180-187)
{"url_path": "/mcp",
 "token": "<mcp_token>",
 "hint": "Use token as Bearer in Authorization header. Append /mcp to your Calaos HTTPS base URL."}
```

---

## Chaînes d'erreur (verbatim, coquilles incluses)

Ces chaînes sont **exactement** celles du code de production. Trois d'entre
elles contiennent une faute de frappe historique (`unkown` au lieu de
`unknown`) : un client doit matcher l'orthographe fautive, pas la corriger.

| Chaîne | Où | Sens |
|---|---|---|
| `unknown action` | [JsonApiHandlerHttp.cpp:214](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) | action HTTP inconnue (orthographe correcte, ici) |
| `unkown audio_action` | [JsonApiHandlerHttp.cpp:775, 862](../src/bin/calaos_server/JsonApiHandlerHttp.cpp), [JsonApiHandlerWS.cpp:374, 461](../src/bin/calaos_server/JsonApiHandlerWS.cpp) | **coquille** — sous-action audio inconnue |
| `unkown player_id` | [JsonApi.cpp:934](../src/bin/calaos_server/JsonApi.cpp) | **coquille** — `id` fourni mais pas un player |
| `empty player id` | [JsonApi.cpp:926](../src/bin/calaos_server/JsonApi.cpp) | `id` absent ou vide |
| `unkown camera id` | [JsonApiHandlerHttp.cpp:905](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) | **coquille** — `id` fourni mais pas une caméra |
| `wrong item` | [JsonApi.cpp:1038](../src/bin/calaos_server/JsonApi.cpp) | `item` absent ou non entier |
| `wrong from/count` | 14 occurrences dans les builders `audio_db` | `from`/`count` absents ou non entiers |
| `wrong io/param` | [JsonApi.cpp:675, 703, 733](../src/bin/calaos_server/JsonApi.cpp) | `get_param`/`set_param`/`del_param` : IO ou param invalide |
| `wrong id` | [JsonApi.cpp:609, 650](../src/bin/calaos_server/JsonApi.cpp) | `get_states` / `query` : IO introuvable |
| `wrong input` | [JsonApi.cpp:1587, 1625, 1708, 1814, 1840, 1967, 1990](../src/bin/calaos_server/JsonApi.cpp) | plage horaire ou scénario introuvable |
| `scenario creation failed` | [JsonApi.cpp:1748, 1760](../src/bin/calaos_server/JsonApi.cpp) | |
| `scenario modification failed` | [JsonApi.cpp:1946](../src/bin/calaos_server/JsonApi.cpp) | |
| `uuid not found` | [HistLogger.cpp:340](../src/bin/calaos_server/HistLogger.cpp) | `eventlog` avec un `uuid` inconnu |
| `scope denied` | [JsonApiHandlerWS.cpp:162](../src/bin/calaos_server/JsonApiHandlerWS.cpp) | message refusé en session `serviceScope` |
| `invalid token` | [JsonApiHandlerWS.cpp:540, 551](../src/bin/calaos_server/JsonApiHandlerWS.cpp) | `login_service` refusé |
| `id not set` | [JsonApiHandlerHttp.cpp:436, 504](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) | `get_cover`/`get_camera_pic` : `id` invalide |
| `invalid width or rotate parameter` | [JsonApiHandlerHttp.cpp:453, 521](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) | |
| `unable to get url` | [JsonApiHandlerHttp.cpp:470, 734](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) | pas de pochette disponible |
| `unable to load data from url` | [JsonApiHandlerHttp.cpp:581, 748](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) | `calaos_picture` a échoué |

La clé porteuse est `error` dans tous les cas sauf les quatre dernières lignes,
qui utilisent `{"success":"false","error_str":"…"}`.

---

## Pièges connus

Ces comportements sont ceux du code en production. Ils sont documentés ici
**parce qu'ils sont contre-intuitifs**, pas parce qu'ils sont souhaitables.

### `query` lit `input_id` mais teste `id`

`buildQuery()` conditionne tout son travail à `jParam.Exists("id")` puis fait le
lookup sur `jParam["input_id"]`
([JsonApi.cpp:641-661](../src/bin/calaos_server/JsonApi.cpp)). Conséquences pour
un client :

- envoyer seulement `input_id` → `Exists("id")` est faux → réponse `{}` ;
- envoyer seulement `id` → lookup sur une chaîne vide → `{"error":"wrong id"}` ;
- **il faut envoyer les deux** : `id` (n'importe quelle valeur, seule sa
  présence compte) **et** `input_id` (l'identifiant réellement interrogé), plus
  `param`.

### `audio_action` avalé par deux sous-actions sur trois

`audioGetDbStats()` ajoute `audio_action: "get_stats"` aux params du player
**et renvoie ces params**, donc le client le voit
([JsonApi.cpp:966-972](../src/bin/calaos_server/JsonApi.cpp)) — c'est visible
dans le golden `t317b_http_audio_db_get_stats.json`. Mais
`audioGetPlaylistSize()` et `audioGetTime()` ajoutent `audio_action` aux params
du player puis construisent un **nouveau** `Params` pour la réponse : le champ
n'atteint jamais le client
([JsonApi.cpp:989-996, 1013-1020](../src/bin/calaos_server/JsonApi.cpp)),
confirmé par `t317b_http_audio_get_playlist_size.json` et
`t317b_http_audio_get_time.json`. Un client ne peut donc pas se fier à
`audio_action` pour corréler une réponse à sa requête : il faut utiliser
`msg_id` (WS) ou l'ordre des requêtes (HTTP).

### `processDbResult()` : la ligne `count` est une ligne du tableau

`processDbResult()` recopie **tous** les `Params` renvoyés par la base dans
`items`, y compris celui qui porte la clé `count`
([JsonApi.cpp:1079-1101](../src/bin/calaos_server/JsonApi.cpp)). Ce `Params`
n'est pas retiré : il apparaît dans `items` comme une pseudo-ligne
`{"count":"N"}`. Sa position dépend du backend — le parseur SqueezeboxDB traite
le token `count:` comme un séparateur d'enregistrement
([SqueezeboxDB.cpp:66-73](../src/bin/calaos_server/Audio/SqueezeboxDB.cpp)),
et `getRandoms()` l'ajoute explicitement en **dernier**
([SqueezeboxDB.cpp:774-775](../src/bin/calaos_server/Audio/SqueezeboxDB.cpp)).
Un client doit filtrer les lignes dépourvues des champs qu'il attend, sans
présumer d'un index.

Deux autres règles de ce même builder :

- `total_count` reprend la valeur du **dernier** `Params` porteur d'un `count` ;
- si cette valeur vaut exactement la chaîne `"0"`, le tableau `items` est
  **vidé** mais `total_count` est quand même émis ;
- si **aucun** `Params` ne porte de `count`, la clé `total_count` est
  **absente** de la réponse (absente, jamais `null`).

Exemple d'une réponse `audio_db` dont la base a placé son marqueur `count`
**au milieu** (capturé, `tests/core/golden/e40f_http_db_count_marker_in_the_middle.json`) :

```json
{
  "items": [
    {"id": "alb_11", "name": "Homogenic",  "year": "1997"},
    {"count": "47"},
    {"id": "alb_29", "name": "Vespertine", "year": "2001"}
  ],
  "total_count": "47"
}
```

Trois cas limites, tous capturés, que les clients doivent gérer :

| Ce que renvoie la base | Réponse | Golden |
|---|---|---|
| deux `Params` porteurs de `count` (`"11"` puis `"47"`) | `total_count` vaut `"47"` ; **les deux** pseudo-lignes restent dans `items` | `e40f_ws_db_last_count_marker_wins.json` |
| `count: "00"` | `items` **n'est pas** vidé et `total_count` vaut `"00"` — la comparaison est faite sur la **chaîne** `"0"`, pas sur un nombre | `e40f_ws_db_count_double_zero_does_not_clear.json` |
| `count` non numérique (`"beaucoup"`) | recopié verbatim dans `total_count`, `items` intact | `e40f_ws_db_count_not_a_number.json` |

### `audio_db` sur un player sans base de données fait planter le serveur

`AudioPlayer::database` vaut `nullptr` par défaut
([AudioPlayer.cpp:26-28](../src/bin/calaos_server/Audio/AudioPlayer.cpp)) et
seul `Squeezebox` l'initialise
([Squeezebox.cpp:81](../src/bin/calaos_server/Audio/Squeezebox.cpp)). Aucun des
deux dispatchs ne filtre sur `canDatabase()` (qui renvoie `false` dans la classe
de base, [AudioPlayer.h:101](../src/bin/calaos_server/Audio/AudioPlayer.h)), et
les 16 builders `audio_db` appellent `player->get_database()->…` sans test :
adresser un `audio_db` à un player Roon **déréférence un pointeur nul**. C'est
un bug connu, traité dans un ticket dédié ; en attendant, un client doit vérifier
lui-même le champ `database` renvoyé par `get_home` avant d'émettre un
`audio_db`.

---

## Correspondance opération → builder

Tous les builders sont dans
[JsonApi.cpp](../src/bin/calaos_server/JsonApi.cpp) et déclarés dans
[JsonApi.h](../src/bin/calaos_server/JsonApi.h).

| Opération | Builder |
|---|---|
| `get_home` | `buildJsonHome()` + `buildJsonCameras()` + `buildJsonAudio()` |
| `get_io` | `buildJsonGetIO()` → `buildJsonIO()` (+ `buildJsonStatusInfo()`) |
| `get_state` | `buildJsonState()` |
| `get_states` | `buildJsonStates()` |
| `query` | `buildQuery()` |
| `get_param` / `set_param` / `del_param` | `buildJsonGetParam()` / `buildJsonSetParam()` / `buildJsonDelParam()` |
| `set_state` | `decodeSetState()` |
| `get_playlist` | `decodeGetPlaylist()` + `getNextPlaylistItem()` |
| `get_timerange` / `set_timerange` | `buildJsonGetTimerange()` / `buildJsonSetTimerange()` |
| `autoscenario` | `buildAutoscenario{List,Get,Create,Delete,Modify,AddSchedule,DelSchedule}()` |
| `audio` | `audioGet{PlaylistSize,Time,PlaylistItem,CoverInfo}()` |
| `audio_db` | `audioGetDbStats()`, `audioDbGet*()` (16 builders, tous via `processDbResult()` sauf `get_stats` et `get_track_infos`) |
| `eventlog` | `buildJsonEventLog()` |
| `register_push` | `registerPushToken()` |
| `settings` / `change_cred` | `changeCredentials()` |

`buildFlatIOList()` existe dans `JsonApi` mais **n'est appelé par aucune des deux
tables de dispatch** : il n'est pas atteignable depuis cette API.

---

## PollListenner

**Fichier :** [src/bin/calaos_server/PollListenner.h](../src/bin/calaos_server/PollListenner.h)

Équivalent HTTP des événements WebSocket : un client HTTP s'enregistre, puis
récupère périodiquement les événements accumulés. Un enregistrement inactif
depuis 300 s est détruit (`TIMEOUT_POLLLISTENNER`). Voir
[10_events_notifications.md](10_events_notifications.md) pour le protocole et la
forme des événements.

---

## UDPServer

**Fichier :** [src/bin/calaos_server/UDPServer.h](../src/bin/calaos_server/UDPServer.h)

Serveur UDP pour la découverte du serveur Calaos sur le réseau local (broadcast).

---

## HttpClient (utilitaire)

**Fichier :** [src/lib/UrlDownloader.h](../src/lib/UrlDownloader.h)

Classe utilitaire pour télécharger des ressources HTTP depuis le serveur vers des
URLs externes (utilisée par les drivers Web, les capteurs Hue, etc.). Basée sur
libcurl avec l'event loop libuv.
