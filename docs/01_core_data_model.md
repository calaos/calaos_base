# Core Data Model — IOBase, Room, ListeRoom, IOFactory

> **Convention de traçabilité** (E4.0f, appliquée ici par E4.5a) : chaque affirmation
> vérifiable porte sa provenance — **(capturé, `tests/…`)** = octets produits par le code
> et figés, **(dérivé, `Fichier.cpp:L`)** = forme lue dans le source.

## Vue d'ensemble

Le modèle de données central organise les entrées/sorties (IOs) en pièces (Rooms), gérées
globalement par `ListeRoom`. La fabrique `IOFactory` instancie les IOs par leur type string
(auto-enregistrement au démarrage).

---

## Modèle de propriété (série E4.2)

C'est l'invariant à connaître avant tout le reste, parce qu'il décide qui a le droit de
faire un `delete`.

```
ListeRoom  ──owns──►  Room  ──owns──►  IOBase
   │                                     ▲
   └─ io_table / cameraCache / audioCache ┘   (NON-POSSÉDANTS)

ListeRule  ──owns──►  Rule  ──owns──►  Condition / Action
   │                    ▲
   └─ rules_scenarios ──┘                      (NON-POSSÉDANT)
```

- **`Room` possède ses IOs** en `vector<std::unique_ptr<IOBase>>` (E4.2b, dérivé,
  [Room.h:42-57](../src/bin/calaos_server/Room.h)). Détruire la pièce détruit ses IOs, et
  aucun autre conteneur de l'arbre n'a le droit de les détruire.
- **`ListeRoom` possède les `Room`** en `vector<std::unique_ptr<Room>>` ; `io_table`,
  `cameraCache` et `audioCache` sont des **index non-possédants**, tenus à jour non pas par
  `ListeRoom` mais par `IOBase` lui-même — son constructeur appelle `addIOHash()`, son
  destructeur `delIOHash()` (dérivé,
  [ListeRoom.h:41-61](../src/bin/calaos_server/ListeRoom.h),
  [IOBase.cpp:66-80](../src/bin/calaos_server/IOBase.cpp)). Une entrée d'index ne peut donc
  pas survivre à l'objet qu'elle désigne, quel que soit le chemin de destruction.
- **`Rule` possède ses `Condition`/`Action`**, **`ListeRule` possède ses `Rule`** (E4.2d,
  dérivé, [Rule.h:61-81](../src/bin/calaos_server/Rule.h),
  [ListeRule.h:45-71](../src/bin/calaos_server/ListeRule.h)). `rules_scenarios` est un
  **index non-possédant** sur les mêmes `Rule`.
- **Les `Condition`/`Action` référencent l'IO par son `id`, plus par pointeur** (E4.2c) : un
  id qui ne résout plus est un `nullptr` franc rendu par `ListeRoom::findIO()`, traité une
  fois dans `Evaluate()` — et une condition dont un id manque est **fausse** (fail closed),
  jamais ignorée (dérivé,
  [Rules/ConditionStd.h:45-58, 73-78](../src/bin/calaos_server/Rules/ConditionStd.h)).
- Les back-pointers d'`AutoScenario` vers les `Rule` d'étape passent par un **jeton de vie**
  (`RuleRef`, un `weak_ptr<bool>` pris sur `Rule::aliveToken()`) : la référence devient nulle
  d'elle-même à la destruction de la règle (E4.2f, dérivé,
  [Scenario/AutoScenario.h:46-95](../src/bin/calaos_server/Scenario/AutoScenario.h)).

**Deux exceptions, et deux seulement**, sont des *transferts* de propriété hors de la pièce,
implémentés par `release()` et jamais par `reset()` : `Room::RemoveIO(pos, del=false)` et
`Room::RemoveIOFromRoom(io)` (dérivé,
[Room.cpp:75-126](../src/bin/calaos_server/Room.cpp)). Tout autre `IOBase*` rendu par une
API de ce document est une **observation non-possédante**.

---

## IOBase

**Fichier :** [src/bin/calaos_server/IOBase.h](../src/bin/calaos_server/IOBase.h)

Classe de base abstraite pour toute entrée ou sortie. Chaque IO est un nœud du graphe de
domotique.

### Attributs clés

| Attribut | Type | Description |
|---|---|---|
| `param` | `Params` | Map string→string de tous les paramètres de l'IO |
| `io_type` | `int` | `IO_UNKNOWN` (défaut), `IO_INPUT`, `IO_OUTPUT` ou `IO_INOUT` |
| `status_info` | `StatusInfo` | Infos de statut : batterie, connexion, signal Wi-Fi, uptime |
| `ioDoc` | `IODoc *` | Documentation auto-générée de l'IO (pour l'interface) |
| `ascenario` | `AutoScenario *` | Back-pointer **non-possédant**, posé sur le seul IO `<scenario>_schedule` ; `~AutoScenario()` le remet à `nullptr` (E4.2f) |
| `hashRegistered` | `bool` | Vrai si le constructeur a bien enregistré cet IO dans `io_table` — seule condition pour que le destructeur ou `renameId()` y touchent |

### Méthodes virtuelles importantes

(dérivé, [IOBase.h:100-171](../src/bin/calaos_server/IOBase.h))

```cpp
virtual DATA_TYPE get_type() = 0;        // TBOOL, TINT ou TSTRING
virtual bool get_value_bool();
virtual double get_value_double();
virtual string get_value_string();
virtual bool set_value(bool val);
virtual bool set_value(double val);
virtual bool set_value(string val);
virtual void hasChanged();               // appelé quand la valeur change
virtual bool LoadFromXml(pugi::xml_node);
virtual bool SaveToXml(pugi::xml_node);
```

Les deux dernières prennent un `pugi::xml_node` depuis la migration E4.4 ; c'est un
renommage direct des anciennes signatures `TiXmlElement *`.

### DATA_TYPE

Défini dans [src/lib/Utils.h:227](../src/lib/Utils.h) :

```cpp
typedef enum { TBOOL, TINT, TSTRING, TUNKNOWN } DATA_TYPE;
```

- `TBOOL` — valeur booléenne (switch, lumière on/off)
- `TINT` — valeur numérique, **rendue en `double`** par `get_value_double()` malgré son nom
  (température, gradateur, volet)
- `TSTRING` — valeur chaîne (player audio, caméra)
- `TUNKNOWN` — sentinelle

> ⚠️ Il n'existe **pas** de `TDOUBLE`. Le nom numérique est `TINT`, et c'est lui qu'il faut
> tester (`io->get_type() == TINT`, dérivé,
> [JsonApi.cpp:270, 279](../src/bin/calaos_server/JsonApi.cpp)).

### Paramètres standard (dans `Params`)

Ce sont les huit paramètres que `IOBase` déclare pour tous ses descendants dans son
constructeur (dérivé, [IOBase.cpp:52-64](../src/bin/calaos_server/IOBase.cpp)) :

