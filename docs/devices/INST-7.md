# INST-7 — Modifier un device existant (calaos_installer)

## Objectif
Retour utilisateur du 15/10 : après création depuis un template, changer un lien (la sonde) ou un param (hystérésis, min_on_time) sans recréer le device et tout perdre.

## Livré
PR calaos/calaos_installer#71.
- « Properties » ou double-clic sur un device : `DialogEditDevice`, construit depuis IODoc par `DeviceEdit` (src/DeviceEdit.*), sans connaissance du climate. La fiche en lecture seule d'INST-2 reste pour un projet en lecture seule ou une impl absente d'IODoc.
- Liens : chaque rôle de `link_roles` choisit un IO de la maison avec les filtres de l'assistant. Le type de valeur vient des templates de l'impl, sinon de l'IO lié. S'y ajoutent writable, jamais un device, un type legacy ni un IO possédé par le device lui-même, la décision 17 entre thermostats (le message nomme le propriétaire) et un seul lien par IO inscriptible. Les rôles requis ou requis par `required_when` sont marqués et signalés, sans être refusés ; un lien vers un id disparu compte comme manquant, avec l'id. Un rôle lié à un IO possédé est affiché, pas modifiable. Changer l'io d'un lien retire son command_io, et le dialogue l'annonce.
- Params : tous les params documentés non readonly, par type. Vidé, un param n'est plus écrit. Un param modifié garde sa place, un nouveau va en dernier. Un éditeur non touché garde la valeur brute du fichier ; OK sans changement n'écrit rien et ne marque pas le projet modifié.
- En lecture seule : les command_io, et les liens et params qu'IODoc ne décrit pas, conservés tels quels.
- Libellés : `label` d'IODoc s'il existe, sinon dérivé du nom (« Current temp ») ; nom technique en infobulle.
- Rien ne touche à l'id, aux IOs possédés, à leurs liens ni aux règles. `WheelGuard` partagé avec l'assistant ; helpers de la décision 17 de `DeviceTemplates` publics. Chaînes fr.
- Revue : OK sans changement qui modifiait le projet et réécrivait des valeurs, IOs du device proposés comme cible, command_io gardé après changement d'io, lien requis vers un id disparu compté comme lié — corrigés, chacun prouvé par un test rouge.

## Mesuré
tst_deviceedit : 29 cas + captures offscreen en/fr (rôles et params lus dans IODoc, libellés, rôles possédés, lien et param changés puis aller-retour XML avec IOs possédés et rules.xml identiques, OK sans changement avec valeurs brutes intactes, command_io retiré et annoncé, non-documenté affiché, effacement, 10 refus, valeur déjà présente, échange chaud/froid, required/required_when, id disparu, 4 tests de widget). 16 suites vertes (589 cas), CI 5/5. Vrai serveur (tests/server_load) : io.xml modifié, avec un proxy à command_io, chargé sans [WRN]/[ERR], aucun attribut perdu ; témoin d'une sortie déjà possédée par un autre thermostat rouge.

## Contre-mutation
CM0 : 0 rouge. 30 mutants, tous rouges.

| Mutant | Tests rouges |
|---|---|
| legacy proposé / writable ignoré / type de valeur ignoré | ioFieldsOfferTheIosThatFit, refusals |
| IOs du device lui-même proposés | ioFieldsOfferTheIosThatFit |
| type non tiré des templates | rolesAndParamsComeFromIODoc |
| décision 17 sautée | refusals, theDialogRefusesAndSaysWhy |
| ses propres sorties comptées comme prises | itsOwnOutputsMaySwap |
| un IO inscriptible pour deux liens accepté | refusals |
| valeurs du fichier refusées | whatTheFileAlreadyHeldIsNotRefused, anotherIoDropsTheCommandIo |
| rôles possédés modifiables | linksToTheDevicesOwnIosAreNotEditable, theDialogChangesTheDevice |
| état inchangé appliqué / éditeurs non touchés réécrits | okWithoutAChangeChangesNothing (+ theDialogChangesTheDevice) |
| command_io gardé | anotherIoDropsTheCommandIo |
| lien pendant compté comme lié | requiredRolesAreReported |
| non-documenté caché / command_io d'un rôle possédé caché | whatIODocDoesNotDocumentIsShown |
| label d'IODoc ignoré | labelsComeFromIODocOrFromTheName |
| lien vidé gardé / param vidé gardé | aLinkOrAParamClearedIsRemoved (+ theDialogChangesTheDevice) |
| nouveau lien non ajouté / param modifié ajouté en double | aLinkAndAParamChangedSurviveASave |
| attributs écrits comme params | aLinkAndAParamChanged…, aLinkOrAParamCleared… |
| required_when ignoré | requiredRolesAreReported, theDialogMarksTheRequiredRoles |
| nombres non vérifiés | refusals, theDialogRefusesAndSaysWhy |
| bools / champ obligatoire non vérifiés | refusals |
| marque « requis » figée | theDialogMarksTheRequiredRoles |
| dialogue sans validation | theDialogRefusesAndSaysWhy |
| champ vidé écrit vide | theDialogChangesTheDevice |
| apply sans validation | refusals (10) |

## Reste ouvert
- L'entrée menu/double-clic et `changed()` dans FormRules n'ont pas de test automatique (FormRules ne se construit pas dans les suites).
- Libellés dérivés des noms techniques, en anglais, jusqu'à DOC-6 (`label` dans IODoc) et INST-9.
- Le type de valeur d'un rôle sans template ni lien est inconnu : tous les IOs inscriptibles sont proposés (DOC-6 : `value_type`).
- Renommer le device ne renomme pas ses IOs possédés (« {name} setpoint »).
- ASan non lancé (libasan absent de l'hôte). server_load hors CI.
