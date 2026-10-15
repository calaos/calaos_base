# FIX-BASE-6 — Tests climate stables sur un runner chargé (calaos_base)

## Objectif
`InternalThermostatSafety_test` a fait échouer un run de master (`ARepeatedReadingKeepsTheSensorFresh`) : une sonde à 0,2 s de timeout, relue toutes les 50 ms sur l'horloge murale, devenait muette dès que le thread de test était retardé de ~150 ms (build de couverture, `make check -j`).

## Livré
PR calaos/calaos_base#207, head db6916a1. Tests seulement, aucun changement de production.
- Les cas qui entretiennent une sonde ont un timeout d'au moins 10 fois leur cadence (Safety `quiet`, nouveau `steady`, `dewst` ; ProxyClimate `fz`, `rq`) ; les bornes hautes et délais d'attente suivent.
- Les bornes basses sur un délai de péremption sont mesurées depuis avant la lecture qu'elles datent (`AStaleSensorStopsTheOutputsAtOnce`, `ASilentHumiditySensorCutsTheCooling`).
- Un sommeil fixe qui valait « un tour de boucle » ou « une période de keep-alive » attend la condition (`NeverBothOnWhileSomethingElseHoldsTheOtherOutput`, `AnAgingSourceIsAskedForAReading`).
- Pas d'horloge injectable : la fraîcheur lit `steady_clock` dans ClimateBase, ProxyClimate et IOBase, la régulation des timers libuv réels ; une horloge factice aurait touché trois classes de production sans couvrir les timers.
- Audit : Safety, ClimateStatus, InternalThermostat, ProxyClimate, ProxyClimateMqtt, KnxThermostatRead.

## Mesuré
- 30 boucles actives épinglées sur le cœur du test : rouges avant (NeverBothOn 6/6, AnAgingSource 6/6, AStaleSensor 2/6, plus ReadingsWithin et ARepeatedReading sous arrêt de 250 ms), 0 échec après sur les 6 binaires.
- Arrêt de 250 ms injecté dans `pumpUntil` à 5 moments : ancien fichier rouge 4 fois sur 5, nouveau fichier vert 5 fois sur 5.
- 20 exécutions de `make check -j8` sur les 6 tests, sous 80 boucles actives : 20/20 verts.
- CI verte (run pull_request).

## Reste ouvert
- Les timers de production restent en temps réel : tout nouveau test climate doit garder une marge de 10 fois sa cadence.
