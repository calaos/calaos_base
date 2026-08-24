# Utility Library — Bibliothèque utilitaire

## Vue d'ensemble

Le dossier `src/lib/` contient les utilitaires partagés entre le serveur, les outils
(`calaos_config`, dont l'interface TUI) et les sous-processus. L'essentiel vit dans le namespace
`Utils`, sauf les classes qui portent leur propre nom (`Params`, `Timer`, `ColorValue`,
`FileUtils`, `UrlDownloader`, `TCPSocket`…).

> **Convention de provenance.** Chaque affirmation de ce document est adossée à une référence
> `Fichier:ligne` **vérifiée au source**. Les signatures citées sont recopiées telles quelles ;
> quand un extrait est raccourci, il est marqué `extrait`.

---

## Le split de `Utils` (T2.2) et le pattern agrégateur

`Utils.h` était un fourre-tout. T2.2 l'a découpé en six unités thématiques, **sans casser aucun
appelant** : `Utils.h` ré-inclut les six, donc `#include <Utils.h>` continue de tout apporter
(dérivé, `src/lib/Utils.h:124-129`, intégral) :

```cpp
#include "Constants.h"
#include "MemMacros.h"
#include "LogSetup.h"
#include "StringUtils.h"
#include "ConfigStore.h"
#include "SystemInfo.h"
```

| Unité | Fichier | Contenu |
|---|---|---|
| `Constants.h` | [src/lib/Constants.h](../src/lib/Constants.h) | Chemins de config, noms de fichiers, ports, couleurs ANSI, constantes Wago |
| `MemMacros.h` | [src/lib/MemMacros.h](../src/lib/MemMacros.h) | `DELETE_NULL`, `FREE_NULL`, `DELETE_NULL_FUNC`, `VAR_UNUSED` |
| `LogSetup.{cpp,h}` | [src/lib/LogSetup.h](../src/lib/LogSetup.h) | Les macros `cDebug()`/`cDebugDom()` et le cycle de vie des loggers |
| `StringUtils.{cpp,h}` | [src/lib/StringUtils.h](../src/lib/StringUtils.h) | Chaînes, URL, base64, conversions template |
| `ConfigStore.{cpp,h}` | [src/lib/ConfigStore.h](../src/lib/ConfigStore.h) | Lecture/écriture de `local_config.xml`, chemins de config et de cache |
| `SystemInfo.{cpp,h}` | [src/lib/SystemInfo.h](../src/lib/SystemInfo.h) | `Watchdog()`, `createRandomUuid()`, `getUptime()` |

Ce qui **reste** dans `Utils.h` après le split : les en-têtes système et tiers agrégés, le typedef
`Json = nlohmann::json`, et un petit reliquat de fonctions (dérivé, `src/lib/Utils.h:134-235`) —
`roundValue()`, `parseParamsItemList()`, `argvOptionCheck()`, `argvOptionParam()`,
`getFileContent()`, `getFileContentBase64()`, `getTmpFilename()`, `getMainLoopTime()`,
`fileExists()`, les foncteurs `Delete`/`DeletorT`, `line_exception`, et les enums de types
(`DATA_TYPE`, `SHUTTER_*`, `UWord`).

---

## StringUtils

**Fichier :** [src/lib/StringUtils.h](../src/lib/StringUtils.h)

⚠️ **Les signatures comptent.** Plusieurs de ces fonctions **modifient leur argument en place** et
ne renvoient rien — ce n'est pas l'API que l'on devine.

```cpp
// URL (dérivé, StringUtils.h:33-36)
std::string url_encode(std::string str);
std::string url_decode(std::string str);
std::string url_decode2(std::string str); //decode 2 times
int htoi(char *s);

// Chaînes (extrait de StringUtils.h:41-47 — `remove_tag` omis, `split` replié sur deux
//         lignes) : noter les sorties par référence
void split(const std::string &str, std::vector<std::string> &tokens,
           const std::string &delimiters = " ", int max = 0);
void replace_str(std::string &source, const std::string searchstr, const std::string replacestr);
void trim_right(std::string &source, const std::string &t);
void trim_left(std::string &source, const std::string &t);
std::string trim(const std::string &str);
std::string escape_quotes(const std::string &s);

// Conversions template (extrait de StringUtils.h:96-118 — déclarations seules, les corps
//                        inline ne sont pas repris)
template<typename T> bool is_of_type(const std::string &str);
template<typename T> bool from_string(const std::string &str, T &dest); //locale "C"
template<typename T> std::string to_string(const T &Value);

// Recherche (dérivé, StringUtils.h:58-60)
enum CaseSensitivity { CaseInsensitive, CaseSensitive };
bool strContains(const std::string &str, const std::string &needle,
                 Utils::CaseSensitivity cs = Utils::CaseSensitive);
bool strStartsWith(const std::string &str, const std::string &needle,
                   Utils::CaseSensitivity cs = Utils::CaseSensitive);

// Base64 (dérivé, StringUtils.h:63-67)
std::string Base64_decode(std::string &str);
std::string Base64_encode(std::string &str);
std::string Base64_encode(void *data, int size);
```

⚠️ **Piège connu, mesuré : `Utils::from_string("")` renvoie `true`** avec la destination
zéro-initialisée (`iss.eof()` est vrai sur une entrée vide), ce qui fait passer une chaîne vide
pour un `0` valide. Chaque appelant doit se défendre par un contrôle de plage.
Voir `docs/refactoring/FINDINGS.md`.

### `maskUrlCredentials()` (T2.17)

```cpp
std::string maskUrlCredentials(const std::string &url);
```

Masque les identifiants d'une URL **avant qu'elle n'atteigne un log** : le mot de passe du
userinfo (`scheme://user:secret@host/` → l'utilisateur est conservé, le secret masqué) et la
valeur des paramètres de requête porteurs d'identifiants — `usr`, `pwd`, `user`, `username`,
`password`, `passwd`, `account`, `loginuse`, `loginpas`, `_sid`, insensible à la casse
(dérivé, `src/lib/StringUtils.h:49-55`). Déplacée depuis `IPCam` pour que `UrlDownloader` puisse
masquer **toute** URL qu'il journalise.

---

## ConfigStore

**Fichier :** [src/lib/ConfigStore.h](../src/lib/ConfigStore.h)

Accès à `local_config.xml` et aux chemins de config/cache (dérivé, `src/lib/ConfigStore.h:32-47`) :

```cpp
void initConfigOptions(char *configdir = NULL, char *cachedir = NULL, bool quiet = false);

std::string getConfigPath();
std::string getCachePath();
std::string getConfigFile(const char *configFile);
std::string getCacheFile(const char *cacheFile);

std::string get_config_option(std::string key, bool no_logger_out = false);
bool set_config_option(std::string key, std::string value);
bool del_config_option(std::string key);
bool get_config_options(Params &options);
bool set_config_options(const Params &toSet, const std::vector<std::string> &toDelete = {});
```

`set_config_options()` est la mise à jour **groupée** : toutes les clés de `toSet` sont créées ou
mises à jour et toutes celles de `toDelete` supprimées dans **un seul cycle**
chargement / modification / écriture atomique. Le fichier est **rechargé au moment de l'appel**,
sous le verrou, si bien que les clés absentes des deux listes conservent la valeur qu'un autre
processus a pu leur donner entre-temps (dérivé, `src/lib/ConfigStore.h:43-47`).

⚠️ **`get_config_option()` / `set_config_option()` sont scrutées par `make check`.** Le script
`tests/check-config-options.sh` scanne les sources à la recherche de ces appels et **échoue** si
une clé utilisée par le code n'est pas déclarée dans le registre `src/lib/ConfigOptions.cpp`
(dérivé, `tests/check-config-options.sh:220-262` pour le scan, `:328-339` pour le contrôle). La
clé doit être un **littéral chaîne sur la même ligne que l'appel**, ou une constante
`const char *NAME = "..."` du **même fichier** — toute autre forme est un échec dur du test.
Le contrôle réciproque existe aussi : une entrée du registre déclarée `consumer(Server)` que
**aucune** source ne lit est signalée comme orpheline (`:341-353`).

### Robustesse : configuration corrompue (T2.4)

Au lieu de refuser de démarrer sur un `local_config.xml` illisible, le serveur restaure
automatiquement le backup le plus récent exploitable, **conserve une copie du fichier corrompu**
dans `<config>/backups/corrupt/`, et envoie une **notification mail + push** ~30 s après le
démarrage indiquant le fichier, la copie conservée et le backup restauré. Le détail du mécanisme
est dans [11_config_persistence.md](11_config_persistence.md).

---

## ConfigOptions — le registre des options

**Fichiers :** [src/lib/ConfigOptions.h](../src/lib/ConfigOptions.h),
[src/lib/ConfigOptions.cpp](../src/lib/ConfigOptions.cpp)

Registre déclaratif de **toutes** les clés de `local_config.xml` : pour chacune un libellé, une
description longue, un type, un défaut, une plage, des exemples, et le **masque de
consommateurs** — `Server`, `CalaosHome`, `McpSidecar` (dérivé, `src/lib/ConfigOptions.h:65`).
Les options se construisent par chaînage : `.label()`, `.doc()`, `.defDynamic()`, `.range()`,
`.values()`, `.example()`, `.restartRequired()`, `.secret()`, `.confirmReset()`,
`.deprecated()`, `.advanced()`, `.seeAlso()` (dérivé, `src/lib/ConfigOptions.h:73-90`).

Le registre est la **source unique** de deux artefacts :

- `ConfigOptions::genMarkdown()` → **[docs/16_config_options.md](16_config_options.md)**
  (dérivé, `src/lib/ConfigOptions.h:208`) ;
- `ConfigOptions::genJson()` → sortie lisible par machine, sur le modèle de `io_doc.json`
  (dérivé, `src/lib/ConfigOptions.h:209`).

⚠️ **`docs/16_config_options.md` ne s'édite pas à la main.** `tests/check-config-docs.sh`, câblé
dans `make check`, régénère le document avec `calaos_config options --markdown` et le compare au
fichier commité par un `diff -u` : **la moindre retouche manuelle fait échouer la suite**
(dérivé, `tests/check-config-docs.sh:81-96`). Pour le mettre à jour, on modifie le registre puis
on relance :

```sh
_build/src/bin/tools/calaos_config options --markdown > docs/16_config_options.md
```

---

## SystemInfo

**Fichier :** [src/lib/SystemInfo.h](../src/lib/SystemInfo.h)

```cpp
namespace Utils
{
void Watchdog(std::string fname);

std::string createRandomUuid();

unsigned int getUptime();
}
```

(dérivé, `src/lib/SystemInfo.h:27-34`, intégral.) `createRandomUuid()` s'appuie sur la
bibliothèque embarquée `sole` (UUID v4).

---

## Constants.h

**Fichier :** [src/lib/Constants.h](../src/lib/Constants.h)

Défines partagés (extrait, `src/lib/Constants.h:31-70`) : chemins (`PREFIX_CONFIG_PATH`,
`ETC_CONFIG_PATH`, `HOME_CONFIG_PATH`, `HOME_CACHE_PATH`), noms de fichiers de configuration
(`LOCAL_CONFIG` = `"local_config.xml"`, `IO_CONFIG` = `"io.xml"`, `RULES_CONFIG` = `"rules.xml"`,
`WIDGET_CONFIG` = `"widgets.xml"`), URLs Calaos, chemins de fuseaux horaires, séquences de
couleur ANSI, et les ports par défaut :

```cpp
#define WAGO_LISTEN_PORT        4646
#define BCAST_UDP_PORT          4545
#define JSONAPI_PORT            5454
```

---

## MemMacros.h

**Fichier :** [src/lib/MemMacros.h](../src/lib/MemMacros.h)

```cpp
#define DELETE_NULL(p) \
    if (p) { delete p; p = NULL; }

#define FREE_NULL(p) \
    if (p) { free(p); p = NULL; }

#define DELETE_NULL_FUNC(fn, p) \
    if (p) { fn(p); p = NULL; }

#define VAR_UNUSED(x) (void)x;
```

(dérivé, `src/lib/MemMacros.h:25-34`, intégral.)

---

## Params

**Fichier :** [src/lib/Params.h](../src/lib/Params.h)

Map `string → string` avec accès simplifié (dérivé, `src/lib/Params.h:33-74`) :

```cpp
Params p = { {"key", "val"}, {"key2", "val2"} };  //ctor initializer_list

p.Add("key", "value");        //ajoute ou écrase  (Params.cpp:26-29)
p.Exists("key");              //teste la présence
p.get_param("key");           //"" si absent      (Params.cpp:39-44)
p.get_param_const("key");     //version const     (Params.cpp:46-53)
p.Delete("key");
p.size();
p.clear();
p.get_item(i, key, value);    //accès indexé

std::string s = p.toString();
json_t *j = p.toJson();       //jansson
Json nj = p.toNJson();        //nlohmann
Params q = Params::fromNJson(nj);
```

⚠️ **`Params` n'expose ni `begin()` ni `end()`** : un `for (auto &pair : p)` ne compile pas. Le
parcours se fait par index avec `get_item()` (dérivé, `src/lib/Params.h:33-74` — aucun itérateur
n'y est déclaré).

⚠️ **Footgun connu : `operator[]` renvoie **par valeur** et est `const`** — `string operator[]
(string key) const;` (dérivé, `src/lib/Params.h:63`). Écrire `p["k"] = v` compile et est un
**no-op silencieux** : l'affectation porte sur un temporaire. Utiliser `Add()`. Ce défaut a déjà
produit un bug vivant (throttle de notification batterie qui ne s'appliquait jamais, corrigé par
T1.11) ; voir `docs/refactoring/FINDINGS.md`.

---

## Logger et LogSetup

**Fichiers :** [src/lib/Logger.h](../src/lib/Logger.h), [src/lib/LogSetup.h](../src/lib/LogSetup.h)

Système de log multi-domaine avec niveaux.

### Macros

```cpp
cDebug()          << "message debug global";
cInfo()           << "message info";
cWarning()        << "attention";
cError()          << "erreur";
cCritical()       << "critique";

cDebugDom("mqtt") << "message debug du domaine mqtt";
cInfoDom("wago")  << "connexion établie";
```

(dérivé, `src/lib/LogSetup.h:26-37` — les dix macros y sont définies, `cXxx()` déléguant à
`cXxxDom()` via `Utils::calaosLogger(domain)`.)

### Niveaux

```cpp
LOG_LEVEL_UNKNOWN = 0, LOG_LEVEL_CRITICAL = 1, LOG_LEVEL_ERROR = 2,
LOG_LEVEL_WARNING = 3, LOG_LEVEL_INFO = 4, LOG_LEVEL_DEBUG = 5
```

(dérivé, `src/lib/Logger.h:104-111`.)

### Configuration

Les niveaux se règlent par **options de configuration**, pas par variables d'environnement
(dérivé, `src/lib/Logger.cpp:108` et `:119`) :

```sh
calaos_config set debug_level 5
calaos_config set debug_domains hifirose:5,network:0
```

`debug_level` hors de l'intervalle 0…5 retombe sur `LOG_LEVEL_INFO` (4)
(dérivé, `src/lib/Logger.cpp:112-114`) ; les domaines non listés dans `debug_domains` gardent le
niveau global (dérivé, `src/lib/Logger.cpp:117`). Ces deux clés sont **relayées aux pilotes
externes et au sidecar MCP** par les variables d'environnement `CALAOS_LOG_LEVEL` et
`CALAOS_LOG_DOMAINS` (dérivé, `src/bin/calaos_server/McpServerManager.cpp:250-251`), consommées
côté Python par `src/lib/calaos-python/calaos_extern_proc/logger.py:95` et `:106`.
La seule variable d'environnement lue par le logger C++ lui-même est `CALAOS_FORCE_COLOR`
(dérivé, `src/lib/Logger.cpp:26`).

⚠️ **Ces singletons ne sont jamais détruits (T3.14)**, délibérément : le cache de niveaux et la
table des loggers vivent jusqu'à la fin du processus, sans quoi un destructeur statique les
réutilisait après leur propre destruction — c'est ce qui imprimait un chemin corrompu à chaque
arrêt du serveur (dérivé, `src/lib/Logger.cpp:85-99`).

---

## Timer

**Fichier :** [src/lib/Timer.h](../src/lib/Timer.h)

Timer basé sur libuv (via `uvw`), intégré dans la boucle événementielle principale.

⚠️ **Un `Timer` est *répétitif*, pas one-shot.** Le constructeur démarre le handle avec le même
délai en timeout **et** en intervalle de répétition (dérivé, `src/lib/Timer.cpp:60-61`) :

```cpp
handleTimer->start(uvw::TimerHandle::Time{time},
                   uvw::TimerHandle::Time{time});
```

L'exécution unique se demande explicitement par `Timer::singleShot()`.

```cpp
// API réelle (dérivé, src/lib/Timer.h:60-71)
Timer(double time, sigc::slot<void, void *> slot, void *data);
Timer(double time, sigc::slot<void> slot);

static void singleShot(double time, sigc::slot<void> slot);

void Reset();            //relance la période courante  (Timer.cpp:83-86)
void Reset(double time); //change la période et relance
void Tick();
double getTime();
```

Il n'y a **pas** de `stop()` public : on arrête un `Timer` en le détruisant. C'est sûr, y compris
depuis son propre callback : le handle porte un `aliveTag` que le callback uvw ne tient qu'en
`weak_ptr`, donc un `Timer` détruit pendant qu'un événement est en vol n'est plus déréférencé
(dérivé, `src/lib/Timer.h:52-55` et `src/lib/Timer.cpp:48-58`, `:64-81`).

`Idler` (même fichier, `src/lib/Timer.h:74-92`) est le pendant « à la prochaine itération de
boucle », avec le même garde-fou de durée de vie.

---

## ColorUtils

**Fichier :** [src/lib/ColorUtils.h](../src/lib/ColorUtils.h)

Conversions de couleurs pour les IOs RGB. La classe s'appelle **`ColorValue`** et ses composantes
sont **privées** (union RGB/HSV/HSL + alpha) : on passe par les accesseurs
(dérivé, `src/lib/ColorUtils.h:93-123`).

```cpp
// Construction (dérivé, ColorUtils.h:32-34, 84-88)
ColorValue c;                              //invalide tant qu'on n'a rien posé
ColorValue c2(255, 128, 0);                //RGB
ColorValue c3("#ff8000");                  //depuis une chaîne
ColorValue c4 = ColorValue::fromRgb(255, 128, 0);
ColorValue c5 = ColorValue::fromHsv(30, 100, 100);
ColorValue c6 = ColorValue::fromHsl(30, 100, 50);
ColorValue c7 = ColorValue::fromXYBrightness(x, y, brightness); //CIE 1931

// Accès (dérivé, ColorUtils.h:36-82)
c.isValid();
c.getRed(); c.getGreen(); c.getBlue();
c.getHSVHue(); c.getHSVSaturation(); c.getHSVValue();
c.getHSLHue(); c.getHSLSaturation(); c.getHSLLightness();
c.setRgb(r, g, b, a = 255);
c.setHsv(h, s, v, a = 255);
c.setHsl(h, s, l, a = 255);
c.toXYBrightness(x, y, brightness);
ColorValue rgb = c.toRgb();
std::string s = c.toString();
c.setString(s);
```

⚠️ Les fabriques sont `fromRgb` / `fromHsv` / `fromHsl` — **pas** `fromRGB` / `fromHSV` — et il
n'existe **pas** de `fromString()` : la conversion depuis une chaîne passe par le constructeur
`ColorValue(string)` ou par `setString()`.

---

## FileUtils

**Fichier :** [src/lib/FileUtils.h](../src/lib/FileUtils.h)

Classe à **méthodes statiques** (dérivé, `src/lib/FileUtils.h:7-36`, intégral pour la liste) :

```cpp
static std::size_t fileSize(const std::string &filename);
static bool exists(const std::string &filename);
static bool isDir(const std::string &path);
static bool mkdir(const std::string &path);
static bool mkpath(const std::string &path); //full path with subdirs
static bool rmdir(const std::string &path);
static bool unlink(const std::string &filename);
static bool isReadable(const std::string &path);
static bool isWritable(const std::string &path);
static bool isExecutable(const std::string &path);
static bool rename(const std::string &src, const std::string &dst);
static std::string filename(const std::string &path);
static bool copyFile(const std::string &src, const std::string &dst);
static std::vector<std::string> listDir(const std::string &path);

static bool resolveSafePath(const std::string &root,
                            const std::string &userPath,
                            std::string &outPath);
```

⚠️ Il n'y a **ni `fileExists()`, ni `dirExists()`, ni `readFile()`, ni `writeFile()`** dans
`FileUtils` : les noms sont `exists()` et `isDir()`, et la lecture d'un fichier entier se fait par
`Utils::getFileContent()` (dérivé, `src/lib/Utils.h:147`).

### `resolveSafePath()` — résolution de chemin sûre

Résout un sous-chemin fourni par l'utilisateur contre un répertoire racine de confiance. Rejette
les chemins absolus, les octets NUL et tout segment `..` **avant** de toucher au système de
fichiers, puis canonicalise racine et candidat par `realpath()` et vérifie que le candidat résolu
reste **à l'intérieur** de la racine résolue. Renvoie `true` en cas de succès et écrit le chemin
absolu canonique dans `outPath`. Renvoie `false` sur toute tentative de traversée, fichier absent
ou lien symbolique qui s'échappe de la racine. **L'appelant doit traiter `false` comme « non
trouvé » (HTTP 404)** pour ne pas divulguer l'arborescence
(dérivé, `src/lib/FileUtils.h:26-36`).

---

## XmlUtils

**Fichier :** [src/lib/XmlUtils.h](../src/lib/XmlUtils.h)

Namespace `Calaos::XmlUtils` : les helpers pugixml qui portent les comportements que **TinyXML 1
avait implicitement** et que pugixml n'a pas tels quels. Le port du lecteur/écrivain de
configuration (E4.4cd) est par ailleurs une réécriture 1:1 ; tout ce qui n'est pas un renommage
mécanique vit ici, pour être **visible et partagé** plutôt que re-dérivé à chacun des ~30 sites
d'appel (dérivé, `src/lib/XmlUtils.h:22-34`).

- `firstChildElement()` / `nextSiblingElement()` — parcours **des éléments seuls**.
  `TiXmlNode::FirstChildElement()` sautait tout ce qui n'est pas un élément ; les
  `first_child()`/`next_sibling()` de pugixml, non. Deux sites le remarqueraient :
  `InPlageHoraire::LoadRange()` lit les attributs de **chaque** enfant sans regarder son nom (un
  nœud texte parasite deviendrait un `TimeRange` vide) et `ConditionScript::LoadFromXml()` se sert
  de la nullité du premier enfant comme test « pas de script ici »
  (dérivé, `src/lib/XmlUtils.h:48-77`).
- `setAttribute()` — **remplace** au lieu d'ajouter. `append_attribute()` créerait un second
  attribut de même nom ; `InPlageHoraire::SaveToXml()` écrit toute la map de paramètres puis
  réécrit `months`, qui **est** dans cette map. Un `append` naïf émettrait `months=` deux fois dans
  chaque IO horaire de chaque configuration (dérivé, `src/lib/XmlUtils.h:79-89`).

Seule l'API pugixml **≥ 1.10** est utilisée (dérivé, `src/lib/XmlUtils.h:33`).

---

## UrlDownloader

**Fichier :** [src/lib/UrlDownloader.h](../src/lib/UrlDownloader.h)

Client HTTP asynchrone. **Transport : interface multi de libcurl pilotée par la boucle libuv
(T2.5)** — plus de sous-processus, plus de thread de travail, plus de fichier temporaire
(dérivé, `src/lib/UrlDownloader.h:31-33`). Utilisé par tous les drivers qui font des requêtes HTTP
(Hue, Web, IPCam, InfluxDB, notifications push…).

```cpp
// (dérivé, UrlDownloader.h:126-194)
UrlDownloader *dl = new UrlDownloader("http://api.example.com/data"); //autodelete=false
dl->setHeader("Authorization", "Bearer token");
dl->bodyDataSet(R"({"cmd":"on"})");
dl->m_signalCompleteData.connect([](const std::string &data, int status) {
    // traite la réponse
});
dl->httpPost();
```

⚠️ **Trois pièges de nommage** :

- le second paramètre du constructeur est **`autodelete`**, pas « async » —
  `UrlDownloader(string url, bool autodelete = false)` (dérivé, `src/lib/UrlDownloader.h:126`) ;
- il n'y a **pas** de `start()` public ni de `setBody()`. On règle le corps par `bodyDataSet()` et
  on lance par l'un des quatre verbes : `httpGet()`, `httpPost()`, `httpPut()`, `httpDelete()`
  (dérivé, `src/lib/UrlDownloader.h:176-179`) ;
- les signaux s'appellent `m_signalComplete` (code HTTP), `m_signalCompleteData` (données + code)
  et `m_signalData` (flux) — il n'y a pas de `resultData`
  (dérivé, `src/lib/UrlDownloader.h:190-192`).

### Cycle de vie (T2.10)

- `cancel()` interrompt un transfert **à tout moment** : le transfert libcurl est avorté (la
  connexion tombe), tous les signaux sont déconnectés et **aucun callback ne se déclenche
  ensuite**. Un objet non-autodelete est alors sûr à détruire (ou à conserver) ; un objet
  autodelete se libère lui-même après `cancel()`.
- Un objet **autodelete ne doit JAMAIS être détruit de l'extérieur** : il se détruit lui-même (via
  un `Idler`) une fois le transfert terminé ou annulé ; un `delete` externe entrerait en course
  avec cela. `cancel()` est le seul contrôle externe.
- Détruire un objet **non-autodelete** en cours de transfert est sûr : le destructeur détache le
  transfert en vol (jeton `alive`) avant de rendre la main.

(dérivé, `src/lib/UrlDownloader.h:35-45`.)

### TLS et l'option `insecure` (T2.17 puis T2.19, décision utilisateur)

```cpp
void setInsecure();                                    //ce transfert seulement
static bool insecureParamEnabled(const std::string &paramValue);
void setInsecureFromParam(const std::string &paramValue);
static void insecureGet(string url, string get_data = ""); //GET fire-and-forget non vérifié
```

**T2.19 remplace le défaut de T2.17 pour les URLs utilisateur** : toute URL configurée par
l'utilisateur (caméras, pont Hue, IOs Web, scripts Lua, InfluxDB, équipements audio…) **saute la
vérification de certificat par défaut** — le parc installé est en HTTPS auto-signé sur le LAN, un
défaut vérifié casserait toutes les installations existantes. Le durcissement est un opt-in **par
équipement** avec `insecure="false"`. Seules les URLs de service calaos.fr en dur (`NotifManager`
push…) restent vérifiées, en **ne passant pas** par ces helpers
(dérivé, `src/lib/UrlDownloader.h:143-159`).

`insecureParamEnabled()` est bornée et ne lève jamais : **seule la chaîne exacte `"false"`**
durcit l'équipement ; tout le reste — paramètre absent ou vide inclus, c'est-à-dire les configs
antérieures à T2.17 — signifie *insecure* (dérivé, `src/lib/UrlDownloader.h:153-157`) :

```cpp
static bool insecureParamEnabled(const std::string &paramValue)
{ return paramValue != "false"; }
```

### Borne mémoire

Le corps de réponse accumulé en interne est plafonné à `defaultBufferMaxSize = 16 * 1024 * 1024`
(dérivé, `src/lib/UrlDownloader.h:123`), réglable par `bufferMaxSizeSet()`. Les consommateurs en
flux (MJPEG) reçoivent **tous** les octets en direct par `m_signalData` ; seule la copie interne
destinée à `m_signalCompleteData` cesse de croître, pour qu'un flux sans fin ne mange pas la RAM
(dérivé, `src/lib/UrlDownloader.h:96-100`).

---

## WebSocketFrame

**Fichier :** [src/lib/WebSocketFrame.h](../src/lib/WebSocketFrame.h)

Parsing et construction de frames WebSocket (RFC 6455). Utilisé par `WebSocket`, `HttpClient`,
`HttpServer`, `JsonApiHandlerWS` et `RemoteUIManager` (dérivé — les cinq fichiers l'incluent).

> **SHA1 → OpenSSL (T2.3).** `src/lib/SHA1.{cpp,h}` **n'existent plus**. Le seul condensé SHA-1
> encore calculé est celui du handshake WebSocket, et il passe par l'EVP d'OpenSSL
> (dérivé, `src/bin/calaos_server/WebSocket.cpp:400-408`) :
>
> ```cpp
> uint8_t message_digest[EVP_MAX_MD_SIZE];
> unsigned int digest_len = 0;
> if (EVP_Digest(key.data(), key.size(), message_digest, &digest_len,
>                EVP_sha1(), nullptr) != 1 ||
>     digest_len != 20)
> ```
>
> Les occurrences textuelles de « SHA1 » restantes dans l'arbre sont ce site-là et son message de
> log. C'est un usage imposé par la RFC 6455, pas un choix cryptographique.

---

## base64

**Fichier :** [src/lib/base64.h](../src/lib/base64.h)

```cpp
std::optional<std::string> base64_decode_checked(std::string const &s);
std::string base64_decode(std::string const &s);
```

`base64_decode_checked()` rejette une entrée invalide (caractère hors alphabet, longueur
impossible — 4k+1 symboles —, bits de fin non canoniques) ; une entrée **sans padding** mais de
longueur valide est acceptée, et une entrée vide se décode en chaîne vide. `base64_decode()` est
le shim de compatibilité qui renvoie `""` sur entrée invalide, là où historiquement l'entrée était
**silencieusement tronquée** (dérivé, `src/lib/base64.h:20-26`).

Le versant *encodage* utilisé par le reste du code est `Utils::Base64_encode()` de `StringUtils`
(dérivé, `src/lib/StringUtils.h:66-67`).

---

## tcpsocket

**Fichier :** [src/lib/tcpsocket.h](../src/lib/tcpsocket.h)

Classe `TCPSocket` : socket **synchrone**, TCP ou UDP (`Create(int nPort, char nType)` avec
`TCPSocketTCP` / `TCPSocketUDP`), avec `Listen()`, `Accept()`, `Connect()`, `Send()`, `Recv()`,
`Broadcast()`, `SendTo()` (dérivé, `src/lib/tcpsocket.h:37-79`).

Appelants réels : le serveur de découverte UDP (`UDPServer.h`), le driver Wago
(`IO/Wago/WagoMap.cpp`), l'ampli Hifi Rose (`Audio/AVRRose.cpp`), l'outil `tools/wago_test.cpp` et
`src/lib/Utils.cpp` (dérivé — grep des inclusions de `tcpsocket.h`). Les autres drivers réseau
passent par `UrlDownloader` (HTTP) ou par `uvw` directement.

---

## ExpressionEvaluator

**Fichier :** [src/lib/ExpressionEvaluator.h](../src/lib/ExpressionEvaluator.h)

Évaluation d'expressions mathématiques, basée sur la bibliothèque embarquée `exprtk`. L'API est
faite de **trois fonctions statiques**, sans objet ni variables nommées
(dérivé, `src/lib/ExpressionEvaluator.h:6-13`, intégral) :

```cpp
class ExpressionEvaluator
{
public:

    static bool isExpressionValid(const std::string &expression);
    static double calculateExpression(const std::string &expression, double rawValue, bool &failed);
    static bool evaluateExpressionBool(const std::string &expression, const std::string &rawValue, bool &failed);
};
```

La valeur d'entrée est passée en paramètre (`rawValue`), et l'échec est remonté par le booléen de
sortie `failed`.

---

## TimeRange et sunset

**Fichiers :** [src/lib/TimeRange.h](../src/lib/TimeRange.h), [src/lib/sunset.h](../src/lib/sunset.h)

`TimeRange` porte les plages horaires (utilisées par `InPlageHoraire` et les auto-scénarios) et
inclut directement `sunset.h` pour les bornes relatives au soleil, calculées depuis la latitude et
la longitude (dérivé — `sunset.h` n'est inclus que par `src/lib/TimeRange.h` et
`src/lib/TimeRange.cpp`). `TimeRange` lit `latitude` / `longitude` dans les options de
configuration ; c'est d'ailleurs pour cette raison que `tests/check-config-options.sh` scanne ce
fichier avec une règle dédiée (règle D, dérivé, `tests/check-config-options.sh:277-284`).

⚠️ **Plages nocturnes (T3.13)** : une plage inversée (`23:00 → 01:00`) était **vide et ne se
déclenchait jamais** ; elle wrappe désormais sur minuit. Détails et effets de bord dans
[04_scenarios.md](04_scenarios.md).

---

## Calendar

**Fichier :** [src/lib/Calendar.h](../src/lib/Calendar.h)

Classes `TimeZoneElt`, `TimeZone` et `Calendar` : sélection de fuseau horaire (lecture de
`/etc/timezone` et de `zone.tab`), réglage heure/date, synchronisation de l'horloge matérielle
(`syncHwClock()`) (dérivé, `src/lib/Calendar.h:37-120`).

⚠️ **Ce module n'a aucun consommateur aujourd'hui.** `Calendar.h` n'est inclus que par son propre
`Calendar.cpp` et par `src/lib/Makefile.am` : il est compilé dans `libcalaos_common` mais rien ne
l'appelle. Il n'offre par ailleurs **aucune gestion de jours fériés**, et **`TimeRange` ne
l'utilise pas** — les deux affirmations contraires figuraient dans ce document avant la revue
E4.5e. C'est un vestige de l'interface tactile.

---

## ThreadedQueue

**Fichier :** [src/lib/ThreadedQueue.h](../src/lib/ThreadedQueue.h)

File d'attente thread-safe. Unique consommateur : `HistLogger`, qui fait tourner la base
d'historique dans un thread séparé (dérivé — `ThreadedQueue` n'apparaît que dans
`src/bin/calaos_server/HistLogger.h`).

---

## Prefix

**Fichier :** [src/lib/Prefix.h](../src/lib/Prefix.h)

Singleton qui retrouve le préfixe d'installation de Calaos à partir d'`argv[0]`, pour en dériver
les répertoires : `binDirectoryGet()`, `libDirectoryGet()`, `dataDirectoryGet()`. Installé dans
`/opt/usr`, `dataDirectoryGet()` renvoie `/opt/usr/share/calaos`
(dérivé, `src/lib/Prefix.h:30-53`).

---

## Ponts JSON

**Fichiers :** [src/lib/Jansson_Addition.h](../src/lib/Jansson_Addition.h),
[src/lib/Json_Addition.h](../src/lib/Json_Addition.h)

Deux bibliothèques JSON **cohabitent** dans l'arbre, et ce n'est pas un état transitoire non
documenté : le chemin de l'API (`JsonApi.cpp`) est en **jansson**, tandis que le cache d'état et
`eventlog` sont en **nlohmann**. `Params` sait sérialiser vers les deux — `toJson()` renvoie un
`json_t *`, `toNJson()` un `Json` (dérivé, `src/lib/Params.h:68-69`).

- `Jansson_Addition.h` — accesseurs typés tolérants au-dessus de jansson
  (`jansson_bool_get()`, `jansson_string_get()`…), qui renvoient une valeur par défaut au lieu de
  déréférencer un `json_t *` nul (dérivé, `src/lib/Jansson_Addition.h:29-39`).
- `Json_Addition.h` — un `adl_serializer` nlohmann pour `std::optional<T>` : `nullopt` sérialise
  en `null` et réciproquement (dérivé, `src/lib/Json_Addition.h:24-41`).

---

## CalaosModule.h

**Fichier :** [src/lib/CalaosModule.h](../src/lib/CalaosModule.h)

API de modules pour l'interface tactile (widgets), fondée sur EFL — `Evas`, `Ecore`, `Edje`
(dérivé, `src/lib/CalaosModule.h:24-31`). ⚠️ **Aucun fichier de l'arbre ne l'inclut** : il ne
figure que dans `src/lib/Makefile.am:52`. Vestige, conservé pour mémoire.

---

## calaos-python

**Dossier :** [src/lib/calaos-python/](../src/lib/calaos-python/)

Paquet Python `calaos_extern_proc`, partagé par tous les sidecars Python (Reolink, Roon, MCP) :
`extern_proc.py` (client du protocole `ExternProc`), `message.py` (framing), `logger.py`
(logging aligné sur les domaines et niveaux du serveur, via `CALAOS_LOG_LEVEL` /
`CALAOS_LOG_DOMAINS`). Voir [14_python_extern_proc.md](14_python_extern_proc.md).

---

## Bibliothèques tierces embarquées

Toutes sont déclarées dans [src/lib/Makefile.am](../src/lib/Makefile.am) (dérivé,
`src/lib/Makefile.am:51-142`).

| Bibliothèque | Emplacement | Usage |
|---|---|---|
| `pugixml` | `src/lib/pugixml/` | DOM XML + XPath 1.0. `configure` **préfère le paquet distro** ; la copie embarquée n'est compilée que s'il est absent (`if HAVE_PUGIXML_SYSTEM`). Sources upstream verbatim, ne pas les éditer. |
| `exprtk` | `src/lib/exprtk/` | Évaluation d'expressions mathématiques (header-only, chemin d'inclusion) |
| `libquickmail` | `src/lib/libquickmail/` | Envoi d'e-mails SMTP |
| `llhttp` | `src/lib/llhttp/` | Parsing HTTP (compilé : `api.c`, `http.c`, `llhttp.c`) |
| `sole` | `src/lib/sole/` | Génération d'UUID v4 (header-only) |
| `sqlite_modern_cpp` | `src/lib/sqlite_modern_cpp/hdr/` | Interface C++ moderne pour SQLite (header-only) |
| `uri_parser` | `src/lib/uri_parser/` | Parsing d'URIs (`hef_uri_syntax`) |
| `uvw` | `src/lib/uvw/src/` | Wrapper C++ de libuv (header-only) |
| `sunset` | `src/lib/sunset.c` | Lever/coucher du soleil selon latitude/longitude |
| `nlohmann/json` | `src/lib/json.hpp` | JSON, exposé sous l'alias `Json` par `Utils.h:79-80` |
| `cpptui` | `src/lib/cpptui/` | TUI header-only (18 k lignes), utilisée **uniquement** par `src/bin/tools/config_tui/`. `EXTRA_DIST` seulement : rien à compiler (`src/lib/Makefile.am:114-119`) |

**Retirée :** TinyXML 2.5.3 + TinyXPath (E4.4e) — 14 242 lignes, deux CVE, remplacée par pugixml.

**Dépendances système** (non embarquées) : `jansson`, `libcurl`, `libuv`, `sigc++`, `sqlite3`,
`openssl`, `lua`/`luajit`.