| Clé | Valeur | Obligatoire | Description |
|---|---|---|---|
| `id` | string | oui | Identifiant unique de l'IO, **lecture seule** après création |
| `name` | string | oui | Nom affiché |
| `io_type` | `"input"`/`"output"`/`"inout"` | — | Posé par le constructeur d'après le type C++, jamais lu depuis le XML |
| `visible` | `"true"/"false"` | non | Affiché ou non par les UIs ; défaut `"true"` |
| `enabled` | `"true"/"false"` | non | IO actif ou non ; **ajouté à `"true"` s'il est absent** de la config |
| `gui_type` | string | non | Type d'affichage UI, **lecture seule** : chaque driver le force dans son constructeur (`light`, `camera`, `audio_player`, `var_bool`…) |
| `log_history` | `"true"/"false"` | non | Écrire une entrée dans le journal d'événements pour cet IO ; défaut `"false"` |
| `logged` | `"true"/"false"` | non | Envoyer la valeur à InfluxDB si configuré ; défaut `"false"` |

Le paramètre `type` (le nom enregistré dans `IOFactory`) vient de l'attribut XML et est repris
tel quel dans `Params` par `IOFactory::readParams()`
(dérivé, [IO/IOFactory.cpp:30-36](../src/bin/calaos_server/IO/IOFactory.cpp)).

> ⚠️ **`var_type` n'est pas un paramètre.** C'est un champ **calculé** de l'API JSON, dérivé
> de `get_type()` au moment de la sérialisation : `TINT` → `"float"`, `TBOOL` → `"bool"`,
> `TSTRING` → `"string"` (dérivé,
> [JsonApi.cpp:277-282](../src/bin/calaos_server/JsonApi.cpp)). Le chercher dans `Params`
> ne donne rien. Visible sur le fil (capturé,
> `tests/core/golden/e40b_ws_get_io.json`, extrait) :
> ```json
> "e40_int": {
>   "gui_type": "var_int", "id": "e40_int", "io_type": "inout",
>   "name": "Int value", "rw": "false", "state": "0",
>   "type": "InternalInt", "var_type": "float", "visible": "true"
> }
> ```

### Signal de changement

(dérivé, [IOBase.cpp:158-163](../src/bin/calaos_server/IOBase.cpp))

```cpp
void EmitSignalIO();  // → ListeRule::ExecuteRuleSignal(id)
                      // → DataLogger::Instance().log(this)
```

Les deux effets comptent : le second est ce qui alimente InfluxDB pour les IOs marqués
`logged`.

