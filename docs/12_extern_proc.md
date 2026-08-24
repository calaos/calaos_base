# ExternProc — Framework de sous-processus IPC

> **Provenance des exemples.** Chaque bloc de code de ce document est marqué
> `(dérivé, Fichier:L-L)` quand il est reconstruit à partir du code, ou
> `(intégral, Fichier:L-L)` quand il en est une copie byte-identique. Les
> affirmations de comportement renvoient au site qui les porte.

## Vue d'ensemble

`ExternProc` est un framework léger d'IPC (Inter-Process Communication) qui permet à
`calaos_server` de déléguer certains drivers à des sous-processus isolés. Le serveur pilote
un sous-processus via `ExternProcServer` ; le sous-processus se connecte en retour avec
`ExternProcClient`.

Les binaires qui embarquent `EXTERN_PROC_CLIENT_MAIN()` — c'est-à-dire les vrais
sous-processus — sont **au nombre de six** (`grep -l EXTERN_PROC_CLIENT_MAIN src/`) :

| Driver | Binaire installé | Fichier `main()` |
|---|---|---|
| **MQTT** | `calaos_mqtt` | `IO/Mqtt/MqttExternProc_main.cpp` |
| **KNX** | `calaos_knx` | `IO/KNX/KNXExternProc_main.cpp` |
| **Wago** | `calaos_wago` | `IO/Wago/WagoExternProc_main.cpp` |
| **OneWire** | `calaos_1wire` | `IO/OneWire/OWExternProc_main.cpp` |
| **OLA** (DMX) | `calaos_ola` | `IO/OLA/OLAExternProc_main.cpp` |
| **Lua scripts** | `calaos_script` | `LuaScript/ScriptExtern_main.cpp` |

Deux autres sous-processus parlent le **même protocole** mais sont écrits en **Python** ;
le binaire installé est alors un **wrapper shell généré par `configure`**, pas un exécutable
C++ :

| Driver | Binaire installé | Script lancé |
|---|---|---|
| **Roon** | `calaos_roon` (`Audio/calaos_roon.in`) | `Audio/ExternProcRoon_main.py` |
| **Reolink** | `calaos_reolink` (`IO/Reolink/calaos_reolink.in`) | `IO/Reolink/ExternProcReolink_main.py` |

> ⚠️ **Il n'existe pas de sous-processus Reolink en C++.** `IO/Reolink/ReolinkCtrl.cpp` est
> le contrôleur **côté serveur** (il instancie `ExternProcServer`), au même titre que
> `MqttCtrl.cpp` ou `OWCtrl.cpp` — pas un `main()` de sous-processus. Voir
> [14_python_extern_proc.md](14_python_extern_proc.md).

Les huit contrôleurs côté serveur qui appellent `startProcess()` sont `WagoMap`, `MqttCtrl`,
`ReolinkCtrl`, `KNXCtrl` (**deux** processus : commande et moniteur de bus), `OLACtrl`,
`OwCtrl`, `ScriptExec` et `RoonCtrl`.

---

## Protocole de framing

**Fichier :** [src/bin/calaos_server/IO/ExternProc.h](../src/bin/calaos_server/IO/ExternProc.h)

Messages framés sur un socket Unix. Le contrat est écrit dans l'en-tête
(intégral, `ExternProc.h:37-44`) :

```
 * Small framing for messages
 * +--------+----------------------+------------+
 * | OPCODE | LENGTH               | DATA ..... |
 * | 1 byte | 4 bytes (big endian) |            |
 * +--------+----------------------+------------+
 *
 * opcode: TypeMessage (0x21), any other value invalidates the frame
 * length: payload size in bytes, big endian, capped at MaxPayloadLength
```

- **En-tête : 5 octets exactement** — `1` octet d'opcode + `4` octets de longueur
  big-endian (`ExternProc.cpp:322-334` en lecture, `:398-413` en écriture).
- **Il n'y a pas d'octet de début de trame.** L'opcode est le premier octet du flux.
- `OPCODE` : `TypeMessage = 0x21`. `TypeUnkown = 0x00` existe dans l'enum
  (`ExternProc.h:71-75`) mais n'est jamais émis : c'est la valeur de repos après `clear()`.
