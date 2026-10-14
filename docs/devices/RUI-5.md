# RUI-5 — Grands widgets climate (calaos_remote_ui)

## Objectif
Le climate complet dans les widgets 2×2 et plus, sans vue intermédiaire ([UI_CLIMATE.md](UI_CLIMATE.md), round5/final/remote). Une différence voulue par l'utilisateur : le titre en bas, la pastille de mode et ⋯ en haut.

## Livré
PR calaos/calaos_remote_ui#7.
- ClimateLargeWidget remplace ClimateWidget : pastille (mot court) et ⋯ en haut, cadran 300° et « − 22° + » au milieu (deux paires empilées et « Écart min. » dans le cadran en auto), titre défilant en bas ; widget allumé de la couleur de l'action.
- ClimateDetail se pose aussi dans un widget (widgetMetrics) : mêmes commandes, coalescence, refus, écho et popups qu'en vue plein écran (correctif des bindings de RUI-4 inclus). Seuls le mode et les réglages ouvrent des popups, sur la couche haute, supprimés avec le widget.
- Cadran ≥ 240 (simple) et ≥ 210 (auto) en 2×2 sur 720×720 et 800×480 ; boutons de 80 à 72 puis 64 avant le cadran ; nombre de 80 à 60 px si le widget est trop étroit.
- Les bindings d'un popup vivent autant que son voile (libérés à sa suppression réelle) : un bouton d'un popup reconstruit, masqué et en attente de suppression, peut recevoir un clic sans lire de mémoire libérée.
- Dans les petits widgets, un nom trop long pour ses lignes défile (`LV_LABEL_LONG_SCROLL_CIRCULAR`) au lieu de finir en points de suspension.
- Textes en français ; helpers anglais de l'ancien widget retirés. Les 1×1 et 2×1 en cellules de 180 px ne débordent plus.

## Mesuré
218 + 98 tests hôte (Climate aussi sous ASan) ; ESP32 waveshare-86-panel −11,7 Ko (−0,13 %), 44 % libres ; captures SDL 720×720 (dont 3×3 sur grille 4×4) et 800×480 parmi des widgets existants ; tap puis set_state vérifié.

## Contre-mutation
CM0 : 0 rouge. 18 mutants tués : titre en haut (ClimateLarge.ModeAndSettingsOnTop…, EveryFixtureFitsEveryCell), cadran sans taille minimale (4), paires non empilées (2), widget jamais allumé (TheWidgetIsLitInTheColourOfWhatItDoes, TheLightFollowsTheServer), note du cadran (AutoHasTwoPairs…), mot long sur la pastille (3), popup dans le widget (3), popup qui survit au widget (3), bindings de popup partagés (OpeningPopupsOverAndOverDoesNotGrowTheBindings ×2), nom de 180 px (ClimateSmall.EveryFixtureFitsEveryCell), tailles toutes petites (crash), degré toujours retiré (ANarrowWidgetShowsTheNumber…), pas de police 60 px (2), gros boutons en auto (ANarrowWidgetKeepsTheBigNumber…), ⋯ retiré (7), tap parasite hors ligne (2), bindings du popup libérés à la fermeture (AButtonOfARebuiltPopupStillHasItsBindingUntilItIsDeleted, ASan), nom sans défilement (ClimateSmall.ANameThatDoesNotFitScrollsLikeTheOthers).

## Reste ouvert
- Pas de disposition paysage pour les widgets larges (3×2, 2×3, 4×2) : une colonne, cadran plafonné à 340.
- Light 26 pour un 1×1 en cellule de 180 px ; décimale en point, tiret « - » et polices de remplacement ; jauge d'attente relative à la première valeur vue.
- Un refus du serveur efface le brouillon de toutes les instances d'un même device (décompte des refus par `io_id`).
