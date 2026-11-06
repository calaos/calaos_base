# FIX-INST-3 — Destruction des rooms et des listes, garde de `setValues` (calaos_installer)

## Objectif
Corriger les défauts voisins relevés par la revue de FIX-INST-2 : `~Room` hors de `ListeRoom`,
ordre de destruction statique `ListeRoom`/`ListeRule`, `setValues` qui ignorait les éditeurs
désactivés et la garde hors plage de T3.247.

## Livré
PR calaos/calaos_installer#56.
- `~Room` retire elle-même les règles de ses IOs puis les détruit (un IO entrée+sortie une seule
  fois), sans dépendre de `ListeRoom` ; `clear()` et `Remove` dépilent la room avant de la détruire.
- Le constructeur de `ListeRoom` construit `ListeRule` d'abord, qui est donc détruit après lui.
  `~Rule` devient virtuel (aucune sous-classe en production).
- `setValues` saute les éditeurs désactivés (c'est ainsi que readonly et non éditable sont rendus)
  et fait passer les valeurs hors plage par l'élargissement + `flagOutOfRange`.
- Suites `tst_roomteardown` (fork + délai) et `tst_teardownorder` (fork, sondes sur pipe) ; tests
  ajoutés à `tst_widgetioproperties`. Les tests ne sont buildés qu'en CI Linux.

## Mesuré
Avant : 4 tests de room en timeout, ordre de sortie inversé, 4 tests `setValues` rouges. Après :
69/69, 26/26, 8/8, 4/4 ; suites propres sous ASan + UBSan (conteneur Arch) ; CI 4/4 verte ;
revue approuvée sans bloquant.

## Contre-mutation
7 mutants, tous détectés : `~Room` d'origine (4 timeouts), sans retrait des règles, sans
dédoublonnage entrée/sortie, constructeur sans `ListeRule::Instance()`, `setValues` sans test
d'activation, sans élargissement int, sans flag double.

## Reste ouvert
Repris dans FIX-INST-4 (pointeurs pendants des conditions, `pid < 0`, info-bulle, coût de sortie,
`findText`, include inutile). Bouton d'aide encore cliquable en dialogue non éditable.
