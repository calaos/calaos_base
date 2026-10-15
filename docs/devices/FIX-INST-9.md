# FIX-INST-9 — Plantage à l'ouverture de l'éditeur Remote UI (Linux)

## Objectif
Retour utilisateur du 15/10 : SIGSEGV dans `TimezoneHelper::filteredIanaTimezones()` à l'ouverture de l'éditeur Remote UI, sur Fedora 44 (Qt 6.11.2). Le build Windows ne plantait pas.

## Livré
PR calaos/calaos_installer#72.
- Cause : un build incrémental local mélangeait des objets compilés en avril contre Qt 6.10 (GCC 15, Fedora 43), dont `TimezoneHelper.o`, et des objets compilés contre Qt 6.11. qmake ne suit pas les en-têtes de Qt, et Qt 6.11 a changé la structure de `QPodArrayOps`/`QMovableArrayOps` sans changer les noms de symboles. Reproduit : objets 6.11 + seul l'ancien `TimezoneHelper.o` → SIGSEGV. Code sain : build propre et ASan+UBSan OK. Windows et Flatpak partent d'un dossier vide.
- `.qmake.conf` range obj/moc/uic/rcc dans `build-qt<version>/` (appli, tests, machine_creator) ; `qmake/calaos_former_layout.prf` refuse un dossier de build à l'ancien rangement, avec un message clair.
- `IODoc::rootObject()` ne modifie plus le `QJsonObject` qu'il parcourt (seule occurrence du schéma trouvée).
- Suites `tst_builddirs` (rouge avant le fix) et `tst_timezonehelper` (liste triée, sans doublon, zones valides, éditeur hors écran).

## Mesuré
18 suites, 608 tests verts. CI : build, tests, Flatpak, Windows, macOS verts.

## Contre-mutation
CM0 : 0 rouge. M1 sans OBJECTS_DIR, M2 dossier sans version, M3 sans MOC_DIR → intermediatesArePerQtVersion. M4 sans calaos_former_layout, M5 sans *.moc → formerLayoutIsRefused. M7 doublon → listIsSortedWithoutDuplicates. M8 sans filtre → listKeepsOnlyGeographicZonesAndUtc. M9 sans UTC → listIsNotEmpty. M10 IODoc → tst_iodoccatalogue. Équivalents : sort() retiré (Qt renvoie une liste triée), un seul des deux filtres retiré (redondants).

## Reste ouvert
- `mapIter` de FormDali périmé si on relance la recherche pendant un chargement de groupes.
- ~178 alias tzdata dans la liste des fuseaux (Asia/Calcutta et Asia/Kolkata).
- La clé de dossier ne tient compte que de la version de Qt, pas du compilateur.
