# FIX-RUI-1 — Cinq tests hôte rouges sous ASan (calaos_remote_ui)

## Objectif
Corriger ce que la revue de RUI-4 avait relevé comme une course sur le vecteur d'événements du worker `AppDispatcher` : 5 tests `host_tests` rouges sous ASan (ConfigUpdate.ScreensaverNumbers…, 4 IoChanged.*).

## Livré
PR calaos/calaos_remote_ui#8.
- Diagnostic : ce n'est pas une course du dispatcher. Les tests lisaient un payload par un pointeur vers un vecteur temporaire (`configOf(receive(...))`, `single(ioChanged(...))`), libéré à la fin de l'instruction (config_update_test.cpp:49-50, io_changed_test.cpp:21-22, 43, 102). TSan sur tout `host_tests` ne montre aucun data race dans `AppDispatcher`.
- Les 5 sites gardent les événements dans une variable.
- Test de stress `AppDispatcherStress` (4 producteurs × 300 événements pendant que 3 threads s'abonnent : livraison complète et ordre par producteur), propre sous TSan.
- ESP32 : `dispatch()` et la suppression de la file de `stopWorkerTask()` partagent un mutex, un événement envoyé pendant l'arrêt est abandonné (il fuyait), handles de file et de tâche initialisés à null. Lu dans le code, non reproduit sur l'hôte.
- CI : job `host-tests-sanitizers` (ASan et TSan sur `host_tests`, ASan sur `widget_tests`), documenté dans DOC-CI.md.

## Mesuré
`host_tests` 219 sous ASan et sous TSan, 0 avertissement ; 219 + 98 en build normal ; build ESP32 waveshare-86-panel OK (44 % libres) ; CI verte, jobs sanitizers compris.

## Contre-mutation
Le test de stress est un garde-fou sans bug de dispatcher à tuer. Trois mutants du dispatcher, tous détectés par TSan : push sans verrou de file (41 avertissements), `subscribe` sans verrou (11), copie des abonnés sans verrou dans le worker (12). Le défaut corrigé (pointeur vers un temporaire) n'est attrapé que sous ASan/TSan, d'où le job de CI.

## Reste ouvert
- Rien n'empêche à la compilation un pointeur vers un temporaire dans les tests : seul le job sanitizer le détecte.
- La correction ESP32 n'est exercée que par la compilation en CI, pas par un test.
- Préexistant : si le worker ne s'arrête pas en 1 s, la queue est supprimée alors qu'il peut encore faire un `xQueueReceive` dessus.
- TSan sur runners ubuntu-24.04 : en cas d'« unexpected memory mapping », `sudo sysctl vm.mmap_rnd_bits=28` avant l'exécution.
