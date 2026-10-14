# MOB-4 — Réglage climate (calaos_mobile)

## Objectif
Remplacer le dépliage inline de MOB-3 par le réglage du design final : bottom sheet sur téléphone, dialogue sur desktop ([UI_CLIMATE.md](UI_CLIMATE.md), round4/final), avec les commandes de MOB-2 corrigées pour plusieurs commandes en vol.

## Livré
PR calaos/calaos_mobile#25.
- Action showClimateDetails ; ClimateSheet (BottomSheet sur Drawer, ouverte par le code seulement, Back et Escape pris au relâchement, corps défilable en paysage) et DialogClimate (Dialog.qml ; le dialogue du dessous est désactivé quand un autre est ouvert par-dessus) ; sheets / dialogues empilés pour le mode et « Plus de réglages ». Dépliage inline de MOB-3 supprimé.
- Cadran 300° (ClimateDial), « − 22° + » avec les faces PNG, deux curseurs et deux paires en auto, butée min_range (boutons intérieurs refusés, jamais dans le mauvais sens), pastille de mode, ventilation / oscillation / humidité selon les capacités (désactivées hors ligne), lockout en secondes (arrondi au supérieur, compté depuis la réception de la trame : IOBase.deviceStamp).
- set_state numérotés : msg_id user_cmd:<io>#<n>, réponse CommandAnswered(io, n, ok) décodée et transmise par IOBase.commandAnswered ; ClimateDrafts rattache chaque refus à son vol. Un champ n'est suivi que si la valeur envoyée diffère de l'attendue ; la réponse (état publié) égale à l'envoi de la commande k libère 0..k ; une valeur que personne n'a envoyée libère le champ ; un succès ne change rien ; un refus retire son seul vol (le mode n'a pas de vol) ; expiration à 10 s (readback_hold d'un proxy).
- Le modèle lâche le device à la fermeture ; les rangées de choix gardent leurs delegates d'une trame à l'autre.

## Mesuré
Qt Quick Test 370 verts en rendu logiciel et en RHI OpenGL, tst_iobase 26, décodeur 76, builds mobile et desktop, qmllint 0 sur les nouveaux fichiers, CI 3/3. Captures RHI comparées aux maquettes. Cas testés : trois + puis 25 extérieur ; plage auto low+ ×2, high+, puis 18/26 ; + − + avec réponse ou refus de la première ; refus de mode pendant un vol de consigne ; commande acceptée lente puis refus ; réponses dans le désordre.

## Contre-mutation
CM0 : 0 rouge. 38 mutants tués : 21 du premier tour ci-dessous, et 17 de la revue (refus par numéro, succès, numérotation, stamp du verrou, expiration, libération du modèle, mode suivi, swipe, Back, dialogue du dessous, rangées stables, mauvais sens, arrondi, rangées et boutons hors ligne, plage grisée, défilement).

Premier tour :

| Mutant | Tests rouges (extraits) |
|---|---|
| D1 réponse rattachée à la commande la plus récente | test_aValueSentTwiceKeepsTheLastDraft |
| D2 une valeur extérieure ne libère pas | test_threePlusThenAnOutsideChangeWins, test_autoRangeLowTwiceHighThenOutsideValues (7) |
| D3 tout champ suivi | test_aFieldEqualToWhatWasExpectedIsNotFollowed |
| D4 un refus vide tout | test_theFirstCommandRefusedKeepsTheOthers, test_aValueSentTwiceAndTheFirstRefused (4) |
| D5 valeur inchangée = nouvelle | test_aSentValueStaysUntilTheServerShowsIt (2) |
| D6 vols non affichés | 25 tests |
| L1 min_range non appliqué | test_theInnerButtonsAreRefusedAtMinRange, test_canStepFollowsWhatMovesTheValue (7) |
| L2 humidité sans capacité | test_theMorePageNeedsSomethingToShow |
| M1 boutons jamais refusés | test_minRangeRefusesTheInnerButtons (6) |
| M2 mode courant renvoyé | test_theCurrentModeIsNotResent (2) |
| M3 brouillon perdu à la destruction | test_aPendingEditIsSentWhenTheViewGoesAway |
| M4 jamais d'expiration | test_nothingHeardForAWhileShowsTheServer |
| M5 secondes affichées en minutes | test_theLockoutCountsInSeconds (3) |
| M6 la sheet n'envoie pas à la fermeture | test_closingSendsWhatIsStillBeingTyped |
| M7 la sheet de mode reste ouverte | test_choosingAModeSendsItAndClosesOnlyTheChoice |
| M8 overlay non rendu au 1er dialogue | test_aTapOutsideClosesTheTopDialogOnly (3) |
| M9 le tap n'ouvre rien | test_tapAsksForTheSettingsOfThatDevice (2) |
| M10 écart jamais dit | test_theInnerButtonsAreRefusedAtMinRange |
| M11 ventilation en brouillon seulement | test_aFanChoiceIsSentAtOnceAndTheSheetStaysOpen (2) |
| M12 titre du dialogue | test_opensWithTheDialAndTheSetpointLine (2) |
| M13 refus ignoré | test_aRefusedOnlyCommandShowsTheServerAgain (4) |
| M14 le dialogue n'envoie pas à la fermeture | test_closeSendsWhatIsStillBeingTyped |

## Reste ouvert
- Transport HTTP : aucune réponse remontée, les vols attendent l'expiration (10 s).
- Mode non mis en brouillon (la pastille attend l'écho du serveur).
- Textes anglais, .ts non régénérés ; titre du dialogue dans le style réel de Dialog.qml.
- Pas de jauge d'attente (min_off_time non publié).
- QtQuick.Shapes sur iOS statique non vérifié ; Back et swipe testés sur le harnais, pas sur un vrai Android ; app non lancée contre un serveur.
- Un champ refusé dans une commande acceptée par ailleurs (application champ par champ côté serveur) reste affiché jusqu'à 10 s.
- Un id d'IO se terminant par « #chiffres » serait mal découpé sur le chemin non numéroté (négligeable).
