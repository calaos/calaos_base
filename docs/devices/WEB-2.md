# WEB-2 — Composant ClimateIo (calaos-web-app)

## Objectif
Afficher et piloter tout device climate depuis `device.capabilities` et `device.state`, sans
branche par `impl`.

## Livré
PR calaos/calaos-web-app#28.
- `climate` dans `GUI_TYPES`/`IO_COMPONENTS` ; `ClimateIo.vue` : boutons de mode, consigne bornée
  (slider + −/+), plage à deux poignées (`RangeSlider`) avec `min_range`, température courante
  (« — »), selects fan/swing, badge d'action dans l'en-tête ; off grise, `fan_only` masque la
  consigne, mode inconnu affiché ; un device à plage seule montre sa plage dans tous les modes, une
  plage à demi connue n'est pas éditable.
- Commandes via `sendSetState` : mode simple, JSON partiel fusionné et debouncé (une commande par
  geste) ; le device envoie un `msg_id` et un refus (`success:"false"`) efface aussitôt la valeur
  en attente ; la poignée non tenue suit le serveur pendant un glisser (pas d'écrasement entre
  clients) ; brouillon purgé par champ.
- Mock-server : six fixtures (toutes les combinaisons de capacités), refus avec republication.

## Mesuré
1293 tests unitaires, 98 e2e (desktop + mobile, dont un vrai glisser souris et un refus forcé),
CI verte. Revue en trois tours dans un navigateur réel : glisser de plage qui n'envoyait rien,
retour lent après refus (5 s → 6 ms), écrasement d'un autre client — corrigés. Captures dans
`.wt/review-web-2-shots`.

## Contre-mutation
19 mutants tués (flush avant le mode, `fan_only`, `min_range`, mode envoyé en JSON, debounce,
brouillon, off, mode absent, commit au relâchement, suivi serveur de la poignée, plage seule, plage
à demi connue, brouillon purgé à tort, annulation de mode, republication du mock, verdict ignoré,
drag réinjecté…).

## Reste ouvert
Une commande acceptée en partie (`success:"true"`) peut garder le brouillon du champ refusé jusqu'à
5 s ; icône `mdi:thermostat` faute d'artwork climat Calaos.
