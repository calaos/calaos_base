# PUSH-1 — Push Android data seul pour la nouvelle app (calaos_base)

## Objectif
Message FCM data seul pour l'app Android Qt 6 (AND-7 côté calaos_mobile : image caméra dans la notification), sans casser l'app 3.1.x du Play Store qui n'affiche pas les messages data seuls.

## Livré
PR calaos/calaos_base#215.
- `register_push` : champ optionnel `push_format` (`"notification"` par défaut | `"data"`), dans `data` en WebSocket v3, à la racine en HTTP api.php. Valeur inconnue = notification, un avertissement par processus sans jeton ni valeur. Réenregistrement = format remplacé (l'app l'envoie à chaque fois). Sans effet sur iOS.
- Stockage `push_tokens.push_format` (migration ALTER TABLE au démarrage). Si la migration échoue (base en lecture seule, verrouillée, disque plein), les jetons sont lus et écrits sans la colonne, au format notification, jusqu'au prochain démarrage. Une lecture qui échoue répond en erreur ; une purge en échec ne fait plus perdre la lecture.
- Envoi : entrée gorush séparée pour Android data : `platform` 2, `android.priority` = high, `data` {title, body, message[, event_uuid]} en strings, sans title/message/notification/sound au premier niveau (vérifié dans gorush v1.18.9). Android legacy et iOS inchangés octet pour octet.
- Jetons retirés des logs (trace d'enregistrement, dump du corps envoyé, SQL étendu des erreurs). URL du service push injectable pour les tests seulement.
- Contrat : docs/08_http_api.md (register_push), docs/10_events_notifications.md (format data, repli, limites FCM).

## Mesuré
PushPayload_test, PushTokenStore_test (8), PushTokenLocked_test (4, rouge avant le correctif), JsonApiSession_test (+4), envoi testé contre un faux serveur HTTP local, logs capturés sans jeton. Couverture différentielle : 0 ligne non couverte, 0 exclue. Deux revues opus : comparaison ancien/nouveau code sur 79 104 cas sans différence ; modes de panne vérifiés ; tests stables (240 runs en parallèle). CI verte.

## Contre-mutation
CM0 : 0 rouge. M1–M9 et N1–N9 tués.

## Reste ouvert
- Envoi réel vers gorush à valider sur la box avec AND-7.
- Après un échec de migration, nouvelle tentative seulement au prochain démarrage.
