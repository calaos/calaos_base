# Orchestration — état opérationnel & reprise

> Ce fichier est le **point d'entrée pour reprendre le travail** depuis n'importe quel
> Claude / n'importe quelle machine. Il est git-tracké : il voyage avec le repo.
> Lire dans l'ordre : ce bloc REPRISE → `DECISIONS.md` → `FINDINGS.md` → `BOARD.md`.

---

## 🔁 REPRISE — lire en premier

- **PHASE 2 COMPLÈTE (13/13, incl. T2.7 mergé antérieurement).** Phase 1 complète (19/19).
  Wave 5 terminée (2026-08-15) : T2.9, T2.3, T2.12, T2.6, T2.1, T2.10, T2.8, T2.4 ✅, puis
  file sérialisée T2.13 ✅, T2.11 ✅, T2.5 ✅, et enfin **T2.2 ✅ mergé** (2026-08-15, split
  Utils en 6 unités — Constants.h, MemMacros.h, LogSetup, StringUtils, ConfigStore,
  SystemInfo — pattern agrégateur, Utils.h ré-inclut tout ; 31/31 tests). **Rien en vol.**
  **Pas poussé** (origin = `9d8d37b5`). Les follow-ups T2.14-T2.18 restent 📋 en backlog.
- **WAVE 6 EN VOL** (lancée 2026-08-15) : 9 tickets Phase 3 en parallèle — T3.1, T3.2a,
  T3.2c, T3.2d, T3.2e, T3.2f, T3.3, T3.4, T3.5. Worktrees
  `/tmp/claude-1000/calaos-wave6/t3.X`, branches `refactor/t3.*`, base `82887cc0`.
  Contraintes de brief : **T3.2a garde son abstraction DANS IO/KNX/** (11 sous-classes, pas
  13) ; T3.2c/d/e/f = dédup LOCAL sans dépendre de T3.2a ; types XML (REGISTER_IO*) et ioDoc
  invariants ; T3.5 lignes recalées (`requestTimeout_cb` :468, `buffer_notif` :209-212) ;
  T3.3 Syno à :185, option conservatrice sur les items « policy » + flag validation ;
  T3.4 = base64.{cpp,h} + wrappers `StringUtils` SANS casser les signatures (pas d'édition
  de call sites). Suite de référence : 31/31. À la reprise : branches avec commits →
  revue (subagent) → merge (subagent, sérialisé) → board ✅ ; branches vides → relancer.
- **Note post-T2.2** : la préservation du local_config.xml corrompu (décision T2.4) vit
  désormais dans `ConfigStore.cpp` `loadConfigDocument()` (follow-up).
- **Restrictions de périmètre imposées aux agents wave 5** : T2.1 ne touche NI MySensors
  (supprimé par T2.12) NI Gpio (T2.13) NI WebSocket/Http* (T2.11) et ne modifie aucun call-site
  singleShot ; T2.9 = dead-code seulement (option usesIO-virtual différée) ; T2.8 évite
  src/lib/Makefile.am (T2.3 l'édite) — quarantaine par attribut no_sanitize de préférence.
- **À la reprise si les agents ont fini** : pour chaque branche `refactor/t2.*` avec commit
  au-dessus de `4808e23b` → revue (subagent) → merge (subagent, sérialisé, procédure ci-dessous)
  → board ✅. Branches vides → relancer.
- **Aussi en backlog** : câblage pytest dans make check ; UAF JsonApi buildJsonState (FINDINGS) ;
  13 alertes Dependabot.
- **Décisions récentes** (détail dans DECISIONS.md) : Wago = respawn infini backoff ≤5 s ;
  MQTT non-UTF8 = '?' ; OneWire = hex majuscules ; clamp OTA 30 j validé ; GPIO
  `debounce_time` câblé (T2.13 ✅) ; **MySensors = code mort à supprimer entièrement (T2.12,
  rend T3.2b obsolète)**. Plus aucune validation en attente.

---

## Rôle & mode de travail

**Claude ici = orchestrateur pur.** Toute implémentation, **toute revue de diff, tout merge et
toute investigation** se font en **subagent** ; le contexte principal ne reçoit que des
**verdicts compacts** (≤ ~20 lignes) : décisions, SHAs, statut de build, findings.

Règles dures :
- **Ne jamais lire un diff complet dans le contexte principal.** Un subagent « reviewer » lit
  le diff et renvoie : verdict (approve/reject) + findings + risques.
- **Merge délégué** : un subagent fait rebase + résolution conflit + build d'intégration + `ff-merge`
  et renvoie seulement `merged @SHA, N/N tests` ou `conflit à <fichier:ligne>`.
- **Investigations déléguées** : le subagent renvoie la conclusion, pas les lectures.
- **Ne jamais push sans demander** (chaque push brûle du crédit CI). Commit librement.
- **Builds subagent = synchrones** (une seule invocation Bash foreground, sinon l'agent stalle).
- Demander validation utilisateur sur tout changement de comportement / d'API.

## Workflow wave (rappel)

- Worktrees git depuis master local, un par ticket : `refactor/t1.x`, propriété **exclusive** des
  fichiers par ticket dans une wave (pas de recouvrement de fichiers entre tickets concurrents).
- Build/test docker (**synchrone**, timeout 600000) :
  ```
  docker run --rm -v <worktree>:/workspaces/calaos_base -w /workspaces/calaos_base \
    vsc-calaos_base-12022039c4b5f0e1b3db46145edacf81b99ac88513f5f47e81e91e6919b1be26:latest \
    bash -c "./autogen.sh && ./configure && make -j12 && make check"
  ```
- ASan : `CXXFLAGS="-g -O1 -fsanitize=address" LDFLAGS="-fsanitize=address"`,
  `ASAN_OPTIONS=detect_leaks=0`. **Échec pré-existant à IGNORER** : stack-use-after-scope
  `exprtk.hpp:15688` (tracké en T2.8 — voir FINDINGS).

## Procédure de merge (déléguée à un subagent)

1. Revue diff → verdict.
2. `git rebase master` dans le worktree.
3. Résoudre le conflit `tests/Makefile.am` (voir pattern ci-dessous).
4. Build docker d'intégration (attendre `N/N` verts, N = nb de tests courant).
5. `git merge --ff-only refactor/tX.Y` sur master.
6. Mettre à jour `BOARD.md` (statut → ✅) + commit.
7. Nettoyer worktree (artefacts root docker) :
   `docker run --rm -v <wave-dir>:/wt debian:12 rm -rf /wt/tX.Y` puis
   `git worktree prune` + `git branch -d refactor/tX.Y`.

### Pattern récurrent : conflit `tests/Makefile.am`

Chaque ticket **ajoute son propre bloc `# TX.Y`** avec son propre `if HAVE_GTEST … endif` en fin
de fichier. Quand deux tickets ajoutent tous deux en fin de fichier, git produit un conflit où le
`endif` du côté HEAD a été **consommé** par le marqueur `=======`. Résolution correcte :
supprimer `<<<<<<<`, **remplacer `=======` par `endif`** (rendre au côté HEAD son endif),
supprimer `>>>>>>>` (le côté entrant a déjà son endif). **Ne pas** transformer `>>>>>>>` en endif
(→ endif surnuméraire). **Toujours vérifier** après : `grep -c '^if HAVE_GTEST'` == `grep -c '^endif'`.

## Tickets Phase 1 (miroir compact de BOARD.md)

| Ticket | Titre | Statut |
|---|---|---|
| T1.1 | Rule/IO lifecycle (C1, C2 + scenario) | ✅ |
| T1.2 | Condition semantics (M1, M7) | ✅ |
| T1.3 | ListeRoom robustness (M6, m9, m10) | ✅ |
| T1.4 | JsonApi hardening (F6, F7, F3, F4, F2, F11 +) | ✅ |
| T1.5 | Transport & WS framing limits (F9, F10, F12 +) | ✅ |
| T1.6 | RemoteUI HMAC constant-time + dedup | ✅ |
| T1.7 | MCP token CSPRNG (F1) | ✅ |
| T1.8 | Python sidecar auth & quality (F5) | ✅ |
| T1.9 | ExternProc framing (F13 + sockfd) | ✅ |
| T1.10 | RemoteUI WebSocket/OTA lifecycle | ✅ |
| T1.11 | IOBase/IOFactory id integrity | ✅ |
| T1.12 | Utils CSPRNG/safety + tcpsocket | ✅ |
| T1.13 | LAN & Hue memory safety | ✅ |
| T1.14 | Reolink driver lifecycle & log hygiene | ✅ |
| T1.15 | Lua sandbox + exec watchdog | ✅ |
| T1.16 | MCP client + Roon Python robustness | ✅ |
| T1.17 | Extern-proc driver mains (Wago/OLA/OneWire/Mqtt) | ✅ |
| T1.18 | ActionMail/ActionPush dangling-this | ✅ |
| T1.19 | IO controllers (MySensors/Gpio/Web) | ✅ |

Backlog wave 4 (candidats) : **T1.8, T1.10, T1.14, T1.16, T1.17, T1.19** — majoritairement
Python / drivers, donc largement file-disjoints.
