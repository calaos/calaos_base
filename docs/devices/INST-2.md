# INST-2 — Arbre pièce → device → IOs et suppression honnête (calaos_installer)

## Objectif
Montrer les devices et leurs IOs possédés dans l'arbre, supprimer en cascade, et dire à
l'utilisateur ce que la suppression fait aux règles et aux liens avant qu'il confirme.

## Livré
PR calaos/calaos_installer#62.
- Arbre pièce → device → IOs possédés (`QTreeWidgetItemDevice`, `src/HomeTree.*`) ;
  `removeIOItems` ne supprime que les items de plus haut niveau ; propriétés d'un device en lecture
  seule (`DialogDeviceInfo`).
- `deletionImpact` (`Calaos/DeletionImpact.*`), calculé avant la suppression : IOs détruits, règles
  supprimées, règles script devenues inertes (même définition que le serveur, conditions
  `trigger=false` comprises), règles gardant un id de variable orphelin, liens de devices restants
  qui pointent dans le vide (§4.2).
- `DialogDeleteImpact` pour un IO, un device et une pièce (Cancel par défaut, IOs détruits listés).
- Menu : « Move to… » désactivé avec l'infobulle « Moves with its device » sur un IO possédé.
- Glisser-déposer : le côté réception, mort, fonctionne via `moveIOInHome` ; le glisser vers les
  conditions/actions garde `text/uri-list` (IOs possédés compris) ; seule la dépose d'un IO possédé
  sur une pièce est refusée. Nettoyages : `dropMimeData`, `Room::RemoveInput/RemoveOutput(IOBase*)`,
  `ListeRule::scriptTriggeredBy` supprimés ; commentaires périmés corrigés.

## Mesuré
tst_deleteimpact 22, tst_hometree 26, tst_iomove 41 (arbre de production) ; toutes les suites
vertes sous ASan + UBSan ; CI 4/4. Revue en deux tours : régression du glisser vers l'éditeur de
règles (format mime) et refus de glisser un IO possédé, corrigés.

## Contre-mutation
18 mutants, 18 tués (double suppression et items oubliés par ASan ; inertie, `trigger=false`,
`command_io`, iid/oid, démarrage, impact de pièce, liens du device supprimé, structure de l'arbre,
format mime, IO possédé glissé ou déposé, drag extérieur, bouton par défaut…).

## Reste ouvert
- Traductions : faites avec INST-6.
- `stopAutoScroll()` non appelé dans `dropEvent` → FIX-INST-6.
- INST-4 : insertion à chaud d'un device dans l'arbre (`addItemInput/Output` ne le gèrent pas).
