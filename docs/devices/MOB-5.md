# MOB-5 — Libellés des nouveaux défauts (calaos_mobile)

## Objectif
Afficher les défauts `sensor_waiting` et `sensor_disconnected` publiés par le thermostat depuis CLIM-7 (décision 20), au lieu du libellé brut de `humanize()`.

## Livré
PR calaos/calaos_mobile#26.
- `faultLabel()` (ClimateModel.qml) : `sensor_waiting` = « Waiting for the sensor », `sensor_disconnected` = « Sensor disconnected », en `qsTr`. Les deux restent en ambre dans la liste et dans le cadran, comme les autres défauts.
- Deux lignes dans `test_faultStatus_data` (Qt Quick Test).

## Mesuré
Qt Quick Test 372 verts, CI 3/3. Diff vérifié par l'orchestrateur.

## Contre-mutation
CM0 : 0 rouge. Deux mutants tués : `sensor_waiting` rendu brut (test_faultStatus(sensor_waiting)), `sensor_disconnected` remplacé (test_faultStatus(sensor_disconnected)).

## Reste ouvert
Traductions fr non ajoutées : aucune chaîne climate de MOB-2 à MOB-5 n'est dans `lang/*.ts` (MOB-6).
