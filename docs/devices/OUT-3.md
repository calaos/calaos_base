# OUT-3 — Tests hôte et CI de PR (calaos_remote_ui)

## Objectif
Réparer les runs rouges de `build-firmware` sur main, ajouter des tests hôte de caractérisation
(`PagesConfig::fromJson`, `remote_ui_io_states`, `io_changed`, `AppStore`) et un workflow
`pull_request`.

## Livré
PR calaos/calaos_remote_ui#3.
- `luckfox-86-panel` sortie de la matrice (`enabled:false`) dans `build-firmware` et
  `build-docker-images`.
- Cible CMake `BUILD_HOST_TESTS` (GoogleTest 1.17.0 récupéré, épinglé par hash) : vraies sources,
  HAL factice, sonde d'événements sur le vrai `AppDispatcher`. Seul changement de production : une
  déclaration `friend`.
- `pull-request.yml` : `linux-dev` + ctest, et un build ESP32 `waveshare-86-panel`. `DOC-CI.md` à
  jour.

## Mesuré
- 91 tests verts : PagesConfig 20, io_states 20, config_update 11, io_changed 14, AppStore 26.
- CI de PR : host-tests 2 min, ESP32 8 min. 200 exécutions sans échec.
- Contre-mutations : `visible` tolérant → 2 rouges ; fusion de `visible` dans le store → 1 rouge ;
  fallback de grille → 1 rouge.

## Constats
- Le rouge de main venait de `luckfox-86-panel` seule (ni Dockerfile, ni image, ni `bin_path`,
  sous-module absent). Les rouges de juillet venaient du tag mouvant `release-v5.5`, déjà corrigés
  par l'épinglage v5.5.5.
- Bugs épinglés pour RUI-2 (tests `*KnownBugs*`) :
  - `visible`/`enabled` en chaîne perd tout le lot `remote_ui_io_states` ;
  - un `state` non-chaîne perd l'`io_changed` ;
  - un booléen dans `io_items` perd toute la mise à jour de config ;
  - un champ mal typé jette toutes les pages, et `stoi` n'est pas rattrapé ;
  - `AppStore` ne fusionne pas `visible`/`enabled`/`brightness` et ne notifie que sur un
    changement de `state` ;
  - une valeur numérique en forme objet efface le `state`.

## Reste ouvert
- Réparer ou retirer `luckfox-86-panel`.
- Options du dispatch de `build-firmware` ; ccache en CI.
- L'image ghcr `:latest` du job ESP32 peut être en retard sur un Dockerfile modifié par la PR.
- `AppStore` sans reset (`--gtest_repeat`) ; pas de test du chemin `connect()` réel.
