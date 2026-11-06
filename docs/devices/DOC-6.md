# DOC-6 — IODoc lisible et traduit pour les devices (calaos_base)

## Objectif
Revue d'INST-7 : le io_doc.json fr des devices était entièrement en anglais, rôles de lien et params n'avaient pas de libellé, un relais était proposé pour target_temp_low, et `emitter` était du texte libre.

## Livré
PR calaos/calaos_base#205.
- `IODoc` : `label` traduit sur chaque param (`paramAdd*`, dernier argument optionnel) et chaque rôle de lien ; `value_type` (number, bool, string) sur chaque `link_role` (`linkRoleAdd(role, label, description, valueType, …)`). Publiés dans `genDocJson` et `genDocMd` (colonnes Label et Value des rôles ; colonne Label des params seulement si un param en a un).
- Types : number pour current_temp, consignes, humidity, remote_temp, remote_temp_source ; bool pour output_heat et output_cool ; string pour mode, action, fan_mode, swing_mode.
- Libellés sur tous les params des devices (ClimateBase, Proxy, Thermostat, Device) et sur les 8 params de base d'IOBase, que les devices publient (tous les IOs gagnent donc un label sur ces 8 params).
- `emitter` du thermostat en liste (`paramAddList`) : electric, water_radiator, floor, heat_pump, fan_coil.
- 78 msgids ajoutés à `calaos.pot` et `fr.po` avec la méthode de DOC-4 ; les autres catalogues ne sont pas touchés.
- Tests : `EveryParamAndLinkRoleOfEveryDeviceHasALabel`, `TheValueTypeOfALinkRoleIsTheOneOfTheIoItTakes`, `TheEmitterIsAListOfTheValuesOfTheTemplates`, `EveryTextOfEveryDeviceDocumentationIsTranslated` (exceptions explicites et obligatoirement rencontrées : Action, InfluxDB, Mode, Modes, Visible, auto, heat_cool). `IODocJson` : signature, labels des quatre `paramAdd*`, colonnes md.
- CI : le contrôle fr ≠ en couvre chaque device (description du device, de chaque param et de chaque rôle).

## Mesuré
4 tests rouges avant le feat. CI verte : Build & test, Diff coverage, Format, MCP, Dependabot, Workflow gating. `IODocJson_test` 42, `DeviceTemplates_test` 56.

## Contre-mutation
CM0 : 0 rouge.

| Mutant | Rouge |
|---|---|
| value_type toujours number | ADeviceDocumentsItsFamilyAndItsLinkRoles, ARoleCanBeRequiredWhenAParamHasAValue, TheMarkdownListsRolesFieldsAndChildParams, TheValueTypeOfALinkRoleIsTheOneOfTheIoItTakes |
| paramAddFloat sans label | EveryKindOfParamCanCarryALabel |
| paramAddList sans label | EveryKindOfParamCanCarryALabel, EveryParamAndLinkRoleOfEveryDeviceHasALabel |
| md sans colonne Label des rôles | ARequirementCanAlsoNeedALinkToExist, ARoleCanBeRequiredWhenAParamHasAValue, TheMarkdownListsRolesFieldsAndChildParams |
| md : colonne Label des params jamais affichée | EveryKindOfParamCanCarryALabel, TheMarkdownListsRolesFieldsAndChildParams |
| output_heat en number | TheValueTypeOfALinkRoleIsTheOneOfTheIoItTakes |
| mode du proxy en bool | TheValueTypeOfALinkRoleIsTheOneOfTheIoItTakes |
| émetteur sans heat_pump | TheEmitterIsAListOfTheValuesOfTheTemplates |
| libellé Hysteresis sans `_()` | EveryTextOfEveryDeviceDocumentationIsTranslated |
| libellé Remote temperature sans `_()` | EveryTextOfEveryDeviceDocumentationIsTranslated |
| libellé d'id retiré dans IOBase | EveryParamAndLinkRoleOfEveryDeviceHasALabel |

Revue : traductions reprises (« reprise de la sortie », « l'autre source », « Thermostat », « Climatisation pilotée », « Active le mode chauffage »).

## Reste ouvert
- INST-9 : lire `label` et filtrer par `value_type` ; `./generate_iodoc.sh` dans calaos_installer après le run push de master.
- de, es, hi, nb, pl, ru n'ont toujours pas ces textes.
