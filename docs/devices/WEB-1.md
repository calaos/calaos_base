# WEB-1 — Décodage de l'objet device (calaos-web-app)

## Objectif
Décoder l'objet `device` des items climate (spec §5.3) sans jamais casser un lot.

## Livré
PR calaos/calaos-web-app#26.
- Types `DeviceInfo`/`DeviceCapabilities`/`DeviceState`, guard `toDeviceInfo` qui ne lève jamais :
  seules les sous-parties invalides sont retirées, nombres JSON stricts (chaîne numérique tolérée
  avec un warn), `target_temp` refusé si `step <= 0` ou `min > max`, warn quand un device présent
  est rejeté.
- `device?` sur les items et sur `io_changed` ; le store **remplace** l'objet entier (§5.3).
- Mock-server : un proxy climate et un thermostat chauffage seul, leurs IOs liés en
  `visible="false"`, `set_state` en JSON partiel ou mode simple, `success:false` si tout est
  rejeté, objet `device` entier à chaque `io_changed`.
- `climate` n'entre pas encore dans `GUI_TYPES` (WEB-2).

## Mesuré
1158 tests unitaires, 76 e2e, CI verte. Revue en deux tours (bloquant : fusion au lieu de
remplacement, corrigé).

## Contre-mutation
| Mutant | Test rouge |
|---|---|
| CM0 | aucun |
| fusion au lieu de remplacement (revue) | replaces the device object whole |
| `true` accepté comme nombre (revue) | rejects coerced non-numbers, bad bounds, and warns on dropped objects |
| filtre `ios` retiré / `target_temp` incomplet accepté | drops only the malformed parts |
| `device` non copié dans le message | decodes the device object of an io_changed and ignores a malformed one |
| bornage du mock retiré | is best effort… clamps to bounds |

## Reste ouvert
Rendu (WEB-2). Le mock ne propage pas les changements aux IOs liés.
