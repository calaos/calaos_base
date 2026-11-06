# INST-1 — Modèle Device et io.xml format 2 (calaos_installer)

## Objectif
Classe `Device`, lecture/écriture de `<calaos:device>` et de `format="2"`, lecture seule d'un
format plus récent ; fin de la troncature « read dummy ».

## Livré
PR calaos/calaos_installer#58.
- Éléments inconnus conservés à tous les niveaux (sous-arbre, texte, CDATA, commentaires,
  namespaces déclarés sur l'élément lui-même), réécrits après le contenu connu de leur parent ;
  sautés en entier dans les plages horaires. Les `pages` de remote_ui passent par la même copie.
- `Device` (IOBase, entrée+sortie de sa room) ; IOs possédés rattachés par l'objet (pointeurs dans
  les deux sens, remis à zéro par le premier détruit) : un id de device vide ou dupliqué ne détourne
  plus ses IOs ; liens et params dans l'ordre du fichier ; impl inconnue conservée.
- Écriture : device à sa place parmi les IOs, IOs possédés imbriqués, puis liens, puis params ;
  `format="2"` si au moins un device ; fichiers sans device inchangés à l'octet.
- Cascade via `deleteIOInput/Output` (règles comprises) ; l'arbre retire aussi les éléments des
  IOs possédés (`ListeRoom::iosDeletedWith`).
- Format > 2 : lecture seule ; format invalide : lecture seule avec son propre message. Fermer,
  Nouveau, Ouvrir s'arrêtent si l'enregistrement est refusé ou échoue ; « Enregistrer sous »
  annulé garde le chemin.

## Mesuré
projectmanager 94, devices 30, widgetioproperties 27, roomteardown 16, teardownorder 4 ; ASan +
UBSan propres ; CI 4/4. Revue en deux tours : bloquant (éléments d'arbre pointant vers des IOs
libérés) corrigé et vérifié en exerçant `FormRules` hors écran sous ASan.

## Contre-mutation
27 mutants, 26 tués (troncature, double écriture, format, borne, garde, cascade, renommage, ordre,
doublon, imbrication, CDATA, clear, plages, liens, `command_io`, namespaces, commentaires, position,
nettoyage des pointeurs, adoption, cascade sur liste vivante, `iosDeletedWith`…) ; le survivant
visait une remise à zéro redondante, retirée.

## Reste ouvert
- INST-2 : si les IOs possédés deviennent des enfants de l'item du device, `removeIOItems` ne doit
  supprimer que les items de plus haut niveau (sinon double suppression).
- Ordre : l'installateur écrit les entrées puis les sorties (comme avant), le device restant à sa
  place parmi les entrées ; cycles stables dès la première sauvegarde.
- Bug de déplacement d'IO (préexistant) → FIX-INST-7.