**Dernière lecture.** `lastReading()` donne l'instant (`steady_clock`) de la dernière valeur
reçue depuis le démarrage, vide avant ; une valeur restaurée du cache d'état n'en est pas une.
`EmitSignalIO()` l'horodate, ainsi que `markReading()` (protégé), qu'appellent les drivers qui
écartent sans la signaler une valeur égale à la précédente, là où la lecture arrive :
`MqttInputAnalog`/`MqttInputTemp` sur un message, `WIAnalog`/`WITemp` sur une réponse
Modbus, `OWTemp` quand le processus 1-Wire a renvoyé une valeur pour sa sonde (compteur par
sonde dans `OwCtrl`), `KNXInputAnalog`/`KNXInputTemp` sur un télégramme de leur groupe,
`WebInputAnalog`/`WebInputTemp` sur un téléchargement 2xx contenant un nombre. Pas dans
`InputAnalog` : sa relecture périodique (`hasChanged()` → `readValue()`) relit le cache du
driver et ne distingue pas une réception d'une relecture.
Un message MQTT **retenu** (rejoué par le broker à chaque connexion ; ESPHome publie ses états
retenus) n'est pas une lecture : `calaos_mqtt` transmet le drapeau (`"retain": true` sur le
fil, absent sinon), `MqttCtrl::replayingRetained()` le dit pendant la distribution, et
`MqttInputAnalog`/`MqttInputTemp` prennent la valeur sans dater de lecture
(`IOBase::replayed` empêche `EmitSignalIO()` de dater le changement qu'il signale).
`requestReading()` demande une lecture à l'appareil quand son protocole le permet : une
lecture de groupe pour `KNXInputAnalog`/`KNXInputTemp` (une seule en vol par IO, la suivante
attend un télégramme du groupe ou 10 s), rien ailleurs.
`datesEveryReading()` (faux par défaut) dit si le driver date toute lecture : vrai pour ces
drivers et pour les IOs internes (chaque écriture est signalée), faux pour un `WebInput*` sur
fichier local (relu à chaque période, réécrit ou non). Un thermostat ou un proxy journalise
au chargement une sonde (`current_temp`, `humidity` d'un plancher, `remote_temp_source`) qui
ne date que ses changements : stable `sensor_timeout` secondes, elle compte comme muette.

### StatusInfo

Infos de statut pour les périphériques IoT (dérivé,
[IOBase.h:38-62](../src/bin/calaos_server/IOBase.h)) :

```cpp
enum class StatusConnected { STATUS_NONE = 0, STATUS_DISCONNECTED = 1, STATUS_CONNECTED = 2 };

struct StatusInfo {
    double battery_level = 0.0;          // pourcentage
    bool battery_level_set = false;      // vrai dès qu'un niveau a été rapporté
                                         // (0 % est une lecture valide, d'où ce drapeau)
    StatusConnected connected = StatusConnected::STATUS_NONE;
    double wireless_signal = 0.0;        // pourcentage
    uint64_t uptime = 0;                 // secondes
    string ip_address;
    string wifi_ssid;
};
```

Quatre setters surchargés, un par forme de valeur :
```cpp
void setStatusInfo(StatusType type, double value);
void setStatusInfo(StatusType type, const string &value);
void setStatusInfo(StatusType type, StatusConnected value);
void setStatusInfo(StatusType type, uint64_t value);
```

`hasStatusInfo()` est un OU sur les six champs, `getStatusInfo()` rend le tout sous forme de
`Params` (dérivé, [IOBase.h:198-208](../src/bin/calaos_server/IOBase.h)).

⚠️ Le test est **le drapeau** pour la batterie, mais **la valeur** pour les autres :
`wireless_signal == 0.0` et `uptime == 0` sont indistinguables de « jamais renseigné ». C'est
exactement la confusion que `battery_level_set` a été ajouté pour lever côté batterie, et elle
subsiste sur les deux autres champs numériques.

---

## Room

**Fichier :** [src/bin/calaos_server/Room.h](../src/bin/calaos_server/Room.h)

Conteneur d'IOs représentant une pièce physique ou logique, et **propriétaire unique** de
ses IOs.

```cpp
class Room {
    string name;       // nom de la pièce
    string type;       // type ("living", "bedroom", …)
    int hits;          // compteur d'accès (tri UI)
    vector<std::unique_ptr<IOBase>> ios;   // E4.2b : possession
};
```

| Méthode | Contrat |
|---|---|
| `void AddIO(IOBase *p)` | **Prend la propriété** de `p`. Un `p` nul est ignoré et journalisé |
| `void RemoveIO(int i, bool del = true)` | `del=true` : détruit l'IO. `del=false` : **transfère** la propriété à l'appelant (`release()`). Hors bornes = no-op journalisé. Lève un `EventIODeleted` dans les deux cas |
| `void RemoveIOFromRoom(IOBase *io)` | **Transfère** la propriété hors de la pièce sans détruire (chemin « déplacer un IO vers une autre pièce » de l'API JSON). Lève un `EventRoomChanged` |
| `IOBase *get_io(int i)` | Observation non-possédante, **`nullptr` hors bornes** |
| `bool LoadFromXml(pugi::xml_node)` | Instancie les IOs enfants via `IOFactory` |

`~Room()` détache chaque IO des règles avec la politique **`RuleDetachPolicy::Destroy`**
explicite — la pièce et ses IOs disparaissent pour de bon, il n'y a personne à prévenir et
rien à réactiver (dérivé, [Room.cpp:36-58](../src/bin/calaos_server/Room.cpp)).

---

## ListeRoom

**Fichier :** [src/bin/calaos_server/ListeRoom.h](../src/bin/calaos_server/ListeRoom.h)

Singleton. Registre global de toutes les pièces et de tous les IOs.

### Accesseurs de résolution (E4.2a)

Point d'entrée unique pour toute question « donne-moi l'IO d'id X ».

```cpp
IOBase *findIO(const std::string &id) const;   // nullptr si inconnu
bool    hasIO(const std::string &id) const;    // test de présence, ne rend aucun pointeur
template<typename T> T *findIOAs(const std::string &id) const;  // findIO + dynamic_cast
IOBase *findIOByIndex(int index);              // ordre pièces × ordre intra-pièce
Room   *findRoomOfIO(const std::string &id);   // findIO + getRoomByIO, sans déréférencement
```

Le contrat, sur lequel le reste de l'arbre s'appuie (dérivé,
[ListeRoom.h:85-126](../src/bin/calaos_server/ListeRoom.h)) :

- le pointeur rendu est **non-possédant** : l'IO appartient à sa `Room` ;
- un id inconnu donne `nullptr`, **explicitement** — la recherche n'insère jamais dans
  `io_table` (pas d'`operator[]` sur une clé absente) et ne déréférence jamais ce qu'elle
  trouve ;
- un id **vide n'est pas une identité** : il résout toujours à `nullptr`, y compris dans le
  cas dégénéré d'un `io.xml` malformé ayant poussé un IO sans id sous la clé `""` ;
- la résolution est **par id seulement** : elle ne parcourt jamais les pièces, donc un IO
  qu'`addIOHash()` a rejeté comme doublon est invisible ici (le premier enregistré reste
  celui qui fait autorité).

`findIOByIndex()` fixe l'**ordre d'itération des IOs de tout le serveur** (pièces dans leur
ordre de déclaration, puis IOs dans leur ordre intra-pièce). Cet ordre ne doit pas changer :
il décide de l'ordre dans lequel les règles voient leurs entrées.

### Noms historiques

Conservés pour que les ~60 sites d'appel existants restent intacts ; ce sont de simples
délégations (dérivé, [ListeRoom.cpp:224-232](../src/bin/calaos_server/ListeRoom.cpp)) :

```cpp
IOBase *get_io(std::string id);   // → findIO(id)
IOBase *get_io(int i);            // → findIOByIndex(i)
int get_io_count();               // io_table.size() : le nombre d'IOs INDEXÉS,
                                  // pas le nombre d'IOs attachés aux pièces
```

### Table de hachage et caches

```cpp
unordered_map<string, IOBase *> io_table;   // id → IOBase*
list<IOBase *> cameraCache;                 // IOs dont gui_type == "camera"
list<IOBase *> audioCache;                  // IOs dont gui_type == "audio_player"
list<Scenario *> auto_scenario_cache;
```

Tous **non-possédants**. `addIOHash()` refuse un id déjà pris (le premier reste autoritaire,
la collision est journalisée) et range l'IO dans `cameraCache`/`audioCache` d'après son seul
`gui_type` ; `delIOHash()` n'efface l'entrée `io_table` que si elle pointe encore sur **cet**
IO — sinon un IO rejeté à l'enregistrement effacerait, en mourant, l'entrée de son homonyme
vivant (dérivé, [ListeRoom.cpp:60-113](../src/bin/calaos_server/ListeRoom.cpp)).

`getCameraList()` / `getAudioList()` rendent une **copie** de `list<IOBase *>`. Le type
d'élément *et* le retour par valeur font partie du contrat de payload de `JsonApi` : un
`static_assert` de `tests/core/ListeRoomIdResolution_test.cpp:335-338` les épingle, pour que
la conversion à la propriété ne les transforme pas en smart pointers.

### Création / suppression d'IO

```cpp
IOBase *createIO(Params param, Room *room);
bool    delete_io(IOBase *io, bool del = true);
void    detachIOFromRules(IOBase *io, bool modify = false,
                          RuleDetachPolicy policy = RuleDetachPolicy::Disable);
bool    deleteIO(IOBase *io, bool modify = false,
                 RuleDetachPolicy policy = RuleDetachPolicy::Disable);
```

- `createIO()` construit via `IOFactory` et **donne la propriété à `room`**. Rend `nullptr`
  si la création a échoué (type inconnu) ou si l'id a été rejeté comme doublon — et dans ce
  dernier cas l'IO à moitié construit est détruit ici, jamais laissé attaché à la pièce
  (dérivé, [ListeRoom.cpp:480-520](../src/bin/calaos_server/ListeRoom.cpp)). Un `room` nul
  est refusé et journalisé.
- `delete_io()` est la moitié **propriété** : elle retire l'IO de la pièce qui le possède.
  `del=false` **transfère** la propriété à l'appelant (l'IO reste résolvable par id).
  Elle rend `false` quand aucune pièce ne possède l'IO — et alors **rien n'a été détruit**,
  donc l'appeler deux fois est sans danger. Avec `del=true` elle est aussi refusée pendant
  la marche des règles, comme `deleteIO()` ; `del=false` (déplacement) reste permis.
- `detachIOFromRules()` est la moitié **règles** : elle traite les règles qui utilisent cet
  IO et le sort de la liste de scrutation, **sans toucher à sa propriété**. Elle est publique
  parce que `~Room()` doit l'exécuter pour chacun de ses IOs.
- `deleteIO()` est l'enchaînement des deux, c'est-à-dire le chemin complet « l'utilisateur a
  supprimé cet IO » de l'API JSON. Pour un `Device`, chaque IO possédé est d'abord détaché
  et détruit de la même façon ; les IOs externes liés ne sont pas touchés.

> ⚠️ **`modify` ne veut pas dire « modifier la config ».** `modify = true` signifie *cet IO
> est en cours d'édition* : dans ce cas `ListeRule::RemoveRule()` n'est **pas** appelée, donc
> aucune règle n'est ni désactivée ni détruite. Le retrait de la liste de scrutation
> (`ListeRule::Remove(io)`), lui, a lieu **dans tous les cas** — il est piloté par « qui s'est
> enregistré », pas par le drapeau (dérivé,
> [ListeRoom.cpp:378-402](../src/bin/calaos_server/ListeRoom.cpp)).

> **`policy`** est passée à `ListeRule::RemoveRule()`. Le défaut, **`Disable`**, ne détruit
> aucune règle : elle est conservée intacte et seulement marquée comme référençant un IO
> disparu — décision utilisateur, voir [03_rules_engine.md](03_rules_engine.md). `Destroy`
> est le comportement historique, et il doit rester **explicite** aux sites de démontage.
> Après une suppression réussie en mode `Disable`, `refreshBrokenScenarios()` désactive les
> auto-scénarios devenus cassés (T3.18) — voir [04_scenarios.md](04_scenarios.md).

---

## Device

**Fichier :** [src/bin/calaos_server/IO/Device.h](../src/bin/calaos_server/IO/Device.h) —
spec : [CALAOS_DEVICES_FEAT.md](../CALAOS_DEVICES_FEAT.md) §3 et §4.

Sous-classe d'`IOBase` déclarée par `<calaos:device id type impl name>`, enfant d'une pièce.
`type` est la famille (`climate`), `impl` choisit la classe : `IOFactory` la cherche sous la
clé `device:<impl>`, enregistrée par `REGISTER_DEVICE(impl, Classe)`.

- Un `<calaos:device>` que ce build ne sait pas instancier (impl inconnue ou absente) est
  journalisé et gardé **tel quel** : sous-arbre XML brut réécrit à l'identique à la
  sauvegarde, à sa place dans la pièce, sans IO ni enfants instanciés (cas d'un retour à une
  version plus ancienne). Son id et ceux de ses éléments imbriqués sont **réservés** :
  `ListeRoom::isIdReserved()` les refuse à `get_new_id()` et à `createIO()`, sans quoi un IO
  créé après coup entrerait en collision avec le fichier. Il compte comme un device pour le
  format 2 et pour le refus de `config put`.
- Un élément ordinaire qui donne un `Device` (attribut `impl`) est refusé.
- Tout IO construit puis refusé passe par `ListeRoom::discardUnattachedIO()` : InputTime,
  InPlageHoraire et InputAnalog s'inscrivent dans la boucle d'événements des règles dès
  leur constructeur, et leur destructeur ne s'en retire pas.

```xml
<calaos:device id="salon_ac" type="climate" impl="proxy" name="Clim salon">
  <calaos:internal id="salon_ac_mode" type="InternalString" visible="false"/>  <!-- possédé -->
  <calaos:link name="mode" io="salon_ac_mode"/>
  <calaos:link name="current_temp" io="salon_temp"/>  <!-- externe -->
  <calaos:param name="hysteresis" value="0.5"/>
</calaos:device>
```

- **IOs possédés** : `input`/`output`/`internal` imbriqués, créés par `IOFactory` puis
  ajoutés à la liste plate de la pièce, juste après le device. Règles, cache d'état,
  `get_home` et MCP les voient comme tout IO. Chacun porte l'id de son device
  (`IOBase::getOwnerDevice()`, un membre et pas un param). Un `<calaos:device>` imbriqué
  n'est pas construit. Une impl recrée un IO possédé obligatoire perdu par un fichier
  édité à la main avec `createOwnedLink()` : dans la pièce du device, lié sous son rôle ;
  un lien qui nomme un IO absent garde son id, sinon `<device>_<rôle>` ou une variante
  libre. Un device masqué par un autre de même id ne crée rien.
- **Propriété** : la pièce possède tous les IOs. Le device ne garde que des ids ;
  `ownedIos()` et `linkIo()` résolvent à chaque appel. Un id en double se comporte comme
  aujourd'hui : l'IO entre dans la pièce, le premier garde l'id. Pour deux devices de même
  id, seul celui qui tient l'id possède (et sauvegarde) les IOs marqués de cet id.
- **Liens** : vérifiés par `ListeRoom::resolveDeviceLinks()` à la fin de
  `Config::LoadConfigIO()`, donc une fois toutes les pièces chargées et avant les règles.
  Un lien non résolu est journalisé (`cErrorDom("device")`) et conservé dans le fichier ;
  `linkIo()` rend `nullptr`, le device se charge quand même.
- **Sauvegarde** : `Device::SaveToXml()` écrit l'élément, ses IOs possédés, ses liens puis
  ses params, dans l'ordre du fichier. `Room::SaveToXml()` saute un IO possédé tant que son
  device existe ; orphelin, il est écrit au niveau de la pièce plutôt que perdu.
  `Config::SaveConfigIO()` écrit `<calaos:home format="2">` dès qu'un device existe (inerte
  compris) ; sans device, la sortie est identique à celle du format 1, même si le fichier lu
  portait `format="2"`.
- **Suppression** : `ListeRoom::deleteIO(device)` détache des règles et détruit d'abord
  chaque IO possédé, recherché à nouveau après chaque suppression, puis le device ; la passe
  sur les auto-scénarios ne tourne qu'une fois. `delete_io()` ne cascade pas.
- **Valeur et commandes** (spec §5) : `get_value_string()` est le scalaire vu par les
  règles : `scalarFromState(deviceState())`, par défaut le `mode`. `set_value(string)`
  accepte un mode simple ou un objet JSON partiel, remis à `applyCommand()` : par défaut
  `mode` d'abord, puis chaque autre champ par `applyCommandField()` de la sous-classe
  (helpers `enumField()`, `numberField()`), en best effort. Une famille surcharge
  `applyCommand()` pour valider ensemble des champs liés. Format sur le fil :
  [08_http_api.md](08_http_api.md#devices--commande-et-objet-device).
- **Publication** : `gui_type` vaut la famille. Le device s'abonne par `IOChangeHub` à
  l'IO de chacun de ses liens ; tout changement, ou une commande (même refusée), appelle
  `deviceStateChanged()`, qui émet le signal du device pour les règles si son scalaire a
  changé (le scalaire de référence est relu à la fin de `resolveLinks()` : un mode restauré
  du cache ne déclenche rien) et arme un `Timer` membre de 100 ms (`PUBLISH_DELAY`) : un seul `io_changed` par
  fenêtre, sans réarmement, portant l'objet `device` entier. Les sous-classes fournissent
  `deviceCapabilities()`, `deviceState()` et, si besoin, `deviceIos()`. Un destructeur de
  sous-classe qui écrit ses IOs liés appelle d'abord `stopListening()`.

### ClimateBase

**Fichier :** [src/bin/calaos_server/IO/ClimateBase.h](../src/bin/calaos_server/IO/ClimateBase.h) —
spec §6. Famille `climate`, abstraite (constructeur protégé) : les impls `proxy` et
`thermostat` en dérivent.

- `ClimateMode` / `ClimateAction` sérialisés en chaînes ESPHome (`fan_only`, `heating`…).
- Capacités dérivées des liens résolus (table §6.3) et des params `modes`, `fan_modes`,
  `swing_modes`, `min_range` ; bornes de consigne lues sur l'IO écrit
  (`setpointBounds()`). Les erreurs de configuration sont journalisées une fois, dans
  `resolveLinks()`.
- **Fraîcheur d'une sonde** : `freshness(io)` vaut `Fresh` quand l'IO a fourni une lecture
  depuis le démarrage (`lastReading()`) il y a moins de `sensor_timeout` secondes (défaut 9000,
  soit 2 h 30 : un report Zigbee horaire perdu ne suffit pas à couper ; une valeur qui n'est
  pas un nombre strictement positif est journalisée et remplacée par le défaut) et ne se
  déclare pas déconnectée (`connected=false`, par exemple le testament d'ESPHome) ; sinon
  `NotRead`, `Silent` ou `Disconnected`. `isFresh(io)` en est le raccourci.
  `requestReadingIfOld(role)` demande une lecture (`requestReading()`) à une sonde jamais
  lue ou dont la dernière lecture a plus de la moitié de `sensor_timeout` : une sonde KNX
  « émission sur variation » ne se taira pas dans une pièce stable.
- Hooks d'impl : `supportedModes()` (le thermostat restreint ses modes selon ses sorties),
  `currentMode()` / `writeMode()` (le thermostat stocke son mode). Par défaut, le mode est lu
  sur le lien `mode` et écrit sur son `command_io`, sinon sur son `io`.
- L'état ne publie un nombre qu'une fois que l'IO lié a reçu une valeur : `IOBase::hasValue()`
  passe à vrai à chaque `EmitSignalIO()` et quand `Internal` restaure une valeur sauvegardée.
- `applyCommand()` valide `target_temp_low`/`_high` ensemble contre `min_range` et refuse la
  paire entière en cas d'écart ; les écritures sont ordonnées (haut d'abord quand la plage
  monte) pour que la paire intermédiaire, vue par le hook de changement, respecte aussi
  `min_range`.

### ProxyClimate

**Fichier :** [src/bin/calaos_server/IO/ProxyClimate.h](../src/bin/calaos_server/IO/ProxyClimate.h) —
spec §7. `impl="proxy"` : l'appareil régule, Calaos relaie. Chaque champ est lu sur l'`io` de son
lien et écrit sur son `command_io`, sinon sur son `io` quand c'est une sortie.

```xml
<calaos:device id="salon_ac" type="climate" impl="proxy" name="Clim salon">
  <calaos:input  id="salon_ac_mode"     type="MqttInputString"  visible="false" .../>
  <calaos:output id="salon_ac_mode_cmd" type="MqttOutputString" visible="false" .../>
  <calaos:input  id="salon_ac_fan"      type="MqttInputString"  visible="false" .../>
  <calaos:output id="salon_ac_fan_cmd"  type="MqttOutputString" visible="false" .../>
  <calaos:output id="salon_ac_remote"   type="MqttOutputAnalog" visible="false" .../>
  <calaos:link name="mode"     io="salon_ac_mode" command_io="salon_ac_mode_cmd"/>
  <calaos:link name="fan_mode" io="salon_ac_fan"  command_io="salon_ac_fan_cmd"/>
  <calaos:link name="remote_temp" io="salon_ac_remote"/>
  <calaos:link name="remote_temp_source" io="salon_temp"/>  <!-- externe -->
  <calaos:param name="auto_payload" value="heat_cool"/>
  <calaos:param name="remote_temp_keepalive" value="120"/>
</calaos:device>
```

- **Mode** : miroir du lien `mode`. `heat_cool` (ESPHome) se lit `auto`. `auto` s'écrit selon,
  dans l'ordre : le param `auto_payload` (`auto` ou `heat_cool`), la graphie `auto`/`heat_cool`
  que l'appareil a rapportée en dernier, sinon `heat_cool`. Une valeur brute qui n'est pas un mode
  n'est pas publiée et n'est journalisée qu'une fois par valeur (16 valeurs au plus ; des octets
  non triviaux sont remplacés par une étiquette).
- **Lien non inscriptible** : un lien sans `command_io` dont l'`io` n'est pas une sortie (un
  `MqttInputAnalog` accepte un `set_value` et ne l'envoie nulle part) ne peut pas être écrit.
  Pour `mode`, `target_temp`, `target_temp_low`/`_high`, `fan_mode`, `swing_mode` et
  `remote_temp`, la capacité correspondante n'est pas offerte (`modes`, `target_temp`,
  `target_temp_range` si une borne manque, `fan_modes`, `swing_modes`, `remote_temp`), la
  commande est refusée en entier et le chargement journalise l'erreur. L'état reste lu.
  `ClimateBase::isWritable(role)` porte la règle ; le proxy la définit par `isOutput()`.
- **Température distante** (§7) : si `remote_temp` et `remote_temp_source` sont liés, la valeur de
  la source est écrite sur `remote_temp` à chaque changement **et** toutes les
  `remote_temp_keepalive` secondes (défaut 120, fractions admises ; un `Timer` membre détruit avec
  le device). Rien n'est envoyé tant que la source n'est pas fraîche (`isFresh()` : une
  lecture depuis le démarrage, la dernière il y a moins de `sensor_timeout` s, et pas
  déconnectée ; 0 est une valeur) : l'appareil retombe alors sur sa sonde. Une source muette
  mais toujours « connectée » cesse donc d'être relayée, avec une erreur par silence ou
  déconnexion ; la lecture suivante est relayée aussitôt (une valeur répétée, au prochain
  keep-alive). Chaque keep-alive demande aussi une lecture à une source vieillissante
  (`requestReadingIfOld()`).
- **Relecture en retard** : une commande `target_temp_low`/`_high` partielle est validée contre la
  dernière valeur **commandée** de l'autre borne, jusqu'à ce que la relecture de cette borne
  change ou que `readback_hold` secondes (défaut 10) s'écoulent. Sans cela, deux commandes
  rapprochées se valideraient contre la valeur que la première a remplacée. Une écriture refusée
  n'est pas retenue.

### InternalThermostat

**Fichier :** [src/bin/calaos_server/IO/InternalThermostat.h](../src/bin/calaos_server/IO/InternalThermostat.h) —
spec §8.1 et §8.2. `impl="thermostat"` : Calaos régule.

- **Modes** : `off`, plus `heat` avec `output_heat`, `cool` avec `output_cool`, `auto` avec
  les deux. Le mode est stocké par le device et persisté dans le cache d'état
  (`iostates.cache`, clé = id du device) quand il change, `off` par défaut ; un mode
  sauvegardé que les sorties ne permettent plus repasse à `off`, journalisé et sauvegardé
  (il ne revient pas seul avec sa sortie). Il est
  restauré avant `ClimateBase::resolveLinks()`, donc sans déclencher les règles.
- **IOs possédés** : `target_temp`, `action` et, avec `output_cool`, `target_temp_low` /
  `_high`. Absents, ils sont recréés au chargement (`InternalInt` `save="true"` à 20, 19 et
  max(25, 19 + `min_range`) ; `InternalString` pour `action`) et écrits à la sauvegarde
  suivante.
- **`evaluate()`** : appelé par `IOChangeHub` sur `current_temp`, les consignes et `humidity`,
  par un changement de ses sorties, par les verrous, une fois au premier tour de boucle après
  le chargement, toutes les min(`sensor_timeout` / 4, 60) s (une sonde qui se tait ne signale
  rien, une valeur répétée non plus ; ce tour demande d'abord une lecture aux sondes
  vieillissantes), et une seule fois par commande, quand tous ses champs
  sont appliqués
  (`{"mode":"heat","target_temp":18}` ne chauffe pas contre l'ancienne consigne).
  Chauffe : ON sous `consigne − hysteresis`, OFF au-dessus de la consigne ; entre les deux,
  bornes comprises, elle garde l'état de sa dernière commande, pas celui d'une relecture en
  retard. Froid symétrique. `auto` : chauffe contre `target_temp_low`, froid contre
  `target_temp_high`. Une consigne sans valeur laisse sa sortie OFF. `hysteresis` vaut 0,5
  par défaut.
- **Sûreté** : `outputsAllowed()` est demandé en premier ; une sortie refusée passe OFF
  sur-le-champ, sans attendre `min_on_time`, et son `min_off_time` protège le redémarrage.
  Le mode `off` coupe de la même façon, ainsi qu'une sortie trouvée ON avec l'autre ; si
  l'autre est tenue ON de l'extérieur et ne peut pas encore être remise, c'est la sienne
  qui s'arrête : jamais les deux. Chaque règle cesse d'elle-même avec sa cause :
  - **sonde** : quoi que dise une surcharge du hook, les deux sorties restent OFF tant que
    `current_temp` n'est pas fraîche (`isFresh()`). Au démarrage, la valeur du cache d'état ne
    compte pas : il faut une lecture. Une sonde qui a lu puis se tait `sensor_timeout`
    secondes, ou qui se déclare déconnectée, coupe les sorties, avec une erreur par épisode ;
    la lecture suivante, même identique, relance la régulation ;
  - **point de rosée** (`emitter=floor`) : la base refuse `output_cool` tant que `humidity`
    n'est pas fraîche (jamais lue depuis le démarrage, muette `sensor_timeout` s ou
    déconnectée) ou lit
    `humidity_max` ou plus (défaut 65) ; journalisé à chaque changement de cause. Sans lien
    `humidity`, `cool` et `auto` sont absents des modes, `target_temp_range` n'est ni offert
    ni appliqué (`isWritable()` refuse les bornes) et le chargement journalise l'erreur ;
    `output_cool` reste une sortie du thermostat, tenue OFF. Le chauffage n'est pas concerné ;
  - **démarrage** : le premier tour de boucle compte comme un passage à OFF des deux sorties,
    donc `min_off_time` s'écoule avant le premier ON (un relais coupé juste avant un
    redémarrage du serveur ne refait pas un cycle court). **Conséquence** : après un
    démarrage, aucune chauffe avant la première lecture de `current_temp`, plus
    `min_off_time`. La valeur du cache d'état et un message MQTT retenu ne comptent pas :
    une sonde Zigbee non retenue qui n'émet qu'une fois par heure laisse le chauffage OFF
    jusqu'à 1 h ; une sonde KNX est lue au premier tour de vérification, une sonde Wago ou
    1-Wire à sa prochaine scrutation.
