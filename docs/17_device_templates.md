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
              { "name": "sensor_timeout", "value": "9000" }, { "name": "put_back_delay", "value": "1" },
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
  <calaos:param name="put_back_delay" value="1"/>
  <calaos:param name="put_back_delay_max" value="60"/>
</calaos:device>
```

## Les cinq templates

| id | impl | Demandé | IOs possédés | IO externes |
|---|---|---|---|---|
| `esphome_climate` | proxy | préfixe de topic, object id, broker, modes, deux consignes ?, bornes de consigne, action ?, modes de ventilation et d'oscillation | mode, current_temp, target_temp ou low/high, action, fan_mode, swing_mode (Mqtt), remote_temp | sonde de la pièce (optionnelle) |
| `esphome_climate_ir` | proxy | préfixe, object id, broker, modes, sonde ESPHome ?, bornes, ventilation, oscillation | mode, current_temp, target_temp, fan_mode, swing_mode (Mqtt) | — |
| `thermostat_heat` | thermostat | émetteur | target_temp (`InternalInt`), action (`InternalString`) | sonde, sortie chaud |
| `thermostat_heat_cool` | thermostat | émetteur | target_temp, low, high, action | sonde, sorties chaud et froid |
| `thermostat_floor_heat_cool` | thermostat | humidité maximale | target_temp, low, high, action | sonde, sonde d'humidité, sorties chaud et froid |

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
| `esphome_climate` avec sonde relayée | — | — | — | — | 9000 | — | — (`remote_temp_keepalive` 120) |

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
- **`humidity_max` 60 %** (le défaut du serveur reste 65 %) : à 25 °C et 60 % d'humidité, le
  point de rosée est vers 16,7 °C. Un plancher rafraîchi par de l'eau à 16 °C a sa surface au-dessus,
  avec une marge que 65 % (rosée vers 18 °C) n'aurait plus. C'est un champ du template :
  l'installateur l'abaisse si l'eau est plus froide.
- **`put_back_delay` 1 s, max 60 s** : défauts du serveur. La remise d'une sortie passe par ses
  verrous, qui protègent déjà le matériel.
- **Proxy** : `remote_temp_keepalive` 120 s, défaut du serveur ; un appareil revient à sa propre
  sonde quand les mises à jour cessent (spec §7). `sensor_timeout` 9000 s arrête le relais d'une
  sonde muette.

## Température distante sur ESPHome

ESPHome ne prend pas de température distante par MQTT d'origine. Avec le champ « sonde de la
pièce », Calaos publie la température sur `<topic_prefix>/calaos/remote_temp` à chaque changement
et toutes les `remote_temp_keepalive` secondes (`MqttOutputAnalog`, relu sur le même topic :
`MqttCtrl` refuse un IO sans `topic_sub`). Le YAML du nœud doit l'écouter ; le template porte ce
texte dans ses `notes`.

Climatiseur Mitsubishi sur CN105 (composants `cn105` de MitsubishiCN105ESPHome ou
`mitsubishi_heatpump`, dont l'id est `hp`) :

```yaml
mqtt:
  on_message:
    - topic: salon-clim/calaos/remote_temp
      then:
        - lambda: |-
            auto t = parse_number<float>(x);
            if (t.has_value()) id(hp).set_remote_temperature(*t);
```

Si le composant a une option de repli (`remote_temperature_timeout` de MitsubishiCN105ESPHome),
la régler au-dessus du keep-alive, par exemple `10min`.

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

## Tests

- `core/DeviceTemplates_test` : structure de chaque template (champs, `when`, références,
  substitutions, options), types enregistrés et rôles connus d'après `io_doc.json`, rôles
  obligatoires présents, chaque param d'IO ou de device documenté, puis, pour dix jeux de valeurs
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
