# ExternProc — Framework de sous-processus IPC

## Vue d'ensemble

`ExternProc` est un framework léger d'IPC (Inter-Process Communication) qui permet à `calaos_server` de déléguer certains drivers à des sous-processus isolés. Utilisé par :

| Driver | Sous-processus | Fichier main |
|---|---|---|
| **MQTT** | `calaos_mqtt` | `IO/Mqtt/MqttExternProc_main.cpp` |
| **KNX** | `calaos_knx` | `IO/KNX/KNXExternProc_main.cpp` |
| **Wago** | `calaos_wago` | `IO/Wago/WagoExternProc_main.cpp` |
| **OneWire** | `calaos_1wire` | `IO/OneWire/OWExternProc_main.cpp` |
| **OLA** (DMX) | `calaos_ola` | `IO/OLA/OLAExternProc_main.cpp` |
| **Reolink** | `calaos_reolink` | `IO/Reolink/ReolinkCtrl.cpp` |
| **Lua scripts** | `calaos_script` | `LuaScript/ScriptExtern_main.cpp` |
| **Roon** | `calaos_roon` | `Audio/ExternProcRoon_main.py` (Python) |
| **Reolink** (Python) | `calaos_reolink` | `IO/Reolink/ExternProcReolink_main.py` (Python) |

Le serveur principal gère les sous-processus via `ExternProcServer`, et les sous-processus utilisent `ExternProcClient` pour se connecter.

---

## Protocole de framing

**Fichier :** [src/bin/calaos_server/IO/ExternProc.h](../src/bin/calaos_server/IO/ExternProc.h)

Messages frammés sur un socket Unix (pipe) :

```
+-------+---------+--------+----------------+
| START | TYPE    | SIZE   | PAYLOAD        |
| 0x02  | 1 byte  | 2 bytes| jusqu'à 65536B |
+-------+---------+--------+----------------+
```

- `START` : marqueur de début (`0x02`)
- `TYPE` : `TypeMessage = 0x21` (seul type supporté actuellement)
- `SIZE` : taille du payload en big-endian (2 octets, max 65535)
- `PAYLOAD` : données JSON (string UTF-8)

---

## ExternProcServer (côté calaos_server)

```cpp
ExternProcServer server("prefix");  // préfixe pour le socket Unix

// Démarrer le sous-processus
server.startProcess("/usr/bin/calaos_mqtt", "mqtt_instance_id", "--host 192.168.1.5");

// Envoyer un message JSON au sous-processus
server.sendMessage(R"({"action":"subscribe","topic":"capteurs/#"})");

// Signaux
server.messageReceived.connect([](const string &msg) {
    // traite la réponse JSON du sous-processus
});
server.processConnected.connect([]() {
    // sous-processus prêt
});
server.processExited.connect([]() {
    // sous-processus terminé (crash → relancer)
});

// Arrêt
server.terminate();
```

Le socket Unix est créé dans `/tmp/` ou `$CALAOS_HOME/run/`.

---

## ExternProcClient (côté sous-processus)

À utiliser dans le `main()` du sous-processus driver.

```cpp
class MonDriverProcess : public ExternProcClient
{
public:
    EXTERN_PROC_CLIENT_CTOR(MonDriverProcess)
    
    virtual bool setup(int &argc, char **&argv) override {
        // initialisation (connexion broker, etc.)
        return true; // false = quitte
    }
    
    virtual int procMain() override {
        run(5000);  // boucle avec timeout 5s
        return 0;
    }
    
    virtual void readTimeout() override {
        // appelé toutes les 5s
    }
    
    virtual void messageReceived(const string &msg) override {
        // traite les messages venant de calaos_server
        // et appelle sendMessage() pour répondre
    }
};

EXTERN_PROC_CLIENT_MAIN(MonDriverProcess)
```

---

## Macros utilitaires

```cpp
EXTERN_PROC_CLIENT_CTOR(ClassName)
// Génère : ClassName(int &argc, char **&argv) : ExternProcClient(argc, argv) {}

EXTERN_PROC_CLIENT_MAIN(ClassName)
// Génère le main() standard qui instancie et exécute ClassName
```

---

## Communication JSON

Les messages entre le serveur et le sous-processus sont des chaînes JSON libres. Chaque driver définit son propre protocole JSON interne.

### Exemple MQTT

**Serveur → sous-processus :**
```json
{"action": "subscribe", "topic": "capteurs/temp", "id": "id-temp-001"}
{"action": "publish", "topic": "commandes/lumiere", "payload": "1"}
{"action": "connect", "host": "192.168.1.5", "port": 1883, "user": "admin", "password": "secret"}
```

**Sous-processus → serveur :**
```json
{"type": "message", "topic": "capteurs/temp", "payload": "{\"value\": 21.5}"}
{"type": "connected"}
{"type": "disconnected"}
```

---

## Gestion des erreurs et redémarrage

Si un sous-processus se termine de façon inattendue, le signal `processExited` est émis. Le driver parent (ex: `MqttCtrl`) doit gérer le redémarrage.

---

## FDs supplémentaires

`ExternProcClient` permet d'ajouter des file descriptors à la boucle principale :

```cpp
appendFd(socket_fd);    // surveille ce FD
removeFd(socket_fd);

// override dans la sous-classe :
virtual bool handleFdSet(int fd) override {
    // traite l'activité sur fd
    return true;  // false = arrêt de la boucle
}
```

---

## Drivers Python

Reolink et Roon utilisent des sous-processus Python qui s'appuient sur la bibliothèque `calaos_extern_proc` ([src/lib/calaos-python/](../src/lib/calaos-python/)). Elle implémente le même protocole de framing et la même interface `ExternProcClient` qu'en C++.

Voir la documentation complète : [14_python_extern_proc.md](14_python_extern_proc.md)

---

## Création d'un nouveau driver ExternProc

1. Créer `MonDriverCtrl` dans le serveur (hérite de `sigc::trackable`)
   - Instancie `ExternProcServer`
   - Implémente le protocole JSON
   - Gère le cycle de vie du processus

2. Créer `MonDriverExternProc_main.cpp` (le sous-processus)
   - Hérite de `ExternProcClient`
   - Utilise `EXTERN_PROC_CLIENT_MAIN()`
   - Implémente `setup()`, `procMain()`, `messageReceived()`

3. Enregistrer dans `Makefile.am` comme binaire séparé

4. Créer les IOs qui utilisent `MonDriverCtrl`
