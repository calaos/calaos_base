# Configuration & Persistence

## Vue d'ensemble

La configuration Calaos est persistée dans trois fichiers XML :
- `io.xml` — définition des IOs (pièces + entrées/sorties)
- `rules.xml` — règles d'automatisation (conditions + actions)
- `local_config.xml` — paramètres serveur (identifiants, SMTP, InfluxDB, options diverses) — voir
  [16_config_options.md](16_config_options.md) pour la liste complète et documentée de ses options

Un cache d'état SQLite sauvegarde les dernières valeurs des IOs pour les restaurer au redémarrage ;
les tokens de notification push (`push_tokens`) vivent eux aussi dans cette base SQLite (voir
[src/bin/calaos_server/HistLogger.cpp](../src/bin/calaos_server/HistLogger.cpp)), pas dans
`local_config.xml`.

---

## Config (classe principale)

**Fichier :** [src/bin/calaos_server/CalaosConfig.h](../src/bin/calaos_server/CalaosConfig.h)

Singleton. Charge et sauvegarde `io.xml` (IOs) et `rules.xml` (règles). **`local_config.xml` n'est
pas géré par `Config`** : il est lu et écrit par des fonctions libres du namespace `Utils`
(`Utils::get_config_option[s]`, `set_config_option[s]`, `del_config_option`, voir plus bas). `Config`
ne touche `local_config.xml` qu'indirectement, via `BackupFiles()` qui le copie (lecture seule) au
même titre que `io.xml` et `rules.xml`.

```cpp
Config &conf = Config::Instance();

// Chargement au démarrage
conf.LoadConfigIO();    // charge io.xml → remplit ListeRoom
conf.LoadConfigRule();  // charge rules.xml → remplit ListeRule

// Sauvegarde (après modification)
conf.SaveConfigIO();
conf.SaveConfigRule();

// Cache d'état (persistance des valeurs IO)
conf.SaveValueIO("id-abc", "true");
bool ok = conf.ReadValueIO("id-abc", value);

// Cache de paramètres
conf.SaveValueParams("id-abc", params);
bool ok = conf.ReadValueParams("id-abc", params);

// Backup
conf.BackupFiles();  // crée des fichiers .bak
```

---

## Chemins de fichiers

**Fichier :** [src/lib/Prefix.h](../src/lib/Prefix.h)

Classe `Prefix` qui résout les chemins selon l'environnement (système installé vs développement).

| Fichier | Chemin typique |
|---|---|
| `io.xml` | `/etc/calaos/io.xml` |
| `rules.xml` | `/etc/calaos/rules.xml` |
| `local_config.xml` | `/etc/calaos/local_config.xml` |
| Cache état | `/var/lib/calaos/states_cache.db` |
| Logs | `/var/log/calaos/` |

En développement (`~/.config/calaos/` ou variable d'env `CALAOS_HOME`).

---

## Format io.xml

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<calaos:home xmlns:calaos="http://www.calaos.fr">
  <calaos:room name="Salon" type="living" hits="3">
    <calaos:io type="WagoOutputLight"
               id="id-lum-salon"
               name="Lumière principale"
               host="192.168.1.10"
               var="0"
               enabled="true"
               gui_type="light" />
    
    <calaos:io type="WagoInputSwitch"
               id="id-sw-salon"
               name="Interrupteur salon"
               host="192.168.1.10"
               var="1"
               enabled="true" />
  </calaos:room>
  
  <calaos:room name="Extérieur" type="outdoor" hits="0">
    <calaos:io type="MqttInputTemp"
               id="id-temp-ext"
               name="Température extérieure"
               host="192.168.1.5"
               topic="capteurs/exterieur/temperature"
               path="$.value"
               enabled="true" />
  </calaos:room>
</calaos:home>
```

## Format local_config.xml

Paramètres serveur globaux (identifiants, SMTP, InfluxDB, etc.) — la liste complète et documentée
de chaque option (type, valeur par défaut, composant consommateur) est dans
[16_config_options.md](16_config_options.md), généré depuis le registre
[src/lib/ConfigOptions.cpp](../src/lib/ConfigOptions.cpp) :

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<calaos:config xmlns:calaos="http://www.calaos.fr">
  <calaos:option name="cn_user" value="user"/>
  <calaos:option name="cn_pass" value="pass"/>
  <calaos:option name="port_api" value="5454"/>
  <calaos:option name="smtp_server" value="smtp.example.com"/>
  <!-- Générés automatiquement au premier démarrage par McpServerManager -->
  <calaos:option name="mcp_token" value="<64 hex>"/>
  <calaos:option name="mcp_service_token" value="<64 hex>"/>
</calaos:config>
```

Lecture/écriture via les fonctions libres `Utils::get_config_option[s]` / `set_config_option[s]` /
`del_config_option` ([src/lib/Utils.cpp](../src/lib/Utils.cpp)). Les options `mcp_token` (Bearer
pour les clients MCP) et `mcp_service_token` (login de service du sidecar) sont auto-générées si
absentes — voir [15_mcp_server.md](15_mcp_server.md). Côté parseurs tiers (ex. `xml.etree` Python),
attention : les éléments sont namespacés (`{http://www.calaos.fr}option`).

### Un fichier partagé par plusieurs écrivains

`local_config.xml` n'est pas la propriété exclusive du serveur. Quatre processus le lisent et
l'écrivent :

- **`calaos_server`** — au démarrage (semis des valeurs par défaut, purge des clés obsolètes) et à
  chaque écriture déclenchée par l'API (`processConfig` dans
  [JsonApiHandlerHttp.cpp](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)) ;
