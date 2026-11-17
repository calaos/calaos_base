# Templates de devices

**Fichiers :** [src/bin/calaos_server/IO/DeviceTemplates.cpp](../src/bin/calaos_server/IO/DeviceTemplates.cpp) —
spec : [CALAOS_DEVICES_FEAT.md](../CALAOS_DEVICES_FEAT.md) §3, §10 et §11.

Un template décrit comment créer un device complet (spec §2) pour une intégration connue : les
champs à demander à l'utilisateur, les IOs possédés à créer, les liens et les params. L'assistant
de calaos_installer l'applique sans rien savoir de la famille du device : tout ce qui est propre
au climate est dans les données du template, et ce que le device accepte est dans `io_doc.json`
(rôles de lien, params, commande).

## Génération

```
calaos_server --gentemplates <chemin>   # → <chemin>/device_templates.json
```

L'option se traite comme `--gendoc` : avant tout chargement de configuration, puis sortie du
process (code 1 si le fichier n'a pas pu être écrit, le répertoire est créé s'il manque). Les
libellés et descriptions sont traduits dans la locale du process (le français demande les `.mo`
installés : `make install`). `scripts/create_iodoc [dossier]` appelle les deux options, en anglais
puis en français, avec `--config` et `--cache` dans un dossier temporaire (le `HOME` peut être en
lecture seule), et range `io_doc.json`, `io_doc.md` et `device_templates.json` dans `build_doc/en`
et `build_doc/fr`. Le JSON est écrit comme `io_doc.json` : indenté de 4 espaces, ASCII pur
(`°C` devient `\u00b0C`).

Le job « Build & test » de la CI exécute ce script après `make install` et publie
le résultat dans l'artefact `io-doc` (90 jours, un dossier `en` et un dossier `fr`), hors pull request (une PR de fork porte le nom de branche du fork). Un run mensuel sur master (`schedule`, ou `workflow_dispatch`) le republie. Le job de CI de
calaos_installer le télécharge et compare ses copies (`data/doc/{en,fr}`) au JSON parsé. Les
paramètres, conditions et actions de `io_doc.json`/`io_doc.md` sont triés par nom : l'ordre de la
`unordered_map` dépendrait de la bibliothèque standard.

## Format (version 1)

```json
{ "format": 1, "templates": [ { "id": "thermostat_heat", ... }, ... ] }
```

`format` change quand un client écrit pour une version ne peut plus lire la suivante. Les
templates sont dans l'ordre où l'assistant les propose.

### Un template

| Clé | Contenu |
|---|---|
| `id` | identifiant stable du template |
| `family`, `impl` | `type` et `impl` de l'élément `<calaos:device>` ; `impl` est la clé du device sous `devices` dans `io_doc.json` |
| `name`, `description` | textes traduits pour l'utilisateur |
| `attributes` | autres attributs de `<calaos:device>` (`name`, `visible`), valeurs à substituer |
| `fields` | les champs demandés, dans l'ordre d'affichage |
| `ios` | les IOs possédés |
| `links` | les `<calaos:link>`, dans l'ordre du fichier |
| `params` | les `<calaos:param>`, dans l'ordre du fichier |
| `notes` | optionnel : textes à montrer après la création (markdown, substitués), chacun avec un `when` possible |

### Champs

Chaque champ porte `name`, `type`, `label`, `description` et `required`.

| `type` | Valeur | Clés en plus |
|---|---|---|
| `string` | texte libre | `default` ; `secret: true` pour une saisie masquée (mot de passe) |
| `number` | nombre | `min`, `max`, `step`, `unit`, `default` |
| `bool` | case à cocher | `default` |
| `enum` | une valeur parmi `values` | `values` : `[{value, label, params}]`, `default` |
| `multi` | plusieurs valeurs parmi `values` ; `required` : une au moins | `values` : `[{value, label}]`, `default` (liste de `value`) |
| `room` | la pièce où le device est créé (un seul par template) | — |
| `io` | un IO existant de la maison, cible d'un lien | `io_filter` : `{value: number\|bool\|string, writable}` |

L'assistant affiche les `label` (traduits), jamais les `value`. Le champ `modes` des templates
ESPHome est obligatoire : décoché en entier, le param ne serait pas écrit et le device offrirait
tous les modes.

Pour le thermostat, `modes` restreint les modes que ses sorties permettent : `off` reste
offert, `auto` suppose `heat` et `cool`, et une vanne `output` sans `modes` n'offre que `heat`.
Ces règles sont publiées par `list_rule` sur le param (docs/01) ; les clients ne les recopient pas.

`io_filter.value` est le type de valeur de l'IO (le `DATA_TYPE` du serveur : `TINT` pour
`number`) ; `writable: true` demande un IO que le device peut écrire (sortie ou interne). Les
`params` d'une option d'`enum` remplacent les params du template de même nom : c'est ainsi que le
type d'émetteur règle l'hystérésis et les verrous. Toutes les options d'un champ fixent les mêmes
params, et les valeurs écrites dans `params` sont celles de l'option par défaut : revenir à une
option rend toutes ses valeurs.

