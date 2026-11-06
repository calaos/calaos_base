# INST-10 — Découverte ESPHome dans l'assistant (calaos_installer)

## Objectif
Recette du 15/10 : un id d'entité erroné (`clim_mitsu` au lieu de `clim_mitsu_buanderie`) et « deux consignes » coché à tort ont laissé la clim muette. L'assistant trouve le climat sur le broker au lieu de faire deviner ces valeurs.

## Livré
PR calaos/calaos_installer#76 (une première version avec un client MQTT écrit à la main a été refusée : l'installer utilise déjà QtMqtt pour Zigbee2MQTT).
- Panneau « Le trouver sur le broker MQTT » pour les deux templates ESPHome, construit seulement avec Qt MQTT (`QT_MQTT_AVAILABLE`, comme Zigbee2MQTT) ; sans Qt MQTT, saisie manuelle.
- `MqttListener` (QMqttClient, session propre, id client aléatoire) écoute `esphome/discover/#`, `+/climate/+/#`, `+/+/climate/+/#` et les `status` pendant 3 s ; liste nom, préfixe, id d'entité, état en ligne, topics publiés.
- Entités « ancien / hors ligne » (ni découvertes ni en ligne, ou voisines d'une entité découverte du même nœud) grisées en bas : ce sont les états retenus d'un ancien id. Un message retenu vide supprime le topic.
- Remplissage : préfixe, id, nom s'il est vide, une ou deux consignes, ventilation, oscillation, action, sonde, modes et plage lus dans la config de découverte (`heat_cool` → `auto`, `auto` d'ESPHome et modes personnalisés écartés et signalés).
- Broker : celui le plus utilisé par les IOs MQTT du projet ; boucle locale remplacée par le broker Z2M ou l'adresse de la box ; identifiants du projet envoyés seulement à son broker. Erreurs lisibles (hôte introuvable, refus, délai, identifiants refusés, abonnement refusé, non-MQTT). Entrée lance la recherche.
- CI Linux : `qt6-mqtt`, `CALAOS_REQUIRE_MQTT=1`.

## Mesuré
tst_esphomediscovery 30 (faux broker en processus, indépendant de QMqttClient), tst_dialognewdevice 23 ; build avec et sans Qt MQTT ; CI 5/5. Revue opus : rien de bloquant, retours traités (ancien id, message vide, identifiants, Entrée, AGENTS.md).

## Contre-mutation
CM0 : 0 rouge. 44 mutants, tous rouges (parsing des topics et de la découverte, une/deux consignes, modes, plage, protocole et erreurs, choix du broker, identifiants, entités anciennes, message vide, Entrée).

## Reste ouvert
- Un ancien id sur un nœud en ligne et sans config de découverte n'est pas repérable.
- Casse des modes de ventilation personnalisés ; préfixes à plus de deux niveaux ; TLS / WebSocket.
- Zigbee2MQTT n'affiche toujours pas la cause d'un échec de connexion (helper commun possible).
- Recouvrement partiel avec la PR #50 (découverte Home Assistant).
- Essai sur la vraie box (VAL-1).
