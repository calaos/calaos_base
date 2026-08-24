# Serveur MCP — calaos_mcp

> **Provenance des exemples.** Chaque bloc est marqué `(capturé, <golden>)` quand il vient
> d'un fichier de référence de `tests/core/golden/`, `(dérivé, Fichier:L-L)` quand il est
> reconstruit depuis le code, ou `(intégral, Fichier:L-L)` quand il en est une copie
> byte-identique.

## Vue d'ensemble

`calaos_mcp` est un **sidecar Python** qui expose l'installation domotique Calaos
via le **Model Context Protocol** (MCP, transport Streamable HTTP). Il permet à un
client LLM (Claude Desktop, Claude code, claude.ai web…) de lire l'état et de
piloter les équipements en langage naturel.

Principes de conception :

- **Lancé automatiquement** par `calaos_server` au démarrage (aucune config
  manuelle, comme les drivers Python Reolink/Roon).
- **Aucun port supplémentaire** : le sidecar écoute sur un **socket Unix** et est
  reverse-proxifié via le chemin `/mcp` du port HTTP principal (5454). Le reverse
  proxy HTTPS déjà en place sur Calaos OS (haproxy) couvre donc MCP sans
  configuration réseau additionnelle (`https://<hôte>/mcp`).
- **Auto-configuration** : le token Bearer (`mcp_token`) et le token de service
  (`mcp_service_token`) sont générés et persistés dans `local_config.xml` au
  premier démarrage.

> ⚠️ Contrairement aux drivers `ExternProc` (voir [12_extern_proc.md](12_extern_proc.md)
> et [14_python_extern_proc.md](14_python_extern_proc.md)), `calaos_mcp`
> n'utilise **pas** le framing binaire IPC. C'est un serveur HTTP
> (FastMCP/uvicorn) sur socket Unix, proxifié en octets bruts par le C++.

---

## Architecture

```
            Internet
               │ https://calaos.local/mcp   (Bearer mcp_token)
               ▼
       ┌────────────────┐
       │ haproxy (TLS)  │  (Calaos OS, déjà en place)
       └───────┬────────┘
               │ http://127.0.0.1:5454/mcp   (+ X-Forwarded-For appendu)
               ▼
  calaos_server (daemon C++)
   ├── HttpServer (port 5454)
   │    └── WebSocket::ProcessData  ── sniff 1re ligne ──┐
   │         ├── /api        → JsonApiHandlerWS          │
   │         ├── /api/v3/... → RemoteUIWebSocketHandler  │
   │         └── /mcp/*      → McpProxyHandler ──────────┤ octets bruts
   │                                                     ▼
   │                                   $CALAOS_CACHE_PATH/mcp.sock (0660)
   │                                                     ▲ uvicorn (fd=…)
   │  McpServerManager (spawn au boot) ── spawn ────────►│
   │   uvw::ProcessHandle($bindir/calaos_mcp)            │
   │   env: CALAOS_CONFIG_PATH, CALAOS_CACHE_PATH,       │
   │        CALAOS_MCP_SOCKET, CALAOS_API_URL,           │
   │        CALAOS_LOG_LEVEL/DOMAINS                     │
   │        (aucun secret dans l'environnement)          │
   │                                                     ▼
   │                              calaos_mcp (sidecar Python)
   │                               ├── FastMCP (9 tools) sur /mcp
   │                               ├── BearerAuthMiddleware (mcp_token)
   │                               └── CalaosClient ── WS login_service ──┐
   │                                                                      │
   └── JsonApiHandlerWS ◄── ws://127.0.0.1:5454/api (session serviceScope)┘
```

Chaîne d'un appel d'outil :
client MCP → haproxy → proxy C++ (`/mcp`) → socket Unix → sidecar → tool FastMCP
→ `CalaosClient` (WS `login_service`) → `JsonApi` → mutation IO réelle.

---

## Côté C++

### McpServerManager
**Fichiers :** [McpServerManager.h](../src/bin/calaos_server/McpServerManager.h),
[McpServerManager.cpp](../src/bin/calaos_server/McpServerManager.cpp)

Singleton. `McpServerManager::Instance().start()` est appelé dans
[main.cpp](../src/bin/calaos_server/main.cpp) juste après `HttpServer::Instance(port)`
(`main.cpp:170-175`). `stop()` est appelé dès la sortie de la boucle libuv, **avant**
`HttpServer::disconnectAll()` (`main.cpp:223-224`). Responsabilités :

1. **Génération des tokens** (`ensureTokens`, `:104-137`) : si `mcp_token` /
   `mcp_service_token` sont absents de `local_config.xml`, les générer (64 caractères
   hexadécimaux = **256 bits**) et les persister avec `Utils::set_config_option`.
2. **Socket** : `$CALAOS_CACHE_PATH/mcp.sock` (`:86-92`), `unlink` préalable si résiduel
   (`:149`).
3. **Spawn** (`uvw::ProcessHandle`) de `$bindir/calaos_mcp` avec les variables
   d'environnement (voir plus bas). Si le wrapper n'est pas installé (`--without-mcp`, deps
   Python absentes), un avertissement est logué et **rien n'est lancé** (`:178-187`).
   stdout/stderr du sidecar sont récupérés, découpés en lignes et réémis dans le log Calaos
   sous le domaine `mcp` — stdout en `info`, stderr en `error` (`:295-330`).
