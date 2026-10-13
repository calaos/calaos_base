# SEC-1 — Un panneau RemoteUI ne lit et n'écrit que ce que sa configuration référence (calaos_base)

## Objectif
Un panneau authentifié par HMAC était servi comme une session complète : il recevait tous les
`io_changed`, pouvait écrire n'importe quel IO et atteignait `login`, `get_home`, `set_param`…

## Livré
PR calaos/calaos_base#190.
- Ensemble lisible (expansion RUI-1) : seuls les événements d'IO de cet ensemble atteignent le
  panneau (crochet `forwardsEvent()` de `JsonApiHandlerWS`, transparent pour les autres clients).
- Ensemble écrivable : les `io_id` des widgets seuls ; un device se commande par son id, jamais par
  ses relais, IOs possédés ou `command_io`.
- Seuls `set_state`, `remote_ui_get_config`, `remote_ui_relay_state` (ce qu'envoie le firmware)
  sont servis ; le reste reçoit `{"error":"forbidden"}`, journalisé sous `logTag`.
- Ensembles recalculés au premier usage après la connexion, à chaque config_update / io_states et
  après tout `io_added` / `io_deleted`.

## Mesuré
`RemoteUIScopeTest` 14 cas (10 rouges sur master) ; couverture 74/74 ; ASan vert ; CI verte. Revue
avec un banc réel : serveur ASan de la PR + simulateur du firmware (linux-dev) sur un réseau docker
privé, widgets lumière/scénario/température : connexion, états et `io_changed` reçus, IO non
référencé jamais livré ; commandes autorisées appliquées, `set_state` hors périmètre, `get_home` et
`login` refusés ; arrêt du serveur panneau connecté sans rapport ASan. Toutes les voies du firmware
(HMAC au handshake, ping WS, provisioning et OTA en HTTP, envois serveur directs) hors du filtre.

## Contre-mutation
8 mutants tués (écrivable = lisible, set_state non vérifié, filtre retiré, événements non-IO
transmis, pas de recalcul sur add/delete ni à la connexion, autres actions déléguées, écrivable
pris après l'expansion).

## Reste ouvert
Widget device non exercé sur le simulateur (RUI-3) ; événements audio/timerange coupés (aucun widget
ne les utilise aujourd'hui) ; références de lignes de `docs/07_remoteui.md` décalées.
