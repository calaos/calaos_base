# FIX-INST-10 — calaos_installer toujours en thème clair

## Objectif
Retour utilisateur du 15/10 : sous Windows en thème sombre, Fusion et QtQuick Controls suivaient le schéma système et la moitié de l'éditeur Remote UI devenait illisible. Décision de l'utilisateur : l'installer s'affiche toujours en clair (Windows, macOS, Linux). Feuille de style inchangée.

## Livré
PR calaos/calaos_installer#73.
- `AppTheme::prepareEnvironment()` avant `QApplication` (Windows : `QT_QPA_PLATFORM=windows:darkmode=0` si non défini, pour la barre de titre, les menus et les dialogues natifs), puis `AppTheme::forceLight()` après `setStyle("fusion")` : `QStyleHints::setColorScheme(Light)` dès Qt 6.8, palette claire explicite sinon.
- Test `tests/tst_apptheme` (système sombre simulé par GTK `Adwaita:dark`), captures avant/après de l'éditeur Remote UI. CI : gtk3, xvfb.

## Mesuré
Test rouge avant le fix (2 échecs), vert après. 17 suites offscreen vertes. CI : build, Windows, macOS, Flatpak verts. Capture « après » vérifiée par l'orchestrateur : éditeur entièrement clair et lisible.

## Contre-mutation
CM0 : 0 rouge. M1 (appel supprimé) et M2 (Dark) : paletteStaysLightUnderDarkScheme, widgetsStayLightUnderDarkScheme. M3 (Window sombre) et M4 (WindowText clair) : lightPaletteIsLight.

## Reste ouvert
- Rendu natif sous Windows réel (barre de titre, menus, dialogues) à confirmer par l'utilisateur.
- Pas de test du repli Qt < 6.8 ni de l'appel dans main.cpp.
