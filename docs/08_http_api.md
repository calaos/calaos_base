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

### Deux refus qui arrivent **avant** le JsonApi

Un client peut recevoir deux codes HTTP sans qu'aucune action JSON n'ait été
lue. Ils ne portent **pas** de corps JSON applicatif, mais une page HTML, et la
connexion est fermée :

| Code | Quand | Où |
|---|---|---|
| `431 Request Header Fields Too Large` | les en-têtes d'une requête dépassent **32 Kio** (`TransportLimits::MaxHeadersSize`, constante, **non configurable** : haproxy ne transmet de toute façon pas plus de `tune.bufsize`) | [HttpClient.h:71-76, :248](../src/bin/calaos_server/HttpClient.h), [HttpClient.cpp:186-193](../src/bin/calaos_server/HttpClient.cpp) |
| `429 Too Many Requests` | le client a déjà `max_connections_per_ip` connexions ouvertes (**50** par défaut) | [HttpClient.cpp:266-281](../src/bin/calaos_server/HttpClient.cpp), [HttpClient.h:68](../src/bin/calaos_server/HttpClient.h), [HttpClient.cpp:95-101](../src/bin/calaos_server/HttpClient.cpp) |

Le plafond de connexions est compté **par identité client**, tirée de
`X-Forwarded-For` quand elle existe et du pair TCP sinon
([HttpClient.cpp:195-215](../src/bin/calaos_server/HttpClient.cpp)) ; il n'est
appliqué qu'à la **première requête analysée**, puisque l'en-tête n'existe pas
avant — la montée WebSocket sur `/api` passe donc par le même contrôle, au même
endroit ([HttpClient.cpp:273-281](../src/bin/calaos_server/HttpClient.cpp)).
⚠️ Ne pas le confondre avec le `429` de `/api/v3/remote_ui/ws`, qui vient d'un
**autre mécanisme** : la limitation d'authentification RemoteUI
(`AuthFailureReason::RateLimited`,
[RemoteUI/AuthFailureReason.h:57-58](../src/bin/calaos_server/RemoteUI/AuthFailureReason.h),
[WebSocket.cpp:314-315](../src/bin/calaos_server/WebSocket.cpp)). Pour la
sémantique de l'option, voir [16_config_options.md](16_config_options.md).

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
| idem, sur `audio` / `audio_db` | le lecteur tolérant de `audio_action` renvoie `""` sur une racine qui n'est pas un objet | `{"error":"unkown audio_action"}` |
| idem, sur `autoscenario` | `type` vide, aucune branche ne correspond | **rien du tout** ([JsonApiHandlerWS.cpp:474-495](../src/bin/calaos_server/JsonApiHandlerWS.cpp)) |
| idem, sur `query` / `get_states` | `Exists("id")` faux | `{}` |
| Requête HTTP avec les arguments sous `data` | le décodeur aplatissant ne prend **que les scalaires** : un objet imbriqué devient une chaîne vide ([JsonApiHandlerHttp.cpp](../src/bin/calaos_server/JsonApiHandlerHttp.cpp), `decodeJsonObject()`) | `jsonParam["data"] == ""` et **aucun** argument utile → mêmes réponses vides que ci-dessus |

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
en chaînes JSON sans exception
([Params.cpp:134-147](../src/lib/Params.cpp)), et les builders qui écrivent
leur document à la main écrivent eux aussi des chaînes partout
(`buildJsonIO`, `buildJsonHome`, `buildJsonState`, `buildJsonCameras`,
`buildJsonAudio`, `buildJsonStatusInfo`…). Un client ne doit donc **jamais**
attendre un `true`, un `false` ou un nombre JSON : il reçoit `"true"`,
`"false"`, `"0"`, `"21.5"`.

