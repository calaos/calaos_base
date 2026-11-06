# INST-3 — Négociation du format avec le serveur (calaos_installer)

## Objectif
Lire `io_format` du `config get`, déclarer `"io_format": 2` dans chaque `config put` (décision 16),
ne pas envoyer de devices à un serveur trop ancien, afficher l'`error` du serveur sur un refus, et
ne jamais supprimer des devices de la box sans le dire.

## Livré
PR calaos/calaos_installer#65.
- Helper `src/OnlineConfig.{h,cpp}` (QtCore) : requêtes get/put, analyse des réponses (`io_format`
  = entier 1..1000, sinon 1), mémoire du format par hôte pour la session ; une seule constante de
  format partagée avec `ListeRoom`.
- Chaque `put` est précédé d'un `config get` de sondage : serveur sans format 2 et projet avec
  devices → refus sans envoi (« serveur trop ancien ») ; devices de la box absents du projet (ids,
  et devices sans id comptés) → confirmation qui les liste, Annuler par défaut.
- `put` refusé : le texte `error` du serveur est affiché ; sondage refusé : idem ; `DialogOpenOnline`
  sans fichiers : message au lieu d'un spinner infini. Chaînes fr traduites.

## Mesuré
`tst_onlineconfig` 27 cas ; 12 suites vertes ; CI 4/4. Revue en deux tours avec un vrai banc de bout
en bout (calaos_server master en conteneur, vrais dialogues hors écran, proxy d'enregistrement,
faux serveur ancien) : ouverture/enregistrement, sondage, serveur trop ancien, io.xml illisible,
projet sans device, confirmation Annuler/Oui — tous conformes.

## Contre-mutation
8 mutants tués (io_format retiré du put, format absent = 2, `error` non capturé, chaîne « 2 »
acceptée, seuil 1 au lieu de 2, diff des ids neutralisé, sans-id ignorés…).

## Reste ouvert
Clé de mémoire par texte d'hôte (une autre écriture du même hôte coûte un get de plus).