4. **Redémarrage automatique** : `on<uvw::ExitEvent>` **et** `on<uvw::ErrorEvent>` → respawn
   avec backoff **1, 2, 5, 10, 30, 60 s** puis 60 s indéfiniment, sauf si un arrêt propre est
   en cours (`:217-230`, `:279-293`). Le compteur repart à zéro dès qu'un spawn réussit
   (`:273-276`).
5. **Arrêt** : `SIGTERM` au sidecar puis `unlink` du socket (`:156-172`).

> ⚠️ **Garde `pid > 0` (T3.9).** `stop()` porte la même garde que `~ExternProcServer` : après
> un `uv_spawn` raté le handle garde `pid == 0`, et `uv_kill(0, SIGTERM)` SIGTERMe **tout le
> groupe de processus**. Voir [12_extern_proc.md](12_extern_proc.md), § « La garde `pid > 0` ».

#### Génération des tokens : CSPRNG, pas d'UUID (T1.7)

Les tokens sont tirés d'**OpenSSL `RAND_bytes`**, 32 octets rendus en 64 caractères hex. Le
choix est explicite dans le code, et il exclut nommément `Utils::createRandomUuid()`
(intégral, `McpServerManager.cpp:44-53`) :

```cpp
// Generate a 64-hex-char token (256 bits) from a cryptographically secure
// random source (OpenSSL RAND_bytes), matching the token format/length
// already documented in AGENTS.md and used elsewhere in calaos_server
// (RemoteUIProvisioningHandler::generateAuthToken, HMACAuthenticator::
// generateNonce). Deliberately does NOT use Utils::createRandomUuid, which
// seeds rand() from the clock on every call (weak, and can even collide
// within the same microsecond).
std::string generateToken()
{
    constexpr size_t TOKEN_BYTES = 32; // 256 bits -> 64 hex chars
```

Si `RAND_bytes` échoue, **aucun token n'est écrit** : le manager logue une erreur et le
sidecar sera incapable de s'authentifier (ou le proxy refusera tout), plutôt que de persister
un secret faible (`:56-60`, `:110-113`, `:126-129`).

⚠️ Vider `mcp_token` dans `local_config.xml` en fait générer un nouveau au démarrage suivant,
ce qui **invalide tous les clients déjà configurés** (`ConfigOptions.cpp:860-871`).

Variables d'environnement passées au sidecar (**aucun secret** ne transite par
l'environnement — mitigation S1), dérivé de `McpServerManager.cpp:247-261` :

| Variable | Valeur |
|---|---|
| `CALAOS_CONFIG_PATH` | répertoire de config **actif** (voir gotcha ci-dessous) |
| `CALAOS_CACHE_PATH` | `Utils::getCachePath()` |
| `CALAOS_MCP_SOCKET` | chemin du socket Unix |
| `CALAOS_API_URL` | `ws://127.0.0.1:<port_api>/api` |
| `CALAOS_LOG_LEVEL`, `CALAOS_LOG_DOMAINS` | options `debug_level` / `debug_domains` |

S'y ajoutent, héritées du parent : `PATH`, `HOME`, `LANG`, `LC_ALL`, `LANGUAGE`,
`LD_LIBRARY_PATH`, `PWD`.

> **Gotcha config path :** `Utils::getConfigPath()` ignore `--config` (il
> re-dérive depuis `$HOME`). Pour passer le **vrai** répertoire de config au
> sidecar, `McpServerManager` utilise `Utils::getConfigFile("")` (qui respecte
> `_configBase`) puis retire le `/` final (`:238-245`). Utiliser `getConfigPath()` ici
> ferait lire au sidecar le mauvais `local_config.xml`.

### McpProxyHandler
**Fichiers :** [McpProxyHandler.h](../src/bin/calaos_server/McpProxyHandler.h),
[McpProxyHandler.cpp](../src/bin/calaos_server/McpProxyHandler.cpp)

Reverse proxy octets-bruts entre une connexion TCP `/mcp/*` et le socket Unix du
sidecar. `WebSocket::ProcessData` accumule les premiers octets et appelle
`McpProxyHandler::sniffRequest()` (`WebSocket.cpp:73-77`). Le verdict est un enum à **cinq**
valeurs (`McpProxyHandler.h:45-52`) :

| Verdict | Traitement (`WebSocket.cpp:78-141`) |
|---|---|
| `NotEnoughData` | on attend d'autres octets ; au-delà de la limite de sniff, la connexion repart en HTTP normal |
| `Mcp` | bascule en mode proxy : le timeout de lecture HTTP est annulé, tout est splicé en brut dans les deux sens (supporte les réponses SSE longues du Streamable HTTP) |
| `InvalidPath` | `400 Bad Request` + fermeture (mitigation S5, anti path-traversal vers `/api`) |
| `Smuggling` | `400 Bad Request` + fermeture (mitigation S8) |
| `NotMcp` | la connexion repart dans le flux JsonApi / WebSocket normal |