**Les deux exceptions de toute l'API** sont l'objet `device` des devices
(voir [Devices](#devices--commande-et-objet-device)) et `buildJsonEventLog()`, réécrit en
nlohmann : `total_page`, `total_count`, `page` et `per_page` y sont de vrais
**entiers JSON**
([JsonApi.cpp:2361-2366](../src/bin/calaos_server/JsonApi.cpp)). Hors de l'objet
`device`, c'est le seul endroit où un client doit attendre un nombre. Toujours dans `eventlog`, le champ
`event_raw` de chaque entrée est un **objet JSON imbriqué** (le JSON de
l'événement est reparsé), et non une chaîne
([HistLogger.cpp:334-341](../src/bin/calaos_server/HistLogger.cpp)).

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
  issu de `Params::toJson()` sort avec ses clés **triées alphabétiquement** —
  et les objets construits à la main aussi, `nlohmann::json` étant une `std::map`.

En entrée, en revanche, les booléens et les nombres JSON **sont** acceptés et
convertis en chaînes par le décodeur aplatissant
([JsonApiHandlerHttp.cpp](../src/bin/calaos_server/JsonApiHandlerHttp.cpp),
`decodeJsonObject()`) : envoyer
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

> ⚠️ **L'IP indexée est celle du pair TCP, pas celle du client.** Les deux
> transports passent à `LoginThrottle` le résultat de `clientIp()`, qui renvoie
> `HttpClient::getClientIp()`, c'est-à-dire l'adresse du **socket**
> ([HttpClient.cpp:710-724](../src/bin/calaos_server/HttpClient.cpp),
> [JsonApiHandlerHttp.cpp:55-60](../src/bin/calaos_server/JsonApiHandlerHttp.cpp),
> [JsonApiHandlerWS.cpp:45-50](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
> Le plafond de connexions par client, lui, utilise bien `X-Forwarded-For`
> ([HttpClient.cpp:195-215](../src/bin/calaos_server/HttpClient.cpp)) — les deux
> mécanismes n'ont donc **pas** la même notion de « client ». Derrière le reverse
> proxy, où `calaos_server` est déployé, tous les clients partagent l'adresse du
> proxy et **donc le même compteur d'échecs** : un client tiers en échec bloque
> les autres, et le blocage n'est pas par client. Défaut connu, non corrigé à ce
> jour, suivi en `T3.24` (voir `docs/refactoring/FINDINGS.md`).

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
l'accès ([JsonApiHandlerWS.cpp:530-566](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
En session `serviceScope`, **9** messages sont refusés : `set_param`, `del_param`,
`audio_db`, `set_timerange`, `autoscenario`, `eventlog`, `register_push`,
`settings`, `config` (`JsonApi::serviceScopeDeniedCommands()`).

⭐ **La portée est une propriété de la session, pas du transport.** Les neuf sont
refusées quel que soit le transport par lequel la session a été ouverte, et la
charge utile du refus est **la même** des deux côtés — seule l'enveloppe change :
`{"msg":"<msg>","msg_id":"<le vôtre>","data":{"error":"scope denied"}}` en
WebSocket, `{"error":"scope denied"}` en HTTP.

ℹ️ **Aujourd'hui aucune session HTTP ne peut être en portée de service** :
`login_service` n'existe que sur la WebSocket, et le proxy `/mcp` ne construit
aucune session d'API. La garde côté HTTP existe pour le transport qui ouvrira une
session de service ensuite.

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
| `set_param` | ✅ | ✅ | `scope denied` en session service ; paramètres dérivés refusés |
| `del_param` | ✅ | ✅ | `scope denied` en session service ; paramètres dérivés refusés |
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

Les messages `admin_unlock`, `admin_set` et `admin` ne sont pas comptés ici : voir
[Administration](#administration-admin_unlock-admin_set-admin).

Action HTTP inconnue → `{"error":"unknown action"}` **si le corps était un JSON
valide**, sinon HTTP 400 + fermeture
([JsonApiHandlerHttp.cpp:189-214](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).
Message WS inconnu (session authentifiée) → **aucune réponse**, il n'y a pas de
branche `else` ([JsonApiHandlerWS.cpp:156-229](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).

⚠️ **« Paramètres dérivés refusés » a une contrepartie, et elle n'est pas une
faille : `config` / `put` reste ouvert sous la MÊME authentification.** Le refus
de `set_param` / `del_param` ne rend pas les paramètres de définition
inatteignables ; il rend leur écriture **bruyante**. `config put` réécrit un
fichier entier, produit un instantané et demande un redémarrage du serveur, là
où `set_param` écrivait **en silence**, sans trace et sans que rien ne change à
l'écran. C'est ce qui justifie la garde : elle ne ferme pas une porte, elle
supprime le chemin muet.

ℹ️ La garde est un **préfixe**, pas une liste : toute clef commençant par
`autoscenario_` est refusée sur **n'importe quel** IO, et pas seulement sur un
scénario ni seulement pour les clefs que la définition produit aujourd'hui. Rien
d'autre dans `src/` n'utilise ce préfixe, donc la portée élargie est sans effet
mesurable ; elle est plus large que ce que le tableau laisse lire.

---

## Administration (`admin_unlock`, `admin_set`, `admin`)

> **Statut : contrat.** Cette section décrit le protocole que calaos_server **va**
> implémenter (tickets CB2, CB1, CB3, CB4), que la webapp (WA1) et l'installer (CI1) codent
> déjà contre, et dont le mock de la webapp et le faux daemon dérivent. Contrairement au reste
> du document, elle n'est pas dérivée du code. Le plan est
> `docs/admin/PLAN.md` ; le contrat du **daemon** est la section admin de
> [`docs/API.md` de calaos-container](https://github.com/calaos/calaos-container/blob/main/docs/API.md),
> dont le tableau « Relay table » et « Error translation » fixent ce que calaos_server envoie
> et traduit. En cas de divergence entre ce texte et ce fichier sur le côté daemon, `API.md`
> fait foi.

### Principe

Le mot de passe admin est distinct du mot de passe foyer (`cn_user` / `cn_pass`). Il est
vérifié par le daemon calaos-container, jamais par calaos_server. Le déverrouillage rend une
**session admin** opaque (`admin_session`) émise par le daemon.

- **calaos_server ne garde aucune session.** Le client la conserve en mémoire et la renvoie à
  chaque message qui la demande. Elle survit donc à un redémarrage de calaos_server.
- Tout message admin exige d'abord une session foyer : `login` en WS, `cn_user` / `cn_pass` à
  la racine en HTTP. Le mot de passe admin vient **en plus**, pas à la place.
- Les trois messages admin sont **refusés en session service** (sidecar MCP) : voir
  [Portée MCP](#portée-mcp).
- Les trois messages répondent **toujours**, `msg_id` absent ou non (contrairement à
  `set_state` / `register_push`), sauf le cas « ancien calaos_server » ci-dessous.

| Message | WS (`msg`) | HTTP (`action`) |
|---|:---:|:---:|
| `admin_unlock` | ✅ | ✅ |
| `admin_set` | ✅ | ✅ |
| `admin` | ✅ | ❌ (`{"error":"unknown action"}`) |
| `admin_event` (push serveur → client) | ✅ | ❌ |
| `config` / `get_mcp_info` (déjà existants, gardés) | ❌ | ✅ |

### Format d'erreur (WS et HTTP)

Toute erreur admin a la même charge utile, aux deux transports :

```json
{"success": "false", "error": "bad_password", "msg": "Wrong admin password"}
```

- `error` est un **code** du vocabulaire ci-dessous. Le client branche sur `error`, jamais sur `msg`.
- `msg` est une phrase anglaise lisible, destinée aux journaux et au repli d'affichage. Elle
  ne contient jamais de chemin, de sortie de sous-processus ni de chaîne d'erreur Go.
- Des champs facultatifs, **toujours des chaînes**, accompagnent certains codes :
  `retry_after` (secondes) avec `rate_limited`, `job_id` avec `upgrade_in_progress`,
  `min_length` avec `password_too_short`.
- En WS la charge utile est sous `data`, avec `msg` et `msg_id` de l'enveloppe habituelle
  (attention : le `msg` de l'enveloppe est le nom du message, le `msg` de `data` est le texte
  d'erreur) :

  ```json
  {"msg": "admin_unlock", "msg_id": "4",
   "data": {"success": "false", "error": "bad_password", "msg": "Wrong admin password"}}
  ```
- En HTTP la charge utile est le corps de la réponse, avec le statut **200**. Les nombres de
  la colonne « HTTP » du vocabulaire sont les statuts **du daemon**, donnés à titre
  indicatif : calaos_server ne les reproduit pas. Seul l'échec d'authentification foyer
  garde son comportement actuel (HTTP 400 et fermeture).
- Un ancien installer qui reçoit un refus sur `config` affiche `error` tel quel : les codes
  sont donc choisis lisibles. Voir [`config`](#config--clé-type-http-uniquement-2).

Cas d'erreur d'un `config` / `get_mcp_info` refusé : la même charge utile
(`{"success":"false","error":"bad_session","msg":"…"}`), à la place de la réponse habituelle.

### Vocabulaire des codes

| Code | Statut daemon | Cas | Texte conseillé côté client |
|---|---|---|---|
| `admin_not_set` | 409 | l'admin n'est pas défini | proposer le bootstrap |
| `admin_already_set` | 409 | bootstrap alors qu'un admin existe, y compris une course perdue entre deux bootstraps | « Quelqu'un vient de définir le mot de passe admin », avec bannière |
| `bad_password` | 403 | mot de passe admin faux | « Mot de passe admin incorrect » |
| `rate_limited` | 429 | limiteur du daemon (environ 5 échecs par minute, global), vérification déjà en cours, ou `LoginThrottle` de calaos_server | « Trop d'essais, réessayez dans une minute » |
| `bad_session` | 401 | `admin_session` absente, vide, inconnue, expirée ou invalidée (`admin_set`, `admin_reset`, 5 échecs de code push en phase 2) | redemander le mot de passe admin, **sans message d'erreur** |
| `password_too_short` | 400 | nouveau mot de passe de moins de 10 caractères | afficher la longueur minimale (`min_length`) |
| `https_required` | n/a | transport non sûr, voir [Transport](#transport-securetransport) | « Utilisez https:// » |
| `daemon_too_old` | 404 | un ancien daemon, encore en mémoire, ne connaît pas les routes admin | « Activation de l'administration en cours », nouvel essai automatique ; « Redémarrez la box » seulement si l'état dure plus de 2 min |
| `daemon_unreachable` | n/a | daemon injoignable alors que l'admin a déjà été vu défini ; erreur de bearer persistante | réessayer |
| `admin_unavailable` | n/a | daemon injoignable et admin jamais vu défini (Docker, dev, autre distribution) | « Administration indisponible (calaos-container injoignable ; disponible seulement sur Calaos OS) », nouvel essai automatique |
| `upgrade_in_progress` | 409 | une mise à jour est déjà en cours (`update_check`, `update_upgrade*`, `system_reboot`) | « Une mise à jour est déjà en cours » |
| `not_allowed` | 403 | action hors de la table, scope MCP, bootstrap refusé, action de phase 2 pas encore livrée | afficher `msg` |
| `invalid_request` | 400 | champ manquant ou invalide (`new_pass`, `action`, `package`…) | afficher `msg` |
| `package_not_found` | 404 | le paquet de `update_upgrade` n'a pas de mise à jour en attente | afficher `msg` |
| `internal` | 500 | tout le reste ; le détail est dans le journal de calaos_server | message générique |

Les codes de `invalid_request` à `internal` complètent le vocabulaire du plan : ils viennent
de la traduction des erreurs du daemon.

### Traduction des erreurs du daemon

calaos_server traduit la réponse du daemon ainsi (reprise de la section « Error translation »
de `API.md`) :

| Réponse du daemon | Code renvoyé au client |
|---|---|
| connexion refusée, délai dépassé, réponse invalide | `daemon_unreachable` ou `admin_unavailable`, selon [`AdminState`](#état-admin-et-règle-collante) |
| `401` dont le `code` n'est pas `bad_session` (y compris un corps sans `code`), après relecture du token et **une** nouvelle tentative | `daemon_unreachable` |
| `404` sur `GET /api/v2/admin/status` (quel que soit le corps), ou tout `404` sur `/api/v2/` dont le corps n'est pas une enveloppe avec un `code` | `daemon_too_old` |
| `bad_session`, `bad_password`, `rate_limited`, `password_too_short`, `admin_not_set`, `admin_already_set`, `upgrade_in_progress`, `not_allowed` | le même code |
| `invalid_request`, `package_not_found` | le même code, avec le `msg` du daemon (affichable sans risque) |
| `internal`, `not_found`, `method_not_allowed`, tout code inconnu | `internal` |

`rate_limited` recopie `retry_after` ; `upgrade_in_progress` recopie `job_id`. Un `401` sans
`code: bad_session` est une erreur de **bearer**, jamais de session : sans cette distinction,
un token périmé ferait redemander le mot de passe admin en boucle.

Règle du paquet (`update_upgrade`) : calaos_server applique la règle de `API.md`
(section « Upgrade jobs », « `package` must match… ») et **non** la seule expression
`^[A-Za-z0-9.+-]+$` : ce motif, plus le premier caractère lettre ou chiffre, l'absence de
`..` et au plus 128 caractères. Un nom invalide donne `invalid_request` sans appel au daemon.
Le nom voyage dans le corps JSON de l'appel au daemon, jamais dans le chemin ni la requête.

### Transport (`secureTransport`)

Un seul prédicat, `secureTransport()`, décide si un message admin est accepté : le pair TCP
est en **loopback** (haproxy) **et** `X-Forwarded-Proto` vaut `https`. Un en-tête absent
est un refus (fail closed). Aucune interface, aucune plage VPN n'est jugée sûre.

- Requiert `secureTransport()` : `admin_unlock`, `admin_set`, **tous** les `admin`
  (`update_status` compris) et, **une fois l'admin défini**, `config` et `get_mcp_info`.
- Ne le requiert pas : le reste de l'API foyer, ainsi que `config` et `get_mcp_info` tant que
  l'admin n'est pas défini.
- Refus : `https_required`. La connexion WS n'est pas fermée et le handshake sur le port 5454
  en clair reste accepté ; seuls les messages admin sont refusés.
- Un client qui s'est souvenu d'un hôte `http://…:5454` réécrit l'URL en `https://` et
  réessaie **une fois** (CI1). La webapp affiche « Utilisez https:// ».
- Exception, dans l'autre sens : `admin_event` est diffusé à toute connexion WS authentifiée,
  port 5454 compris, car il ne porte rien de sensible (voir [Événements](#événements-admin_event)).

Le prédicat n'est pas infalsifiable : un conteneur du réseau hôte peut forger l'en-tête.

### Messages

#### `admin_unlock`

Vérifie le mot de passe admin et rend une session. Compte dans `LoginThrottle` : un échec
(`bad_password`) compte comme un échec de login, mais **ne ferme pas** le WS foyer.

```json
// WS — requête
{"msg": "admin_unlock", "msg_id": "2", "data": {"admin_pass": "correct horse battery"}}
// WS — réponse
{"msg": "admin_unlock", "msg_id": "2",
 "data": {"success": "true", "admin_session": "9b1f…e4", "expires_in": "900"}}
```

```json
// HTTP — requête (POST /api.php)
{"action": "admin_unlock", "cn_user": "user", "cn_pass": "pass", "admin_pass": "correct horse battery"}
// HTTP — réponse
{"success": "true", "admin_session": "9b1f…e4", "expires_in": "900"}
```

- `admin_session` : 64 caractères hexadécimaux, opaque. `expires_in` : secondes restantes
  (`"900"` à l'émission). La durée est **absolue** : l'utiliser ne la prolonge pas.
- Erreurs : `https_required`, `rate_limited`, `invalid_request` (`admin_pass` absent),
  `admin_not_set`, `bad_password`, `daemon_too_old`, `daemon_unreachable`,
  `admin_unavailable`, `internal`.
- Un nouveau déverrouillage émet une nouvelle session sans invalider les précédentes (le
  daemon en garde au plus 32).

#### `admin_set`

Définit le mot de passe admin (bootstrap) ou le change. Le mode se déduit de la requête :

| `admin_session` | État | Mode | Résultat |
|---|---|---|---|
| absente | admin non défini | **bootstrap** | défini |
| absente | admin défini | | `admin_already_set` |
| présente | admin défini | **changement** | remplacé si la session est valide, sinon `bad_session` |
| présente | admin non défini | | `bad_session` (ou `admin_not_set`) |

Une clef `admin_session` présente mais vide compte comme **présente** (donc `bad_session`),
et `recovery` n'existe pas ici : le mode récupération n'est envoyé que par
`calaos-os admin set`, jamais relayé.

```json
// WS — bootstrap
{"msg": "admin_set", "msg_id": "3", "data": {"new_pass": "a long admin password"}}
// WS — changement
{"msg": "admin_set", "msg_id": "3",
 "data": {"new_pass": "another long password", "admin_session": "9b1f…e4"}}
// WS — réponse
{"msg": "admin_set", "msg_id": "3", "data": {"success": "true", "mode": "bootstrap"}}
```

```json
// HTTP — requête
{"action": "admin_set", "cn_user": "user", "cn_pass": "pass",
 "new_pass": "a long admin password", "admin_session": "9b1f…e4"}
// HTTP — réponse
{"success": "true", "mode": "change"}
```

- `mode` vaut `"bootstrap"` ou `"change"`.
- **Aucune session n'est renvoyée** : toutes les sessions sont invalidées (la sienne
  comprise). Le client enchaîne par `admin_unlock`.
- **Bootstrap refusé** (`not_allowed`, `msg` explicite) si les identifiants foyer effectifs,
  après le repli de `checkCredentials` sur les clés legacy, sont vides, valent `user` / `pass`,
  ou viennent des clés `calaos_user` / `calaos_password`. Le client invite alors à changer le
  mot de passe foyer d'abord (`change_cred`).
- `new_pass` : au moins 10 caractères (points de code Unicode) et au plus 1024 octets UTF-8.
  Plus court : `password_too_short`.
- Erreurs : `https_required`, `invalid_request` (`new_pass` absent), `not_allowed`,
  `admin_already_set`, `admin_not_set`, `bad_session`, `password_too_short`, `rate_limited`,
  `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`, `internal`.
- Chaque réussite publie l'alerte `admin_set` (push à tous les jetons et événement persistant
  affiché au login suivant) et l'événement WS [`admin_event`](#événements-admin_event).
- En phase 2, le changement exigera en plus le code push ; la forme sera ajoutée ici.

#### `admin`

Relaye une action de la table fermée vers le daemon. **WS uniquement.**

```json
{"msg": "admin", "msg_id": "5",
 "data": {"action": "update_upgrade", "admin_session": "9b1f…e4",
          "params": {"package": "calaos-server"}}}
```

```json
{"msg": "admin", "msg_id": "5",
 "data": {"success": "true",
          "result": {"id": "3f9c2a7e5b1d4c80", "kind": "upgrade", "package": "calaos-server",
                     "status": "running", "reboot_required": false, "daemon_restart": false,
                     "started_at": "2026-10-22T09:12:03Z"}}}
```

- `action` est une valeur de la table ci-dessous. Une autre valeur donne `not_allowed`.
  Aucun chemin, en-tête ni champ de corps ne vient du client, à part `params.package`.
- `result` est le champ `data` de la réponse du daemon, **recopié tel quel, avec ses types
  JSON** (booléens, nombres, objets imbriqués). C'est la troisième exception à la règle
  « tout est chaîne » (après l'objet `device` et `eventlog`) ; `success` reste une chaîne.
  Les formes des `result` sont définies par `API.md` (`<Job>`, `<Status>`, etc.).
- `admin_session` est requise partout, **sauf** `update_status`.

| `action` | `params` | Appel du daemon | Session | `result` |
|---|---|---|:---:|---|
| `update_status` | | `GET update/status` | non | `{"progress": <Status>, "job": <Job> ou null}` |
| `update_check` | | `GET update/check` | oui | paquets à mettre à jour |
| `update_available` | | `GET update/available` | oui | idem, sans contacter les miroirs |
| `update_upgrade_all` | | `POST update/upgrade-all` | oui | `<Job>` (le daemon répond 202 et le job tourne) |
| `update_upgrade` | `package` | `POST update/upgrade` | oui | `<Job>` |
| `system_reboot` | | `POST system/reboot` | oui | `null` |
| `system_restart` | | `POST system/restart` (relance le kiosque `calaos-home`) | oui | `null` |
| `system_info` | | `GET system/info` | oui | `{hostname, uptime, cpu_usage, mem_usage}` |
| `mcp_regenerate` | | aucun : traité dans calaos_server | oui | `{}` |
| `approver_list`, `approver_enroll`, `approver_confirm`, `console_open` | | phase 2 | oui | `not_allowed` tant que non livrés |

Chemins relatifs à `/api/v2/`. Jamais relayés, quoi que le client envoie : `system/install/*`,
`system/rollback_snapshot`, `system/fs_status`, `network/*`, `update/installed`,
`admin/reset`, `admin/alerts/ack`, `admin/push/*`, `admin/set` avec `recovery`.

- **`update_status` sans session** : accepté une fois l'admin défini, sous
  `secureTransport()`. C'est ce qui permet à n'importe quel client foyer de reprendre l'affichage
  d'une mise à jour en cours après une reconnexion, sans redemander le mot de passe admin.
  Admin non défini : `admin_not_set`.
- **Session manquante ou vide** sur une action qui l'exige : calaos_server répond lui-même
  `bad_session` et **n'appelle pas** le daemon, car sans l'en-tête le bearer seul
  autorise.
- **`update_upgrade_all` / `update_upgrade`** rendent un `<Job>` sans attendre la fin : suivre
  l'avancement par `admin_event` et `update_status` (voir
  [Reprise après coupure](#reconnexion-et-reprise-de-la-webapp)). Tant que CC3 n'est pas
  livré côté daemon, `/api/v2/` n'a pas de route d'upgrade : le relais `update_upgrade*`
  n'est donc jamais livré avant CC3.
- **`system_reboot`** : `upgrade_in_progress` pendant un job.
- **`mcp_regenerate`** régénère `mcp_token`, puis redémarre le sidecar MCP. Les clients MCP
  déjà configurés avec l'ancien jeton sont refusés ; le nouveau se lit par `get_mcp_info`.
  La session est validée auprès du daemon comme pour `config` (voir plus bas).
- Erreurs possibles : `https_required`, `invalid_request`, `not_allowed`, `bad_session`,
  `admin_not_set`, `upgrade_in_progress`, `package_not_found`, `rate_limited`,
  `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`, `internal`.

### `config` et `get_mcp_info` une fois l'admin défini

Ces deux actions **HTTP** existent déjà ([`config`](#config--clé-type-http-uniquement-2),
[`get_mcp_info`](#get_mcp_info--http-uniquement)). Dès que l'admin est défini, elles exigent :

- `secureTransport()` (sinon `https_required`) ;
- `admin_session` **dans le corps JSON**, jamais dans l'URL : haproxy journalise les URL, et
  le repli sur les paramètres GET (corps non JSON) ne lit **pas** cette clef.
  `get_mcp_info` accepte pourtant ce repli pour ses autres champs : sans session dans le corps,
  la réponse est `bad_session`.

```json
// config get, admin défini
{"action": "config", "type": "get", "cn_user": "user", "cn_pass": "pass",
 "admin_session": "9b1f…e4"}
// get_mcp_info, admin défini
{"action": "get_mcp_info", "cn_user": "user", "cn_pass": "pass", "admin_session": "9b1f…e4"}
// refus
{"success": "false", "error": "bad_session", "msg": "Admin session required"}
```

L'installer déverrouille **une fois** par dialogue (`admin_unlock`), puis réutilise
`admin_session` pour ses `config get` / `put` successifs. `admin_pass` n'est jamais envoyé
avec `config`. La session expire 15 min après l'émission, quoi qu'il fasse : sur `bad_session`
l'installer redemande le mot de passe.

Pour valider la session, calaos_server appelle `GET /api/v2/admin/status` avec
`X-Calaos-Admin-Session`, et n'accepte que `200` avec `admin_set: true`. Si le daemon répond
alors `admin_set: false`, l'admin vient d'être réinitialisé : la règle « non défini »
s'applique (réponse ordinaire).

`change_cred` (mot de passe **foyer**) n'est pas concerné : il reste au niveau foyer, car il
exige déjà l'ancien mot de passe.

Ordre des contrôles pour tout message gardé : portée MCP, `secureTransport()`, état
(`AdminState`), présence de la session, appel du daemon.

### Portée MCP

`admin_unlock`, `admin_set` et `admin` rejoignent la liste des messages refusés en session
service (11 au lieu de 9). Le refus a la forme d'erreur admin avec `not_allowed` et
`"msg": "scope denied"`. La console ne se connecte pas non plus avec un mot de passe : seul un
ticket l'authentifie (phase 2).

### État admin et règle collante

`AdminState` (module de calaos_server, livré par CB2) lit `GET /api/v2/admin/status` du daemon :
au démarrage, puis toutes les 60 s (toutes les 5 s pendant les 2 premières minutes tant que
l'état est inconnu), et à chaque `admin_set` ou `admin_reset` reçu sur `/events` (CB3).
Un drapeau persistant `admin_seen_set` (fichier du même nom dans `Utils::getCachePath()`)
mémorise qu'un admin a été vu défini.

| Daemon | Drapeau | Admin | Effet |
|---|---|---|---|
| joignable, `admin_set: true` | posé | défini | règles ci-dessus |
| joignable, `admin_set: false` | effacé | non défini | `admin_unlock` / `admin` : `admin_not_set` ; `admin_set` bootstrap ; `config`, `get_mcp_info` comme aujourd'hui ; Origin et ACAO comme aujourd'hui |
| joignable, `404` (ancien daemon) | effacé | non défini | `admin*` : `daemon_too_old` ; le reste comme aujourd'hui |
| injoignable ou réponse invalide | **présent** | présumé défini | **fermé** : `config`, `get_mcp_info` et `admin*` répondent `daemon_unreachable` ; le contrôle d'Origin s'applique et ACAO n'est plus reflété |
| injoignable ou réponse invalide | absent | inconnu | tout se comporte comme aujourd'hui (Docker, dev, box sans admin) ; `admin*` répondent `admin_unavailable` |

Quand le daemon est joignable, sa réponse fait foi. Seule la fenêtre « un admin vient d'être
défini mais calaos_server n'a encore jamais joint le daemon » (drapeau absent) se comporte
comme sans admin tant que le daemon est injoignable.

L'URL du daemon est `http://127.0.0.1:8000` ; `CALAOS_CONTAINER_URL` et
`CALAOS_CONTAINER_TOKEN_FILE` la surchargent pour les tests (lues au démarrage, hôte
`127.0.0.1` ou `::1` seulement). Le bearer est lu dans `/run/calaos/calaos-ct.token`, au
démarrage et à chaque `401`.

### Événements (`admin_event`)

calaos_server est abonné en permanence à `/api/v2/events` du daemon (CB3) et pousse aux
clients WS authentifiés, **port 5454 compris**, un message non sollicité (sans `msg_id`) :

```json
{"msg": "admin_event",
 "data": {"type": "upgrade_progress", "ts": "2026-10-22T09:12:05Z",
          "data": {"status": "upgrading", "total_percent": 42, "current_pkg": "calaos-server", "…": "…"}}}
{"msg": "admin_event",
 "data": {"type": "admin_set", "ts": "2026-10-22T09:14:00Z"}}
```

- `ts` : horodatage RFC 3339 UTC. Pour `admin_set` / `admin_reset` c'est l'horodatage de
  l'alerte, **sans** `id` ni autre champ (pas de `data`).
- `upgrade_progress` : `data` est l'objet `<Status>` de `API.md`, tel quel (types JSON natifs).
- C'est **tout** ce que reçoit un client foyer. `upgrade_job` n'est pas retransmis : pour
  connaître le job ou `reboot_required`, lire `admin {update_status}`. `push_code` est consommé
  par calaos_server (phase 2). Tout autre type est ignoré.
- Aucun rejeu : un événement émis pendant qu'un client est déconnecté est perdu.

### Origin

Les contrôles décrits ici sont ceux de CB1 ; ils s'appliquent au handshake WS **et** aux
requêtes HTTP `/api.php`.

- Absence d'`Origin` : acceptée (clients natifs : installer, mobile, remote_ui, MCP).
- `Origin: null` : refusé. Casse, point final et port par défaut sont normalisés ; le port
  n'est comparé que si `Host` en contient un.
- Un Origin est accepté dans trois cas : pair loopback avec `X-Forwarded-Proto: https` et hôte
  de l'Origin égal à `Host` ; hôte qui est une IP portée par une interface de la box ; hôte de
  l'option `api_allowed_hosts`.
- Refus : HTTP 403 avant le handshake ou la requête, sans `Access-Control-Allow-Origin`
  (celui-ci n'est renvoyé que pour un Origin accepté).
- **Tant que l'admin n'est pas défini** (selon `AdminState`), rien ne change : tout Origin est
  accepté et `Access-Control-Allow-Origin` reflète comme aujourd'hui. Les Origins qui seraient
  refusés sont seulement journalisés. Le contrôle s'applique dès que l'admin est défini, et
  aussi quand le daemon est injoignable avec le drapeau `admin_seen_set` présent.
- En développement, le proxy Vite réécrit l'Origin vers la cible (WA2).

### Cas de fonctionnement

| Situation | `admin_unlock` / `admin` | `admin_set` | `config`, `get_mcp_info` | Origin |
|---|---|---|---|---|
| Admin non défini, daemon joignable | `admin_not_set` | bootstrap | comme aujourd'hui | comme aujourd'hui |
| Admin défini, daemon joignable | normal | changement (session) ou `admin_already_set` | session + `secureTransport()` | contrôlé |
| Aucun daemon (Docker, dev), jamais d'admin vu | `admin_unavailable` | `admin_unavailable` | comme aujourd'hui | comme aujourd'hui |
| Daemon injoignable, drapeau posé | `daemon_unreachable` | `daemon_unreachable` | `daemon_unreachable` | contrôlé |
| Ancien daemon encore en mémoire | `daemon_too_old` | `daemon_too_old` | comme aujourd'hui | comme aujourd'hui |
| Ancien calaos_server | **aucune réponse** | aucune réponse | comme aujourd'hui | n/a |

Un ancien calaos_server ignore en silence un message WS inconnu, et répond
`{"error":"unknown action"}` à une action HTTP inconnue. Le client WS doit donc armer le
délai de 5 s ci-dessous.

### Délais

| Délai | Valeur | Qui |
|---|---|---|
| Réponse à `admin_unlock` (client → « calaos-server trop ancien, mettez à jour la box ») | **5 s** | webapp (WA1). Une vérification légitime répond avant, car le daemon refuse tout de suite (`429`) une vérification concurrente au lieu de la mettre en file |
| Appel de calaos_server au daemon (connexion et réponse) | 4 s, donc avant les 5 s du client | calaos_server (valeur proposée à CB2) |
| Durée de la session admin | **15 min absolues** à l'émission, non glissantes | daemon |
| Sondage de `admin/status` | 60 s ; 5 s pendant les 2 premières minutes tant que l'état est inconnu | `AdminState` |
| `daemon_too_old` : bascule sur « Redémarrez la box » | 2 min d'état persistant | webapp |
| `rate_limited` | `retry_after` secondes (champ de l'erreur) | daemon, `LoginThrottle` |
| Ping WS client | 15 s (existant) ; haproxy `timeout tunnel 3600s` | calaos_server |
| Alertes `admin_set` / `admin_reset` | persistées par le daemon jusqu'à l'ack, rejouées à chaque abonné `/events` | daemon |

### Reconnexion et reprise de la webapp

calaos_server ne garde rien : tout l'état utile est côté client ou côté daemon. Une
coupure (redémarrage de calaos_server par sa propre mise à jour, perte de réseau) se
rattrape ainsi :

1. Reconnecter le WS (avec attente croissante), puis `login` foyer.
2. **Garder la même `admin_session`** et la renvoyer. Si la session est encore valable, aucune
   saisie n'est nécessaire (elle survit aussi à un redémarrage du daemon).
3. Envoyer `admin {action: update_status}`. Il ne demande pas de session et donne l'état
   courant de la mise à jour (`result.progress`) et le dernier job (`result.job`). C'est la
   source de vérité : les événements ne sont pas rejoués.
4. Sur `bad_session` à n'importe quel message : redemander le mot de passe admin, **sans
   message d'erreur** (session expirée, ou invalidée par un `admin_set` / `admin_reset`).
   Rejouer ensuite l'action qui avait échoué, sauf `update_upgrade*` : relire d'abord
   `update_status`, car l'upgrade a pu partir.
5. Suivre une mise à jour : lancer `update_upgrade*` et garder `result.id`, afficher les
   `admin_event` `upgrade_progress`, et s'arrêter quand `progress.status` vaut `done` ou
   `error`. Puis lire `update_status` une dernière fois pour `job.reboot_required` et
   `job.error`.
6. Si `update_status` rend `admin_not_set` après un `admin_reset` (événement ou non), revenir
   à l'écran de bootstrap.
7. `daemon_too_old`, `admin_unavailable` et `daemon_unreachable` sont transitoires : réessayer
   automatiquement avec une attente croissante, sans effacer la session.
8. Un `admin_event` `admin_set` ou `admin_reset` reçu pendant qu'une session est ouverte
   signifie que celle-ci est invalidée : l'oublier. Les alertes manquées pendant une
   déconnexion sont rejouées par le daemon à calaos_server, qui les présente au login suivant
   (événement persistant).

Compatibilité : webapp neuve contre calaos_server ancien, `admin_unlock` reste sans réponse
(voir Délais) ; installer ancien contre serveur neuf, le refus de `config get` est affiché
tel quel (`error` est lisible).

---

## Sous-actions

Sept commandes redispatchent sur une sous-clé. **37 sous-actions au total**, ce
qui porte l'API à **57 opérations distinctes** (20 commandes feuilles + 37
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
non numérique donne `{"error":"wrong from/count"}`. Les **seize** refusent par
ailleurs `{"error":"no music database"}` quand le player n'a pas de base
musicale — voir « pièges » ; ce refus arrive **après** celui de `from`/`count`.

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

### `autoscenario` — clé `type` (8 des deux côtés)

| `type` | Arguments | Réponse |
|---|---|---|
| `list` | — | `{"scenarios":[…]}` |
| `get` | `id` | l'objet scénario, ou `{"error":"wrong input"}` |
| `create` | `name`, `visible`, `cycle`, `disabled`, `room_name`, `room_type`, `steps` | `{"id":"…"}` ou `{"error":"scenario creation failed"}` |
| `delete` | `id` | `{"success":"true"}` |
| `modify` | idem `create` + `id` | `{"success":"true"}` ou `{"error":"scenario modification failed"}` |
| `add_schedule` | `id` | `{"id":"<id de la plage horaire>"}` |
| `del_schedule` | `id` | `{"success":"true"}` |
| `reenable` | `id` | `{"success":"true"}`, ou `{"error":"wrong input"}`, ou le **refus** `{"error":"scenario still references missing IOs: <ids>"}` — voir « scénario désactivé » plus bas |

⚠️ Un `type` inconnu (ou absent) **ne produit aucune réponse** sur les deux
transports : il n'y a pas de branche `else`
([JsonApiHandlerHttp.cpp:880-901](../src/bin/calaos_server/JsonApiHandlerHttp.cpp),
[JsonApiHandlerWS.cpp:474-495](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
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

Une fois l'admin défini, `config` exige `secureTransport()` et une `admin_session` : voir
[Administration](#administration-admin_unlock-admin_set-admin).

| `type` | Effet | Réponse |
|---|---|---|
| `get` | sauvegarde puis relit `io.xml`, `rules.xml`, `local_config.xml` | `{"config_files":{…},"io_format":2,"success":"true"}` |
| `put` | écrit les fichiers fournis dans `config_files` (et `io_format`, voir ci-dessous) après backup, puis **redémarre le serveur** une fois la réponse écrite | `{"success":"true"\|"false"}` ; refus : `{"success":"false","error":"…"}` |

`type` inconnu → `{"success":"false"}`. En `put`, seuls les trois noms de
fichiers connus sont acceptés et le contenu doit commencer par `<?xml`
([JsonApiHandlerHttp.cpp:594-686](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

**`io_format`** (entier) annonce le plus haut format d'`io.xml` que le serveur écrit et
comprend ; 2 = peut contenir des `<calaos:device>` (`<calaos:home format="2">`, écrit dès qu'un
device existe). Une réponse sans `io_format` vient d'un serveur de format 1 : un client ne doit
pas lui envoyer de devices.

**Refus d'un `put` qui perdrait des devices.** Le client déclare son support dans la **requête**,
pas dans le fichier : `config put` accepte une clef `io_format` à côté de `config_files`
(entier ou chaîne entièrement décimale, de 2 à 1000 ; absente, inexploitable, hors bornes ou `< 2` = client de format 1).
Si le serveur détient au moins un device (les devices inertes d'impl inconnue comptent) et que
la requête ne déclare pas `io_format >= 2`, le `put` est refusé en entier : ni backup, ni
écriture (`rules.xml` et `local_config.xml` compris), ni redémarrage. Un `io.xml` qui ne
s'analyse pas est toujours refusé, déclaration ou non, avec `"error":"io.xml does not parse"`
(sans device côté serveur il reste accepté, comme avant). Réponse :
`{"success":"false","error":"update calaos_installer: this io.xml would drop N device(s)"}`
avec N le nombre de devices du serveur. Un client qui déclare `io_format >= 2` est cru : le
contenu n'est plus inspecté, supprimer des devices, le dernier compris, est intentionnel. Sans
device côté serveur, ou sans clef `io.xml` dans `config_files`, rien n'est jugé.

### `camera` — clé `type` (HTTP uniquement, 2)

| `type` | Réponse |
|---|---|
| `get_picture` | un JPEG (`Content-Type: image/jpeg`) ; `camfail.jpg` si la caméra ne répond pas |
| `get_video` | flux MJPEG relayé, ou flux construit image par image si la caméra n'a pas d'URL vidéo |

`id` inconnu → `{"error":"unkown camera id"}`. `type` inconnu → **rien**, pas de
branche `else` ([JsonApiHandlerHttp.cpp:903-1029](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

### `settings` — clé `action` (WS uniquement, 1)

| `action` | Arguments | Réponse |
|---|---|---|
| `change_cred` | `old_user`, `old_pw`, `new_user`, `new_pw` | `{"action":"change_cred","success":"true"\|"false"}` |

En cas de succès, la session est **déconnectée** (`loggedin = false`) : il faut
se relogguer avec les nouveaux identifiants
([JsonApiHandlerWS.cpp:516-528](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
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
| `action: event_picture` | octets JPEG bruts, ou HTTP 404 si `pic_uid` est introuvable ([JsonApiHandlerHttp.cpp:1087-1108](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) |

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
lus dans les params) — un IO string vide donne `"state": ""`. Pour un IO
`audio_player`, `state` est le **dernier évènement** (`onplay`, `onpause`, `onstop`,
`onsongchange`, `onplaylistchange`, `onvolumechange`, `onerror`), `""` avant le premier ; ne pas le
confondre avec le `status` de `get_state` (`playing`, `pause`, `stop`…, voir plus bas).

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

### Devices : commande et objet `device`

Spec : [CALAOS_DEVICES_FEAT.md](../CALAOS_DEVICES_FEAT.md) §5. Un device (`<calaos:device>`,
voir [01_core_data_model.md](01_core_data_model.md#device)) est un IO comme un autre pour
l'API : pas de nouvelle commande.

**Commande.** `set_state` (et les règles, Lua `calaos:set_io`, MCP) passe une chaîne à
`Device::set_value()` :

- une chaîne qui commence par `{` est un **objet JSON de mise à jour partielle** :
  `{"mode":"heat","target_temp":21.5}`. Seuls les champs présents sont appliqués ;
- toute autre chaîne est un **mode simple** : `off` équivaut à `{"mode":"off"}`.

Dans le JSON, les nombres sont de vrais nombres JSON : `"21.5"` est refusé. Chaque champ est
validé par la famille : il doit exister **pour cette instance** (une capacité n'existe que si
son lien existe), une valeur d'énumération doit appartenir à la liste, un nombre est arrondi
au pas puis borné à `[min, max]` (`40` devient `30`, `21.3` avec un pas de `0.5` devient
`21.5`, sans résidu binaire). `mode` est appliqué en premier, les autres champs ensuite ; une
famille peut valider ensemble des champs liés (plage `target_temp_low`/`_high`), pour que le
résultat ne dépende pas de l'ordre des clés. C'est du **best effort** : un champ refusé est
journalisé (`cWarningDom("device")`, avec l'id du device et la raison ; un nom de champ hors
`[a-z0-9_]` et la valeur reçue ne sont jamais écrits en clair) et les champs valides sont
appliqués quand même. Un JSON invalide (y compris un nombre hors de la plage d'un double,
`1e400`) est refusé en entier. `[1]` ou `"x"` ne commencent pas par `{` : ce sont des modes,
refusés comme tels.

La réponse garde le contrat de `set_state` : `{"success":"true"}` si **au moins un champ** a
été appliqué, `{"success":"false"}` sinon (tous refusés, JSON invalide, objet vide). Un succès
peut donc cacher des champs refusés ; le journal les nomme. Toute commande, même refusée,
publie ensuite un `io_changed` du device : un client qui affichait la valeur demandée
retrouve l'état réel sans attendre.

Requête WS (dérivée, `tests/core/DeviceApi_test.cpp`) — la valeur est une **chaîne** qui
contient le JSON :
```json
{"msg": "set_state", "msg_id": "1",
 "data": {"id": "clim", "value": "{\"mode\":\"heat\",\"target_temp\":23}"}}
```

**Publication dans `get_home`.** L'item du device porte `state` (son scalaire : le mode,
celui que voient les règles), `gui_type` = la famille (`climate`) et un objet `device` ;
c'est le seul objet typé d'un item, ses nombres et booléens sont de vrais types JSON. Il est
aussi présent dans `get_io`, pas dans la configuration des écrans RemoteUI. Les IOs possédés
restent des items ordinaires (`visible="false"`), sans objet
`device`. Extrait capturé, `tests/core/golden/core4_http_get_home_device.json` (device de
test) :
```json
{
  "id": "clim", "type": "climate", "gui_type": "climate", "state": "cool",
  "var_type": "string", "io_type": "inout", "name": "clim",
  "device": {
    "family": "climate",
    "impl": "test_climate",
    "capabilities": {
      "modes": ["off", "heat", "cool"],
      "target_temp": {"min": 16, "max": 30, "step": 0.5, "unit": "°C"},
      "current_temp": true
    },
    "state": {"mode": "cool", "target_temp": 21.5, "current_temp": 23.4},
    "ios": {"mode": "clim_mode", "target_temp": "clim_target", "current_temp": "ext_temp"}
  }
}
```

- `family` et `impl` : les attributs `type` et `impl` du `<calaos:device>` ;
- `capabilities` et `state` : remplis par la famille, seulement pour les liens présents ;
- `ios` : rôle → id de l'IO, pour chaque lien dont l'IO existe (un lien non résolu
  n'apparaît pas).

**Publication dans `io_changed`.** Quand l'IO d'un de ses liens change, ou après une commande,
le device émet **un** `io_changed` pour son propre id au bout de 100 ms : les
changements de cette fenêtre sont regroupés, et la fenêtre n'est pas prolongée par les
suivants (un flux continu publie toutes les 100 ms). L'événement porte `state` et l'objet
`device` **entier** :
```json
{"msg": "event", "data": {
  "type": "3", "type_str": "io_changed",
  "event_raw": "io_changed id:clim state:heat",
  "data": {"id": "clim", "state": "heat",
           "device": {"family": "climate", "impl": "test_climate", "capabilities": {…},
                      "state": {"mode": "heat", "current_temp": 23.4, …}, "ios": {…}}}}}
```

L'objet `device` est **toujours envoyé entier**, dans `get_home` comme dans chaque
`io_changed` : un client **remplace** sa copie, il ne la fusionne jamais, pour qu'une
capacité ou une entrée `ios` retirée par le serveur ne survive pas chez lui. Un client qui ne
connaît pas les devices continue de lire `state`. La forme plate `event_raw` ne porte que les
chaînes (`id`, `state`) ; le JSON brut de l'historique (si `log_history`) contient l'objet.

Le changement du scalaire d'un device (son mode) déclenche aussi ses règles, comme pour tout
IO : une condition « `clim` == `heat` » est évaluée dès que le mode change, qu'il vienne d'une
commande ou du lien `mode`.

**Famille `climate`** (`ClimateBase`, spec §6). Capacités, chacune présente seulement si son
lien existe :

- `modes` : lien `mode`, liste restreinte par le param `modes` (sinon les six) ;
- `target_temp` : `min`/`max`/`step`/`unit` lus sur l'IO écrit par le lien (son `command_io`,
  sinon son `io`), à défaut 7, 35, 0.5 et `°C`. Sans lien `target_temp` mais avec une plage,
  l'objet vient des IOs de la plage (`min` de low, `max` de high) ; avec une plage, il porte
  aussi `min_range` ;
- `target_temp_range` : liens `target_temp_low` **et** `_high` (reste un booléen) ;
- `reports_action` : lien `action` ;
- `fan_modes` / `swing_modes` : liste du param du même nom, séparée par des virgules ;
- `remote_temp` : liens `remote_temp` **et** `remote_temp_source`.

L'état porte `mode`, `current_temp`, `target_temp`, `target_temp_low`/`_high`, `action`,
`fan_mode`, `swing_mode`. Un nombre n'y figure qu'une fois que l'IO lié a reçu une valeur
(`IOBase::hasValue()` : un changement signalé, ou une valeur sauvegardée restaurée), pour ne
jamais publier le 0 d'un capteur pas encore lu. Un mode ou une action inconnus (`heat_cool`),
un `fan_mode`/`swing_mode` hors de sa liste ou vide sont omis plutôt que relayés.

Le scalaire (§5.1) est le mode : une chaîne vide signifie **mode inconnu** (lien `mode` qui n'a
rien reçu ou valeur hors énumération), et c'est `""` que voient alors les règles.

`target_temp_low` et `target_temp_high` sont validés **ensemble**, l'absent pris dans l'état
courant (refusé s'il n'a encore rien reçu) : si `high - low` est inférieur au param
`min_range` (0 par défaut), la paire est refusée **entière** et journalisée, les autres champs
de la commande s'appliquent quand même. Une écriture refusée par l'IO lié (entrée, IO en
lecture seule) est journalisée avec la raison « the linked IO refused the value ».

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
([JsonApi.cpp:1800-1804](../src/bin/calaos_server/JsonApi.cpp)). IO qui n'est
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
    "schedule": "false",
    "broken": "false", "disabled_missing_io": "false", "missing_ios": "",
    "steps_count": "2",
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
> d'étapes **réelles** ([IO/Scenario.cpp:141](../src/bin/calaos_server/IO/Scenario.cpp)),
> puis la boucle émet ces étapes ([:145-168](../src/bin/calaos_server/IO/Scenario.cpp))
> et une étape terminale **synthétique** `step_type: "end"` est ajoutée
> **hors de la boucle** ([:171-189](../src/bin/calaos_server/IO/Scenario.cpp)).
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

### Scénario désactivé : `broken`, `disabled_missing_io`, `missing_ios`, `reenable`

Un scénario dont une étape référence un IO introuvable est **entièrement
désactivé** : il ne démarre plus, ni à l'heure programmée, ni sur commande
([IO/Scenario.cpp:80-91](../src/bin/calaos_server/IO/Scenario.cpp) — la coupure
est dans `set_value()`, et **seulement** sur `val == true`, pour que l'arrêt
d'un scénario déjà lancé reste possible). Il reste **visible et intact dans la
configuration** — rien n'est perdu à la sauvegarde.

> ⚠️ **Le payload, lui, est amputé : l'action dont l'IO manque DISPARAÎT du
> tableau `steps`.** `Scenario::toJson()` saute toute action dont l'IO ne résout
> pas (`if (!sa.io) continue;`, [IO/Scenario.cpp:158](../src/bin/calaos_server/IO/Scenario.cpp)
> pour les étapes réelles et [:181](../src/bin/calaos_server/IO/Scenario.cpp) pour
> l'étape `end`), **sans rien émettre à la place**. Les deux goldens cités par
> cette section le montrent côte à côte : la deuxième étape porte **2** actions
> dans `e40c_ws_autoscenario_get.json` et **1** seule dans
> `e40c_ws_autoscenario_get_broken.json` — l'action visant `e40c_target` a
> simplement disparu, et une étape qui perd sa seule action est répondue avec un
> tableau `actions` **vide**. Le comportement est épinglé sous ce nom :
> `ABrokenStepSilentlyLosesItsActionFromThePayload`
> ([tests/core/JsonApiScenario_test.cpp:1771](../tests/core/JsonApiScenario_test.cpp)).
>
> **Conséquence directe pour un client : `steps` ne permet PAS de dire quelle
> étape est en cause.** L'étape amputée est indiscernable d'une étape qui a
> toujours eu moins d'actions, et le tableau ne porte aucun marqueur. Une
> interface qui veut désigner le problème à l'utilisateur doit lire
> **`missing_ios`** — c'est la seule clé du payload qui nomme ce qui manque.
> C'est le constat (d) de T3.18, et c'est le défaut qui a motivé la refonte
> `E4.6`.

Le payload de scénario porte donc **trois clés** que tout client doit lire. Elles
sont **produites, jamais consommées** : `autoscenario create` / `modify` ne
construisent leurs `Params` que sur six clés nommées en dur (`auto_scenario`,
`name`, `visible`, `cycle`, `disabled`, plus la pièce) et **ignorent** les trois
([JsonApi.cpp:2036-2044](../src/bin/calaos_server/JsonApi.cpp),
[IO/Scenario.cpp:123-139](../src/bin/calaos_server/IO/Scenario.cpp)) :

| Clé | Valeur | Sens |
|---|---|---|
| `broken` | `"true"` / `"false"` | **dérivé, jamais persisté** : au moins une étape référence un IO qui ne résout pas, ici et maintenant |
| `disabled_missing_io` | `"true"` / `"false"` | **persisté et collant** : écrit comme paramètre de l'IO scénario, il survit au redémarrage et à la réapparition de l'IO ([Scenario/AutoScenario.cpp:112, 258](../src/bin/calaos_server/Scenario/AutoScenario.cpp)) |
| `missing_ios` | chaîne, ids séparés par `, ` | ce qu'il faut réparer ; **vide** quand il n'y a rien à réparer ([Scenario/AutoScenario.cpp:212-247](../src/bin/calaos_server/Scenario/AutoScenario.cpp)) |

⚠️ **Les deux drapeaux divergent volontairement, et c'est l'état qui compte.**
`broken: "false"` avec `disabled_missing_io: "true"` signifie « réparé, en
attente d'une réactivation manuelle » — c'est l'état qu'une interface doit
transformer en bouton « réactiver ». Il est capturé tel quel (capturé,
`tests/core/golden/e40c_ws_autoscenario_get_repaired.json`, **extrait de `data`**) :

```json
{"broken": "false", "disabled_missing_io": "true", "missing_ios": ""}
```

Un scénario réellement cassé porte les trois ensemble (capturé,
`tests/core/golden/e40c_ws_autoscenario_get_broken.json`, **extrait de `data`**) :

```json
{"broken": "true", "disabled_missing_io": "true", "missing_ios": "e40c_target"}
```

Noter que `broken` peut être vrai avec `missing_ios` **vide** : une règle
d'étape *détruite* ne laisse aucun id à nommer, contrairement à un IO simplement
introuvable. (Dérivé : `isBroken()` répond vrai sur le seul verrou
`stepRuleDestroyed`, **avant** tout scan
([Scenario/AutoScenario.cpp:185](../src/bin/calaos_server/Scenario/AutoScenario.cpp)),
tandis que `getMissingIoDescription()` ne collecte rien d'une `RuleRef` nulle
([:216-226](../src/bin/calaos_server/Scenario/AutoScenario.cpp)) — aucun golden
ne capture ce coin.)

⚠️ **`disabled_missing_io` reste néanmoins écrivable par `set_param`
aujourd'hui** — [`T3.137`](refactoring/T3.137.md) ferme les paramètres de la
**définition** (`autoscenario_*` et `as_<étape>_pause` / `_actions`), et laisse
celui-ci délibérément ouvert, et l'écriture est **asymétrique** : elle atteint la
configuration mais **pas** le booléen en mémoire, si bien qu'un scénario sain
marqué à la main continue de tourner **jusqu'au redémarrage**, où il se retrouve
désactivé. C'est un défaut connu, tranché mais **non implémenté** (`T3.20` :
le paramètre deviendra en lecture seule côté API). Un client ne doit pas s'en
servir : la seule écriture supportée est `autoscenario reenable`.

**La réactivation est manuelle et peut être refusée.** Elle n'est pas un
`set_param` — un `set_param` ne sait pas refuser *ce paramètre-là* — mais une commande à part
entière, `autoscenario` / `type: "reenable"`, disponible sur les **deux**
transports ([JsonApiHandlerHttp.cpp:897-901](../src/bin/calaos_server/JsonApiHandlerHttp.cpp),
[JsonApiHandlerWS.cpp:491-495](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).
Trois réponses possibles (dérivées de
[JsonApi.cpp:2197-2226](../src/bin/calaos_server/JsonApi.cpp) et
[Scenario/AutoScenario.cpp:272-306](../src/bin/calaos_server/Scenario/AutoScenario.cpp)) :

- `id` qui n'est pas un scénario → `{"error":"wrong input"}` ;
- le scénario est **encore** cassé → **refus**, et le refus **nomme** les
  équipements : `{"error":"scenario still references missing IOs: <ids>"}` ;
- sinon `{"success":"true"}`, le drapeau est levé, `io.xml` est réécrit et un
  événement `scenario_changed` est émis. Réactiver un scénario qui n'était pas
  désactivé est un **no-op qui réussit**, pas une erreur.

### `eventlog`

Requête (dérivée, [JsonApi.cpp:2228-2371](../src/bin/calaos_server/JsonApi.cpp)) :
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
([HistLogger.cpp:289-293](../src/bin/calaos_server/HistLogger.cpp)).

Les événements sont rendus du plus récent au plus ancien (`created_at`, à la
seconde) ; ceux d'une même seconde, du plus ancien au plus récent. Un index
`events_created_at` sert la lecture paginée, il est ajouté au démarrage aux
bases existantes.

#### Toute lecture reçoit une réponse

Un verrou passager sur `events.db` est attendu jusqu'à 2 s. Quand la requête
SQL échoue malgré tout (base verrouillée plus longtemps pendant une mise à jour
ou au démarrage, fichier illisible), la réponse est l'erreur habituelle d'`eventlog`,
avec la raison SQLite, sur les deux chemins (paginé et `uuid`) :

```json
{"error": "history unavailable: database is locked"}
```
En WebSocket, ce document est dans `data` et le `msg_id` de la requête est
renvoyé tel quel, comme pour toute réponse `eventlog` :
```json
{"msg": "eventlog", "msg_id": "eventlog_page:3:1",
 "data": {"error": "history unavailable: database is locked"}}
```
*(capturés, `tests/core/golden/evlog_http_eventlog_db_locked.json` et
`evlog_ws_eventlog_db_locked.json`, enveloppe comprise.)* Si la base n'a pas pu
être ouverte du tout, l'erreur est `history is disabled`. Un client qui
numérote ses pages dans `msg_id` (`eventlog_page:<génération>:<page>`) reçoit
donc toujours une réponse par page demandée.

#### `per_page` est validé, et son refus est le seul refus **synchrone**

`per_page ≤ 0` est refusé **avant** que la requête n'atteigne le thread SQLite
([JsonApi.cpp:2337-2342](../src/bin/calaos_server/JsonApi.cpp)) — c'est la garde
T3.19, qui remplace une division entière par zéro fatale au processus :

```json
{"error": "per_page is out of range"}
```
*(capturé, `tests/core/golden/t319_http_eventlog_per_page_out_of_range.json`,
**intégral** ; le golden WebSocket `t319_ws_eventlog_per_page_out_of_range.json`
porte le même document, car le test y capture le contenu de `data` sans son
enveloppe.)*

Ce qu'un client doit en retenir, et qui n'est pas intuitif :

- **la garde teste la valeur, pas le succès du parsing** — et la distinction
  n'est pas intuitive. Un `per_page` **absent ou vide** garde le défaut `100` et
  reçoit sa réponse : sur un flux vide l'extraction échoue **au sentry**, avant
  `num_get`, donc la variable n'est jamais écrasée. Un `per_page` **illisible**
  (`"abc"`, `"true"`) atteint `num_get`, qui écrit `0` en échec : il est
  **refusé**. Un parse partiel (`"1,5"`) vaut `1` et est servi. Les deux
  premiers cas sont épinglés par `AnEmptyPerPageKeepsTheDefaultHundred` et
  `AnAbsentPerPageKeepsTheDefaultHundred` ;
- **un `per_page` négatif est désormais refusé alors qu'il recevait des
  données.** C'est le seul changement de contrat client de T3.19 : SQLite lit un
  `LIMIT` négatif comme « pas de limite », si bien que la requête renvoyait
  **toutes** les lignes du journal sous un document annonçant `per_page: -5` ;
- **`page`, elle, n'est pas gardée ici** — c'est délibéré. Son refus reste celui
  de `HistLogger`, **asynchrone** et formulé autrement
  (`page is out of range`, [HistLogger.cpp:248-254](../src/bin/calaos_server/HistLogger.cpp)).
  Une `page` illisible est lue comme `0`, donc **indistinguable** de la page 0.

### `register_push`
Enregistre le jeton push d'un appareil (FCM pour Android, APNs pour iOS). Les
arguments sont dans `data` en WebSocket et à la racine en HTTP, où `cn_user` et
`cn_pass` sont exigés comme pour toute action.
```json
{"msg": "register_push", "msg_id": "1",
 "data": {"token": "<jeton>", "hardware": "android", "push_format": "data"}}
```
```json
{"cn_user": "user", "cn_pass": "pass", "action": "register_push",
 "token": "<jeton>", "hardware": "android", "push_format": "data"}
```
Réponse : `{"success":"true"}` (dans `data` en WebSocket, muette sans `msg_id`).

| Champ | Valeurs | Défaut |
|---|---|---|
| `token` | jeton de l'appareil, non vide | — (refus `"false"`) |
| `hardware` | `android` ou `ios`, exactement | — (refus `"false"`) |
| `push_format` | `notification` ou `data` (chaîne) | `notification` |

- `push_format` est **optionnel** : les apps des stores (Android 3.1.x, iOS) ne
  l'envoient pas et gardent le format `notification`, inchangé.
- `data` est réservé à l'app Android Qt 6 (SDK Firebase Java) : le serveur lui
  envoie un message FCM **data seul**, que l'app affiche elle-même. Le format du
  message est décrit dans
  [10_events_notifications.md](10_events_notifications.md#format-data-android).
  Sur iOS, `data` est mémorisé mais sans effet.
- Une valeur inconnue (casse comprise : `Data`) est **acceptée** et traitée
  comme `notification` ; un avertissement est journalisé une fois par processus,
  sans le jeton ni la valeur.
- Réenregistrer un jeton déjà connu rafraîchit sa date et **remplace** son
  format par celui de la requête : une requête sans `push_format` le remet en
  `notification`. L'app doit donc envoyer `push_format` à chaque
  enregistrement.
- Les jetons non réenregistrés depuis 30 jours sont supprimés.

### `get_mcp_info` — **HTTP uniquement**

Cette action n'existe **pas** dans le dispatch WebSocket : elle n'est câblée que
dans `JsonApiHandlerHttp`
([JsonApiHandlerHttp.cpp:176-188](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).
Une fois l'admin défini, elle exige `secureTransport()` et une `admin_session` dans le corps :
voir [Administration](#administration-admin_unlock-admin_set-admin).
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
| `unkown camera id` | [JsonApiHandlerHttp.cpp:909](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) | **coquille** — `id` fourni mais pas une caméra |
| `wrong item` | [JsonApi.cpp:1089](../src/bin/calaos_server/JsonApi.cpp) | `item` absent ou non entier |
| `wrong from/count` | 14 occurrences dans les builders `audio_db` | `from`/`count` absents ou non entiers |
| `wrong io/param` | [JsonApi.cpp:675, 703, 733](../src/bin/calaos_server/JsonApi.cpp) | `get_param`/`set_param`/`del_param` : IO ou param invalide |
| `param refused` | [JsonApi.cpp:1270, 1313](../src/bin/calaos_server/JsonApi.cpp) (`ret = {{ "error", "param refused" }};`) | `set_param`/`del_param` : le modèle ou l'API refuse cette clef — `id`, un paramètre structurel, un octet illisible en XML, ou un paramètre de définition de scénario. **Distinct de `wrong io/param`**, qui veut dire *introuvable* |
| `wrong id` | [JsonApi.cpp:609, 650](../src/bin/calaos_server/JsonApi.cpp) | `get_states` / `query` : IO introuvable |
| `wrong input` | [JsonApi.cpp:1777, 1815, 1898, 2004, 2030, 2157, 2180, 2203](../src/bin/calaos_server/JsonApi.cpp) | plage horaire ou scénario introuvable |
| `scenario creation failed` | [JsonApi.cpp:1938, 1950](../src/bin/calaos_server/JsonApi.cpp) | |
| `scenario modification failed` | [JsonApi.cpp:2136](../src/bin/calaos_server/JsonApi.cpp) | |
| `uuid not found` | [HistLogger.cpp:292](../src/bin/calaos_server/HistLogger.cpp) | `eventlog` avec un `uuid` inconnu |
| `history unavailable: <raison>` | [HistLogger.cpp:549](../src/bin/calaos_server/HistLogger.cpp) | `eventlog` : la requête SQL a échoué (ex. `database is locked`) — refus **asynchrone** ; la raison est le message générique de SQLite, jamais le contenu d'un événement |
| `history is disabled` | [HistLogger.cpp:441](../src/bin/calaos_server/HistLogger.cpp) | `eventlog` : `events.db` n'a pas pu être ouvert au démarrage |
| `page is out of range` | [HistLogger.cpp:252](../src/bin/calaos_server/HistLogger.cpp) | `eventlog` : `page` négative ou au-delà de `total_page` — refus **asynchrone**, formulé par `HistLogger` |
| `per_page is out of range` | [JsonApi.cpp:2339](../src/bin/calaos_server/JsonApi.cpp) | `eventlog` : `per_page` ≤ 0 (T3.19) — refus **synchrone**, la base n'est jamais atteinte |
| `no music database` | [JsonApi.cpp:982](../src/bin/calaos_server/JsonApi.cpp) | `audio_db` sur un player sans base (T3.19) |
| `scenario still references missing IOs: <ids>` | [Scenario/AutoScenario.cpp:281](../src/bin/calaos_server/Scenario/AutoScenario.cpp) | `autoscenario reenable` refusé (T3.18) — la seule chaîne d'erreur de l'API à porter une **partie variable** |
| `scope denied` | [JsonApiHandlerWS.cpp:162](../src/bin/calaos_server/JsonApiHandlerWS.cpp) | message refusé en session `serviceScope` |
| `invalid token` | [JsonApiHandlerWS.cpp:544, 555](../src/bin/calaos_server/JsonApiHandlerWS.cpp) | `login_service` refusé |
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
([JsonApi.cpp:1018-1023](../src/bin/calaos_server/JsonApi.cpp)) — c'est visible
dans le golden `t317b_http_audio_db_get_stats.json`. Mais
`audioGetPlaylistSize()` et `audioGetTime()` ajoutent `audio_action` aux params
du player puis construisent un **nouveau** `Params` pour la réponse : le champ
n'atteint jamais le client
([JsonApi.cpp:1040-1046, 1064-1070](../src/bin/calaos_server/JsonApi.cpp)),
confirmé par `t317b_http_audio_get_playlist_size.json` et
`t317b_http_audio_get_time.json`. Un client ne peut donc pas se fier à
`audio_action` pour corréler une réponse à sa requête : il faut utiliser
`msg_id` (WS) ou l'ordre des requêtes (HTTP).

### `processDbResult()` : la ligne `count` est une ligne du tableau

`processDbResult()` recopie **tous** les `Params` renvoyés par la base dans
`items`, y compris celui qui porte la clé `count`
([JsonApi.cpp:1130-1152](../src/bin/calaos_server/JsonApi.cpp)). Ce `Params`
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

### `audio_db` sur un player sans base de données est refusé, pas servi

`AudioPlayer::database` vaut `nullptr` par défaut
([AudioPlayer.cpp:26-28](../src/bin/calaos_server/Audio/AudioPlayer.cpp)) et
seul `Squeezebox` l'initialise
([Squeezebox.cpp:81](../src/bin/calaos_server/Audio/Squeezebox.cpp)) — un player
Roon, celui de la configuration de référence, n'en a pas. Les 16 builders
`audio_db` déréférençaient ce pointeur sans test : la commande **arrêtait net le
serveur**. Depuis T3.19 les 16 passent par une garde commune,
`audioDbUnavailable()` ([JsonApi.cpp:976-985](../src/bin/calaos_server/JsonApi.cpp)),
et répondent (capturé, `tests/core/golden/t319_http_audio_db_no_database.json`,
**intégral**) :

```json
{"error": "no music database"}
```

Trois précisions qui décident du code client :

- **La garde teste le pointeur, pas `canDatabase()`.** Ce drapeau est une
  constante par classe ([AudioPlayer.h:101](../src/bin/calaos_server/Audio/AudioPlayer.h))
  qui n'est que **publiée** dans `get_home` : les deux peuvent diverger. Un
  player qui annonce `database: "true"` mais dont le pointeur est nul est
  **refusé** ; l'inverse est **servi**. Le champ `database` de `get_home` reste
  donc une indication, pas une garantie.
- **La garde est appelée *après* le contrôle de `from`/`count`**, jamais avant :
  une requête déjà mal paginée continue de recevoir `{"error":"wrong from/count"}`.
- **Une base vide n'est pas une base absente** : elle répond une liste vide,
  comme avant. Seule l'**absence** de base est refusée.

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
tables de dispatch** : il n'est pas atteignable depuis cette API. Ce n'est pas
pour autant du code mort — son unique appelant est le moteur Lua, qui s'en sert
pour bâtir la table `context` d'un script
([LuaScript/ScriptExec.cpp:162](../src/bin/calaos_server/LuaScript/ScriptExec.cpp)).

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
