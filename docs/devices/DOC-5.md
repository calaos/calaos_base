# DOC-5 — Documentation utilisateur climate (doc-website)

## Objectif
Chapitre « Chauffage et climatisation » (fr+en) : principe d'un device, templates et assistant, guide ESPHome (CN105 + Fujitsu, température relayée), thermostat interne, affichage web/mobile/panneau/MCP, règles de chauffage, dépannage.

## Livré
PR calaos/doc-website#3, mergée à la demande de Raoul avant la revue pour relecture en ligne (https://doc.calaos.fr/fr/climate/). 8 pages x 2 langues, 33 captures françaises d'une maison de démo inventée, scripts de capture rejouables dans tools/screenshots/ (web, mobile, remote_ui, installer), io.xml/rules.xml de démo générés par l'assistant réel.

## Mesuré
Hugo 0.168 extended en docker : 0 warning de ref, fr/en rendus, 0 image manquante ; CI build-test verte ; déploiement vérifié (pages fr et en en 200).

## Reste ouvert
- Revue indépendante et retours de Raoul.
- Vérifier CLIM-8 (`emulate_range`) et CLIM-9 (`valid_min`/`valid_max`) contre le code mergé, retirer les encadrés « en cours de développement ».
- Refaire les captures installer après INST-9 (5 assistants, sortie refusée, éditeur) et INST-10 (découverte ESPHome).
- Note YAML du template esphome_climate à aligner sur la recette `mqtt_subscribe` (demandé à CLIM-8).
