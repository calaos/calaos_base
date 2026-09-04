# Configuration & Persistence

> **Convention de traçabilité** (E4.0f, appliquée ici par E4.5a) : chaque affirmation
> vérifiable porte sa provenance — **(capturé, `tests/…`)** = octets produits par le code
> et figés, **(dérivé, `Fichier.cpp:L`)** = forme lue dans le source.

## Vue d'ensemble

La configuration Calaos est persistée dans trois fichiers XML :
- `io.xml` — définition des IOs (pièces + entrées/sorties)
- `rules.xml` — règles d'automatisation (conditions + actions)
- `local_config.xml` — paramètres serveur (identifiants, SMTP, InfluxDB, options diverses) — voir
  [16_config_options.md](16_config_options.md) pour la liste complète et documentée de ses options

À côté, deux fichiers d'**état** qui ne sont pas de la configuration et ne vivent pas au même
endroit (dérivé, [src/lib/ConfigStore.cpp:183-207](../src/lib/ConfigStore.cpp)) :

| Fichier | Format | Contenu | Écrit par |
|---|---|---|---|
| `iostates.cache` | **JSON** | dernières valeurs et paramètres des IOs | `Config` ([CalaosConfig.cpp:487-576](../src/bin/calaos_server/CalaosConfig.cpp)) |
| `events.db` | **SQLite** | journal d'événements **et** tokens de notification push (`push_tokens`) | `HistLogger` ([HistLogger.cpp:41, 107](../src/bin/calaos_server/HistLogger.cpp)) |

Les deux sont dans le **répertoire de cache** (`$HOME/.cache/calaos/` par défaut), pas dans le
répertoire de configuration. Ce sont **deux mécanismes distincts** : le cache d'état n'est pas
une base SQLite, et les `push_tokens` ne sont pas dans le cache d'état.

---

## Config (classe principale)

**Fichier :** [src/bin/calaos_server/CalaosConfig.h](../src/bin/calaos_server/CalaosConfig.h)

Singleton. Charge et sauvegarde `io.xml` (IOs) et `rules.xml` (règles). **`local_config.xml` n'est
pas géré par `Config`** : il est lu et écrit par des fonctions libres du namespace `Utils`
(`Utils::get_config_option[s]`, `set_config_option[s]`, `del_config_option`), qui vivent depuis le
split T2.2 dans [src/lib/ConfigStore.cpp](../src/lib/ConfigStore.cpp) (déclarées dans
[src/lib/ConfigStore.h:39-47](../src/lib/ConfigStore.h)) — voir plus bas. `Config` ne touche
`local_config.xml` qu'indirectement, via `BackupFiles()` qui le copie (lecture seule) au même
titre que `io.xml` et `rules.xml`.

```cpp
Config &conf = Config::Instance();

// Chargement au démarrage
conf.LoadConfigIO();    // charge io.xml → remplit ListeRoom
conf.LoadConfigRule();  // charge rules.xml → remplit ListeRule

// Sauvegarde (après modification)
conf.SaveConfigIO();
conf.SaveConfigRule();

// Cache d'état (persistance des valeurs IO). save = false diffère l'écriture
// disque jusqu'au prochain tick du timer de 60 s.
conf.SaveValueIO("id-abc", "true", /*save =*/ true);
bool ok = conf.ReadValueIO("id-abc", value);

// Cache de paramètres
conf.SaveValueParams("id-abc", params, /*save =*/ true);
bool ok = conf.ReadValueParams("id-abc", params);

// Flush explicite du cache (le destructeur le fait aussi)
conf.saveStateCache();

// Backup horodaté des trois fichiers de config
conf.BackupFiles();
```

