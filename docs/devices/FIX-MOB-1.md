# FIX-MOB-1 — Sheets et dialogues fermés à la déconnexion (calaos_mobile)

## Objectif
Quand l'app perd la connexion et revient à la page de login, les sheets et dialogues ouverts (climate : détail, mode, « Plus de réglages ») restaient affichés par-dessus (retour utilisateur du 15/10).

## Livré
PR calaos/calaos_mobile#28. Cause : BottomSheet (Drawer sur Overlay.overlay) et PopupBase (reparenté sur dialogOverlayLayer) vivent au niveau de la fenêtre ; le pop du StackView ne les touche pas.
- Mobile : BottomSheet se ferme dès que applicationStatus n'est plus LoggedIn.
- Desktop : pas de page de login, les pages restent pendant une reconnexion. OverlayLayer ferme, du haut vers le bas de sa pile, les popups dont `closeOnDisconnect` est true (défaut false) : DialogClimate (détail, mode, « Plus de réglages »), DialogSensorDetails, DialogRGBColorPicker, DialogKeyboard ouvert pour un IO. Restent ouverts : TabList, Bookmarks, AddWidget, confirmations, clavier générique (y compris depuis DialogNetInterface), Reboot, RecoveryBoot, NetInterface, confirmation de mise à jour.
- OverlayLayer tient une pile de popups : fermer le dialogue du dessus (même hors déconnexion) rend l'overlay à celui du dessous ; les popups détruits ou masqués sont filtrés ; désactiver la couche ferme toute la pile du haut vers le bas. `resumeOverlay` supprimé.

## Mesuré
Qt Quick Test 423 verts, 2 skip, rendu logiciel offscreen ; CI 3/3. Avant le fix, 6 tests sur 9 rouges côté mobile. Deux revues (opus) : merge.

## Contre-mutation
CM0 : 0 rouge. 16 mutants tués : M1–M6 (BottomSheet et première version de PopupBase), N1–N10 (défaut true, release qui remet l'overlay à null, fermeture du bas en premier, clavier sans forIo, dialogues mode et capteur non marqués, couche qui ignore la déconnexion, entrées mortes non filtrées, désactivation qui ne ferme que le haut, closeAll à l'envers).

## Reste ouvert
- `climate.flush()` s'exécute à l'auto-fermeture sur un transport mort (commande perdue sans message).
- Le dialogue natif de saisie mobile n'est pas fermé.
- Tests d'IO détruit non rouges sans le fix (garde-fous) ; test du clavier sauté sans QtQuick.VirtualKeyboard.
- App complète et iOS non lancés.