- `LENGTH` : taille du payload **en octets**, big-endian, plafonnée à
  `MaxPayloadLength = 4 * 1024 * 1024` (4 MiB, `ExternProc.h:57`).
- `PAYLOAD` : données JSON, UTF-8. La longueur compte des **octets**, pas des caractères —
  ce qui a son importance côté Python (voir [14_python_extern_proc.md](14_python_extern_proc.md)).

### Ce que fait le parseur devant une trame invalide

Deux cas, et ils ne se traitent pas pareil (`ExternProc.cpp:336-357`) :

- **Opcode inconnu** : la trame est marquée non valide, les 5 octets d'en-tête sont
  consommés, et la machine à états **repart en lecture d'en-tête** sur les octets suivants.
  Aucune erreur n'est remontée : le flux est resynchronisé à l'aveugle.
- **Longueur annoncée > 4 MiB** : un pair cassé ou hostile pourrait annoncer jusqu'à 4 GiB
  et faire buffériser d'autant. Le message est logué en erreur, `hasError()` passe à `true`,
  le tampon est vidé — et l'appelant **ferme la connexion** au lieu de continuer
  (`ExternProc.cpp:169-177` côté serveur, `:511-518` côté client).

---

## ExternProcServer (côté calaos_server)

```cpp
// (dérivé, ExternProc.cpp:31-40, 180-186, 135-149, 118-133)
ExternProcServer server("mqtt");   // préfixe du chemin de socket

// Démarre le sous-processus. La ligne de commande construite est :
//   <process> --socket <sockpath> --namespace <name> <args>
server.startProcess("/usr/bin/calaos_mqtt", "mqtt", jsonConfig);

// Envoie un message (une trame) au sous-processus
server.sendMessage(R"({"topic":"commandes/lumiere","payload":"1"})");

// Signaux (ExternProc.h:104-106)
server.messageReceived.connect([](const string &msg) { /* payload d'une trame */ });
server.processConnected.connect([]() { /* le sous-processus s'est connecté au socket */ });
server.processExited.connect([]() { /* sous-processus terminé, ou spawn en échec */ });

// Arrêt
server.terminate();
```

**Chemin du socket** — construit une seule fois dans le constructeur
(dérivé, `ExternProc.cpp:33-36`) :

```
/tmp/calaos_proc_<uuid>_<prefix>_<pid>
```

C'est **toujours `/tmp`**, quels que soient `$CALAOS_HOME`, `--config` ou `--cache` : aucun
autre répertoire n'est consulté. Le fichier est supprimé par le destructeur
(`ExternProc.cpp:114-115`).

**Un seul client à la fois.** `ipcServer` accepte les connexions mais ne garde qu'un
`client` (`ExternProc.cpp:42-50`) : une seconde connexion écrase la référence à la première.
Le modèle est « un `ExternProcServer` pour un sous-processus ».

**`processExited` est émis dans les deux sens de l'échec** : sortie normale du processus
(`ExitEvent`, `:187-192`) **et** échec de `uv_spawn` (`ErrorEvent`, `:193-199`). Dans les deux
cas l'émission est différée de 0,1 s par `Timer::singleShot`, pour ne pas relancer depuis
l'intérieur du callback libuv.

### ⚠️ La garde `pid > 0` avant `kill(SIGTERM)` (T3.7, T3.9)

Après un `uv_spawn` **raté**, le handle libuv reste référencé mais son **pid vaut 0**. Or
`uv_kill(0, SIGTERM)` ne signale pas « rien » : il signale **tout le groupe de processus**.
Le défaut était réel et il a été observé — il a tué le harnais automake pendant l'exécution
de `make check` (T3.2a).

Les deux sites de `kill()` de cette classe portent désormais la garde (intégral,
`ExternProc.cpp:100-107`) :

```cpp
    //Only signal a process that really spawned: after a failed uv_spawn the
    //handle keeps pid 0, and uv_kill(0, SIGTERM) signals our whole process
    //group (this killed the make check harness, see KnxIo_test.cpp).
    if (process_exe && process_exe->referenced() && process_exe->pid() > 0)
    {
        process_exe->kill(SIGTERM);
        process_exe->close();
    }
```

