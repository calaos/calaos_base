# Lua Scripting — Moteur de scripts

## Vue d'ensemble

Calaos intègre un moteur de scripts **LuaJIT** (`requirements_calaos_common` de
[configure.ac:51](../configure.ac)) permettant d'écrire des conditions et des actions complexes.
Les scripts sont utilisés dans :

- `ConditionScript` — condition évaluée par un script ;
- `ActionScript` — action exécutée par un script.

**Deux propriétés structurent tout le reste :**

1. **Un script ne tourne jamais dans `calaos_server`.** Il est exécuté dans un sous-processus
   dédié `calaos_script` (dérivé, `src/bin/calaos_server/LuaScript/ScriptExec.cpp:188-189`) ;
   `calaos_server` ne lie même pas `ScriptManager.cpp`
   (dérivé, `src/bin/calaos_server/Makefile.am:282-283` et `:356-364`).
2. **Le script tourne dans un bac à sable (T1.15)** : l'environnement global est reconstruit à
   partir d'une **liste blanche explicite**, et une garde de temps d'exécution l'interrompt au
   bout de 5 secondes.

> **Provenance.** Aucun des 145 fichiers de référence de `tests/core/golden/` ne couvre le moteur
> Lua. Les affirmations ci-dessous sont donc adossées au **code** (`Fichier.cpp:ligne` vérifié) et
> aux **suites de test** ; c'est signalé à chaque fois.

---

## Modèle d'exécution — le sous-processus `calaos_script`

**Fichiers :** [src/bin/calaos_server/LuaScript/ScriptExec.h](../src/bin/calaos_server/LuaScript/ScriptExec.h),
[ScriptExec.cpp](../src/bin/calaos_server/LuaScript/ScriptExec.cpp),
[ScriptExtern_main.cpp](../src/bin/calaos_server/LuaScript/ScriptExtern_main.cpp)

`ScriptExec` n'a **qu'une seule fonction, statique** (dérivé,
`src/bin/calaos_server/LuaScript/ScriptExec.h:34`) :

```cpp
static ExternProcServer *ExecuteScriptDetached(const string &script,
                                               std::function<void(bool ret)> cb,
                                               Params env = Params());
```

Elle ne conserve **aucun contexte de règle** : le seul état passé au script est la `Params env`.

Déroulé (dérivé, `src/bin/calaos_server/LuaScript/ScriptExec.cpp:46-191`) :

1. `ExternProcServer *process = new ExternProcServer("lua");` (`:48`) ;
2. le binaire est lancé — `Prefix::Instance().binDirectoryGet() + "/calaos_script"` puis
   `process->startProcess(exe, "lua");` (`:188-189`) ;
3. à la connexion, le serveur envoie un message `execute` portant le **texte du script**, le
   **contexte** (`jsonApi->buildFlatIOList()`) et l'**env** (`:154-165`) ;
4. ensuite, tous les événements IO sont relayés au sous-processus pour qu'il tienne son cache
   local à jour — `EventIOAdded`, `EventIODeleted`, `EventIOChanged`, `EventIOPropertyDelete`,
   `EventIOStatusChanged` (`:171-185`) ;
5. le sous-processus répond `finished` avec `return_val` valant `"true"` ou `"false"` (dérivé,
   `src/bin/calaos_server/LuaScript/ScriptExtern_main.cpp:136-144`), puis `::exit(0)`.

Le binaire est déclaré dans [src/bin/calaos_server/Makefile.am:356-364](../src/bin/calaos_server/Makefile.am) :

```make
bin_PROGRAMS += calaos_script
calaos_script_SOURCES = \
        LuaScript/ScriptExtern_main.cpp \
        LuaScript/ScriptManager.h \
        LuaScript/ScriptManager.cpp \
        LuaScript/ScriptBindings.h \
        LuaScript/ScriptBindings.cpp \
        IO/ExternProc.cpp \
        IO/ExternProc.h
```