La regex de chemin est stricte (intégral, `McpProxyHandler.cpp:41`) :

```cpp
    static const std::regex re(R"(^/mcp(?:/[A-Za-z0-9._~-]+)*/?$)");
```

La chaîne de requête est retirée avant validation (`:129-132`) — uvicorn la validera plus
loin. La limite de sniff est de **8 KiB** (`SNIFF_LIMIT`, `:34`) : si la fin du bloc
d'en-têtes n'est pas vue avant, la connexion est proxifiée **sans** le contrôle
anti-smuggling plutôt que d'être bloquée (`:141-154`).

`detectSmuggling()` (`:61-107`) refuse trois formes : **deux `Content-Length`** ou plus, un
`Transfer-Encoding` **accompagné** d'un `Content-Length`, et un `Transfer-Encoding` dont la
valeur n'est ni `chunked` ni `identity`.

L'authentification Bearer **n'est pas** faite par le proxy : elle est déléguée
au sidecar Python, ce qui évite de dupliquer la logique. `McpProxyHandler::sendError()` sert
aussi au reste du serveur pour répondre `503 Too many connections` sur un socket brut
(`HttpServer.cpp:91`).

### login_service / serviceScope
**Fichier :** [JsonApiHandlerWS.cpp](../src/bin/calaos_server/JsonApiHandlerWS.cpp)

`processLoginService()` (`:530-567`) authentifie le sidecar via `mcp_service_token`, en
comparaison sécurisée (`secureCompare`) et **derrière le throttle de login** (`LoginThrottle`,
partagé avec le login utilisateur). Un token de référence **vide n'accorde jamais l'accès**,
quoi qu'envoie le client. Un échec renvoie une réponse d'erreur **puis ferme la
connexion**. Le succès pose `loggedin = true` **et** `serviceScope = true`.

Réponse d'un `login_service` réussi (capturé, `tests/core/golden/e40e_ws_login_service_success.json`,
intégral) :

```json
{
  "data": {
    "success": "true"
  },
  "msg": "login_service",
  "msg_id": "1"
}
```

Réponse d'un token invalide (capturé,
`tests/core/golden/e40e_ws_login_service_invalid_token.json`, intégral) :

```json
{
  "data": {
    "error": "invalid token",
    "success": "false"
  },
  "msg": "login_service",
  "msg_id": "1"
}
```

Dans la portée service, **sept** commandes sont refusées (`JsonApiHandlerWS.cpp:177-221`) :
`set_param`, `del_param`, `audio_db`, `set_timerange`, `eventlog`, `register_push`,
`settings`. Le refus prend la forme d'un message dont le `msg` est **le nom de l'action
refusée**, et dont le `data` porte `error` **sans** clé `success` (capturé,
`tests/core/golden/e40e_ws_scope_denied.json`, intégral — ici pour `set_param`) :

```json
{
  "data": {
    "error": "scope denied"
  },
  "msg": "set_param",
  "msg_id": "1"
}
```

#### ⚠️ Deux écarts de portée mesurés, gelés, à connaître

Ce sont des **limites réelles du cloisonnement**, pas des détails d'implémentation. Elles
sont mesurées, pas déduites, et corriger l'une ou l'autre changerait un comportement visible
client — d'où le gel.

1. **Une session `serviceScope` reçoit TOUS les événements de la maison.**
   `JsonApiHandlerWS::handleEvents()` (`:53-60`) ne teste **que** `loggedin` et **jamais**
   `serviceScope`. Le cloisonnement n'est appliqué qu'à la moitié requête/réponse de l'API,
   pas à la moitié push. Concrètement : le sidecar, à qui l'on **refuse** de lire les
   paramètres, la base audio ou le journal d'événements, **reçoit malgré tout le flux temps
   réel complet** — ids d'IO et valeurs d'état compris, donc l'essentiel de ce que le refus
   était censé protéger. Mesuré par E4.0d.

2. **`autoscenario` n'est pas soumis au `serviceScope`.** La commande n'est pas dans la liste
   des sept (`:205-206` : elle appelle `processAutoscenario` sans garde), alors qu'elle
   **crée, modifie et supprime** des scénarios ainsi que leurs règles associées. Une session
   de scope service à qui l'on refuse d'écrire une plage horaire peut donc **détruire des
   scénarios**. L'asymétrie est mesurée.

> Le sidecar Python n'exploite ni l'un ni l'autre : il n'ouvre aucun abonnement d'événement
> autre que l'invalidation de son cache, et **aucun de ses 9 tools n'émet `autoscenario`**
> (voir § « Surface morte » plus bas). Le point 2 reste un trou de contrôle d'accès côté
> serveur, pas une capacité offerte au LLM.

---

## Côté Python — sidecar

**Répertoire :** [src/bin/calaos_mcp/](../src/bin/calaos_mcp/)
Le package est installé sous `$prefix/lib/calaos/calaos_mcp/` ; le wrapper shell
`$bindir/calaos_mcp` (généré depuis `calaos_mcp.in`) règle `PYTHONPATH` et lance
`python3 -m calaos_mcp`.