La même garde est portée par `ExternProcServer::terminate()` (`ExternProc.cpp:123-126`) et
par `McpServerManager::stop()` (`McpServerManager.cpp:163-170`, cf.
[15_mcp_server.md](15_mcp_server.md)). **Tout nouveau site qui appelle `kill()` sur un
`uvw::ProcessHandle` doit reprendre cette garde.**

### Variables d'environnement injectées dans le sous-processus

(dérivé, `ExternProc.cpp:252-273`)

| Variable | Valeur |
|---|---|
| `CALAOS_CACHE_PATH` | `Utils::getCachePath()` |
| `CALAOS_CONFIG_PATH` | `Utils::getConfigPath()` |
| `CALAOS_LOG_LEVEL` | option de config `debug_level` |
| `CALAOS_LOG_DOMAINS` | option de config `debug_domains` |
| `CALAOS_FORCE_COLOR` | `Logger::isColorEnabled()` |

S'y ajoutent, **héritées du parent quand elles existent** (sinon transmises vides) : `PATH`,
`HOME`, `LANG`, `LC_ALL`, `LANGUAGE`, `LD_LIBRARY_PATH`, `PWD`. L'environnement est
**remplacé, pas complété** : tout ce qui n'est pas dans cette liste ne parvient pas au
sous-processus.

> ⚠️ `CALAOS_CONFIG_PATH` vaut ici `Utils::getConfigPath()`, qui **ignore `--config`** et
> re-dérive le chemin depuis `$HOME`. Un sous-processus lancé avec un `--config` explicite
> lit donc potentiellement le mauvais répertoire. Le sidecar MCP contourne le problème
> autrement (`McpServerManager.cpp:238-245`).

**stdout et stderr** du sous-processus sont capturés sur deux pipes séparés, découpés en
lignes et réémis respectivement sur `std::cout` et `std::cerr` du serveur
(`ExternProc.cpp:222-250`).

---

## ExternProcClient (côté sous-processus)

À utiliser dans le `main()` du sous-processus driver.

```cpp
// (dérivé, ExternProc.h:125-172, ExternProc.cpp:415-441)
class MonDriverProcess : public ExternProcClient
{
public:
    EXTERN_PROC_CLIENT_CTOR(MonDriverProcess)

    virtual bool setup(int &argc, char **&argv) override {
        if (!connectSocket())   // obligatoire : le ctor ne se connecte pas
            return false;
        // ... initialisation du driver
        return true;            // false => le main() rend 1 et quitte
    }

    virtual int procMain() override {
        run(5000);              // boucle select() avec timeout 5 s
        return 0;
    }

    virtual void readTimeout() override {
        // appelé à chaque expiration du timeout de select()
    }

    virtual void messageReceived(const string &msg) override {
        // payload d'une trame venant de calaos_server ;
        // répondre avec sendMessage()
    }
};

EXTERN_PROC_CLIENT_MAIN(MonDriverProcess)
```

**Le constructeur ne connecte pas le socket.** Il se contente de lire `--socket` et
`--namespace` dans `argv`, puis d'initialiser le logger sous le nom du namespace
(`ExternProc.cpp:415-435`). C'est `setup()` qui doit appeler `connectSocket()` — tous les
drivers de l'arbre le font en première instruction.

⚠️ Le constructeur consomme les options en faisant `argc -= 2; argv += 2;` **sans vérifier
leur position** (`ExternProc.cpp:419-430`) : le contrat est que `--socket` et `--namespace`
soient les **deux premières** options, dans cet ordre, ce que `startProcess()` garantit. Un
driver qui recevrait ses propres options avant celles-là verrait `argc`/`argv` corrompus.
`--namespace` absent fait retomber le nom sur `"extern_process"`.

**`connectSocket()` refuse un chemin trop long** pour `sun_path` au lieu de le tronquer
silencieusement et de se connecter ailleurs (`ExternProc.cpp:453-460`).

`sockfd` est initialisé à `-1` et non à `0`, pour qu'une destruction avant
`connectSocket()` ne referme pas un descripteur arbitraire (`ExternProc.h:163-165`).

### Boucle principale

