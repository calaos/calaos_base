# IO Drivers — Protocoles et types d'IOs

> **Écrit contre le code et les goldens** (E4.5c). Chaque affirmation porte sa provenance :
> `(dérivé, Fichier.cpp:L-L)` pour une lecture du source, `(capturé, <golden>)` pour un extrait
> d'un fichier de référence de `tests/core/golden/`. Les chemins sont relatifs à `src/`.

## Architecture commune

Tous les IOs héritent de `IOBase`. Ils **s'auto-enregistrent** dans `IOFactory` au chargement du
binaire, via une macro posée au niveau fichier dans le `.cpp` du driver
(dérivé, [IO/IOFactory.h:40-46](../src/bin/calaos_server/IO/IOFactory.h)) :

```cpp
// (dérivé, IO/IOFactory.h:45-46, intégral)
#define REGISTER_IO_USERTYPE(NAME, TYPE) REGISTER_FACTORY(NAME, TYPE, IOBase)
#define REGISTER_IO(TYPE) REGISTER_IO_USERTYPE(TYPE, TYPE)
```

- `REGISTER_IO(X)` publie le type XML `X` servi par la classe `X`.
- `REGISTER_IO_USERTYPE(NOM, Classe)` publie un **alias** : le type XML `NOM` est servi par
  `Classe`. C'est ce qui permet à `WagoInputSwitch` d'être servi par `WIDigitalBP`, ou à `slim`
  d'être servi par `Squeezebox`.

**La recherche du type est insensible à la casse** : `CreateIO()` passe le type en minuscules
avant de consulter le registre (dérivé, [IO/IOFactory.cpp:42](../src/bin/calaos_server/IO/IOFactory.cpp)),
et `RegisterClass()` fait de même à l'enregistrement
(dérivé, [IO/IOFactory.h:81](../src/bin/calaos_server/IO/IOFactory.h)). Deux enregistrements qui
ne diffèrent que par la casse **entrent en collision** : le premier gagne, la collision est
seulement journalisée — le code tourne depuis des initialiseurs statiques, avant `main()`
(dérivé, [IO/IOFactory.h:83-96](../src/bin/calaos_server/IO/IOFactory.h)).

Les **types génériques** (`InputSwitch`, `OutputLight`…) sont dans
`src/bin/calaos_server/IO/` ; les **implémentations de protocole** sont dans les sous-dossiers
(`Wago/`, `KNX/`, `Mqtt/`, `Gpio/`, `OneWire/`, `OLA/`, `Hue/`, `LAN/`, `Web/`, `Reolink/`,
`RemoteUI/`). Les caméras sont dans `IPCam/` et les lecteurs audio dans `Audio/`
(voir [06_ipcam.md](06_ipcam.md) et [05_audio.md](05_audio.md)).

### ⚠️ La plupart des types génériques ne sont PAS des types XML

`InputSwitch`, `InputAnalog`, `OutputLight`, `OutputLightDimmer`, `OutputLightRGB`,
`OutputShutter`, `OutputShutterSmart`, `OutputAnalog`, `OutputString`, `InputString`,
`InputTemp`, `InputSwitchLongPress`, `InputSwitchTriple`, `AudioPlayer` et `IPCam` sont des
**classes de base abstraites**. Aucune n'appelle `REGISTER_IO` : écrire
`type="OutputLight"` dans `io.xml` produit un type inconnu (vérifié : aucune de ces classes
n'apparaît dans les 84 invocations de `REGISTER_IO*` de l'arbre). Ce sont les types **de driver** qui
s'écrivent dans la configuration.

### Un type inconnu est ignoré, le reste de la configuration se charge

C'est le contrat des drivers retirés (voir plus bas) :

```cpp
// (dérivé, IO/IOFactory.cpp:44-53, intégral)
    auto it = ioFunctionRegistry.find(type);
    if (it != ioFunctionRegistry.end())
        obj = it->second(params);

    if (obj)
        cInfo() << type << ": Ok";
    else
        cWarning() <<  type << ": Unknown Input type !";

    return obj;
```

Le chargeur de pièce saute simplement l'IO nul
(dérivé, [Room.cpp:176-181](../src/bin/calaos_server/Room.cpp) : `if (io) AddIO(io.release());`).
Le comportement est épinglé par un test :
`CoreSmokeTest.UnknownIoTypeIsSkipped` charge une pièce contenant `InternalBool` **et**
`ThisTypeDoesNotExist`, puis vérifie `get_io_count() == 1`
(dérivé, [tests/core/CoreSmoke_test.cpp:86-99](../tests/core/CoreSmoke_test.cpp)).

⚠️ **Conséquence à connaître** : l'IO inconnu n'est jamais entré dans la pièce, et
`Room::SaveToXml()` ne sérialise que les IOs de la pièce
(dérivé, [Room.cpp:187-200](../src/bin/calaos_server/Room.cpp)). **La prochaine sauvegarde de la
configuration perd donc définitivement l'entrée.** Le serveur démarre, mais il ne conserve pas
ce qu'il n'a pas su charger.

### Documentation de référence des paramètres

La liste **exhaustive et à jour** des paramètres de chaque type est **générée par le binaire
lui-même**, à partir des appels `ioDoc->paramAdd*()` des constructeurs :

```
calaos_server --gendoc <chemin>
```

produit `<chemin>/io_doc.md` et `<chemin>/io_doc.json`
(dérivé, [main.cpp:127-132](../src/bin/calaos_server/main.cpp),
[IO/IOFactory.cpp:70-115](../src/bin/calaos_server/IO/IOFactory.cpp)). Les tableaux de ce document
en sont un extrait de lecture, pas la source d'autorité.

### `gui_type` : ce que voient les clients

Le paramètre `type` décrit le matériel ; **les interfaces doivent lire `gui_type`**
(dérivé, [IO/io_types.txt](../src/bin/calaos_server/IO/io_types.txt), qui liste les valeurs :
`light`, `temp`, `analog_in`, `analog_out`, `light_dimmer`, `light_rgb`, `shutter`,
`shutter_smart`, `var_bool`, `var_int`, `var_string`, `scenario`, `avreceiver`, `string_in`,
`string_out`, puis les non affichés `timer`, `time`, `time_range`, `switch`, `switch3`,
`switch_long`, `audio_player`, `camera`).

Deux valeurs de `gui_type` sont **structurantes** : `ListeRoom::addIOHash()` ne pousse un IO dans
`cameraCache` que si `gui_type == "camera"`, et dans `audioCache` que si
`gui_type == "audio_player"` (dérivé, [ListeRoom.cpp:81-87](../src/bin/calaos_server/ListeRoom.cpp)).
Ce sont ces deux caches que `get_home` rend dans ses tableaux `cameras` et `audio`.