- **Verrous** : `min_on_time` / `min_off_time` (secondes, décimales acceptées, 0 par
  défaut), mesurés depuis la dernière commutation faite par le thermostat. Une commutation
  de régulation dans un verrou est reportée par un `Timer` membre réarmable, puis
  `evaluate()` repasse. Une sortie ne passe ON que si l'autre est OFF depuis au moins
  `min_off_time`, en `auto` comme lors d'un changement de mode : jamais les deux à la fois.
- **Écritures** : une sortie n'est écrite que si sa dernière commande ou sa relecture
  diffère de l'état voulu ; une écriture refusée ne change ni l'état tenu ni son horloge.
  Le thermostat suit aussi ses sorties, 1 ms après la marche des règles qui les a changées.
  Une sortie changée par autre chose (règle, Lua, `set_state`) compte comme
  commutée à cet instant : l'état tenu la suit, ses verrous partent de là, et la remettre
  passe par ses verrous (seul un arrêt forcé leur échappe). Les remises d'une sortie qu'on
  continue de changer sont espacées de `put_back_delay` (1 s ; 1 ms au moins), doublé tant
  que chaque remise est défaite avant la suivante, jusqu'à `put_back_delay_max` (60 s ; pas
  moins que `put_back_delay`), une valeur refusée étant journalisée et remplacée, même sans
  verrou et pour un arrêt forcé ; un avertissement est journalisé une fois par sortie. La
  relecture d'un OFF débloque une bascule sans attendre la mesure suivante. Pour l'autre
  sortie, une sortie compte comme ON tant que sa dernière commande ou sa relecture le dit. `action` (`off`,
  `idle`, `heating`, `cooling`) suit la dernière commande des sorties et n'est écrit que
  s'il change.
