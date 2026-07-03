# Board de refactoring Calaos Server

Board de suivi pour la distribution des tickets à des sous-agents de dev autonomes.
Chaque ticket est un fichier `doc/refactoring/<ID>.md`.

**Statuts** : `📋 Backlog` · `🔨 In Progress` · `👀 Review` · `✅ Done` · `⛔ Blocked`

> Règle anti-conflit : chaque ticket possède un jeu de fichiers **exclusif**. Deux tickets d'une même vague ne modifient jamais le même fichier.

---

## Context

`calaos_base` = serveur domotique Calaos, ~75 000 lignes C++14 (libuv/uvw, sigc++, jansson **et** nlohmann/json, luajit, SQLite) + drivers Python (MCP, Reolink, Roon) via IPC `ExternProc`. Code en production. Un audit en 3 volets (cœur/règles, réseau/IPC/sécurité, drivers/lib/tests/build) révèle une dette homogène :

- **Cause racine** : propriété par pointeurs bruts avec multiples alias non-propriétaires du même `IOBase*`/`Rule*` → bugs de cycle de vie (dangling/UAF au runtime).
- **Sécurité réseau** : path traversal, tokens PRNG faible, absence de limites taille/connexions (DoS pré-auth), comparaisons non constant-time, secrets loggués.
- **Duplication massive** + **double lib JSON**.
- **Filet quasi nul** : 3 tests unitaires, aucun sur le cœur, CI sans `make check`.

### Décisions retenues
- **Périmètre** : dépôt complet.
- **C++** : C++14 → **C++20** (build Arch officiel + `debian:12`/GCC 12 le supportent).
- **Ambition** : refactoring **+ fixes** (tickets séparés).
- **Infra qualité en Phase 0** (prérequis).

