# MOB-1 — Conserver et mettre à jour l'objet device (calaos_mobile)

## Objectif
Exposer l'objet `device` (spec §5.3) des items de `get_home` et le tenir à jour par `io_changed`,
sans casser les autres IOs ni les trames malformées. Préalable de MOB-2.

## Livré
PR calaos/calaos_mobile#22.
- Objet `device` conservé dans `ioData` et exposé à QML : `hasDevice`, `device`,
  `deviceCapabilities`, `deviceState`, signal `deviceChange`.
- Le décodeur émet `DeviceChange` pour un `io_changed` (l'objet était auparavant aplati en chaîne
  vide) ; le modèle **remplace** l'objet en bloc (§5.3) ; `load()` notifie quand le device
  apparaît, disparaît ou change.
- Règle de validité unique `Common::isUsableDevice` (objet non vide) au décodeur, au chargement et
  dans le slot. Warning quand une autre map/liste serait aplatie.
- `gui_type` « climate » chargé en `Unkown` sans crash (widget : MOB-2).
- Note pour MOB-2 : `DeviceChange` est dispatché avant l'événement `state` de la même trame.

## Mesuré
`make check` vert (tst_calaoseventdecoder 63, tst_iobase 22), variantes desktop et mobile
compilées, CI 3/3. Revue en deux tours (bloquants : fusion, pas de notification au chargement,
`{}` traité différemment), corrigés et vérifiés par sondes.

## Contre-mutation
| Mutant | Tests rouges |
|---|---|
| CM0 | aucun |
| décodeur sans branche device | v3IoChangedCarriesDeviceObject, v3IoChangedMalformedDeviceIsDropped ×4 |
| fusion au lieu de remplacement | deviceChangeReplacesWholesaleAndNotifies |
| pas d'emit dans `load()` | loadNotifiesWhenDeviceAppearsDisappearsOrDiffers |
| `isUsableDevice` accepte `{}` | v3IoChangedMalformedDeviceIsDropped(empty object), emptyDeviceWarnsOnceNotOnClone |
| warning d'aplatissement supprimé | v3IoChangedWarnsOnFlattenedStructure |
| garde d'id supprimée | deviceChangeForAnotherIoIsIgnored |
| `clear()` supprimé dans `load()` | reloadWithoutDeviceDropsThePreviousOne |

## Reste ouvert
- Le `case DeviceChange` de `dispatchEvents` n'est couvert que par la compilation.
- `src/Common.h:134-138` : `isUsableDevice` sépare le commentaire des `to*Safe` de `toIntSafe` ;
  à remettre en place dans MOB-2.