- **Réentrance** : hors marche des règles (commande, verrou), une écriture de sortie
  exécute les règles aussitôt. Si l'une d'elles change une entrée, la passe en cours
  s'arrête et recommence, sans jamais s'imbriquer.
- **Une sortie, un thermostat** : au chargement, un thermostat dont `output_heat` ou
  `output_cool` est déjà une sortie d'un thermostat chargé avant lui journalise l'erreur
  (les deux ids) et laisse ce lien de côté comme s'il ne résolvait pas : pas de capacité,
  aucune écriture. Le même IO en `output_heat` et `output_cool` garde la chauffe. Les
  liens d'un `ProxyClimate` ne comptent pas.
- **Id en double** : seul le device qui tient l'id régule ; son double ne s'abonne à rien,
  ne réclame aucune sortie et n'en écrit aucune.
- **Suppression** : les sorties restent en l'état. Aucune API ne supprime un device en
  marche : cela n'arrive qu'au rechargement de la configuration ou à l'arrêt du serveur,
  quand les sorties et leurs drivers peuvent déjà avoir disparu.

#### Sorties protégées (spec §8.3)

`output_heat` et `output_cool` appartiennent au thermostat. Rien ne les bloque : le moteur de
règles ne doit jamais casser, la valeur écrite est appliquée et le thermostat reprend la main
ensuite selon ses verrous.

