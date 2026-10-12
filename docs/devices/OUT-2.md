# OUT-2 — Tests et CI de PR (calaos_installer)

## Objectif
Premier filet automatisé pour calaos_installer : tests de caractérisation de
`IOXmlReader`/`IOXmlWriter` et `RuleXmlWriter`/`loadRulesFromFile`, et workflow `pull_request`
Linux. Prérequis d'INST-1.

## Livré
PR calaos/calaos_installer#51.
- `calaos_model.pri` : sources du modèle partagées entre l'app et les tests.
- `tests/tests.pro` + `tst_projectmanager` (Qt Test, build hors arbre).
- `.github/workflows/pr_ci.yml` : conteneur Arch, build de l'app + `make check` offscreen, sur
  `pull_request` et push master, docs ignorées.
- Sections de test d'`AGENTS.md` et du README.

## Mesuré
- 61 cas verts : 7 io.xml ×3 et 6 rules.xml ×3 issus de `tests/*/` (modèle identique après
  aller-retour, 2e écriture identique octet pour octet), plus 22 cas synthétiques.
- App : 221 objets, identiques à master. CI : Linux, Flatpak, Windows et macOS verts.
- Mutations (l'écrivain perd un attribut, `val_var` ignoré, mois non inversés) : tests rouges.

## Constats (figés par les tests, non corrigés)
- Élément inconnu dans `<room>` ou `<input>` : tronque tout le reste du fichier, autres pièces
  comprises. C'est le bug qu'INST-1 corrige.
- `loadIOsFromFile` renvoie `true` malgré une erreur du lecteur.
- `InPlageHoraire` sans `months` : tous les mois désactivés à l'écriture.
- Room sans `hits` : entier non initialisé.
- Id vide : lié à la 1re IO sans id (règle ré-aiguillée en silence, faux « already exists »).
- Perdus sans message : types inconnus de condition/action, condition output sans `<output>`,
  attribut camera hors `view_camera`, enfants de `remote_ui` hors `pages`.
- Défauts injectés à la lecture : host, port 502, name, temps WOVolet/WOVoletSmart, champs de plage.

## Reste ouvert
- Corriger `months`, `hits` et l'id vide : ticket FIX-INST-1.
- INST-1 doit mettre à jour les deux tests de troncature dans le même commit.
- Pas de test GUI. CI sur le Qt d'Arch (rolling), pas sur 6.10.3 exact.
