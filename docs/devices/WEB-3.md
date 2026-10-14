# WEB-3 — Refonte de ClimateIo (calaos-web-app)

## Objectif
Refondre `ClimateIo` selon [UI_CLIMATE.md](UI_CLIMATE.md) : rangée en lecture seule qui ouvre un réglage (panneau latéral, dialogue centré sous 1024 px), mode et réglages secondaires en `BaseDialog`, tous les statuts du tableau.

## Livré
PR calaos/calaos-web-app#29.
- `IoRowFrame` (prop `opens`), `ClimateIo` (rangée), `ClimateDetail` (cadran 300°, − 22° +, pastille, « Plus de réglages »), `ClimateDial`, dialogues mode et réglages. Logique extraite en `useClimate` (brouillons, commandes JSON fusionnées 350 ms, mode simple), `useClimateText`, `useClimatePanel`, `useMediaQuery`.
- Décodage optionnel de `lockout`, `connected`, `fault`, `humidity` (CLIM-6). Décompte de lockout local à partir de `remaining`. Statut pur et testé (`climateStatus`), priorité : hors ligne > défaut > mode inconnu/arrêt > attente > action.
- `BaseDialog` : `wide` et boutons facultatifs. Variables de thème `--c-heat`, `--c-border-strong`. `RangeSlider` et le slot `panel` d'`IoRowFrame`, devenus inutilisés, sont supprimés.
- Fixtures du mock : un appareil par statut.

## Mesuré
- Vitest : 1383 tests verts (dont 107 de ClimateIo : chaque statut en ligne et en réglage, pastille, consigne, plage auto, butée, capacités, commandes, brouillons, deux commandes en vol, refus, décompte, panneau ou dialogue). Mock-server : 117 verts.
- Playwright : 129 tests, 120 verts et 9 skipped (parcours mobile). `climate.spec.ts` : ouverture, consigne et mode, chaque statut, dialogue centré à 1023 px, panneau à 1024 px, captures fr et en.
- Lint et typecheck propres. CI verte.

## Contre-mutation
CM0 : 0 rouge sur 1383 tests. Vingt et un mutants, tous rouges.

| Mutant | Rouge |
|---|---|
| M1 défaut avant hors ligne | climateStatus « unreachable before a fault » ; ClimateIo « offline before anything else » (2) |
| M2 butée `<` au lieu de `<=` | atRangeStop « min_range apart or closer », « float noise » ; ClimateIo « refuses the inner buttons at the min_range stop » (3) |
| M3 plage sans clamp min_range | ClimateIo « clamps a step that would leave the ends closer than min_range » (1) |
| M4 lockout jamais fini en local | climateStatus « ends the lock-out at zero seconds » ; ClimateIo « falls back to the action when the countdown ends » (2) |
| M5 mode sans flush de la consigne | ClimateIo « sends a setpoint chosen just before a mode change first » (1) |
| M6 −/+ permis en mode inconnu | canAdjust « refuses −/+ … unknown mode » ; ClimateIo « refuses -/+ when offline or mode unknown » (2) |
| M7 « Plus de réglages » ignore l'humidité | ClimateIo « offers More settings only when fan, swing or humidity exist » (1) |
| M8 bascule panneau à 48rem | 4 tests ClimateIo « opens a side panel… » (4) |
| M9 pas de brouillon | 8 tests ClimateIo, dont « two commands in flight » et « a refused command » |
| M10 liste sans place pour le panneau | RoomView « makes room only while a device of this room has its settings open » (1) |
| M11 valeur que personne n'a envoyée sans effet sur les commandes en vol | ClimateIo « keeps the chosen value on screen until the server answers », « ends the draft of a range end when the server reports another value », « lets the server win when it answers with a value nobody sent » (3) |
| M12 overflow restauré sans compteur | BaseDialog « stacked dialogs » × 3 et « restores the page on close » (4) |
| M13 total de jauge = dernière valeur | ClimateIo « measures the gauge against the longest wait seen » (1) |
| M14 `connected: "false"` ignoré | guards « reads connected as the string "false" » (1) |
| M15 angle sans garde min = max | climate-dial « stays on the arc when min equals max » (1) |
| M16 consigne affichée en fan_only | ClimateIo « shows no setpoint in fan_only » (1) |
| M17 curseurs de plage toujours chaud/froid | ClimateDial « takes the colours of the range ends from the paint » ; ClimateIo « greys the knobs » (2) |
| M18 humidité non formatée | ClimateIo « formats the humidity for the language » (1) |
| M19 ouverture sur l'appui d'Espace | IoRowFrame « opens on the release of Space » (1) |
| M20 valeur attendue qui ignore les commandes en vol | ClimateIo « keeps a value the user took back on screen until the server has caught up with it » (2) |
| M21 une commande qui se termine efface le brouillon qu'une plus récente attend encore | ClimateIo « the same value sent twice » : réponse, refus, délai (3) |

Revue : quatre bloquants corrigés (perte d'un appui avec deux commandes en vol ; changement extérieur masqué par une commande en vol, désormais : une valeur que personne n'a envoyée fait gagner le serveur ; brouillon effacé par une commande plus ancienne de même valeur ; verrou de défilement de BaseDialog sans compteur), chacun prouvé par un `test:` rouge.

## Écarts avec la maquette
Icônes MDI pleines ; faces −/+ PNG Calaos ; police Ubuntu de l'app ; « depuis 2 h 10 » omis (`last_seen` non publié) ; jauge d'attente basée sur la première valeur `remaining` vue ; ligne « Attente », décompte dans le réglage ; défaut de sonde en rouge `--c-danger` ; sans `reports_action`, le statut affiche le mode ; « Plus de réglages » aussi pour l'humidité seule.

## Reste ouvert
- À valider contre un vrai serveur une fois CLIM-6 mergé.
- `last_seen` et la durée totale du lockout absents du fil.
- Curseur du cadran non déplaçable (−/+ seulement, comme la maquette).
