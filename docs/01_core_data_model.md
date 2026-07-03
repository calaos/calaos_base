# Core Data Model — IOBase, Room, ListeRoom, IOFactory

## Vue d'ensemble

Le modèle de données central organise les entrées/sorties (IOs) en pièces (Rooms), gérées globalement par `ListeRoom`. La fabrique `IOFactory` instancie les IOs par leur type string (auto-enregistrement au démarrage).

---

## IOBase

**Fichier :** [src/bin/calaos_server/IOBase.h](../src/bin/calaos_server/IOBase.h)

Classe de base abstraite pour toute entrée ou sortie. Chaque IO est un nœud du graphe de domotique.

### Attributs clés

| Attribut | Type | Description |
|---|---|---|
| `param` | `Params` | Map string→string de tous les paramètres de l'IO |
| `io_type` | `int` | `IO_INPUT`, `IO_OUTPUT` ou `IO_INOUT` |
| `status_info` | `StatusInfo` | Infos de statut : batterie, connexion, signal Wi-Fi, uptime |
| `ioDoc` | `IODoc*` | Documentation auto-générée de l'IO (pour l'interface) |

### Méthodes virtuelles importantes

```cpp
virtual DATA_TYPE get_type() = 0;        // TBOOL, TDOUBLE ou TSTRING
virtual bool get_value_bool();
virtual double get_value_double();
virtual string get_value_string();
virtual bool set_value(bool val);
virtual bool set_value(double val);
virtual bool set_value(string val);
virtual void hasChanged();               // appelé quand la valeur change
virtual bool LoadFromXml(TiXmlElement*);
virtual bool SaveToXml(TiXmlElement*);
```

### DATA_TYPE

Défini dans `Utils.h` :
- `TBOOL` — valeur booléenne (switch, lumière on/off)
- `TDOUBLE` — valeur numérique (température, gradateur, volet)
- `TSTRING` — valeur chaîne (player audio, caméra)

### Paramètres standard (dans `Params`)

| Clé | Valeur | Description |
|---|---|---|
| `id` | string | Identifiant unique de l'IO |
| `name` | string | Nom affiché |
| `type` | string | Type enregistré dans IOFactory |
| `enabled` | `"true"/"false"` | IO actif ou non |
| `gui_type` | string | Type d'affichage UI |
| `var_type` | string | `bool`/`float`/`string` |

### Signal de changement

```cpp
void EmitSignalIO();  // déclenche ListeRule::ExecuteRuleSignal(id)
```

### StatusInfo

Infos de statut pour les périphériques IoT :

```cpp
struct StatusInfo {
    double battery_level;          // 0-100%
    StatusConnected connected;     // STATUS_NONE / DISCONNECTED / CONNECTED
    double wireless_signal;        // 0-100%
    uint64_t uptime;               // secondes
    string ip_address;
    string wifi_ssid;
};
```

Méthodes setter :
```cpp
void setStatusInfo(StatusType type, double value);
void setStatusInfo(StatusType type, const string &value);
void setStatusInfo(StatusType type, StatusConnected value);
```

---

## Room

**Fichier :** [src/bin/calaos_server/Room.h](../src/bin/calaos_server/Room.h)

Conteneur d'IOs représentant une pièce physique ou logique.

```cpp
class Room {
    string name;       // nom de la pièce
    string type;       // type ("living", "bedroom", …)
    int hits;          // compteur d'accès (tri UI)
    vector<IOBase*> ios;
};
```

---

## ListeRoom

**Fichier :** [src/bin/calaos_server/ListeRoom.h](../src/bin/calaos_server/ListeRoom.h)

Singleton. Registre global de toutes les pièces et de tous les IOs.

### Accès aux IOs

```cpp
IOBase *get_io(string id);   // par identifiant unique
IOBase *get_io(int i);       // par index dans la table
int get_io_count();          // nombre total d'IOs
```

### Table de hachage

```cpp
unordered_map<string, IOBase*> io_table;  // id → IOBase*
```

Toujours maintenu à jour via `addIOHash()` / `delIOHash()`.

### Caches spécialisés

```cpp
list<IOBase*> cameraCache;       // IOs de type IPCam
list<IOBase*> audioCache;        // IOs de type AudioPlayer
list<Scenario*> auto_scenario_cache;
```

### Création/suppression d'IO

```cpp
IOBase* createIO(Params param, Room *room);  // crée via IOFactory et ajoute
bool deleteIO(IOBase *io, bool modify = false); // supprime + modifie config si modify=true
```

---

## IOFactory

**Fichier :** [src/bin/calaos_server/IO/IOFactory.h](../src/bin/calaos_server/IO/IOFactory.h)

Registre de fabrique. Chaque classe IO s'enregistre à l'initialisation statique via la macro `REGISTER_IO`.

### Enregistrement d'un nouveau type IO

```cpp
// Dans le .cpp du driver, au niveau fichier :
REGISTER_IO(MonNouveauType)
// ou avec un nom différent du type C++ :
REGISTER_IO_USERTYPE("nom_xml", MonNouveauType)
```

La macro `REGISTER_IO(TYPE)` crée un objet `Registrar` statique qui appelle `IOFactory::Instance().RegisterClass(...)`.

### Instanciation

```cpp
IOBase *io = IOFactory::Instance().CreateIO("WagoInputSwitch", params);
// ou depuis XML :
IOBase *io = IOFactory::Instance().CreateIO(xmlElement);
```

### Génération de doc

```cpp
IOFactory::Instance().genDoc("/tmp/");  // génère des fichiers de doc par type IO
```

---

## Params

**Fichier :** [src/lib/Params.h](../src/lib/Params.h)

Map string→string utilisée partout pour stocker paramètres d'IOs, règles, conditions.

```cpp
Params p;
p.Add("key", "value");
string v = p["key"];          // retourne "" si absent
bool exists = p.Exists("key");
p.Delete("key");
```

---

## Cycle de vie d'un IO

```
Config::LoadConfigIO()
  → IOFactory::CreateIO(xmlNode)     // alloue l'objet
  → Room::AddIO(io)                  // ajoute à la pièce
  → ListeRoom::addIOHash(io)         // indexe dans io_table
  → io->LoadFromXml(node)            // charge les paramètres
  → StartReadRules::addIO()          // compte les IOs à initialiser

[valeur disponible]
  → StartReadRules::ioRead()         // décrémente compteur
  [tous lus] → ListeRule::ExecuteStartRules()
```

---

## IODoc

**Fichier :** [src/bin/calaos_server/IO/IODoc.h](../src/bin/calaos_server/IO/IODoc.h)

Système de documentation auto-déclarative. Chaque driver remplit son `IODoc` dans son constructeur pour décrire ses paramètres, permettant à `IOFactory::genDoc()` de générer de la doc et à l'UI de présenter les champs de configuration.

```cpp
ioDoc->friendlyName("Mon IO");
ioDoc->linkDescription("http://...");
ioDoc->addParam(IODocParam::...);
```
