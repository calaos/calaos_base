# CORE-4 — Commandes et publication d'état (calaos_base)

## Objectif
Le device comme IO (spec §5) : `set_value(string)` en mode simple ou JSON partiel, objet `device`
dans `get_home` et dans `io_changed`, `gui_type` = famille.

## Livré
PR calaos/calaos_base#185.
- `Device::set_value()` : JSON partiel si la chaîne commence par `{`, sinon mode. Analyse sans
  exception (`Json::parse(..., false)` : `1e400` faisait tomber le serveur) ; valeur brute et noms de
  champs hors `[a-z0-9_]` journalisés seulement via `Utils::logTag()`.
- Application : `mode` d'abord, puis les autres champs ; hook virtuel `applyCommand()` (par défaut
  champ par champ, best effort) pour qu'une famille valide ensemble des champs liés ; helpers
  `enumField()`/`numberField()` (arrondi au pas puis bornage, sans résidu binaire). Retour `true` si
  au moins un champ est appliqué ; toute commande publie un `io_changed`, même refusée.
- `deviceObject()` = {family, impl, capabilities, state, ios} (objet entier, nombres JSON) dans
  `get_home`/`get_io` ; abonnement `IOChangeHub` aux IOs liés ; publication regroupée sur 100 ms
  (fenêtre non réarmée : au plus 10 publications/s/device) ; membres JSON typés dans `CalaosEvent`.
- `lastScalar` initialisé à la résolution des liens (un mode restauré ne déclenche pas les règles) ;
  `stopListening()` protégé pour les destructeurs des sous-classes ; `get_value_string()` final via
  `scalarFromState()`.

## Mesuré
`make check` 182 tests (179 PASS, 3 SKIP) ; DeviceApi 19 cas ; ASan propre, y compris 18 entrées
hostiles (imbrications de 200 000 niveaux, chaînes de 20 Mo) ; porte de couverture : 0 ligne non
couverte ; format sur le fil conforme aux décodeurs mobile, web et RemoteUI. Revue en deux tours
(bloquant : plantage par un client authentifié), corrigé.

## Contre-mutation
18 mutants tués, dont : JSON traité comme un mode, pas de best effort, pas d'arrondi, pas de
bornage, publication immédiate, timer jamais libéré, pas d'objet dans `buildJsonIO`, `toJson()` sans
objets, `ios` avec liens non résolus, abonnements perdus, timer fuité (ASan), parse sans garde
`out_of_range`, mode non prioritaire, pas de publication sur refus, `lastScalar` non initialisé,
résidu binaire, `stopListening()` vide.

## Reste ouvert
- CLIM-1 : `target_temp_low`/`target_temp_high` doivent surcharger `applyCommand()` et être validés
  ensemble (`min_range`).
- RUI-1 : l'objet n'est pas dans `remote_ui_io_states` (le firmware le reçoit par `io_changed`).
- Lua reçoit un param `device=""` (bruit sans effet).