`run(timeoutms)` est un `select()` sur le socket IPC plus les FD ajoutés par `appendFd()`
(`ExternProc.cpp:523-573`). Trois façons d'en sortir : `select()` renvoie une erreur,
`processSocketRecv()` échoue (pair fermé, ou erreur de framing), ou `handleFdSet()` renvoie
`false`.

Le timeout usuel dépend du driver : `200` ms pour MQTT (`MqttExternProc_main.cpp`), Roon et
Reolink ; la valeur n'a rien de conventionnel, c'est celle qui convient au driver.

---

## Macros utilitaires

```cpp
// (intégral, ExternProc.h:174-184)
#define EXTERN_PROC_CLIENT_CTOR(class_name) \
class_name(int &__argc, char **&__argv): ExternProcClient(__argc, __argv) {}

#define EXTERN_PROC_CLIENT_MAIN(class_name) \
int main(int argc, char **argv) \
{ \
    class_name inst(argc, argv); \
    if (inst.setup(argc, argv)) \
        return inst.procMain(); \
    return 1; \
}
```

---

## Communication JSON

Le payload d'une trame est une **chaîne libre** du point de vue du framing. Chaque driver
définit son propre protocole JSON interne, et il n'y a **aucune enveloppe commune** : pas de
champ `msg`, pas de `type`, pas d'`action` imposés.

### Exemple réel : MQTT

Le protocole MQTT est **volontairement minimal**, et il n'a **ni action ni type** :

**Paramètres de connexion au broker** — ils ne passent **pas** par une trame IPC. Ils sont
sérialisés en JSON et donnés au sous-processus comme **unique argument de ligne de commande**
(dérivé, `MqttCtrl.cpp:29-41`) :

```json
{"host": "192.168.1.5", "port": "1883", "keepalive": "120", "user": "admin", "password": "secret"}
```

`user`/`password` ne sont ajoutés que si **les deux** existent dans la configuration de l'IO
(`MqttCtrl.cpp:35-39`). Le sous-processus exige exactement `argc == 2` après consommation de
`--socket`/`--namespace` et refuse de démarrer sinon
(`MqttExternProc_main.cpp:251-255`).

**Serveur → sous-processus** — publier, et rien d'autre
(dérivé, `MqttCtrl.cpp:107-117` ; consommé par `MqttExternProc_main.cpp:213-231`) :

```json
{"topic": "commandes/lumiere", "payload": "1"}
```

**Sous-processus → serveur** — un message reçu du broker
(dérivé, `MqttExternProc_main.cpp:312-322` ; consommé par `MqttCtrl.cpp:50-83`) :

```json
{"topic": "capteurs/temp", "payload": "21.5"}
```

**Il n'existe pas de message d'abonnement.** Le sous-processus s'abonne à `#` — c'est-à-dire
à **tout le broker** — dès la connexion (`MqttExternProc_main.cpp:136,153`), et c'est le
serveur qui filtre : `MqttCtrl::subscribeTopic()` enregistre des callbacks côté serveur
(`MqttCtrl.cpp:92-105`) et `topicMatchesSubscription()` fait le tri à la réception
(`MqttCtrl.cpp:69-80`).

Les payloads non-UTF8 ne sont plus supprimés en silence : `payloadToJsonString()` remplace
les octets invalides par `?` et délivre le message (note de version « MQTT »).

---

## Gestion des erreurs et redémarrage

Le redémarrage est la responsabilité du **contrôleur parent**, et la politique **n'est pas la
même partout** — c'est mesuré, pas déduit :

| Contrôleur | Politique de respawn sur `processExited` |
|---|---|
| `MqttCtrl` (`:43-48`) | relance **immédiate**, sans délai |
| `KNXCtrl` (`:36-41`, `:48-53`) | relance **immédiate**, sur les deux processus |
| `OLACtrl` (`:31-36`) | relance **immédiate** |
| `OwCtrl` (`:33-38`) | relance **immédiate** |
| `ReolinkCtrl` (`:37-43`) | relance **immédiate**, après purge des caméras enregistrées |
| `RoonCtrl` (`:39-44`) | relance **immédiate** |
| `ScriptExec` (`:116-135`) | **aucun** respawn : le handler nettoie et rend la main au script appelant |
| `WagoMap` (`:57-64`, `:113-135`) | **backoff, infini** — voir ci-dessous |

