# Drivers Python — calaos_extern_proc

> **Provenance des exemples.** Chaque bloc est marqué `(dérivé, Fichier:L-L)` quand il est
> reconstruit depuis le code, ou `(intégral, Fichier:L-L)` quand il en est une copie
> byte-identique.

## Vue d'ensemble

Certains drivers ExternProc sont écrits en **Python 3** plutôt qu'en C++. Ils utilisent la
bibliothèque Python `calaos_extern_proc`, qui implémente **exactement le même protocole de
framing** que le côté C++ (voir [12_extern_proc.md](12_extern_proc.md)).

Drivers Python actuels :

| Driver | Script | Binaire installé (wrapper shell) | Dépendance Python |
|---|---|---|---|
| **Reolink** | `IO/Reolink/ExternProcReolink_main.py` | `calaos_reolink` | `reolink_aio` |
| **Roon** | `Audio/ExternProcRoon_main.py` | `calaos_roon` | `roonapi` |

Le côté C++ ne lance **pas** `python3` directement : `ReolinkCtrl` et `RoonCtrl` appellent
`startProcess()` sur un **wrapper shell généré par `configure`** depuis
`calaos_reolink.in` / `calaos_roon.in`. Ce wrapper positionne `PYTHONPATH` puis fait
`exec python3 $CALAOS_PREFIX/lib/calaos/<script>.py "$@"` (intégral,
`IO/Reolink/calaos_reolink.in:1-4`) :

```bash
#!/bin/bash
CALAOS_PREFIX=@prefix@
export PYTHONPATH=$CALAOS_PREFIX/lib/python:$PYTHONPATH
exec python3 $CALAOS_PREFIX/lib/calaos/ExternProcReolink_main.py "$@"
```

Les deux dépendances tierces sont sondées par `configure` (`configure.ac:194-213`) : si
`roonapi` ou `reolink_aio` manque, un avertissement est émis et le driver correspondant
n'est pas construit.

---

## Bibliothèque calaos_extern_proc

**Dossier :** [src/lib/calaos-python/](../src/lib/calaos-python/)

Package Python installable (`setup.py`, nom `calaos_extern_proc`).

| Module | Contenu |
|---|---|
| `extern_proc.py` | Classe `ExternProcClient` |
| `message.py` | Classes `ExternProcMessage`, `MessageType`, `MessageState` (framing) |
| `logger.py` | Système de log compatible Calaos |

### Installation

```bash
cd src/lib/calaos-python
pip install -e .
```

Dépendances déclarées (dérivé, `setup.py:7-9,21`) : `colorama>=0.4.4,<0.5`,
`python_requires=">=3.6"`. La **borne haute** sur colorama est volontaire ; ne pas la
supprimer en recopiant la version courte de cette ligne.

> ⚠️ Un import de `calaos_extern_proc` **appelle `configure_logger()` tout seul**
> (`__init__.py:17`). Le rappeler dans un `__main__` n'est pas une erreur — c'est ce que font
> les deux drivers de l'arbre — mais ce n'est pas ce qui *active* le log : c'est ce qui le
> **reconfigure** avec les variables d'environnement telles qu'elles sont à cet instant
> (`configure_logger()` vide les handlers existants, `logger.py:124`).

---

## ExternProcClient (Python)

**Fichier :** [src/lib/calaos-python/calaos_extern_proc/extern_proc.py](../src/lib/calaos-python/calaos_extern_proc/extern_proc.py)

Équivalent du `ExternProcClient` C++, même protocole sur socket Unix.

### Attributs

```python
# (dérivé, extern_proc.py:16-28)
self.sockfd       # -1 tant que connect_socket() n'a pas réussi, puis socket.socket
self.sockpath     # chemin du socket (argument --socket), "" par défaut
self.name         # namespace du processus (--namespace), "extern_process" par défaut
self.cachePath    # $CALAOS_CACHE_PATH, "." si la variable est absente
self.configPath   # $CALAOS_CONFIG_PATH, "." si la variable est absente
```

### Méthodes à surcharger

