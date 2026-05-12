# IO Drivers — Protocoles et types d'IOs

## Architecture commune

Tous les drivers héritent de `IOBase`. Ils s'enregistrent dans `IOFactory` via la macro `REGISTER_IO`. Les types génériques (switch, volet, lumière…) sont définis dans `src/bin/calaos_server/IO/` ; les implémentations spécifiques à un protocole sont dans les sous-dossiers.

### Hiérarchie type générique → driver

```
IOBase
  ├── InputSwitch           → WagoInputSwitch, KNXInputSwitch, MqttInputSwitch, GpioInputSwitch…
  ├── InputSwitchLongPress  → WagoInputSwitchLP, KNXInputSwitchLP, GpioInputSwitchLP…
  ├── InputSwitchTriple     → WagoInputSwitchTriple, KNXInputSwitchTriple…
  ├── InputAnalog           → WagoInputTemp, KNXInputAnalog, MqttInputAnalog…
  ├── InputTemp             → KNXInputTemp, MqttInputTemp…
  ├── OutputLight           → WagoOutputLight, KNXOutputLight, MqttOutputLight, RemoteUIOutputRelay…
  ├── OutputLightDimmer     → WagoOutputLightDimmer, KNXOutputLightDimmer, MqttOutputLightDimmer…
  ├── OutputLightRGB        → HueOutputLightRGB, KNXOutputLightRGB, MqttOutputLightRGB…
  ├── OutputShutter         → WagoOutputShutter, KNXOutputShutter, MqttOutputShutter…
  ├── OutputShutterSmart    → WagoOutputShutterSmart, KNXOutputShutterSmart, GpioOutputShutterSmart…
  ├── OutputAnalog          → WagoOutputAnalog, KNXOutputAnalog, MqttOutputAnalog…
  ├── AudioPlayer           → Squeezebox, RoonPlayer
  └── IPCam                 → Axis, Foscam, Gadspot, Planet, StandardMjpeg, SynoSurveillanceStation
```

---

## Types d'IOs génériques

### Entrées (Input)

| Classe | Type XML | DATA_TYPE | Description |
|---|---|---|---|
| `InputSwitch` | `InputSwitch` | TBOOL | Interrupteur simple |
| `InputSwitchLongPress` | `InputSwitchLongPress` | TBOOL | Interrupteur avec appui long |
| `InputSwitchTriple` | `InputSwitchTriple` | TBOOL | Interrupteur triple action |
| `InputAnalog` | `InputAnalog` | TDOUBLE | Entrée analogique |
| `InputTemp` | `InputTemp` | TDOUBLE | Capteur de température |
| `InputString` | `InputString` | TSTRING | Entrée chaîne |
| `InputTime` | `InputTime` | TBOOL | Déclencheur horaire (heure du jour) |
| `InputTimer` | `InputTimer` | TBOOL | Minuterie (délai) |
| `InPlageHoraire` | `InPlageHoraire` | TBOOL | Plage horaire (vrai/faux selon heure) |

### Sorties (Output)

| Classe | Type XML | DATA_TYPE | Description |
|---|---|---|---|
| `OutputLight` | `OutputLight` | TBOOL | Lumière on/off |
| `OutputLightDimmer` | `OutputLightDimmer` | TDOUBLE | Gradateur (0-100%) |
| `OutputLightRGB` | `OutputLightRGB` | TSTRING | Lumière couleur RGB |
| `OutputShutter` | `OutputShutter` | TSTRING | Volet (up/down/stop/%) |
| `OutputShutterSmart` | `OutputShutterSmart` | TSTRING | Volet avec calibration temps |
| `OutputAnalog` | `OutputAnalog` | TDOUBLE | Sortie analogique |

### IOs internes

| Classe | Description |
|---|---|
| `IntValue` | Valeur interne (variable globale, entier) |
| `Scenario` | IO de type scénario (bouton de déclenchement) |
| `Internal` | Valeur interne booléenne ou entière |

---

## Driver Wago (Modbus TCP)