⚠️ Une relance « immédiate » sur un binaire qui échoue au démarrage est une **boucle serrée**
de spawn. Seul Wago s'en protège.

### Wago : relance indéfinie à backoff court (décision utilisateur)

Le Wago est la pièce maîtresse de l'installation : le processus est relancé **pour toujours**,
avec une rampe courte et un **plafond bas à 5 s** pour que la reprise soit rapide dès que
l'automate ou le réseau revient (intégral, `WagoMap.h:165-172`) :

```cpp
    //Backoff delay in seconds before respawning the subprocess. The Wago is
    //the centerpiece of the installation, so we retry FOREVER: a short ramp
    //avoids a tight spawn loop, but the cap stays low (5s) so recovery is
    //fast once the PLC/network is back (e.g. after maintenance cut it).
    //attempt is the 0-based count of consecutive failures so far.
    static double respawnDelay(int attempt)
    {
        static const double delays[] = { 1.0, 2.0, 3.0, 5.0 };
```

Soit **1 s, 2 s, 3 s, puis 5 s pour toujours**. Le compteur d'échecs est remis à zéro dès que
`processConnected` est émis (`WagoMap.cpp:68-73`). Toutes les `RESPAWN_LOG_EVERY = 10`
tentatives, une erreur nomme le binaire et l'automate visé
(`WagoMap.h:161-163`, `WagoMap.cpp:123-126`). Une seule relance est armée à la fois
(`WagoMap.cpp:115-116`).

---

## FDs supplémentaires

`ExternProcClient` permet d'ajouter des descripteurs à la boucle `select()` :

```cpp
// (dérivé, ExternProc.h:147,152-154 ; ExternProc.cpp:539-543,560-570,586-597)
appendFd(socket_fd);    // surveille ce FD
removeFd(socket_fd);

// override dans la sous-classe :
virtual bool handleFdSet(int fd) override {
    // traite l'activité sur fd
    return true;  // false => sortie de la boucle run()
}
```

C'est ce que fait MQTT : `appendFd(m_client->socket())` sur le socket mosquitto, et
`handleFdSet()` appelle `m_client->loop(0, 1)` (`MqttExternProc_main.cpp:302` et `:319-323`).

Un driver qui a **sa propre boucle** peut se passer de `run()` : `getSocketFd()` et
`processSocketRecv()` sont exposés pour cela (`ExternProc.h:156-158`).

---

## Drivers Python

Reolink et Roon utilisent des sous-processus Python qui s'appuient sur la bibliothèque
`calaos_extern_proc` ([src/lib/calaos-python/](../src/lib/calaos-python/)). Elle implémente
**exactement le même framing** — 1 octet d'opcode `0x21` + 4 octets de longueur big-endian —
et la même interface `ExternProcClient` qu'en C++.

Voir la documentation complète : [14_python_extern_proc.md](14_python_extern_proc.md).

> Le sidecar MCP (`calaos_mcp`) **n'utilise pas** ce framework : c'est un serveur HTTP sur
> socket Unix, proxifié en octets bruts. Voir [15_mcp_server.md](15_mcp_server.md).

---

## Création d'un nouveau driver ExternProc

1. Créer `MonDriverCtrl` dans le serveur (hérite de `sigc::trackable`)
   - Instancie `ExternProcServer("mondriver")`
   - Connecte `messageReceived`, `processConnected`, `processExited`
   - **Choisit sa politique de respawn** — préférer le backoff de `WagoMap` à la relance
     immédiate, qui boucle en cas d'échec au démarrage
   - Implémente le protocole JSON

2. Créer `MonDriverExternProc_main.cpp` (le sous-processus)
   - Hérite de `ExternProcClient`
   - Utilise `EXTERN_PROC_CLIENT_CTOR()` et `EXTERN_PROC_CLIENT_MAIN()`
   - Implémente `setup()` (qui **doit** appeler `connectSocket()`), `procMain()`,
     `readTimeout()` et `messageReceived()`

3. Déclarer le binaire dans `src/bin/calaos_server/Makefile.am`, en incluant
   `IO/ExternProc.cpp` et `IO/ExternProc.h` dans ses `_SOURCES`

4. Créer les IOs qui utilisent `MonDriverCtrl`
