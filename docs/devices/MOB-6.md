# MOB-6 — Traduction fr de l'UI climate (calaos_mobile)

## Objectif
Aucune chaîne climate de MOB-2 à MOB-5 n'était dans `lang/*.ts` : l'UI climate restait en anglais pour un utilisateur français.

## Livré
PR calaos/calaos_mobile#27.
- `lupdate` (lang/lang.pro, emplacements relatifs, traductions existantes conservées) puis `lrelease` : calaos_fr.ts et calaos_fr.qm. 48 nouvelles chaînes traduites, dont 46 climate (7 contextes), avec le vocabulaire de la web app et du panneau : consigne, sonde, « Démarre dans N min », « En attente de la sonde », « Sonde déconnectée », modes Arrêt / Chauffage / Froid / Auto / Déshumidification / Ventilation, « Écart minimum », « Plus de réglages », Ventilation, Oscillation, Humidité.
- `swingLabel()` : une oscillation à l'arrêt se lit « Fixe » (comme la web app), plus « Arrêt » ; l'option off de ventilation est distinguée par un commentaire de désambiguïsation (« Désactivé »).
- Tests : le .qm se charge, toutes les chaînes climate du .ts sont finies et non vides, les libellés de la liste, de la sheet, des choix empilés et du dialogue sortent en français.

## Mesuré
Qt Quick Test 401 verts (17 de traduction), CI 3/3. Captures RHI fr de la liste (10 états) et de la sheet, vérifiées par l'orchestrateur.

## Contre-mutation
CM0 : 0 rouge (24 rouges contre l'ancien .ts). Deux mutants tués : `swingLabel` retiré (test_theMoreSettingsInFrench), une traduction repassée en unfinished (test_everyClimateStringIsTranslated).

## Reste ouvert
- calaos_de.ts non mis à jour (les chaînes climate restent en anglais en allemand).
- « Démarre dans 1 min 5 » suit la web app (secondes non complétées à deux chiffres).
