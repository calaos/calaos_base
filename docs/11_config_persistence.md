# Configuration & Persistence

## Vue d'ensemble

La configuration Calaos est persistée dans trois fichiers XML :
- `io.xml` — définition des IOs (pièces + entrées/sorties)
- `rules.xml` — règles d'automatisation (conditions + actions)
- `local_config.xml` — paramètres serveur (identifiants, SMTP, InfluxDB, NTP, tokens push, options diverses)

Un cache d'état SQLite sauvegarde les dernières valeurs des IOs pour les restaurer au redémarrage.

---

## Config (classe principale)

**Fichier :** [src/bin/calaos_server/CalaosConfig.h](../src/bin/calaos_server/CalaosConfig.h)

Singleton. Orchestre le chargement et la sauvegarde.

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

Paramètres serveur globaux (credentials, SMTP, NTP, InfluxDB, tokens push, etc.) :

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<calaos:config xmlns:calaos="http://www.calaos.fr">
  <calaos:option name="calaos_user" value="user"/>
  <calaos:option name="calaos_password" value="pass"/>
  <calaos:option name="port_api" value="5454"/>
  <calaos:option name="smtp_server" value="smtp.example.com"/>
  <calaos:option name="mail_to" value="user@example.com"/>
  <calaos:option name="ntp_server" value="pool.ntp.org"/>
  <!-- Générés automatiquement au premier démarrage par McpServerManager -->
  <calaos:option name="mcp_token" value="<64 hex>"/>
  <calaos:option name="mcp_service_token" value="<64 hex>"/>
</calaos:config>
```

Lecture/écriture via `Utils::get_config_option` / `set_config_option`. Les
options `mcp_token` (Bearer pour les clients MCP) et `mcp_service_token`
(login de service du sidecar) sont auto-générées si absentes — voir
[15_mcp_server.md](15_mcp_server.md). Côté parseurs tiers (ex. `xml.etree`
Python), attention : les éléments sont namespacés (`{http://www.calaos.fr}option`).

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