(capturé, `src/bin/calaos_server/Makefile.am:356-364`, intégral.)

### Appelants

- `ConditionScript::EvaluateAsync()` (dérivé, `src/bin/calaos_server/Rules/ConditionScript.cpp:42-50`) ;
- `ActionScript::Execute()` (dérivé, `src/bin/calaos_server/Rules/ActionScript.cpp:36`).

⚠️ `ConditionScript::Evaluate()` (la variante synchrone) est un **chemin d'erreur dur** : elle
journalise `Scripts needs to be evaluated using EvaluateAsync() !` et renvoie `false`
(dérivé, `src/bin/calaos_server/Rules/ConditionScript.cpp:38-39`).

### Deuxième borne : durée de vie du processus

```cpp
#define SCRIPT_PROCESS_MAX_LIFETIME 3600.0
```

(capturé, `src/bin/calaos_server/LuaScript/ScriptExec.cpp:44`, intégral.) Un `Timer` armé à la
connexion tue le processus au-delà (`:142-151`). La borne est **volontairement très au-dessus** de
`SCRIPT_MAX_EXEC_TIME`, parce que `calaos:waitForIO()` gare légitimement un script aussi longtemps
que l'IO attendu met à changer (dérivé, `src/bin/calaos_server/LuaScript/ScriptExec.cpp:40-42`).

---

## Le bac à sable (T1.15)

**Fichier :** [src/bin/calaos_server/LuaScript/ScriptManager.cpp](../src/bin/calaos_server/LuaScript/ScriptManager.cpp)

`luaL_openlibs()` n'est **jamais** appelée : elle ouvrirait aussi `io`, `debug` et `package` — et
avec lui `require()` et la bibliothèque `ffi` préchargée de LuaJIT (dérivé,
`src/bin/calaos_server/LuaScript/ScriptManager.cpp:94-98`). L'environnement est bâti à la main par
`buildSandboxEnv()` (`:99-141`) et installé comme table globale (`lua_replace(L,
LUA_GLOBALSINDEX)`, `:140`) ; `_G` pointe sur le bac à sable lui-même, donc pas de porte dérobée
(`:137-138`).

### Bibliothèques ouvertes (5)

`base`, `string`, `table`, `math`, `os` (dérivé, `src/bin/calaos_server/LuaScript/ScriptManager.cpp:40-48`).

### Fonctions de base autorisées (12)

`assert`, `error`, `ipairs`, `next`, `pairs`, `pcall`, `select`, `tonumber`, `tostring`, `type`,
`unpack`, `xpcall` (capturé, `src/bin/calaos_server/LuaScript/ScriptManager.cpp:56-71`, intégral
pour la liste).

### Tables passées entières (3)

`string`, `table`, `math` (dérivé, `src/bin/calaos_server/LuaScript/ScriptManager.cpp:74-80`).

### `os` reconstruit — 4 fonctions seulement

`clock`, `date`, `difftime`, `time` (dérivé,
`src/bin/calaos_server/LuaScript/ScriptManager.cpp:85-92`). Le commentaire du code nomme les
exclues : « *execute, remove, rename, getenv, exit, tmpname and setlocale are left out* » (`:82-84`).

### `print` est **remplacée**, pas retirée

Elle route vers le log du serveur : `cInfoDom("script.lua") << "LuaPrint: " << msg;`
(dérivé, `src/bin/calaos_server/LuaScript/ScriptManager.cpp:133-135` et
`src/bin/calaos_server/LuaScript/ScriptBindings.cpp:84`).

### Ce qui n'est plus accessible

Rien n'est « supprimé » : ce qui n'est pas dans la liste blanche n'entre simplement pas. Le
commentaire du code explique le raisonnement (dérivé,
`src/bin/calaos_server/LuaScript/ScriptManager.cpp:50-54`) : `load`/`loadstring`/`loadfile`/`dofile`
chargent du code (bytecode compris) depuis n'importe où ; `getfenv`/`setfenv`/`rawget`/`rawset`/
`rawequal`/`setmetatable`/`getmetatable`/`newproxy` s'échappent de l'environnement sur lequel le
bac à sable est construit ; `collectgarbage`/`gcinfo`/`coroutine` n'ont aucun usage dans une règle.

