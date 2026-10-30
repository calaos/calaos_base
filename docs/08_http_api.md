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
| WebSocket | `/api/console` | GET + upgrade, sous-protocole `calaos-console.v1` | `ConsoleTunnel`, voir [Console web](#console-web-apiconsole) |

Tout autre chemin est rejeté en 404 avant d'atteindre le JsonApi
([HttpClient.cpp:458-470](../src/bin/calaos_server/HttpClient.cpp)).
`/api/v2` et `/api/v3…` passent le filtre de chemin mais n'ont **pas** de
handler JsonApi : le serveur logge `API version not implemented` et **ne répond
rien du tout** ([HttpClient.cpp:650-659](../src/bin/calaos_server/HttpClient.cpp)).
Avant d'arriver au JsonApi, `HttpClient::handleJsonRequest()` propose l'URL au
handler RemoteUI puis au handler OTA ; le JsonApi n'est que le fallback
([HttpClient.cpp:611-673](../src/bin/calaos_server/HttpClient.cpp)).

Côté WebSocket, seuls `/api`, `/api/console`, `/api/v3/remote_ui/ws` et `/echo` sont
acceptés ; seul `/api` crée un `JsonApiHandlerWS`, `/api/console` crée un `ConsoleTunnel` — les
deux autres chemins sont routés
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

L'adresse indexée est celle que haproxy a vue : le dernier élément de
`X-Forwarded-For`, lu seulement quand le pair TCP est le loopback
(`HttpClient::getEffectiveClientIp()`). Un client direct du LAN est indexé sur
son pair TCP. Un dernier élément qui n'est pas une adresse IPv4 ou IPv6 est
ignoré : le pair TCP le remplace, dans le limiteur comme dans les journaux et
dans `X-Calaos-Client-IP` envoyé au daemon.

Tout processus du réseau de l'hôte atteint aussi le loopback et peut écrire
n'importe quel `X-Forwarded-For`. Changer de nom à chaque essai donnerait un
nouveau délai à chaque fois, d'où un plafond (`LoginThrottle::key()`). Il
s'applique par portée : login foyer, `admin:` et `service:` (`login_service`).
Dans chaque portée, au plus 16 noms venus de l'en-tête et jamais connectés
gardent leur propre compteur, jusqu'à 300 s sans essai. Au-delà, les nouveaux
noms partagent le compteur `loopback-overflow`, freiné normalement. Un login réussi par
ce compteur partagé ne l'efface pas.

Un nom qui s'est connecté garde son compteur hors plafond tant qu'il réussit un
login au moins tous les 7 jours (les échecs ne prolongent pas ce délai). Cette
confiance est en mémoire : un redémarrage de calaos_server l'oublie. Un client
inconnu (nouvel appareil, nouvelle adresse, ou n'importe quel client juste après
un redémarrage) est refusé, même avec le bon mot de passe, tant qu'un usurpateur
entretient le compteur partagé, et jusqu'à 300 s après.

Le plafond ne couvre que les noms venus de l'en-tête. Un pair loopback sans
`X-Forwarded-For` (ou qui y répète sa propre adresse) est indexé sur
`127.0.0.1`, comme un pair direct. Ce compteur n'est pas plafonné : un conteneur
du réseau de l'hôte peut le maintenir bloqué, et bloquer ainsi les clients
locaux qui passent par le loopback. Seul le socket Unix de la phase 3 corrige
cela.

Un foyer derrière haproxy n'a en pratique jamais 16 clients en échec en 5 min.
Une fois le délai à 60 s, un usurpateur du login foyer est limité à environ
17 essais par minute, mais la première minute en permet une centaine (chaque
compteur passe d'abord par 1, 2, 4… s). Pour `admin_unlock`, la vraie borne
reste le limiteur global du daemon. Les pairs directs ne sont jamais regroupés.

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
En session `serviceScope`, **14** messages sont refusés : `set_param`, `del_param`,
`audio_db`, `set_timerange`, `autoscenario`, `eventlog`, `register_push`,
`settings`, `config`, les trois messages admin `admin_unlock`, `admin_set`, `admin`,
`console`, le message d'ouverture de [`/api/console`](#console-web-apiconsole), et l'action
HTTP `config_backup` (`JsonApi::serviceScopeDeniedCommands()`, voir [Portée MCP](#portée-mcp)).

⭐ **La portée est une propriété de la session, pas du transport.** Les quatorze sont
refusées quel que soit le transport par lequel la session a été ouverte, et la
charge utile du refus est **la même** des deux côtés — seule l'enveloppe change :
`{"msg":"<msg>","msg_id":"<le vôtre>","data":{"error":"scope denied"}}` en
WebSocket, `{"error":"scope denied"}` en HTTP. Les trois messages admin, `console` et `config_backup` refusent
avec la forme d'erreur admin : `{"success":"false","error":"not_allowed","msg":"scope denied"}`.

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
L'action HTTP `config_backup` (phase 3) n'est pas comptée non plus : voir
[Configuration](#configuration-config_list-config_set-config_backup).

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
> implémenter (tickets CB2, CB1, CB3, CB4, puis AP1b et CO2 pour la phase 2), que la webapp
> (WA1, WA3, CO3) et l'installer (CI1) codent déjà contre, et dont le mock de la webapp et le faux
> daemon dérivent. Contrairement au reste
> du document, elle n'est pas dérivée du code. Le plan est
> `docs/admin/PLAN.md` ; le contrat du **daemon** est la section admin de
> [`docs/API.md` de calaos-container](https://github.com/calaos/calaos-container/blob/main/docs/API.md),
> dont le tableau « Relay table » et « Error translation » fixent ce que calaos_server envoie
> et traduit. En cas de divergence entre ce texte et ce fichier sur le côté daemon, `API.md`
> fait foi.
>
> **Livraison de la phase 2 (verrou).** Les approbateurs et les codes push (AP1b) vivent sur la
> branche d'intégration `admin/p7`. Elle ne rejoint `master` qu'**avec** AP1a (approbateurs et
> codes du daemon), CO1 (console du daemon) et WA3 (enrôlement et saisie du code dans la
> webapp), jamais seule : un daemon AP1a exige un code pour changer le mot de passe admin, ce
> qu'un calaos_server ou une webapp de phase 1 ne savent pas fournir (`internal`), et les
> routes d'approbateur n'existent pas sur un daemon de phase 1.
>
> **Phase 3 (contrat).** Les actions de ce document marquées « phase 3 » (clé de chiffrement du
> code push, services, logs, heure, réseau, `config_list` / `config_set` / `config_backup` /
> `config_restore`) vivent sur la branche d'intégration `admin/p8` et suivent la même règle : le contrat du **daemon** est
> `docs/API.md` de calaos-container (branche `admin/p8`), qui fait foi pour les routes, les
> limites et les codes du daemon ; ce texte fixe ce que calaos_server accepte du client, ce qu'il
> envoie au daemon et ce qu'il rend. Un `calaos_server` de phase 3 tolère un daemon plus ancien
> (voir `daemon_too_old` ci-dessous) ; une webapp de phase 3 exige un calaos_server de phase 3.

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
  `min_length` avec `password_too_short`, `api_allowed_hosts` avec `admin_not_set`,
  `attempts_left` avec `bad_code`, `max` avec `approver_limit` et `console_limit`,
  `change_id` et `expires_in` avec `network_pending`, `applied` (étapes déjà appliquées, séparées
  par `,`), `failed` (étape en échec) et `restart_required` (`"true"` ou `"false"`) avec
  l'`internal` d'un `time_set` partiellement appliqué, `outcome` (`"unknown"`) avec le
  `daemon_unreachable` ou l'`admin_unavailable` d'un `time_set`, `network_apply`,
  `network_confirm` ou `network_revert` resté sans réponse (voir ci-dessous).
- En WS la charge utile est sous `data`, avec `msg` et `msg_id` de l'enveloppe habituelle
  (attention : le `msg` de l'enveloppe est le nom du message, le `msg` de `data` est le texte
  d'erreur) :

  ```json
  {"msg": "admin_unlock", "msg_id": "4",
   "data": {"success": "false", "error": "bad_password", "msg": "Wrong admin password"}}
  ```
- En HTTP la charge utile est le corps de la réponse, avec le statut **200**. Les nombres de
  la colonne « Statut daemon » du vocabulaire sont les statuts **du daemon**, donnés à titre
  indicatif : calaos_server ne les reproduit pas. Seul l'échec d'authentification foyer
  garde son comportement actuel (HTTP 400 et fermeture).
- Un ancien installer qui reçoit un refus sur `config` affiche `error` tel quel : les codes
  sont donc choisis lisibles. Voir [`config`](#config--clé-type-http-uniquement-2).

Cas d'erreur d'un `config` / `get_mcp_info` refusé : la même charge utile
(`{"success":"false","error":"bad_session","msg":"…"}`), à la place de la réponse habituelle.

### Vocabulaire des codes

| Code | Statut daemon | Cas | Texte conseillé côté client |
|---|---|---|---|
| `admin_not_set` | 409 | l'admin n'est pas défini ; porte en plus `api_allowed_hosts` (chaîne, valeur brute de l'option, `""` si absente) | proposer le bootstrap, afficher `api_allowed_hosts` |
| `admin_already_set` | 409 | bootstrap alors qu'un admin existe, y compris une course perdue entre deux bootstraps | « Quelqu'un vient de définir le mot de passe admin », avec bannière |
| `bad_password` | 403 | mot de passe admin faux | « Mot de passe admin incorrect » |
| `rate_limited` | 429 | limiteur du daemon (environ 5 échecs par minute, global), vérification déjà en cours, ou `LoginThrottle` de calaos_server | « Trop d'essais, réessayez dans `retry_after` s » |
| `household_password_required` | n/a | bootstrap refusé : identifiants foyer vides, `user` / `pass` ou clés legacy | « Changez d'abord le mot de passe foyer » (`change_cred`) |
| `bad_session` | 401 | `admin_session` absente, vide, inconnue, expirée ou invalidée (`admin_set`, `admin_reset`, 5 échecs de code push en phase 2) | redemander le mot de passe admin, **sans message d'erreur** |
| `password_too_short` | 400 | nouveau mot de passe de moins de 10 caractères | afficher la longueur minimale (`min_length`) |
| `https_required` | n/a | transport non sûr, voir [Transport](#transport-securetransport) | « Utilisez https:// » |
| `daemon_too_old` | 404 | un ancien daemon, encore en mémoire, ne connaît pas les routes admin, ou (phase 3) la route de **cette** action : un `404 not_found` sur une route de la table de relais ne peut venir que d'un daemon qui la précède | « Activation de l'administration en cours », nouvel essai automatique ; « Redémarrez la box » seulement si l'état dure plus de 2 min. Phase 3 : le panneau de l'action concernée seulement |
| `daemon_unreachable` | n/a | daemon injoignable alors que l'admin a déjà été vu défini ; erreur de bearer persistante | réessayer |
| `admin_unavailable` | n/a | daemon injoignable et admin jamais vu défini (Docker, dev, autre distribution) | « Administration indisponible (calaos-container injoignable ; disponible seulement sur Calaos OS) », nouvel essai automatique |
| `upgrade_in_progress` | 409 | une mise à jour est déjà en cours (`update_check`, `update_upgrade*`, `system_reboot`) | « Une mise à jour est déjà en cours » |
| `not_allowed` | 403 | action hors de la table, scope MCP, action de phase 2 pas encore livrée | afficher `msg` |
| `invalid_request` | 400 | champ manquant ou invalide (`new_pass`, `action`, `package`…) | afficher `msg` |
| `package_not_found` | 404 | le paquet de `update_upgrade` n'a pas de mise à jour en attente | afficher `msg` |
| `package_held` | 409 | le paquet de `update_upgrade` est bloqué sur la box (`apt-mark hold`) : aucun job | « Ce paquet est bloqué sur la box » |
| `approver_required` | 409 | action protégée par un code push (console, mot de passe admin, approbateur) sans aucun téléphone approbateur enrôlé | proposer l'enrôlement |
| `approver_limit` | 409 | enrôlement alors que deux approbateurs existent ; porte `max` | « Deux téléphones approbateurs au plus » |
| `approver_exists` | 409 | enrôlement d'un téléphone déjà approbateur | « Ce téléphone est déjà approbateur » |
| `approver_not_found` | 404 | `approver_id` inconnu | relire `approver_list` |
| `approver_last` | 409 | retrait du seul approbateur | « Enrôlez d'abord un autre téléphone » |
| `code_required` | 403 | action protégée envoyée sans `code_id` et `code`, ou enrôlement commencé sans approbateur alors qu'un autre a été enrôlé depuis | demander un code (`code_request`) |
| `bad_code` | 403 | code push faux, ou demandé pour une autre action ou d'autres paramètres ; porte `attempts_left`. Au 5ᵉ échec la session est invalidée (`bad_session` ensuite) | « Code incorrect, `attempts_left` essais restants » |
| `code_expired` | 410 | code inconnu de la session, expiré (60 s), utilisé ou remplacé ; droit d'ouverture de la console expiré (30 s) ou déjà pris | demander un nouveau code |
| `console_limit` | 409 | deux consoles sont déjà ouvertes ; porte `max` | « Deux consoles au plus sont ouvertes » |
| `network_pending` | 409 | un changement réseau attend déjà sa confirmation (`network_apply`) ; porte `change_id` et `expires_in` | proposer de le confirmer ou de l'annuler (`network_confirm` / `network_revert`) |
| `service_unavailable` | 409 | l'unité d'une action `services_*` n'est pas installée ou est masquée | « Ce service n'est pas disponible sur cette box » |
| `login_failed` | n/a | [`/api/console`](#console-web-apiconsole) seulement : identifiants foyer du message d'ouverture faux | retour à la connexion foyer |
| `internal` | 500 | tout le reste ; le détail est dans le journal de calaos_server | message générique |

`network_pending` et `service_unavailable` sont des codes de phase 3. Les codes de `invalid_request` à `internal` complètent le vocabulaire du plan : ils viennent
de la traduction des erreurs du daemon. Ceux d'`approver_required` à `login_failed` sont ceux
du second facteur et de la console (phase 2).

### Traduction des erreurs du daemon

calaos_server traduit la réponse du daemon ainsi (reprise de la section « Error translation »
de `API.md`) :

| Réponse du daemon | Code renvoyé au client |
|---|---|
| connexion refusée, délai dépassé, réponse invalide | `daemon_unreachable` ou `admin_unavailable`, selon [`AdminState`](#état-admin-et-règle-collante) |
| `401` dont le `code` n'est pas `bad_session` (y compris un corps sans `code`), après relecture du token et **une** nouvelle tentative | `daemon_unreachable` |
| `404` sur `GET /api/v2/admin/status` (quel que soit le corps), ou tout `404` sur `/api/v2/` dont le corps n'est pas une enveloppe avec un `code` | `daemon_too_old` |
| `bad_session`, `bad_password`, `rate_limited`, `password_too_short`, `admin_not_set`, `admin_already_set`, `upgrade_in_progress`, `not_allowed`, `package_held`, `approver_required`, `approver_limit`, `approver_exists`, `approver_not_found`, `approver_last`, `code_required`, `bad_code`, `code_expired`, `console_limit`, `network_pending`, `service_unavailable` | le même code |
| `invalid_request`, `package_not_found` | le même code, avec le `msg` du daemon (affichable sans risque) |
| `not_found` sur une route de la table de relais (le chemin est fixé par la table : la route n'existe pas dans un daemon qui la précède) | `daemon_too_old`, pour cette action seulement |
| `internal`, `not_found` sur tout autre appel de calaos_server (`admin/push/{code_id}`…), `method_not_allowed`, tout code inconnu | `internal` |

`rate_limited` recopie `retry_after` ; `upgrade_in_progress` recopie `job_id` ;
`password_too_short` recopie `min_length` (la longueur est vérifiée par le daemon seul) ;
`bad_code` recopie `attempts_left` ; `approver_limit` et `console_limit` recopient `max` ;
`network_pending` recopie `change_id` et `expires_in`. L'`internal` d'un `time_set` dont une étape a
échoué recopie en plus `applied` et `failed` du `data` du daemon (voir [Heure de la box](#heure-de-la-box)).
Chaque appel relayé porte `X-Calaos-Client-IP` (voir `API.md`, « request headers »), posé
par calaos_server depuis `getEffectiveClientIp()`. Un `401` persistant du bearer sur
`admin/status` compte comme « réponse invalide » pour `AdminState` (drapeau conservé). Un `401` sans
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
  (`update_status` et, en phase 3, `config_list`, `config_set`, `config_backup` compris), l'action
  HTTP `config_backup` et, **une fois l'admin défini**, `config` et `get_mcp_info`. Aucune action
  de phase 3 n'est servie sur le port 5454 en clair : `https_required`.
- Ne le requiert pas : le reste de l'API foyer, ainsi que `config` et `get_mcp_info` tant que
  l'admin n'est pas défini.
- Refus : `https_required`. La connexion WS n'est pas fermée et le handshake sur le port 5454
  en clair reste accepté ; seuls les messages admin sont refusés.
- Exception : [`/api/console`](#console-web-apiconsole) refuse dès le handshake (`403`), avant
  toute montée WebSocket.
- Un client qui s'est souvenu d'un hôte `http://…:5454` réécrit l'URL en `https://` et
  réessaie **une fois** (CI1). La webapp affiche « Utilisez https:// ».
- Exception, dans l'autre sens : `admin_event` est diffusé à toute connexion WS authentifiée,
  port 5454 compris, car il ne porte rien de sensible (voir [Événements](#événements-admin_event)).

Le prédicat n'est pas infalsifiable : un conteneur du réseau hôte peut forger l'en-tête.

### Messages

En HTTP, `admin_unlock` et `admin_set` ne lisent que le corps JSON. Un corps qui n'est pas un
objet JSON donne `invalid_request` sans appel au daemon : `admin_pass`, `new_pass` et
`admin_session` ne sont jamais lus dans les paramètres GET. WA1 n'envoie un message admin
qu'après un `login` réussi (avant, le serveur l'ignore en silence).

#### `admin_unlock`

Vérifie le mot de passe admin et rend une session.

Les échecs admin sont comptés par `LoginThrottle` sous une clef distincte (`admin:<ip>`), que
seul `admin_unlock` consulte et que seul un `admin_unlock` réussi efface. Si cette clef est
bloquée, la réponse est `rate_limited` avec `retry_after` (secondes restantes, arrondies au
dessus), sans appel au daemon. Un échec admin ne ferme pas la connexion et ne bloque pas le
login foyer. En HTTP, le blocage du login foyer garde son comportement actuel (HTTP 400 et
fermeture).

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

- `admin_session` : 64 caractères hexadécimaux, opaque. calaos_server refuse en `bad_session`,
  sans appeler le daemon, toute `admin_session` qui ne vérifie pas `^[0-9a-f]{64}$`. `expires_in` : secondes restantes
  (`"900"` à l'émission). La durée est **absolue** : l'utiliser ne la prolonge pas.
- Erreurs : `https_required`, `rate_limited`, `invalid_request` (`admin_pass` absent),
  `not_allowed` (portée MCP), `admin_not_set`, `bad_password`, `daemon_too_old`, `daemon_unreachable`,
  `admin_unavailable`, `internal`.
- Un nouveau déverrouillage émet une nouvelle session sans invalider les précédentes (le
  daemon en garde au plus 32 ; au-delà, la plus ancienne est supprimée et répond alors
  `bad_session`).

#### `admin_set`

Définit le mot de passe admin (bootstrap) ou le change. Le mode se déduit de la requête :

| `admin_session` | État | Mode | Résultat |
|---|---|---|---|
| absente | admin non défini | **bootstrap** | défini |
| absente | admin défini | | `admin_already_set` |
| présente | admin défini | **changement** | remplacé si la session est valide et le code push bon, sinon `bad_session`, `approver_required`, `code_required`, `bad_code`, `code_expired` |
| présente (vide comprise) | admin non défini | | `admin_not_set` (répondu par calaos_server selon `AdminState`) |

Si `AdminState` est en retard et que le daemon répond `bad_session`, ce code est renvoyé.
Une clef `admin_session` présente mais vide compte comme **présente** (donc `bad_session` si l'admin est défini, `admin_not_set` sinon),
et `recovery` n'existe pas ici : le mode récupération n'est envoyé que par
`calaos-os admin set`, jamais relayé.

```json
// WS — bootstrap
{"msg": "admin_set", "msg_id": "3", "data": {"new_pass": "a long admin password"}}
// WS — changement, avec le code push de l'action admin_password
{"msg": "admin_set", "msg_id": "3",
 "data": {"new_pass": "another long password", "admin_session": "9b1f…e4",
          "code_id": "c3d4e5f6a7b8c9d0", "code": "482913"}}
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
- **Bootstrap refusé** (`household_password_required`) si les identifiants foyer effectifs,
  après le repli de `checkCredentials` sur les clés legacy, sont vides, valent `user` / `pass`,
  ou viennent des clés `calaos_user` / `calaos_password`. Le client invite alors à changer le
  mot de passe foyer d'abord (`change_cred`).
- `new_pass` : au moins 10 caractères (points de code Unicode) et au plus 1024 octets UTF-8.
  Plus court : `password_too_short`.
- **Changement : code push.** `code_id` et `code` (6 chiffres) viennent d'un `code_request`
  d'action `admin_password` dont `code_params.password` est **le même** `new_pass` : le daemon
  lie le code à ce mot de passe. calaos_server les recopie dans le corps de `admin/set` en mode
  changement seulement ; un bootstrap les ignore. Les deux champs vont ensemble : l'un sans
  l'autre, un `code_id` qui n'est pas 16 hexadécimaux minuscules ou un `code` qui n'est pas
  6 chiffres donne `invalid_request` sans appel au daemon. Sans approbateur enrôlé, le
  changement répond `approver_required` : il reste `calaos-os admin set` en SSH.
- Erreurs : `https_required`, `invalid_request` (`new_pass` absent, code mal formé),
  `not_allowed` (portée MCP), `household_password_required`,
  `admin_already_set`, `admin_not_set`, `bad_session`, `password_too_short`, `rate_limited`,
  `approver_required`, `code_required`, `bad_code`, `code_expired`,
  `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`, `internal`.
- Chaque réussite publie l'alerte `admin_set` (push à tous les jetons et événement persistant
  affiché au login suivant) et l'événement WS [`admin_event`](#événements-admin_event).

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
  Aucun chemin ni en-tête ne vient du client ; le corps envoyé au daemon est reconstruit
  champ par champ à partir des `params` validés de la table (le reste est ignoré).
- `result` est le champ `data` de la réponse du daemon, **recopié tel quel, avec ses types
  JSON** (booléens, nombres, objets imbriqués), sauf `update_status` sans session (vue foyer,
  ci-dessous). C'est la troisième exception à la règle
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
| `approver_list` | | `GET admin/approvers`, puis `push_tokens` de calaos_server | oui | `[<Approver>]` : `{id, device_name, token_id, created_at}` plus `platform`, `registered_at`, `last_seen`, `secure` (phase 3), jamais de jeton |
| `approver_candidates` | | `GET admin/approvers`, puis `push_tokens` de calaos_server | oui | `[{token_id, device_name, platform, registered_at, last_seen, approver, secure}]`, jamais de jeton |
| `approver_enroll` | `token_id`, `code_id`?, `code`? | `POST admin/approvers/enroll {token, device_name, code_id, code}` | oui | `{approver_id, code_id, expires_in}` |
| `approver_confirm` | `code_id`, `code` | `POST admin/approvers/confirm` | oui | `<Approver>` |
| `approver_remove` | `approver_id`, `code_id`, `code` | `POST admin/approvers/remove` | oui | `{approver_id}` |
| `code_request` | `code_action`, `code_params`?, `approver_id`? | `POST admin/codes {action, params, approver_id}` | oui | `{code_id, approver_id, expires_in}` |
| `code_verify` | `code_id`, `code` | `POST admin/codes/verify` | oui | `{action, expires_in}` |
| `console_open` | `code_id`, `code` | `POST admin/codes/verify` ; le droit est pris par [`/api/console`](#console-web-apiconsole) | oui | `{action: "console_open", expires_in: 30}` |
| `services_list` | | `GET system/services` | oui | `{services: [...]}`, voir [Services](#services) |
| `services_restart` | `unit` | `POST system/services/restart {unit}` | oui | `{id, state: "restarting"}` |
| `services_start` | `unit` | `POST system/services/start {unit}` | oui | `{id, state: "starting"}` |
| `services_stop` | `unit` | `POST system/services/stop {unit}` | oui | `{id, state: "stopping"}` |
| `services_enable` | `unit` | `POST system/services/enable {unit}` | oui | `{id, state: "enabling"}` |
| `services_disable` | `unit` | `POST system/services/disable {unit}` | oui | `{id, state: "disabling"}` |
| `logs_sources` | | `GET logs/sources` | oui | `{sources: [{id, label}]}`, voir [Logs](#logs) |
| `logs_query` | `sources`?, `priority`?, `since`?, `until`?, `text`?, `lines`?, `cursor`?, `direction`? | `POST logs/query`, corps reconstruit champ par champ | oui | `{entries, truncated, complete, next_cursor}` |
| `time_get` | | `GET system/time` | oui | `<BoxTime>`, voir [Heure de la box](#heure-de-la-box) |
| `time_zones` | | `GET system/timezones` | oui | `{timezones: [...]}` |
| `time_set` | `timezone`?, `ntp`?, `time`? (un au moins) | `POST system/time` | oui | `<BoxTime>` plus `restart_required` |
| `network_state` | | `GET network/state` | oui | `{interfaces, pending}`, voir [Réseau](#réseau) |
| `network_apply` | `intf`, `dhcp`, `ipv4`?, `gateway`?, `dns_servers`?, `search_domains`?, `code_id`, `code` | `POST network/apply` | oui | `{change_id, intf, applied_at, confirm_within}` |
| `network_confirm` | `change_id` | `POST network/confirm` | oui | `{change_id, intf, state: "confirmed"}` |
| `network_revert` | `change_id` | `POST network/revert` | oui | `{change_id, intf, state: "reverted"}` |
| `config_list` | | aucun : traité dans calaos_server | oui | `{options: [...]}`, voir [Configuration](#configuration-config_list-config_set-config_backup) |
| `config_set` | `key`, `value` | aucun : traité dans calaos_server | oui | `{key, value, set, restart_required}` (un secret : sans `value`) |
| `config_backup` | | aucun : traité dans calaos_server | oui | `{format, created_at, server_version, hostname, io_format, files}` |
| `config_restore` | `files`, `dry_run` | aucun : traité dans calaos_server | oui | `{dry_run, files, backup, box, io_format, kept, warnings, previous?, restarting}`, voir [Restauration](#restauration-config_restore) |

Chemins relatifs à `/api/v2/`. Jamais relayés, quoi que le client envoie : `system/install/*`,
`system/rollback_snapshot`, `system/fs_status`, `network/list`, `network/trial`, `network/{intf}`
(l'ancienne route POST), `update/installed`, `admin/reset`, `admin/alerts/ack`, `admin/push/*`,
`admin/set` avec `recovery`. Les routes `network/state`, `apply`, `confirm` et `revert` sont
relayées (phase 3, voir [Réseau](#réseau)).

**Types des `params` de phase 3.** Les `params` des actions de phase 3 gardent leurs types JSON
(`dhcp`, `ntp` : booléens ; `lines` : entier ; `dns_servers`, `search_domains`, `sources` :
tableaux de chaînes) ; une valeur d'un autre type (`"true"` pour un booléen, `"3"` pour une
priorité) est `invalid_request`. Le corps envoyé au daemon est reconstruit champ par champ à
partir des `params` validés ; un malformé n'atteint jamais le daemon (deux barrières : le daemon
applique les mêmes limites). `result` est le `data` du daemon, types compris, comme pour les
actions de phase 1. Délais de calaos_server : 4 s, sauf `logs_query` (10 s), 15 s pour
`services_start` et `services_stop` (plus que les 10 s du daemon), et 35 s pour `services_enable`,
`services_disable`, `time_set`, `network_apply`, `network_confirm` et `network_revert` ; le
client attend 40 s pour `services_enable`, `services_disable`, `time_set` et les trois `network_*`.
Le daemon peut y dépasser 35 s : chaque commande y a son propre délai, `timedatectl` 10 s
(`time_set` en enchaîne jusqu'à six : liste des fuseaux, lecture de l'état, trois étapes, relecture),
`networkctl` 30 s (`network_apply` en enchaîne trois, plus deux pour revenir en arrière si
l'application échoue ; `network_revert` deux ; `network_confirm` un, après l'attente d'un
changement concurrent). Sur ces quatre écritures, un délai dépassé ou une réponse illisible
donne donc `daemon_unreachable` (ou `admin_unavailable`) avec **`"outcome": "unknown"`** :
l'écriture a pu s'appliquer. Le client **ne la renvoie pas** ; il relit `network_state` ou
`time_get` (voir [Heure de la box](#heure-de-la-box) et [Réseau](#réseau)).

- **`update_status` sans session** : accepté une fois l'admin défini, sous
  `secureTransport()`. C'est ce qui permet à n'importe quel client foyer de reprendre l'affichage
  d'une mise à jour en cours après une reconnexion, sans redemander le mot de passe admin.
  Admin non défini : `admin_not_set`. Sans `admin_session` valide (absente, vide ou mal formée),
  la réponse est la **vue foyer**, sans l'inventaire des paquets : `progress` est le `<Status>`
  filtré comme [`admin_event`](#événements-admin_event), `job` perd `package` et son `error`
  devient `upgrade failed` ou `interrupted`. Avec une `admin_session` bien formée, elle est
  envoyée au daemon et la réponse est complète ; si le daemon la refuse (expirée, invalidée),
  calaos_server refait l'appel sans session et rend la vue foyer, jamais `bad_session`.
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
  `admin_not_set`, `upgrade_in_progress`, `package_not_found`, `package_held`, `rate_limited`,
  `approver_required`, `approver_limit`, `approver_exists`, `approver_not_found`,
  `approver_last`, `code_required`, `bad_code`, `code_expired`, `network_pending`,
  `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`, `internal`.

#### Approbateurs et codes push

Le second facteur de la phase 2 (contrat du daemon : `API.md`, « Approvers » et « Push
codes »). Un **approbateur** est un téléphone qui a fait `register_push` et que l'admin a
enrôlé ; le daemon lui envoie un **code push** de 6 chiffres, valable 60 s, que l'utilisateur
tape dans la webapp. Protégés par un code : l'ouverture de la console (`console_open`), le
changement du mot de passe admin (`admin_set`), l'enrôlement d'un second approbateur et le
retrait d'un approbateur. Les mises à jour, le reboot et le reste ne demandent que la session.

- **Aucun client n'envoie ni ne reçoit de jeton push.** `approver_candidates` liste les
  entrées de `push_tokens` sous la forme `token_id` (16 premiers hexadécimaux du SHA-256 du
  jeton, comme le `token_id` d'`<Approver>`), `device_name` (le nom de `register_push`, ou
  `Android device` / `iOS device` si l'app n'en a pas envoyé), `platform` (`ios` ou
  `android`, le `hardware` de `register_push` ; `desktop` est réservé, aucune app de bureau
  ne s'enregistre aujourd'hui), `registered_at` (premier `register_push` ; pour un jeton plus
  ancien que la colonne, ou enregistré par un serveur plus ancien après un retour en arrière,
  son dernier connu à l'ouverture suivante de la base), `last_seen` (dernier
  `register_push`), dates RFC 3339 UTC, et `approver` (booléen JSON : ce `token_id` figure
  dans `approver_list`) et, en phase 3, `secure` (booléen JSON : une clé push est stockée pour
  ce jeton, voir [Code push chiffré](#code-push-chiffré)), le plus récemment vu d'abord. L'appel
  au daemon valide la session.
  Depuis iOS 16 tout iPhone se nomme `iPhone` : ces trois champs, avec la fin du `token_id`,
  permettent de distinguer deux téléphones de même nom.
- **`approver_list`** rend la liste du daemon, dans son ordre, réduite à `id`,
  `device_name` (nettoyé comme celui de `register_push`), `token_id` et `created_at` : un
  champ ajouté plus tard par le daemon n'atteint pas les clients. Il ajoute à chaque
  approbateur `platform`, `registered_at` et `last_seen` lus dans `push_tokens` par `token_id`,
  et, en phase 3, `secure` (booléen JSON, comme pour les candidats).
  Un approbateur dont le jeton manque à `push_tokens` (jamais enregistré auprès de ce serveur,
  ou purgé avant que ce serveur le sache approbateur) les a vides et `secure` vaut `false` : il ne
  reçoit plus de codes tant que l'app ne s'est pas reconnectée.
- **`approver_enroll`** : le client nomme le téléphone par `params.token_id`. calaos_server le
  retrouve dans `push_tokens` et envoie lui-même au daemon **ce** jeton et **son**
  `device_name` ; tout `token`, `device_name` ou autre champ envoyé par le client est ignoré.
  Un `token_id` inconnu donne `invalid_request` (`"Unknown token_id"`) sans appel au daemon.
  `code_id` et `code` (ensemble ou pas du tout) portent le code `approver_enroll` d'un
  approbateur existant ; le premier enrôlement n'en demande pas. Le daemon envoie alors un code
  d'enrôlement au nouveau téléphone, à confirmer par `approver_confirm`.
- **`code_request`** : `code_action` vaut `console_open`, `admin_password`, `approver_enroll`,
  `approver_remove` ou, en phase 3, `network_config` (toute autre valeur, `approver_confirm`
  compris : `invalid_request`).
  `code_params` est reconstruit avec les seules clés liées par le daemon : `{password}` pour
  `admin_password` (le futur `new_pass`, 1 à 1024 octets), `{approver_id}` pour
  `approver_remove`, `{}` pour les deux autres, et pour `network_config` les six chaînes
  décrites dans [Réseau](#réseau). `approver_id` choisit le téléphone (par défaut le plus ancien).
- **`code_verify` / `console_open`** (`params {code_id, code}`) : vérifient un code
  `console_open` ; la session reçoit un droit d'ouverture de la console à usage unique,
  valable 30 s, que le message d'ouverture de [`/api/console`](#console-web-apiconsole)
  consomme avec la **même** `admin_session` et le même `code_id`. Ne pas redemander de code
  entre les deux. WA3 utilise `console_open` ; `code_verify` en est la forme générique, gardée
  pour le contrat.
- Formats vérifiés par calaos_server avant tout appel (`invalid_request` sinon) : `code_id`,
  `approver_id` et `token_id` sont 16 hexadécimaux minuscules, `code` 6 chiffres ASCII.
- Limites du daemon : un code en attente par session, 10 s entre deux codes d'une session,
  10 codes par heure pour la box ; 5 codes faux invalident la session.
- **Remise d'un code** : le daemon publie `push_code {approver_id, code_id}` sur `/events`,
  sans jeton ni code. calaos_server lit `GET admin/push/{code_id}` (bearer seul, une fois),
  qui rend `{token, message, action, code, expires_in}`, et pousse à **ce seul jeton** s'il
  figure dans `push_tokens` : ni les autres téléphones, ni un client, ni le journal ne voient
  le code ou le jeton. `push_code` n'est jamais retransmis. Deux formes, selon que le jeton a
  une clé push (voir [Code push chiffré](#code-push-chiffré)) ou non : le **texte hérité**
  (`message` nettoyé, au plus 256 points de code) pour un jeton sans clé, l'**avis** plus la
  charge chiffrée pour un jeton avec clé. Dans les deux cas, `message` doit commencer par le
  préfixe fixe du daemon, `Calaos admin code NNNNNN: ` (6 chiffres) ; tout autre texte est
  ignoré, un faux daemon ne peut donc pas rédiger ses propres pushes. Au
  plus 20 codes sont poussés par heure glissante (le double du budget du daemon) ; seul un
  push parti compte, pas une récupération qui n'a rien servi ni un jeton inconnu. Au-delà, le
  `push_code` est ignoré. Un code perdu se redemande au bout de 10 s.
- **Jeton purgé à 30 jours.** La purge de `register_push` ne touche jamais le jeton d'un
  approbateur (marqué par `approver_set` ou d'après `approver_list`) : il garde son nom, son
  format et sa clé gelés, et reçoit ses codes même si l'app ne s'est pas connectée depuis
  30 jours. Libéré par `approver_removed` (ou absent de la liste du daemon), il vieillit comme
  les autres. `approver_candidates.last_seen` permet à la webapp d'avertir d'un téléphone
  approbateur qui ne se connecte plus.

#### Code push chiffré

Phase 3. Le code de 6 chiffres ne figure plus dans le texte de la notification (écran verrouillé,
relais push, FCM, APNs) : un téléphone qui a enregistré une **clé push** reçoit un avis générique
et le code **chiffré avec cette clé**, que seuls le téléphone et calaos_server connaissent.
L'application déchiffre et affiche le code ; le téléphone n'a pas à joindre la box. La clé
n'authentifie rien : le second facteur reste le code tapé dans la webapp (voir
`docs/admin/PLAN.md`, amendement (a) du 2026-10-25).

**Clé.** 32 octets du CSPRNG de l'application, générés une fois, envoyés à chaque
[`register_push`](#register_push) dans `push_key` (64 hexadécimaux minuscules). Elle est stockée
dans `push_tokens.push_key` et ne quitte jamais calaos_server ni le téléphone.

**Avis.** Texte composé par calaos_server à partir du seul champ `action` de la réponse du
daemon (table fermée, jamais le `message` du daemon) :
`Calaos admin code to <purpose>. Open the Calaos app to read it.`

| `action` | `<purpose>` |
|---|---|
| `console_open` | `open the web console` |
| `admin_password` | `change the admin password` |
| `approver_enroll` | `add an approver phone` |
| `approver_remove` | `remove an approver phone` |
| `approver_confirm` | `enroll this phone as the admin approver` |
| `network_config` | `change the network configuration` |

Un `action` hors de cette table, ou un `code` qui n'est pas 6 chiffres ASCII : rien n'est poussé
(journalisé sans valeur). **Daemon plus ancien** : sa réponse `{token, message}` n'a ni `action` ni
`code` ; calaos_server pousse alors le texte hérité (`message`, sous le garde `isCodeMessage`)
même à un jeton avec clé, ce que ce téléphone recevait déjà avant la phase 3. `approver_confirm` est le code d'enrôlement envoyé au **nouveau**
téléphone : il est chiffré avec la clé de ce téléphone, ce qui prouve qu'il la détient.

**Construction** (exacte ; `||` est la concaténation d'octets, dans cet ordre).

```
nonce = 16 octets du CSPRNG de calaos_server, un par code
ks    = HMAC-SHA256(key, "calaos-admin-code/enc" || code_id || nonce)
ct    = code_ascii[6] XOR ks[0..6)              les 6 premiers octets de ks
tag   = HMAC-SHA256(key, "calaos-admin-code/mac" || code_id || nonce || action || expires || ct)[0..16)
```

- `key` : les 32 octets décodés de `push_key`. `code_ascii` : les 6 chiffres en ASCII.
- `code_id` : ses 16 caractères hexadécimaux, en ASCII. `action` : son texte ASCII.
- `expires` : le texte ASCII exact de `admin_code_expires`. `nonce` et `ct` : octets bruts.
- `expires` = heure de la réponse du daemon plus `expires_in`, en UTC, à la seconde, au format
  RFC 3339 avec `Z` (`2026-10-25T09:13:03Z`).
- HMAC-SHA256 existe tel quel côté OpenSSL, `javax.crypto.Mac`, `CCHmac` et
  `QMessageAuthenticationCode` : aucune autre primitive.

**Charge utile chiffrée.** Six chaînes ; le bloc chiffré n'a aucune autre clé.

| Clé | Contenu |
|---|---|
| `admin_code_id` | `code_id`, 16 hexadécimaux minuscules |
| `admin_code_action` | `action` |
| `admin_code_expires` | `expires`, RFC 3339 UTC à la seconde, suffixe `Z` |
| `admin_code_nonce` | `nonce`, 32 hexadécimaux minuscules |
| `admin_code_ct` | `ct`, 12 hexadécimaux minuscules |
| `admin_code_tag` | `tag`, 32 hexadécimaux minuscules |

**Forme poussée, par plateforme** (jamais `event_uuid`, jamais le code) :
- iOS : l'avis en `message` (l'alerte), les six clés dans `data` (`mutable_content` est toujours
  posé).
- Android : un jeton avec clé reçoit **toujours** le message FCM data seul, quel que soit son
  `push_format` (un message `notification` serait affiché par le système, application en arrière-plan,
  sans appeler `onMessageReceived` : rien ne serait stocké). `data` porte `title` (`Calaos`),
  `body` et `message` (l'avis, jamais le code) en plus des six clés.

**Réception.** L'application stocke la charge telle que reçue avec l'heure de réception, sans
cryptographie (service Android, extension iOS). À l'ouverture, au premier plan ou au tap, elle :

1. refuse, **avant** le tag, une charge dont `admin_code_action` est hors de la table, dont
   `admin_code_expires` ne vérifie pas `^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$` (la concaténation
   `action || expires` n'a pas de préfixe de longueur : elle n'est sans ambiguïté que sous cette
   forme) ou dont un champ hexadécimal n'a pas la bonne longueur ou n'est pas en minuscules ;
2. recalcule `tag` à partir des **chaînes reçues** (sans reformater `expires`) et le compare **en
   temps constant avant** de déchiffrer (`CRYPTO_memcmp` pour OpenSSL, `MessageDigest.isEqual`
   en Java, `timingsafe_bcmp` sur iOS, une boucle de cumul par OU exclusif en C++ à côté de
   `QMessageAuthenticationCode`) ;
3. calcule `code = ct XOR ks[0..6)`.

Un `tag` faux est abandonné sans bruit, journalisé sans valeur. **Échéance.** L'application ne
compare **pas** `expires` à l'horloge du téléphone : une horloge de box en retard ou en avance ferait
sinon abandonner tous les codes. Elle affiche le code pendant 60 s (durée de vie d'un code) à
compter de l'heure de **réception**, puis l'abandonne ; le daemon applique l'échéance réelle
(`code_expired`). `expires` reste authentifié par le tag, c'est une indication, pas un test. Ne
jamais journaliser clé, nonce, `ct`, `tag`, code ni jeton. Les étiquettes `calaos-admin-code/enc`
et `/mac` valent version 1 : un format futur change les étiquettes et les clés de la charge.

**Vecteurs de test.** Calculés avec Python (`hmac`, `hashlib`) et recalculés avec la CLI
`openssl dgst -sha256 -mac HMAC` (OpenSSL 3.5) dans un conteneur : les deux donnent les mêmes
octets. Les implémentations de calaos_server et de calaos_mobile les rejouent.

| | Vecteur 1 | Vecteur 2 | Vecteur 3 |
|---|---|---|---|
| `key` | `000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f` | idem | `a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5` |
| `code_id` | `0123456789abcdef` | `fedcba9876543210` | `5d0c8e1f2a3b4c6d` |
| `nonce` | `f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff` | `00112233445566778899aabbccddeeff` | `ffeeddccbbaa99887766554433221100` |
| code | `482913` | `007351` | `999999` |
| `action` | `console_open` | `network_config` | `approver_confirm` |
| `expires` | `2026-10-25T09:13:03Z` | `2026-10-25T14:00:59Z` | `2026-12-31T23:59:59Z` |
| `ks[0..6)` | `b10aebd59d39` | `87e90db85a5c` | `f5e5fbf8e244` |
| `ct` | `8532d9ecac0a` | `b7d93a8b6f6d` | `ccdcc2c1db7d` |
| `tag` | `9e72bd26d3926652bc157819dd26d995` | `f1bc83f4bdfbe0e9b6a47a65cb29248d` | `0a8b6d66ac8eb1bac575ad2505da6083` |

Le vecteur 1 en `data`, tel que poussé :

```json
{"admin_code_id": "0123456789abcdef", "admin_code_action": "console_open",
 "admin_code_expires": "2026-10-25T09:13:03Z", "admin_code_nonce": "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
 "admin_code_ct": "8532d9ecac0a", "admin_code_tag": "9e72bd26d3926652bc157819dd26d995"}
```

Cas à refuser : le vecteur 1 avec un seul caractère du `tag` changé, avec un bit de `ct` changé
(malléabilité d'un chiffrement par flot), avec `nonce` ou `code_id` changé, avec
`admin_code_action` remplacé par `network_config`, avec `admin_code_expires` remplacé par
`2026-10-25T09:13:04Z`, ou vérifié avec la clé du vecteur 3 ; dans tous ces cas le `tag` ne
correspond pas. Le vecteur 1 tel quel est accepté tant que 60 s ne se sont pas écoulées depuis sa
réception, quelle que soit l'horloge du téléphone (même en 2030), et abandonné ensuite.

Recalcul (Python 3, `ks6` et `tag` en hexadécimal) :

```python
import hmac, hashlib
def seal(key_hex, code_id, nonce_hex, code, action, expires):
    key, nonce = bytes.fromhex(key_hex), bytes.fromhex(nonce_hex)
    ks = hmac.new(key, b"calaos-admin-code/enc" + code_id.encode() + nonce, hashlib.sha256).digest()
    ct = bytes(a ^ b for a, b in zip(code.encode(), ks[:6]))
    tag = hmac.new(key, b"calaos-admin-code/mac" + code_id.encode() + nonce
                   + action.encode() + expires.encode() + ct, hashlib.sha256).digest()[:16]
    return ks[:6].hex(), ct.hex(), tag.hex()
```

**Texte hérité.** Un jeton **sans** clé (apps des stores) reçoit le `message` du daemon tel
quel, code en clair : `Calaos admin code NNNNNN: <purpose>. Valid 60 s, never share it.`
Cette forme est retirée une release après la sortie de l'app mobile qui envoie `push_key`.

**Journal.** Ni la clé, ni le nonce, ni `ct`, ni `tag`, ni le code, ni le jeton ne sont écrits,
même à `debug_level 5` (un test de `PushTokenStore_test` le fixe).

#### Console web (`/api/console`)

Un shell root de la box dans le navigateur. Le daemon ouvre la console, possède le pty et en fixe
les limites (contrat : `API.md`, « Console ») ; calaos_server relaie, message par message, le
WebSocket du navigateur vers la socket privée de cette console (`ConsoleTunnel`).

**Parcours de la webapp**

1. `admin {action: "code_request", params: {code_action: "console_open"}}` : un code part vers
   le téléphone approbateur.
2. `admin {action: "console_open", params: {code_id, code}}` sur `/api` : le code devient un
   droit d'ouverture à usage unique, valable 30 s, lié à cette `admin_session`.
3. Dans ces 30 s : `new WebSocket("wss://<hôte>/api/console", "calaos-console.v1")`,
   `binaryType = "arraybuffer"`, puis, à l'événement `open`, le message d'ouverture. Rien d'autre
   avant `ready` : ce qui arrive entre les deux est ignoré.

**Montée WebSocket**

- Chemin `/api/console`, **sans paramètre** : aucun secret dans l'URL (haproxy journalise les
  URL). Tout ce qui authentifie est dans le premier message.
- Sous-protocole `calaos-console.v1` obligatoire (second argument de `new WebSocket`) ; la
  réponse `101` le reprend. Absent : `400`.
- `secureTransport()` obligatoire (pair loopback, donc haproxy, et `X-Forwarded-Proto: https`) :
  sinon `403` avant la montée. Le port 5454 en clair ne sert jamais de console.
- Contrôle d'[Origin](#origin) **toujours** appliqué sur ce chemin, admin défini ou non : `403`.
  La page servie par la box elle-même passe. Un navigateur ne lit pas le statut d'un refus de
  montée : il voit un `close` en `1006`.
- calaos_server pingue toutes les 15 s (pong automatique du navigateur) et ferme après trois
  pongs manqués. Il répond lui-même aux pings du daemon sur la socket.

**Message d'ouverture** (premier message, texte JSON, au plus 64 Kio, dans les 30 s)

```json
{"type": "open", "cn_user": "user", "cn_pass": "pass",
 "admin_session": "9b1f…e4", "code_id": "c3d4e5f60718293a",
 "columns": 120, "rows": 40, "term": "xterm-256color"}
```

| Champ | Règle |
|---|---|
| `type` | `"open"` |
| `cn_user`, `cn_pass` | identifiants foyer, comme `login` (même `LoginThrottle`) ; vides, ils sont refusés (`login_failed`), même sur une box sans mot de passe foyer : la console est un shell root |
| `admin_session` | la session qui a vérifié le code (64 hexadécimaux minuscules) |
| `code_id` | le `code_id` vérifié par `console_open` (16 hexadécimaux minuscules) |
| `columns`, `rows` | entiers JSON de 1 à 1000 : la taille de départ du terminal |
| `term` | facultatif : `xterm-256color` (défaut du daemon), `xterm` ou `vt100` |

Contrôles, dans cet ordre, sans appel au daemon tant qu'un refus tombe : forme du message
(`invalid_request`), identifiants foyer (`rate_limited` avec `retry_after` dans la fenêtre du
limiteur, `login_failed`), portée MCP (`not_allowed`, voir [Portée MCP](#portée-mcp)), champs
(`invalid_request`), état admin (`admin_not_set`, `daemon_too_old`, `daemon_unreachable`,
`admin_unavailable`), session absente ou mal formée (`bad_session`). Puis calaos_server appelle
`POST admin/console {code_id, term}` avec `X-Calaos-Admin-Session`, `X-Calaos-Client-IP` et son
bearer, et traduit : `bad_session` (session expirée ou invalidée), `code_expired` (droit
inconnu, expiré, demandé par une autre session que celle qui a vérifié le code, ou **déjà
pris : un droit ne sert qu'une fois** ; le droit d'une autre session lui reste),
`console_limit` (avec `max`), `rate_limited`, `internal`.

calaos_server ne prend d'un client ni `console_id` ni chemin de socket. Il se connecte à
`<répertoire>/console-<console_id>.sock`, reconstruit depuis un `console_id` de 16
hexadécimaux sous le répertoire monté (`/run/calaos-container/console` ;
`CALAOS_CONTAINER_CONSOLE_DIR` pour les tests, lue au démarrage, chemin absolu sans `.` ni
`..`), et seulement si la réponse du daemon nomme exactement cette socket, `ws_path` `/ws` et
`ws_protocol` `calaos-console.v1` (sinon `internal`). Une console n'est donc attachée que par la
connexion qui l'a ouverte. La montée sur la socket ne porte ni bearer, ni session, ni
identifiant (la socket `0600` est la barrière du daemon) ; son premier message est
`{"columns", "rows"}`, reconstruit à partir des champs validés. Socket absente, fermée ou
muette pendant 10 s : `daemon_unreachable` ; montée refusée par le daemon : `internal`.

**Refus**

Tout refus avant `ready` est un message texte, puis une fermeture `1008` (`console refused`) :

```json
{"type": "error", "success": "false", "error": "code_expired", "msg": "The push code expired, request a new one"}
```

Mêmes codes et champs que la [forme d'erreur admin](#format-derreur-ws-et-http) (`retry_after`
avec `rate_limited`, `max` avec `console_limit`). Sur `bad_session`, redemander le mot de passe
admin ; sur `code_expired`, redemander un code ; sur `login_failed`, revenir à la connexion
foyer.

**Console ouverte**

Le serveur l'annonce une fois :

```json
{"type": "ready", "console_id": "4fbd81b12ca13a2b", "expires_in": 3600, "idle_timeout": 900}
```

| Sens | Message | Effet |
|---|---|---|
| navigateur → serveur | texte `{"type": "input", "data": "<texte>"}` | saisie (`onData` de xterm.js), écrite telle quelle dans le pty |
| navigateur → serveur | binaire | saisie brute (`onBinary` de xterm.js) |
| navigateur → serveur | texte `{"type": "resize", "columns": <int>, "rows": <int>}` | redimensionnement ; ignoré si une valeur n'est pas un entier de 1 à 1000 |
| serveur → navigateur | binaire | sortie brute du pty : `term.write(new Uint8Array(event.data))` |

- Tout autre message texte (type inconnu, JSON invalide) est ignoré ; le serveur n'envoie
  aucun autre texte après `ready`. Un message vide ferme la connexion (règle WebSocket du
  serveur) : ne pas envoyer d'`input` vide.
- Un message de plus de 64 Kio ferme la console (`1009`, `message too big`) : découper un grand
  collage en morceaux de 64 Kio au plus.
- Seule la saisie réarme le délai d'inactivité du daemon (15 min), pas les pings ni les
  redimensionnements. La console dure au plus 1 h (`expires_in`).
- Contre-pression : quand un côté ne lit plus, calaos_server cesse de lire l'autre au-delà de
  256 Kio en attente et reprend sous 64 Kio. Rien n'est mis en mémoire sans borne.

**Fermeture**

| Cause | Le navigateur reçoit | La socket reçoit |
|---|---|---|
| le daemon ferme en `1000` (shell sorti) ou `1001` (1 h, inactivité, `admin_set`, `admin_reset`, 5 codes faux, arrêt) | le même code, sa raison si c'est un des jetons ci-dessous, `closed` sinon | l'écho de son code |
| le daemon ferme avec un autre code ou sans code | `1001`, même règle pour la raison | `1001` |
| la socket tombe sans trame de fermeture | `1001`, `closed` | rien |
| le daemon viole le protocole (trame masquée, fragment orphelin, message de plus de 1 Mio) | `1001`, `closed` | `1002` ou `1009` |
| le navigateur ferme en `1000` ou `1001` | (réponse à sa fermeture) | le même code |
| le navigateur ferme avec un autre code, ou sa connexion tombe | | `1001` |
| calaos_server ferme le WebSocket lui-même (trois pongs manqués, message de plus de 4 Mio, trame invalide, message vide) | son code (`1000`, `1009`, `1002`…) | `1000` pour `1000`, `1001` sinon, aussitôt |
| un message du navigateur dépasse 64 Kio | `1009`, `message too big` | `1000` |

Raison de fermeture relayée depuis le daemon : un jeton exact de cette liste, jamais son
texte libre. Une raison absente, inconnue ou plus longue devient `closed`.

| Raison | Sens |
|---|---|
| `closed` | fermeture sans cause précise, ou raison du daemon absente ou inconnue |
| `exited` | le shell est sorti |
| `expired` | la durée maximale d'une console (1 h) est atteinte |
| `idle` | aucune saisie pendant le délai d'inactivité (15 min) |
| `not_connected` | personne ne s'est attaché à la console à temps |
| `shutdown` | le daemon s'arrête |
| `admin_set` | le mot de passe admin vient d'être défini ou changé |
| `admin_reset` | l'admin vient d'être réinitialisé |
| `bad_code` | trop de codes push faux (5) |

Une console ne se rattache pas : après une fermeture, en ouvrir une autre demande un nouveau
code. Chaque ouverture lève l'alerte `console_opened` (push « Calaos: console opened » à tous les
téléphones, voir [Événements](#événements-admin_event)).

#### Services

Phase 3. Contrat du daemon : `API.md`, « Services ». L'état des unités d'une liste fermée de Calaos
OS, et le redémarrage, le démarrage, l'arrêt et l'activation au démarrage de certaines. Le client
nomme un `id`, jamais une unité. Session admin seulement (pas de code push).

`id` de la liste, dans l'ordre : `calaos-server`, `calaos-home`, `mosquitto`, `zigbee2mqtt`,
`influxdb`, `grafana`, `hue2mqtt`, `envoy`, `olad`, `ntp`, puis, en lecture seule ou en
redémarrage seul, `calaos-container`, `haproxy`, `networkd`, `ssh`. Le DDNS, reporté, n'a pas
de ligne.

**`services_list`** (sans `params`) → `result` : `{"services": [{"id", "unit", "kind",
"actions", "restartable", "active_state", "sub_state", "enabled", "startup", "boot_unit", "since"}]}`,
recopié du daemon.
- `kind` vaut `service`. `restartable` est un booléen, vrai quand `restart` est dans `actions`.
- `actions` : les verbes que la ligne accepte, parmi `restart`, `start`, `stop`, `enable`,
  `disable`, dans cet ordre ; `[]` pour une ligne en lecture seule (`networkd`). Seul `restart`
  pour `calaos-server`, `calaos-container`, `haproxy`, `ssh` (un `stop` ou un `disable` y
  laisserait une box qu'aucun client ne répare) et `ntp` (le client NTP se pilote par `time_set`,
  voir [Heure de la box](#heure-de-la-box)). Les autres lignes acceptent les cinq. Le client
  n'affiche un bouton que pour un verbe de `actions`.
- `active_state` est l'`ActiveState` de systemd ou `not-found` (unité non installée) ;
  `enabled` est l'`UnitFileState` brut (`""` pour `not-found`).
- `startup` : l'unité de `boot_unit` démarre-t-elle au boot, en un mot : `enabled`, `disabled`,
  `masked`, `static` (tout autre état), ou `""` pour `not-found`. C'est ce champ que le client
  affiche, pas `enabled`.
- `boot_unit` : l'unité sur laquelle `enable` et `disable` agissent, égale à `unit` pour toutes les
  lignes.
- `since` est une date RFC 3339 UTC ou `null`.

```json
{"msg": "admin", "msg_id": "9",
 "data": {"action": "services_restart", "admin_session": "9b1f…e4", "params": {"unit": "mosquitto"}}}
```

**`services_restart`** (`params.unit`) → `result` : `{"id": "mosquitto", "state": "restarting"}`.
- calaos_server exige `unit` conforme à `^[a-z0-9-]{1,32}$`, sinon `invalid_request` sans appel au
  daemon. Le daemon refuse un `id` inconnu ou non redémarrable (`networkd`) en `invalid_request`
  (« unknown or non-restartable service »).
- `upgrade_in_progress` pendant un job, pour **toute** unité.
- Le daemon répond d'abord, puis redémarre l'unité environ 500 ms plus tard : la réponse ne dit rien
  du résultat, qui se lit en sondant `services_list`. Redémarrer `calaos-server` coupe la connexion
  du client dans les secondes qui suivent ; ce n'est pas une erreur, le client reprend après un
  nouveau login (voir [Reprise](#reconnexion-et-reprise-de-la-webapp)) avec la même `admin_session`.
- Erreurs : `https_required`, `invalid_request`, `bad_session`, `admin_not_set`,
  `upgrade_in_progress`, `service_unavailable`, `network_pending` (409, avec `change_id` et
  `expires_in` : un changement réseau attend sa confirmation, et le restart de `calaos-container`
  le perdrait), `rate_limited`, `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`,
  `internal`.

**`services_start`, `services_stop`, `services_enable`, `services_disable`** (`params.unit`) →
`result` : `{"id": "mosquitto", "state": "starting"}`, avec `state` à `starting`, `stopping`,
`enabling` ou `disabling` selon l'action. Recopié du daemon (`POST system/services/<verbe>
{"unit"}`).
- Même builder que `services_restart` : `unit` doit respecter `^[a-z0-9-]{1,32}$`, sinon
  `invalid_request` sans appel au daemon. Le daemon refuse en `invalid_request` un `id` inconnu ou
  un verbe absent de `actions` (« action not allowed for this service »).
- `enable` et `disable` agissent maintenant **et** au boot (`enable --now`, `disable --now`) : il
  n'y a pas d'activation sans démarrage. Un `stop` dure jusqu'au prochain redémarrage de la box,
  ou jusqu'à la prochaine mise à jour du paquet si l'unité est `enabled`.
- Contrairement à `restart`, le daemon lance la commande **avant** de répondre (10 s au plus). Le résultat se lit
  en sondant `services_list` (`active_state`, `startup`). Une commande qui sort en erreur est
  `internal`, sans détail dans le message.
- Délai de calaos_server : 15 s pour `start` et `stop` (le client attend 20 s), 35 s pour `enable`
  et `disable`, qui peuvent durer jusqu'à 30 s à cause du rechargement de systemd (le client
  attend 40 s).
- `upgrade_in_progress` pendant un job, pour toute unité. `service_unavailable` (409) : l'unité
  n'est pas installée (« the unit is not installed ») ou est masquée (« the unit is masked ») ; le
  masquage reste une décision prise en SSH. Le daemon relit l'état de l'unité avant de refuser, il
  ne se fie pas à la liste que le client a affichée.
- Scope MCP : ces actions héritent du refus d'`admin` (`not_allowed`).
- Erreurs : celles de `services_restart`, sauf `network_pending` (seul `calaos-container` y est
  exposé, et il n'a que `restart`).

#### Logs

Phase 3. Contrat du daemon : `API.md`, « Logs ». Le daemon lit le journal systemd, borne,
filtre et **masque** les messages. Session admin seulement. Les logs n'atteignent le navigateur
que par https, jamais en diffusion.

**`logs_sources`** (sans `params`) → `{"sources": [{"id", "label"}]}` : seize sources dans
l'ordre de `API.md` (`calaos-server`, `calaos-home`, `haproxy`, `mosquitto`, `zigbee2mqtt`,
`influxdb`, `grafana`, `hue2mqtt`, `envoy`, `calaos-container`, `olad`, `ntp`, `networkd`, `ssh`,
`kernel`, `system`). `label` est un texte anglais fixe ; la webapp traduit par
`id`.

**`logs_query`** → `result` : `{"entries": [...], "truncated", "complete", "next_cursor"}` du daemon.
Tous les `params` sont facultatifs et validés par calaos_server avant tout appel (`invalid_request`
sinon), avec les mêmes limites que le daemon :

| Paramètre | Règle |
|---|---|
| `sources` | tableau de 1 à 8 `id` connus ; défaut `["system"]` |
| `priority` | niveau maximal affiché : entier JSON `0` à `7` **ou** nom (`emerg`, `alert`, `crit`, `err`, `warning`, `notice`, `info`, `debug`) ; la chaîne `"3"` est refusée |
| `since`, `until` | RFC 3339, année 1970 à 2100 (de l'instant UTC) ; `since` égal ou postérieur à `until` est refusé |
| `text` | au plus 256 caractères ; sous-chaîne insensible à la casse, jamais une expression régulière |
| `lines` | entier de 1 à 1000 ; défaut 200 |
| `cursor` | curseur d'une réponse précédente, 1 à 512 caractères ASCII imprimables, espace exclu |
| `direction` | `backward` (défaut : les `lines` plus récentes avant `until` ou `cursor`) ou `forward` (les plus anciennes après `cursor`) ; `forward` sans `cursor` ni `since` est refusé |

- Sans `since`, une requête `backward` couvre les 7 derniers jours.
- `entries` : du plus ancien au plus récent dans les deux sens ; chaque entrée est `{"cursor", "ts",
  "source", "unit", "priority", "pid"?, "message", "cut"?}`. `ts` est RFC 3339 UTC en
  millisecondes, `priority` toujours un entier `0` à `7` dans la réponse, `message` est coupé à
  2048 octets (`"cut": true`).
- **`text` porte sur le message déjà masqué** : chercher la valeur d'un mot de passe ou d'un
  jeton masqué ne trouve rien. La chaîne cherchée n'est jamais journalisée.
- Une requête examine au plus 5000 entrées du journal ou 5 s. `complete: false` : la fenêtre a
  pris fin avant `lines` résultats ; la réponse reste un succès avec les entrées lues.
  `truncated: true` : la limite de 1 Mio a coupé la réponse (les entrées les plus éloignées du point
  de départ sont retirées).
- `next_cursor` est le curseur de la dernière entrée **examinée** dans le sens de la requête
  (correspondante ou non), `null` si rien n'a été examiné ; si `truncated`, celui de la dernière
  entrée gardée.
- **Suivre** (« follow ») = le client répète `logs_query` en `forward` avec le dernier
  `next_cursor`, toutes les 2 s, par ce même relais ; aucun flux, aucun état côté serveur.
  « Charger plus ancien » = `backward` avec le premier curseur reçu.
- Masquage (au mieux) : voir `API.md`, « Redaction » ; calaos_server n'en ajoute pas. Les
  valeurs d'un mot de passe, d'un code ou d'une session ne sont de toute façon jamais écrites par
  calaos_server.
- Limites du daemon : 2 requêtes par seconde et par session, 4 `journalctl` en parallèle sur la
  box ; au-delà `rate_limited` avec `retry_after` (le client attend ce délai). Un échec de
  `journalctl` est `internal`.
- Délai de calaos_server : 10 s ; le client attend environ 12 s (le daemon peut examiner 5 s, puis
  répondre jusqu'à 1 Mio).
- Erreurs : `https_required`, `invalid_request`, `bad_session`, `admin_not_set`, `rate_limited`,
  `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`, `internal`.

#### Heure de la box

Phase 3. Contrat du daemon : `API.md`, « Box time ». Session admin seulement. Le client NTP de
Calaos OS est chrony (`chrony.service`, sinon `systemd-timesyncd.service`) : id `ntp` dans les
services et les journaux.

`<BoxTime>` : `{"now": "<RFC 3339 UTC>", "timezone": "Europe/Paris", "ntp": true, "synchronized":
true, "rtc": "<RFC 3339>" ou null}`.

- **`time_get`** (sans `params`) → `<BoxTime>`.
- **`time_zones`** (sans `params`) → `{"timezones": ["Africa/Abidjan", …]}`.
- **`time_set`** → `<BoxTime>` plus `"restart_required": <bool>`. `params` : `timezone`
  (`^[A-Za-z0-9_+/][A-Za-z0-9_+\-/]{0,63}$`, la règle du daemon, qui vérifie en plus qu'elle
  figure dans `time_zones`), `ntp` (booléen), `time` (RFC 3339, année 2020 à 2100 de l'instant
  UTC : `2020-01-01T00:30:00+01:00` est refusé) ; chacun facultatif, **un au moins** (aucun : `invalid_request`,
  « no field to set »).
  - Ordre : `timezone`, puis `ntp` (`timedatectl set-ntp`, qui pilote chrony), puis
    `time`. `time` avec NTP actif après l'étape `ntp` : `invalid_request` (« ntp is active »).
  - `restart_required` vaut `true` quand `timezone` a changé : la zone est lue par les processus
    de la box à leur démarrage, le client propose donc `services_restart` sur `calaos-server` (et
    `calaos-home` si `services_list` ne le donne pas en `not-found`).
  - **Échec partiel.** La première étape qui échoue arrête la suite : `internal`, avec les champs
    `applied` (étapes déjà appliquées, par exemple `timezone`) et `failed` (par exemple `ntp`), qui
    viennent du `data` du daemon. Les étapes précédentes restent appliquées, `time_get` donne l'état
    réel. calaos_server ajoute à cette erreur `restart_required` (`"true"` si `applied` contient
    `timezone`, `"false"` sinon) : un fuseau déjà changé demande le redémarrage même si l'étape
    suivante a échoué.
  - **Sans réponse** (`daemon_unreachable` ou `admin_unavailable` avec `outcome: "unknown"`, voir
    les délais plus haut) : ne pas renvoyer `time_set`. Relire `time_get` ; si son `timezone` est
    celui envoyé alors qu'il différait avant, proposer le redémarrage comme pour
    `restart_required: true`.
  - Un décalage de plus d'une heure se confirme côté client : reculer l'horloge casse la validité
    des certificats TLS et d'apt.
- Les sessions, les codes, les consoles et le changement réseau en attente mesurent leur durée sur
  l'horloge monotone : changer l'heure ne les allonge ni ne les raccourcit.
- Erreurs : `https_required`, `invalid_request`, `bad_session`, `admin_not_set`, `rate_limited`,
  `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`, `internal`.

#### Réseau

Phase 3. Contrat du daemon : `API.md`, « Network configuration ». Un changement s'applique en
**essai**, et ne devient permanent que si un client le **confirme** depuis la nouvelle adresse ;
sans confirmation dans les **120 s**, le daemon revient seul en arrière, et un redémarrage de la box
revient aussi en arrière (l'essai vit dans `/run`). IPv4 seulement, pas de Wi-Fi. Un seul
changement à la fois.

- **`network_state`** (sans `params`) → `{"interfaces": [{"name", "ipv4", "gateway", "mac", "state",
  "dhcp", "dns_servers", "search_domains"}], "pending": {"change_id", "intf", "applied_at",
  "expires_in"} ou null}`. Session admin seulement ; `expires_in` est le nombre de secondes
  restantes. Une webapp rechargée y retrouve le compte à rebours (à calculer depuis `applied_at`,
  pas depuis l'heure de réception).
- **`network_apply`** protégé par un **code push** de l'action `network_config` : `code_request`
  d'abord, puis `network_apply` avec `code_id` et `code`. Sans approbateur : `approver_required`.
  `params` :

  | Paramètre | Règle |
  |---|---|
  | `intf` | `^[a-zA-Z0-9_.-]{1,15}$`, ni `.`, ni `..`, ni `lo`, ni `apply`, `confirm`, `revert`, `state`, `list`, `trial` ; l'interface doit exister (le daemon le vérifie) |
  | `dhcp` | booléen JSON |
  | `ipv4` | CIDR IPv4 avec préfixe explicite (`192.168.1.10/24`) ; une adresse sans préfixe est refusée ; aucun zéro de tête dans un octet ; obligatoire si `dhcp` est faux |
  | `gateway` | adresse IPv4, facultative (aucune ligne `Gateway=` si vide) |
  | `dns_servers` | au plus 4 adresses IPv4 |
  | `search_domains` | au plus 4 domaines |
  | `code_id`, `code` | 16 hexadécimaux minuscules, 6 chiffres |

  Avec `dhcp: true`, `ipv4`, `gateway`, `dns_servers` et `search_domains` sont absents ou vides,
  sinon `invalid_request`. → `result` : `{"change_id": "<16 hex>", "intf", "applied_at": "<RFC 3339
  UTC>", "confirm_within": 120}`. La box peut changer d'adresse avant que la réponse atteigne le
  client : la connexion tombe, ce n'est pas une erreur.
  Ordre des contrôles du daemon : corps, session, `network_pending` (409, avec `change_id` et
  `expires_in`, le code reste utilisable), `upgrade_in_progress`, puis le code (`approver_required`,
  `code_required`, `bad_code`, `code_expired`).
- **Liaison du code.** Le code `network_config` est lié aux **chaînes validées telles qu'envoyées**,
  six clés toujours présentes : `intf`, `dhcp` (`"true"` ou `"false"`), `ipv4`, `gateway`,
  `dns_servers` (liste jointe par `,`), `search_domains` (liste jointe par `,`) ; `""` quand
  absent ; aucune normalisation d'adresse. Pour `code_request` avec `code_action: "network_config"`,
  le client envoie dans `code_params` les **mêmes champs typés** que `network_apply` (sans
  `code_id` ni `code`) ; calaos_server les valide avec les mêmes règles et reconstruit les six
  chaînes comme le daemon. Un champ modifié entre la demande du code et `network_apply` donne
  `bad_code` (et consomme un des 5 essais de la session).
- **`network_confirm`** et **`network_revert`** (`params.change_id`, 16 hexadécimaux minuscules) :
  toute session admin **vivante** peut les appeler, pas seulement celle qui a appliqué (elle a pu se
  reconnecter à la nouvelle adresse) ; aucun code. → `{"change_id", "intf", "state":
  "confirmed"}` ou `"reverted"`. Un `change_id` inconnu ou terminé (confirmé, annulé, retour
  arrière) : `invalid_request`.
- Événements : voir [Événements](#événements-admin_event) (`network_pending`, `network_applied`,
  `network_rolled_back`).
- Erreurs : `https_required`, `invalid_request`, `bad_session`, `admin_not_set`, `rate_limited`,
  `network_pending`, `upgrade_in_progress`, `approver_required`, `code_required`, `bad_code`,
  `code_expired`, `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`, `internal`.
- Délai de calaos_server pour `network_apply`, `network_confirm` et `network_revert` : 35 s ; le
  client attend 40 s. Le daemon peut dépasser 35 s (voir les délais plus haut) : sans réponse,
  `daemon_unreachable` (ou `admin_unavailable`) porte `outcome: "unknown"`. Le client ne renvoie
  pas l'écriture : il relit `network_state` (`pending` et son `change_id` disent si l'application a
  eu lieu et si la confirmation ou l'annulation a pris), ou attend `network_pending` /
  `network_applied`. Renvoyer un `network_confirm` déjà pris répondrait `invalid_request`.
- `network/list`, `network/trial` et l'ancienne route `network/{intf}` ne sont jamais relayées.

#### Configuration (`config_list`, `config_set`, `config_backup`)

Phase 3. Traitée **dans calaos_server** (propriétaire des fichiers et du registre
`ConfigOptions.cpp`) ; le daemon ne fait que valider la session (`GET admin/status` avec
`X-Calaos-Admin-Session`, comme `mcp_regenerate`). Session admin seulement (pas de code push).

La liste blanche est un confort et un garde-fou, **pas une frontière de sécurité** : le `config put`
HTTP existant (session admin) réécrit toutes les options de `local_config.xml` (`listen_address`,
`port_api`, `cn_pass`, `mcp_token` et `api_allowed_hosts` compris), comme le consigne
`docs/admin/PLAN.md` (amendement (c)). Chaque clé est résolue dans le registre (`type`, choix, bornes, description), donc
la validation est celle du registre. Attributs : `secret` (en écriture seule : jamais rendu, affiché
« défini / non défini »), `restart` (prise en compte après un redémarrage de calaos-server).

| Groupe | Clés | |
|---|---|---|
| Logging | `debug_level`, `debug_domains` | restart |
| Notifications | `smtp_server`, `smtp_port`, `smtp_auth`, `smtp_tls`, `smtp_username`, `smtp_password` (secret), `user_emails` | immédiat |
| History | `history_keep_days` | immédiat |
| InfluxDB | `influxdb_enabled`, `influxdb_version`, `influxdb_host`, `influxdb_port`, `influxdb_database`, `influxdb_org`, `influxdb_bucket`, `influxdb_token` (secret), `influxdb_log_timeout` | restart |
| MCP | `mcp_rate_limit`, `mcp_ban_failures`, `mcp_ban_seconds` | restart (sidecar) |
| OTA | `ota_enabled`, `ota_rescan_interval` | restart |
| Origin | `api_allowed_hosts` | immédiat (relu à chaque contrôle d'Origin, sans joker) |
| Location | `latitude`, `longitude` | immédiat |
| Kiosk | `show_cursor`, `dpms_enable`, `dpms_standby`, `lang` | pris par calaos-home à son prochain démarrage |

Ni listées ni modifiables ici : `cn_user`, `cn_pass`, `calaos_user`, `calaos_password`,
`mcp_token`, `mcp_service_token`, `update_notified_hash`, `port_api`, `listen_address`, `wwwroot`,
`debug_wwwroot`, `debug_enabled`, `max_*`, les `*_timeout` du serveur HTTP, `url_*`,
`calaos_server_host`, `ota_firmware_path`, `notif_development`, et toute clé hors du registre.

- **`config_list`** (sans `params`) → `{"options": [{"key", "group", "type", "description",
  "value", "default", "secret", "set", "restart", "choices"?}]}`, une entrée par clé de la table,
  dans son ordre. `secret`, `set` et `restart` sont des booléens JSON, tout le reste des chaînes :
  - `type` : l'identifiant du registre (`ConfigOptions::typeId()`), l'un des 13 : `string`,
    `bool`, `int`, `float`, `port`, `host`, `path`, `email`, `email_list`, `password`, `token`,
    `enum`, `csv`. Un client traite un identifiant inconnu comme `string`.
  - `group` : le groupe de la table ci-dessus (`Logging`, `Notifications`, `History`, `InfluxDB`,
    `MCP`, `OTA`, `Origin`, `Location`, `Kiosk`), **jamais** la catégorie du registre (`smtp`,
    `calaos_home`, `network`…).
  - `description` : le libellé court du registre (`label()`, par exemple « SMTP port »), traduit
    dans la langue de calaos_server ; jamais le texte long (`doc()`).
  - `default` : la valeur par défaut littérale du registre, `""` quand l'option n'en a pas
    (`smtp_port`, `user_emails`, `dpms_standby`, `lang`…).
  - `set` : la clé a une valeur non vide dans `local_config.xml`. C'est tout ce qu'un secret dit.
  - `value` : la valeur stockée, ou `default` quand la clé n'est pas stockée (`set` faux) ;
    **absent** pour un secret, même défini.
  - `restart` : la colonne de la table (`true` : prise en compte après un redémarrage de
    calaos-server ; les clés Kiosk sont `false`, calaos-home les relit à son démarrage).
  - `choices` : présent seulement quand le registre déclare une liste ; tableau des **valeurs
    brutes** (chaînes), sans leurs libellés, dans l'ordre du registre. Un `bool` peut en déclarer
    une : `ota_enabled` donne `["true", "1", "false", "0"]`.
  - Un `bool` est actif quand `value` vaut `true` **ou** `1` (un fichier écrit à la main,
    `ota_enabled`). Un client écrit toujours `true` ou `false`.
- **`config_set`** (`params.key`, `params.value`, deux chaînes) → `{"key", "value", "set",
  "restart_required"}`. `value` et `set` suivent la règle de `config_list`, relus après
  l'écriture ; `restart_required` est la colonne `restart`. `""` supprime la clé, et les lectures
  reprennent leur défaut : la réponse porte alors `default` (ou `""`) et `"set": false`. Pour un
  secret, la réponse est `{"key", "set", "restart_required"}` sans **aucune** valeur (`set` vaut
  `false` après un `""`). Erreurs propres, toutes `invalid_request` avec un `msg` affichable :
  - `key is required` / `value is required` : champ absent ou qui n'est pas une chaîne ;
  - `This key cannot be set here` : clé hors de la table (la comparaison est exacte) ;
  - `value is longer than 4096 bytes`, `value must not contain a control character`
    (octet < 0x20 ou 0x7f, NUL compris) ;
  - sinon la raison du registre telle quelle (`ConfigOption::validate()`, traduite). Le registre
    décide : `bool` accepte `true` et `false`, plus `1` et `0` là où il les liste
    (`ota_enabled`) ; une plage, une liste, un format d'adresse ou de port sont les siens.

  Ces contrôles précèdent la session : un refus n'appelle pas le daemon et n'écrit rien.
  `Config::BackupFiles()` précède l'écriture, qui suit le chemin de `calaos_config set`
  (`Utils::set_config_option()`, ou `del_config_option()` pour `""`) ; les clés
  « immédiat » agissent là où le code les relit déjà. `restart_required` : la webapp propose
  `services_restart` sur `calaos-server`. Un `local_config.xml` impossible à écrire : `internal`.
- **`config_backup`** (sans `params`) → `{"format": "calaos-config-backup/2", "created_at",
  "server_version", "hostname", "io_format": <entier>, "files": {"io.xml", "rules.xml",
  "local_config.xml"}}`, chaque valeur étant le contenu du fichier. Un fichier absent de la box est
  **omis** de `files` (jamais une chaîne vide, qu'une restauration écrirait). `widgets.xml` n'y
  est pas : aucun programme ne le lit. `created_at` est en RFC 3339 UTC à la seconde
  (`2026-10-25T12:34:56Z`), `server_version` la version de calaos_server, `hostname` le nom
  d'hôte brut (`gethostname()`), `io_format` le format d'`io.xml` de ce serveur (nombre JSON, le
  même que `config get` : `2` aujourd'hui). Comme `config get`, les IO et règles en mémoire sont
  d'abord réécrites dans `io.xml` et `rules.xml`. La webapp en fait l'archive zip du projet
  installer ; le nom `….json` de l'action HTTP ne change pas.
- La session est validée comme `mcp_regenerate`.
- Le journal de calaos_server (`cInfoDom("admin")`) écrit `Admin config set <clé> by <ip> (session
  <id d'audit>)` et `Admin config backup by <ip>`, jamais une valeur.
- Erreurs : `https_required`, `invalid_request`, `not_allowed` (portée MCP), `bad_session`,
  `admin_not_set`, `daemon_too_old`, `daemon_unreachable`, `admin_unavailable`, `internal`.

**Sauvegarde en HTTP.** `config_backup` est aussi une **action HTTP** (corps JSON seulement,
`admin_session` dans le corps, jamais dans l'URL) :

```json
{"action": "config_backup", "cn_user": "user", "cn_pass": "pass", "admin_session": "9b1f…e4"}
```

La réponse est le document de sauvegarde ci-dessus, tel quel (pas de `success`), avec :

```
Content-Type: application/json
Content-Disposition: attachment; filename="calaos-backup-<hostname>-<YYYYMMDD-HHMM>.json"
Cache-Control: no-store
X-Content-Type-Options: nosniff
```

`<hostname>` est le `hostname` du document dont on n'a gardé que `[A-Za-z0-9._-]`, coupé à 32
caractères ; vide une fois nettoyé, il devient `box`. `<YYYYMMDD-HHMM>` est la date de création en
UTC (celle de `created_at`). `Content-Length` est présent, et `Cache-Control` n'apparaît qu'une
fois. `secureTransport()` et le contrôle d'Origin de `/api.php` s'appliquent. Refus, tous **sans
fichier** (pas d'en-tête `Content-Disposition`, aucun contenu de configuration) :

- `admin_session` absente, vide, qui n'est pas une chaîne de 64 hex, ou refusée par le daemon :
  `bad_session` ; transport non sûr (port 5454 en clair, pair hors loopback) : `https_required` ;
  admin non défini ou daemon injoignable : le code de l'[état admin](#état-admin-et-règle-collante).
  Charge d'erreur admin habituelle, statut 200.
- Identifiants foyer faux : le refus foyer de toute action HTTP, statut **400** et fermeture de la
  connexion (la webapp le lit comme « refus foyer »). Un corps qui n'est pas un objet JSON (session
  dans l'URL) : 400 aussi, la session des paramètres GET n'est jamais lue.
- En portée service (MCP) : `not_allowed`, `"msg": "scope denied"`.

La sauvegarde contient des **secrets** (`cn_pass`, `mcp_token`, mots de passe de caméras dans
`io.xml`) : ce que `config get` remet déjà à l'admin et ce dont une restauration a besoin. Le
fichier tombe en clair dans les téléchargements, ce que l'interface dit. La restauration se fait
par [`config_restore`](#restauration-config_restore) ; le `config put` et l'installer restent
possibles.

#### Restauration (`config_restore`)

Phase 3. Action `admin` **locale à calaos_server** (aucun appel au daemon sauf la validation de la
session et la lecture de `update/status`), session admin seulement, **sans code push**. Elle
remet `io.xml`, `rules.xml` et, si elle est fournie, `local_config.xml` depuis un document
`calaos-config-backup/2` (voir [Configuration](#configuration-config_list-config_set-config_backup)).
Elle n'écrit que ce que le `config put` HTTP peut déjà écrire (`docs/admin/PLAN.md`, amendement
(c)), avec en plus une validation, un aperçu et une copie automatique dans `backups/`. Elle ne
touche **jamais** au mot de passe admin, aux approbateurs ni à la configuration réseau, que seul le
daemon détient (amendement (g)).

```
params  {"files": {"io.xml": "<?xml…", "rules.xml": "<?xml…", "local_config.xml"?: "<?xml…"},
         "dry_run": <bool>}
result  {"dry_run": <bool>, "files": ["io.xml", "rules.xml", "local_config.xml"],
         "backup": {"rooms", "ios", "devices", "rules"}, "box": {"rooms", "ios", "devices", "rules"},
         "io_format": <int>, "kept": ["listen_address", …], "warnings": ["household_login_changes", …],
         "previous": "2026/10/25-10-2026_17-14-15"?, "restarting": <bool>}
```

`files` reprend les valeurs du champ `files` d'une sauvegarde. `backup` et `box` comptent les pièces
de la sauvegarde et celles de la box (pour que la webapp montre ce qui va changer) ; `kept` liste
les clés de `local_config.xml` dont la box garde la valeur ; `previous` est le dossier de `backups/`
qui contient l'état d'avant, absent en `dry_run` ; `restarting` est faux en `dry_run`.

- **Validation, avant le contrôle de session et avant tout appel au daemon** : `invalid_request`,
  dont le `msg` est la raison, affichable. Elle refuse :
  - un autre nom de fichier que ces trois-là ;
  - l'absence de `io.xml` ou de `rules.xml` ;
  - un fichier de plus de 2 Mio, qui n'est pas de l'UTF-8 valide ou qui ne commence pas par
    `<?xml` ;
  - un fichier que pugixml n'analyse pas, ou dont la racine n'est pas celle attendue
    (`calaos:ioconfig`, `calaos:rules`, `calaos:config`) ;
  - un `io.xml` dont le format dépasse `IO_FORMAT_DEVICES` (« made by a newer Calaos (format N) »).

  Le refus de perte de devices du `config put` ne s'applique pas : une restauration est explicite.
  L'aperçu avertit par `devices_removed`.
- **Fusion de `local_config.xml`** : chaque option vient de la sauvegarde, sauf une liste fixe de
  clés **liées à la box**, qui gardent la valeur courante de la box (ou restent absentes) :
  `listen_address`, `port_api`, `wwwroot`, `debug_wwwroot`, `calaos_server_host`,
  `ota_firmware_path`, `mcp_service_token`, `update_notified_hash`. Les clés inconnues ou obsolètes
  sont gardées (la migration du démarrage s'en charge). Quand des valeurs changent, `warnings` le
  dit, jamais avec les valeurs : `household_login_changes` (`cn_user` ou `cn_pass` diffèrent),
  `mcp_token_changes`, `ddns_changes`. `invalid_options` liste les clés que le registre
  refuserait. Sans `local_config.xml` dans l'envoi, le fichier de la box n'est pas touché et
  l'avertissement `no_local_config` s'ajoute. Autres avertissements : `devices_removed`.
- **Ordre** :
  1. validation ;
  2. session (`GET admin/status`, comme `mcp_regenerate`) ;
  3. `GET update/status` : un job en cours donne `upgrade_in_progress` ;
  4. `dry_run: true` répond ici ; rien n'est écrit ;
  5. `Config::BackupFiles()`, qui renvoie désormais le dossier. Un échec donne `internal` et rien
     n'est écrit (plus strict que `config put` : la copie est le moyen d'annuler) ;
  6. les sauvegardes de la configuration sont gelées jusqu'à la sortie (`SaveConfigIO`,
     `SaveConfigRule` et les écritures d'option deviennent sans effet), pour qu'un appel
     concurrent ne réécrive pas la configuration en mémoire par-dessus les fichiers restaurés ;
  7. chaque fichier est écrit en `<nom>.restore`, `fsync`, puis `rename` ;
  8. la réponse porte `previous` et `restarting: true` ;
  9. la boucle s'arrête environ 500 ms après l'envoi de la trame (le chemin de sortie du `config
     put`) ; systemd relance calaos-server. Le client reprend après un nouveau login (voir
     [Reprise](#reconnexion-et-reprise-de-la-webapp)) avec la même `admin_session`.

  Un échec d'écriture donne `internal` avec « previous files in backups/<dossier> », et le serveur
  redémarre quand même si un fichier a été remplacé.
- **Taille** : le message WebSocket doit tenir dans `max_websocket_message_size` (4 Mio par
  défaut) ; la webapp le vérifie d'abord.
- Erreurs : `https_required`, `invalid_request`, `bad_session`, `admin_not_set`,
  `upgrade_in_progress`, `not_allowed` (portée MCP, hérité d'`admin`), `daemon_too_old`,
  `daemon_unreachable`, `admin_unavailable`, `internal`.
- Le journal de calaos_server (`cInfoDom("admin")`) écrit `Admin config restore check by <ip>
  (session <id d'audit>)` pour un aperçu et `Admin config restore by <ip> (session <id d'audit>):
  <fichiers>; previous files in backups/<dossier>` pour une restauration, jamais le contenu d'un
  fichier ni une valeur.

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
`X-Calaos-Admin-Session`, et n'accepte que `200` avec `admin_set: true`. Après un
`admin_reset`, la session est invalidée : la réponse est `bad_session`, et `AdminState` passe
à « non défini » à l'événement `admin_reset`.

Avant de servir `config` ou `get_mcp_info`, calaos_server relit `admin/status` dès qu'un daemon
a déjà répondu, admin défini ou non : un admin défini hors de calaos_server (`calaos-os admin
set`) ferme ces actions dès la requête suivante. Seul l'état « daemon injoignable, admin jamais
vu » les sert sans appel.

`change_cred` (mot de passe **foyer**) n'est pas concerné : il reste au niveau foyer, car il
exige déjà l'ancien mot de passe.

Ordre des contrôles pour tout message gardé : authentification foyer, portée MCP,
`secureTransport()`, `LoginThrottle` admin (`admin_unlock`), validation de la requête (table,
champs), état (`AdminState`), présence et format de la session, appel du daemon.

### Portée MCP

`admin_unlock`, `admin_set`, `admin` et `console` rejoignent la liste des messages refusés en
session service (13 au lieu de 9). En phase 3, l'action HTTP `config_backup` s'y ajoute
(`not_allowed`, 14 commandes) ; tout `admin {action}` de phase 3 hérite du refus de `admin`. Le refus a la forme d'erreur admin avec `not_allowed` et
`"msg": "scope denied"`. Sur [`/api/console`](#console-web-apiconsole), un message d'ouverture
qui s'authentifie avec le jeton de service (`token`, celui de `login_service`) ouvre une session
de service et reçoit donc ce refus, sans appel au daemon.

### État admin et règle collante

`AdminState` (module de calaos_server, livré par CB2) lit `GET /api/v2/admin/status` du daemon :
au démarrage, puis toutes les 60 s (toutes les 5 s pendant les 2 premières minutes tant que
l'état est inconnu), et à chaque `admin_set` ou `admin_reset` reçu sur `/events` (CB3).
Un drapeau persistant `admin_seen_set` (fichier du même nom dans `Utils::getCachePath()`)
mémorise qu'un admin a été vu défini. Sur Calaos OS, ce répertoire est `/root/.cache/calaos` du conteneur
calaos-server, monté depuis `/mnt/calaos/cache` de l'hôte (pkgdebs, `start_calaos_server`) : le
drapeau survit à la recréation du conteneur à chaque mise à jour et, comme la configuration, suit
un rollback de `/mnt/calaos`.

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
clients WS authentifiés par `login`, **port 5454 compris** (ni à la session service MCP ni aux
panneaux RemoteUI), un message non sollicité (sans `msg_id`) :

```json
{"msg": "admin_event",
 "data": {"type": "upgrade_progress", "ts": "2026-10-22T09:12:05Z",
          "data": {"status": "upgrading", "total_percent": 42, "current_pkg": "calaos-server", "…": "…"}}}
{"msg": "admin_event",
 "data": {"type": "admin_set", "ts": "2026-10-22T09:14:00Z"}}
```

- `ts` : horodatage RFC 3339 UTC. Pour `upgrade_progress`, c'est l'heure de réception par calaos_server. Pour les alertes `admin_set`, `admin_reset`,
  `approver_set`, `approver_removed` et `console_opened` c'est l'horodatage de l'alerte, **sans**
  `id` ni autre champ (pas de `data`, ni `device_name`, ni `token_id`, ni `session_audit_id`).
- `upgrade_progress` : `data` est l'objet `<Status>` de `API.md` **filtré** (types JSON natifs),
  car un client foyer n'a pas à connaître l'inventaire des paquets de la box. Seuls passent, par
  liste blanche, `status`, `total_percent`, `current_step`, `current_percent`, `pkg_index`,
  `pkg_count`, `progress`, `progress_total` et `packages[]` réduit à `{state, percent}` ;
  `error`, s'il est non vide, devient `upgrade failed` ; `current_pkg`, `message`,
  `packages[].name` et tout champ ajouté plus tard par le daemon sont retirés. Le premier
  `upgrade_progress` de chaque abonnement (instantané du daemon) est filtré de même.
- Phase 3, changement réseau (`data` en types JSON natifs, `ts` = heure de réception par
  calaos_server) :

  ```json
  {"msg": "admin_event", "data": {"type": "network_pending", "ts": "2026-10-25T09:12:04Z",
   "data": {"intf": "eth0", "change_id": "5d0c8e1f2a3b4c6d", "applied_at": "2026-10-25T09:12:03Z", "confirm_within": 120}}}
  {"msg": "admin_event", "data": {"type": "network_applied", "ts": "2026-10-25T09:12:40Z",
   "data": {"intf": "eth0", "change_id": "5d0c8e1f2a3b4c6d"}}}
  ```

  `network_pending` : un changement attend sa confirmation ; après un redémarrage du daemon il est
  republié une fois, avec l'`applied_at` d'origine et `confirm_within` = secondes restantes.
  `network_applied` : il a été confirmé (le daemon n'y met pas `applied_at`). Ni l'un ni l'autre
  n'est rejoué : un client qui se reconnecte lit `network_state`. Ils ne portent aucune adresse
  (`intf`, `change_id`, et pour `network_pending` les durées), car ils partent aussi sur le port
  5454 en clair.
- `network_rolled_back` (alerte du daemon : retour arrière par délai ou par redémarrage après
  l'échéance) suit le chemin d'`admin_set` (événement persistant affiché au login suivant,
  acquittement, déduplication sur `id`), avec `ts` et `data.intf` ; **aucun push** vers les
  téléphones. Texte de l'événement persistant : voir le tableau ci-dessous.
- C'est **tout** ce que reçoit un client foyer. `upgrade_job` n'est pas retransmis : pour
  connaître le job ou `reboot_required`, lire `admin {update_status}`. `push_code` est consommé
  par calaos_server (voir [Approbateurs et codes push](#approbateurs-et-codes-push)) et
  jamais retransmis. Tout autre type est ignoré.
- Les alertes `approver_set` (un téléphone vient d'être enrôlé) et `approver_removed` suivent
  le chemin d'`admin_set` : événement persistant, push à **tous** les jetons, bannière au
  login. Leur texte commence par un préfixe fixe, puis le nom entre guillemets :
  `New approver phone on your Calaos box: "<device_name>"` ou
  `Approver phone removed from your Calaos box: "<device_name>"`. Le nom est choisi par
  qui a enrôlé le téléphone : il est nettoyé comme celui de `register_push`, ses guillemets
  (`"`, `«`, `»`, `“`, `”`, `„`, `‟`, `″`, `‶`, `＂`) deviennent `'`, et il ne commence
  jamais le message, pour ne pas pouvoir se faire passer pour la box. Un nom vide
  s'affiche `?`.
- L'alerte `console_opened` (une console d'administration vient de s'ouvrir) suit le même
  chemin, avec un texte fixe : `Calaos: console opened`. Son `session_audit_id` reste au
  daemon : ni dans le push, ni dans l'événement persistant, ni dans `admin_event`.
- **Textes des alertes.** Chaque alerte enregistrée est un événement persistant de l'historique
  (`eventlog`) : `event_type` `"22"` (`EventPushNotification`), `io_id` et `pic_uid` vides,
  `event_raw` = `{"message": "<texte>", "pic_uid": ""}`. Le texte est fixe (seul le nom entre
  guillemets ou l'interface varient), en anglais, et c'est sur lui que la bannière du login
  reconnaît l'alerte ; le push, quand il y en a un, porte le même texte.

  | Alerte | `message` | Push |
  |---|---|:---:|
  | `admin_set` | `Admin password set on your Calaos box` | oui |
  | `admin_reset` | `Admin password removed from your Calaos box` | oui |
  | `approver_set` | `New approver phone on your Calaos box: "<device_name>"` | oui |
  | `approver_removed` | `Approver phone removed from your Calaos box: "<device_name>"` | oui |
  | `console_opened` | `Calaos: console opened` | oui |
  | `network_rolled_back` | `Network change rolled back on your Calaos box: <intf>`, `<intf>` étant le `data.intf` de l'alerte, ou `?` s'il n'est pas un nom d'interface valide (règle d'`intf` de `network_apply`, `lo` exclu) | non |
- calaos_server n'enregistre et ne retransmet une alerte qu'à sa
  première réception, **redémarrages compris** : les 64 derniers `id` enregistrés sont gardés
  dans le fichier `admin_alerts` de `Utils::getCachePath()` (à côté d'`admin_seen_set`). Une
  alerte déjà enregistrée, rejouée par le daemon après un ack perdu ou un redémarrage, est
  seulement acquittée : ni second événement persistant, ni push, ni `admin_event`.
- À la première réception, il enregistre l'événement persistant, le retransmet, relit
  `admin/status`, l'acquitte (`admin/alerts/ack`) dès que l'événement est écrit, et envoie le
  push à tous les jetons. L'ack n'attend pas le push : une alerte laissée en attente serait
  rejouée, et enregistrée de nouveau, à chaque redémarrage.
- Le daemon ne renvoie pas une alerte sur le flux où elle est déjà passée, ni une alerte
  acquittée : calaos_server retente lui-même un push ou un ack perdu au bout de 60 s, puis 120,
  240, 480 et 960 s (5 essais, 64 séries au plus), dans le respect de la limite de push par
  type, puis abandonne. Un redémarrage abandonne aussi les essais en cours ; jamais un second
  push une fois le push parti.
- Au plus **un push par type d'alerte toutes les 10 min** : le premier est toujours envoyé, les
  suivants de la fenêtre n'ont que leur événement persistant. Un faux daemon sur le port 8000
  ne peut donc pas inonder les téléphones. Pour `approver_set` et `approver_removed`, la
  fenêtre est par téléphone (`token_id`) : l'enrôlement d'un second téléphone juste après un
  premier est poussé lui aussi. Pour `console_opened`, elle est par alerte (`id`) : chaque
  ouverture est poussée, même juste après celle de l'admin ; les codes `console_open` du
  daemon la bornent déjà.
- Une alerte dont l'`id` est inutilisable est acquittée (si elle fait au plus 256 caractères)
  et journalisée, jamais enregistrée ni retransmise ; un événement imbriqué au-delà de 2048
  niveaux est ignoré (même limite que les requêtes des clients).
- L'abonnement porte le bearer dans l'en-tête `Authorization`. Coupé, il est rétabli avec une
  attente croissante (1 s, doublée jusqu'à 60 s) ; calaos_server envoie un ping toutes les 15 s
  et quitte un flux muet depuis 75 s (le daemon pinge toutes les 30 s).
- Aucun rejeu : un événement émis pendant qu'un client est déconnecté est perdu.

### Origin

Les contrôles décrits ici sont ceux de CB1 (`OriginPolicy`, appelé par
`HttpClient::processHeaders`) ; ils s'appliquent au handshake WS (tout chemin) **et** aux
requêtes HTTP `/api`, `/api.php` et `/api/…`, préflight `OPTIONS` compris. Les fichiers statiques
(`/app/…`) ne sont jamais refusés : Chromium envoie un Origin avec les scripts module et les
polices de la webapp, et un refus donnerait une page blanche. Ils sont servis sans
`Access-Control-Allow-Origin` une fois le contrôle actif. Le tunnel `/mcp` est aiguillé avant et
n'est pas concerné (voir [15_mcp_server.md](15_mcp_server.md)). Le handshake de
[`/api/console`](#console-web-apiconsole) est **toujours** contrôlé, admin défini ou non.

- Absence d'`Origin` : acceptée (clients natifs : installer, mobile, remote_ui, MCP).
- `Origin: null` : refusé, comme tout Origin qui n'est pas `http://` ou `https://` suivi d'un
  hôte et d'un port éventuel (chemin, `user@` ou caractère non ASCII : refusé). Casse, point
  final et port par défaut sont normalisés, une IP est comparée sous sa forme canonique. Comparé
  à `Host`, un `Host` sans port vaut le port par défaut du schéma de l'Origin.
- Un Origin est accepté dans trois cas :
  - pair loopback avec `X-Forwarded-Proto: https`, Origin en `https://` et hôte et port de
    l'Origin égaux à ceux de `Host` ;
  - hôte qui est une IP portée par une interface de la box, loopback exclu (dans un Origin il
    désigne la machine du navigateur), égale à l'hôte de `Host` et sur le même port : derrière
    haproxy, une page servie par un autre service de la box (`http://IP:3000`) est refusée. Les IP sont relues par
    `getifaddrs` au plus toutes les 30 s, et aussitôt (au plus une fois par seconde) quand une
    IP inconnue se présente ;
  - hôte de l'option `api_allowed_hosts`. Une entrée peut porter un port (`hôte:port`), sinon
    tout port est accepté ; elle n'est pas comparée à `Host` (machine de dev sur un autre port).
    Le schéma d'une entrée est ignoré. Une entrée inutilisable (chemin, joker) est ignorée et
    signalée une fois dans le journal.
- Pas de hostname, de `.local` ni de `localhost` automatiques.
- Refus : HTTP 403 avant le handshake ou la requête, sans `Access-Control-Allow-Origin`
  (celui-ci n'est renvoyé que pour un Origin accepté : reflété en HTTP comme au handshake WS ;
  en HTTP il s'accompagne de `Access-Control-Allow-Credentials: false` et de `Vary: Origin`).
- **Tant que l'admin n'est pas défini** (selon `AdminState`), rien ne change : tout Origin est
  accepté, `Access-Control-Allow-Origin` reflète l'Origin en HTTP et vaut `*` au handshake WS,
  comme aujourd'hui. Les Origins qui seraient refusés sont seulement journalisés, avec l'IP du
  client (`X-Forwarded-For` derrière haproxy) : une ligne par Origin, au plus 64 Origins par
  heure. Le contrôle s'applique dès que l'admin est défini, et aussi quand le daemon est
  injoignable avec le drapeau `admin_seen_set` présent. Il porte sur chaque nouvelle requête ou
  handshake : une connexion WS ouverte avant n'est pas revérifiée.
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
| Réponse à `admin_unlock` (client → « calaos-server trop ancien, mettez à jour la box ») | **5 s** | webapp (WA1). Une vérification légitime répond avant, car le daemon refuse tout de suite (`429`) une vérification concurrente au lieu de la mettre en file. Les 4 s supposent qu'une opération argon2id du daemon (CC1) tient en moins de 3 s sur la plus lente des box supportées. calaos_server ne met jamais lui-même en file un appel admin |
| Appel de calaos_server au daemon | 4 s par défaut (connexion et réponse) ; 10 s pour `logs_query` ; 15 s pour `services_start` et `services_stop` ; 35 s pour `services_enable`, `services_disable` et `time_set` ; **120 s pour `update_check`** (`apt-get update` synchrone) ; 30 s pour `system_restart` (`systemctl restart calaos-home` attend l'arrêt du conteneur du kiosque) | calaos_server (CB2/CB3) |
| Réponse aux messages admin (client) | 5 s pour `admin_unlock`, `admin_set` et tout `admin` sauf `update_check`, `system_restart`, `logs_query`, `time_set` et `services_start` / `stop` / `enable` / `disable` ; 125 s pour `update_check` ; 35 s pour `system_restart` (au-delà des 30 s de calaos_server) ; 12 s pour `logs_query` (au-delà des 10 s de calaos_server) ; 40 s pour `time_set` (au-delà des 35 s de calaos_server) ; 20 s pour `services_start` / `stop` (au-delà des 15 s de calaos_server) ; 40 s pour `services_enable` / `disable` (au-delà des 35 s de calaos_server) | webapp (WA1) |
| Durée de la session admin | **15 min absolues** à l'émission, non glissantes | daemon |
| Sondage de `admin/status` | 60 s ; 5 s pendant les 2 premières minutes tant que l'état est inconnu ; l'échec n'est journalisé qu'une fois | `AdminState` |
| `admin_unavailable` : nouvel essai | toutes les 5 s pendant les 2 premières minutes, puis toutes les 60 s | webapp (WA1), même cadence que le sondage d'`AdminState` |
| `daemon_too_old` : bascule sur « Redémarrez la box » | 2 min d'état persistant | webapp |
| `rate_limited` | `retry_after` secondes (champ de l'erreur) | daemon, `LoginThrottle` |
| Ping WS client | 15 s (existant) ; haproxy `timeout tunnel 3600s` | calaos_server |
| Message d'ouverture de `/api/console` après la montée | 30 s, puis `invalid_request` | calaos_server |
| Attachement à la socket de la console | 10 s, puis `daemon_unreachable` ; le daemon tue une console non attachée au bout de 30 s | calaos_server, daemon |
| Console ouverte | 1 h absolue ; 15 min sans saisie ; ping du daemon toutes les 30 s, coupure sans pong en 60 s | daemon |
| Alertes `admin_set` / `admin_reset` / `approver_set` / `approver_removed` / `console_opened` | persistées par le daemon jusqu'à l'ack, rejouées à chaque abonné `/events` | daemon |
| Changement réseau en attente | 120 s pour confirmer (`confirm_within`), depuis `applied_at` ; retour arrière automatique ensuite | daemon |
| Suivi des logs (`logs_query` en `forward`) | toutes les 2 s ; 2 requêtes par seconde et par session au plus | webapp, daemon |
| Code push | valable 60 s ; droit console 30 s après `code_verify` ; 10 s entre deux codes d'une session | daemon |

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
7. `admin_unavailable` : nouvel essai selon la cadence des Délais ; `daemon_too_old` et
   `daemon_unreachable` : attente croissante. Dans les deux cas, sans effacer la session.
8. Un `admin_event` `admin_set` ou `admin_reset` reçu pendant qu'une session est ouverte
   signifie que celle-ci est invalidée : l'oublier. Les alertes manquées pendant une
   déconnexion sont rejouées par le daemon à calaos_server, qui les présente au login suivant
   (événement persistant).
9. Phase 3, redémarrage de `calaos-server` (`services_restart`) : la connexion tombe dans les
   secondes qui suivent, ce que le client traite comme « redémarrage » et non comme une erreur ;
   il reprend aux étapes 1 à 3, puis sonde `services_list`.
10. Phase 3, changement réseau : si l'adresse de la box change, le client rouvre la page à la
    nouvelle adresse, se reconnecte, déverrouille, lit `network_state` (le changement en attente et
    son échéance, calculée depuis `applied_at`), puis appelle `network_confirm` ou `network_revert`.

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
 "data": {"token": "<jeton>", "hardware": "android", "push_format": "data",
          "device_name": "Pixel 8"}}
```
```json
{"cn_user": "user", "cn_pass": "pass", "action": "register_push",
 "token": "<jeton>", "hardware": "android", "push_format": "data",
 "device_name": "Pixel 8"}
```
Réponse : `{"success":"true"}` (dans `data` en WebSocket, muette sans `msg_id`).

| Champ | Valeurs | Défaut |
|---|---|---|
| `token` | jeton de l'appareil, non vide | — (refus `"false"`) |
| `hardware` | `android` ou `ios`, exactement | — (refus `"false"`) |
| `push_format` | `notification` ou `data` (chaîne) | `notification` |
| `device_name` | nom de l'appareil, affiché à l'admin | `''` (le nom connu est gardé) |
| `push_key` | clé de chiffrement du code push, 64 hexadécimaux minuscules (phase 3) | absente (la clé connue est gardée) |

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
- Les jetons non réenregistrés depuis 30 jours sont supprimés, sauf ceux des approbateurs.
- `device_name` est **optionnel** et nettoyé avant d'être stocké dans `push_tokens` :
  UTF-8 invalide, caractères de contrôle, bidi (`U+202E`, `U+061C`…), invisibles,
  sélecteurs de variante et caractères de remplissage qui s'affichent vides (`U+3164`,
  `U+115F`, `U+2800`…) retirés ; tout espace Unicode (Zs) devient une espace simple,
  les espaces sont repliées et rognées, puis le nom est coupé à 64 points de code. Il
  sert à choisir le téléphone approbateur (`approver_candidates`) et part avec
  l'enrôlement. Un nom vide une fois nettoyé est refusé (le daemon le refuserait) :
  le jeton est enregistré, son nom connu reste. Le nom ne prouve rien : n'importe quel
  client foyer peut l'envoyer pour son jeton.
- Le jeton d'un **approbateur** (marqué à l'alerte `approver_set`, et d'après
  `approver_list` à chaque `approver_candidates`) garde le nom et le format de
  son enrôlement : un `register_push` de ce jeton, par qui que ce soit, ne fait que
  rafraîchir sa date (pour la clé push, voir plus bas). `approver_removed` le libère.
- `push_key` (phase 3) est **optionnelle** : 32 octets du CSPRNG de l'app, en 64 hexadécimaux
  minuscules, générés une fois et envoyés avec chaque enregistrement. Elle est stockée dans
  `push_tokens.push_key` (colonne ajoutée à l'ouverture de la base) et sert uniquement à chiffrer
  les codes push de ce jeton (voir [Code push chiffré](#code-push-chiffré)) ; elle n'authentifie
  pas le téléphone. Une valeur qui n'est pas 64 hexadécimaux minuscules est **ignorée** (le jeton
  est quand même enregistré) et un avertissement est journalisé une fois par processus, sans la
  valeur. Une requête sans `push_key` valide ne change pas la clé déjà stockée.
  Pour un jeton non approbateur, une nouvelle clé valide **remplace** l'ancienne. Pour un
  approbateur : une **première** clé est acceptée (les approbateurs de la phase 2 n'en ont pas, et
  ils ne peuvent pas se ré-enrôler avec le même jeton : `approver_exists`, `approver_last`) ; dès
  qu'il en a une, elle est **gelée** (même règle que le nom et le format) et un autre `push_key`
  ne fait que rafraîchir la date. À l'`approver_set`, la clé gelée est celle qui a scellé le code
  `approver_confirm` envoyé dans les 10 minutes précédentes, que le téléphone a prouvé détenir,
  même si un autre `register_push` l'a remplacée entre-temps. Risque assumé de la première clé : qui détient le jeton et le
  mot de passe foyer peut la poser avant le téléphone, lire les codes (ce qu'il peut déjà faire
  en clair) et empêcher le vrai téléphone de les déchiffrer ; `secure` devient visible dans la
  webapp, et le remède est un second approbateur ou `calaos-os admin reset` en SSH. Une clé perdue
  par un approbateur dont le jeton ne change pas se résout de la même façon (jeton neuf par
  réinstallation, ou `admin reset`). Un jeton sans clé reçoit le texte hérité.
  Les listes `approver_candidates` et `approver_list` exposent `secure` (clé stockée ou non).

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
