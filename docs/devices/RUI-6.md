# RUI-6 — Libellés des nouveaux défauts (calaos_remote_ui)

## Objectif
Afficher en français les défauts `sensor_waiting` et `sensor_disconnected` du thermostat (CLIM-7, décision 20), qui s'affichaient en anglais (« Sensor waiting »).

## Livré
PR calaos/calaos_remote_ui#9.
- `faultLabelFr()` : « En attente de la sonde » / « Attente » et « Sonde déconnectée » / « Déconnectée » (forme longue dans les 2×1, 1×2, 2×2 et la vue plein écran, courte dans les 1×1). Le thermostat reste réglable sous ces défauts.
- En 2×1 sur cellules de 180 px, une forme longue qui prendrait trois lignes prend la forme courte (comme le 1×2).
- Fixtures de chaque défaut dans les tests « chaque état tient dans chaque cellule » (petits et grands widgets), mots et couleurs, statut.

## Mesuré
220 + 98 tests hôte ; CI verte (ESP32, simulateur, tests sous ASan et sous TSan) ; commit `test:` rouge avant le correctif (5 tests). Diff vérifié par l'orchestrateur.

## Contre-mutation
4 mutants tués : formes longue et courte inversées (3 rouges), `sensor_disconnected` retiré, retour à l'anglais (5 rouges, dont ClimateSmall.EveryFixtureFitsEveryCell), 2×1 qui ne raccourcit jamais (ClimateSmall.EveryFixtureFitsEveryCell), `sensor_waiting` dit « Sonde absente » (3 rouges).

## Reste ouvert
Rien.
