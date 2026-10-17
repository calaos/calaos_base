# PUSH-2 — Push iOS : mutable_content sur toutes les notifications (calaos_base)

## Objectif
L'extension iOS PushNotif (pastille calculée côté app, IOS-3) n'est exécutée par iOS que pour une notification mutable-content, posé jusqu'ici seulement avec une image caméra.

## Livré
PR calaos/calaos_base#217. Entrée iOS (platform 1) : `mutable_content: true` toujours ; `data.event_uuid` seulement avec une image ; pas de `badge` serveur. gorush v1.18.9 ne lit que `mutable_content` (notify/notification.go:78, appliqué notification_apns.go:359-360) : la clé morte `mutable-content` est retirée. Android legacy et data inchangés. Docs : 10_events_notifications.md.

## Mesuré
PushPayload_test 13/13 (nouveau test iOS sans image, pins iOS mis à jour, pins Android intacts), tests de logs sans jeton verts, CI verte. Clé gorush revérifiée dans le source v1.18.9 par l'orchestrateur.

## Contre-mutation
CM0 : 0 rouge. M1 (retrait), M2 (seulement avec image), M3 (clé avec tiret), M4 (badge), M5 (event_uuid toujours), M6 (mutable_content sur Android) : tous tués.

## Reste ouvert
- Envoi réel vers gorush/APNs à valider avec IOS-3.
