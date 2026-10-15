# CLIM-7 — Statut de la sonde du thermostat (calaos_base)

## Objectif
Décision 20 : `connected: false` est réservé au proxy injoignable. Un thermostat reste joignable et réglable, sa sonde dit pourquoi elle tient les sorties OFF. `family` est publiée en minuscules.

## Livré
PR calaos/calaos_base#203. Les sorties restent OFF dans tous les cas : seul le statut publié change.
- `connected: false` : publié par `ProxyClimate::deviceState()` quand son lien `mode` se déclare déconnecté. `ClimateBase` ne le publie plus (`connectionRole()` supprimé).
- `fault` de la sonde du thermostat (hors mode `off`) : `sensor_missing` (pas de lien `current_temp`), `sensor_waiting` (aucune lecture depuis le démarrage), `sensor_stale`, `sensor_disconnected` (mode et consigne restent acceptés).
- `family`, `gui_type` et `impl` publiés en minuscules (`gui_type` est la famille, les clients choisissent leur widget dessus).
- docs/01_core_data_model.md mis à jour.

## Mesuré
- `make check` : 208 tests, 205 PASS, 2 SKIP, 1 FAIL local (`run-python-tests.sh` : paquet `mcp` trop ancien dans le conteneur de dev, sans lien ; vert en CI).
- `ClimateStatus_test` : 17 cas ; golden `clim6_ws_get_home_status` régénéré par `CALAOS_GOLDEN_UPDATE=1`.
- Couverture différentielle (CI) : 34 lignes ajoutées sous src/, 18 couvertes, 0 non couverte.
- ASan : 0 erreur sur 5 binaires. CI verte.

## Contre-mutation
CM0 : 0 rouge. 8 mutants, 8 tués.
- CM1 lien `current_temp` absent non distingué, CM2 `sensor_waiting` dit `sensor_missing` → TheSensorFaultSaysWhy
- CM3 sonde déconnectée sans `fault` → ADisconnectedThermostatSensorIsAFault
- CM4 proxy sans `connected` → AnUnreachableApplianceIsNotConnected, GetHomeCarriesTheStatusFields
- CM5 proxy `connected: false` dès qu'il n'est pas frais → AnUnreachableApplianceIsNotConnected
- CM6 `family` non mise en minuscules → TheFamilyIsPublishedInLowerCase, GetHomeCarriesTheStatusFields
- CM7 `impl` non mis en minuscules → TheImplPublishedIsTheFactoryKey, GetHomeCarriesTheStatusFields
- CM8 `gui_type` non mis en minuscules → GetHomeCarriesTheStatusFields

## Reste ouvert
- Clients : libellés « Sonde déconnectée » et « En attente de la sonde » (WEB-4, MOB-5, RUI-6).
- Conteneur de dev : paquet python `mcp` à mettre à jour.
