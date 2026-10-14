# CLIM-6 — État affichable du climate (calaos_base)

## Objectif
Spec §5.3 : le `state` d'un device climate explique ce qu'il fait ([UI_CLIMATE.md](UI_CLIMATE.md), « Statut affiché »).

## Livré
PR calaos/calaos_base#199. Chaque champ est absent quand il ne s'applique pas.
- **`humidity`** : la valeur du lien `humidity` dès qu'il en a une (thermostat et proxy ; rôle documenté côté proxy).
- **`connected: false`** : tant que l'IO de `connectionRole()` se déclare déconnecté (`freshness()` = `Disconnected` ; `mode` pour le proxy, toutes les entités ESPHome partageant le même testament ; `current_temp` pour le thermostat). Jamais `true`.
- **`lockout: {"output": "heat"|"cool", "remaining": N}`** (thermostat) : une sortie voulue ON attend son verrou (`min_off`, bascule, espacement des remises) ; `remaining` en secondes, arrondi au supérieur, recalculé à chaque publication. Un arrêt retardé par `min_on_time` n'en est pas un.
- **`fault`** (thermostat, hors mode `off`) : `sensor_missing` (pas de lien ou aucune lecture depuis le démarrage), `sensor_stale`, puis, pour un plancher en `cool`/`auto`, `humidity_high` ou `humidity_stale`. Une sonde déconnectée donne `connected: false`, pas un `fault`.
- **Publication** : le début et la fin d'un verrou ou d'un défaut publient le device (`io_changed`) ; un changement de disponibilité d'un IO (`setStatusInfo(Connected)`) notifie `IOChangeHub`, ce qui republie les devices liés.
- **`impl`** publié tel que la factory l'a utilisé, en minuscules (revue de MCP-1 : contournement de la protection des relais par la casse).
- docs/01_core_data_model.md mis à jour.

## Mesuré
- `make check` : 205 tests (202 PASS, 3 SKIP).
- Nouveau binaire `ClimateStatus_test` : 15 cas (chaque champ présent et absent, publication au début et à la fin). Golden `clim6_ws_get_home_status` généré par `CALAOS_GOLDEN_UPDATE=1`. `IODocJson_test` : rôle `humidity` du proxy.
- Couverture différentielle (CI) : 95 lignes ajoutées sous src/, 52 couvertes, 0 non couverte, 0 exclue.
- ASan : 0 erreur sur 7 binaires. CI verte.

## Contre-mutation
CM0 : 0 rouge. 21 mutants, 21 tués.
- CM1 humidité publiée sans valeur → HumidityIsPublishedOnceRead, TheDewPointCutIsAFault
- CM2 `connected` dès que la sonde n'est pas fraîche, CM3 `connected` jamais écrit → ADisconnectedSensorOrApplianceIsPublished, GetHomeCarriesTheStatusFields
- CM4 pas de notification sur changement de disponibilité, CM5 thermostat connecté par `mode` → ADisconnectedSensorOrApplianceIsPublished
- CM6 verrou jamais noté → 5 tests de verrou ; CM7 arrêt retenu par `min_on` compté comme verrou → AStopHeldByMinOnTimeIsNoLockOut ; CM8 verrou jamais effacé, CM17 `remaining` arrondi à l'inférieur → ALockOutSaysWhichOutputAndHowLong
- CM9 jamais de `fault`, CM18 `fault` jamais publié → 6 tests ; CM10 `fault` en mode off → AMissingSensorIsAFaultUnlessTheModeIsOff, GetHomeCarriesTheStatusFields ; CM11 sonde absente dite muette → AMissingSensorIsAFaultUnlessTheModeIsOff ; CM12 sonde muette sans `fault` → AStaleSensorIsAFaultWhileItLasts, AFaultStartingAloneIsPublished
- CM13 humidité toujours « haute » → TheDewPointCutIsAFault, HumidityIsNoFaultWhileHeating ; CM20 défaut d'humidité hors refroidissement → HumidityIsNoFaultWhileHeating
- CM14 changement de statut jamais publié → AFaultStartingAloneIsPublished
- CM15 verrou de la sortie froid ignoré, CM16 `output` toujours `heat` → ALockOutOfTheCoolingOutputSaysCool
- CM19 verrou publié après son échéance → ALockOutPastItsDeadlineIsNotPublished
- CM21 `impl` non mis en minuscules → TheImplPublishedIsTheFactoryKey, GetHomeCarriesTheStatusFields

## Reste ouvert
- Divergence d'ordre d'affichage entre clients : le web met `fault` avant `off`, mobile et RemoteUI `off` avant `fault`. Sans effet tant que le serveur n'envoie pas de `fault` en mode off.
- `connected` : seul mobile exige un vrai booléen (le serveur en envoie un).
- Le `family` publié reste tel qu'écrit dans io.xml (non canonicalisé).
- Revue : une sonde de thermostat déconnectée publie `connected: false`, les clients affichent « Hors ligne » et bloquent mode et consigne alors que Calaos répond ; après un redémarrage, `sensor_missing` (« Sonde absente ») s'affiche jusqu'à la première lecture (jusqu'à 1 h pour une sonde Zigbee horaire). À trancher.
- Revue : 200 bascules connecté/déconnecté en 1 s donnent 13 publications (debounce 100 ms).