---

## Option `insecure` — TLS des URLs configurées par l'utilisateur

**Décision utilisateur (T2.19)** : toute URL **configurée par l'utilisateur** est jointe **sans
vérification du certificat par défaut**. Le parc installé est majoritairement en HTTPS
auto-signé sur le LAN ; un défaut « vérifié » casserait toutes les installations existantes.
Seuls les services **codés en dur** (`calaos.fr`, push) restent vérifiés, parce qu'ils ne passent
pas par ces helpers (dérivé, [lib/UrlDownloader.h:143-151](../src/lib/UrlDownloader.h)).

La règle de décision tient en une ligne :

```cpp
// (dérivé, src/lib/UrlDownloader.h:156-159, intégral)
    static bool insecureParamEnabled(const std::string &paramValue)
    { return paramValue != "false"; }
    void setInsecureFromParam(const std::string &paramValue)
    { if (insecureParamEnabled(paramValue)) setInsecure(); }
```

| valeur de `insecure` | politique effective |
|---|---|
| paramètre absent (config existante) | **non vérifié** |
| `""` (vide) | **non vérifié** |
| `"true"` | **non vérifié** |
| `"false"` | **vérifié** |
| `"False"`, `"FALSE"`, `"0"`, `"no"`, n'importe quoi d'autre | **non vérifié** |

⚠️ **La comparaison est sensible à la casse.** `insecure="False"` **ne durcit rien** : seule la
chaîne exacte `false` active la vérification.

### Qui expose le paramètre, qui ne l'expose pas

Trois familles seulement **déclarent** `insecure` dans leur `ioDoc` et l'honorent par IO :

| famille | site |
|---|---|
| Caméras IP (toutes) | [IPCam/IPCam.cpp:39](../src/bin/calaos_server/IPCam/IPCam.cpp), politique appliquée via `IPCam::tlsInsecure()` ([IPCam/IPCam.h:77](../src/bin/calaos_server/IPCam/IPCam.h)) |
| IOs Web | [IO/Web/WebDocBase.cpp:76](../src/bin/calaos_server/IO/Web/WebDocBase.cpp), appliqué en [IO/Web/WebCtrl.cpp:140,467](../src/bin/calaos_server/IO/Web/WebCtrl.cpp) |
| Hue | [IO/Hue/HueOutputLightRGB.cpp:43](../src/bin/calaos_server/IO/Hue/HueOutputLightRGB.cpp), appliqué en `:53,145,160` |