- **`calaos_config`** — l'outil en ligne de commande (`get`/`set`/`del`/`purge`, voir
  [16_config_options.md](16_config_options.md)) ;
- **`calaos_mail`** — relancé par `NotifManager` à **chaque envoi de mail** de notification
  ([src/bin/calaos_server/NotifManager.cpp](../src/bin/calaos_server/NotifManager.cpp)) ;
- **Calaos Home** (l'écran tactile, `calaos_mobile` compilé en `CALAOS_DESKTOP`, hors de ce dépôt) —
  il lit et écrit ses propres clés (`show_cursor`, `dpms_enable`, `lang`, `calaos_server_host`…)
  directement dans le même fichier, au même format `<calaos:option>`.

Les trois premiers passent tous par `Utils`, qui sérialise les accès inter-processus avec un
`flock(LOCK_EX)` (`LOCK_SH` en lecture) posé sur un fichier compagnon `local_config.xml.lock`, et
écrit de façon atomique et durable : écriture dans un fichier temporaire du même répertoire,
`fchmod`/`fchown` aux mode/uid/gid d'origine, `fsync()` du fichier puis du répertoire, et enfin
`rename()` — ajouté par le commit `7d7808b9`. Cela évite qu'une écriture concurrente ne perde des
clés ou qu'une coupure de courant ne laisse un fichier vide ou aux mauvais droits (le fichier
contient des secrets : `smtp_password`, `influxdb_token`, `mcp_token`, `mcp_service_token`, `cn_pass`).

**Limite connue :** Calaos Home ne passe pas par `Utils` et ne prend donc **pas** ce verrou. Une
écriture de Calaos Home concurrente à une écriture de `calaos_server`/`calaos_config`/`calaos_mail`
n'est pas sérialisée par ce mécanisme ; le pire cas reste une clé écrasée par le dernier écrivain,
jamais un fichier corrompu (l'écriture atomique protège toujours contre ça). Le vrai correctif serait
côté `calaos_mobile`, hors de ce dépôt.

---

## Format rules.xml

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<calaos:rules xmlns:calaos="http://www.calaos.fr">
  <rule type="rule" name="Allumer lumière salon">
    
    <condition type="ConditionStd">
      <input id="id-sw-salon" operator="==" value="true"/>
    </condition>
    
    <condition type="ConditionStd">
      <input id="id-temp-ext" operator="<" value="20.0"/>
    </condition>
    
    <action type="ActionStd">
      <output id="id-lum-salon" value="true"/>
    </action>
    
    <action type="ActionPush">
      <message>Lumière allumée dans le salon</message>
    </action>
    
  </rule>
</calaos:rules>
```

---

## Cache d'état

Le cache d'état est une map `id → valeur` (string) sauvegardée dans un fichier JSON/SQLite. Il permet de restaurer les dernières valeurs connues des IOs au redémarrage du serveur, avant même que les IOs aient pu se re-synchroniser avec les périphériques physiques.

Version actuelle : `CONFIG_STATES_CACHE_VERSION = 1`

```cpp
// Structure interne du cache
unordered_map<string, string>  cache_states;   // id → valeur string
unordered_map<string, Params>  cache_params;   // id → paramètres
```

Le cache est sauvegardé avec un timer (`saveCacheTimer`) pour éviter les écritures trop fréquentes.

---

## Sauvegarde des IOs

`Config::SaveConfigIO()` sérialise l'état actuel de `ListeRoom` :

```
ListeRoom → Room::SaveToXml() → IOBase::SaveToXml()
  → écrit dans io.xml
```

---

## Sauvegarde des règles

`Config::SaveConfigRule()` sérialise `ListeRule` :

```
ListeRule → Rule::SaveToXml()
  → Condition::SaveToXml()
  → Action::SaveToXml()
  → écrit dans rules.xml
```

Les règles d'auto-scénario (`auto_sc_mark == true`) sont **exclues** de la sauvegarde manuelle (gérées par `AutoScenario`).
