# CLIM-2 — `ProxyClimate` (calaos_base)

## Objectif
`impl="proxy"` (spec §7) : l'appareil régule, Calaos relaie. Lecture sur l'`io` de chaque lien, écriture sur son `command_io` (ou l'`io` si c'est une sortie), mode miroir, relais de la température distante.

## Livré
PR calaos/calaos_base#191.
- `heat_cool` lu `auto` ; `auto` écrit selon le param `auto_payload` (`auto`|`heat_cool`), sinon la dernière graphie rapportée, sinon `heat_cool`. Valeurs brutes inconnues non publiées, loguées une fois par valeur (16 max, étiquette si octets non triviaux).
- Lien non inscriptible (sans `command_io`, `io` qui n'est pas une sortie : un `MqttInputAnalog` accepte un `set_value` et ne l'envoie nulle part) : pour `mode`, `target_temp`, `low`/`high`, `fan_mode`, `swing_mode`, `remote_temp`, capacité non offerte (`target_temp_range` exige les deux bornes), commande refusée en entier, rien relayé, erreur au chargement. Règle portée par `ClimateBase::isWritable(role)` ; le proxy répond `isOutput()` sur la cible d'écriture. (Revue : seul `mode` était protégé.)
- Température distante : relais à chaque changement et toutes les `remote_temp_keepalive` s (défaut 120, `Timer` membre), valeur identique comprise ; rien avant `hasValue()`, rien tant que la source se déclare déconnectée. Une source figée mais « connectée » reste relayée (pas d'horodatage sur les IOs) : arrêt reporté sur CLIM-4.
- Relecture en retard (note de revue CLIM-1) : une plage partielle est validée contre la dernière valeur commandée de l'autre borne jusqu'à relecture ou `readback_hold` s (défaut 10) ; hooks `rangeBound`/`writeRangeBound` dans `ClimateBase`. Écriture refusée non retenue.
- Test de bout en bout sur de vrais IOs Mqtt contre le faux sidecar de MqttOutputString_test (keep-alive à valeur identique publié, mode `heat_cool`/`auto`, entrées non écrivables).

## Mesuré
`make check` 188 tests (185 PASS, 3 SKIP) ; `ProxyClimate_test` 29 cas, `ProxyClimateMqtt_test` 4 cas ; ASan propre ; porte de couverture verte ; CI verte.

## Contre-mutation
CM0 : 0 rouge (build normal et ASan). 30 mutants tués :
- Mode et log : heat_cool non lu ; auto toujours `auto` ; auto sans apprentissage ; `auto_payload` ignoré ; mode inconnu logué à chaque fois ; log non borné ; octets bruts loggés ; durée ≤ 0 acceptée.
- Température distante : relais sans `hasValue` ; sans relais sur changement ; sans timer ; période ignorée ; écrit sur `io` au lieu de `command_io` ; source déconnectée relayée ; keep-alive qui saute une valeur inchangée (e2e Mqtt).
- Relecture en retard : valeur commandée inutilisée ; jamais effacée par la relecture ; jamais expirée ; écriture refusée retenue ; paire non retenue.
- Inscriptibilité : lien mode non inscriptible offert ; toute io inscriptible (état avant correctif) ; `target_temp` appliqué sur une entrée ; fan/swing appliqués sur une entrée ; plage qui ignore l'inscriptibilité ; capacité `target_temp` ou `remote_temp` qui l'ignore ; relais non gardé ; liens non inscriptibles non loggés.
- ASan : timer qui survit au device (heap-use-after-free).

## Reste ouvert
- Graphie d'auto apprise non persistante.
- DOC-1/2 : params `auto_payload`, `remote_temp_keepalive`, `readback_hold` à documenter.
- CLIM-4 : arrêt du relais pour une source figée mais « connectée ».
- MqttCtrl garde la souscription d'un IO détruit (appel d'un IO libéré au rechargement) : ticket FIX-BASE-3.