La liste faisant foi est celle **assertée par le test** `LuaSandbox.DangerousGlobalsAreAbsent`
(dérivé, `tests/LuaSandbox_test.cpp:60-92`) — 29 noms qui doivent tous valoir `nil` :

> `io`, `os.execute`, `os.remove`, `os.rename`, `os.getenv`, `os.exit`, `os.tmpname`,
> `os.setlocale`, `package`, `require`, `module`, `load`, `loadstring`, `loadfile`, `dofile`,
> `debug`, `ffi`, `jit`, `getfenv`, `setfenv`, `setmetatable`, `getmetatable`, `rawget`, `rawset`,
> `rawequal`, `collectgarbage`, `newproxy`, `gcinfo`, `coroutine`

---

## Le watchdog d'exécution (T1.15)

```cpp
/* Maximum time, in seconds, a script may spend inside the lua interpreter
 * before it gets aborted. Time spent blocked in a binding that waits on the
 * outside world (waitForIO(), requestUrl()) is not counted.
 */
#define SCRIPT_MAX_EXEC_TIME 5.0
```

(capturé, `src/bin/calaos_server/LuaScript/ScriptManager.h:27-31`, intégral.)

Le hook est installé **avant** le chargement du script (dérivé,
`src/bin/calaos_server/LuaScript/ScriptManager.cpp:208`, intégral) :

```cpp
lua_sethook(L, Lua_DebugHook, LUA_MASKLINE | LUA_MASKCOUNT, 1);
```

Le compteur d'instructions vaut **1** : le hook se déclenche à chaque ligne **et** à chaque
instruction de la VM, ce qui le rend efficace même sur une trace compilée par le JIT — c'est
précisément ce que vérifie `LuaSandbox.HotNumericLoopIsAbortedByTheWatchdog`
(dérivé, `tests/LuaSandbox_test.cpp:225`).

À l'expiration, `Calaos::Lua_DebugHook` lève une erreur Lua dont le message est
`"Aborting script, takes too much time to execute (<elapsed> sec.)"`
(dérivé, `src/bin/calaos_server/LuaScript/ScriptBindings.cpp:94-101`).

**Horloge.** `ScriptManager::monotonicTime()` s'appuie sur `std::chrono::steady_clock`, et
**pas** sur `Utils::getMainLoopTime()` : cette dernière renvoie l'heure de boucle mise en cache par
libuv, qui n'est rafraîchie qu'à chaque itération. Or un script s'exécute intégralement à
l'intérieur d'une seule itération, donc cette valeur resterait figée pendant toute son exécution
(dérivé, `src/bin/calaos_server/LuaScript/ScriptManager.h:65-68`).

**Pause/reprise.** Le temps passé bloqué à l'extérieur de l'interpréteur ne compte pas :
`watchdogPause()` / `watchdogResume()` sont à compteur de références, et la dernière reprise
décale `start_time` de la durée de la pause (dérivé,
`src/bin/calaos_server/LuaScript/ScriptManager.cpp:163-176`). Le wrapper RAII
`ScriptWatchdogPause` (dérivé, `src/bin/calaos_server/LuaScript/ScriptManager.h:96-104`) porte un
avertissement à respecter : **ne jamais en garder un vivant à travers un `lua_error()`**, le
`longjmp` sauterait le destructeur (`:93-94`).

---

## ScriptManager

**Fichier :** [src/bin/calaos_server/LuaScript/ScriptManager.h](../src/bin/calaos_server/LuaScript/ScriptManager.h)

Singleton (constructeur privé). API publique réellement existante :

