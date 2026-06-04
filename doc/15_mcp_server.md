# Serveur MCP — calaos_mcp

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
               │ http://127.0.0.1:5454/mcp
               ▼
  calaos_server (daemon C++)
   ├── HttpServer (port 5454)
   │    └── WebSocket::ProcessData  ── sniff 1re ligne ──┐
   │         ├── /api        → JsonApiHandlerWS          │
   │         ├── /api/v3/... → RemoteUIWebSocketHandler  │
   │         └── /mcp/*      → McpProxyHandler ──────────┤ octets bruts
   │                                                     ▼
   │                                   /var/cache/calaos/mcp.sock (0660)
   │                                                     ▲ uvicorn (uds)
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
[main.cpp](../src/bin/calaos_server/main.cpp) juste après `HttpServer::Instance()`.
`stop()` est appelé à l'arrêt de la boucle libuv. Responsabilités :

1. **Génération des tokens** (`ensureTokens`) : si `mcp_token` /
   `mcp_service_token` sont absents de `local_config.xml`, les générer (64
   caractères hex = 256 bits, via `Utils::createRandomUuid()` × 2) et les
   persister avec `Utils::set_config_option`.
2. **Socket** : `$CALAOS_CACHE_PATH/mcp.sock`, `unlink` préalable si résiduel.
3. **Spawn** (`uvw::ProcessHandle`) de `$bindir/calaos_mcp` avec les variables
   d'environnement (voir plus bas). stdout/stderr du sidecar sont récupérés et
   réémis dans le log Calaos sous le domaine `mcp` (avec rédaction des blocs de
   64 hex au cas où, mitigation S1).
4. **Redémarrage automatique** : `on<uvw::ExitEvent>` → respawn avec backoff
   exponentiel (1, 2, 5, 10, 30, 60 s) sauf si arrêt propre en cours.
5. **Arrêt** : `SIGTERM` au sidecar puis `unlink` du socket.

Variables d'environnement passées au sidecar (**aucun secret** ne transite par
l'environnement — mitigation S1) :

| Variable | Valeur |
|---|---|
| `CALAOS_CONFIG_PATH` | répertoire de config **actif** (voir gotcha ci-dessous) |
| `CALAOS_CACHE_PATH` | `Utils::getCachePath()` |
| `CALAOS_MCP_SOCKET` | chemin du socket Unix |
| `CALAOS_API_URL` | `ws://127.0.0.1:<port_api>/api` |
| `CALAOS_LOG_LEVEL`, `CALAOS_LOG_DOMAINS` | niveau/domaines de log |

> **Gotcha config path :** `Utils::getConfigPath()` ignore `--config` (il
> re-dérive depuis `$HOME`). Pour passer le **vrai** répertoire de config au
> sidecar, `McpServerManager` utilise `Utils::getConfigFile("")` (qui respecte
> `_configBase`) puis retire le `/` final. Utiliser `getConfigPath()` ici
> ferait lire au sidecar le mauvais `local_config.xml`.

### McpProxyHandler
**Fichiers :** [McpProxyHandler.h](../src/bin/calaos_server/McpProxyHandler.h),
[McpProxyHandler.cpp](../src/bin/calaos_server/McpProxyHandler.cpp)

Reverse proxy octets-bruts entre une connexion TCP `/mcp/*` et le socket Unix du
sidecar. `WebSocket::ProcessData` appelle `McpProxyHandler::sniffRequest()` sur
les premiers octets ; selon le verdict :

- `Mcp` → bascule en mode proxy, tout est splicé en brut dans les deux sens
  (supporte les réponses SSE longues du Streamable HTTP).
- `InvalidPath` → 400 (regex stricte `^/mcp(/[A-Za-z0-9._~-]+)*/?$`, mitigation
  S5, anti path-traversal vers `/api`).
- `Smuggling` → 400 (double `Content-Length`, `Transfer-Encoding` + `CL`, ou
  `TE` ≠ chunked/identity ; mitigation S8).
- `NotMcp` → la connexion repart dans le flux JsonApi normal.

L'authentification Bearer **n'est pas** faite par le proxy : elle est déléguée
au sidecar Python, ce qui évite de dupliquer la logique.

### login_service / serviceScope
**Fichier :** [JsonApiHandlerWS.cpp](../src/bin/calaos_server/JsonApiHandlerWS.cpp)

`processLoginService()` authentifie le sidecar via `mcp_service_token` et marque
la session `serviceScope = true`. Dans cette portée, les actions de
configuration ou sensibles sont refusées (`scopeDenied`) : `set_param`,
`del_param`, `audio_db`, `set_timerange`, `eventlog`, `register_push`,
`settings`.

---

## Côté Python — sidecar

**Répertoire :** [src/bin/calaos_mcp/](../src/bin/calaos_mcp/)
Le package est installé sous `$prefix/lib/calaos/calaos_mcp/` ; le wrapper shell
`$bindir/calaos_mcp` (généré depuis `calaos_mcp.in`) règle `PYTHONPATH` et lance
`python3 -m calaos_mcp`.

```
src/bin/calaos_mcp/
├── calaos_mcp.in              # wrapper shell généré par configure
└── python/calaos_mcp/
    ├── __main__.py            # pré-bind du socket Unix (0660) + uvicorn(fd=…)
    ├── server.py              # app FastMCP + 9 tools + lifespan
    ├── config.py              # lit mcp_token/mcp_service_token depuis le XML
    ├── client.py              # CalaosClient : WS login_service + reconnect
    ├── auth.py                # BearerAuthMiddleware (S7 + rate-limit S11)
    ├── safety.py              # sanitisation anti-prompt-injection (S3)
    ├── models.py
    └── tools/
        ├── _home.py           # helpers de traversée de get_home
        ├── io.py              # list_ios, find_io, get_io_state, set_io_state
        ├── rooms.py           # list_rooms, get_room
        ├── scenario.py        # list_scenarios, run_scenario
        └── audio.py           # audio_control