Pour les caméras, **tous** les chemins de transfert honorent le paramètre : instantané
([IPCam/IPCam.cpp:108](../src/bin/calaos_server/IPCam/IPCam.cpp)), commandes PTZ
(`IPCam::camGet()`, [IPCam/IPCam.h:82-88](../src/bin/calaos_server/IPCam/IPCam.h)), API Synology
([IPCam/SynoSurveillanceStation.cpp:176,211,244](../src/bin/calaos_server/IPCam/SynoSurveillanceStation.cpp)),
relais MJPEG de l'API HTTP
([JsonApiHandlerHttp.cpp:982-987](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) et pièce
jointe d'une règle
([Rules/ActionCameraDownload.h:106-109](../src/bin/calaos_server/Rules/ActionCameraDownload.h)).

D'autres consommateurs sont **toujours non vérifiés, sans option** — il n'y a pas de paramètre
d'équipement où l'accrocher :

| consommateur | site |
|---|---|
| Bindings Lua (`downloadFile`, POST) | [LuaScript/ScriptBindings.cpp:375,390](../src/bin/calaos_server/LuaScript/ScriptBindings.cpp) |
| DataLogger (influxdb) | [DataLogger.cpp:59,102,203](../src/bin/calaos_server/DataLogger.cpp) |
| Squeezebox (pochette via jsonrpc LMS) | [Audio/Squeezebox.cpp:748-750](../src/bin/calaos_server/Audio/Squeezebox.cpp) |
| AVR Rose | [Audio/AVRRose.cpp:414,440](../src/bin/calaos_server/Audio/AVRRose.cpp) |

---

## Types d'IOs génériques (non instanciables directement)

Ces classes fournissent le contrat ; les drivers en héritent.

| Classe de base | `get_type()` | Rôle |
|---|---|---|
| `InputSwitch` | `TBOOL` | Interrupteur simple |
| `InputSwitchLongPress` | `TINT` | Interrupteur avec appui long |
| `InputSwitchTriple` | `TINT` | Interrupteur à clics multiples |
| `InputAnalog` | `TINT` | Entrée analogique |
| `InputTemp` (hérite d'`InputAnalog`) | `TINT` | Capteur de température |
| `InputString` | `TSTRING` | Entrée chaîne |
| `OutputLight` | `TBOOL` | Sortie on/off |
| `OutputLightDimmer` | `TSTRING` | Gradateur |
| `OutputLightRGB` | `TSTRING` | Couleur RGB |
| `OutputShutter` | `TSTRING` | Volet montée/descente |
| `OutputShutterSmart` | `TSTRING` | Volet avec position estimée par le temps |
| `OutputAnalog` | `TINT` | Sortie analogique |
| `OutputString` | `TSTRING` | Sortie chaîne |
| `AudioPlayer` | `TSTRING` | Lecteur audio ([05_audio.md](05_audio.md)) |
| `IPCam` | `TSTRING` | Caméra IP ([06_ipcam.md](06_ipcam.md)) |

⚠️ **`DATA_TYPE` n'a que quatre valeurs**, et `TDOUBLE` n'en fait pas partie :

```cpp
// (dérivé, src/lib/Utils.h:227, intégral)
typedef enum { TBOOL, TINT, TSTRING, TUNKNOWN } DATA_TYPE;
```

Les entrées et sorties analogiques, les gradateurs et les capteurs de température ne sont donc
**pas** `TDOUBLE` : `InputAnalog`, `InputTemp` et `OutputAnalog` sont `TINT`, et
`OutputLightDimmer` est `TSTRING`. Les valeurs à virgule circulent par
`get_value_double()` / `set_value(double)`
(dérivé, [IOBase.h:105,170](../src/bin/calaos_server/IOBase.h)), indépendamment de `get_type()`,
qui sert à choisir la représentation côté client.

(dérivé, déclarations `class X : public IOBase` de `IO/*.h`, `IPCam/IPCam.h:30`,
`Audio/AudioPlayer.h:32` ; `set_value_real()` est le point d'extension pur virtuel des familles
sortie — [IO/OutputLight.h:63](../src/bin/calaos_server/IO/OutputLight.h),
[IO/OutputLightDimmer.h:71](../src/bin/calaos_server/IO/OutputLightDimmer.h),
[IO/OutputAnalog.h:45](../src/bin/calaos_server/IO/OutputAnalog.h),
[IO/OutputString.h:39](../src/bin/calaos_server/IO/OutputString.h).)

### IOs internes et logiques — types XML réellement enregistrés

| Type XML | Classe | Fichier | Description |
|---|---|---|---|
| `InternalBool` | `Internal` | `IO/IntValue.cpp:28` | Variable interne booléenne |
| `InternalInt` | `Internal` | `IO/IntValue.cpp:27` | Variable interne numérique |
| `InternalString` | `Internal` | `IO/IntValue.cpp:29` | Variable interne chaîne |
| `Scenario` | `Scenario` | `IO/Scenario.cpp:28` | Bouton de scénario ([04_scenarios.md](04_scenarios.md)) |
| `InputTime` | `InputTime` | `IO/InputTime.cpp:28` | Déclencheur horaire |
| `InputTimer` | `InputTimer` | `IO/InputTimer.cpp:27` | Minuterie |
| `InPlageHoraire` | `InPlageHoraire` | `IO/InPlageHoraire.cpp:27` | Plage horaire |
| `TimeRange` | `InPlageHoraire` | `IO/InPlageHoraire.cpp:28` | Alias de `InPlageHoraire` |
| `OutputFake` | `OutputFake` | `IO/OutputFake.cpp:26` | Sortie factice (tests) |
| `AVReceiver` | `IOAVReceiver` | `Audio/AVReceiver.cpp:29` | Ampli AV ([05_audio.md](05_audio.md)) |

⚠️ **Piège de casse sur `InternalBool` / `InternalInt` / `InternalString`.** La recherche du
type par la fabrique est insensible à la casse, mais la classe `Internal` redécide son type de
donnée en **comparant le paramètre `type` à l'octet près** :

```cpp
// (dérivé, IO/IntValue.h:53-59, intégral)
    virtual DATA_TYPE get_type()
    {
        if (get_param("type") == "InternalBool") return TBOOL;
        if (get_param("type") == "InternalInt") return TINT;
        if (get_param("type") == "InternalString") return TSTRING;
        return TUNKNOWN;
    }
```

Écrire `type="internalbool"` **crée bien l'IO** — la fabrique le trouve — mais son `get_type()`
rend `TUNKNOWN`. Respecter la casse exacte des trois noms.

⚠️ Les plages horaires **nocturnes** (`23:00 → 01:00`) wrappent désormais sur minuit et se
déclenchent (T3.13) ; elles étaient vides auparavant. Voir
[03_rules_engine.md](03_rules_engine.md).

---

## Driver Wago (Modbus TCP)

**Dossier :** [src/bin/calaos_server/IO/Wago/](../src/bin/calaos_server/IO/Wago/)

Automates Wago 750 en Modbus TCP. Le dialogue Modbus se fait dans un **sous-processus**
`calaos_wago` (dérivé, [IO/Wago/WagoMap.cpp:48-50](../src/bin/calaos_server/IO/Wago/WagoMap.cpp)),
piloté par `ExternProcServer` sur un socket IPC ; `WagoCtrl` (enrobage de libmbus) vit dans ce
sous-processus.

### Types XML

| Type XML | Alias historique | Classe | Base |
|---|---|---|---|
| `WagoInputSwitch` | `WIDigitalBP`, `WIDigital` | `WIDigitalBP` | `InputSwitch` |
| `WagoInputSwitchLongPress` | `WIDigitalLong` | `WIDigitalLong` | `InputSwitchLongPress` |
| `WagoInputSwitchTriple` | `WIDigitalTriple` | `WIDigitalTriple` | `InputSwitchTriple` |
| `WagoInputAnalog` | `WIAnalog` | `WIAnalog` | `InputAnalog` |
| `WagoInputTemp` | `WITemp` | `WITemp` | `InputTemp` |
| `WagoOutputLight` | `WODigital` | `WODigital` | `OutputLight` |
| `WagoOutputDimmer` | `WODali` | `WODali` | `OutputLightDimmer` |
| `WagoOutputDimmerRGB` | `WODaliRVB` | `WODaliRVB` | `OutputLightRGB` |
| `WagoOutputAnalog` | `WOAnalog` | `WOAnalog` | `OutputAnalog` |
| `WagoOutputShutter` | `WOVolet` | `WOVolet` | `OutputShutter` |
| `WagoOutputShutterSmart` | `WOVoletSmart` | `WOVoletSmart` | `OutputShutterSmart` |

(dérivé, les 23 `REGISTER_IO*` de `IO/Wago/*.cpp`. Il n'existe **ni** `WagoOutputLightDimmer`
**ni** `WagoOutputLightRGB` : les noms sont `WagoOutputDimmer` et `WagoOutputDimmerRGB`.)

### Paramètres

| Paramètre | Obligatoire | Description |
|---|---|---|
| `host` | oui | IP de l'automate |
| `port` | non, défaut **502** | Port Modbus TCP |
| `var` | oui | Adresse Modbus (0…65535) |
| `wago_841` | sorties TOR | `false` si l'automate est un 750-842, `true` sinon |
| `knx` | non | `true` si la sortie est un device KNX (750-849 + module KNX/TP1) |

(dérivé, [IO/Wago/WagoIOBase.h:60-62](../src/bin/calaos_server/IO/Wago/WagoIOBase.h),
[IO/Wago/WODigital.cpp:40-44](../src/bin/calaos_server/IO/Wago/WODigital.cpp).
La clé du pont KNX est **`knx`**, un booléen — pas `knx_group`, qui est une clé du driver KNX.)

### WagoCtrl

```cpp
// (dérivé, IO/Wago/WagoCtrl.h:28-54, extrait)
WagoCtrl(std::string host, int port = 502);
bool Connect();
void Disconnect();
bool is_connected();
bool read_bits(Utils::UWord address, int nb, vector<bool> &values);
bool write_single_bit(Utils::UWord address, bool val);
bool read_words(Utils::UWord address, int nb, vector<Utils::UWord> &values);
bool write_single_word(Utils::UWord address, Utils::UWord val);
```

### Respawn du sous-processus — infini, à backoff court (décision utilisateur)

Le Wago est la pièce maîtresse de l'installation : le sous-processus est relancé
**indéfiniment**, jamais abandonné, avec une rampe courte et un **plafond bas** pour que la
reprise soit immédiate après une coupure réseau ou une maintenance.

```cpp
// (dérivé, IO/Wago/WagoMap.h:170-177, intégral)
    static double respawnDelay(int attempt)
    {
        static const double delays[] = { 1.0, 2.0, 3.0, 5.0 };
        constexpr int ndelays = sizeof(delays) / sizeof(delays[0]);
        if (attempt < 0) attempt = 0;
        if (attempt >= ndelays) attempt = ndelays - 1;
        return delays[attempt];
    }
```

Délais : 1 s, 2 s, 3 s, puis **5 s pour toujours**. Le compteur d'échecs est remis à zéro dès que
le processus est de nouveau connecté
(dérivé, [IO/Wago/WagoMap.cpp:70-71](../src/bin/calaos_server/IO/Wago/WagoMap.cpp)). Pour ne pas
inonder le journal tout en ne restant pas silencieux, une erreur est journalisée **une tentative
sur dix** (`RESPAWN_LOG_EVERY = 10`, dérivé,
[IO/Wago/WagoMap.h:163](../src/bin/calaos_server/IO/Wago/WagoMap.h),
[IO/Wago/WagoMap.cpp:123-129](../src/bin/calaos_server/IO/Wago/WagoMap.cpp)).

---

## Driver KNX

**Dossier :** [src/bin/calaos_server/IO/KNX/](../src/bin/calaos_server/IO/KNX/)

Bus KNX via le démon `knxd`, à travers le sous-processus `calaos_knx`
(dérivé, [IO/KNX/KNXCtrl.cpp:27-31](../src/bin/calaos_server/IO/KNX/KNXCtrl.cpp)). Un **second**
sous-processus, lancé avec `--internal-monitor-bus`, surveille le bus
(dérivé, [IO/KNX/KNXCtrl.cpp:28,52-55](../src/bin/calaos_server/IO/KNX/KNXCtrl.cpp)).

### Types XML

`KNXInputSwitch`, `KNXInputSwitchLongPress`, `KNXInputSwitchTriple`, `KNXInputAnalog`,
`KNXInputTemp`, `KNXOutputAnalog`, `KNXOutputLight`, `KNXOutputLightDimmer`, `KNXOutputLightRGB`,
`KNXOutputShutter`, `KNXOutputShutterSmart` — onze types
(dérivé, [IO/KNX/KNXIo.cpp:319-329](../src/bin/calaos_server/IO/KNX/KNXIo.cpp)). Depuis T3.2a les
onze classes sont des coquilles posées sur le mixin `KNXIo<Base>`, **les noms de type XML sont
inchangés : ils sont l'ABI des `io.xml` existants**
(dérivé, [IO/KNX/KNXIo.cpp:22-26,314-317](../src/bin/calaos_server/IO/KNX/KNXIo.cpp)).

### Paramètres

| Paramètre | Obligatoire | Description |
|---|---|---|
| `host` | oui, défaut `127.0.0.1` | Hôte de `knxd` |
| `knx_group` | oui | Adresse de groupe, ex. `1/2/3` |
| `listen_knx_group` | non | Adresse de groupe pour l'écoute d'état |
| `eis` | non, 0…15, défaut `EIS_Value_Int` | Type de donnée EIS/KNX |
| `read_at_start` | défaut `"false"` | Émettre une requête de lecture au démarrage |

(dérivé, [IO/KNX/KNXBase.cpp:33-38](../src/bin/calaos_server/IO/KNX/KNXBase.cpp).)

Les types multi-adresses remplacent `knx_group` par des clés dédiées
(dérivé, [IO/KNX/KNXIo.cpp:231-236,278-279,301-302](../src/bin/calaos_server/IO/KNX/KNXIo.cpp)) :

- RGB : `knx_group_red` / `_green` / `_blue`, et `listen_knx_group_red` / `_green` / `_blue` ;
- volets : `knx_group_up` et `knx_group_down`.

⚠️ **`listen_knx_group` n'est pas un repli, c'est un écrasement.** Dès que la clé
`listen_<base>` **existe**, elle remplace `<base>` pour la lecture, que `<base>` soit renseigné ou
non :

```cpp
// (dérivé, IO/KNX/KNXBase.cpp:45-52, intégral)
string KNXBase::getReadGroupAddr(const string &group_base)
{
    string knx_group = params->get_param(group_base);
    if (params->Exists("listen_" + group_base))
        knx_group = params->get_param("listen_" + group_base);

    return knx_group;
}
```

Avec les deux clés posées, c'est donc **`listen_knx_group` qui est lue**, jamais `knx_group`. La
même règle s'applique aux variantes `_red` / `_green` / `_blue`. L'écriture, elle, vise toujours
`knx_group` (dérivé, [IO/KNX/KNXIo.cpp:164,190,218](../src/bin/calaos_server/IO/KNX/KNXIo.cpp)).

---

## Driver MQTT

**Dossier :** [src/bin/calaos_server/IO/Mqtt/](../src/bin/calaos_server/IO/Mqtt/)

Sous-processus `calaos_mqtt`
(dérivé, [IO/Mqtt/MqttCtrl.cpp:27-28](../src/bin/calaos_server/IO/Mqtt/MqttCtrl.cpp), source
`MqttExternProc_main.cpp`), piloté depuis le serveur par `MqttCtrl`.

### Types XML

`MqttInputSwitch`, `MqttInputAnalog`, `MqttInputTemp`, `MqttInputString`, `MqttOutputLight`,
`MqttOutputLightDimmer`, `MqttOutputLightRGB`, `MqttOutputShutter`, `MqttOutputAnalog`
(dérivé, les neuf `REGISTER_IO` de `IO/Mqtt/*.cpp`).

### Paramètres communs

| Paramètre | Obligatoire | Description |
|---|---|---|
| `host` | non, défaut `127.0.0.1` | Broker MQTT |
| `port` | non, défaut `1883` | Port du broker |
| `keepalive` | non, défaut `120` | Secondes entre deux PING |
| `user` / `password` | non | Authentification (les deux ou aucun) |
| **`topic_sub`** | oui | Topic de **souscription** |
| **`topic_pub`** | oui | Topic de **publication** |
| `path` | oui | Emplacement de la valeur dans le payload |

(dérivé, [IO/Mqtt/MqttCtrl.cpp:405-415](../src/bin/calaos_server/IO/Mqtt/MqttCtrl.cpp).)

⚠️ Les clés sont **`topic_sub`** et **`topic_pub`**. Il n'existe ni `topic` ni `topic_set`.

⚠️ **`path` n'est pas du JSONPath**, et sa syntaxe d'index de tableau n'est pas celle que
l'on croit. Le chemin est **découpé sur `/` seul**, et un jeton n'est traité comme un index que
s'il **commence par `[`** ; tout autre jeton est cherché comme **clé d'objet**
(dérivé, [IO/Mqtt/MqttCtrl.cpp:145-188](../src/bin/calaos_server/IO/Mqtt/MqttCtrl.cpp)) :

| payload | `path` correct |
|---|---|
| `{"temperature":14.23}` | `temperature` |
| `{"color":{"x":0.4}}` | `color/x` |
| `{"weather":[{"description":"pluie"}]}` | **`weather/[0]/description`** |

⚠️ **`weather[0]/description` ne fonctionne pas** : le jeton `weather[0]` ne commence pas par
`[`, il est donc cherché tel quel comme clé d'objet, `parent.at("weather[0]")` échoue, la valeur
rendue est **vide** et un avertissement `Error in path …, subpath not found` est journalisé
(dérivé, [IO/Mqtt/MqttCtrl.cpp:157,177-186](../src/bin/calaos_server/IO/Mqtt/MqttCtrl.cpp)).
C'est pourtant la forme donnée par la description du paramètre publiée à calaos_installer
(dérivé, [IO/Mqtt/MqttCtrl.cpp:415](../src/bin/calaos_server/IO/Mqtt/MqttCtrl.cpp)) — voir
`docs/refactoring/FINDINGS.md`. L'index doit être **son propre segment**.

### Paramètres spécifiques

- `MqttInputSwitch`, `MqttOutputLight` : `on_value`, `off_value`.
- `MqttOutputLight`, `MqttOutputLightDimmer`, `MqttOutputAnalog` : `data`, gabarit de payload où
  `__##VALUE##__` est substitué par la valeur.
- `MqttOutputLightDimmer` : `in_expr` / `out_expr`, expressions de conversion où `x` est la valeur
  brute (ex. `x / 2.54` pour ramener 0-254 en pourcentage).
- `MqttOutputLightRGB` : `data` (avec `__##VALUE_R##__`, `__##VALUE_G##__`, `__##VALUE_B##__`,
  `__##VALUE_HEX##__`, ou `__##VALUE_X##__` / `__##VALUE_Y##__` /
  `__##VALUE_BRIGHTNESS##__`), plus `path_x`, `path_y`, `path_brightness`.
- `MqttOutputShutter` : `topic_pub`, `topic_sub` (**optionnel** ici), `payload_open` /
  `payload_close` / `payload_stop` (défauts `OPEN` / `CLOSE` / `STOP`), `state_open` /
  `state_close` (défauts `open` / `closed`). Sans `topic_sub`, l'état est géré par la logique
  temporelle de Calaos
  (dérivé, [IO/Mqtt/MqttOutputShutter.cpp:33-41](../src/bin/calaos_server/IO/Mqtt/MqttOutputShutter.cpp)).

### MQTT RGB — l'état suit désormais le retour du broker

`MqttOutputLightRGB` ignorait le retour du broker et émettait ses événements sans vérification.
Il pose maintenant `useRealState = true` et n'émet le changement que lorsque le broker confirme la
couleur sur `topic_sub`
(dérivé, [IO/Mqtt/MqttOutputLightRGB.cpp:31-46](../src/bin/calaos_server/IO/Mqtt/MqttOutputLightRGB.cpp)).

⚠️ **Cela exige que le broker republie l'état sur `topic_sub`.** Un broker qui ne renvoie rien
laisse l'IO sans mise à jour d'état.
**Limite connue et documentée dans le code** : le payload ne porte pas d'état on/off distinct,
l'extinction est donc **déduite d'un retour de couleur noire**
(dérivé, [IO/Mqtt/MqttOutputLightRGB.cpp:35-36](../src/bin/calaos_server/IO/Mqtt/MqttOutputLightRGB.cpp)).

### Payloads non-UTF8

Un payload binaire était auparavant **supprimé en silence**. Il est désormais délivré, chaque
octet invalide remplacé par `?`, avec un avertissement
(dérivé, [IO/Mqtt/MqttExternProc_main.cpp:31,62,72-96](../src/bin/calaos_server/IO/Mqtt/MqttExternProc_main.cpp)) :
`Binary (non UTF-8) payload received, invalid bytes are replaced with '?'`.

### Topics de statut (batterie, connexion, signal…)

`MqttCtrl::subscribeStatusTopics(IOBase *io)` souscrit aux topics de statut déclarés et alimente
`IOBase::status_info` (dérivé, [IO/Mqtt/MqttCtrl.h:39-42](../src/bin/calaos_server/IO/Mqtt/MqttCtrl.h),
[IOBase.h:62,200-205](../src/bin/calaos_server/IOBase.h)). Les clés vont **par paires
`<sujet>_topic` / `<sujet>_path`**, avec parfois un `<sujet>_expr` de conversion
(dérivé, [IO/Mqtt/MqttCtrl.cpp:417-440](../src/bin/calaos_server/IO/Mqtt/MqttCtrl.cpp)) :

| sujet | clés |
|---|---|
| batterie | `battery_topic`, `battery_path`, `battery_expr` |
| connexion | `connected_status_topic`, `connected_status_path`, `connected_status_expr` |
| signal sans fil | `wireless_signal_topic`, `wireless_signal_path`, `wireless_signal_expr` |
| uptime | `uptime_topic`, `uptime_path`, `uptime_expr` |
| adresse IP | `ip_address_topic`, `ip_address_path` |
| SSID | `wifi_ssid_topic`, `wifi_ssid_path` |

Notifications associées : `notif_battery` (défaut `"true"`, sous 30 % de batterie) et
`notif_connected` (défaut `"false"`).

⚠️ Il n'existe ni `topic_battery` ni `topic_online` : les noms sont bien `battery_topic` et
`connected_status_topic`.

### MqttCtrl

```cpp
// (dérivé, IO/Mqtt/MqttCtrl.h:24-42, extrait)
void subscribeTopic(const string topic, MsgReceivedSignal callback);
void publishTopic(const string topic, const string payload);
string getValueJson(const Params &params, string path, string payload);
string getValue(const Params &params, bool &err, string topic_param, string path_param = "path");
double getValueDouble(const Params &params, bool &err);
ColorValue getValueColor(const Params &params, bool &err);
void setValue(const Params &params, bool val);
void setValueString(const Params &params, string val);
void setValueInt(const Params &params, int val, string dataParam = "data");
void setValueColor(const Params &params, ColorValue val);
bool topicMatchesSubscription(string subscription, string topic);
void subscribeStatusTopics(Calaos::IOBase *io);
```

---

## Driver GPIO

**Dossier :** [src/bin/calaos_server/IO/Gpio/](../src/bin/calaos_server/IO/Gpio/)

GPIO Linux via **le sysfs uniquement** : `GpioCtrl` écrit dans `/sys/class/gpio/export`,
`/sys/class/gpio/gpioN/direction`, `/edge`
(dérivé, [IO/Gpio/GpioCtrl.cpp:101-121](../src/bin/calaos_server/IO/Gpio/GpioCtrl.cpp)).
Il n'y a **aucun** usage de `libgpiod` dans l'arbre (vérifié par recherche sur `src/`).

### Types XML

`GpioInputSwitch`, `GpioInputSwitchLongPress`, `GpioInputSwitchTriple`, `GpioOutputSwitch`,
`GpioOutputShutter`, `GpioOutputShutterSmart`
(dérivé, les six `REGISTER_IO` de `IO/Gpio/*.cpp`).

### Paramètres — entrées

| Paramètre | Obligatoire | Description |
|---|---|---|
| **`gpio`** | oui, 0…65535 | Numéro de pin GPIO |
| **`active_low`** | non, défaut `"false"` | Niveau inversé |
| **`debounce`** | non, défaut **0,05 s** | Anti-rebond, en **secondes** |

(dérivé, [IO/Gpio/GpioInputBase.h:52-55](../src/bin/calaos_server/IO/Gpio/GpioInputBase.h).)

⚠️ Les clés sont **`gpio`** et **`active_low`**. Ni `gpio_number` ni `inverted` n'existent.

### Paramètres — sorties

- `GpioOutputSwitch` : `gpio`, `active_low`
  (dérivé, [IO/Gpio/GpioOutputSwitch.cpp:35-36](../src/bin/calaos_server/IO/Gpio/GpioOutputSwitch.cpp)).
- `GpioOutputShutter`, `GpioOutputShutterSmart` : `gpio_up`, `gpio_down`, `active_low_up`,
  `active_low_down`
  (dérivé, [IO/Gpio/GpioOutputShutterBase.h:60-63](../src/bin/calaos_server/IO/Gpio/GpioOutputShutterBase.h)).

### `debounce` est enfin honoré (T2.13, T3.2f)

La valeur était lue depuis la configuration puis **ignorée** : 0,05 s était codé en dur. Elle est
désormais transmise à `GpioCtrl`
(dérivé, [IO/Gpio/GpioInputBase.h:65-73](../src/bin/calaos_server/IO/Gpio/GpioInputBase.h)).
**Une installation qui avait réglé une valeur va voir son réglage s'appliquer.**

⚠️ **La clé s'appelle `debounce`**, en secondes. `debounce_time` est le nom de la variable C++
côté serveur, **pas** une clé de configuration : l'écrire dans `io.xml` n'a aucun effet.

Bornes et repli (dérivé,
[IO/Gpio/GpioCtrl.h:57-58,60-64,74-81](../src/bin/calaos_server/IO/Gpio/GpioCtrl.h)) :

```cpp
// (dérivé, IO/Gpio/GpioCtrl.h:57-65, intégral)
    static constexpr double DEBOUNCE_TIME_DEFAULT = 0.05;
    static constexpr double DEBOUNCE_TIME_MAX = 5.0;

    //True when v is a usable debounce time: > 0 and <= DEBOUNCE_TIME_MAX
    //seconds. NaN fails both comparisons and is rejected too.
    static bool debounceTimeValid(double v)
    {
        return v > 0.0 && v <= DEBOUNCE_TIME_MAX;
    }
```

Une valeur est retenue si elle est **strictement > 0 et ≤ 5,0 s**. Sinon — illisible, négative,
nulle, hors borne, `NaN` — on retombe sur 0,05 s **et un avertissement est journalisé**
(`Invalid debounce '<valeur>', falling back to 0.05s`). Un paramètre **absent** retombe aussi sur
0,05 s, silencieusement.

---

## Driver OneWire

**Dossier :** [src/bin/calaos_server/IO/OneWire/](../src/bin/calaos_server/IO/OneWire/)

Bus 1-Wire, principalement pour les DS18B20. Sous-processus `calaos_1wire`
(source `OWExternProc_main.cpp`), piloté par `OWCtrl`
(dérivé, [IO/OneWire/OWCtrl.cpp:27-29](../src/bin/calaos_server/IO/OneWire/OWCtrl.cpp)).

**Type XML : `OWTemp`** (classe `OWTemp`, hérite d'`InputTemp`), dérivé,
[IO/OneWire/OWTemp.cpp:32](../src/bin/calaos_server/IO/OneWire/OWTemp.cpp).

| Paramètre | Obligatoire | Description |
|---|---|---|
| `ow_id` | oui | ID du capteur sur le bus |
| `ow_args` | **oui** | Arguments d'initialisation owfs (ex. `-u` pour l'USB) |
| `use_w1` | non | Forcer le module noyau `w1` au lieu d'owfs |

(dérivé, [IO/OneWire/OWTemp.cpp:41-46](../src/bin/calaos_server/IO/OneWire/OWTemp.cpp).)

### Deux backends

Le sous-processus scanne **soit** `/sys/bus/w1/devices/w1_bus_master1/w1_master_slaves`
(mode `--use-w1`), **soit** la racine owfs via `OW_get("/")`
(dérivé, [IO/OneWire/OWExternProc_main.cpp:145-191](../src/bin/calaos_server/IO/OneWire/OWExternProc_main.cpp)).

### Capteurs nouvellement visibles (T1.17)

Le filtre qui distingue un **device** d'un dossier virtuel owfs (`alarm/`, `bus.0/`,
`settings/`) avait des bornes strictes qui écartaient à tort les familles commençant par `0`,
`9`, `A` ou `F`. Les bornes sont désormais inclusives :

```cpp
// (dérivé, IO/OneWire/OWFSUtils.h:36-43, intégral)
inline bool entryIsDevice(const std::string &s)
{
    if (s.empty())
        return false;
    const char c = s[0];
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F');
}
```

**Des capteurs jusque-là invisibles peuvent donc apparaître** — c'est assumé.

⚠️ Deux précisions qui comptent :
- Le filtre est **hexadécimal MAJUSCULE, délibérément pas `isxdigit()`** : les adresses owfs sont
  en majuscules, les dossiers virtuels en minuscules (`alarm`, `bus.0` commencent par les
  chiffres hexa `a`/`b`). Passer à `isxdigit()` reclasserait ces dossiers en devices
  (dérivé, [IO/OneWire/OWFSUtils.h:32-35](../src/bin/calaos_server/IO/OneWire/OWFSUtils.h)).
- Le filtre ne s'applique **qu'au chemin owfs**. Le mode `--use-w1` lit la liste des esclaves
  du noyau et n'y passe pas
  (dérivé, [IO/OneWire/OWExternProc_main.cpp:150-166,183-186](../src/bin/calaos_server/IO/OneWire/OWExternProc_main.cpp)).

---

## Driver OLA (Open Lighting Architecture)

**Dossier :** [src/bin/calaos_server/IO/OLA/](../src/bin/calaos_server/IO/OLA/)

DMX512 via OLA. Sous-processus `calaos_ola` (source `OLAExternProc_main.cpp`), piloté par
`OLACtrl` (dérivé, [IO/OLA/OLACtrl.cpp:27-29](../src/bin/calaos_server/IO/OLA/OLACtrl.cpp)).

| Type XML | Base | Paramètres |
|---|---|---|
| `OLAOutputLightDimmer` | `OutputLightDimmer` | `universe` (0…9999), `channel` (0…512) |
| `OLAOutputLightRGB` | `OutputLightRGB` | `universe`, `channel_red`, `channel_green`, `channel_blue` |

(dérivé, [IO/OLA/OLAOutputLightDimmer.cpp:25,34-35](../src/bin/calaos_server/IO/OLA/OLAOutputLightDimmer.cpp),
[IO/OLA/OLAOutputLightRGB.cpp:25,34-37](../src/bin/calaos_server/IO/OLA/OLAOutputLightRGB.cpp).)

---

## Driver Hue

**Dossier :** [src/bin/calaos_server/IO/Hue/](../src/bin/calaos_server/IO/Hue/)

Ampoules Philips Hue via l'API REST du bridge. **Type XML : `HueOutputLightRGB`**
(dérivé, [IO/Hue/HueOutputLightRGB.cpp:30](../src/bin/calaos_server/IO/Hue/HueOutputLightRGB.cpp)).

| Paramètre | Obligatoire | Description |
|---|---|---|
| `host` | oui | IP du bridge Hue |
| **`api`** | oui | Clé API rendue par le bridge à l'association (assistant Hue de calaos_installer) |
| **`id_hue`** | oui | Identifiant unique de l'ampoule, rendu par l'assistant Hue |
| `insecure` | non, défaut `"true"` | Voir [§ option `insecure`](#option-insecure--tls-des-urls-configurées-par-lutilisateur) |

(dérivé, [IO/Hue/HueOutputLightRGB.cpp:38-43](../src/bin/calaos_server/IO/Hue/HueOutputLightRGB.cpp).
Les clés sont `api` et `id_hue` — ni `api_key` ni `light_id` n'existent.)

---

## Driver LAN

**Dossier :** [src/bin/calaos_server/IO/LAN/](../src/bin/calaos_server/IO/LAN/)

| Type XML | Base | Paramètres |
|---|---|---|
| `PingInputSwitch` | `InputSwitch` | `host` (obligatoire), `timeout` (ms), `interval` (ms, défaut `15000`) |
| `WOLOutputBool` | `IOBase` | `address` (MAC de l'hôte à réveiller, obligatoire), `interval` (ms, défaut `15000`) |

(dérivé, [IO/LAN/PingInputSwitch.cpp:28,36-38](../src/bin/calaos_server/IO/LAN/PingInputSwitch.cpp),
[IO/LAN/WOLOutputBool.cpp:27,35-36](../src/bin/calaos_server/IO/LAN/WOLOutputBool.cpp).)

---

## Driver Web

**Dossier :** [src/bin/calaos_server/IO/Web/](../src/bin/calaos_server/IO/Web/)

IOs adossés à une requête HTTP GET ou POST — ou à un **fichier local**, si l'URL commence par `/`
ou `file://`
(dérivé, [IO/Web/WebDocBase.cpp:33-34,48-49](../src/bin/calaos_server/IO/Web/WebDocBase.cpp)).

### Types XML

`WebInputAnalog`, `WebInputTemp`, `WebInputString`, `WebOutputAnalog`, `WebOutputLight`,
`WebOutputLightRGB`, `WebOutputString`
(dérivé, les sept `REGISTER_IO` de `IO/Web/*.cpp`).

### Paramètres

**En lecture** (dérivé, [IO/Web/WebDocBase.cpp:48-69](../src/bin/calaos_server/IO/Web/WebDocBase.cpp)) :

| Paramètre | Description |
|---|---|
| `url` | URL (ou chemin de fichier) d'où télécharger le document |
| `file_type` | `xml`, `json` ou `text` |
| `path` | Emplacement de la valeur — sa syntaxe **dépend de `file_type`** |

- `file_type=json` → chemin découpé sur `/`, **l'index de tableau étant son propre segment
  entre crochets** : `weather/[0]/description`, et non `weather[0]/description`. Le parseur est
  le même que celui de MQTT — mêmes règles, même piège
  (dérivé, [IO/Web/WebCtrl.cpp:184-227](../src/bin/calaos_server/IO/Web/WebCtrl.cpp)) ;
- `file_type=xml` → **expression XPath** ;
- `file_type=text` → `ligne/pos/séparateur` : la ligne est découpée par le séparateur et la valeur
  d'indice `pos` est rendue ; si le séparateur est absent, la ligne entière est rendue.

**En écriture** (dérivé, [IO/Web/WebDocBase.cpp:29-44](../src/bin/calaos_server/IO/Web/WebDocBase.cpp)) :

| Paramètre | Description |
|---|---|
| `url` | URL de POST ; `__##VALUE##__` y est substitué par la valeur quand `data` est vide |
| `data` | Corps du POST ; `__##VALUE##__` y est substitué par la valeur |
| `data_type` | En-tête `Content-Type` du POST |

Et dans les deux cas `insecure` (défaut `"true"`, voir plus haut).

### XPath : les expressions rendent enfin les bonnes valeurs (E4.4b)

Le moteur XPath est passé de TinyXPath (non maintenu) à pugixml. Des configurations
**silencieusement cassées** se remettent à fonctionner : sélection d'un **élément** sans
`text()`, chemins relatifs multi-étapes, fonctions `string()` / `number()` / `boolean()` /
`local-name()` / `round()`. **Un seul cas change une valeur qui « marchait »** :
l'arithmétique, jadis tronquée en entier et débordant en int32, est désormais en flottant
conforme à XPath 1.0 (`//temp/@value + 0` rend `21.5` et non plus `21`). Voir
`docs/refactoring/RELEASE_NOTES.md`.

---

## Driver Reolink

**Dossier :** [src/bin/calaos_server/IO/Reolink/](../src/bin/calaos_server/IO/Reolink/)

Événements des caméras Reolink, via le sous-processus `calaos_reolink` piloté par `ReolinkCtrl`
(dérivé, [IO/Reolink/ReolinkCtrl.cpp:34-35](../src/bin/calaos_server/IO/Reolink/ReolinkCtrl.cpp)).

**Type XML : `ReolinkInputSwitch`** (hérite d'`InputSwitch`), dérivé,
[IO/Reolink/ReolinkInputSwitch.cpp:31](../src/bin/calaos_server/IO/Reolink/ReolinkInputSwitch.cpp).

| Paramètre | Obligatoire | Description |
|---|---|---|
| `hostname` | oui | IP ou nom d'hôte de la caméra |
| `username` / `password` | oui | Authentification |
| `event_type` | oui, défaut `motion` | Type d'événement écouté (liste fermée) |

(dérivé, [IO/Reolink/ReolinkInputSwitch.cpp:40-54](../src/bin/calaos_server/IO/Reolink/ReolinkInputSwitch.cpp).
⚠️ La clé est `hostname`, pas `host`.)

Ce driver est distinct des caméras IP : il ne fournit **pas** de flux vidéo. Pour les caméras,
voir [06_ipcam.md](06_ipcam.md).

---

## Driver RemoteUI (IOs embarqués)

**Dossier :** [src/bin/calaos_server/IO/RemoteUI/](../src/bin/calaos_server/IO/RemoteUI/)

IOs portés par les appareils RemoteUI — tableaux de bord embarqués reliés au serveur par
WebSocket. Voir [07_remoteui.md](07_remoteui.md) pour le protocole.

| Type XML | Classe | Base |
|---|---|---|
| `RemoteUI` | `RemoteUI` | `IOBase` — l'appareil lui-même |
| `RemoteUIOutputRelay` | `RemoteUIOutputRelay` | `OutputLight` — un relais de l'appareil |

(dérivé, [IO/RemoteUI/RemoteUI.cpp:84](../src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp),
[IO/RemoteUI/RemoteUIOutputRelay.cpp:29](../src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.cpp).)

Paramètres de `RemoteUIOutputRelay` : `remote_ui_id` (id de l'appareil parent) et `relay_num`
(numéro de relais, 1…99), tous deux obligatoires
(dérivé, [IO/RemoteUI/RemoteUIOutputRelay.cpp:37-38](../src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.cpp)).

Paramètres notables de `RemoteUI` : `device_type` (liste fermée, défaut `waveshare-86-panel`),
`provisioning_code`, `grid_w`, `grid_h`, plus les informations remontées par l'appareil
(`device_manufacturer`, `device_platform`, `device_version`, `mac_address`, `auth_token`,
`device_secret`) — dérivé,
[IO/RemoteUI/RemoteUI.cpp:99-108](../src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp).

Les deux sens de communication d'un relais :

```cpp
// (dérivé, IO/RemoteUI/RemoteUIOutputRelay.cpp:47-56, extrait)
bool RemoteUIOutputRelay::set_value_real(bool val)   // serveur → appareil
{
    RemoteUIManager::Instance().sendCommand(remote_ui_id, "remote_ui_set_relay", …);
}

void RemoteUIOutputRelay::updateStateFromDevice(bool val);  // appareil → serveur, sans reboucler
```

⚠️ Le nom de la commande envoyée est **`remote_ui_set_relay`** (et non `relay_set`).

---

## Drivers retirés

**MySensors** (T2.12) et **Gadspot** (T3.6) ont été **supprimés** de la base de code : plus aucun
utilisateur. Vérifié : aucun fichier de `src/` ni de `tests/` ne les mentionne.

**Ce que devient une configuration qui les référence encore** — comportement vérifié au code, pas
seulement annoncé :

1. Le serveur **démarre normalement** ; tous les autres IOs, règles et scénarios se chargent.
2. Le type inconnu est **ignoré** avec un avertissement au journal :
   `<type>: Unknown Input type !`
   (dérivé, [IO/IOFactory.cpp:51](../src/bin/calaos_server/IO/IOFactory.cpp)).
3. ⚠️ **L'IO est effacé du fichier, et un simple redémarrage y suffit.** N'ayant jamais été
   ajouté à sa pièce, il n'existe pour aucun écrivain : `Config::SaveConfigIO()` est l'**unique**
   producteur d'`io.xml` et reconstruit un document **neuf** à partir des seules pièces de
   `ListeRoom` (dérivé, [CalaosConfig.cpp:313-330](../src/bin/calaos_server/CalaosConfig.cpp),
   [Room.cpp:187-200](../src/bin/calaos_server/Room.cpp)) ; aucun mécanisme de préservation du
   XML inconnu n'existe (vérifié : `rawXml` / `preserveUnknown` → 0 occurrence dans `src/`).
   Et la réécriture n'attend **aucune action de l'utilisateur** : `main.cpp:196` planifie
   `ListeRoom::checkAutoScenario()` **0,1 s après le démarrage**, qui se termine par
   `SaveConfigIO()` (dérivé, [ListeRoom.cpp:339-341](../src/bin/calaos_server/ListeRoom.cpp)).
   **Neuf autres** sites la déclenchent : huit dans l'API JSON (ajout/suppression d'IO,
   opérations de pièce) et un dans le provisioning RemoteUI. **Relever les équipements concernés
   avant la mise à jour.**
4. Une règle ou un scénario qui **référençait** cet IO tombe sous la règle générale de la
   dépendance manquante : **la règle est désactivée** (E4.2e) et **le scénario est désactivé**
   (T3.18). Voir [03_rules_engine.md](03_rules_engine.md) et [04_scenarios.md](04_scenarios.md).

Il n'y a **aucune migration** : un équipement MySensors ou Gadspot doit être remplacé dans la
configuration.

---

## Ajouter un nouveau driver IO

1. Créer un dossier `src/bin/calaos_server/IO/MonDriver/`.
2. Écrire la classe, héritée du type générique approprié (`OutputLight`, `InputSwitch`,
   `InputAnalog`…) — ou du mixin `ThinIo<Base>` si le driver est mince.
3. Implémenter le constructeur prenant `Params &`, la méthode de sortie pure virtuelle de la
   famille (`set_value_real()`) et, pour une entrée, l'appel à `hasChanged()` quand la valeur
   matérielle bouge.
4. Remplir `ioDoc` **dans le constructeur** : `friendlyNameSet()`, `descriptionSet()`,
   `paramAdd*()`, `actionAdd()`, `conditionAdd()`. C'est cette description qui alimente
   `--gendoc` **et** l'interface de calaos_installer — un paramètre non déclaré ici est invisible
   pour l'installeur.
5. Si le driver joint une URL configurée par l'utilisateur, déclarer `insecure`
   (défaut `"true"`) et appeler `setInsecureFromParam(get_param("insecure"))` sur **chaque**
   `UrlDownloader` — pas seulement sur le chemin principal.
6. Appeler `REGISTER_IO(MaClasse)` au niveau fichier dans le `.cpp`. Le nom devient le type XML
   et **ne pourra plus changer** : c'est l'ABI des `io.xml` déjà déployés. Pour un second nom,
   utiliser `REGISTER_IO_USERTYPE(AutreNom, MaClasse)`.
7. Ajouter les fichiers au `Makefile.am` (et à `po/POTFILES.in` si des chaînes sont traduites).