```cpp
static ScriptManager &Instance();          //ScriptManager.h:46-50
bool ExecuteScript(const string &script);  //ScriptManager.h:55
string getErrorMsg();                      //ScriptManager.h:58
bool hasError();                           //ScriptManager.h:60
void abortScript();                        //ScriptManager.h:90

// Watchdog (statique)
static double monotonicTime();             //ScriptManager.h:70
static void watchdogStart();               //ScriptManager.h:73
static void watchdogPause();               //ScriptManager.h:78
static void watchdogResume();              //ScriptManager.h:79
static bool watchdogExpired(double &elapsed); //ScriptManager.h:82
```

### Contrat de retour d'`ExecuteScript()`

⚠️ **Un script doit renvoyer un booléen.** Renvoyer `nil`, un nombre ou une chaîne n'est **pas**
un « faux », c'est une **erreur** : `errorScript` reste vrai et `getErrorMsg()` vaut
`"Error:\nScript must return either \"true\" or \"false\""`
(dérivé, `src/bin/calaos_server/LuaScript/ScriptManager.cpp:255-260`). Le succès n'est enregistré
que dans la branche `lua_isboolean` (`:261-265`).

⚠️ `abortScript()` positionne `abort = true` et **rien ne le remet à `false`** : une instance de
`ScriptManager` qui a été abortée abortera tous les scripts suivants. Sans conséquence en
production, puisque le processus `calaos_script` sort après un seul script
(dérivé, `src/bin/calaos_server/LuaScript/ScriptExtern_main.cpp:144`).

---

## ScriptBindings — l'API Lua exposée

**Fichier :** [src/bin/calaos_server/LuaScript/ScriptBindings.h](../src/bin/calaos_server/LuaScript/ScriptBindings.h)

Le script voit un objet global **`calaos`** (minuscule), un *userdata* pointant sur le singleton
`Lua_Calaos`, enregistré via `Lunar` (dérivé,
`src/bin/calaos_server/LuaScript/ScriptManager.cpp:200-202`) :

```cpp
Lunar<Lua_Calaos>::Register(L);
Lunar<Lua_Calaos>::push(L, &luaCalaos);
lua_setglobal(L, "calaos");
```

Les méthodes s'appellent avec la syntaxe **deux-points** — `calaos:getIOValue(id)` — parce que
`Lunar::thunk` retire l'objet du premier argument
(dérivé, `src/bin/calaos_server/LuaScript/Lunar.h:132-133`).

### Les 11 entrées de la table de méthodes

Onze entrées utiles, plus la **sentinelle `{ 0, 0 }`** qui termine le tableau — soit douze lignes
dans l'initialiseur (capturé, `src/bin/calaos_server/LuaScript/ScriptBindings.cpp:106-120`,
intégral :)

```cpp
Lunar<Lua_Calaos>::RegType Lua_Calaos::methods[] =
{
    { "getOutputValue", &Lua_Calaos::getIOValue },
    { "setOutputValue", &Lua_Calaos::setIOValue },
    { "getInputValue", &Lua_Calaos::getIOValue },
    { "getIOValue", &Lua_Calaos::getIOValue },
    { "setIOValue", &Lua_Calaos::setIOValue },
    { "getIOParam", &Lua_Calaos::getIOParam },
    { "setIOParam", &Lua_Calaos::setIOParam },
    { "waitForIO", &Lua_Calaos::waitForIO },
    { "requestUrl", &Lua_Calaos::requestUrl },
    { "sendPushNotif", &Lua_Calaos::sendPushNotif },
    { "getEnv", &Lua_Calaos::getEnv },
    { 0, 0 }
};
```

⚠️ **C'est la liste complète.** `getOutputValue` / `getInputValue` / `getIOValue` sont **trois
alias d'une seule fonction**, et `setOutputValue` / `setIOValue` **deux alias** d'une autre.
Il n'existe **ni** `get_io`, `get_io_bool`, `get_io_number`, `get_io_string`, `set_io`,
`get_param`, `set_param`, `log`, `get_hour`, `get_minute`, `get_second`, `get_day`, `get_month`,
`get_year`, **ni** `get_weekday` — ces quinze noms figuraient dans ce document avant la revue
E4.5e et **aucun n'existe** : un script qui les appelle échoue avec
`attempt to call method '…' (a nil value)`.