`BackupFiles()` n'a **qu'un seul appelant de production** : le handler HTTP `config`, juste avant
d'écraser les fichiers de config poussés par un client
(dérivé, [JsonApiHandlerHttp.cpp:783](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

Le dossier est horodaté **à la seconde**, ce qui ne suffit pas à le rendre unique : jusqu'à T3.64
deux `config put` dans la même seconde le partageaient, et le second **écrasait** la copie du
premier — l'état d'avant le premier téléversement était perdu. `BackupFiles()` prend désormais le
**premier nom libre** de la suite `<date-heure>`, `<date-heure>-2`, `<date-heure>-3`… (borne
`MAX_BACKUPS_PER_SECOND = 1000`, au-delà la sauvegarde est refusée avec une erreur journalisée).
Le nom du cas courant — un seul téléversement — est donc inchangé, et le lecteur
(`findBackupsNewestFirst()`, plus bas) n'est pas concerné : il parcourt l'arborescence en récursif
et trie par date de modification, **sans jamais parser le nom du dossier**.

⚠️ **Rien ne purge `<config>/backups`** (voir [`refactoring/FINDINGS.md`](refactoring/FINDINGS.md),
`[F-BACKUP-1]`).

---

## Chemins de fichiers

**Fichier :** [src/lib/ConfigStore.cpp](../src/lib/ConfigStore.cpp) (namespace `Utils`).

> ⚠️ Ce n'est **pas** `Prefix` qui résout ces chemins : `src/lib/Prefix.h` ne connaît que les
> répertoires d'installation (`binDirectoryGet()`, `libDirectoryGet()`, `dataDirectoryGet()`).

### Configuration

`Utils::getConfigFile(name)` (dérivé,
[ConfigStore.cpp:86-137, 165-181](../src/lib/ConfigStore.cpp)) :

1. si la variable d'environnement **`CALAOS_CONFIG`** est posée (ou l'option `--config` du
   serveur, qui passe par `Utils::initConfigOptions()`), c'est elle qui gagne, sans recherche ;
2. sinon, le premier de ces répertoires qui contient déjà un `io.xml` :
   `$HOME/.config/calaos/`, puis `/etc/calaos`, puis `<prefix>/etc/calaos` ;
3. si aucun ne convient, `$HOME/.config/calaos/` est **créé** et utilisé.

Le chemin est mémorisé dans un statique immortel après le premier appel : `getConfigFile()` est
atteignable depuis la chaîne `atexit`, et un statique ordinaire y était lu **après** son propre
destructeur (T3.14).

### Cache

`Utils::getCacheFile(name)` (dérivé, [ConfigStore.cpp:183-207](../src/lib/ConfigStore.cpp)) rend
toujours `$HOME/.cache/calaos/<name>`, en créant le répertoire au besoin. L'option `--cache` du
serveur permet de le forcer.

| Fichier | Chemin |
|---|---|
| `io.xml` | `<config>/io.xml` |
| `rules.xml` | `<config>/rules.xml` |
| `local_config.xml` | `<config>/local_config.xml` (+ `local_config.xml.lock`) |
| Backups | `<config>/backups/<AAAA>/<MM>/<JJ-MM-AAAA_HH-MM-SS>/`, suffixé `-2`, `-3`… si le nom est déjà pris (T3.64) |
| Copies de configs corrompues | `<config>/backups/corrupt/<nom>.<AAAAMMJJ-HHMMSS>` |
| Cache d'état | `<cache>/iostates.cache` |
| Journal d'événements + tokens push | `<cache>/events.db` |
| Images des notifications push | `<cache>/push_pictures/` |

Les noms de fichiers eux-mêmes sont des constantes
(dérivé, [src/lib/Constants.h:31-38](../src/lib/Constants.h)).

---

## Format io.xml

Racine `<calaos:ioconfig>`, un seul enfant `<calaos:home>`, puis les pièces
(dérivé, [CalaosConfig.cpp:259-311, 320-330](../src/bin/calaos_server/CalaosConfig.cpp)).

Les IOs **ne sont pas des `<calaos:io>`** : le nom de balise porte la catégorie, et le lecteur
n'en accepte que sept — `calaos:input`, `calaos:output`, `calaos:internal`, `calaos:avr`,
`calaos:camera`, `calaos:audio`, `calaos:remote_ui` (dérivé,
[Room.cpp:159-185](../src/bin/calaos_server/Room.cpp)). À l'écriture, `IOBase::SaveToXml()`
n'émet que deux de ces sept, `calaos:input` ou `calaos:output` selon `isInput()`, les autres
classes surchargeant la fonction (dérivé,
[IOBase.cpp:171-183](../src/bin/calaos_server/IOBase.cpp)).

Tous les paramètres d'un IO sont des **attributs** de sa balise : `IOFactory::readParams()` verse
tout attribut rencontré dans le `Params` de l'IO, et `SaveToXml()` réémet tout le `Params`
(dérivé, [IO/IOFactory.cpp:30-36](../src/bin/calaos_server/IO/IOFactory.cpp)).

Forme minimale, **intégrale** — c'est exactement le document que `CalaosTest::minimalIoXml()`
écrit sur disque et que le serveur relit dans toute la suite de caractérisation (dérivé,
[tests/core/CalaosCoreFixture.cpp:43-49, 63-96, 127-136](../tests/core/CalaosCoreFixture.cpp)) :

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<calaos:ioconfig xmlns:calaos="http://www.calaos.fr">
<calaos:home>
  <calaos:room name="TestRoom" type="living" hits="0">
    <calaos:internal type="InternalBool" id="io_core_bool_in" name="Bool input" enabled="true" visible="true" />
    <calaos:internal type="InternalBool" id="io_core_bool_out" name="Bool output" enabled="true" visible="true" />
    <calaos:internal type="InternalInt" id="io_core_int" name="Int value" enabled="true" visible="true" />
    <calaos:internal type="InternalString" id="io_core_string" name="String value" enabled="true" visible="true" />
  </calaos:room>
</calaos:home>
</calaos:ioconfig>
```

Un IO de driver a la même forme, avec les attributs que son `ioDoc` déclare — ici une sortie
lumière Wago (dérivé, [IO/Wago/WODigital.cpp:27-44](../src/bin/calaos_server/IO/Wago/WODigital.cpp)
pour `host`/`port`/`var`/`wago_841`/`knx` et l'alias de type `WagoOutputLight`,
[IO/OutputLight.cpp:45-51](../src/bin/calaos_server/IO/OutputLight.cpp) pour `io_style`) :

```xml
<calaos:output type="WagoOutputLight" id="output_1" name="Lumière principale"
               host="192.168.1.10" port="502" var="0" wago_841="true"
               enabled="true" visible="true" io_style="light" />
```

L'attribut `hits` de la pièce est le seul champ lu en entier : absent, vide ou illisible, il vaut
`0` — reproduction délibérée du comportement de l'ancien lecteur TinyXML (dérivé,
[CalaosConfig.cpp:294-301](../src/bin/calaos_server/CalaosConfig.cpp)).

Si `io.xml` n'existe pas, le serveur en écrit un vide (`<calaos:home></calaos:home>`) et continue.

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

(dérivé, [ConfigStore.cpp:684-748, 797-801](../src/lib/ConfigStore.cpp) : racine
`calaos:config`, enfants `calaos:option` porteurs des attributs `name` et `value`.)

Lecture/écriture via les fonctions libres `Utils::get_config_option[s]` / `set_config_option[s]` /
`del_config_option` ([src/lib/ConfigStore.cpp](../src/lib/ConfigStore.cpp)). Les options
`mcp_token` (Bearer pour les clients MCP) et `mcp_service_token` (login de service du sidecar)
sont auto-générées si absentes — voir [15_mcp_server.md](15_mcp_server.md). Côté parseurs tiers
(ex. `xml.etree` Python), attention : les éléments sont namespacés
(`{http://www.calaos.fr}option`).

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
`rename()` — ajouté par le commit `7d7808b9` (dérivé,
[ConfigStore.cpp:457-582](../src/lib/ConfigStore.cpp)). Cela évite qu'une écriture concurrente ne
perde des clés ou qu'une coupure de courant ne laisse un fichier vide ou aux mauvais droits (le
fichier contient des secrets : `smtp_password`, `influxdb_token`, `mcp_token`,
`mcp_service_token`, `cn_pass`).

Le verrou est pris sur un fichier **séparé**, jamais sur `local_config.xml` lui-même : `flock()`
verrouille un inode, et l'écriture atomique remplace justement cet inode par `rename()`.

**Limite connue :** Calaos Home ne passe pas par `Utils` et ne prend donc **pas** ce verrou. Une
écriture de Calaos Home concurrente à une écriture de `calaos_server`/`calaos_config`/`calaos_mail`
n'est pas sérialisée par ce mécanisme ; le pire cas reste une clé écrasée par le dernier écrivain,
jamais un fichier corrompu (l'écriture atomique protège toujours contre ça). Le vrai correctif serait
côté `calaos_mobile`, hors de ce dépôt.

---

## Format rules.xml

Racine `<calaos:rules>`, enfants `<calaos:rule>` ; le lecteur exige les attributs `name` **et**
`type` et ignore tout autre nom de balise (dérivé,
[CalaosConfig.cpp:371-394](../src/bin/calaos_server/CalaosConfig.cpp)).

⚠️ **Tout est namespacé, et les noms d'attributs ne sont pas ceux qu'on devine** : l'opérateur
s'écrit `oper` (pas `operator`), la valeur `val` (pas `value`), et le `type` d'une condition ou
d'une action est le nom **de fabrique**, pas le nom de la classe C++ : les conditions sont
`standard` (ou l'attribut vide), `start`, `script`, `output` ; les actions sont `standard` (ou
vide), `mail`, `script`, `touchscreen`, `push` (dérivé,
[Rules/RulesFactory.cpp:65-135](../src/bin/calaos_server/Rules/RulesFactory.cpp),
[Rules/ConditionStd.cpp:521-538](../src/bin/calaos_server/Rules/ConditionStd.cpp),
[Rules/ActionStd.cpp:357-371](../src/bin/calaos_server/Rules/ActionStd.cpp)).

Forme d'une règle simple, **intégrale** — le document que `CalaosTest::minimalRulesXml()` écrit
et que le serveur relit (dérivé,
[tests/core/CalaosCoreFixture.cpp:98-124, 138-142](../tests/core/CalaosCoreFixture.cpp)) :

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<calaos:rules xmlns:calaos="http://www.calaos.fr">
  <calaos:rule name="TestRule" type="rule">
    <calaos:condition type="standard" trigger="true">
      <calaos:input id="io_core_bool_in" oper="==" val="true" />
    </calaos:condition>
    <calaos:action type="standard">
      <calaos:output id="io_core_bool_out" val="true" />
    </calaos:action>
  </calaos:rule>
</calaos:rules>
```

Une `ActionPush` s'écrit ainsi (dérivé,
[Rules/ActionPush.cpp:137-161](../src/bin/calaos_server/Rules/ActionPush.cpp)) — le message est
dans un **CDATA**, pas dans un élément `<message>` :

```xml
<calaos:action type="push">
  <calaos:push attachment=""><![CDATA[Lumière allumée dans le salon]]></calaos:push>
</calaos:action>
```

Les attributs de `<calaos:rule>` autres que `name` et `type` sont versés tels quels dans le
`Params` de la règle et réémis à la sauvegarde ; c'est par là que passe `auto_scenario`, qui
rattache une règle d'étape à son scénario (dérivé,
[Rule.cpp:310-376](../src/bin/calaos_server/Rule.cpp)).

Si `rules.xml` n'existe pas, le serveur en écrit un vide et continue. Un `rules.xml` vide mais
valide journalise « `<calaos:rules>` node not found » — le test porte sur le **premier enfant**,
pas sur le nœud lui-même ; c'est cosmétique et connu.

---

## Robustesse : configuration corrompue (T2.4, décision utilisateur)

Un `io.xml` ou un `rules.xml` illisible **ne tue plus le démon** — c'était un `exit(-1)`. Le
chemin, en entier (dérivé,
[CalaosConfig.cpp:55-198](../src/bin/calaos_server/CalaosConfig.cpp)) :

1. **Journalisation** de l'erreur de parse, avec la description pugixml et un **offset en
   octets** dans le fichier.
2. **Préservation** de l'octet près du fichier corrompu dans
   `<config>/backups/corrupt/<nom>.<AAAAMMJJ-HHMMSS>` — jamais écrasé silencieusement.
   Épinglé par `CorruptIoXmlWithoutBackupDoesNotKillTheProcess`, qui compare le contenu préservé
   au contenu corrompu original (capturé,
   [tests/core/CalaosConfigRobustness_test.cpp:111-136](../tests/core/CalaosConfigRobustness_test.cpp)).
3. **Restauration** : les backups portant ce nom de fichier sont parcourus **du plus récent au
   plus ancien** (tri par date de modification). Chaque candidat est **d'abord parsé en place** —
   un backup lui-même corrompu est sauté avec un avertissement, et surtout jamais recopié sur le
   fichier vivant. Le premier qui parse est restauré. Épinglé par
   `BackupWalkSkipsCorruptNewestAndRestoresOlderOne`.
4. Si **aucun** backup n'est exploitable, le serveur démarre avec une configuration **vide** pour
   ce fichier. Il démarre quand même.
5. **Une seule notification agrégée**, mail **+** push, envoyée **~30 s après le démarrage**
   (`CONFIG_ALERT_DELAY_SEC = 30.0`) : les chargements ont lieu avant que la boucle d'événements
   ne tourne, la notification est donc différée par un `Timer::singleShot`. Le message nomme le
   fichier, le chemin de la copie préservée, et le backup restauré — ou dit explicitement que la
   configuration est **vide**.

Le canal est **partagé** : depuis E4.2e, la même notification transporte aussi le rapport des
**règles désactivées** faute d'un IO manquant, et T3.18 y ajoute la mention des **scénarios**
concernés — un scénario désactivé ainsi ne repart pas tout seul et exige un `autoscenario
reenable` explicite (dérivé,
[CalaosConfig.cpp:398-448](../src/bin/calaos_server/CalaosConfig.cpp)). Tout est agrégé dans un
seul corps de message, envoyé une seule fois.

Limites connues, assumées : `backups/corrupt/` n'est pas borné (des boots corrompus répétés
s'y accumulent), le délai de 30 s est une heuristique et non un vrai signal « boucle démarrée »,
et **`local_config.xml` n'est pas couvert** par ce mécanisme (son parsing est dans `ConfigStore`).

---

## Fidélité XML (E4.4cd)

Le lecteur/écrivain est passé de TinyXML 1 (2.5.3, vendorisé, non maintenu) à **pugixml**. Le
portage est un renommage 1:1 ; tout ce qui ne l'est pas est isolé dans
[src/lib/XmlUtils.h](../src/lib/XmlUtils.h), avec la raison de chaque helper.

**Deux pertes de fidélité, présentes de longue date, disparaissent** :

- **CDATA (scripts Lua, messages de notification) : +18 caractères d'espacement parasites.**
  L'imprimeur TinyXML mettait le CDATA sur sa propre ligne, si bien qu'un corps de script de
  21 octets se relisait à 39. Non cumulatif, et Calaos relisait proprement — le préjudice était
  pour les consommateurs conformes au standard (calaos_installer et son `QDomDocument`, XSLT,
  outils tiers). Corrigé : le corps est restitué à l'octet près.
- **Perte de données sur `]]>`.** Un corps contenant cette séquence était écrit puis relu
  **tronqué** (170 octets écrits → 89 relus) et produisait un fichier XML invalide. pugixml
  scinde la séquence en deux sections CDATA consécutives valides, et `XmlUtils::text()` les
  recolle — aller-retour intégral.

Trois autres différences à connaître si l'on **diffe une config** avant/après migration :

- l'indentation reste de **quatre espaces** (`XmlUtils::CONFIG_INDENT`), le défaut pugixml étant
  une tabulation ; le reformat de la première sauvegarde se limite donc à l'ordre des attributs
  et à l'espacement des balises auto-fermantes ;
- un `local_config.xml` **édité à la main sans déclaration XML** gagne un `<?xml version="1.0"?>`
  à la première sauvegarde (pugixml en émet toujours une) ;
- un **BOM UTF-8** était préservé par TinyXML ; il est désormais supprimé.

`local_config.xml` est le seul document **chargé-modifié-sauvegardé** : il est donc parsé avec
`XmlUtils::CONFIG_PARSE_OPTIONS`, qui conserve déclaration, commentaires, PI et doctype — sans
quoi la première écriture mangerait silencieusement les commentaires d'un fichier édité à la
main. `io.xml` et `rules.xml`, eux, sont **reconstruits de zéro** à chaque sauvegarde.

Régression mineure assumée : les erreurs de parsing journalisent désormais un **offset en
octets** là où TinyXML donnait un **numéro de ligne**.

---

## Cache d'état

Le cache d'état permet de restaurer les dernières valeurs connues des IOs au redémarrage, avant
même que les IOs aient pu se re-synchroniser avec les périphériques physiques.

C'est un **fichier JSON**, `<cache>/iostates.cache`, à exactement deux objets de premier niveau
— `iostates` (id → valeur, **toujours une chaîne**) et `ioparams` (id → objet de paires
chaîne/chaîne). Indenté de 4 espaces (dérivé,
[CalaosConfig.cpp:487-576](../src/bin/calaos_server/CalaosConfig.cpp)).

Forme du document (**dérivée** du builder, pas capturée : ce fichier n'est figé par aucun
golden) pour l'état `t24_io = "t24_value"` et les paramètres `t24_params = {key: value}` de
`CacheRoundTripAndNoLeftoverTmp` — l'ordre des clés est alphabétique, `nlohmann::json` étant
un `std::map` (dérivé,
[CalaosConfig.cpp:540-558](../src/bin/calaos_server/CalaosConfig.cpp),
[tests/core/CalaosConfigRobustness_test.cpp:300-320](../tests/core/CalaosConfigRobustness_test.cpp)) :

```json
{
    "ioparams": {
        "t24_params": {
            "key": "value"
        }
    },
    "iostates": {
        "t24_io": "t24_value"
    }
}
```

```cpp
// Structure interne du cache
unordered_map<string, string>  cache_states;   // id → valeur string
unordered_map<string, Params>  cache_params;   // id → paramètres
```

Il n'y a **pas** de numéro de version dans ce fichier.

Écriture : `dump(4, ' ', false, Json::error_handler_t::replace)` dans un `.tmp` du même
répertoire, puis `rename()` — un `.tmp` orphelin est supprimé si l'écriture échoue. Le
`error_handler_t::replace` est là pour qu'une valeur non-UTF-8 remontée par du matériel ne fasse
pas échouer le `dump()` et perdre tout le cache (épinglé par `NonUtf8StateValueDoesNotLoseTheCache`).

Lecture : toute la désérialisation est dans un `try`. Un cache illisible **ou** valide en JSON
mais de forme inattendue (valeurs d'état non-string, par exemple) est journalisé et remplacé par
un cache **vide** — jamais une sortie du démon (épinglé par `UnparsableCacheFallsBackToEmptyCache`
et `WrongShapeCacheDoesNotAbortAndEndsUpEmpty`).

Cadence : un `Timer` de **60 s** écrit le cache en tâche de fond, ce qui évite une écriture par
changement de valeur ; `SaveValueIO`/`SaveValueParams` acceptent `save = false` pour se contenter
de la mise à jour mémoire. Le **destructeur** de `Config` détruit le timer puis force un dernier
`saveStateCache()` — sans quoi jusqu'à 60 s d'états seraient perdus à l'arrêt (épinglé par
`DestructorFlushesPendingStates`).

---

## Sauvegarde des IOs

`Config::SaveConfigIO()` sérialise l'état actuel de `ListeRoom` :

```
ListeRoom → Room::SaveToXml() → IOBase::SaveToXml()
  → document pugixml complet
  → écrit dans io.xml_tmp, puis rename() vers io.xml
