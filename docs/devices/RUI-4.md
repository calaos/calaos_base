# RUI-4 — Petits widgets climate et vue plein écran (calaos_remote_ui)

## Objectif
Petits widgets climate (1×1, 2×1, 1×2) en lecture seule, qui ouvrent une vue plein écran de réglage ([UI_CLIMATE.md](UI_CLIMATE.md), round5/final/remote pour les widgets, round4/final/remote pour la vue et les popups). Polices et tailles déjà compilées seulement.

## Livré
PR calaos/calaos_remote_ui#6.
- `ClimateSmallWidget` : 1×1 (pièce, « › consigne », icône et mot de statut, nom sur 2 lignes) ; 2×1 et 1×2 (mini-arc avec la consigne, nom, statut, pièce, jauge d'attente du relais). Aucun −/+. Un tap ouvre la vue. Les tailles ≥ 2×2 gardent `ClimateWidget` (RUI-5), dont les mises en page 1×1/2×1/1×2 sont retirées.
- `ClimateDetail` sur la couche haute (sous le toast, l'OTA et l'économiseur) : cadran `lv_arc` 300°, « − 22° + », deux paires et butée `min_range` en auto, pastille de mode, « Plus de réglages » ; popups de mode et de réglages (ventilation, oscillation, humidité selon les capacités). Une commande par geste (pause de 700 ms), les choix de popup partent aussitôt, refus et écho comme RUI-3, édition envoyée à la fermeture.
- `climate_present` (sans LVGL) : statut, couleurs, consigne, butée, boutons autorisés, géométrie du cadran, décompte local de l'attente, mesures. Champs CLIM-6 (humidity, connected, fault, lockout) lus de façon tolérante.
- Icônes PNG recolorées (sources SVG dans `main/images/src`, `scripts/render-climate-icons.sh`), chevrons et croix dessinés ; couleurs gris, ambre et fond « chauffe » ajoutées au thème.

## Mesuré
216 + 77 tests hôte (77 `Climate*` propres sous ASan) ; ESP32 waveshare-86-panel +63 Ko (+0,72 %), 43 % libres ; captures du simulateur SDL en 720×720 et 800×480 pour chaque taille et chaque état ; tap puis `set_state` vérifié dans le simulateur.

## Contre-mutation
CM0 : 0 rouge. 23 mutants tués :

| Mutant | Rouge |
|---|---|
| hors ligne ignoré | 9 (ClimatePresentStatus.OfflineWinsOverEverythingElse…) |
| butée stricte | 2 (TheInnerButtonsAreRefusedAtTheMinimumGap ×2) |
| « + » bas toujours permis | 2 |
| jauge suit la dernière valeur | ClimatePresentLockout.TheGaugeFillsFromTheFirstValueSeen |
| cadran à 90° | ClimatePresentDial.TheArcRunsThreeHundredDegreesOpenAtTheBottom |
| décimale non rognée | 13 |
| minutes arrondies vers le bas | 4 |
| popup de mode reste ouvert | ClimateDetailView.ChoosingAModeSendsItAtOnceAndClosesThePopup |
| fermer perd l'édition | ClimateDetailView.ClosingSendsWhatIsPendingAtOnce |
| mode attend la pause | 2 |
| refus ignoré | ClimateDetailView.ARefusalShowsTheServerValueAtOnce |
| vue au-dessus du toast | ClimateSmall.TheViewStaysUnderWhatMustAlwaysBeOnTop |
| humidité jamais listée | ClimateDetailView.TheHumidityFollowsTheServerWhileThePopupIsOpen |
| ouvre sans device | ClimateSmall.WithoutADeviceThereIsNothingToOpen |
| chauffe allume en cyan | ClimateSmall.TheWidgetIsLitInTheColourOfWhatItDoes |
| statut non ajusté | ClimateSmall.EveryFixtureFitsEveryCell |
| attente 1×2 non raccourcie | ClimateSmall.ATallCellShortensAWaitThatWouldTakeThreeLines |
| plages jamais empilées | 2 |
| lockout ignoré | 6 |
| `connected:"false"` ignoré | 1 |
| device hors ligne accepte un tap | ClimateDetailView.AStrayTapOnAnUnreachableDeviceChangesNothing |
| bindings du popup vidés avec le corps (revue) | APopupSurvivesTheBodyBeingRebuiltUnderIt, APopupSurvivesTheDeviceGoing (heap-use-after-free sous ASan) |
| bindings jamais libérés à la fermeture du popup | OpeningPopupsOverAndOverDoesNotGrowTheBindings |

## Écarts avec la maquette
Décimale en point comme le reste du panneau ; tiret ASCII « 21-23° » (pas de demi-cadratin dans les polices) ; consigne du mini-arc en Bold 32 et pièce en Light 26 (tailles compilées) ; jauge d'attente relative à la première valeur vue ; pas de « depuis 2 h 10 » ; défauts en ambre ; sans `reports_action`, le statut est le mode ; en auto avec butée, l'arc se réduit et le texte de butée passe sous la ligne.

## Reste ouvert
- Pas de fermeture automatique de la vue sur inactivité ; un `config_update` ferme la vue (l'édition est envoyée).
- Textes en français pour les petites tailles, widget ≥ 2×2 en anglais jusqu'à RUI-5.
- Revue : use-after-free des popups après reconstruction du corps de la vue, corrigé (bindings du popup séparés, copie dans `eventCb`).