```
src/bin/calaos_mcp/
├── calaos_mcp.in              # wrapper shell généré par configure
├── pyproject.toml             # dépendances épinglées (installation reproductible)
└── python/calaos_mcp/
    ├── __main__.py            # pré-bind du socket Unix (0660) + uvicorn(fd=…)
    ├── server.py              # app FastMCP + 9 tools + lifespan
    ├── config.py              # lit mcp_token/mcp_service_token/tunables depuis le XML
    ├── client.py              # CalaosClient : WS login_service + reconnect
    ├── auth.py                # BearerAuthMiddleware (S7 + rate-limit/ban S11)
    ├── safety.py              # sanitisation anti-prompt-injection (S3)
    ├── models.py              # ⚠️ non importé par le reste du package
    └── tools/
        ├── _home.py           # helpers de traversée de get_home
        ├── io.py              # list_ios, find_io, get_io_state, set_io_state
        ├── rooms.py           # list_rooms, get_room
        ├── scenario.py        # list_scenarios, run_scenario
        └── audio.py           # audio_control
```

### __main__.py — socket Unix
uvicorn force `chmod 0666` sur le socket qu'il crée. Pour garantir `0660`
(mitigation S6), le sidecar **pré-bind** le socket lui-même (`umask 0o117` + `chmod 0660`,
`__main__.py:75-87`) et le passe à uvicorn via `fd=` (`:98-103`). Le socket résiduel est
délié avant le bind et après l'arrêt du serveur (`:70-73`, `:107-111`). `access_log` est
désactivé.

Le niveau de log Calaos **numérique 1–5** est traduit en niveau uvicorn
(`critical`, `error`, `warning`, `info`, `debug`) ; une valeur non numérique est passée telle
quelle en minuscules, ce qui permet de mettre directement un nom uvicorn dans la variable
(`:37-55`).

> ⚠️ L'échelle **n'est pas** celle de `calaos_extern_proc`, qui utilise **0–4** (voir
> [14_python_extern_proc.md](14_python_extern_proc.md)). Les deux lisent pourtant la même
> variable `CALAOS_LOG_LEVEL`.

### config.py — lecture des secrets et des réglages
Lit `mcp_token`, `mcp_service_token` et les trois réglages de throttle directement dans
`local_config.xml` (`$CALAOS_CONFIG_PATH`), **jamais** depuis l'environnement (S1). Un token
absent fait échouer le démarrage avec un message explicite (`config.py:72-75`).

> **Gotcha namespace XML :** les options sont des éléments
> `{http://www.calaos.fr}option`. `root.iter("option")` ne matche rien — il faut
> comparer le **nom local** (après le `}`), ce que fait `config.py:59-61`.

