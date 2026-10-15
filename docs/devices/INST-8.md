# INST-8 — FormActionStd étiré (calaos_installer)

## Objectif
Retour utilisateur du 15/10 : depuis INST-5, en mode texte le popup gardait la taille du formulaire JSON. Il doit revenir à sa taille naturelle à chaque bascule et à chaque action.

## Livré
PR calaos/calaos_installer#70.
- Cause : un widget de premier niveau ne rétrécit jamais seul. Après un formulaire JSON, le popup restait à 267 px de haut pour le mode texte et pour tout IO affiché ensuite (192 px avant). Un device en mode texte demandait 209 px à cause de la ligne du bouton de bascule.
- `fitToContent()` : taille = max(taille du `.ui`, contenu du layout, hauteur pour largeur), appelée dans `setAction`, à chaque bascule texte/formulaire, quand une notice du formulaire apparaît ou disparaît, et dans `showEvent`.
- Le bouton « Use the form » / « Edit as text » est dans la rangée de « Dynamic value... » : le mode texte d'un device a la hauteur d'un IO ordinaire (192 px, géométrie du `.ui` inchangée depuis avant INST-5).

## Mesuré
Avant : IO ordinaire après un formulaire 438x267, mode texte après le formulaire 438x267, device en mode texte 438x209. Après : 192, 192, 192 ; formulaire 438x268. 5 tests rouges avant le fix, `tst_devicecommand` 75 cas verts, `make check` 15 suites, CI 5/5. Captures avant/après vérifiées par l'orchestrateur.

## Contre-mutation
CM0 : 0 rouge. 8 mutants, 6 tués, 2 équivalents.

| Mutant | Rouge |
|---|---|
| bascule texte/formulaire sans ajustement | theTextModeShrinksBackAfterTheForm, theFormGrowsAgainAfterTheText |
| édition du formulaire sans ajustement | aNoticeThatAppearsGrowsThePopup |
| premier affichage sans ajustement | theFormGrowsAgainAfterTheText |
| bouton de bascule sur sa propre ligne | anOrdinaryIoHasTheSameSizeWhateverCameBefore, theTextModeOfADeviceHasTheSizeOfAnOrdinaryIo, theTextModeShrinksBackAfterTheForm, theFormGrowsAgainAfterTheText |
| taille qui ne rétrécit jamais | anOrdinaryIoHasTheSameSizeWhateverCameBefore, theTextModeShrinksBackAfterTheForm, theFormGrowsAgainAfterTheText |
| hauteur du contenu ignorée | aNoticeThatAppearsGrowsThePopup |
| `setAction` sans ajustement | équivalent (déjà ajusté par `showCommandForm(false)` et `showEvent`) |
| taille du `.ui` ignorée | équivalent (le contenu fait déjà 192 px) |

## Reste ouvert
- Hauteur inexacte avant le premier `show` (hints trop petits), sans effet : seule la largeur, constante, sert au placement.
