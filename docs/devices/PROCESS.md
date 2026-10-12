# Devices / climate — processus et reprise

## REPRISE — lire en premier

- **État (2026-10-12)** : spec validée, board créé, aucun ticket démarré. Les premiers démarrables
  sans dépendance sont OUT-1, OUT-2 et OUT-3, puis CORE-5 après OUT-1.
- **Page de suivi en ligne** : voir la ligne « Suivi » plus bas. Elle est statique : la republier
  à chaque fin de ticket.
- **Bloquant connu** : DOC-1 attend T3.247 (board du refactoring), sans quoi régénérer
  `io_doc.json` réécrit en silence les adresses DMX 356 à 612.

## Cycle d'un ticket

1. **Démarrage** : statut 🔨 dans [`BOARD.md`](BOARD.md), fiche `docs/devices/<ID>.md` créée par
   l'agent d'implémentation (objectif, périmètre, fichiers, ce que les tests prouvent). Dans
   calaos_mobile et calaos-web-app, une ligne de renvoi vers ce board dans leur `docs/BOARD.md`.
2. **Implémentation** dans un worktree dédié, branche `feat/devices-<id>` (en minuscules), agent
   configuré selon la colonne « Agent » du board.
3. **Validation locale** avant toute PR :
   - calaos_base : `./autogen.sh && ./configure && make -j32 && make check -j16` (un seul agent qui
     builde ; `-j16` / `-j8` à partir de 3 agents), build lancé en **un seul appel synchrone** ;
   - les autres dépôts : la commande de test de leur README ou CLAUDE.md.
4. **Tests** : tout le code nouveau est couvert.
   - calaos_base : la porte OUT-1 vérifie la couverture différentielle. Les goldens sont régénérés
     (`CALAOS_GOLDEN_UPDATE=1`), jamais écrits à la main. Chaque fiche porte une section de
     contre-mutation au format du refactoring : un témoin à zéro rouge, puis mutant → noms des
     tests rouges.
   - Les autres dépôts : tests unitaires plus, pour l'UI, un test de composant ou e2e.
5. **PR** sur GitHub, description qui renvoie à la fiche ; la CI doit être verte.
6. **Revue** par un agent séparé (opus / high, skill `code-review`) sur le diff de la PR. Ses
   remarques bloquantes sont corrigées dans la même PR.
7. **Merge** une fois CI verte et revue close.
8. **Clôture**, dans le même commit `docs:` que la mise à jour du statut :
   - `BOARD.md` : statut ✅ et lien vers la PR ;
   - la fiche : ce qui a été livré et mesuré ;
   - ce bloc REPRISE : état et prochain ticket démarrable ;
   - la page de suivi en ligne, republiée ;
   - la spec, si l'implémentation a tranché un point ouvert (§14).

## Règles

- **Revue** : l'agent qui implémente ne relit jamais son propre diff.
- **Builds** : au plus quatre agents qui buildent calaos_base en même temps. Pour arrêter un
  conteneur, toujours le désigner par son montage de worktree, jamais par image.
- **Commits** : un commit `test:` avant le commit `feat:`/`fix:` quand le test prouve un défaut.
  Commits `docs:` séparés.
- **`tests/Makefile.am`** : deux branches qui ajoutent chacune un bloc `if HAVE_GTEST` en fin de
  fichier partagent le même `endif` final. Après un rebase, régénérer la queue du fichier et
  recompter les `if`/`endif`. Le détail est dans `docs/refactoring/ORCHESTRATION.md`.
- **Spec** : un écart découvert pendant l'implémentation est remonté à l'utilisateur, pas contourné
  en silence.

## Suivi

- Board : `docs/devices/BOARD.md` (ce dossier).
- Page en ligne : https://claude.ai/code/artifact/3ef10e21-0b16-4d5c-8dfc-ddece4cd2058 (Claude Doc, mise à jour par les outils `Claude_Docs`).
