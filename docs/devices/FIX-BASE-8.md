# FIX-BASE-8 — Conditions du lecteur audio (calaos_base + calaos_installer)

## Objectif
`lecteur == onplay` (condition standard) et la condition de sortie sur un lecteur n'étaient jamais vraies : depuis 215c0c69 (2015) `AudioPlayer::hasChanged()` calculait l'évènement dans une variable locale, `get_value_string()` renvoyait "". FIX-BASE-7 l'avait rendu visible par un opt-out et un avertissement.

## Livré
PR calaos_base#214, calaos_installer#80.
- Serveur : dernier évènement gardé (`lastEvent`), renvoyé par `get_value_string()` ; opt-out `hasComparableState` retiré du lecteur (IPCam et RemoteUI le gardent) ; `astatus` initialisé.
- Roon ne rejoue plus `onplay` à chaque avancée de la position : `set_status` seulement sur un vrai changement d'état, la position passe toujours par `EventAudioStatusChanged`. Squeezebox n'avait pas ce schéma.
- Sémantique : condition d'évènement, pas un état « en lecture » ; `onsongchange` et `onvolumechange` remplacent `onplay` en cours de lecture ; "" avant le premier évènement, rien de persisté. Dans `get_home`/`get_room`/`get_io`, le `state` d'un `audio_player` vaut ce dernier évènement (aucun client ne casse : web, mobile, remote_ui, MCP vérifiés). Docs 05, 08 et 18 à jour.
- Installer : formulaire de condition avec les 7 évènements (ajout de onplaylistchange et onvolumechange), aussi pour `audio_player` (branche défensive : le modèle installer enregistre les lecteurs en `audio`).

## Mesuré
Tests rouges d'abord : AudioPlayerCondition_test (règles XML, conditions standard et sortie, 7 évènements, `!=`, dernier évènement, condition non déclenchante évaluée pendant la lecture, `state` de get_io/get_home), RoonPlayerEvents_test (seek qui avance sans nouvel `onplay`, reprise après pause, arrêt), tst_conditionform. CI verte des deux côtés. Revue opus : un bloquant (Roon) corrigé.

## Contre-mutation
Serveur : CM0 0 rouge ; 7 mutants tués (dont `set_status` à chaque mise à jour Roon). Installer : CM0 0 rouge ; 4 mutants tués, 1 équivalent.

## Reste ouvert
- Autres usages de `audio` côté installer (FormActionStd, formrules, DialogScriptEditor, projectmanager) inchangés.