```python
# (dérivé, extern_proc.py:134-145)
def setup(self) -> bool:
    # Initialisation avant la boucle principale. La classe de base retourne True
    # sans rien faire : c'est à la sous-classe d'appeler self.parse_arguments()
    # puis self.connect_socket(). Retourner False pour quitter.

def read_timeout(self):
    # Appelé quand select() expire sans rien à lire

def message_received(self, msg: str):
    # Appelé avec le payload d'une trame reçue de calaos_server

def handle_fd_set(self, fd: int) -> bool:
    # Appelé quand un FD supplémentaire est actif ; False arrête la boucle
```

### Méthodes utilitaires

```python
# (dérivé, extern_proc.py:30-131)
self.parse_arguments()       # --socket (requis) et --namespace, via parse_known_args
self.connect_socket() -> bool
self.send_message(data: str) # envoie une trame (verrou + sendall, thread-safe)
self.run(timeout_ms: int)    # boucle principale (select)
self.stop()                  # arrête la boucle
self.append_fd(fd: int)      # surveille un FD supplémentaire
self.remove_fd(fd: int)
```

Trois points de qualité corrigés par T1.8 et épinglés par
[tests/python/test_extern_proc.py](../tests/python/test_extern_proc.py) :

- **`stop()` interrompt réellement `run()`** — il y a un drapeau `_running` testé avant et
  après le `select()` (`extern_proc.py:24,90-120`). Test :
  `test_stop_interrupts_run_loop`.
- **`send_message()` utilise `sendall()`**, pas `send()` : un `send()` partiel tronquait la
  trame sous charge (`extern_proc.py:125-131`). Test :
  `test_send_message_uses_sendall_full_frame`.
- **La longueur annoncée compte des octets UTF-8**, pas des caractères `str`
  (`message.py:18-20`). Test : `test_framing_counts_utf8_bytes_not_characters`.

### Pattern d'utilisation

```python
# (dérivé, ExternProcRoon_main.py:222-234 et ExternProcReolink_main.py:1679-1687)
import json
from calaos_extern_proc import ExternProcClient, configure_logger

class MonDriver(ExternProcClient):
    def setup(self):
        self.parse_arguments()
        if not self.connect_socket():
            return False
        # initialisation du driver...
        return True

    def message_received(self, msg):
        data = json.loads(msg)
        if data.get("action") == "commande":
            self.send_message(json.dumps({"status": "ok"}))

    def read_timeout(self):
        pass

if __name__ == "__main__":
    configure_logger()
    client = MonDriver()
    if client.setup():
        client.run(200)   # les deux drivers de l'arbre utilisent 200 ms
```

---

## ExternProcMessage (Python)

**Fichier :** [src/lib/calaos-python/calaos_extern_proc/message.py](../src/lib/calaos-python/calaos_extern_proc/message.py)

Implémente le framing binaire, **strictement identique au C++** :

```
[OPCODE: 1 octet = 0x21][LENGTH: 4 octets big-endian][PAYLOAD: LENGTH octets UTF-8]
```

En-tête de **5 octets** (`message.py:36`, commentaire du code :
`# Header is 5 bytes (1 byte type + 4 bytes size)`), longueur écrite octet par octet en
big-endian (`message.py:78-81`) et relue de même (`message.py:40-45`). C'est le même format
que `ExternProc.cpp:322-334` et `:398-413`.

> ⚠️ **Correction d'une fausseté de ce document.** Une note affirmait ici que « la taille du
> payload est encodée sur 4 octets côté Python contre 2 octets côté C++ » et invitait à
> « vérifier la compatibilité ». C'était faux dans les deux moitiés : **les deux côtés
> encodent la longueur sur 4 octets**, et l'aller-retour est épinglé par
> `tests/python/test_extern_proc.py::test_framing_roundtrip_over_socketpair`, qui fait
> circuler deux trames sur un `socketpair` et vérifie les payloads reçus.
> ⚠️ Ce test est un aller-retour **Python↔Python** (`test_extern_proc.py:60-78`) : il épingle
> le format que Python **émet et relit**, pas l'interopérabilité avec le C++. Celle-ci ne
> tient qu'à la lecture des deux implémentations, mises côte à côte ci-dessus — aucun test
> de l'arbre ne fait circuler une trame **entre** un `ExternProcServer` C++ et un client
> Python.