```

### __main__.py — socket Unix
uvicorn force `chmod 0666` sur le socket qu'il crée. Pour garantir `0660`
(mitigation S6), le sidecar **pré-bind** le socket lui-même (`umask 0o117` +
`chmod 0660`) et le passe à uvicorn via `fd=`. Le niveau de log Calaos numérique
(1–5) est traduit en niveau uvicorn (`critical`…`debug`).

### config.py — lecture des secrets
Lit `mcp_token` et `mcp_service_token` directement dans `local_config.xml`
(`CALAOS_CONFIG_PATH`), jamais depuis l'environnement (S1).

> **Gotcha namespace XML :** les options sont des éléments
> `{http://www.calaos.fr}option`. `root.iter("option")` ne matche rien — il faut
> comparer le **nom local** (après le `}`).

### client.py — CalaosClient
WebSocket persistant vers `CALAOS_API_URL`. Premier message :
`login_service` avec `mcp_service_token`. Corrélation requête/réponse par
`msg_id`, cache de `get_home` invalidé sur les événements `io_added`/`io_deleted`/
`room_added`/`room_deleted`, reconnexion avec backoff.

Formats à respecter (appris à l'exécution) :
- `get_state` : requête `{"items": ["id1", "id2"]}` (tableau d'identifiants),
  réponse `{"id1": "valeur", ...}` (dictionnaire plat).
- `get_home` : `home` est un **tableau** de pièces, IOs sous `items` (pas `ios`),
  toutes les valeurs scalaires sont des **chaînes** (`rw` = `"true"`/`"false"`).
  Le helper [tools/_home.py](../src/bin/calaos_mcp/python/calaos_mcp/tools/_home.py)
  centralise cette traversée (`iter_rooms`, `iter_ios`, `is_writable`).

### server.py — FastMCP
`FastMCP("calaos")` monté sur `/mcp`. `BearerAuthMiddleware` valide
`Authorization: Bearer <mcp_token>` en temps constant (`hmac.compare_digest`,
S7) avec rate-limit/ban par IP (S11). `/healthz` et `/mcp/healthz` restent
ouverts (probe). La protection DNS-rebinding de FastMCP est désactivée car le
seul point d'entrée est le proxy local.

### Outils MCP (9)

| Tool | Rôle |
|---|---|
| `list_rooms` | pièces + nb d'IOs |
| `get_room` | détail d'une pièce + IOs et états |
| `list_ios` | IOs filtrés par pièce / `gui_type` |
| `find_io` | recherche floue d'IO (≤ 5 résultats) |
| `get_io_state` | état courant d'un IO |
| `set_io_state` | commande un IO inscriptible (valide `rw` + `var_type`, S14) |
| `list_scenarios` | scénarios disponibles |
| `run_scenario` | déclenche un scénario |
| `audio_control` | play/pause/next/prev/volume d'un lecteur audio |

Toutes les valeurs renvoyées au LLM passent par `safety.py` : suppression des
caractères de contrôle / séquences ANSI, et encapsulation des valeurs non
fiables dans `{"untrusted_text": …}` pour que le modèle les traite comme des
données et non des instructions (S3).

> **Non encore implémenté** (cf. plan) : tool `camera_snapshot` (+ allowlist
> caméra `mcp_visible`, S12), Resources (`calaos://home/topology`,
> `calaos://state/current`), Prompts (`goodnight`, `leaving_home`,
> `home_status`), helper CLI `calaos-mcp-info`, tests pytest.

---

## Sécurité (mitigations implémentées)

| ID | Mesure |
|---|---|
| S1 | Secrets uniquement dans `local_config.xml`, jamais en variable d'environnement |
| S2 | Compte de service à portée restreinte (`login_service`/`serviceScope`) plutôt qu'admin |
| S3 | Sanitisation anti-prompt-injection des sorties LLM |
| S5 | Regex stricte sur `/mcp/*` (anti path-traversal) |
| S6 | Socket Unix `0660` (pré-bind + chmod, pas le `0666` d'uvicorn) |
| S7 | Comparaison du Bearer en temps constant |
| S8 | Défenses anti HTTP request smuggling dans le proxy |
| S11 | Rate-limit par IP (30 req/min) + ban 10 min après 5 échecs d'auth |
| S14 | Validation stricte des valeurs `set_io_state` selon `var_type` |

---

## Build

`configure` sonde les dépendances Python du sidecar (`mcp`, `uvicorn`,
`fastapi`) et active `HAVE_PYTHON_MCP` si présentes (sinon avertit et n'installe
pas le sidecar). Désactivable explicitement avec `--without-mcp`.

```bash
pip3 install "mcp[cli]" uvicorn fastapi
./autogen.sh && ./configure && make && sudo make install
```

Fichiers de build : [src/bin/calaos_mcp/Makefile.am](../src/bin/calaos_mcp/Makefile.am)
(installe le wrapper `bin_SCRIPTS` + les `.py` via `_PYTHON`),
ajout dans [configure.ac](../configure.ac) et [src/bin/Makefile.am](../src/bin/Makefile.am).

---

## Découverte du token & configuration client

Récupérer l'URL et le token via l'action JsonApi `get_mcp_info` (session admin) :

```bash
curl -s -X POST http://localhost:5454/api \
  -d '{"action":"get_mcp_info","cn_user":"user","cn_pass":"pass"}'
# → {"url_path":"/mcp","token":"<hex>","hint":"…"}
```

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
curl http://127.0.0.1:5454/mcp/healthz        # {"status":"ok",...}

# 4. Client MCP : initialize + tools/list + tools/call (Bearer requis)
#    via le SDK Python `mcp.client.streamable_http`, ou MCP Inspector :
npx @modelcontextprotocol/inspector http://127.0.0.1:5454/mcp \
  --header "Authorization: Bearer <token>"
```

Modes de panne attendus : sidecar tué → respawn < 2 s ; Bearer invalide → 401 ;
trop d'appels → 429 ; IO inexistant dans `set_io_state` → erreur structurée ;
arrêt de `calaos_server` → `SIGTERM` au sidecar, socket nettoyé.

> **Note environnement dev :** le port par défaut est **5454**
> (`JSONAPI_PORT`). Si ce port est déjà occupé, le serveur ne démarrera pas son
> écoute ; positionner l'option `port_api` pour en changer.