### IOs possédés, liens, params

```json
"ios": [ { "key": "target_temp", "element": "internal", "type": "InternalInt",
           "params": { "name": "{name} setpoint", "save": "true", "value": "20", ... },
           "when": "..." } ],
"links": [ { "role": "target_temp", "io": { "owned": "target_temp" } },
           { "role": "current_temp", "io": { "field": "current_temp" } },
           { "role": "mode", "io": { "owned": "mode" }, "command_io": { "owned": "mode_cmd" } } ],
"params": [ { "name": "hysteresis", "value": "0.3", "when": "..." } ]
```

- Un IO possédé devient `<calaos:{element} id type …params/>` dans le `<calaos:device>`. Son id
  est `<id du device>_<key>` (spec §3.2). Si cet id est pris (un IO de la maison, un id réservé
  par un device inerte, ou un id déjà attribué par l'assistant), l'id est `<id du device>_<key>_N`
  avec N le plus petit entier à partir de 0 qui donne un id libre : la règle de
  `Device::createOwnedLink()` (`get_new_id(id + "_")`).
- Une référence `{"owned": key}` désigne l'IO possédé créé pour cette clé, `{"field": nom}` l'IO
  choisi dans un champ `io`.
- `role` est un rôle de `link_roles` du device dans `io_doc.json`.

### Règles d'application

1. **Substitution.** Dans toute valeur, `{nom}` est remplacé par la valeur du champ `nom`, et
   `{id}` par l'id du device. Une valeur `multi` s'écrit séparée par des virgules dans l'ordre
   de `values`, un `bool` en `true`/`false`, un nombre sans zéro inutile (`16`, `0.5`). Une accolade
   qui ne nomme ni un champ ni `id` reste telle quelle.
2. **`when`.** Un IO, un lien, un param ou une note n'est créé que si le champ nommé est
   « vrai » : `bool` coché, texte, `multi` ou `io` non vides. Un champ `number`, `enum` ou `room`
   a toujours une valeur et vaut donc toujours vrai : `when` ne sert pas à tester une valeur
   précise (passer par les `params` d'une option d'`enum`). `!nom` inverse la condition.
3. **Valeur vide.** Un param d'IO ou de device dont la valeur substituée est vide n'est pas écrit
   (un `mqtt_user` laissé vide n'écrit pas `user=""`).
4. **Lien sans cible.** Un lien vers un champ `io` vide ou vers un IO possédé non créé n'est pas
   écrit.
5. **Options.** Les `params` de l'option choisie d'un champ `enum` remplacent la valeur des params
   de même nom avant la substitution.

`tests/core/DeviceTemplates_test.cpp` (`instantiate()`) est l'implémentation de référence de
ces règles.

### Exemple

Le template `thermostat_heat`, abrégé, avec `name` = `Chauffage salon`, `room` = `Salon`,
`current_temp` = `salon_temp`, `output_heat` = `salon_relais`, `emitter` = `electric` et l'id
`salon_chauffage` :

```json
{ "id": "thermostat_heat", "family": "climate", "impl": "thermostat",
  "attributes": { "name": "{name}", "visible": "true" },
  "fields": [ { "name": "name", "type": "string", "required": true, ... },
              { "name": "room", "type": "room", ... },
              { "name": "current_temp", "type": "io", "io_filter": { "value": "number", "writable": false }, ... },
              { "name": "output_heat", "type": "io", "io_filter": { "value": "bool", "writable": true }, ... },
              { "name": "emitter", "type": "enum", "default": "electric",
                "values": [ { "value": "electric", "label": "Electric heaters",
                              "params": { "hysteresis": "0.3", "min_on_time": "120", "min_off_time": "120" } },
                            ... ] } ],
  "ios": [ { "key": "target_temp", "element": "internal", "type": "InternalInt",
             "params": { "name": "{name} setpoint", "visible": "false", "save": "true", "value": "20",
                         "value_min": "7", "value_max": "30", "step": "0.5", "unit": "°C" } },
           { "key": "action", "element": "internal", "type": "InternalString",
             "params": { "name": "{name} action", "visible": "false" } } ],
  "links": [ { "role": "current_temp", "io": { "field": "current_temp" } },
             { "role": "target_temp", "io": { "owned": "target_temp" } },
             { "role": "action", "io": { "owned": "action" } },
             { "role": "output_heat", "io": { "field": "output_heat" } } ],
  "params": [ { "name": "emitter", "value": "{emitter}" }, { "name": "hysteresis", "value": "0.3" },
              { "name": "min_on_time", "value": "120" }, { "name": "min_off_time", "value": "120" },
              { "name": "sensor_timeout", "value": "9000" }, { "name": "valid_min", "value": "-30" },
              { "name": "valid_max", "value": "60" }, { "name": "put_back_delay", "value": "1" },
              { "name": "put_back_delay_max", "value": "60" } ] }
```

donne, dans la pièce `Salon` :

```xml
<calaos:device id="salon_chauffage" type="climate" impl="thermostat" name="Chauffage salon" visible="true">
  <calaos:internal id="salon_chauffage_target_temp" type="InternalInt" name="Chauffage salon setpoint"
                   save="true" step="0.5" unit="°C" value="20" value_max="30" value_min="7" visible="false"/>
  <calaos:internal id="salon_chauffage_action" type="InternalString" name="Chauffage salon action" visible="false"/>
  <calaos:link name="current_temp" io="salon_temp"/>
  <calaos:link name="target_temp" io="salon_chauffage_target_temp"/>
  <calaos:link name="action" io="salon_chauffage_action"/>
  <calaos:link name="output_heat" io="salon_relais"/>
  <calaos:param name="emitter" value="electric"/>
  <calaos:param name="hysteresis" value="0.3"/>
  <calaos:param name="min_on_time" value="120"/>
  <calaos:param name="min_off_time" value="120"/>
  <calaos:param name="sensor_timeout" value="9000"/>
  <calaos:param name="valid_min" value="-30"/>
  <calaos:param name="valid_max" value="60"/>
  <calaos:param name="put_back_delay" value="1"/>
  <calaos:param name="put_back_delay_max" value="60"/>
</calaos:device>
```

## Les six templates

| id | impl | Demandé | IOs possédés | IO externes |
|---|---|---|---|---|
| `esphome_climate` | proxy | préfixe de topic, object id, broker, modes, deux consignes ?, double consigne émulée ?, bornes de consigne, action ?, modes de ventilation et d'oscillation | mode, current_temp, target_temp ou low/high, action, fan_mode, swing_mode (Mqtt), remote_temp | sonde de la pièce (optionnelle) |
| `esphome_climate_ir` | proxy | préfixe, object id, broker, modes, sonde ESPHome ?, bornes, ventilation, oscillation | mode, current_temp, target_temp, fan_mode, swing_mode (Mqtt) | — |
| `thermostat_heat` | thermostat | émetteur | target_temp (`InternalInt`), action (`InternalString`) | sonde, sortie chaud |
| `thermostat_heat_cool` | thermostat | émetteur | target_temp, low, high, action | sonde, sorties chaud et froid |
| `thermostat_floor_heat_cool` | thermostat | humidité maximale | target_temp, low, high, action | sonde, sonde d'humidité, sorties chaud et froid |
| `thermostat_floor_zone` | thermostat | modes (off, heat, cool), humidité maximale | target_temp, action | sonde, vanne de zone (`output`), sonde d'humidité (optionnelle) |

- **Topics ESPHome.** Un climate ESPHome publie `<topic_prefix>/climate/<object_id>/<x>/state`
  et écoute `.../<x>/command`. Mode, consignes, ventilation et oscillation sont un seul
  `MqttOutput*` par rôle (lu sur `state`, écrit sur `command`, `data="__##VALUE##__"`) ; la
  température et l'action sont des `MqttInput*`. Les valeurs d'ESPHome sont en minuscules et
  sont celles de Calaos (`fan_only`, `heating`).
- **Statut du nœud.** Chaque IO possédé porte `connected_status_topic="<topic_prefix>/status"` et
  `connected_status_expr="value == 'online'"` : ESPHome y publie `online`, et `offline` par son
  message de dernière volonté. Sans l'expression, seuls `true`, `1` et `yes` valent « connecté ».
  Un nœud hors ligne passe ses IOs à `connected=false`, que voient l'état affichable (CLIM-6) et
  la sûreté (une source de `remote_temp` déconnectée n'est plus relayée).
- **`auto_payload` vaut `heat_cool`**, écrit dans le template. ESPHome distingue `heat_cool`
  (tenir une plage) de `auto` (programme de l'appareil) ; le mode `auto` de Calaos est la plage.
  `climate_ir` n'offre que `heat_cool`. Pour un composant qui rapporte `auto`, changer le param
  après création.
- **Zone de plancher réversible** (décision 22) : une vanne par zone sert au chaud et au froid,
  le mode donne le sens. Pas de plage : `auto` demanderait une plage par zone, le mode global
  de la maison se règle par une variable et des règles. Les zones sans froid gardent
  `off, heat`. La pompe à chaleur et ses contacts chaud/froid sont pilotés par des règles sur
  les vannes. Sans sonde d'humidité, le froid n'a pas de coupure au point de rosée : c'est à
  l'installation (température d'eau) de garder le plancher au-dessus.
- **Sorties des thermostats** : choisies parmi les IOs existants (relais Wago, KNX, Mqtt…), pas
  créées : le template ne connaît pas le matériel. Une sortie appartient à un seul thermostat
  (décision 17).

## Valeurs par défaut (spec §14)

| Template / émetteur | hysteresis | min_on_time | min_off_time | min_range | sensor_timeout | humidity_max | put_back_delay / max |
|---|---|---|---|---|---|---|---|
| `thermostat_heat`, radiateurs électriques (`electric`) | 0.3 | 120 | 120 | — | 9000 | — | 1 / 60 |
| `thermostat_heat`, radiateurs à eau (`water_radiator`) | 0.5 | 300 | 300 | — | 9000 | — | 1 / 60 |
| `thermostat_heat`, plancher chauffant (`floor`) | 0.5 | 900 | 900 | — | 9000 | — | 1 / 60 |
| `thermostat_heat_cool`, PAC réversible sur contacts (`heat_pump`) | 0.5 | 600 | 300 | 2 | 9000 | — | 1 / 60 |
| `thermostat_heat_cool`, ventilo-convecteurs (`fan_coil`) | 0.5 | 120 | 120 | 1 | 9000 | — | 1 / 60 |
| `thermostat_floor_heat_cool` | 0.5 | 900 | 900 | 2 | 9000 | 60 (champ) | 1 / 60 |
| `thermostat_floor_zone` | 0.5 | 900 | 900 | — | 9000 | 60 (champ, avec la sonde d'humidité) | 1 / 60 |
| `esphome_climate` avec sonde relayée | — | — | — | — | 9000 | — | — (`remote_temp_keepalive` 120) |
| `esphome_climate`, double consigne émulée | 0.5 | 300 | 300 | 2 | — | — | — (`changeover_delay` 600) |

Pourquoi :

- **Radiateurs électriques** : sans inertie, ils dépassent peu la consigne, une bande étroite
  (0,3 °C) donne le confort. 2 minutes de verrou suffisent à ignorer le bruit d'une sonde sans
  user le relais (au plus 15 cycles par heure).
- **Radiateurs à eau** : une tête thermique met 3 à 5 minutes à s'ouvrir et la chaudière ne doit
  pas faire de cycles courts : 5 minutes.
- **Plancher** : des heures d'inertie. 15 minutes de verrou (exemple de la spec §3.1) et
  l'hystérésis par défaut : élargir la bande ne ferait que creuser le creux, le dépassement vient
  de l'inertie, pas du seuil.
- **PAC sur contacts** : un compresseur demande 3 à 5 minutes d'arrêt avant de repartir et 5 à
  10 minutes de marche pour le retour d'huile : 600 s de marche et 300 s d'arrêt, qui s'appliquent
  aussi au passage chaud/froid. `min_range` de 2 °C évite les bascules en `auto`.
- **Ventilo-convecteurs** : peu d'inertie, la production a ses propres protections : 2 minutes,
  plage d'1 °C.
- **`sensor_timeout`** dépend de la sonde, pas de l'émetteur : 9000 s (2 h 30) survit à un
  rapport horaire Zigbee perdu. Une sonde Wago ou 1-Wire lue toutes les secondes peut descendre
  à 600 s après création.
- **`valid_min` -30 / `valid_max` 60** (CLIM-9), sur les trois templates `thermostat_*` : large
  exprès. Une cave ou un garage hors gel à -2 °C doit continuer de chauffer, donc aucune borne
  proche de 0 °C. -30 à 60 °C écarte quand même les valeurs de défaut des sondes : 85 °C à la
  mise sous tension d'une DS18B20, -127 °C d'une sonde 1-Wire débranchée, une valeur énorme
  d'une Wago ouverte ou en court-circuit. L'installateur resserre pour une pièce habitée.
  Les templates `esphome_*` n'en ont pas : le proxy relaie la sonde de l'appareil et ne
  régule pas, `valid_min`/`valid_max` n'existent que sur `thermostat`.
  Les bornes n'ont pas d'hystérésis : une sonde qui oscille sur une borne coupe et rétablit les
  sorties au rythme de `min_off_time`, pas plus vite. Une valeur qui n'est pas un nombre (NaN,
  infini) est toujours hors bornes, même sans `valid_min` ni `valid_max`.
- **`humidity_max` 60 %** (le défaut du serveur reste 65 %) : à 25 °C et 60 % d'humidité, le
  point de rosée est vers 16,7 °C. Un plancher rafraîchi par de l'eau à 16 °C a sa surface au-dessus,
  avec une marge que 65 % (rosée vers 18 °C) n'aurait plus. C'est un champ du template :
  l'installateur l'abaisse si l'eau est plus froide.
- **`put_back_delay` 1 s, max 60 s** : défauts du serveur. La remise d'une sortie passe par ses
  verrous, qui protègent déjà le matériel.
- **Double consigne émulée** (`emulate_range`, décision 21) : la clim garde sa propre protection
  de compresseur ; 300 s de marche et d'arrêt évitent seulement que Calaos la fasse cycler, et
  600 s entre la fin du chaud et le début du froid (ou l'inverse) laissent la pression s'équilibrer.
  L'unité s'arrête dès que la pièce revient dans la bande (décision 21) : c'est `min_off_time` qui
  borne la fréquence des cycles, au plus un démarrage toutes les `min_on_time + min_off_time`.
  Un mode changé à la télécommande dans les `readback_hold` secondes (10) qui suivent une commande
  de Calaos, et qui est le mode d'avant cette commande, n'est vu qu'au premier événement après
  cette fenêtre (sonde, consigne, ou la vérification périodique) : au plus environ 70 s.
  Le template ne crée pas les consignes basse et haute : le proxy les crée au chargement
  (`InternalInt` sauvegardés à 19 et 25, bornés comme la consigne de l'unité), un lien ne
  pouvant avoir qu'une cible. Avec « Deux consignes » aussi coché, `target_temp` n'est pas créé et
  le proxy refuse l'émulation au chargement. Le format n'exprime pas d'exclusion entre deux champs (`when` ne porte que sur
  les IOs, liens, params et notes) : l'assistant de l'installer doit empêcher ce cumul (ouvert).
- **Proxy** : `remote_temp_keepalive` 120 s, défaut du serveur ; un appareil revient à sa propre
  sonde quand les mises à jour cessent (spec §7). `sensor_timeout` 9000 s arrête le relais d'une
  sonde muette.

## Température distante sur ESPHome

ESPHome ne prend pas de température distante par MQTT d'origine. Avec le champ « sonde de la
pièce », Calaos publie la température sur `<topic_prefix>/calaos/remote_temp` à chaque changement
et toutes les `remote_temp_keepalive` secondes (`MqttOutputAnalog`, relu sur le même topic :
`MqttCtrl` refuse un IO sans `topic_sub`). Le YAML du nœud doit l'écouter ; le template porte ce
texte dans ses `notes`.

Climatiseur Mitsubishi sur CN105 (composant MitsubishiCN105ESPHome) : la température relayée
devient un capteur du nœud et la source de température distante du climate ;
`remote_temperature_timeout` le ramène sur sa propre sonde quand Calaos se tait (le régler
au-dessus du keep-alive) :

```yaml
sensor:
  - platform: mqtt_subscribe
    id: calaos_temp
    topic: salon-clim/calaos/remote_temp
climate:
  - platform: cn105
    remote_temperature_source:
      sensor_id: calaos_temp
    remote_temperature_timeout: 10min
```

Climate ESPHome qui régule sur sa propre sonde (plateforme `thermostat`) : la température relayée
devient cette sonde ; le filtre `timeout` la passe à NaN si Calaos se tait, et le thermostat
ESPHome s'arrête :

```yaml
sensor:
  - platform: mqtt_subscribe
    id: calaos_temp
    topic: salon-clim/calaos/remote_temp
    filters:
      - timeout: 5min
climate:
  - platform: thermostat
    sensor: calaos_temp
    # ...
```

Un climate infrarouge (`climate_ir`) ne prend pas de température distante (spec §7).

## Programme horaire des climates

Contrat de CP-CONTRACT (climate-program). Le code (CP-BASE-LEVELS, CP-BASE-PROXY,
CP-BASE-STATE) et les interfaces se codent contre ce texte.

> **Contrat cible : rien de cette section n'est encore sur master.** Il est livré par
> CP-BASE-LEVELS (niveaux, IOs, application, InternalThermostat), CP-BASE-PROXY
> (ProxyClimate) et CP-BASE-STATE (`state.program`). D'ici là, les climates n'ont ni ces
> params ni ces IOs.

### Modèle

Un climate (`InternalThermostat`, vanne de zone comprise, et `ProxyClimate`) peut porter un
programme horaire. Le programme **ne génère aucune règle** : le device lui-même écrit sa
consigne cible quand l'état d'une plage change. La logique vit dans `ClimateBase`, la
sous-classe fournit seulement l'écriture de la consigne (voir « Application »).

- Deux **jeux** de niveaux indépendants : `heat` (chaud) et `cool` (froid), de 0 à 6 niveaux
  chacun. Pas de saison globale : le mode du climate choisit le jeu.
- Un **niveau** `n` (1 à 6) d'un jeu `s` est formé de :
  - une plage `InPlageHoraire` possédée par le device ;
  - une consigne, `InternalInt` possédé et sauvegardé ;
  - une priorité (voir ci-dessous).
- Chaque jeu a une **consigne par défaut**, appliquée quand aucun niveau du jeu n'est actif. Le
  jeu `cool` a en plus un **arrêt par défaut** : la consigne par défaut vaut « aucune ».
- Un seul bouton `program_enabled` par climate.
- **Niveaux actifs.** Un niveau est actif quand sa plage est active (`InPlageHoraire`, qui gère
  créneaux multiples par jour, mois, bornes solaires et passage de minuit). Si plusieurs sont
  actifs, le plus prioritaire l'emporte ; si aucun, la consigne par défaut du jeu s'applique.
- **Priorité.** Le numéro du niveau : le niveau 1 l'emporte sur le 2, et ainsi de suite. Elle
  n'est pas réglable.

### IOs possédés

Chaque IO est créé par `Device::createOwnedLink()`, donc son id est `<id du device>_<rôle>`
(avec le suffixe `_N` de « IOs possédés, liens, params » si l'id est déjà pris, ce que le
serveur et l'installer ne doivent pas supposer : l'id réel se lit dans `device.ios` de l'état).
Chaque IO est lié sous un rôle de même nom, car un device n'est notifié que par ses IOs liés.
Dans la table, `<c>` est l'id du climate, `n` va de 1 au nombre de niveaux du jeu.

| Id | Rôle | Type | Sauvegardé | Créé si | Rôle fonctionnel |
|---|---|---|---|---|---|
| `<c>_heat_level<n>` | `heat_level<n>` | `InPlageHoraire` | oui (en tant qu'IO possédé, dans `io.xml`) | `n` ≤ `heat_levels` | plage du niveau `n` du jeu chaud |
| `<c>_heat_level<n>_setpoint` | `heat_level<n>_setpoint` | `InternalInt` | `save="true"` | idem | consigne du niveau, °C |
| `<c>_heat_default` | `heat_default` | `InternalInt` | `save="true"` | `heat_levels` ≥ 1 | consigne chaud par défaut, °C |
| `<c>_cool_level<n>` | `cool_level<n>` | `InPlageHoraire` | oui | `n` ≤ `cool_levels` | plage du niveau `n` du jeu froid |
| `<c>_cool_level<n>_setpoint` | `cool_level<n>_setpoint` | `InternalInt` | `save="true"` | idem | consigne du niveau, °C |
| `<c>_cool_default` | `cool_default` | `InternalInt` | `save="true"` | `cool_levels` ≥ 1 | consigne froid par défaut, °C |
| `<c>_cool_default_off` | `cool_default_off` | `InternalBool` | `save="true"` | `cool_levels` ≥ 1 | `true` : le défaut du froid est « arrêt » ; `cool_default` est alors ignorée |
| `<c>_program_enabled` | `program_enabled` | `InternalBool` | `save="true"` | `heat_levels` + `cool_levels` ≥ 1 | programme activé |

- **Consignes.** Les `InternalInt` de consigne portent `value_min`, `value_max`, `step` et
  `unit` de la consigne cible du climate, soit les bornes de `capabilities.target_temp` à la
  création (7, 30, 0.5, `°C` pour un thermostat interne ; `temp_min` et `temp_max` pour un
  proxy ; les bornes de la plage pour un climate à plage seule). Ils sont **bornés** par l'IO
  (`AnalogBounds::clampToBounds`), **non arrondis** : l'arrondi au pas est fait à l'application
  (voir « Écriture »), donc `21.3` écrit sur un IO se relit `21.3`. Les bornes copiées sur les
  IOs à la création deviennent périmées si `temp_min` ou `temp_max` change ensuite. Un
  `InternalInt` ne représente pas « aucune » : c'est le rôle de `cool_default_off`.
- **Valeurs initiales** (à la création de l'IO, jamais réécrites ensuite, même si le param
  change) : `heat_default` = param `heat_default_setpoint` (défaut 17) ; `cool_default` = param
  `cool_default_setpoint` (défaut 26) ; `cool_default_off` = param `cool_default_off` (défaut
  `false`) ; `program_enabled` = `true`. La consigne initiale d'un niveau est la **valeur
  courante** de l'IO `_default` de son jeu au moment où le niveau est créé. Les plages
  démarrent vides : un niveau sans créneau n'est jamais actif.
- **Création par le serveur.** Les IOs de ce tableau sont créés par le serveur
  (`Device::createOwnedLink`) à partir des params `heat_levels`, `cool_levels` et des
  consignes par défaut, pas par la liste `ios` du template : elle est statique et ne peut pas
  produire `n` niveaux. Le template ne fait que renseigner ces params (voir « Paramètres du
  device »).
- **Visibilité.** Tous les IOs ci-dessus sont `visible="false"` et `log_history` absent, comme
  `target_temp` : l'état du climate (`device.state.program`) est la vue destinée aux
  interfaces. Ils restent adressables par `set_state`, `get_timerange` et les règles.
- **Règles.** `program_enabled`, `*_setpoint`, `*_default` et `cool_default_off` se lisent et
  s'écrivent comme tout IO (conditions et actions de règles, Lua, `set_state`). Une plage se
  lit comme condition de règle (« niveau Soir actif ») comme n'importe quelle plage.
- **Sauvegarde.** Les IOs possédés s'écrivent imbriqués dans le `<calaos:device>` (voir
  « IOs possédés, liens, params »). Les valeurs de consigne et `program_enabled` survivent au
  redémarrage ; l'état « niveau actif » se recalcule au démarrage.

### Paramètres du device

Params du `<calaos:device>` (documentés dans l'IODoc, `descriptionBaseSet`/`paramAdd`) :

| Param | Défaut | Sens |
|---|---|---|
| `heat_levels` | `0` | nombre de niveaux du jeu chaud, entier de 0 à 6 |
| `cool_levels` | `0` | nombre de niveaux du jeu froid, entier de 0 à 6 |
| `program_min_gap` | `2` | écart minimal, en °C, entre consigne basse et haute en mode `auto` quand le programme les pilote (voir « Mode auto ») |
| `heat_default_setpoint` | `17` | valeur initiale de `heat_default` (°C) |
| `cool_default_setpoint` | `26` | valeur initiale de `cool_default` (°C) |
| `cool_default_off` | `false` | valeur initiale de l'IO `cool_default_off` : `true` si le défaut du froid est « arrêt » |

Les trois derniers params sont lus par le serveur quand il crée les IOs `_heat_default`,
`_cool_default` et `_cool_default_off`, et seulement à ce moment : après la création, la valeur
de l'IO fait foi et se change comme n'importe quel IO. C'est ainsi que l'installer configure les
consignes par défaut. Quand les niveaux d'un jeu passent de 0 à au moins 1, son `_default` est
créé avec la valeur du param.

**Templates.** Les params sont des champs `number` (`bool` pour `cool_default_off`) renseignés
dans l'assistant, valeur par défaut comprise (`{heat_levels}` etc. dans `params`) :
`thermostat_heat` et `thermostat_floor_zone` portent `heat_levels`, `heat_default_setpoint` ;
`thermostat_heat_cool`, `thermostat_floor_heat_cool` et `esphome_climate` portent en plus
`cool_levels`, `cool_default_setpoint`, `cool_default_off` et `program_min_gap`. Les champs sont
ajoutés aux templates par CP-BASE-LEVELS (thermostats) et CP-BASE-PROXY (`esphome_climate`).

- **Aucun niveau** (`heat_levels` = 0 et `cool_levels` = 0, valeurs par défaut) : le climate n'a
  **pas de programme**. Aucun IO du tableau n'existe, `program_enabled` non plus, l'état n'a
  pas de champ `program` et le climate se comporte exactement comme avant.
- **Un seul jeu** (`cool_levels` = 0 par exemple) : seul le jeu qui existe agit. En mode `cool`,
  rien n'est appliqué (la consigne reste celle de l'utilisateur) ; le champ `cool` est absent de
  `program`.
- **Modes offerts.** `heat_levels` > 0 exige que le climate offre `heat`, `cool_levels` > 0 qu'il
  offre `cool`. Un jeu dont le mode n'est pas offert est ignoré au chargement (`cWarning`, param
  remis à 0 en mémoire, jamais réécrit dans `io.xml`). Une vanne de zone sans froid n'a donc que
  `heat_levels`.
- **Au-delà de 6**, la valeur est ramenée à 6 au chargement avec un avertissement ; une valeur
  non entière ou négative vaut 0.
- **Réduire le nombre de niveaux.** Au rechargement de la configuration, les IOs des
  niveaux supérieurs au nouveau nombre (plage et consigne) sont supprimés du device et de
  `io.xml` : leurs créneaux et consignes sont perdus. Les augmenter crée les niveaux
  manquants, plages vides, consigne = valeur courante du `_default` du jeu.

### Application

**Déclencheurs.** Le device réévalue et écrit la consigne à chaque :
changement d'état d'une plage de niveau, changement de `mode`, changement de
`program_enabled`, changement de la consigne du niveau actif (ou de la consigne par défaut
si aucun niveau n'est actif, ou de `cool_default_off`), et une fois au démarrage, quand le
mode et les consignes sont chargés.
Un changement de plage fait par `set_timerange` prend effet immédiatement (voir
docs/08_http_api.md), sans attendre le tick suivant de `RunEventLoop`.

**Écriture.** Le programme écrit la consigne comme le ferait `set_state` du device
(`{"target_temp":…}` ou `{"target_temp_low":…,"target_temp_high":…}`) : arrondie au pas, bornée,
journalisée si refusée. Entre deux déclencheurs, la consigne reste libre : une écriture
manuelle (interface, règle, télécommande) tient jusqu'au prochain déclencheur. Sur un
`ProxyClimate`, l'écriture va à la consigne cible de l'appareil et l'emporte sur son
programme natif jusqu'à la prochaine écriture de Calaos ; Calaos ne ré-impose pas la
consigne entre deux déclencheurs. Les plages écrivent aussi les consignes d'un climate qui a
un programme natif : pas d'exclusion.

**Consigne d'un jeu** `s` (`heat` ou `cool`), pour un instant donné :

1. `L` = niveaux de `s` dont la plage est active.
2. Si `L` n'est pas vide : le niveau le plus prioritaire (numéro le plus petit),
   consigne = `<c>_<s>_level<n>_setpoint`.
3. Sinon : consigne = `<c>_<s>_default`, sauf si `s` = `cool` et `<c>_cool_default_off` = `true`,
   auquel cas elle vaut **arrêt**.

**Par mode** (pas de programme, ou programme désactivé : aucune écriture) :

| Mode | Écriture |
|---|---|
| `off`, `dry`, `fan_only` | aucune ; le programme n'agit jamais hors `heat`, `cool` et `auto` |
| `heat` | `target_temp` = consigne du jeu `heat`. Jeu `heat` absent : aucune écriture |
| `cool` | `target_temp` = consigne du jeu `cool` ; si elle vaut arrêt, voir ci-dessous. Jeu `cool` absent : aucune écriture |
| `auto` | `target_temp_low` = consigne du jeu `heat`, `target_temp_high` = consigne du jeu `cool`, avec l'écart minimal. Un jeu absent laisse sa borne telle quelle |

- **« Arrêt » du froid en `cool`.** Le serveur écrit `target_temp` au **maximum** de
  la capacité (`value_max` de la consigne, 30 par défaut) : la climatisation ne démarre que
  si la pièce dépasse cette borne, le mode reste `cool` et aucune information n'est perdue.
  Le climate n'est jamais passé en mode `off` par le programme.
- **« Arrêt » du froid en `auto`.** Pas de borne haute : `target_temp_high` est écrit au
  maximum de sa capacité (`value_max` de la plage), le froid est neutralisé, le chaud
  continue de tenir la borne basse.
- **Mode `auto`, écart minimal.** Le serveur garantit
  `high - low ≥ max(min_range, program_min_gap)`. `program_min_gap` vaut 2 °C par défaut, la
  valeur de `min_range` de `thermostat_heat_cool` ; `min_range` du device (0 par défaut)
  s'applique toujours en plus. Quand l'écart n'est pas tenu, **la borne haute monte**
  (`high = low + écart`) : le jeu chaud commande, le confort d'hiver ne cède pas à un niveau de
  froid trop bas. Si `low + écart` dépasse le maximum de la borne haute, c'est la borne basse
  qui descend (`low = high - écart`). Les deux écritures se font en **une** commande de paire
  (« validées ensemble »), jamais en deux temps.
  - L'ajustement (`écart = max(min_range, program_min_gap)`) est fait **avant**
    `ClimateBase::applyRange`, et seulement pour une paire écrite par le programme : un
    `set_state` manuel garde le refus de `applyRange` quand l'écart est trop petit.
  - Un écart qui n'est pas un multiple du pas est arrondi au multiple supérieur.
  - Si l'ajustement sort des bornes (ni `high = low + écart` ni `low = high - écart` ne tient
    dans `capabilities`), la paire n'est pas écrite et le refus est journalisé.
  - Comme pour `applyRange`, la paire n'est pas écrite si l'une des deux bornes n'a jamais
    été reçue (proxy qui n'a pas encore publié) : le prochain déclencheur réessaie.
- **Mode `auto` sur un seul jeu.** Si seul `heat_levels` > 0, le programme écrit `low` ; la borne
  haute n'est modifiée que pour respecter l'écart. Symétrique pour `cool_levels` seul.

**Programme désactivé.** `program_enabled` = `false` : plus aucune écriture, la dernière
consigne appliquée reste (aucune consigne manuelle dédiée). À la réactivation, le niveau en
cours est réappliqué (même règle que ci-dessus, aussi pour une consigne changée entre-temps).

**Consigne affichée programme désactivé.** C'est la consigne cible du climate, la
dernière écrite par le programme ou par l'utilisateur ; `device.state.program.enabled` vaut
`false` et les interfaces l'affichent comme une consigne manuelle. L'état reste calculé
(`active_level`, `setpoint` sont ceux que le programme **appliquerait**) pour que l'interface
puisse afficher « programme en pause : niveau Soir (21 °C) ».

**État du climate.** Le climate expose le programme dans `device.state.program` (forme exacte
dans docs/08_http_api.md, « Famille `climate` »). `device.capabilities.program` annonce le
nombre de niveaux de chaque jeu. Un changement d'une plage, d'une consigne de niveau, d'un
défaut ou de `program_enabled` publie un `io_changed` du climate, comme tout IO lié.

## Tests

- `core/DeviceTemplates_test` : structure de chaque template (champs, `when`, références,
  substitutions, options), types enregistrés et rôles connus d'après `io_doc.json`, rôles
  obligatoires présents, chaque param d'IO ou de device documenté, puis, pour onze jeux de valeurs
  (chaque option, chaque `when` dans les deux sens), un device qui se charge sans
  avertissement ni erreur, dont chaque lien se résout, et qui fait l'aller-retour XML sans
  qu'un attribut écrit par le template ne soit perdu ou réécrit. Un device ESPHome suit le statut
  `online`/`offline` que le faux sidecar publie sur `<topic_prefix>/status`.
- `check-gentemplates.sh` : `calaos_server --gentemplates` écrit le fichier, et sort en erreur
  sur un chemin impossible.

## Ajouter un template

1. Une fonction dans `DeviceTemplates.cpp`, ajoutée à `DeviceTemplates::all()`.
2. Au moins un jeu de valeurs dans `variants()` du test, et un par option ou `when`.
3. La liste des ids attendus dans le test et dans `check-gentemplates.sh`.
