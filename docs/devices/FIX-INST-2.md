# FIX-INST-2 — Segfault intermittente de `tst_projectmanager` (calaos_installer)

## Objectif
Trouver la cause de la segfault vue une fois en CI (installer#54) et corriger le pointeur
`spinBox` non initialisé de `WidgetIOProperties::setValues` relevé par T3.247.

## Livré
PR calaos/calaos_installer#55.
- Cause : `ListeRoom::clear()` détruisait les rooms de l'avant vers l'arrière et ne vidait la
  liste qu'à la fin ; `~Room()` retire ses IOs via `delete_input/output`, qui parcourent la liste
  depuis la room 0 et lisaient donc des rooms déjà libérées (use-after-free). Chemin aussi atteint
  dans l'application : fermeture de projet, nouveau projet.
- Correctif : destruction de l'arrière vers l'avant, chaque room dépilée une fois détruite.
- `setValues` remplit un `DoubleSpinBox` via `doubleSpinBox` ; membres de `UiObject` initialisés.

## Mesuré
- Backtrace CI reproduit avec `MALLOC_PERTURB_` : 9/200 plantages avant, 0/200 après.
- Revue : UAF confirmé sous ASan dans un conteneur Arch (même toolchain que la CI) ; suite complète
  propre sous ASan + UBSan avec le correctif ; autres chemins de destruction vérifiés sûrs.
- 69/69 et 21/21, CI 4/4 verte.

## Contre-mutation
| Mutant | Résultat |
|---|---|
| CM0 | 0 rouge |
| `clear()` d'origine (avant puis vidage) | clearNeverLeavesADestroyedRoomInTheList |
| `setValues` float via `spinBox` | entryHelperFillsAFloatEditor (SIGSEGV) |
| `setValues` float sans `setValue` | entryHelperFillsAFloatEditor |

## Reste ouvert
Défauts voisins regroupés dans FIX-INST-3. Un job ASan en CI serait utile.
