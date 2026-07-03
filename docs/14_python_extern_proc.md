# Drivers Python — calaos_extern_proc

## Vue d'ensemble

Certains drivers ExternProc sont écrits en **Python 3** plutôt qu'en C++. Ils utilisent la bibliothèque Python `calaos_extern_proc` qui implémente le même protocole de framing que le côté C++ (`ExternProcClient` / `ExternProcServer`).

Drivers Python actuels :

| Driver | Fichier | Dépendance Python |
|---|---|---|
| **Reolink** | `IO/Reolink/ExternProcReolink_main.py` | `reolink_aio` |
| **Roon** | `Audio/ExternProcRoon_main.py` | `roonapi` |

---

## Bibliothèque calaos_extern_proc

**Dossier :** [src/lib/calaos-python/](../src/lib/calaos-python/)

Package Python installable (`setup.py`, nom `calaos_extern_proc`). Il fournit les mêmes primitives que `ExternProcClient` C++ :

| Module | Contenu |
|---|---|
| `extern_proc.py` | Classe `ExternProcClient` |
| `message.py` | Classe `ExternProcMessage` (framing) |
| `logger.py` | Système de log compatible Calaos |

### Installation

```bash
cd src/lib/calaos-python
pip install -e .
# ou
pip install calaos_extern_proc
```

Dépendances : `colorama>=0.4.4`, Python >= 3.6

---

## ExternProcClient (Python)

**Fichier :** [src/lib/calaos-python/calaos_extern_proc/extern_proc.py](../src/lib/calaos-python/calaos_extern_proc/extern_proc.py)

Équivalent exact du `ExternProcClient` C++. Même protocole de socket Unix.

### Attributs

```python
self.sockfd       # socket Unix connecté au serveur
self.sockpath     # chemin du socket (argument --socket)
self.name         # namespace du processus (argument --namespace)
self.cachePath    # $CALAOS_CACHE_PATH (env)
self.configPath   # $CALAOS_CONFIG_PATH (env)
```

### Méthodes à surcharger

```python
def setup(self) -> bool:
    # Initialisation avant la boucle principale
    # Doit appeler self.parse_arguments() et self.connect_socket()
    # Retourne False pour quitter

def read_timeout(self):
    # Appelé à chaque timeout de la boucle select()

def message_received(self, msg: str):
    # Appelé quand un message JSON est reçu de calaos_server

def handle_fd_set(self, fd: int) -> bool:
    # Appelé quand un FD supplémentaire est actif
    # Retourne False pour arrêter la boucle
```

### Méthodes utilitaires

```python
self.parse_arguments()       # parse --socket et --namespace
self.connect_socket() -> bool
self.send_message(data: str) # envoie un JSON au serveur (thread-safe)
self.run(timeout_ms: int)    # boucle principale (select)
self.stop()                  # arrêt de la boucle
self.append_fd(fd: int)      # surveille un FD supplémentaire
self.remove_fd(fd: int)
```

### Pattern d'utilisation

```python
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
        if data["action"] == "commande":
            # traitement...
            self.send_message(json.dumps({"status": "ok"}))

    def read_timeout(self):
        # vérifications périodiques...
        pass

if __name__ == "__main__":
    configure_logger()
    client = MonDriver()
    if client.setup():
        client.run(5000)  # timeout 5s
```

---

## ExternProcMessage (Python)

**Fichier :** [src/lib/calaos-python/calaos_extern_proc/message.py](../src/lib/calaos-python/calaos_extern_proc/message.py)

Implémente le framing binaire compatible avec le C++.

> **Note :** La taille du payload est encodée sur **4 octets** big-endian côté Python, contre **2 octets** côté C++. Vérifier la compatibilité si le protocole évolue.

```python
# Frame émise par le Python :
# [TYPE: 1 octet][SIZE: 4 octets big-endian][PAYLOAD: SIZE octets]

msg = ExternProcMessage(json_string)
raw = msg.get_raw_data()   # bytes à envoyer

# Parsing d'une frame reçue :
msg = ExternProcMessage()
finished = msg.process_frame_data(bytearray_buffer)
if finished and msg.isvalid:
    payload_str = msg.payload
```

---

## Système de log Python

**Fichier :** [src/lib/calaos-python/calaos_extern_proc/logger.py](../src/lib/calaos-python/calaos_extern_proc/logger.py)

Reprend les conventions de log Calaos (domaines, niveaux, couleurs).

### Configuration

Variables d'environnement :
- `CALAOS_LOG_LEVEL` — niveau global (entier)
- `CALAOS_LOG_DOMAINS` — niveaux par domaine : `"mqtt:2,reolink:4"`
- `CALAOS_FORCE_COLOR` — forcer les couleurs ANSI (`"1"`)

