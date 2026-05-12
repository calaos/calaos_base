# Lua Scripting — Moteur de scripts

## Vue d'ensemble

Calaos intègre un moteur de scripts **LuaJIT** permettant d'écrire des conditions et des actions complexes. Les scripts sont utilisés dans :
- `ConditionScript` — condition évaluée par un script
- `ActionScript` — action exécutée par un script

---

## ScriptManager

**Fichier :** [src/bin/calaos_server/LuaScript/ScriptManager.h](../src/bin/calaos_server/LuaScript/ScriptManager.h)

Singleton. Gère l'état du moteur Lua et exécute les scripts.

```cpp
bool ScriptManager::Instance().ExecuteScript(const string &script);
// retourne true si le script retourne true (pour ConditionScript)
// retourne false en cas d'erreur ou si le script retourne false/nil

string ScriptManager::Instance().getErrorMsg();
bool ScriptManager::Instance().hasError();
void ScriptManager::Instance().abortScript();  // arrêt forcé
```

### Protection anti-boucle infinie

Un hook de debug Lua (`LuaDebugHook`) est installé pour limiter le temps d'exécution. La variable `start_time` est enregistrée au début de l'exécution. Si l'exécution dépasse un seuil, le script est interrompu.

---

## ScriptBindings — API Lua exposée

**Fichier :** [src/bin/calaos_server/LuaScript/ScriptBindings.h](../src/bin/calaos_server/LuaScript/ScriptBindings.h)

Classe `Lua_Calaos` (via `Lunar<Lua_Calaos>`) exposant les fonctions Calaos au script Lua.

### Fonctions disponibles dans les scripts

```lua
-- Lire la valeur d'un IO
calaos:get_io("id-abc")           -- retourne la valeur (bool/number/string)
calaos:get_io_bool("id-abc")
calaos:get_io_number("id-abc")
calaos:get_io_string("id-abc")

-- Écrire la valeur d'un IO
calaos:set_io("id-abc", true)
calaos:set_io("id-abc", 75.0)
calaos:set_io("id-abc", "play")

-- Lire/écrire un paramètre IO
calaos:get_param("id-abc", "name")
calaos:set_param("id-abc", "enabled", "true")

-- Logging
calaos:log("message de debug")

-- Heure courante
calaos:get_hour()         -- heure (0-23)
calaos:get_minute()       -- minutes
calaos:get_second()       -- secondes
calaos:get_day()          -- jour du mois
calaos:get_month()        -- mois (1-12)
calaos:get_year()         -- année
calaos:get_weekday()      -- jour de la semaine (0=dimanche)
```

### Exemple — ConditionScript

```lua
-- Vrai si la température > 25°C ET c'est l'été
local temp = calaos:get_io_number("id-temp-salon")
local month = calaos:get_month()
return temp > 25.0 and (month >= 6 and month <= 8)
```

### Exemple — ActionScript

```lua
-- Calculer et appliquer une valeur dérivée
local lum = calaos:get_io_number("id-capteur-lumiere")
local dim = math.min(100, lum / 10)
calaos:set_io("id-gradateur", dim)
```

---

## Lunar

**Fichier :** [src/bin/calaos_server/LuaScript/Lunar.h](../src/bin/calaos_server/LuaScript/Lunar.h)

Bibliothèque header-only pour binder des classes C++ dans Lua. Utilisée pour exposer `Lua_Calaos`.

---

## ScriptExec

**Fichier :** [src/bin/calaos_server/LuaScript/ScriptExec.h](../src/bin/calaos_server/LuaScript/ScriptExec.h)

Wrapper d'exécution de script avec gestion du contexte (état d'entrée, référence à la règle courante). Utilise `ExternProc` pour lancer les scripts Lua dans un sous-processus isolé `calaos_script` (fichier `ScriptExtern_main.cpp`), ce qui protège le serveur principal d'un crash ou d'une boucle infinie dans un script.

---

## Stockage des scripts

Les scripts sont stockés inline dans les fichiers de config XML (`rules.xml`) comme contenu texte des nœuds `ConditionScript` / `ActionScript` :

```xml
<condition type="ConditionScript">
  <![CDATA[
    local temp = calaos:get_io_number("id-temp")
    return temp > 20.0
  ]]>
</condition>
```
