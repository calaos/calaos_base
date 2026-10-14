# DOC-1 — Format de commande dans IODoc (calaos_base)

## Objectif
IODoc décrit la commande JSON d'un device (spec §11) : `command_format: json`, un champ par entrée
(type, valeurs ou bornes, unité, rôles de lien requis), plus les rôles de lien et les params de
ProxyClimate et d'InternalThermostat. Les devices ne figurent plus parmi les types d'IO de `io_doc.json`.

## Livré
PR calaos/calaos_base#194.
- `IODoc` : `commandFieldEnum`/`commandFieldNumber`, `deviceFamilySet`, `linkRoleAdd`, `paramInChild`.
  `genDocJson` publie `command_format`, `command_fields` (dans l'ordre de déclaration), `family`,
  `link_roles` ; `genDocMd` les tables « Link roles » et « Command ». Bornes en nombres JSON, listes
  d'instance par `values_param`. Un IO ordinaire est inchangé (5 sections).
- Ajouts non cassants avant INST-5 : `value_requires_links` (rôles qu'une valeur d'enum exige en plus,
  cool → output_cool) ; `required_when` sur un rôle (`{param, value}`, humidity quand emitter=floor,
  posé pour CLIM-4) ; `paramInChild` indépendant de l'ordre des appels.
- `writable` = « le device écrit ce lien » (appareil, sortie, ou IO qu'il possède : l'action du thermostat).
- `ClimateBase::documentCommand(modes, modeLink, modeNote, modeRequires)` : mode et consignes. Ce que
  seules certaines impls respectent n'est documenté que par elles : `documentModesParam()` et
  `documentFanSwing()` (modes, fan_modes, swing_modes, fan_mode, swing_mode) sont appelées par le proxy ;
  le thermostat ne les publie pas, il les ignore. `min_range` reste dans la base.
- `ProxyClimate` : `auto_payload`, `remote_temp_keepalive` (120 s), `readback_hold` (10 s) ; 10 rôles de
  lien, seul `mode` obligatoire. Un proxy sans lien `mode` se charge (§3.2, « required » est une
  indication pour l'installer) et journalise un avertissement.
- `InternalThermostat` : 4 modes sans lien requis, chacun avec ses sorties (heat → output_heat,
  cool → output_cool, auto → les deux) ; params `hysteresis` (0.5 ; ne joue qu'à l'allumage), `min_on_time`
  (ignoré par le mode off et la coupure de sûreté), `min_off_time` (0), `put_back_delay` (1 s, au moins
  1 ms), `put_back_delay_max` (60 s, fixe aussi la fenêtre du journal des écritures extérieures) ;
  current_temp, target_temp, output_heat obligatoires ; target_temp_low/high, action (écrite par le
  thermostat), output_cool optionnels ; décision 17 dans les descriptions des sorties.
- `Device::takeParamReads()` : noms de params lus depuis le dernier appel, pour le test « chaque param
  documenté est lu ».
- Les clés `device:<impl>` ne sont plus au premier niveau de `io_doc.json` : elles sont sous `"devices"`,
  par impl, et en fin de `io_doc.md`. Le premier niveau garde les types d'IO plus cette clé réservée
  (qui n'est pas un type : un client qui énumère les clés doit l'ignorer).
- `linkRoleAdd(..., requiredWhenParam, requiredWhenValue, requiredWhenLink)` : `required_when` est
  `{param, value, link?}` ; `link` s'ajoute en ET (humidity requis si emitter=floor ET output_cool lié,
  §6.3). Colonne Required du md : `when emitter=floor and output_cool`.
- Les tests `EveryDocumentedParamIsRead` (proxy, thermostat) vérifient dans les deux sens : tout param
  documenté est lu par l'impl, et tout param lu est documenté. Seuls `modes`, `fan_modes`, `swing_modes`
  sont exemptés pour le thermostat (validés par ClimateBase, non respectés).
- `TheHysteresisParamSetsTheSwitchOnThreshold` : hysteresis=2 déplace le seuil d'allumage (chaud et froid).
- `po/POTFILES.in` : ClimateBase, Device, InternalThermostat, ProxyClimate ; `tests/check-potfiles-devices.sh`
  vérifie les sources de IO/ qui marquent des chaînes.
- Régénération : `io_doc.json` n'est pas versionné dans calaos_base (`scripts/create_iodoc` → `build_doc/`).
  Aucun workflow ne compare le généré au commité : c'est DOC-3, qui regénère aussi en et fr.

## Mesuré
`make check` 193 tests (190 PASS, 3 SKIP) ; `IODocJson_test` 38 cas, `ClimateBase_test` 28,
`ProxyClimate_test` 33, `InternalThermostat_test` 59 ; diff coverage 193 lignes couvertes, 0 non couverte ;
clang-format-14 propre ; CI verte.

## Contre-mutation
CM0 : 0 rouge (avant et après les campagnes). 70 mutants, chacun rebâti depuis des objets propres, tous
rouges. Première campagne (40) : IODocFactory (devices parmi les types, impl avec préfixe, md ou json sans
devices), IODoc (command_format, family, link_roles, values_param, min/max permutés, requires_links,
marque de param, required inversé, valeurs du md), ClimateBase (mode requis, low sans high, max documenté,
fan sur la liste swing), ProxyClimate et InternalThermostat (défauts, rôles, marques, modes, note de mode,
décision 17, avertissement sans lien mode). Deuxième campagne (19) :

| Mutant | Tests rouges |
|---|---|
| le proxy ne documente pas fan/swing, ni modes | TheProxyDocumentsItsCommandAndItsLinks/Params, EveryDocumentedParamIsRead, TheDocumentedFieldsAreAcceptedExactly… |
| le thermostat documente modes (qu'il ignore) | TheThermostatIsDocumentedUnderDevicesLikeTheProxy, InternalThermostatTest.EveryDocumentedParamIsRead |
| le thermostat documente fan/swing | TheThermostatIsDocumented…, EveryDocumentedParamIsRead, TheDocumentedFieldsAreAcceptedExactly… |
| action non writable, description d'hysteresis ou de min_on_time réécrite | TheThermostatIsDocumentedUnderDevicesLikeTheProxy |
| marque perdue si posée avant paramAdd | AParamMarkedBeforeItIsAddedIsMarked |
| marque effacée par paramAddList | AParamAddedAgainKeepsItsMark |
| value_requires_links absent | AnEnumValueCanNeedLinksOfItsOwn, TheThermostatIsDocumented…, AnImplThatStoresItsMode… |
| required_when sans sa valeur, ou permuté dans le md | ARoleCanBeRequiredWhenAParamHasAValue |
| exigences de valeur absentes du md | AnEnumValueCanNeedLinksOfItsOwn |
| « Writable » non défini dans le md | TheMarkdownListsRolesFieldsAndChildParams |
| takeParamReads ne vide pas / getDeviceParam non enregistré | ProxyClimateTest et InternalThermostatTest.EveryDocumentedParamIsRead |
| documentCommand ignore modeRequires, auto n'exige que output_cool | TheThermostatIsDocumented…, AnImplThatStoresItsMode… |
| param modes non marqué | TheProxyDocumentsItsParams, ProxyClimateTest.EveryDocumentedParamIsRead |
| ProxyClimate.cpp ou Device.cpp retiré de POTFILES.in | check-potfiles-devices.sh (rouge, vérifié à la main) |

Troisième campagne (11) — param lu au chargement mais ignoré, et documentation :

| Mutant | Tests rouges |
|---|---|
| hysteresis validé puis ignoré | InternalThermostatTest.TheHysteresisParamSetsTheSwitchOnThreshold (seul ; survivait avant) |
| min_on_time / min_off_time ignorés | MinOnTimeDefersTheSwitchOff / MinOffTimeDefersTheSwitchOn et 5 autres chacun |
| put_back_delay ignoré | ThePutBackSpacingResetsOnceTheOutputIsLeftAlone et 3 autres |
| put_back_delay_max ignoré | TheWriteWarningIsLimitedPerSourceAndOutput |
| remote_temp_keepalive ignoré (proxy) | TheKeepAliveRepeatsTheLastValueWhileTheSourceIsSilent, ADisconnectedSourceIsNotRelayed, TheKeepAliveStopsWhenTheDeviceIsDestroyed |
| readback_hold ignoré (proxy) | ProxyClimateTest.ACommandedValueIsForgottenAfterTheHold |
| le thermostat / le proxy lit un param non documenté | EveryDocumentedParamIsRead (thermostat / proxy, seul) |
| hysteresis / readback_hold documenté sous un autre nom | TheThermostatIsDocumented… / TheProxyDocumentsItsParams, EveryDocumentedParamIsRead |
| required_when : lien erroné / jamais publié / absent du md | ARequirementCanAlsoNeedALinkToExist |

## Reste ouvert
- CLIM-4 documente `emitter`, `sensor_timeout`, `humidity_max` et le rôle `humidity` (avec `required_when`
  et ses tests) s'il est mergé après #194.
- DOC-3 : lire `devices[impl]` ; comparer le JSON parsé (l'ordre du md n'est pas stable) ; faire ignorer
  `devices` par `tst_servertypes` de l'installateur ; regénérer et copier en et fr dans l'installateur.
- Le plafond de doublement de `put_back_delay_max` n'a pas de test propre (seule la fenêtre du journal le prouve).
- `EveryDocumentedParamIsRead` repose sur une liste par impl : un param classé « pris au chargement »
  alors qu'il n'est que validé passerait. À relire en revue quand on y ajoute un param.
- Les bornes de température documentées sont les défauts 7/35/0.5/°C ; les bornes effectives d'une
  instance sont dans `capabilities.target_temp`.
