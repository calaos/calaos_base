# FIX-INST-4 — Règles et pointeurs vers des IOs supprimés (calaos_installer)

## Objectif
Les règles gardaient un pointeur vers un IO détruit (condition sur l'état d'une sortie, entrée de
script), lu ensuite par la sauvegarde.

## Livré
PR calaos/calaos_installer#57.
- `ListeRule::RemoveRulesUsing(IOBase*)` (entrées de condition, `in_event` de script,
  `Condition::output`, sorties d'action ; id vide jamais égal), appelé par `~Room` et `deleteIO*`.
- `setValues` : `findData` pour un ComboBox non éditable ; le helper MySensors renvoie la clé
  (aucun changement de format : le fichier contenait déjà `tcp`).
- Info-bulle hors plage générique ; `pid < 0` vérifié ; include inutile retiré.

## Mesuré
Avant : heap-use-after-free `projectmanager.cpp:389` (`writeCondition`) sous ASan, 10 échecs et
segfault hors ASan. Après : 132 tests verts sous ASan + UBSan (3 passes), CI 4/4 ; revue approuvée.

## Contre-mutation
13 mutants tués (chaque catégorie de référence ignorée, `~Room` et `deleteIO*` sans retrait,
correspondance par pointeur seul, id vide égal, `findText`, ancienne info-bulle…). Essai « vider
`ListeRule` en tête de `~ListeRoom` » écarté : il inverse l'ordre de destruction testé par
FIX-INST-3.

## Reste ouvert → FIX-INST-5
