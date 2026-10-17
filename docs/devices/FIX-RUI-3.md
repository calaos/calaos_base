# FIX-RUI-3 — Panneaux bloqués sur le logo après une mise à jour serveur + firmware

## Objectif
Retour de Raoul (17/10) : une mise à jour calaos-os contenant calaos-server et calaos-remote-ui laissait les panneaux sur le logo Calaos jusqu'à un reset manuel. Un échec d'OTA ne doit jamais bloquer un panneau, et la course doit se résoudre seule.

## Cause
- Firmware : pendant le téléchargement, le serveur redémarre ; le panneau se reconnecte, le serveur repropose l'offre (`RemoteUIWebSocketHandler.cpp:94-111`) et le reducer repasse le téléchargement à « Available » (`flux/app_store.cpp:429-444`). La page de démarrage ne quittait le logo qu'à la connexion sans OTA en attente ou sur Downloading → Error (`startup_page.cpp:978-993`, `:1114-1145`) ; la séquence réelle Available → Error → Idle ne passait par aucun des deux. Aucun nouvel essai, et le reset de l'écran d'erreur annulait un essai déjà relancé.
- Paquet : le postinst du firmware (`build-deb.sh:73-87`) déclenchait le rescan avant la configuration et le redémarrage de calaos-server dans la même transaction.

## Livré
- calaos_remote_ui#12 : page de démarrage réévaluée à chaque changement d'état et depuis la boucle de rendu ; boucle `ota_download` partagée avec chien de garde de 20 s, partition rendue amorçable seulement sur une image complète et vérifiée, `esp_ota_abort` sur toute erreur ; nouvel essai avec backoff 30 s → 30 min, toujours avec la dernière offre reçue (même pendant un téléchargement) ; pas de nouvel essai sur une erreur permanente (4xx, checksum, image rejetée) jusqu'à une nouvelle offre ; récupération après 10 s d'un statut OTA resté actif après un événement perdu ; offre sans checksum refusée. Rescan par trigger dpkg différé (`interest-noawait`), après le postinst de calaos-server.
- calaos_base#216 : offre retenue 60 s après le démarrage et 120 s après un rescan (`OtaOfferGate`), puis envoyée aux panneaux connectés pas à jour ; image servie tout de suite. docs/07_remoteui.md.

## Mesuré
Tests rouges d'abord dans les deux dépôts, dont `OtaManagerFlow.AServerRestartMidDownload…` (la séquence signalée), `DebPackage.*` (vrai dpkg : le rescan passe après le redémarrage du serveur dans les deux ordres et dans la séquence d'apt), `OtaOfferTiming_test`. Firmware : ctest 348/348, ASan et TSan 246/246, build ESP-IDF 5.5. Serveur : couverture de diff 60/60, CI verte. Revue opus : aucun bloquant ; 4 points traités avant le merge.

## Contre-mutation
CM0 : 0 rouge dans chaque campagne. Firmware 17 mutants, paquet 3, serveur 7 : tous rouges.

## Reste ouvert
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` absent : une image vérifiée mais qui plante au démarrage n'est pas remise à l'image précédente. Changer le bootloader des panneaux existants est à décider avec Raoul.
- Pas de test bout en bout simulateur + serveur réel ; pas de reprise HTTP Range ; postinst de calaos-server (calaos/pkgdebs) non vérifié.
- `websocket_client.cpp:132` remet le compteur de reconnexion à zéro à chaque `connect()` : backoff WS bloqué à 5 s.