### Détail des fonctions

| Fonction | Arité | Retour | Site |
|---|---|---|---|
| `calaos:getIOValue(id)` | 1 (string) | 1 valeur, **typée d'après le param `var_type`** de l'IO : `"float"` → nombre, `"bool"` → booléen, sinon chaîne | `ScriptBindings.cpp:135-167` |
| `calaos:setIOValue(id, value)` | 2 | rien | `ScriptBindings.cpp:169-207` |
| `calaos:getIOParam(id, key)` | 2 (string, string) | 1 valeur, coercée : numérique → nombre, `"true"`/`"false"` → booléen, sinon chaîne | `ScriptBindings.cpp:209-248` |
| `calaos:setIOParam(id, key, value)` | 3 | ⚠️ déclare `return 1` mais **n'empile rien** | `ScriptBindings.cpp:250-294` |
| `calaos:waitForIO(id)` | 1 | ⚠️ déclare `return 1` mais **n'empile rien** ; bloque sous `ScriptWatchdogPause` | `ScriptBindings.cpp:296-334` |
| `calaos:requestUrl(url [, post_data])` | 1 ou 2 | rien — ⚠️ **le corps de la réponse est jeté** | `ScriptBindings.cpp:336-377` |
| `calaos:sendPushNotif(message [, attachment])` | 1 ou 2 | rien | `ScriptBindings.cpp:405-428` |
| `calaos:getEnv(key)` | 1 | toujours une chaîne, vide si la clé est absente | `ScriptBindings.cpp:379-403` |

Le type de valeur transmis à `setIOValue()` est dispatché depuis le type Lua vers
`set_value(double)` / `set_value(bool)` / `set_value(std::string)`
(dérivé, `src/bin/calaos_server/LuaScript/ScriptBindings.cpp:185-190`), chacun émettant un message
`set_state` vers le processus parent (`:456-478`).

### `getEnv()` — une seule clé aujourd'hui

`ConditionScript::EvaluateAsync()` passe l'initialiseur `{{ "trigger_id", triggerId }}` comme
troisième argument d'`ExecuteScriptDetached()` (extrait — la ligne source porte en plus la
fermeture d'appel, `src/bin/calaos_server/Rules/ConditionScript.cpp:49`). `ActionScript::Execute()`
ne passe **aucun** env : la valeur par défaut `Params env = Params()` s'applique
(dérivé, `src/bin/calaos_server/LuaScript/ScriptExec.h:34`). Donc `trigger_id` est **la seule clé
peuplée**, et seulement dans une condition.

### `requestUrl()` et TLS (T2.19, décision utilisateur)

⚠️ **La vérification de certificat est désactivée en dur, sans opt-out.** Contrairement aux IOs
Web et caméras, qui honorent un paramètre `insecure` par équipement via `setInsecureFromParam()`,
`requestUrl()` appelle `setInsecure()` **inconditionnellement** à chaque appel
(capturé, `src/bin/calaos_server/LuaScript/ScriptBindings.cpp:344-349`, intégral) :

```cpp
UrlDownloader *dl = new UrlDownloader(url, true);
//T2.19: user-scripted URL, insecure by default (policy). No natural
//per-item config exists here: the URL comes from the Lua code at
//runtime, there is no device param to opt in to verification.
dl->setInsecure();
dl->httpGet();
```

Le raisonnement figure dans le commentaire : l'URL vient du code Lua **à l'exécution**, il n'y a
aucun paramètre d'équipement où déclarer une préférence. Le variant POST est identique avec
`dl->httpPost(string(), post_data);` (`:361-364`).

Les deux chemins mettent ensuite le watchdog en pause et font tourner la boucle jusqu'à la fin du
téléchargement (`:353-354`, `:366-367`).

