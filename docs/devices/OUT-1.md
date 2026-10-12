# OUT-1 — Couverture bloquante sur les PR (calaos_base)

## Objectif
Toute ligne exécutable ajoutée ou modifiée sous `src/` (C/C++) doit être exécutée par `make check`,
sinon la PR est rouge. Le job liste les `fichier:ligne` non couverts ; une exclusion ne passe que
par `LCOV_EXCL_*` avec une raison, et leur nombre est affiché.

## Livré
PR calaos/calaos_base#178.
- `scripts/check-diff-coverage.py` (`--info`, `--base`/`--diff`, `--report-only`,
  `--print-lcov-remove` ; sorties 0/1/2).
- Job « Diff coverage » : bloquant sur `pull_request`, informatif sur `push`. Seul
  `core/IncomingLogStockLevel_test` (FIX-BASE-1) peut y échouer ; tout autre échec fait tomber le
  job.
- `scripts/gcov-exit-shim.c` : les processus qui sortent par `_exit()` perdaient leurs compteurs
  (`UDPServer.cpp` à 0/193).
- Auto-test `tests/check-diff-coverage-selftest.{sh,py}` dans `make check`.

## Mesuré
- `make check` : 176 tests, 173 PASS, 3 SKIP. Couverture globale 49,3 % → 53,0 % grâce au shim.
- Auto-test : 37 cas et 25 mutants permanents, chacun rougit son test désigné ; témoin CM0 :
  39 cas exécutés, 0 rouge.
- Rejouée sur l'historique de master, la porte aurait refusé 5 puis 10 lignes non couvertes.
- Revue (deux tours) : les deux contournements silencieux trouvés au premier tour sont fermés.

## Contre-mutation
25 mutants du script (dont `exit-always-0`, `only-first-hunk`, `hunk-body-not-consumed`,
`deleted-stop-goes-unnoticed`, `zero-hits-count-as-covered`), testés en permanence par
`make check`.

## Reste ouvert
- Protection de branche sur master à décider : sans elle, une PR rouge reste mergeable.
- Fichier CRLF : `\r` compte comme raison d'exclusion.
- Le test toléré peut masquer un échec hors test dans ce seul job (build-and-test le voit).
- Si `--print-lcov-remove` plante, la liste lcov est vide (rapport seulement).
- Python sous `src/` non mesuré ; processus tués par signal non comptés.