### Gouvernance (chaque ticket)
1. 1 ticket = 1 branche = les fichiers listés dans « Fichiers possédés » (pas d'édition hors périmètre).
2. Ordre des vagues strict : vague N+1 après merge de N. Tickets d'une vague = parallélisables.
3. DoD : `./autogen.sh && ./configure && make` OK, `make check` vert, 0 warning nouveau, tests ajoutés pour tout comportement corrigé.
4. Tickets « refactor » = iso-comportement ; tickets « fix » = test de non-régression obligatoire.
5. Pas de reformatage de masse hors ticket `.clang-format`.

---

## 🗂 Kanban

| 📋 Backlog | 🔨 In Progress | 👀 Review | ✅ Done |
|---|---|---|---|
| T0.1 T0.2 T0.3 T0.4 T0.5 | — | — | — |
| T1.1 … T1.9 | | | |
| T2.1 … T2.5 | | | |
| T3.1 T3.2a-f T3.3 T3.4 | | | |
| E4.1 E4.2 E4.3 | | | |

---

## 📊 Suivi

| ID | Phase | Titre | Type | Dépend de | Statut |
|---|---|---|---|---|---|
| [T0.1](T0.1.md) | 0 | CI build + `make check` | infra | — | 📋 |
| [T0.2](T0.2.md) | 0 | Nettoyage build & artefacts | infra | T0.1 | 📋 |
| [T0.3](T0.3.md) | 0 | Scaffolding tests cœur | infra | T0.1 | 📋 |
| [T0.4](T0.4.md) | 0 | Passage C++20 | infra | — | 📋 |
| [T0.5](T0.5.md) | 0 | clang-format + check CI | infra | T0.2 | 📋 |
| [T1.1](T1.1.md) | 1 | Cycle de vie règles/IO (C1,C2) | fix | Phase 0 | 📋 |
| [T1.2](T1.2.md) | 1 | Sémantique conditions (M1,M7) | fix | Phase 0 | 📋 |
| [T1.3](T1.3.md) | 1 | Robustesse ListeRoom (M6,m9,m10) | fix | Phase 0 | 📋 |
| [T1.4](T1.4.md) | 1 | Durcissement JsonApi (F6,F7,F3,F4,F2,F11) | fix | Phase 0 | 📋 |
| [T1.5](T1.5.md) | 1 | Limites transport & framing WS (F9,F10,F12) | fix | Phase 0 | 📋 |
| [T1.6](T1.6.md) | 1 | Auth RemoteUI constant-time (F2) | fix | Phase 0 | 📋 |
| [T1.7](T1.7.md) | 1 | Token MCP CSPRNG (F1) | fix | Phase 0 | 📋 |
| [T1.8](T1.8.md) | 1 | Sidecar Python (F5,+) | fix | Phase 0 | 📋 |
| [T1.9](T1.9.md) | 1 | Framing ExternProc (F13) | fix | Phase 0 | 📋 |
| [T2.1](T2.1.md) | 2 | Timer lifetime | refactor | Phase 1 | 📋 |
| [T2.2](T2.2.md) | 2 | Découpage Utils god-object | refactor | Phase 1 | 📋 |
| [T2.3](T2.3.md) | 2 | SHA1 → OpenSSL | refactor | Phase 1 | 📋 |
| [T2.4](T2.4.md) | 2 | Config robuste (m1-m6) | fix | Phase 1 | 📋 |
| [T2.5](T2.5.md) | 2 | UrlDownloader → libcurl | refactor | Phase 1 | 📋 |
| [T3.1](T3.1.md) | 3 | AVReceiver dédup | refactor | Phase 2 | 📋 |
| [T3.2a](T3.2a.md) | 3 | IO fines — KNX | refactor | Phase 2 | 📋 |
| [T3.2b](T3.2b.md) | 3 | IO fines — MySensors | refactor | Phase 2 | 📋 |
| [T3.2c](T3.2c.md) | 3 | IO fines — Mqtt | refactor | Phase 2 | 📋 |
| [T3.2d](T3.2d.md) | 3 | IO fines — Web | refactor | Phase 2 | 📋 |
| [T3.2e](T3.2e.md) | 3 | IO fines — Wago | refactor | Phase 2 | 📋 |
| [T3.2f](T3.2f.md) | 3 | IO fines — Gpio | refactor | Phase 2 | 📋 |
| [T3.3](T3.3.md) | 3 | IPCam cleanup | refactor | Phase 2 | 📋 |
| [T3.4](T3.4.md) | 3 | base64 durci | fix | Phase 2 | 📋 |
| [E4.1](E4.1.md) | 4 | Lib JSON unique | epic | Phase 3 | 📋 |
| [E4.2](E4.2.md) | 4 | Modèle de propriété (smart ptr/id) | epic | Phase 3 | 📋 |
| [E4.3](E4.3.md) | 4 | Couverture de tests | epic | Phase 0 | 📋 |

---

## Graphe des vagues

```
Phase 0  Fondations qualité      (séquentiel, bloque tout)
   │
Phase 1  Fixes critiques & sécu  (parallèle, fichiers disjoints)
   │
Phase 2  Refactors structurels   (large rayon → sérialisés ; T2.2 quasi-solo)
   │
Phase 3  Factorisation drivers   (parallèle par répertoire de famille)
   │
Phase 4  Epics long terme        (JSON unique, modèle de propriété)
```

## Quick wins à fort levier
1. **T0.1** CI `make check` (aucun test ne garde les merges aujourd'hui).
2. **T1.1** (UAF règles/IO) + **T1.4** (path traversal).
3. **T1.7** (token MCP CSPRNG) + **T1.5** (limites DoS pré-auth).
4. **T3.1** supprimer `AVRMarantz.cpp` (283 l. dupliquées, fichier entier).

## Vérification (par ticket & par vague)
1. **Build** : `./autogen.sh && ./configure && make` sans warning nouveau (+ `--with-mqtt --with-knx --with-owfs --with-ola` pour les drivers).
2. **Tests** : `make check` vert ; chaque ticket « fix » ajoute un test de non-régression.
3. **Sécurité (Phase 1)** : path traversal rejeté, login throttlé, token MCP 256 bits CSPRNG, frame WS > cap rejetée sans OOM.
4. **Fonctionnel** : serveur démarré avec `io.xml`/`rules.xml` réel, état des IO + exécution d'une règle vérifiés via API JSON (port 5454) ; driver `ExternProc` (MQTT/KNX) + sidecar MCP OK.
5. **Revue de vague** : `/code-review` sur le diff cumulé avant merge.
