# RUI-2 — Protocole du firmware (calaos_remote_ui)

## Objectif
Corriger les 17 défauts épinglés par OUT-3 (`*KnownBugs*`) et porter l'objet `device` (spec §5.3,
§9.3) dans le firmware : IoState, parsing, store, commande JSON.

## Livré
PR calaos/calaos_remote_ui#4.
- `protocol_json.h` : lecteurs tolérants ; un champ mal typé ne perd que son champ, son entrée ou
  son widget (flags bool ou chaîne, états bool/nombre, coordonnées non numériques sans exception,
  grille en chaîne, `io_items` et scalaires de config).
- Présence par champ (`visible_set`, `enabled_set`, `state_set`) : une mise à jour partielle ne
  touche que ce qu'elle porte ; un lot complet après reconnexion réécrit l'état de chaque IO.
- Ids : en forme objet la clé de la map fait foi, ids vides refusés, store indexé par la clé.
- `AppStore` fusionne visible/enabled/brightness et notifie sur tout changement ; un
  config_update conserve l'état stocké des IOs connus.
- `IoState::device` (family, impl, capabilities/state/ios en JSON sérialisé) parsé dans
  `remote_ui_io_states`, `io_state`, `io_changed` ; un nouveau device remplace le stocké.
- `sendDeviceCommand` : `set_state` dont la valeur est la chaîne JSON (contrat serveur vérifié).
- Le widget n'enregistre l'état qu'après son verrou d'affichage ; params flottants conservés.

## Mesuré
136 tests hôte (91 avant), builds linux-dev et ESP32, CI verte. Revue en deux tours (bloquants :
marqueur partiel commun aux deux flags, rangement par id interne), corrigés et vérifiés par sondes.

## Contre-mutation
22 mutants tués, dont : flag chaîne ignoré (5 rouges), fusion des flags retirée (4), notification
sur state seul (4), device jeté par le store (3), nombres non scalaires (6), égalité sans device
(3), io_changed sans device (6), `visible_set` toujours vrai (2), store indexé par `update.id`
(BatchStoresUnderTheMapKey), état forcé dans `io_items` (ConfigUpdateKeepsTheStoredStateOfAKnownIo),
clé vide acceptée (EmptyKeyIsSkipped).

## Reste ouvert
- Widget Climate : RUI-3.
- Une mise à jour complète sans `device` n'efface jamais le device stocké (acceptable tant qu'une
  reconfiguration implique un redémarrage ; à reprendre avec RUI-1).
- Verrou d'affichage de `CalaosWidget` non testable en hôte ; indentation à
  `calaos_websocket_manager.cpp:925`.