- **Écritures extérieures** : `ActionStd` (règles), les scripts Lua et `set_state` (HTTP,
  WebSocket, panneau RemoteUI) appellent `Device::writtenFromOutside()`
  avant d'écrire un IO. `InternalThermostat` s'y abonne (`Device::observeOutsideWrites()`) et
  journalise un avertissement (domaine `device`) qui nomme le thermostat, la sortie et la source :
  `a rule`, `a Lua script`, `set_state over HTTP`, `set_state over WebSocket`,
  `set_state from a RemoteUI panel` (`JsonApiHandlerWS::setStateSource()`). Un IO qui n'est
  pas la sortie d'un thermostat chargé, le device lui-même, une sortie laissée de côté (un
  autre thermostat la tient) et un `set_state` refusé avant écriture ne produisent rien. Les
  écritures du thermostat ne passent pas par là. Les autres écrivains (pilote, scénario) restent
  couverts par l'avertissement « changée par autre chose » du suivi des sorties.
- **Limitation** : une ligne au plus par source et par sortie pendant `put_back_delay_max`
  (60 s par défaut), soit au pire une ligne par minute pour une règle qui écrit toutes les
  secondes ; un écrivain rare est journalisé à chaque fois, et un changement de source se voit
  tout de suite. La règle n'est pas nommée : `ActionStd` ne connaît pas la sienne.