**Dossier :** [src/bin/calaos_server/IO/Wago/](../src/bin/calaos_server/IO/Wago/)

Automates Wago 750 connectés en Modbus TCP. Utilise `ExternProc` : un sous-processus `calaos_wago` est lancé et communique via pipe IPC. Le protocole Modbus est géré par `WagoCtrl` (wrapping libmbus) dans ce sous-processus.

### WagoCtrl

```cpp
WagoCtrl(string host, int port = 502);
bool Connect();
bool read_bits(UWord address, int nb, vector<bool> &values);
bool write_single_bit(UWord address, bool val);
bool read_words(UWord address, int nb, vector<UWord> &values);
bool write_single_word(UWord address, UWord val);
```

### Paramètres communs Wago

| Paramètre | Description |
|---|---|
| `host` | IP de l'automate Wago |
| `port` | Port Modbus (défaut 502) |
| `var` | Adresse Modbus |
| `knx_group` | Adresse KNX (si bridge Wago-KNX) |

---

## Driver KNX

**Dossier :** [src/bin/calaos_server/IO/KNX/](../src/bin/calaos_server/IO/KNX/)

Bus domotique KNX via le démon `knxd`. Utilise `ExternProc` : un sous-processus `calaos_knx` est lancé et communique via pipe IPC.

### Composants

| Fichier | Rôle |
|---|---|
| `KNXCtrl` | Contrôleur KNX, gère la connexion à knxd |
| `KNXBase` | Classe de base pour tous les IOs KNX |
| `KNXExternProc_main` | Point d'entrée du sous-processus |
| `KNXExternProc_cli` | Côté client (dans calaos_server) |

### Paramètres communs KNX

| Paramètre | Description |
|---|---|
| `address` | Adresse de groupe KNX (ex: `1/2/3`) |
| `feedbackAddress` | Adresse de retour d'état |
| `datatype` | Type de datapoint EIB/KNX |

---

## Driver MQTT

**Dossier :** [src/bin/calaos_server/IO/Mqtt/](../src/bin/calaos_server/IO/Mqtt/)

Protocole MQTT via un sous-processus externe (`MqttExternProc_main`). Utilise `MqttCtrl` comme interface dans le serveur principal.

### MqttCtrl

```cpp
void subscribeTopic(string topic, MsgReceivedSignal callback);
void publishTopic(string topic, string payload);
string getValue(Params params, bool &err, string topic_param, string path_param = "path");
string getValueJson(Params params, string path, string payload);  // JSONPath
void setValue(Params params, bool val);
void setValueColor(Params params, ColorValue val);
void subscribeStatusTopics(IOBase *io);  // batterie, online, etc.
```

### Paramètres communs MQTT

| Paramètre | Description |
|---|---|
| `host` | Broker MQTT (IP ou hostname) |
| `port` | Port (défaut 1883) |
| `topic` | Topic de souscription |
| `topic_set` | Topic de publication (commande) |
| `path` | JSONPath dans le payload (ex: `$.value`) |
| `user` | Nom d'utilisateur |
| `password` | Mot de passe |

### Gestion du statut (batterie, online)

`MqttCtrl::subscribeStatusTopics()` souscrit automatiquement aux topics de statut si les paramètres `topic_battery`, `topic_online`, etc. sont définis. Les valeurs mettent à jour `IOBase::status_info`.

---

## Driver GPIO

**Dossier :** [src/bin/calaos_server/IO/Gpio/](../src/bin/calaos_server/IO/Gpio/)

Entrées/sorties GPIO Linux via le sous-système sysfs ou gpiod. Géré par `GpioCtrl`.

### Types disponibles

- `GpioInputSwitch` — lecture GPIO en entrée
- `GpioInputSwitchLongPress` — avec détection appui long
- `GpioInputSwitchTriple` — triple action
- `GpioOutputSwitch` — GPIO en sortie (on/off)
- `GpioOutputShutter` — volet sur deux GPIOs (montée/descente)
- `GpioOutputShutterSmart` — volet avec calibration

### Paramètres GPIO

