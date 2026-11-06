# CLEAN-1 — Retrait de `get_chauffage_var()` (calaos_base)

## Objectif
Supprimer `ListeRoom::get_chauffage_var()` côté serveur (aucun appelant en production) et son enum
`ChauffType`. Le param `chauffage_id` et sa clé JSON restent : l'installateur s'en sert pour son
assistant de règles de chauffage.

## Livré
PR calaos/calaos_base#188 : fonction, enum et unique test supprimés ; note de la spec §12 mise à
jour.

## Mesuré
`git grep` : plus aucune référence (serveur, Lua, sidecar MCP, tests, docs) ; `chauffage_id` toujours
publié et testé (`JsonApiHome_test`, `JsonApiCharacterization`, `JsonApiStateWireBytes_test`) ;
`make check` 184 tests (181 PASS, 3 SKIP) ; CI verte ; revue approuvée.

## Contre-mutation
Sans objet pour une suppression pure : remplacée par la preuve d'absence d'appelant.