### Le global `Calaos` (majuscule)

`Lunar::Register()` dépose aussi la table de méthodes dans les globales sous le nom de classe
`"Calaos"` (dérivé, `src/bin/calaos_server/LuaScript/Lunar.h:25-26` et
`src/bin/calaos_server/LuaScript/ScriptBindings.cpp:122`), avec une entrée `new`. Appeler
`Calaos:new()` lève systématiquement. Contenu du littéral de message, recopié à l'octet près
depuis `src/bin/calaos_server/LuaScript/ScriptBindings.cpp:130` (la faute de frappe est dans la
source) :

```
Calaos(): Don't create a new object, juste use the existing one "calaos:..."
```

---

## Exemples

⚠️ Ces exemples n'utilisent que des noms **vérifiés dans la table de méthodes** ci-dessus.

### ConditionScript

```lua
-- Vrai si la température > 25°C ET c'est l'été
local temp = calaos:getIOValue("id-temp-salon")
local month = tonumber(os.date("%m"))
return temp > 25.0 and (month >= 6 and month <= 8)
```

`os.date` est disponible (liste blanche `sandbox_os`, `ScriptManager.cpp:85-92`) ; il n'y a pas de
binding d'heure côté `calaos:`.

### ActionScript

```lua
-- Calculer et appliquer une valeur dérivée
local lum = calaos:getIOValue("id-capteur-lumiere")
local dim = math.min(100, lum / 10)
calaos:setIOValue("id-gradateur", dim)
return true
```

⚠️ Le `return true` final n'est pas décoratif : un script qui ne renvoie pas de booléen est traité
comme une **erreur** (voir *Contrat de retour* ci-dessus).

### Savoir quel IO a déclenché la condition

```lua
local trigger = calaos:getEnv("trigger_id")
if trigger == "" then return false end
return calaos:getIOValue(trigger) == true
```

---

## Lunar

**Fichier :** [src/bin/calaos_server/LuaScript/Lunar.h](../src/bin/calaos_server/LuaScript/Lunar.h)

Bibliothèque header-only pour binder des classes C++ dans Lua. Utilisée pour exposer
`Lua_Calaos`. L'objet est poussé avec `gc = false`, donc Lua ne le détruit jamais
(dérivé, `src/bin/calaos_server/LuaScript/Lunar.h:92` et `:104-111`).

---

## Stockage des scripts

Les scripts sont stockés **inline dans `rules.xml`**, dans un nœud `<calaos:script type="lua">`
lui-même enfant du nœud de condition ou d'action, et écrits en **CDATA**.

Écriture d'une condition (dérivé,
`src/bin/calaos_server/Rules/ConditionScript.cpp:149-165`) : `<calaos:condition type="script">`,
puis un `<calaos:input id="…"/>` par déclencheur, puis `<calaos:script type="lua">` dont le corps
est posé par `XmlUtils::appendCData()` (`:163`). L'écriture d'une action est la même sans les
`<calaos:input>` (dérivé, `src/bin/calaos_server/Rules/ActionScript.cpp:61-72`).

Forme réelle. Ceci est le XML **émis** par la fixture de la suite `RuleIoReference_test`, dérivé
des littéraux de `tests/core/RuleIoReference_test.cpp:94-102` (ce sont des `ss << "…"` échappés en
C++, pas ce bloc tel quel ; les valeurs entre `« »` sont substituées à l'exécution) :

```xml
<calaos:rule name="«name»" type="rule">
  <calaos:condition type="script">
    <calaos:input id="«triggerId»" />
    <calaos:script type="lua"><![CDATA[return true]]></calaos:script>
  </calaos:condition>
  <calaos:action type="standard">
    <calaos:output id="«outputId»" val="true" />
  </calaos:action>
</calaos:rule>
```

⚠️ **Le nœud n'est pas `<condition type="ConditionScript">`** — c'est
`<calaos:condition type="script">`. La valeur de l'attribut est `"script"` en minuscules, et c'est
sur ce littéral que dispatche la fabrique (dérivé,
`src/bin/calaos_server/Rules/RulesFactory.cpp:77` pour les conditions, `:123` pour les actions).

**Le CDATA n'est pas obligatoire à la lecture** : `XmlUtils::text()` concatène les nœuds texte
**et** CDATA (dérivé, `src/lib/XmlUtils.h:127-138`), si bien qu'un corps en texte brut se relit
identiquement — la fixture `RuleLifecycle_test` s'en sert justement sous cette forme (XML émis,
dérivé du littéral de `tests/core/RuleLifecycle_test.cpp:93`) :

