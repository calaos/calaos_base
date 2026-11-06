# DOC-2 — Templates de devices (calaos_base)

## Objectif
Spec §10 : les templates dont l'assistant de l'installer (INST-4) tire un device complet, sans rien savoir du climate. Générés par `calaos_server --gentemplates`, à côté de `--gendoc`.

## Livré
PR calaos/calaos_base#201.
- `IO/DeviceTemplates.{h,cpp}` : `device_templates.json` (format 1, ASCII, textes traduits dans la locale). Un template porte `id`, `family`, `impl`, `attributes`, `fields`, `ios`, `links`, `params`, `notes`. Types de champ : string (secret), number, bool, enum (options qui portent des params), multi, room, io (`io_filter` : value number|bool|string, writable). Références `{owned: clé}` / `{field: nom}` ; `{champ}` et `{id}` substitués ; `when` (`!` pour inverser) ; un param vide n'est pas écrit ; un lien sans cible non plus. Toutes les options d'un enum fixent les mêmes params, et ceux du template sont ceux de l'option par défaut.
- Cinq templates : esphome_climate (deux consignes, action, ventilation et oscillation, sonde relayée en option), esphome_climate_ir, thermostat_heat, thermostat_heat_cool, thermostat_floor_heat_cool. Les sorties des thermostats sont des IO existants à choisir. `auto_payload=heat_cool` est écrit dans les deux templates ESPHome.
- Température distante ESPHome : Calaos publie sur `<prefix>/calaos/remote_temp`. Raccord YAML documenté (CN105 : `mqtt.on_message` vers `set_remote_temperature` ; thermostat ESPHome : capteur `mqtt_subscribe` avec un filtre `timeout`) et repris dans les `notes` du template.
- Valeurs par défaut par émetteur (point ouvert §14) : table de docs/17_device_templates.md.
- `main.cpp` : `--gentemplates <dir>` (code 1 si le fichier n'est pas écrit) ; `scripts/create_iodoc` appelle les deux options, en et fr.
- Correctif : InternalInt/Bool/String documentent le param `value` qu'ils lisent (commit `test:` rouge avant).
- docs/17_device_templates.md (format, règles de l'assistant, exemple, défauts, YAML).

## Mesuré
`make check` 201 (198 PASS, 3 SKIP). DeviceTemplates_test : 49 cas, dont 10 jeux de valeurs (chaque option, chaque `when` dans les deux sens). Chaque device se charge sans [WRN]/[ERR], ses liens se résolvent, ses liens et défauts sont ceux attendus, il utilise les topics ESPHome et fait l'aller-retour XML sans perdre un attribut. check-gentemplates.sh ajouté, IODocJson_test à 39 cas. Couverture différentielle : 611 lignes, 313 couvertes, 0 non couverte. ASan propre. CI verte.

## Contre-mutation
CM0 : 0 rouge. 30 mutants, tous rouges :

| Mutant | Tests rouges |
|---|---|
| auto_payload auto (esphome, IR) | TheDeviceHasTheLinksAndDefaultsOfItsVariant/esphome_* (2 + 2) |
| hystérésis électrique 0.5 ; min_off radiateur 120 ; min_on PAC 300 ; min_range ventilo 2 | TheDeviceHasTheLinksAndDefaultsOfItsVariant/<variante> |
| emitter du plancher réversible ≠ floor ; humidity_max absent | …Defaults…/floor_heat_cool |
| lien humidity absent | ADeviceBuiltFromTheTemplateLoadsWithoutAWarning/floor_heat_cool, …Defaults… |
| `when` de target_temp inversé | …Defaults…/esphome_full, esphome_minimal |
| topic command → set ; current_temperature renommé ; port pris du champ host | EsphomeVariantTest.TheIosUseTheTopicsOfEsphome/* |
| remote_temp sans topic_sub | LoadsWithoutAWarning/esphome_full, TheIosUseTheTopicsOfEsphome/esphome_full |
| sortie de thermostat non writable ; famille Climate | EveryTemplateUsesRegisteredTypesAndKnownRoles |
| value_max 35 ; consigne initiale 21 | …Defaults…/thermostats (6 / 3) |
| generate() sans mkpath | TheFileHoldsTheFiveTemplatesOfTheSpec, check-gentemplates.sh |
| generate() renvoie vrai sur échec | AnUnwritablePathIsReported, check-gentemplates.sh |
| --gentemplates sort toujours 0 ; option mal nommée | check-gentemplates.sh |
| param value d'InternalInt non documenté | EveryParamIsDocumented, TheInternalIosDocumentTheInitialValueTheyRead |
| template plancher non publié | TheFileHolds…, et les 4 tests floor_heat_cool |
| filtre de sonde string | TheTestValuesFitTheFields/thermostats (6) |
| pas de champ room ; `when` de note inconnu ; option électrique sans params | EveryTemplateIsSelfConsistent |
| format 2 | TheFileHolds…, check-gentemplates.sh |
| sortie non ASCII | TheFileHoldsTheFiveTemplatesOfTheSpec |

## Reste ouvert
- Valeurs par défaut à valider par l'utilisateur (spec §14).
- Lien de connexion du proxy (statut ESPHome `<prefix>/status`) : non posé, à trancher avec CLIM-6.
- auto_payload d'un CN105 qui rapporte `auto` : à changer à la main après création.
- `required_when` n'est vérifié que par le chargement, pas par l'analyse statique du test.
- DOC-3 : publier `device_templates.json` en artefact et l'importer dans l'installer.
- Les templates ESPHome ne créent pas d'IO pour `current_humidity/state` (rôle `humidity` facultatif du proxy) : option à ajouter si un climate ESPHome publie l'humidité.
