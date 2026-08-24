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
  donc l'appeler deux fois est sans danger.
- `detachIOFromRules()` est la moitié **règles** : elle traite les règles qui utilisent cet
  IO et le sort de la liste de scrutation, **sans toucher à sa propriété**. Elle est publique
  parce que `~Room()` doit l'exécuter pour chacun de ses IOs.
- `deleteIO()` est l'enchaînement des deux, c'est-à-dire le chemin complet « l'utilisateur a
  supprimé cet IO » de l'API JSON.

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
