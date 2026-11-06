# WEB-4 — Libellés des défauts de sonde de thermostat (calaos-web-app)

## Objectif
Afficher les défauts publiés par le thermostat depuis CLIM-7 (décision 20) avec leur propre libellé, au lieu du générique « Défaut ».

## Livré
PR calaos/calaos-web-app#30.
- `climate.fault.sensor_waiting` (« En attente de la sonde » / « Waiting for the sensor ») et `climate.fault.sensor_disconnected` (« Sonde déconnectée » / « Sensor disconnected »), en.json et fr.json. Aucun changement de code : le statut passait déjà par `climate.fault.<fault>`.
- Test que le thermostat en défaut de sonde reste réglable (−/+ et pastille de mode actifs) ; seuls `connected:false` et un mode inconnu les désactivent.

## Mesuré
Vitest : 1385 verts (3 nouveaux dans ClimateIo). Lint et typecheck propres. CI verte. Diff vérifié par l'orchestrateur (traductions et tests seulement).

## Contre-mutation
CM0 : 0 rouge.

| Mutant | Rouge |
|---|---|
| `sensor_waiting` absent de en.json | « names the faults the server can raise… » ; « keeps a thermostat with a disconnected or waiting sensor adjustable » (2) |
| `sensor_disconnected` absent de en.json | les mêmes (2) |
| `sensor_waiting` ou `sensor_disconnected` absent de fr.json | « names the thermostat sensor faults in French » (1 pour chacun) |

## Reste ouvert
- Pas de thermostat en `sensor_waiting` ni `sensor_disconnected` dans les fixtures du mock.
- Un défaut inconnu du client retombe sur « Défaut ».