- **OFF forcé défait** : quand le thermostat tient une sortie à l'arrêt (mode `off` ou refus de
  `outputsAllowed()`) et qu'une remise est défaite par un écrivain qui la rallume, chaque remise
  défaite est journalisée en **erreur** (`mode off` ou `safety`). Les remises étant espacées de
  `put_back_delay` au moins, cela fait au plus une erreur par espacement. C'est la seule défense
  face à une règle qui rallume : la sortie reste ON presque tout le temps. Le premier
  rallumage, avant toute remise, n'est qu'un avertissement (une fois par sortie). Une remise
  défaite hors d'un arrêt forcé (régulation) n'est pas une erreur.
- **MCP** : le sidecar expose l'écriture directe (`set_io_state`) ; masquer ces sorties dans
  `list_ios` et les refuser en écriture est le ticket MCP-1.

---

## IOFactory

**Fichier :** [src/bin/calaos_server/IO/IOFactory.h](../src/bin/calaos_server/IO/IOFactory.h)

Registre de fabrique. Chaque classe IO s'enregistre à l'initialisation statique via la macro
`REGISTER_IO`.

### Enregistrement d'un nouveau type IO

```cpp
// Dans le .cpp du driver, au niveau fichier :
REGISTER_IO(WODigital)
// ou, pour exposer le même type C++ sous un autre nom XML (les deux noms sont des
// IDENTIFIANTS NUS, pas des chaînes : la macro colle NAME dans le nom de la variable) :
REGISTER_IO_USERTYPE(WagoOutputLight, WODigital)
```

(dérivé, [IO/IOFactory.h:40-46](../src/bin/calaos_server/IO/IOFactory.h) pour les macros,
[IO/Wago/WODigital.cpp:27-28](../src/bin/calaos_server/IO/Wago/WODigital.cpp) pour l'usage.)

La macro crée un objet `Registrar` statique qui appelle `IOFactory::Instance().RegisterClass(…)`.

Deux propriétés à connaître (dérivé,
[IO/IOFactory.h:78-100](../src/bin/calaos_server/IO/IOFactory.h)) :

- la **clé est mise en minuscules** à l'enregistrement comme à la recherche : le `type` du
  XML est donc insensible à la casse ;
- un doublon d'enregistrement (même nom, ou noms ne différant que par la casse) est
  **refusé** — la **première** inscription gagne et la collision est seulement journalisée.
  Lever une exception ici tuerait le processus avant `main()`, puisque ce code tourne depuis
  les initialiseurs statiques.

### Instanciation

```cpp
IOBase *io = IOFactory::Instance().CreateIO("WagoInputSwitch", params);
// ou depuis un nœud XML (lit les attributs dans Params, puis appelle LoadFromXml) :
IOBase *io = IOFactory::Instance().CreateIO(pugiNode);
```

Les deux surcharges rendent un pointeur brut que l'appelant **possède**, ou `nullptr` si le
type est inconnu. Ce n'est délibérément pas un `unique_ptr` : le registre est un
`function<IOBase *(Params &)>` matérialisé par la macro dans ~60 fichiers de driver, et les
deux seuls appelants de l'arbre (`ListeRoom::createIO()` et `Room::LoadFromXml()`) parquent
le résultat dans un `unique_ptr` sur place avant de le remettre à la `Room`.

⚠️ Un IO fraîchement créé s'est **déjà** enregistré dans `io_table` (constructeur d'`IOBase`).
L'abandonner veut donc dire `delete`, ce qui le désenregistre.

### Génération de doc

```cpp
IOFactory::Instance().genDoc("/tmp/");  // → /tmp/io_doc.md + /tmp/io_doc.json
```

Deux fichiers, pas un par type. Le parcours instancie un exemplaire jetable de **chaque**
type enregistré ; ces exemplaires partagent tous `id="doc"` et sont tenus hors de la table
vive par le garde RAII `IOBase::ScopedDocGen` (dérivé,
[IO/IOFactory.cpp:70-115](../src/bin/calaos_server/IO/IOFactory.cpp)).

---

## Params

**Fichier :** [src/lib/Params.h](../src/lib/Params.h)

`std::map<string, string>` utilisée partout pour stocker paramètres d'IOs, règles, conditions.

```cpp
Params p;
p.Add("key", "value");
string v = p["key"];          // retourne "" si absent
bool exists = p.Exists("key");
p.Delete("key");
```