`get_config()` est mis en cache par `@lru_cache(maxsize=1)` (`:37`) : le sidecar ne relit
jamais `local_config.xml` de lui-même, un changement de réglage demande un **redémarrage**
(c'est ce que déclare `restartRequired()` sur ces options côté `ConfigOptions.cpp`).

### client.py — CalaosClient
WebSocket persistant vers `CALAOS_API_URL`. Premier message : `login_service` avec
`mcp_service_token` (`:84-93`). Corrélation requête/réponse par `msg_id` de la forme
`mcp-<n>` (`:114-116`), avec un **timeout de 10 s** par requête et une entrée de `_pending`
qui n'est jamais fuitée, même sur annulation (`:118-137`).

Le cache de `get_home` est invalidé sur les événements `io_added`, `io_deleted`,
`room_added`, `room_deleted` (`:108-110`).

Reconnexion : boucle de fond démarrée par `connect()`, **non bloquante** — un tool appelé
avant la première connexion lève `RuntimeError("Not connected to calaos_server yet…")`
(`:39-45`, `:118-123`). Le backoff double à chaque échec et **plafonne à 60 s** ; une
fermeture propre le remet à 1 s (`:47-60`). À chaque perte de connexion, toutes les requêtes
en vol sont résolues en `ConnectionError` plutôt que de laisser les appelants attendre leur
timeout (`_on_disconnect`, `:62-75`).

Formats à respecter (appris à l'exécution, et documentés dans le code) :
- `get_state` : requête `{"items": ["id1", "id2"]}` (tableau d'identifiants),
  réponse `{"id1": "valeur", ...}` (dictionnaire plat) — `client.py:144-147`.
- `get_home` : `home` est un **tableau** de pièces, les IOs sont sous `items` (pas `ios`),
  et **toutes** les valeurs scalaires sont des **chaînes** (`rw` = `"true"`/`"false"`). Le
  helper [tools/_home.py](../src/bin/calaos_mcp/python/calaos_mcp/tools/_home.py) centralise
  cette traversée (`iter_rooms`, `iter_ios`, `is_writable`) et son docstring `:3-24` porte la
  forme complète de la réponse.

### server.py — FastMCP
`FastMCP("calaos")` avec des `instructions` qui préviennent le modèle que noms
d'équipements, noms de pièces et valeurs d'état sont de l'**entrée utilisateur non fiable**
(`server.py:50-58`).

Le montage demande une acrobatie, documentée en tête de fichier (`server.py:8-16`) : le
`streamable_http_app()` de FastMCP renvoie une app Starlette dont le lifespan **ne se
déclenche pas** une fois montée dans FastAPI. Le sidecar extrait donc le handler ASGI brut et
fait tourner `mcp._session_manager.run()` dans son propre lifespan (`:163-184`). Deux
enregistrements sont nécessaires : une `Route` explicite sur `/mcp` **exactement** (les
clients Streamable HTTP postent sans slash final, et `Mount("/mcp")` ne matche que les
sous-chemins) et un `mount` pour les sous-chemins (`:202-213`). `redirect_slashes=False`
empêche un 307 vers `/mcp/` avant que le handler ne tourne (`:186-191`).

`/healthz` et `/mcp/healthz` restent **ouverts sans auth** (probe) — ils répondent
`{"status": "ok", "version": …}` (`:194-200`, `auth.py:132-134`). La protection
DNS-rebinding de FastMCP est désactivée car le seul point d'entrée est le proxy local
(`:59-66`).

### auth.py — Bearer, identité du client, throttle

`BearerAuthMiddleware` valide `Authorization: Bearer <mcp_token>` en temps constant
(`hmac.compare_digest`, S7, `auth.py:159-165`). Un échec renvoie `401` avec
`WWW-Authenticate: Bearer` (`:183-184`).

**Identité du client (T1.8).** Le proxy C++ transmet les octets du client tels quels, et
haproxy (`option forwardfor`) **ajoute une nouvelle ligne d'en-tête** `X-Forwarded-For`
**après** celles fournies par le client — il ne fusionne pas. Le sidecar prend donc la
**dernière entrée de la dernière ligne** : le saut de proxy de confiance. Tout ce qui
précède est fourni par le client et peut être tourné à chaque requête pour échapper au
throttle (intégral, `auth.py:65-77`) :

```python
def _source_ip(request: Request) -> str:
    # haproxy (`option forwardfor`) APPENDS A NEW X-Forwarded-For HEADER
    # LINE — it does not merge into a client-supplied header. headers.get()
    # returns the FIRST line, which is fully attacker-controlled, so take
    # the LAST header line (appended by the trusted haproxy hop), then the
    # LAST comma-entry of that line. All earlier lines/entries are supplied
    # by the client and can be rotated per request to evade throttling.
    lines = request.headers.getlist("x-forwarded-for")
    if lines:
        last = lines[-1].rsplit(",", 1)[-1].strip()
        if last:
            return last
    return request.client.host if request.client else "unknown"
```

Sans l'en-tête, **toutes les requêtes partagent un seul seau** — un accès direct au socket
Unix n'offre de toute façon aucune identité par client. C'est la même règle que celle
appliquée côté C++ par `TransportLimits::effectiveClientIp()` (`HttpClient.h:136-155`)
— ⚠️ mais **pour le seul plafond `max_connections_per_ip`**
(`HttpClient.cpp:200-203`). Ne pas généraliser : le **throttle de login** du serveur, lui,
n'emprunte pas ce chemin, voir la réserve du § Sécurité ci-dessous.

**Réglages du throttle** — lus dans `local_config.xml`, donc modifiables par
l'installateur. Ce ne sont **pas** des constantes (dérivé, `config.py:86-91`,
`ConfigOptions.cpp:884-918`) :

| Option | Défaut | Plage | Effet |
|---|---|---|---|
| `mcp_rate_limit` | `300` | 0 – 100000 | requêtes par minute et par IP ; **`0` = désactivé** |
| `mcp_ban_failures` | `20` | 0 – 10000 | échecs d'authentification consécutifs avant bannissement ; **`0` = pas de bannissement** |
| `mcp_ban_seconds` | `120` | 0 – 86400 | durée du bannissement |

⚠️ **`mcp_rate_limit=0` signifie désormais DÉSACTIVÉ.** Auparavant, la comparaison
`len(window) >= rate_limit` faisait que `0` **bloquait tout**. `config.py` normalise donc
maintenant toute valeur `<= 0` vers une sentinelle inatteignable (intégral,
`config.py:15-19`) :

```python
# Sentinel used when mcp_rate_limit<=0: the auth middleware compares
# `len(window) >= rate_limit`, so 0 would 429 every request instead of
# disabling the limit. A practically-unreachable ceiling disables it
# without requiring a change in auth.py (owned by another ticket).
RATE_LIMIT_DISABLED = 1_000_000_000
```

Les trois options sont lues par le **sidecar Python seul** ; `calaos_server` ne les consulte
jamais. Elles demandent un redémarrage pour être prises en compte (`@lru_cache` sur
`get_config`).

Mécanique : fenêtre glissante de **60 s** (`_WINDOW_SECONDS`), état borné à **4096** entrées
par dictionnaire (`MAX_TRACKED_IPS`) et purgé au plus toutes les **60 s** ou dès qu'un
dictionnaire dépasse le plafond (`auth.py:40-45`, `:80-117`). Un dépassement de fenêtre ou un
bannissement actif rendent `429`. Une authentification réussie remet le compteur d'échecs de
l'IP à zéro (`:167-169`).

⚠️ Le compteur de fenêtre est incrémenté **avant** la vérification du Bearer (`:150-156`) :
`mcp_rate_limit` plafonne donc **toutes les requêtes**, pas seulement les tentatives
d'authentification.

### Outils MCP (9)

| Tool | Rôle |
|---|---|
| `list_rooms` | pièces (`name`, `type`, `io_count`) |
| `get_room` | détail d'une pièce (nom insensible à la casse) + IOs et états |
| `list_ios` | IOs filtrés par pièce et/ou `gui_type` |
| `find_io` | recherche d'IO par sous-chaîne sur le nom ou l'id, **≤ 5 résultats** |
| `get_io_state` | état courant d'un IO |
| `set_io_state` | commande un IO inscriptible (validation S14, voir ci-dessous) |
| `list_scenarios` | IOs de `gui_type` `scenario` ou `auto_scenario` |
| `run_scenario` | déclenche un scénario |
| `audio_control` | `play`, `pause`, `stop`, `next`, `prev`, `volume+`, `volume-`, `volume_set` |

`run_scenario` **vérifie d'abord que l'id existe et est bien un scénario**, puis déclenche
par `set_state(<id>, "true")` — il n'utilise **pas** l'action `autoscenario`
(`tools/scenario.py:26-46`). `audio_control` refuse toute action hors de sa liste blanche
avant d'appeler le serveur (`tools/audio.py:5-23`).

#### Écriture d'un IO : `io_type`, pas `rw` (T1.16)

⚠️ **La condition d'inscriptibilité n'est pas le drapeau `rw`.** Se fier à `rw` seul marquait
**tous les outputs en lecture seule** ; la règle réelle est portée par `is_writable()`
(intégral, `tools/_home.py:46-53`) :

```python
def is_writable(io: dict) -> bool:
    """Return True if the IO can be driven via set_state.

    Writability is determined by io_type, not the rw flag: outputs and inouts
    (lights, shutters, relays...) are always writable. The rw param only exists
    on input value objects (IntValue) as an opt-in "edit mode" flag, so relying
    on it alone wrongly marked every output as read-only.
    """
```

Le champ `rw` renvoyé au LLM par `list_ios`, `find_io` et `get_room` est donc le **résultat
de `is_writable()`**, pas la valeur brute du serveur.

La validation `set_io_state` (S14, `tools/io.py:24-45`) dépend du `var_type` :

- `bool` — accepte `true/false/1/0/on/off` (insensible à la casse) et **normalise** vers
  `"true"`/`"false"` ; tout le reste est refusé.
- `float`, `int`, `double` — doivent parser en nombre **fini** : `inf`, `nan` et `1e999`
  (qui déborde en `inf`) sont refusés explicitement ; `int` exige en plus une valeur entière.
- tout autre type — ASCII imprimable et Latin-1 uniquement, **256 caractères maximum**.

#### Sanitisation des sorties (S3)

Toutes les valeurs renvoyées au LLM passent par `safety.py` : suppression des séquences ANSI
et des caractères de contrôle, troncature (256 caractères pour un nom, 1024 pour une valeur),
et encapsulation des valeurs non fiables dans `{"untrusted_text": …}` pour que le modèle les
traite comme des données et non des instructions (`safety.py:12-35`).

#### Surface morte, mesurée

Trois éléments du package sont **câblés mais jamais atteints**. Ils sont signalés ici pour
qu'on ne les prenne pas pour des capacités disponibles :

- **`CalaosClient.autoscenario()`** (`client.py:156-158`) : le passe-plat existe, **aucun des
  9 tools ne l'appelle**. Créer, modifier ou supprimer un scénario **n'est pas** une capacité
  offerte au LLM aujourd'hui.
- **`client.py:23-27` `_ALLOWED_ACTIONS`** : ce `frozenset` d'actions « permises sous la
  portée service » n'est **référencé nulle part** — il ne filtre rien. (Le `_ALLOWED_ACTIONS`
  de `tools/audio.py:5-8`, lui, est bien utilisé ; ce sont deux objets différents portant le
  même nom.) Le vrai cloisonnement est côté serveur, cf. § `serviceScope`.
- **`models.py`** : les modèles pydantic `IO`, `Room`, `Scenario`, `AudioPlayer` sont
  installés (`Makefile.am:23`) mais **importés par aucun module** du package.

> **Non encore implémenté** (cf. plan) : tool `camera_snapshot` (+ allowlist
> caméra `mcp_visible`, S12), Resources (`calaos://home/topology`,
> `calaos://state/current`), Prompts (`goodnight`, `leaving_home`,
> `home_status`), helper CLI `calaos-mcp-info`. Vérifié par recherche dans `src/` : aucun de
> ces symboles n'existe. En revanche les **tests existent désormais** — voir § Tests.

---

## Sécurité (mitigations implémentées)

| ID | Mesure | Site |
|---|---|---|
| S1 | Secrets uniquement dans `local_config.xml`, jamais en variable d'environnement ; rédaction des blocs de 64 hex dans les logs du sidecar | `config.py:47-75`, `McpServerManager.cpp:302-323` |
| S2 | Compte de service à portée restreinte (`login_service`/`serviceScope`) plutôt qu'admin | `JsonApiHandlerWS.cpp:530-567` |
| S3 | Sanitisation anti-prompt-injection des sorties LLM | `safety.py:12-35` |
| S5 | Regex stricte sur `/mcp/*` (anti path-traversal) | `McpProxyHandler.cpp:39-43` |
| S6 | Socket Unix `0660` (pré-bind + chmod, pas le `0666` d'uvicorn) | `__main__.py:75-87` |
| S7 | Comparaison du Bearer en temps constant | `auth.py:164` |
| S8 | Défenses anti HTTP request smuggling dans le proxy | `McpProxyHandler.cpp:61-107` |
| S11 | Rate-limit par IP (**300 req/min par défaut**, `mcp_rate_limit`) + ban (**20 échecs / 120 s par défaut**, `mcp_ban_failures` / `mcp_ban_seconds`), identité prise sur `X-Forwarded-For` | `auth.py:120-184`, `config.py:86-91` |
| S14 | Validation stricte des valeurs `set_io_state` selon `var_type` | `tools/io.py:24-45` |

S'y ajoute, côté serveur, le **throttle de login** appliqué à `login_service` comme au login
utilisateur : backoff par entrée de table doublant à chaque échec, de 1 s à 60 s, table bornée
à 1024 entrées et entrées oubliées après 900 s (`JsonApi.h:50-80`).

> ⚠️ **Ce throttle n'est pas réellement par adresse derrière haproxy — mesuré.** La clé de
> table vient de `clientIp()`, qui rend le **pair TCP** et non
> `TransportLimits::effectiveClientIp()` : `JsonApiHandlerWS.cpp:45-51` et
> `JsonApiHandlerHttp.cpp:55-61` appellent tous deux `HttpClient::getClientIp()`
> (`HttpClient.cpp:710-732`), qui lit `peer<uvw::IPv4>()`. **Les deux transports de login sont
> touchés.** Derrière haproxy, le pair est toujours le proxy : **tous les utilisateurs
> partagent donc un seul seau**, et un attaquant peut bloquer le login de tout le monde.
> `login_service` échappe à la conséquence — le sidecar se connecte en loopback, où le pair
> **est** la bonne identité — mais le login utilisateur, non. Consigné dans
> `docs/refactoring/FINDINGS.md` ; ticket correctif **T3.24**.

---

## Build

### Les dépendances Python — une seule source, jamais une liste à la main

[**`src/bin/calaos_mcp/pyproject.toml`**](../src/bin/calaos_mcp/pyproject.toml) est la **source
unique de vérité** (T3.23) : `mcp`, `fastapi`, `uvicorn`, `websockets`, `pydantic`, `starlette`,
toutes **épinglées**, avec `requires-python = ">=3.11"`. Les deux stages du `Dockerfile`, le
`.devcontainer/Dockerfile` et le job CI `mcp-sidecar-deps` installent **ce fichier-là**, via
[`scripts/pyproject-requirements.py`](../scripts/pyproject-requirements.py).

⚠️ **Ne réécrivez pas la liste des paquets à la main** — c'est précisément ce qui a cassé l'image
publiée : le `Dockerfile` faisait `pip install "mcp[cli]" uvicorn fastapi websockets` sans borne
de version, a résolu un jour vers `mcp 2.0.0`, où `mcp.server.fastmcp` **n'existe plus**, et le
sidecar publié ne démarrait pas. Toute liste recopiée diverge tôt ou tard du manifeste.

```bash
scripts/pyproject-requirements.py src/bin/calaos_mcp/pyproject.toml > req.txt
pip3 install -r req.txt --break-system-packages
./autogen.sh && ./configure && make && sudo make install
```

Installez le jeu en **une seule** invocation de `pip` : `mcp`, `fastapi` et `starlette` sont
**couplés** (`fastapi 0.115.12` exige `starlette>=0.40.0,<0.47.0`), et les résoudre paquet par
paquet choisit en silence des combinaisons incompatibles.

### La sonde `configure`

`configure` active `HAVE_PYTHON_MCP` si le sidecar peut réellement fonctionner, sinon il avertit
et ne l'installe pas. Désactivable explicitement avec `--without-mcp` (`configure.ac:179-185`).

La sonde **n'importe pas le paquet, elle exerce l'API** : `import mcp` réussit avec `mcp 2.0.0`
alors que le sidecar est mort. Elle reprend donc les symboles que les sources atteignent
réellement — dont **`websockets`** (`client.py:16`) et `starlette` — puis **rejoue la séquence**
de `server.py`, y compris les trois attributs qu'aucun `import` ne révélerait :

| ce qui est exercé | pourquoi |
|---|---|
| `from mcp.server.fastmcp import FastMCP` | `server.py:25` — le module disparu en `mcp 2.0.0` |
| `from mcp.server.transport_security import …` | `server.py:26` |
| `fastapi`, `fastapi.responses`, `starlette.middleware.base`, `starlette.routing` | `server.py`, `auth.py` |
| `import uvicorn, websockets` | `__main__.py:20`, `client.py:16` |
| `mcp.settings.transport_security = …` | `server.py:64`, au niveau module |
| `mcp.streamable_http_app()` | `server.py:167` |
| `mcp._session_manager` | `server.py:171` — API **privée**, rien en amont ne la promet |

`pydantic` n'est **pas** sondé : le seul module qui l'importe, `models.py`, n'est importé par
personne, et une `pydantic` manquante ferait de toute façon échouer l'import de `fastapi`.

En cas d'échec, `configure` **imprime la trace d'import** au lieu de dire seulement « no ».
Contrôle rapide sur une image construite, sans socket ni configuration :

```bash
calaos_mcp --help    # traverse toute la chaîne d'import ; sortie 0 = le sidecar peut démarrer
```

Fichiers de build : [src/bin/calaos_mcp/Makefile.am](../src/bin/calaos_mcp/Makefile.am)
(installe le wrapper `bin_SCRIPTS` + les `.py` via `_PYTHON`, sous `HAVE_PYTHON_MCP`),
ajout dans [configure.ac](../configure.ac) et [src/bin/Makefile.am](../src/bin/Makefile.am).

---

## Tests

Trois suites Python couvrent le sidecar et sont **câblées dans `make check`** depuis T2.14,
via [tests/run-python-tests.sh](../tests/run-python-tests.sh) : `test_auth.py` (Bearer,
rate-limit, ban, identité `X-Forwarded-For`), `test_t116_mcp_client.py` et
`test_t116_mcp_config_io.py`. Sans interpréteur utilisable, le script rend `77` et `make
check` reste vert ; sans `pytest`, il retombe sur `unittest` pour les suites `test_t116_*`.

Côté C++, `tests/core/JsonApiSession_test.cpp` couvre `login_service`, le refus de portée et
`get_mcp_info`, avec les fichiers de référence `e40e_ws_login_service_*.json`,
`e40e_ws_scope_denied.json` et `e40e_http_get_mcp_info.json`.

---

## Découverte du token & configuration client

L'action JsonApi `get_mcp_info` renvoie l'URL et le token Bearer à un utilisateur
**authentifié**. ⚠️ **Elle est HTTP uniquement** : envoyée en WebSocket, le message tombe en
fin de chaîne `if/else` et **rien n'est répondu** — épinglé par
`JsonApiSession_test.cpp:1036-1046`.

```bash
curl -s -X POST http://localhost:5454/api \
  -d '{"action":"get_mcp_info","cn_user":"user","cn_pass":"pass"}'
```

Réponse (capturé, `tests/core/golden/e40e_http_get_mcp_info.json`, intégral — le token y est
la valeur déterministe du test) :

```json
{
  "hint": "Use token as Bearer in Authorization header. Append /mcp to your Calaos HTTPS base URL.",
  "token": "e40e-bearer-token",
  "url_path": "/mcp"
}
```

Le champ `token` est le **`mcp_token`** (Bearer client). Le `mcp_service_token`, lui, n'est
**jamais** exposé par cette action — c'est explicitement asserté par le test
(`JsonApiSession_test.cpp:1029-1032`). Les paramètres passent aussi bien dans le corps JSON
qu'en paramètres GET `?action=get_mcp_info&cn_user=…&cn_pass=…`.

Configuration Claude Desktop (`claude_desktop_config.json`) :

```json
{
  "mcpServers": {
    "calaos": {
      "url": "https://calaos.local/mcp",
      "headers": { "Authorization": "Bearer <mcp_token>" }
    }
  }
}
```

---

## Test de bout en bout (dev)

```bash
# 1. Lancer le serveur avec une config de test
calaos_server --config /tmp/calaos-cfg --cache /tmp/calaos-cache -noudp

# 2. Vérifier dans les logs : tokens générés, "MCP sidecar authenticated
#    (service scope)", "Application startup complete".

# 3. Healthcheck via le proxy (sans auth)
curl http://127.0.0.1:5454/mcp/healthz        # {"status":"ok","version":"..."}

# 4. Client MCP : initialize + tools/list + tools/call (Bearer requis)
#    via le SDK Python `mcp.client.streamable_http`, ou MCP Inspector :
npx @modelcontextprotocol/inspector http://127.0.0.1:5454/mcp \
  --header "Authorization: Bearer <token>"
```

Modes de panne attendus : sidecar tué → respawn après 1 s au premier échec (puis 2, 5, 10,
30, 60 s si les échecs s'enchaînent) ; Bearer invalide ou absent → `401` ; dépassement du
rate-limit ou IP bannie → `429` ; chemin `/mcp` malformé ou en-têtes suspects → `400` ;
IO inexistant ou en lecture seule dans `set_io_state` → erreur structurée ; sidecar pas
encore connecté au serveur → erreur `Not connected to calaos_server yet` ; arrêt de
`calaos_server` → `SIGTERM` au sidecar, socket nettoyé.

> **Note environnement dev :** le port par défaut est **5454**
> (`DEFAULT_JSONAPI_PORT`, `McpServerManager.cpp:42`). Si ce port est déjà occupé, le serveur
> ne démarrera pas son écoute ; positionner l'option `port_api` pour en changer — le sidecar
> suit automatiquement, `CALAOS_API_URL` étant construit depuis cette même option
> (`McpServerManager.cpp:69-76,253`).
