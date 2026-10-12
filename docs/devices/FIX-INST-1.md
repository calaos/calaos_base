# FIX-INST-1 — Défauts révélés par OUT-2 (calaos_installer)

## Objectif
Supprimer trois défauts silencieux de la lecture d'un projet (id vide, erreur de lecture d'io.xml
ignorée, `hits` indéfini) et durcir la suite Qt Test.

## Livré
PR calaos/calaos_installer#53 (paires `test:`/`fix:`).
- `get_input("")`/`get_output("")` ne résolvent plus rien. Un id absent est généré, un `id=""`
  explicite est conservé, comme sur le serveur.
- `loadIOsFromFile` renvoie `!error()`, et `MainWindow::Load` affiche l'erreur sans bloquer
  l'ouverture.
- `hits` vaut 0 par défaut (serveur : `as_int(0)`).
- Suite : `cleanup()` échoue sur toute modale inattendue, valeur exacte au lieu de `>= 1`, test de
  `months=""`, image Arch épinglée (`base-devel-20261004.0.606936`).

## Mesuré
- Rouge sur l'ancien code : 4/66 (id vide), 2/67 (chargement), 1/68 (`hits`).
- Revue : correctifs retirés et nouveaux tests gardés, 7 tests rouges ; 68/68 avec les correctifs.
  Aucun appelant ne comptait sur l'id vide.
- CI : 4 jobs verts.

## Reste ouvert
- Le retour de `loadRulesFromFile` est ignoré par `MainWindow::Load`.
- Une référence de règle `id=""` est abandonnée sans message : la verser dans `missingIOReport()`.
- La boîte d'erreur de chargement n'a pas de test et n'est pas traduite.
- `pacman -Syu` fait toujours bouger Qt en CI.
