# MOB-2 — Widget IOClimate (calaos_mobile)

## Objectif
Afficher et piloter un device climate (spec §5.2, §5.3, §6) depuis les rooms desktop et mobile,
uniquement à partir des capacités et de l'état de l'objet `device`.

## Livré
PR calaos/calaos_mobile#23.
- Type `climate` (Common, IOTypeRegistry visible en sortie, ItemListView) ; le serveur publie le
  device en `inout`, une seule carte s'affiche.
- `IOClimate` (QML partagé) : modes, consigne simple ou plage low/high avec `min_range` (bornes
  min/max/step, poignées posées ensemble), température courante (« — » si absente), badge d'action,
  sélecteurs fan/swing ; off et mode inconnu grisent, `fan_only` masque la consigne ; un device à
  plage seule montre sa plage dans tous les modes et n'envoie jamais `target_temp`.
- Commandes via `sendStringValue` (mode simple, JSON partiel), consignes regroupées en une
  commande (400 ms), édition en attente envoyée à la destruction ; un refus `set_state`
  (`msg_id` `user_cmd:<id>`) remet la carte à l'état serveur aussitôt.
- Poids de la carte dans RoomFilterModel selon les capacités ; correctif du bloc `isUsableDevice`
  de `Common.h`.

## Mesuré
`make check` vert (Qt Quick 115, decoder 70, iobase 23, roomfiltermodel 14), builds desktop et
mobile, CI 3/3. Revue en deux tours avec captures sur les vraies ressources (bloquants : consigne
morte d'un device à plage seule, poignée bloquée après un saut de plage) ; aucune régression des
autres commandes par le `msg_id` ; survol de la carte identique au repos.

## Contre-mutation
20 mutants tués, dont : snap sans clamp, `fan_only`, off, coalescence, `min_range`, listes
QVariantMap prises pour des Array, mode inconnu, valeur absente rendue 0, écho, mode déjà actif,
registre, poids, delegate, plage seule non détectée, poignées posées une par une, refus ignoré, pas
de flush à la destruction.

## Reste ouvert
Un refus efface aussi l'écho d'une commande suivante encore en vol (bref clignotement) ; le chemin
HTTP ne revient qu'au bout de 2,5 s ; traductions `.ts` non régénérées.
