# MOB-3 — Item de liste climate (calaos_mobile)

## Objectif
Item de liste commun téléphone et desktop ([UI_CLIMATE.md](UI_CLIMATE.md), round5/final) : arc 300° sur la hauteur, nom + pièce, pastille de mode + statut + consigne, chevron, sans −/+.

## Livré
PR calaos/calaos_mobile#24.
- IOClimate replié = item de liste de 68 dp (ClimateListItem, ClimateDial en Shape/PathAngleArc avec CurveRenderer, ClimateIcon en PathSvg). Le tap sur l'item seul déplie les réglages actuels dans un Loader ; MOB-4 les remplacera par la sheet / le dialogue. RoomFilterModel pèse un climate pour 2 rangées.
- Statut unique, par priorité : hors ligne, mode inconnu, arrêté, fault, lockout, action. « Mode inconnu » reste dans la pastille ; le statut y reprend l'action reçue ou rien. Lockout compté localement (arrondi à la minute), timer actif seulement quand il est affiché. connected, fault, lockout, humidity lus comme optionnels et tolérants.
- Consigne « 21 – 23° » en auto, « -- » si inconnue, précision suivant le step ; séparateur décimal du locale. Cadran auto : curseurs neutres hors ligne / off / inconnu, ambre pendant un verrou.
- Theme : mutedColor, amberColor.

## Mesuré
Qt Quick Test 252 verts en rendu logiciel et en RHI OpenGL, tst_roomfiltermodel 14 verts, builds mobile et desktop, qmllint 0 sur les nouveaux fichiers, CI 3/3. Captures 2x logicielles et RHI comparées aux maquettes.

## Contre-mutation
CM0 : 0 rouge. 26 mutants tués :

| Mutant | Tests rouges |
|---|---|
| M1 `connected === false` inversé | test_offlineStatus, test_statusPriority, test_nameIsYellowOnlyWhileTheDeviceActs, test_listStatus (7) |
| M2 fault au-dessus de off | test_listStatus |
| M3 minutes du lockout arrondies par floor | test_lockoutMinutes |
| M4 décompte figé | test_lockoutStatusCountsDownLocally, test_lockoutLeft |
| M5 plage auto ignorée | test_autoShowsTheRange, test_autoRangeWithDecimals, test_formatListSetpoint |
| M6 « refroidit » en jaune | test_actionStatus, test_listStatus, test_theDialUsesTheColourOfTheAction |
| M7 nom jamais jaune | test_nameIsYellowOnlyWhileTheDeviceActs, test_nameTurnsYellowWhenTheActionStarts |
| M8 pastille jamais grise | test_offStatus, test_offlineStatus, test_unknownModeIsAGreyPill, test_greyPillHasNoCyan |
| M9 mode inconnu sans l'action | test_unknownModeTellsTheReportedAction, test_unknownModeIsExplicit |
| M10 pas de re-stamp du lockout | test_lockoutIsRestartedByANewFrame |
| M11 tap sans effet | test_tapOpensTheCurrentSettingsAndTapsAgainClosesThem |
| M12 bande auto colorée à l'arrêt | test_autoDrawsTwoKnobsAndAColouredBandOnlyWhileActing |
| M13 curseurs absents | test_autoDrawsTwoKnobsAndAColouredBandOnlyWhileActing |
| M14 chevron masqué | test_theItemIsTwoRowsHighAndHasNoPlusMinus |
| M15 « — » au lieu de « -- » | 11 tests (ex. test_unknownSetpointIsADash) |
| M16 pièce affichée quand absente | test_roomTemperatureAbsentLeavesNothing, test_formatRoomIsEmptyWhenAbsent (7) |
| M17–M26 (revue) : pression non relâchée au défilement, couleurs des curseurs, timer, Loader, CurveRenderer, step, séparateur, statut inconnu, acting, tap racine | tous rouges (M17 : 2 tests dont l'annulation par défilement) |

## Reste ouvert
- Réglage inline au tap (MOB-4 : sheet / dialogue).
- Revue : 4 bloquants corrigés (répartition desktop, arcs crénelés sur GPU, tap racine, couleurs du cadran auto).
- Textes anglais (`qsTr`), .ts non régénérés.
- Item 68 dp aussi sur desktop (maquette 80 dp).
- Couleur (ambre) et textes du fault choisis sans maquette.
- QtQuick.Shapes sur iOS statique non vérifié.
