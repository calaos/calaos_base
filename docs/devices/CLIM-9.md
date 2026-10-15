# CLIM-9 — Vraisemblance de la sonde du thermostat (calaos_base + clients)

## Objectif
Analyse du plancher chauffant du 15/10 : une sonde coupée ou en court-circuit lit une valeur absurde que le thermostat prenait pour une mesure. Params `valid_min` / `valid_max` ; une lecture hors bornes vaut absence de lecture valide.

## Livré
PR calaos_base#208 ; clients calaos-web-app#31, calaos_mobile#29, calaos_remote_ui#10.
- `InternalThermostat` : `valid_min` / `valid_max` (absents = pas de contrôle, bornes incluses). Une lecture fraîche hors bornes : sorties OFF aussitôt (pas retardé par `min_on_time` ; `min_off_time` garde le redémarrage), `fault: sensor_out_of_range`, mode et consignes réglables, retour à la première lecture valide. Une lecture non finie (NaN, infini) est toujours hors bornes, même sans bornes. Journal une fois par épisode, rien en mode `off`. Pas d'hystérésis sur les bornes (documenté : une sonde qui oscille sur une borne cycle au rythme de `min_off_time`).
- Précédence du `fault` (spec §8.2, §5.3) : sensor_missing, sensor_waiting, sensor_stale, sensor_disconnected, sensor_out_of_range, puis humidity_high / humidity_stale.
- Templates `thermostat_*` : −30 à 60 °C (DS18B20 85 °C à la mise sous tension, −127 °C débranchée, sonde Wago ouverte ou en court-circuit ; un garage hors gel reste régulé). Templates ESPHome sans bornes (proxies).
- IODoc, po/calaos.pot et fr.po seulement, docs/01 et docs/17.
- Clients : « Sonde hors limites » (panneau : « Hors limites »).

## Mesuré
make check 208 tests (1 échec local sans lien : paquet python `mcp` du conteneur) ; CI verte sur les 4 PR. Revue opus : clients OK, serveur approuvé après corrections (churn po retiré, bornes, NaN, journaux).

## Contre-mutation
CM0 : 0 rouge. 10 mutants serveur tués (borne min ignorée, `>=`/`<=` stricts, fault non rapporté, sorties laissées actives, bornes contradictoires, `outOfRange` non mémorisé, `valid_max` retiré du template, `fresh` trop large, `isfinite` retiré). Clients : 9 mutants tués (web 3, mobile 3, panneau 3).

## Reste ouvert
- Catalogues de, es, hi, nb, pl, ru sans les nouveaux textes IODoc ; calaos_de.ts mobile.
- L'assistant de l'installer pourrait proposer `valid_min` / `valid_max` (l'éditeur les montre déjà comme params documentés).
- Paquet python `mcp` du conteneur de dev à mettre à jour.
