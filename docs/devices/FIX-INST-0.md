# FIX-INST-0 — Une plage horaire sans `months` reste active toute l'année (calaos_installer)

## Objectif
Un `InPlageHoraire` sans attribut `months` ne doit plus être réécrit `000000000000` : le serveur le
considérait alors inactif toute l'année (perte de données silencieuse).

## Livré
PR calaos/calaos_installer#52 (`test:` puis `fix:`). `months` absent → tous les mois actifs ;
valeur explicite, vide ou illisible inchangée, alignée sur le serveur (`InPlageHoraire.cpp:54`,
`:375-391`).

## Mesuré
Test inversé rouge sur l'ancien code (61/62), vert après le correctif (62/62), vérifié par la
revue. Aller-retour d'une valeur explicite non palindromique (`010100000110`) inchangé.

## Reste ouvert
Pas de test pour `months=""` (comportement aligné sur le serveur) : à ajouter dans FIX-INST-1.
