# CLIM-1 — `ClimateBase` (calaos_base)

## Objectif
Base abstraite de la famille `climate` (spec §6) : enums mode/action, capacités dérivées des liens,
objet d'état, validation des commandes, plage low/high contre `min_range`.

## Livré
PR calaos/calaos_base#187 (squash).
- `ClimateMode`/`ClimateAction` en chaînes ESPHome (`fan_only`, `heating`…).
- Capacités présentes si et seulement si leur lien résout (§6.3) : `modes` (restreint par le param
  `modes`), `target_temp` (bornes de l'IO écrit, défauts 7/35/0.5/°C ; sans lien `target_temp`, la
  plage fournit les bornes ; `min_range` publié quand la plage existe), `target_temp_range` (low ET
  high), `reports_action`, `fan_modes`/`swing_modes` (params), `remote_temp` (lien ET source).
- État en types JSON réels ; un nombre n'y entre qu'une fois reçu (nouveau `IOBase::hasValue()`,
  marqué à chaque `EmitSignalIO()` et à la restauration du cache) ; mode, action, fan, swing
  inconnus omis ; scalaire = mode (`""` = inconnu, documenté).
- Hooks d'impl : `supportedModes()`, `currentMode()`, `writeMode()`.
- `applyCommand()` : low/high validés ensemble (paire inversée ou plus étroite que `min_range`
  refusée entière ; écritures ordonnées pour que chaque paire intermédiaire reste valide ; plage
  partielle refusée tant que l'autre borne est inconnue) ; refus par l'IO lié journalisés avec la
  raison ; erreurs de configuration journalisées au chargement.

## Mesuré
`make check` 185 tests ; `ClimateBase_test` 24 cas ; porte de couverture verte ; revue en trois
tours (raisons de refus, valeur restaurée du cache non marquée) : bloquants corrigés.

## Contre-mutation
27 mutants tués, dont : pas de contrôle `min_range`, pas de tolérance, mauvais ordre d'écriture,
low écrit malgré le refus, mode hors capacités, bornes lues sur le mauvais IO, `min_range` non
publié ou publié sans plage, raison vide, plage refusée sans log, valeur jamais reçue publiée,
fan non filtré, borne inconnue utilisée, `EmitSignalIO` ou restauration non marqués.

## Reste ouvert
- Une première lecture égale au défaut, sans cache, reste inconnue jusqu'au premier changement.
- CLIM-3 : un `Internal` sans `save="true"` n'est connu qu'après sa première écriture.
- CLIM-2 : `heat_cool` → `auto` ; proxy à mode en lecture seule (exiger `command_io` ou IO
  inscriptible) ; valeurs relues périmées entre deux commandes rapprochées.
