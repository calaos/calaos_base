# Devices / climate — processus et reprise

## REPRISE — lire en premier

- **État (2026-10-13)** : outillage terminé (OUT-1 #178, OUT-2, OUT-3, FIX-INST-0, FIX-INST-1).
  CORE-1 ✅ (#180), CORE-5 ✅ (#179). FIX-BASE-1 (#181) corrigé, à pousser. CORE-2 en cours.
  Ensuite : CORE-2 (après CORE-1), puis CORE-3, CORE-4 et la suite du graphe.
- **Page de suivi en ligne** : voir la ligne « Suivi » plus bas. Elle est statique : la republier
  à chaque fin de ticket.
- **Bloquant connu** : DOC-1 attend T3.247 (board du refactoring), sans quoi régénérer
  `io_doc.json` réécrit en silence les adresses DMX 356 à 612.

## Cycle d'un ticket

1. **Démarrage** : statut 🔨 dans [`BOARD.md`](BOARD.md). L'agent d'implémentation rend le texte de
   la fiche `docs/devices/<ID>.md` dans son rapport ; l'orchestrateur la commite sur master (aucun
   agent ne touche `docs/devices/`, ce qui évite les conflits entre branches). Dans
   calaos_mobile et calaos-web-app, une ligne de renvoi vers ce board dans leur `docs/BOARD.md`.
2. **Implémentation** dans un worktree dédié sous `/home/raoul/repos/calaos/.wt/<id>` (disque ; `/tmp`
   est un tmpfs de 32 Go en RAM, un build de couverture y prend 12 Go et un build ASan ne tient pas), branche `feat/devices-<id>` (en minuscules), agent
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

## Autorisations (données le 2026-10-12, pour ce chantier)

- Pousser les branches `feat/devices-*` et ouvrir les PR **sans redemander**.
- **Merger soi-même** une fois la CI verte et la revue close, y compris dans calaos-web-app
  (dont le `CLAUDE.md` impose sinon des commits directs sur master).

## Environnements de build — rien ne s'installe sur la machine hôte

| Dépôt | Où builder |
|---|---|
| calaos_base | conteneur de l'image devcontainer `vsc-calaos_base-8a52b90d…:latest` (worktree monté, `-u $(id -u):$(id -g)` pour ne pas laisser de fichiers root) |
| calaos_remote_ui | conteneur construit depuis `.devcontainer/Dockerfile` du dépôt (ESP-IDF) ; build `linux-dev` et tests hôte dedans |
| calaos_installer, calaos_mobile | sur l'hôte (Qt 6 installé) |
| calaos-web-app | sur l'hôte (node/npm et navigateurs Playwright déjà présents) ; `npm ci` reste local au worktree |

## Règles

- **Revue** : l'agent qui implémente ne relit jamais son propre diff.
- **Builds** : au plus quatre agents qui buildent calaos_base en même temps. Pour arrêter un
  conteneur, toujours le désigner par son montage de worktree, jamais par image.
- **Commits** : un commit `test:` avant le commit `feat:`/`fix:` quand le test prouve un défaut.
  Commits `docs:` séparés.
- **`tests/Makefile.am`** : deux branches qui ajoutent chacune un bloc `if HAVE_GTEST` en fin de
  fichier partagent le même `endif` final. Après un rebase, régénérer la queue du fichier et
  recompter les `if`/`endif`. Le détail est dans `docs/refactoring/ORCHESTRATION.md`.
- **`gh pr checks`** : le hook rtk réécrit sa sortie et son résumé peut être faux (« 131 passed /
  41 pending » pour 4 checks). Lire le statut réel avec `rtk proxy gh pr checks <n>`.
- **Spec** : un écart découvert pendant l'implémentation est remonté à l'utilisateur, pas contourné
  en silence.

## Suivi

- Board : `docs/devices/BOARD.md` (ce dossier).
- Page en ligne : https://claude.ai/code/artifact/3ef10e21-0b16-4d5c-8dfc-ddece4cd2058 (Claude Doc, mise à jour par les outils `Claude_Docs`).