```

L'écriture passe **toujours** par un fichier temporaire suivi d'un `rename()` : `rename()` seul
est atomique, alors qu'un `unlink()` préalable ouvrirait une fenêtre pendant laquelle aucun
fichier de config n'existe. Si l'écriture du temporaire échoue, il est supprimé et l'ancien
fichier reste intact (dérivé,
[CalaosConfig.cpp:313-348](../src/bin/calaos_server/CalaosConfig.cpp), épinglé par
`SaveLeavesNoTemporaryFileBehind` et `FailedSaveKeepsThePreviousConfigIntact`).

---

## Sauvegarde des règles

`Config::SaveConfigRule()` sérialise `ListeRule`, avec exactement le même schéma
temporaire + `rename()` :

```
ListeRule → Rule::SaveToXml()
  → Condition::SaveToXml()
  → Action::SaveToXml()
  → écrit dans rules.xml_tmp, puis rename() vers rules.xml
```

⚠️ **Aucune règle n'est exclue de la sauvegarde**, pas même les règles d'auto-scénario : la
boucle parcourt `ListeRule` de bout en bout (dérivé,
[CalaosConfig.cpp:463-467](../src/bin/calaos_server/CalaosConfig.cpp)), et `auto_sc_mark` n'est
consulté nulle part sur ce chemin. Les règles d'étape d'un scénario sont donc bien écrites dans
`rules.xml` — elles portent l'attribut `auto_scenario` — et sont relues au démarrage, puis
adoptées par `AutoScenario::checkScenarioRules()`. Celles qui portent l'attribut sans avoir été
adoptées par un scénario sont supprimées par `ListeRoom::checkAutoScenario()`, qui resauvegarde
ensuite les deux fichiers (dérivé,
[ListeRoom.cpp:309-342](../src/bin/calaos_server/ListeRoom.cpp)).

⚠️ **Une règle désactivée est sauvegardée intacte.** Depuis E4.2e/T3.18 (décision utilisateur),
une règle qui référence un IO introuvable n'est plus amputée : elle est conservée avec toutes ses
conditions et actions, **y compris l'id mort**, et seulement exclue de l'exécution. La
sauvegarde ne perd donc rien — c'est exactement ce que le test de contrat
`SavingRulesAfterAnIoDeletionIsClean` a dû **inverser** : l'id mort devait auparavant disparaître
de `rules.xml`, il doit désormais y survivre. Voir [03_rules_engine.md](03_rules_engine.md).