```python
# (dérivé, message.py:13-23,31-84 ; extern_proc.py:53-84)
msg = ExternProcMessage(json_string)
raw = msg.get_raw_data()   # bytes à envoyer

# Parsing d'une frame reçue :
msg = ExternProcMessage()
finished = msg.process_frame_data(buffer)
if finished and msg.isvalid:
    payload_str = msg.payload
```

⚠️ **`process_frame_data()` ne consomme pas le tampon de l'appelant.** Contrairement à la
version C++ (qui prend une `string &` et fait `erase()`), la version Python ne fait que
rebinder une variable locale. C'est l'appelant qui doit avancer, et
`process_socket_recv()` le fait en tranchant `5 + payload_length` octets
(`extern_proc.py:73-75`).

### Différence de robustesse avec le C++, mesurée et non corrigée

Le C++ **plafonne** la longueur annoncée à 4 MiB et signale une erreur de framing au-delà
(`ExternProc.h:53-57`, `ExternProc.cpp:342-357`). **La version Python n'a aucune borne** :
`message.py:40-45` accepte n'importe quelle longueur sur 32 bits, donc jusqu'à 4 GiB de
bufférisation dans `recv_buffer`. Le pair étant `calaos_server` lui-même sur un socket Unix
local, l'exposition est faible — mais l'asymétrie est réelle et consignée dans
`docs/refactoring/FINDINGS.md`.

De même, `run()` ne se reconnecte pas : un `select()` sur un fd fermé lève, l'exception est
logguée et la boucle s'arrête (`extern_proc.py:115-117`). C'est le côté C++ qui relance le
processus (voir [12_extern_proc.md](12_extern_proc.md), § « Gestion des erreurs et
redémarrage »).

---

## Système de log Python

**Fichier :** [src/lib/calaos-python/calaos_extern_proc/logger.py](../src/lib/calaos-python/calaos_extern_proc/logger.py)

### Configuration

Variables d'environnement, injectées par `ExternProcServer::startProcess()`
(`ExternProc.cpp:260-265`) :

