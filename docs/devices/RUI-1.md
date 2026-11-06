# RUI-1 — Les panneaux RemoteUI reçoivent l'objet device (calaos_base)

## Objectif
Côté serveur du panneau (spec §9.3) : le panneau reçoit l'objet `device` et les IOs d'un device.

## Livré
PR calaos/calaos_base#186.
- `extractReferencedIOs()` étend un widget device au device, à ses IOs possédés et aux IOs de ses
  liens (`io` et `command_io`) ; recalculé à chaque connexion (`sendInitialIOStates`,
  `sendConfigUpdate`), car l'écran peut être chargé avant le device.
- `remote_ui_io_states` (forme objet) : une entrée device devient `{state, gui_type, device}` ; les
  autres IOs gardent leur chaîne brute (octets inchangés, `JsonApiStateWireBytes_test` vert) ;
  `get_state` sur 5454 non touché.
- `io_changed` d'un device atteint le panneau inchangé (testé sur le handler RemoteUI).

## Mesuré
`RemoteUIDevice_test` 7 cas, stable ; format vérifié contre le parseur du firmware (lots mixtes
chaînes/objets, drapeaux visible/enabled) ; CI verte. Revue approuvée, 9 mutants rejoués par le
relecteur sur le code final, tous tués.

## Contre-mutation
Sans IOs possédés, sans `io` des liens, sans `command_io`, sans rafraîchissement à la connexion
(×2), sans enveloppe objet, `gui_type` vide, clé `device` absente, `state` mal formé : tous rouges.

## Reste ouvert
- Un device supprimé reste affiché jusqu'au **reboot du panneau** (le firmware ne vide jamais ses
  états) → RUI-3 : vider `ioStates` à la réception de `config_update`.
- SEC-1 : l'ensemble élargi contient `command_io` et des capteurs d'autres pièces ; séparer
  lisible / écrivable.