### Utilisation

```python
from calaos_extern_proc import configure_logger, cDebugDom, cInfoDom, cWarningDom, cErrorDom, cCriticalDom

configure_logger()  # à appeler une fois au démarrage

cDebugDom("reolink")("connexion à la caméra %s", hostname)
cInfoDom("roon")("Zone trouvée: %s", zone_name)
cErrorDom("mydriver")("Erreur critique: %s", str(e))
```

Format de sortie (couleur si terminal) :
```
[DBG] reolink (ExternProcReolink_main.py:142) connexion à la caméra 192.168.1.50
[INF] roon (ExternProcRoon_main.py:87) Zone trouvée: Salon
```

---

## Driver Reolink (Python)

**Fichier :** [src/bin/calaos_server/IO/Reolink/ExternProcReolink_main.py](../src/bin/calaos_server/IO/Reolink/ExternProcReolink_main.py)

Utilise la bibliothèque `reolink_aio` (async) pour se connecter aux caméras Reolink via leur API propriétaire (protocole Baichuan TCP).

### Architecture interne

- Boucle principale `ExternProcClient.run()` dans le thread principal (communication IPC)
- Event loop `asyncio` dans un **thread séparé** pour les opérations async Reolink
- `concurrent.futures.ThreadPoolExecutor` pour les appels bloquants

### Messages reçus de calaos_server

```json
{"action": "register", "hostname": "192.168.1.50", "username": "admin", "password": "secret", "event_type": "motion"}
{"action": "health_check"}
```

### Messages envoyés à calaos_server

```json
{"event": "detection", "hostname": "192.168.1.50", "event_type": "motion", "channel": 0, "timestamp": "..."}
{"event": "detection", "hostname": "...", "event_type": "person"}
{"status": "connected", "message": "Reolink client ready"}
{"status": "reconnect_failed", "hostname": "...", "circuit_breaker_state": "open"}
{"status": "critical_error", "message": "System deadlock detected, restart required"}
```

### Événements détectés

`motion`, `person`, `face`, `vehicle`, `pet`, `package`, `cry`, `visitor` (sonnette)

### Circuit breaker

Chaque caméra a un `CircuitBreaker` qui limite les tentatives de reconnexion après des échecs répétés :
- `failure_threshold = 3` → passe en état `open`
- `recovery_timeout = 600s` → tente à nouveau après 10 min
- `half_open_max_calls = 2` → test progressif

### Stratégie de reconnexion adaptative

```python
strategies = {
    "network_unreachable": {"delay": 60, "max_retries": 3},
    "connection_refused":  {"delay": 30, "max_retries": 5},
    "connection_timeout":  {"delay": 20, "max_retries": 4},
    "invalid_credentials": {"delay": 300, "max_retries": 1},
}
```

---

## Driver Roon (Python)

**Fichier :** [src/bin/calaos_server/Audio/ExternProcRoon_main.py](../src/bin/calaos_server/Audio/ExternProcRoon_main.py)

Utilise la bibliothèque `roonapi` pour contrôler le logiciel Roon.

### Arguments CLI spécifiques

```
--host      IP du core Roon
--port      Port (défaut 9330)
--list      Mode liste des zones (sans connexion IPC)
```

### Appinfo Roon

```python
appinfo = {
    "extension_id": "calaos_roon_extension",
    "display_name": "Calaos Roon Extension",
    "display_version": "1.0.0",
    "publisher": "Calaos",
    "email": "team@calaos.fr",
}
```

---

## Variables d'environnement communes

| Variable | Description |
|---|---|
| `CALAOS_CACHE_PATH` | Chemin vers le dossier cache Calaos |
| `CALAOS_CONFIG_PATH` | Chemin vers le dossier config Calaos |
| `CALAOS_LOG_LEVEL` | Niveau de log global |
| `CALAOS_LOG_DOMAINS` | Niveaux par domaine (`"dom:level,dom2:level"`) |
| `CALAOS_FORCE_COLOR` | Forcer les couleurs dans les logs |

Ces variables sont injectées par `calaos_server` au lancement du sous-processus via `ExternProcServer::startProcess()`.

---

## Créer un nouveau driver Python

1. Créer `MonDriver_main.py` en héritant de `ExternProcClient`
2. Implémenter `setup()`, `message_received()`, `read_timeout()`
3. Appeler `configure_logger()` et `client.run()` dans `__main__`
4. Déclarer le script dans le `Makefile.am` comme fichier Python installé
5. Côté C++ : créer un `MonDriverCtrl` qui lance le script via `ExternProcServer::startProcess("python3", "...", "/path/to/MonDriver_main.py --...")`
