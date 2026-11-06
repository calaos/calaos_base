# DOC-8 — Doc plus claire et configs d'exemple (doc-website)

## Objectif
Relecture utilisateur du 16/10 : la section « Vanne de zone » et la recette du plancher réversible n'étaient pas compréhensibles ; pouvoir télécharger des configs pour voir le fonctionnement dans l'installer.

## Livré
PR calaos/doc-website#6 (en ligne : https://doc.calaos.fr/fr/calaos_installer/devices/examples/).
- « La vanne de zone » réécrite : la situation d'abord, tableau d'ouverture de la vanne selon le mode, pourquoi la zone a besoin de son mode.
- Recette 6 réécrite : schéma mermaid (mode global → règles → zones → vannes → contacts PAC), règle « Plancher : mode froid » montrée, contacts PAC exclusifs, règles de retour au mode, FAQ (zone en froid pendant le chauffage, passage en été, éteindre une pièce, sans sonde d'humidité). Corrigé : au changement de mode global, le contact PAC bascule tout de suite si une vanne est encore ouverte (les vannes attendent leur durée minimale) ; passer par 0 ferme les vannes aussitôt.
- 4 exemples téléchargeables (dossier + zip, io.xml, rules.xml, LISEZMOI/README) : radiateur électrique, clim ESPHome, plancher chauffant 3 zones + circulateur, plancher réversible 9 zones + PAC + ECS. Matériel remplacé par des variables internes « (à remplacer) ».

## Mesuré
Chaque exemple chargé par calaos_server master (0 avertissement, sauf 5 attendus « no dew-point cut » pour le n° 4) et ouvert dans Calaos Installer master sans erreur, aller-retour d'enregistrement identique ; scripts rejouables dans tools/examples/ (vérifiés sur un projet volontairement cassé). Hugo sans erreur, CI verte, déploiement vérifié.

## Reste ouvert
- Pas de capture de la règle « mode froid » (celle de « mode chaud » l'accompagne).
