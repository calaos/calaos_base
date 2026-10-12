# FIX-INST-7 — Déplacer un IO entre pièces ne le perd plus (calaos_installer)

## Objectif
Déplacer un IO entrée+sortie (Internal*, Scenario, InputTimer, caméra, audio, AVR, device) entre
pièces le faisait disparaître d'io.xml ou lui faisait perdre la moitié de sa nature.

## Livré
PR calaos/calaos_installer#61.
- Primitive modèle `ListeRoom::moveIO(io, room)` : conserve les listes d'origine, un device emmène
  ses IOs possédés, règles intactes (pointeurs), refus sans effet (même pièce, pièce inconnue, IO
  orphelin, IO possédé seul — c'est son device qu'on déplace).
- `moveIOInHome` (`src/HomeTreeMove.{h,cpp}`) : déplacement modèle puis re-parentage de tous les
  items de l'IO, item courant conservé ; utilisé par le menu et le glisser-déposer ; projet marqué
  modifié.

## Mesuré
`tst_iomove` 41 cas (20 rouges avant, dont Internal*/Scenario/InputTimer absents du fichier écrit) ;
toutes les suites vertes, ASan + UBSan propres ; CI 4/4 ; revue approuvée (aucun autre chemin de
déplacement ; le drag est manuel, pas de double traitement par Qt).

## Contre-mutation
14 mutants, 14 tués (listes non restaurées, IOs possédés laissés, IO possédé accepté, pas de
détachement, même pièce / pièce inconnue acceptées, items non déplacés ou partiellement, item
courant perdu, items imbriqués déplacés à tort…).

## Reste ouvert (pris par INST-2)
« Move to... » masqué sans explication pour un IO possédé ; `HomeTreeWidget::dropMimeData` est du
code mort (`setAcceptDrops(false)`) ; `Room::RemoveInput/RemoveOutput(IOBase*)` sans appelant en
production ; commentaire périmé `projectmanager.cpp:95`.