| Variable | Valeur attendue | Défaut si absente/illisible |
|---|---|---|
| `CALAOS_LOG_LEVEL` | entier **0 à 4** | `4` (tout, jusqu'au debug) |
| `CALAOS_LOG_DOMAINS` | `"dom:level,dom2:level"` | aucun override de domaine |
| `CALAOS_FORCE_COLOR` | exactement `"1"` | couleurs seulement si `stdout` est un tty |

Les niveaux Calaos sont traduits en niveaux `logging` Python (intégral, `logger.py:57-63`) :

```python
_CALAOS_TO_PYTHON_LEVEL = {
    0: logging.CRITICAL,
    1: logging.ERROR,
    2: logging.WARNING,
    3: logging.INFO,
    4: logging.DEBUG,
}
```

La valeur est **bornée à `[0, 4]`** avant traduction (`logger.py:66`). Chaque entrée de
`CALAOS_LOG_DOMAINS` dont le niveau n'est pas un entier est **ignorée en silence**
(`logger.py:111-119`), et le filtrage par domaine est appliqué sur le **handler**
(`_DomainLevelFilter`, `logger.py:84-90`).

> ⚠️ La convention numérique n'est **pas** la même partout : le sidecar MCP traduit
> `CALAOS_LOG_LEVEL` sur l'échelle **1–5** vers les noms uvicorn
> (`calaos_mcp/config.py:93-98`), alors que `calaos_extern_proc` utilise **0–4**. Voir
> [15_mcp_server.md](15_mcp_server.md).

### Utilisation

```python
# (dérivé, logger.py:138-143, ExternProcRoon_main.py:3,90,133)
from calaos_extern_proc import configure_logger, cDebugDom, cInfoDom, cWarningDom, cErrorDom, cCriticalDom

cDebugDom("reolink")("connexion à la caméra %s", hostname)
cInfoDom("roon")("Using Roon server: %s:%s", host, port)
cErrorDom("mydriver")("Erreur critique: %s", str(e))
```

`cXxxDom(domaine)` renvoie la méthode `logging` correspondante du logger `CALAOS.<domaine>` :
les arguments suivants sont donc ceux de `logging` (formatage `%s` paresseux, ou f-string
déjà résolue — les deux styles coexistent dans l'arbre).

Format de sortie — `[NIVEAU] <domaine> (<fichier>:<ligne>) <message>`, avec couleur si le flux
est un terminal (dérivé, `logger.py:39-52`) :

```
[DBG] reolink (ExternProcReolink_main.py:142) connexion à la caméra 192.168.1.50
[INF] roon (ExternProcRoon_main.py:90) Using Roon server: 192.168.1.10:9330
```

Le fichier et la ligne sont ceux de l'**appelant** (`record.pathname`/`record.lineno`), pas
de `logger.py`. Les cinq marqueurs sont `[DBG] [INF] [WRN] [ERR] [CRI]`, et `[???]` pour un
niveau inconnu (`logger.py:39-45`).

---

## Driver Reolink (Python)

**Fichier :** [src/bin/calaos_server/IO/Reolink/ExternProcReolink_main.py](../src/bin/calaos_server/IO/Reolink/ExternProcReolink_main.py)

Utilise la bibliothèque `reolink_aio` (async) pour se connecter aux caméras Reolink via leur
API propriétaire (protocole Baichuan TCP).

### Architecture interne

- Boucle `ExternProcClient.run(200)` dans le thread principal (communication IPC)
- Event loop `asyncio` dans un **thread séparé** pour les opérations async Reolink
- `concurrent.futures.ThreadPoolExecutor` pour les appels bloquants

### Messages reçus de calaos_server

Deux actions, et deux seulement — tout autre `action` est logguée en warning et ignorée
(dérivé, `ExternProcReolink_main.py:1615-1619,1649,1676-1677`) :

```json
{"action": "register", "hostname": "192.168.1.50", "username": "admin", "password": "secret", "event_type": "motion"}
{"action": "health_check"}
```

`event_type` vaut `"motion"` par défaut ; `hostname`, `username` et `password` sont
obligatoires (le message est refusé sinon, `:1621-1628`). Le log de réception **rédige**
`username` et `password` avant d'imprimer le message (`:1606-1610`).

> ⚠️ Il n'existe **pas** d'action `unregister` : quand le côté C++ retire une caméra, le
> processus Python continue de la surveiller et les événements sont jetés côté serveur.
> Consigné dans `FINDINGS.md`.

### Messages envoyés à calaos_server

Aucun de ces messages ne porte de champ `action` ; ils se distinguent par la présence de
`event` ou de `status`. **Extraits** — les payloads réels portent davantage de clés :

```json
{"event": "detection", "hostname": "...", "event_type": "motion", "channel": 0, "timestamp": "...", "camera_name": "...", "tcp_push_active": true, "callback_duration": 0.4, "async_callback": true, "adaptive_timeout": 1.5}
```
(dérivé, `:1293-1304` ; variante « sonnette » avec `"doorbell_optimized": true` à `:1190-1202`)

```json
{"status": "connected", "message": "Reolink client ready"}
```
(intégral quant aux clés, `:799-803`)

```json
{"status": "reconnect_failed", "hostname": "...", "message": "Failed to reconnect after N attempts", "circuit_breaker_state": "open", "next_retry_in": 0, "failure_count": 3}
```
(dérivé, `:1068-1076`)

```json
{"status": "critical_error", "message": "System deadlock detected, restart required", "restart_count": 1, "timestamp": 0}
```
(dérivé, `:692-698`)

La réponse à `health_check` est un objet `{"status": "healthy"|"unhealthy", ...}` d'une
vingtaine de clés de diagnostic (`:1649-1675`) ; `healthy` signifie simplement que le dernier
health check remonte à moins de 60 s.

### Événements détectés

`visitor` (sonnette, traité en priorité sur les modèles doorbell), `motion`, puis les types
IA `face`, `person`, `vehicle`, `pet`, `package`, `cry`
(dérivé, `:1180,1225,1236,1268`).

### Circuit breaker

Chaque caméra a un `CircuitBreaker` qui limite les tentatives de reconnexion après des échecs
répétés. ⚠️ **Les valeurs effectives ne sont pas les défauts de la classe** : la classe
déclare `failure_threshold=5, recovery_timeout=300, half_open_max_calls=3` (`:23`), mais
l'instanciation réelle passe (dérivé, `:960-963`) :

- `failure_threshold = 3` → passe en état `open`
- `recovery_timeout = 600` s → tente à nouveau après 10 min
- `half_open_max_calls = 2` → test progressif

### Stratégie de reconnexion adaptative

Le type d'erreur est classé depuis l'`errno` ou le type d'exception (`:426-444`), puis mappé
sur un délai et un nombre de tentatives. **Sept** entrées, pas quatre (intégral,
`:449-455`) :

```python
            "network_unreachable": {"delay": 60, "max_retries": 3},  # Network problem, wait longer
            "connection_refused": {"delay": 30, "max_retries": 5},  # Service down, moderate retry
            "connection_timeout": {"delay": 20, "max_retries": 4},  # Network latency
            "connection_reset": {"delay": 10, "max_retries": 6},    # Temporary problem
            "timeout": {"delay": 15, "max_retries": 4},             # General timeout
            "invalid_credentials": {"delay": 300, "max_retries": 1}, # Credentials, wait long
            "unknown": {"delay": 30, "max_retries": 3}              # Default
```

---

## Driver Roon (Python)

**Fichier :** [src/bin/calaos_server/Audio/ExternProcRoon_main.py](../src/bin/calaos_server/Audio/ExternProcRoon_main.py)

Utilise la bibliothèque `roonapi` pour contrôler le logiciel Roon.

### Arguments CLI

`RoonClient` **remplace** `parse_arguments()` de la classe de base (dérivé, `:35-65`) : il
n'utilise pas `parse_known_args`, donc un argument inconnu fait échouer le démarrage.

```
--socket     chemin du socket IPC   (doit accompagner --namespace)
--namespace  namespace du processus (doit accompagner --socket)
--host       IP du core Roon        (facultatif : découverte automatique sinon)
--port       port du core           (défaut 9330)
--list       liste les zones puis quitte, sans connexion IPC
```

`--list` est **exclusif** de `--socket`/`--namespace`, et ces deux-là vont par paire :
les combinaisons invalides sont refusées par `parser.error()` (`:47-57`). En mode `--list`,
`main` sort avec le code 0 juste après `setup()` (`:228-229`).

### Appinfo Roon

```python
# (intégral, ExternProcRoon_main.py:22-28)
        self.appinfo = {
            "extension_id": "calaos_roon_extension",
            "display_name": "Calaos Roon Extension",
            "display_version": "1.0.0",
            "publisher": "Calaos",
            "email": "team@calaos.fr",
        }
```

### Autorisation et cache

Au premier démarrage, l'extension doit être **autorisée depuis l'interface Roon** : le driver
boucle en attendant un token (`:106-121`). Le `core_id` et le token obtenus sont écrits dans
`$CALAOS_CACHE_PATH/roon_core_id` et `$CALAOS_CACHE_PATH/roon_token` (`:123-127`) et relus aux
démarrages suivants (`:93-97`).

### Protocole IPC

**Serveur → sous-processus** (dérivé, `:170-209`) :

```json
{"action": "subscribe", "zone_id": "<id>"}
{"action": "play",   "zone_id": "<id>"}
{"action": "pause",  "zone_id": "<id>"}
{"action": "stop",   "zone_id": "<id>"}
{"action": "next",   "zone_id": "<id>"}
{"action": "previous", "zone_id": "<id>"}
{"action": "play_pause", "zone_id": "<id>"}
{"action": "set_volume", "zone_id": "<id>", "volume": 42}
```

Un `zone_id` inconnu est refusé avant l'indexation de `self.roon_api.zones` : une zone
inexistante levait auparavant et **tuait la boucle du sous-processus** (`:196-198`, T1.16).
`set_volume` s'applique au **premier output** de la zone (`:200-203`).

**Sous-processus → serveur** : l'objet **zone Roon brut** tel que le renvoie `roonapi`,
augmenté d'une clé `cover_url` quand une pochette est disponible (`:211-220`). Il est envoyé
à l'abonnement (`:176-177`) puis à chaque changement d'état de la zone (`:141-158`). Un échec
de résolution de la pochette est logué en warning et n'interrompt plus le callback d'état
(`:216-218`, T1.16).

⚠️ Deux réserves mesurées, consignées dans `FINDINGS.md` : `subscribe` peut ajouter **deux
fois** la même zone dans `subscribed_zones` (`:173`, aucune déduplication), et `"next"`
apparaît **deux fois** dans l'alternation d'actions de `:183` (bénin).

---

## Variables d'environnement communes

| Variable | Description |
|---|---|
| `CALAOS_CACHE_PATH` | Dossier cache Calaos (`self.cachePath`, défaut `"."`) |
| `CALAOS_CONFIG_PATH` | Dossier config Calaos (`self.configPath`, défaut `"."`) |
| `CALAOS_LOG_LEVEL` | Niveau de log global, **0 à 4** |
| `CALAOS_LOG_DOMAINS` | Niveaux par domaine (`"dom:level,dom2:level"`) |
| `CALAOS_FORCE_COLOR` | `"1"` force les couleurs ANSI |

Ces variables sont injectées par `calaos_server` au lancement du sous-processus via
`ExternProcServer::startProcess()` (`ExternProc.cpp:260-273`). Elles **remplacent**
l'environnement du sous-processus : seuls `PATH`, `HOME`, `LANG`, `LC_ALL`, `LANGUAGE`,
`LD_LIBRARY_PATH` et `PWD` sont hérités en plus.

---

## Tests

Les suites Python sont **câblées dans `make check`** depuis T2.14, via
[tests/run-python-tests.sh](../tests/run-python-tests.sh) (`tests/Makefile.am:26-35`). Le
script suit le protocole simple d'automake : `0` = PASS, `77` = SKIP (pas d'interpréteur
utilisable — `make check` reste vert), `1` = FAIL. Sans `pytest`, il retombe sur
`python3 -m unittest discover` pour les fichiers `test_t116_*.py`, qui ne demandent que la
bibliothèque standard, et annonce les suites sautées.

`tests/python/conftest.py` rend les deux packages de l'arbre importables **sans
installation** (`src/lib/calaos-python` et `src/bin/calaos_mcp/python`).

| Suite | Périmètre |
|---|---|
| `test_extern_proc.py` | framing, `sendall`, `stop()` (T1.8) |
| `test_logger.py` | niveaux, domaines, format (T1.8) |
| `test_auth.py` | Bearer, rate-limit, ban, identité `X-Forwarded-For` (T1.8) |
| `test_t116_mcp_client.py`, `test_t116_mcp_config_io.py`, `test_t116_roon.py` | robustesse client MCP et Roon (T1.16) |

---

## Créer un nouveau driver Python

1. Créer `MonDriver_main.py` en héritant de `ExternProcClient`
2. Implémenter `setup()` — qui **doit** appeler `parse_arguments()` puis `connect_socket()` —
   ainsi que `message_received()` et, si utile, `read_timeout()`
3. Appeler `configure_logger()` et `client.run()` dans `__main__`
4. Déclarer le script dans le `Makefile.am` comme fichier Python installé, et ajouter un
   wrapper `calaos_mondriver.in` (modèle : `Audio/calaos_roon.in`) déclaré dans
   `AC_CONFIG_FILES`
5. Côté C++ : créer un `MonDriverCtrl` qui lance **le wrapper**, pas `python3` :
   `ExternProcServer::startProcess(Prefix::Instance().binDirectoryGet() + "/calaos_mondriver", "mondriver", args)`
6. Sonder la dépendance Python tierce dans `configure.ac`, sur le modèle de `roonapi` /
   `reolink_aio` (`configure.ac:194-213`)