| Paramètre | Description |
|---|---|
| `gpio_number` | Numéro de pin GPIO |
| `inverted` | `"true"` pour inverser la logique |

---

## Driver MySensors

**Dossier :** [src/bin/calaos_server/IO/MySensors/](../src/bin/calaos_server/IO/MySensors/)

Réseau de capteurs sans-fil MySensors (RF24, etc.). Géré par `MySensorsController` et `MySensorsControllerList`.

---

## Driver OneWire

**Dossier :** [src/bin/calaos_server/IO/OneWire/](../src/bin/calaos_server/IO/OneWire/)

Bus 1-Wire via owfs. Principalement pour capteurs de température DS18B20. Utilise `ExternProc` : sous-processus `calaos_1wire` (fichier `OWExternProc_main.cpp`), interfacé via `OWCtrl`.

---

## Driver OLA (Open Lighting Architecture)

**Dossier :** [src/bin/calaos_server/IO/OLA/](../src/bin/calaos_server/IO/OLA/)

Contrôle DMX512 via OLA pour l'éclairage scénique/RGB. Utilise `ExternProc` : sous-processus `calaos_ola` (fichier `OLAExternProc_main.cpp`), interfacé via `OLACtrl`.

---

## Driver Hue

**Dossier :** [src/bin/calaos_server/IO/Hue/](../src/bin/calaos_server/IO/Hue/)

Ampoules Philips Hue via l'API REST du bridge Hue.

**Classe :** `HueOutputLightRGB`

| Paramètre | Description |
|---|---|
| `host` | IP du bridge Hue |
| `api_key` | Clé API Hue |
| `light_id` | ID de l'ampoule |

---

## Driver LAN

**Dossier :** [src/bin/calaos_server/IO/LAN/](../src/bin/calaos_server/IO/LAN/)

| Classe | Description |
|---|---|
| `PingInputSwitch` | Présence réseau (ping) → TBOOL |
| `WOLOutputBool` | Wake-On-LAN |

---

## Driver Web

**Dossier :** [src/bin/calaos_server/IO/Web/](../src/bin/calaos_server/IO/Web/)

IOs basés sur des requêtes HTTP (GET/POST). Permet d'interroger des APIs web ou d'envoyer des commandes HTTP.

---

## Driver Reolink

**Dossier :** [src/bin/calaos_server/IO/Reolink/](../src/bin/calaos_server/IO/Reolink/)

Intégration des caméras Reolink via leur API. Utilise `ExternProc` : sous-processus `calaos_reolink` interfacé via `ReolinkCtrl`.

---

## Driver RemoteUI (IOs embarqués)

**Dossier :** [src/bin/calaos_server/IO/RemoteUI/](../src/bin/calaos_server/IO/RemoteUI/)

IOs physiques portés par les appareils RemoteUI (tableaux de bord embarqués connectés au serveur via WebSocket).

| Classe | Description |
|---|---|
| `RemoteUIOutputRelay` | Relais commandé depuis le serveur vers l'appareil |

Voir aussi [07_remoteui.md](07_remoteui.md) pour le protocole de communication.

```cpp
// Commande envoyée vers le device
void RemoteUIOutputRelay::set_value_real(bool val) {
    // envoie via RemoteUIManager::sendCommand(remote_ui_id, "relay_set", {...})
}

// Mise à jour depuis le device (sans reboucler la commande)
void RemoteUIOutputRelay::updateStateFromDevice(bool val);
```

---

## Ajouter un nouveau driver IO

1. Créer un dossier `src/bin/calaos_server/IO/MonDriver/`
2. Créer la classe héritant du type générique approprié (`OutputLight`, `InputSwitch`, etc.)
3. Implémenter les méthodes virtuelles : `set_value_real()`, `hasChanged()`, constructor avec `Params&`
4. Appeler `REGISTER_IO(MaClasse)` au niveau fichier dans le `.cpp`
5. Remplir `ioDoc` dans le constructeur (documentation auto)
6. Ajouter les fichiers au `Makefile.am`