⚠️ **`operator[]` rend par valeur**, et il est `const`. `p["k"] = v` compile et ne fait
**rien** (l'affectation part dans un temporaire) : il faut `p.Add("k", v)`.

---

## Cycle de vie d'un IO

(dérivé, [CalaosConfig.cpp:281-308](../src/bin/calaos_server/CalaosConfig.cpp),
[Room.cpp:159-185](../src/bin/calaos_server/Room.cpp),
[IO/IOFactory.cpp:56-68](../src/bin/calaos_server/IO/IOFactory.cpp))

```
Config::LoadConfigIO()
  → Room::LoadFromXml(room_node)          // itère les enfants <calaos:input>, <calaos:output>,
    │                                     // <calaos:internal>, <calaos:avr>, <calaos:camera>,
    │                                     // <calaos:audio>, <calaos:remote_ui>
    → IOFactory::CreateIO(node)
      → readParams(node, p)               // tous les attributs XML → Params
      → ctor du driver → IOBase(Params&)  // ⇒ ListeRoom::addIOHash(this) EST FAIT ICI
      → io->LoadFromXml(node)             // config spécifique au driver
    → Room::AddIO(io)                     // la pièce prend la propriété

[le driver qui doit lire une valeur initiale s'annonce lui-même]
  → StartReadRules::addIO()               // incrémente le compteur, dans le ctor du driver
  → StartReadRules::ioRead()              // décrémente à la valeur reçue
  [compteur == 0] → ListeRule::ExecuteStartRules()
```

⚠️ **Charger une maison ne lève aucun event.** `Room::LoadFromXml()` construit et rattache
les IOs en silence ; `EventIOAdded` a un unique site d'émission, `ListeRoom::createIO()`
(`ListeRoom.cpp:533`), qui est le chemin **runtime** de l'API JSON. Un client connecté
pendant le boot ne voit rien de la maison qui se construit — épinglé par
`LoadingAHouseFromConfigRaisesNoEventAtAll`.

⚠️ `StartReadRules` n'est **pas** un compteur d'IOs : seuls les drivers qui font une lecture
asynchrone au démarrage s'y inscrivent (Wago, Web…). `main()` fait un `addIO()`/`ioRead()`
d'encadrement pour que les configurations sans aucun de ces drivers exécutent quand même les
règles `ConditionStart` (dérivé, [main.cpp:186-187](../src/bin/calaos_server/main.cpp)).

---

## IODoc

**Fichier :** [src/bin/calaos_server/IO/IODoc.h](../src/bin/calaos_server/IO/IODoc.h)

Système de documentation auto-déclarative. Chaque driver remplit son `IODoc` dans son
constructeur pour décrire ses paramètres, ses conditions et ses actions, ce qui permet à
`IOFactory::genDoc()` de générer la doc et à l'UI de présenter les champs de configuration.

L'API réelle (dérivé, [IO/IODoc.h:42-52](../src/bin/calaos_server/IO/IODoc.h)) :

```cpp
void friendlyNameSet(const string &friendlyName);
void descriptionSet(const string &description);
void descriptionBaseSet(const string &description);
void linkAdd(const string &description, const string &link);
void paramAdd(const string &name, const string &description, ParamType type,
              bool mandatory, const string defaultval = string(), bool readonly = false);
void paramAddInt(const string &name, const string &description, int min, int max,
                 bool mandatory, int defval = 0, bool readonly = false);
void paramAddFloat(const string &name, const string &description, bool mandatory,
                   double min, double max, double defval = 0, bool readonly = false);
void paramAddList(const string &name, const string &description, bool mandatory,
                  const Params &keyvalues, const string &defkey = string(),
                  bool readonly = false);
void conditionAdd(const string &name, const string &description);
void actionAdd(const string &name, const string &description);
void aliasAdd(string alias);
```

`ParamType` vaut `TYPE_UNKOWN` (sic), `TYPE_STRING`, `TYPE_BOOL`, `TYPE_INT`, `TYPE_FLOAT`
ou `TYPE_LIST`.

Exemple réel (dérivé, [IO/OutputLight.cpp:25-55](../src/bin/calaos_server/IO/OutputLight.cpp)) :

```cpp
ioDoc->descriptionBaseSet(_("Basic light. This light have only 2 states, ON or OFF…"));
ioDoc->conditionAdd("changed", _("Event on any change of value"));
ioDoc->actionAdd("toggle", _("Invert light state"));
Params io_style = {{ "light", _("Light") }, { "outlet", _("Outlet") }, /* … */};
ioDoc->paramAddList("io_style", _("GUI style display…"), true, io_style, "light");
```

### Documentation d'un device

Un device se documente avec les mêmes appels, plus cinq réservés à cet usage
([IO/IODoc.h](../src/bin/calaos_server/IO/IODoc.h)) :

```cpp
void commandFieldEnum(const string &name, const string &description, const vector<string> &values,
                      const vector<string> &requiresLinks, const string &valuesParam = string(),
                      const ValueLinks &valueRequires = ValueLinks());
void commandFieldNumber(const string &name, const string &description, double min, double max,
                        double step, const string &unit, const vector<string> &requiresLinks);
void deviceFamilySet(const string &family);
void linkRoleAdd(const string &role, const string &description, bool required, bool writable,
                 const string &requiredWhenParam = string(), const string &requiredWhenValue = string(),
                 const string &requiredWhenLink = string());
void paramInChild(const string &name);
```

- Ajouter un champ de commande fait publier `"command_format": "json"` et `"command_fields"`, dans
  l'ordre de déclaration. Un champ porte `name`, `type` (`enum` ou `number`), `description`,
  `requires_links` (les rôles de lien qui doivent exister et être inscriptibles sur l'instance),
  puis `values` (ou `values_param`, le param qui porte la liste de l'instance : `fan_modes`) pour un
  enum, `min`/`max`/`step`/`unit` pour un nombre. Contrairement aux paramètres, ces valeurs sont de
  vrais nombres JSON.
- `value_requires_links` (optionnel, enum) ajoute des rôles requis pour une seule valeur :
  `{"cool": ["output_cool"], "auto": ["output_heat", "output_cool"]}`. Les `requires_links` du champ
  valent pour toutes les valeurs.
- `family` et `link_roles` (`role`, `description`, `required`, `writable`) décrivent le device.
  `writable` signifie que **le device écrit ce lien** : vers l'appareil (consigne, mode), vers une
  sortie, ou vers un IO qu'il possède (l'`action` du thermostat). Un rôle non inscriptible est
  seulement lu. `required_when` (optionnel, `{"param", "value", "link"?}`) rend un rôle facultatif
  obligatoire quand un param a cette valeur **et**, si `link` est donné, que ce rôle est lié :
  `humidity` quand `emitter=floor` et `output_cool` lié (spec §6.3).
- `paramInChild` marque un paramètre qui s'écrit en `<calaos:param>` et non en attribut
  (`"storage": "calaos:param"`), quel que soit l'ordre des appels par rapport à `paramAdd` ; un nom
  jamais ajouté est ignoré.
- `ClimateBase::documentCommand(modes, modeLink, modeNote, modeRequires)` déclare le mode et les
  consignes ; une impl l'appelle dans son constructeur avec ses modes et le lien par lequel son mode
  s'écrit (vide si le device stocke lui-même son mode). Ce qu'une impl ignore n'est pas documenté :
  `documentModesParam()` (param `modes`) et `documentFanSwing()` (`fan_modes`, `swing_modes` et les
  champs `fan_mode`, `swing_mode`) ne sont appelées que par celles qui les respectent (le proxy).
- `Device::takeParamReads()` rend les noms de params lus depuis le dernier appel : un test s'en sert
  pour vérifier que chaque param documenté est lu par l'impl.
