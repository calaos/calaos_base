# FIX-BASE-2 — Port réservé volé par un test parallèle (calaos_base)

## Objectif
`core/ListenAddressFallback_test` instable en CI (run 38346076560) ; rendre la réservation de port
sûre sous `make check -j`.

## Livré
PR calaos/calaos_base#189.
- Cause : l'ancien `reservePort()` faisait `bind(0)` puis `close`, rendant le port au pool
  éphémère où piochent les `connect()`/`bind(0)` des tests parallèles (vol reproduit 3 fois sur 300).
- `tests/core/ReservedPort.h` : port choisi hors du pool éphémère (lu dans
  `/proc/sys/net/ipv4/ip_local_port_range`), revendiqué par un `flock` par port gardé ouvert, puis
  vérifié réellement ouvrable du bon type (sinon port suivant) ; appliqué aux 9 tests concernés
  (UDP en `SOCK_DGRAM`).
- `core/ReservedPort_test` (5 cas).

## Mesuré
2 000 exécutions parallèles (`-P16`) sans échec ; revue : 5 tours × 10 tests en parallèle sans
échec ; CI verte.

## Contre-mutation
Sans objet (aucun code `src/` modifié) ; le test de prémisse prouve que l'ancien schéma tombait dans
le pool éphémère.

## Reste ouvert
Fichiers `/tmp/calaos-test-port-N.lock` vides laissés en place (les supprimer ouvrirait une course).
