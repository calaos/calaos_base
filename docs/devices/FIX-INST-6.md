# FIX-INST-6 — Ids de caméra orphelins, règle inerte, autoscroll (calaos_installer)

## Objectif
Qu'une caméra supprimée ne puisse pas être reprise en silence par une action, que l'état « script
sans déclencheur » survive à la sauvegarde, et que le glisser-déposer n'ait plus de timer qui traîne.

## Livré
PR calaos/calaos_installer#63.
- Ids de caméra des actions mail/push (`attachment`) et touchscreen `view_camera` (`camera`)
  réservés à la suppression de la caméra (seuls endroits où l'installateur écrit un id de caméra) ;
  listés dans le dialogue de suppression (`OrphanVariable.attachment`).
- Aller-retour io.xml/rules.xml d'une règle script devenue sans déclencheur : Lua et action gardés,
  toujours inerte, non reprochée ensuite.
- `HomeTreeWidget::dropEvent` appelle `stopAutoScroll()`.

## Mesuré
307 tests verts (ASan + UBSan côté revue) ; CI 4/4. Le test d'autoscroll, d'abord peu fiable (faux
vert sur l'ancien code), positionne désormais le curseur au bord et vérifie que l'autoscroll tourne
au moment de la dépose : rouge 10/10 sans le correctif, vert 30/30 avec.

## Contre-mutation
6 mutants tués (stopAutoScroll retiré, réservation des pièces jointes retirée, action push ignorée,
impact sans pièces jointes, branche touchscreen désactivée).

## Reste ouvert
Chaîne « attached camera » à traduire au prochain lupdate.