```xml
<calaos:script type="lua">return true</calaos:script>
```

> **Fidélité XML (E4.4cd).** Deux pertes de données de longue date sur ces blocs CDATA ont été
> corrigées avec le passage à pugixml : l'ancien imprimeur ajoutait **+18 octets** d'espacement
> parasite à chaque sauvegarde, et un corps contenant la séquence `]]>` était relu **tronqué**. Le
> corps est désormais restitué à l'octet près. Détails dans
> [11_config_persistence.md](11_config_persistence.md).

⚠️ **Condition et action ne rejettent pas la même chose — c'est une asymétrie réelle.**

- Côté **condition**, `ConditionScript::LoadFromXml()` ne renvoie `false` que si le nœud n'a
  **aucun enfant élément** (`XmlUtils::firstChildElement(node)` nul,
  `src/bin/calaos_server/Rules/ConditionScript.cpp:102-103`). Des enfants élément **sans**
  `<calaos:script>` parmi eux se chargent normalement et la fonction renvoie `true` (`:146`) : la
  condition existe, son script est simplement vide.
- Côté **action**, `ActionScript::LoadFromXml()` cherche directement
  `pnode.child("calaos:script")` et renvoie `false` s'il est absent
  (`src/bin/calaos_server/Rules/ActionScript.cpp:46-47`) : là, l'absence du nœud `<calaos:script>`
  fait bien échouer le chargement.

Le cas de la condition vide est couvert par
`tests/core/RuleDisabledMissingIo_test.cpp:295`, qui charge un
`<calaos:condition type="script"></calaos:condition>` **entièrement vide** et vérifie que le nœud
est abandonné sans que la règle soit désactivée pour autant.

---

## Tests

| Suite | Couvre |
|---|---|
| [tests/LuaSandbox_test.cpp](../tests/LuaSandbox_test.cpp) | Les 9 tests T1.15 : absence des 29 globales dangereuses (`:60`), `ffi` inatteignable (`:100`), `os.execute` et `io.open` qui échouent **sans rien écrire** (témoins dans `/tmp`, `:112` et `:130`), bibliothèque standard sûre encore fonctionnelle (`:144`), `os` réduit au temps (`:174`), API `calaos` exposée (`:189`), boucle infinie et boucle numérique chaude interrompues par le watchdog (`:210`, `:225`) |
| [tests/core/RuleLifecycle_test.cpp](../tests/core/RuleLifecycle_test.cpp) | Dispatch et durée de vie des règles à script : règle désactivée si l'IO déclencheur disparaît (`:164`), une seule dispatch pour plusieurs conditions script (`:636`), complétion sur une règle **détruite** ignorée grâce à `Rule::aliveToken()` (`:778`) — « *nothing cancels a ScriptExec callback* » |
| [tests/core/RuleIoReference_test.cpp:434](../tests/core/RuleIoReference_test.cpp) | `ScriptTriggersAreComparedById` |
| [tests/core/RuleDisabledMissingIo_test.cpp:550](../tests/core/RuleDisabledMissingIo_test.cpp) | Un déclencheur script introuvable désactive la règle **et conserve l'id** à la sauvegarde |

Non couverts par un test : `requestUrl()` contre un vrai serveur, `waitForIO()`,
`sendPushNotif()`, et le bout-en-bout du sous-processus `calaos_script`.
