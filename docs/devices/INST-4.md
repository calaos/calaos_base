# INST-4 — Assistant de création depuis les templates (calaos_installer)

## Objectif
Dialogue « Nouveau device depuis un template » : champs générés depuis device_templates.json, sans connaissance du climate ; génération du device, de ses IOs possédés, liens et params, insertion dans l'arbre.

## Livré
PR calaos/calaos_installer#69.
- `src/DeviceTemplates.*` (modèle), format de docs/17 : chargement `:/doc/<lang>/` avec repli en, format 1, template sans family ou impl ignoré avec un avertissement ; `when` (`!`) sur IOs, liens, params et notes ; références owned/field, `{champ}`/`{id}`, valeurs vides et liens sans cible omis, params des options d'enum ; multi dans l'ordre de `values` ; nombres en décimal sans exposant ni zéro inutile. Id du device tiré du nom (ASCII minuscule, œ/æ/ß épelés) ; id pris (IO ou device, réservé par une règle, cible de lien pendante, déjà donné par l'assistant) → `_N` dès 0. Validation : requis, bornes, choix, filtre io (jamais un type que le serveur ne gère plus), même IO inscriptible pour deux champs, sortie déjà output_heat/output_cool d'un autre thermostat (décision 17, message qui le nomme).
- `DialogNewDevice` : string/secret, number (bornes, pas, unité), bool, enum, multi (cases libellées), room, io (« Choisir un IO » si requis, sinon « Aucun ») ; changer de template ne reporte que ce que l'utilisateur a saisi ; validation à Créer, erreurs listées ; notes en markdown après création. Menu « Device from a template... » ; annuler ne marque pas le projet modifié ; `addDeviceItem` insère le device et ses IOs dans l'arbre. Chaînes fr.
- `tests/server_load/run.sh` : vrai calaos_server (calaos_base master, conteneur dev) sur chaque device généré ; `check.py` échoue sans XML, auto-testé en CI.
- Choix validés par l'utilisateur : id tiré du nom ; même IO inscriptible refusé ; cibles de liens pendantes comptées comme prises. Le `when` sur un champ, hors format, a été retiré. Revue : décision 17 entre devices, report des seuls champs saisis au changement de template, types legacy exclus.

## Mesuré
tst_devicetemplates 104 (42 jeux : chaque option, chaque `when` dans les deux sens ; rôles, params et params d'IO documentés dans io_doc.json, liens résolus, aller-retour XML ; variantes nommées ; exemple de docs/17 ; ids libres ; validation ; décision 17 ; IO retiré du serveur ; nombres ; arbre), tst_dialognewdevice 12 (+ captures offscreen en/fr), test_check.py 4. 15 suites vertes (555 cas). Vrai serveur : 42/42 sans [WRN]/[ERR] au chargement, aucun attribut perdu à la sauvegarde ; un témoin avec un lien cassé est rouge. CI 5/5.

## Contre-mutation
CM0 : 0 rouge. 29 mutants, tous rouges.

| Mutant | Tests rouges |
|---|---|
| `!` ignoré dans `when` | substitutionFollowsTheRules, theNamedVariants…, theOwnedIosCarryTheFieldValues |
| multi dans l'ordre coché | substitutionFollowsTheRules, theNamedVariants… |
| `_N` dès 1 | aSecondDevice…, aTakenId…, idsAssignedByTheWizard… |
| lien sans cible écrit / param vide écrit | everyVariantBuildsAValidDevice (38 / 37 rouges), substitution…, theNamedVariants… |
| params d'option d'enum ignorés | anEnumOptionSetsItsParams, fillingTheDialog…, substitution…, theNamedVariants… |
| multi requis vide accepté / bornes non vérifiées | aRequiredMulti…, validation / validation |
| writable ignoré | everyVariant…, ioFieldsListTheIosThatFit, ioFieldsOffer…, validation |
| id réservé par une règle libre / cible de lien pendante libre | aTakenIdGetsTheFirstFreeSuffix |
| ids de l'assistant non réservés | idsAssignedByTheWizardAreTakenToo |
| bool 1/0 | substitutionFollowsTheRules |
| zéros inutiles gardés / exposant gardé | numbersAreWrittenInDecimal, aSmallStepGetsItsDecimals (+ substitution…, theNamedVariants…, theOwnedIos…, validation, editorsFollow…) |
| accents en séparateur / œ non épelé | theDeviceIdComesFromItsName (+ substitution…) |
| même IO inscriptible deux fois accepté | validation |
| sortie d'un autre thermostat acceptée / tout device possède ses rôles de sortie | anOutputOfAnotherThermostatIsRefused |
| type retiré du serveur proposé | anIoTheServerDroppedIsNotOffered |
| template sans impl gardé | aTemplateWithoutFamilyOrImplIsSkipped |
| défauts non touchés reportés au changement de template | switchingTemplatesKeepsOnlyWhatTheUserChanged |
| dialogue sans validation / sans trim / io requis présélectionné / secret en clair | createIsRefused…, aRequiredMulti… / fillingTheDialog… / createIsRefused…, ioFieldsOffer… / editorsFollowTheFieldTypes |
| arbre sans sorties possédées ; sortie possédée créée en entrée | theDeviceIsInsertedInTheTreeAsAFreshTreeShowsIt |

## Reste ouvert
- Templates fr encore en anglais jusqu'à DOC-4.
- « Annuler ne marque pas le projet modifié » n'a pas de test automatique (FormRules ne se construit pas dans les suites).
- ASan non lancé : libasan absent de l'hôte.
- server_load hors CI (il faut un build de calaos_base) ; son auto-test check.py est en CI.
