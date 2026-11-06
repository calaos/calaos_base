# FIX-INST-5 — Suites de FIX-INST-4 (calaos_installer)

## Objectif
Supprimer un IO ne doit faire perdre aux règles que ce qui le référençait : ni les règles d'un
doublon d'id, ni tout le Lua d'une condition script, ni l'id d'une variable orpheline.

## Livré
PR calaos/calaos_installer#59.
- `RemoveRulesUsing` : correspondance par pointeur seul (iid/oid legacy résolus vers le pointeur
  de la caméra au chargement).
- Condition script : seul le déclencheur supprimé est retiré (`removeScriptInput`, qui ne fait plus
  `erase(end())` si l'IO est absent) ; sans déclencheur restant, la règle et son Lua sont gardés.
- Ids de variable orphelins (`params_var` condition/action, `output_param_var`) gardés et réservés
  (`reservePendingIOId`, sous id/iid/oid) ; réservation vidée au changement de projet.
- Info-bulle es/hi/nb/pl, commentaire `ListeRule.h`, `searchRuleInput/Output` retirés.

## Mesuré
12 tests rouges sur master, 136 verts après, y compris sous ASan + UBSan ; `.ts` valides et
acceptés par `lrelease` ; CI 4/4 ; revue approuvée.

## Contre-mutation
13 mutants, 12 tués (détachement script, réservations par catégorie et noms legacy,
sur-réservation, correspondance par id, condition de sortie, sorties d'action, `~Room`, ancien
`removeScriptInput`) ; un équivalent.

## Reste ouvert → FIX-INST-6
Une règle script sans déclencheur est **inerte** côté serveur (jamais déclenchée,
`ConditionScript::Evaluate()` faux) : le commentaire de test qui dit le contraire est faux, et
l'utilisateur doit être averti. Condition standard à plusieurs entrées : supprimer une entrée
supprime toute la règle.
