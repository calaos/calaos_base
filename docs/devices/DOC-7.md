# DOC-7 — Recettes plancher réversible et générateur commun (doc-website)

## Objectif
Décision 22 : documenter le plancher réversible sans device PAC, et remettre la doc climate au niveau du code (CLIM-8, CLIM-9, CLIM-10, INST-9, INST-10).

## Livré
PR calaos/doc-website#5 (en ligne : https://doc.calaos.fr/fr/calaos_installer/rules_heating/).
- Double consigne émulée et vraisemblance de la sonde vérifiées contre le code mergé ; encadrés « en cours de développement » retirés.
- Corrigé : humidité facultative pour le froid au sol (avertissement au chargement) ; découverte ESPHome « Le trouver sur le broker MQTT » documentée.
- Vanne de zone `output`, param `modes`, template « Zone de plancher réversible ».
- Recettes : plancher réversible à 9 zones (mode global 0/1/2, zones sans froid, contacts PAC chaud/froid par règles sur les vannes, toutes vannes fermées → contacts OFF, remise dans le mode global par une condition de sortie sur le device) ; eau chaude sanitaire en thermostat indépendant (valid_max 95, value_max relevé) ; plancher chauffant seul + circulateur.
- Captures de l'installer refaites depuis master (libellés traduits, filtrage par type, découverte ESPHome, assistants des zones et de l'ECS, règles de la maison à 9 zones).

## Mesuré
Hugo sans erreur de ref, fr et en ; CI verte ; déploiement vérifié. Recettes validées sur un vrai calaos_server de master (IOs matérielles remplacées par des Internal, verrous raccourcis) : jamais les deux contacts PAC ensemble, zones sans froid à l'arrêt, ECS indépendante. La règle de remise en mode a révélé FIX-BASE-7 (condition de sortie sur un device toujours fausse), corrigé avant le merge.

## Reste ouvert
- Recapture des assistants si INST-11 change le formulaire de commande.
