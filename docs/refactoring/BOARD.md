# Calaos Server — Refactoring Board

Tracking board for distributing refactoring tickets to autonomous dev sub-agents.
Each ticket is a file `docs/refactoring/<ID>.md`.

> **Ce qui change pour les utilisateurs** : [`RELEASE_NOTES.md`](RELEASE_NOTES.md).
>
> **Phase 4 ?** Lire [`PHASE4.md`](PHASE4.md) (cartographie + découpage en sous-tickets).
>
> **Reprendre le travail ?** Commencer par [`ORCHESTRATION.md`](ORCHESTRATION.md) (état + prochaine
> action + protocoles), puis [`DECISIONS.md`](DECISIONS.md) (choix utilisateur) et
> [`FINDINGS.md`](FINDINGS.md) (backlog de découvertes hors-périmètre).

**Status:** `📋 Backlog` · `🔨 In Progress` · `👀 Review` · `✅ Done` · `⛔ Blocked`

**Anti-conflict rule:** within a single wave, every ticket owns an **exclusive** set of files. Two tickets in the same wave never modify the same file. Cross-phase edits to the same file are allowed only when serialized into different waves (see the graph).

---

## Context

`calaos_base` is the Calaos home-automation server: ~75k lines of C++14 (libuv/uvw, sigc++, **two** JSON libraries — jansson **and** nlohmann/json — luajit, SQLite) plus Python drivers (MCP sidecar, Reolink, Roon) over the `ExternProc` Unix-socket IPC. This is production code.

A three-part audit (core/rules, network/IPC/security, drivers/lib/tests/build), re-verified against the current tree and enriched by a 10-subsystem sweep, revealed homogeneous technical debt:

- **Root cause:** ownership by raw pointers with multiple non-owning aliases of the same `IOBase*`/`Rule*` → lifecycle bugs (dangling / use-after-free at runtime). Confirmed criticals: `RemoteUIWebSocketHandler` post-auth raw-`this` timer, `HueOutputLightRGB` polling timer with an empty destructor, `ActionMail`/`ActionPush` dangling-this download callbacks.
- **Network security:** path traversal, weak-PRNG tokens, no size/connection caps (pre-auth DoS), non-constant-time comparisons, secrets logged before auth.
- **Correctness / edge cases:** off-by-one heap overflow in ping, iterator-invalidation UB in Onkyo reassembly, absent Lua sandbox, `exit(-1)` on malformed XML, many unvalidated inputs.
- **Massive duplication** and a **dual JSON library**.
- **Almost no safety net:** 3 unit tests (none on the core), CI without `make check`.

### Decisions taken
- **Scope:** the whole repository.
- **C++:** C++14 → **C++20** (official Arch build + `debian:12`/GCC 12 support it).
- **JSON:** converge on nlohmann/json (already vendored).
- **Crypto:** reuse OpenSSL (already a hard dependency) instead of bundled SHA1 / weak PRNG.
- **Backlog language:** English (this board replaces the earlier French draft; git history retains it).

---

## Board

| Wave 0a / 0b | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|---|---|---|---|---|
| T0.1 T0.4 T0.6 · T0.2 T0.3 T0.5 | T1.1 … T1.19 | T2.1 … T2.7 | T3.1 T3.2a-f T3.3 T3.4 T3.5 | E4.1 E4.2 E4.3 E4.4 |

---

## Tracking

| ID | Phase | Title | Type | Depends on | Status |
|---|---|---|---|---|---|
| [T0.1](T0.1.md) | 0 | CI build + `make check` | infra | — | ✅ |
| [T0.2](T0.2.md) | 0 | Build & artifact cleanup (tests) | infra | T0.1 | ✅ |
| [T0.3](T0.3.md) | 0 | Core test scaffolding | infra | T0.1 | ✅ |
| [T0.4](T0.4.md) | 0 | C++20 switch + configure cleanup | infra | — | ✅ |
| [T0.5](T0.5.md) | 0 | clang-format + CI check (diff only) | infra | T0.2 | ✅ |
| [T0.6](T0.6.md) | 0 | Docker/build hardening + dead scripts | infra | — | ✅ |
| [T1.1](T1.1.md) | 1 | Rule/IO lifecycle (C1, C2 + scenario) | fix | Phase 0 | ✅ |
| [T1.2](T1.2.md) | 1 | Condition semantics (M1, M7) | fix | Phase 0 | ✅ |
| [T1.3](T1.3.md) | 1 | ListeRoom robustness (M6, m9, m10) | fix | Phase 0 | ✅ |
| [T1.4](T1.4.md) | 1 | JsonApi hardening (F6, F7, F3, F4, F2, F11 + cover) | fix (sec) | Phase 0 | ✅ |
| [T1.5](T1.5.md) | 1 | Transport & WS framing limits (F9, F10, F12 +) | fix (sec) | Phase 0 | ✅ |
| [T1.6](T1.6.md) | 1 | RemoteUI HMAC constant-time + dedup | fix (sec) | Phase 0 | ✅ |
| [T1.7](T1.7.md) | 1 | MCP token CSPRNG (F1) | fix (sec) | Phase 0 | ✅ |
| [T1.8](T1.8.md) | 1 | Python sidecar auth & quality (F5) | fix (sec) | Phase 0 | ✅ |
| [T1.9](T1.9.md) | 1 | ExternProc framing (F13 + sockfd) | fix | Phase 0 | ✅ |
| [T1.10](T1.10.md) | 1 | RemoteUI WebSocket/OTA lifecycle | fix | Phase 0 | ✅ |
| [T1.11](T1.11.md) | 1 | IOBase/IOFactory id integrity | fix | Phase 0 | ✅ |
| [T1.12](T1.12.md) | 1 | Utils CSPRNG/safety + tcpsocket | fix (sec) | Phase 0 | ✅ |
| [T1.13](T1.13.md) | 1 | LAN & Hue memory safety | fix | Phase 0 | ✅ |
| [T1.14](T1.14.md) | 1 | Reolink driver lifecycle & log hygiene | fix | Phase 0 | ✅ |
| [T1.15](T1.15.md) | 1 | Lua sandbox + exec watchdog | fix (sec) | Phase 0 | ✅ |
| [T1.16](T1.16.md) | 1 | MCP client + Roon Python robustness | fix | Phase 0 | ✅ |
| [T1.17](T1.17.md) | 1 | Extern-proc driver mains (Wago/OLA/OneWire/Mqtt) | fix | Phase 0 | ✅ |
| [T1.18](T1.18.md) | 1 | ActionMail/ActionPush dangling-this | fix | Phase 0 | ✅ |
| [T1.19](T1.19.md) | 1 | IO controllers (MySensors/Gpio/Web) | fix | Phase 0 | ✅ |
| [T2.1](T2.1.md) | 2 | Timer lifetime | refactor | Phase 1 | ✅ |
| [T2.2](T2.2.md) | 2 | Utils god-object split | refactor | Phase 1, T1.12 | ✅ |
| [T2.3](T2.3.md) | 2 | SHA1 → OpenSSL | refactor | Phase 1 | ✅ |
| [T2.4](T2.4.md) | 2 | Config robustness (m1–m6 + cache) | fix | Phase 1 | ✅ |
| [T2.5](T2.5.md) | 2 | UrlDownloader → libcurl | refactor | Phase 1 | ✅ |
| [T2.6](T2.6.md) | 2 | Common-lib correctness (ThreadedQueue/ColorUtils/Calendar/Params) | fix | Phase 1 | ✅ |
| [T2.7](T2.7.md) | 2 | ~~NTPClock timer lifetime~~ (resolved by deletion, see plan step 2) | fix | Phase 1, T2.1 | ✅ |
| [T3.1](T3.1.md) | 3 | AVReceiver correctness & dedup | fix+refactor | Phase 2 | ✅ |
| [T3.2a](T3.2a.md) | 3 | Thin IO subclasses — KNX | refactor | Phase 2 | ✅ |
| [T2.8](T2.8.md) | 2 | exprtk stack-use-after-scope (ASan, préexistant) | fix | Phase 1 | ✅ |
| [T2.9](T2.9.md) | 2 | ListeRule dead code updateAllRulesTo* | refactor | T1.1 | ✅ |
| [T2.10](T2.10.md) | 2 | UrlDownloader lifecycle (cancel, pipe, buffering) | fix (sec) | T1.4 | ✅ |
| [T2.11](T2.11.md) | 2 | Transport hardening compléments (431, cap/IP, config) | fix (sec) | T1.5 | ✅ |
| [T2.12](T2.12.md) | 2 | Remove MySensors entirely (dead code, user decision) | removal | Phase 1 | ✅ |
| [T2.13](T2.13.md) | 2 | GPIO: honor `debounce_time` from config | fix | T1.19 | ✅ |
| [T2.14](T2.14.md) | 2 | Wire pytest suites into `make check` | infra | T0.3 | ✅ |
| [T2.15](T2.15.md) | 2 | JsonApi audio-state UAF/leak + RemoteUI token constant-time | fix (sec) | T1.4 | ✅ |
| [T2.16](T2.16.md) | 2 | HttpClient keep-alive request_headers leak | fix (sec) | T2.11 | ✅ |
| [T2.17](T2.17.md) | 2 | UrlDownloader TLS verification by default | fix (sec) | T2.5 | ✅ |
| [T2.18](T2.18.md) | 2 | createIO null-guard audit (JsonApi/AutoScenario) | fix | T1.11 | ✅ |
| [T2.19](T2.19.md) | 2 | Per-IO `insecure` option for Web/Lua (user decision) | feature | T2.17 | ✅ |
| [T3.2b](T3.2b.md) | 3 | ~~Thin IO subclasses — MySensors~~ (obsolete: removal via T2.12) | refactor | T2.12 | ⛔ |
| [T3.2c](T3.2c.md) | 3 | Thin IO subclasses — Mqtt | refactor | Phase 2 | ✅ |
| [T3.2d](T3.2d.md) | 3 | Thin IO subclasses — Web | refactor | Phase 2 | ✅ |
| [T3.2e](T3.2e.md) | 3 | Thin IO subclasses — Wago | refactor | Phase 2 | ✅ |
| [T3.2f](T3.2f.md) | 3 | Thin IO subclasses — Gpio | refactor | Phase 2 | ✅ |
| [T3.3](T3.3.md) | 3 | IPCam cleanup + Synology snapshot fix | fix+refactor | Phase 2 | ✅ |
| [T3.4](T3.4.md) | 3 | base64 hardening | fix | Phase 2, T2.2 | ✅ |
| [T3.5](T3.5.md) | 3 | Squeezebox correctness & lifetime | fix | Phase 2 | ✅ |
| [T3.6](T3.6.md) | 3 | Remove Gadspot driver (dead code, user decision) | removal | T3.3 | ✅ |
| [T3.7](T3.7.md) | 3 | ExternProc pid-0 group-kill guard | fix | — | ✅ |
| [T3.8](T3.8.md) | 3 | AVRRose/NotifServer async lifetimes | fix | T3.1 | ✅ |
| [T3.9](T3.9.md) | 3 | pid-0 kill guard: remaining sites | fix | T3.7 | ✅ |
| [T3.10](T3.10.md) | 3 | Promote ThinIo + harmonize driver mixins | refactor | T3.2a-f | ✅ |
| [T3.11](T3.11.md) | 3 | Hygiene: SHA1/curl residue, npm lockfile | cleanup | T2.3, T2.5 | ✅ |
| [T3.22](T3.22.md) | 3 | Dependabot : déclarer le manifeste **pip** `src/bin/calaos_mcp` (aujourd'hui non surveillé — seul npm/`data/debug` l'est) **et** poser une stratégie `groups:`, parce que chaque merge dans `master` est une **publication** (`docker-publish-dev.yml` sans `needs:`) : une PR par paquet = un cycle tag+image+`build_deb` par paquet. Prérequis de fond : **F-DEP-1** (le `Dockerfile` installe ces dépendances **non pinnées**, donc `pyproject.toml` ne décrit pas ce qui est déployé) | cleanup | T3.11, F-DEP-1 | 📋 |
| [T3.23](T3.23.md) | 3 | **Le sidecar MCP ne démarrait pas dans l'image publiée** (`mcp 2.0.0`, `mcp.server.fastmcp` disparu — `Dockerfile:38`/`:69` installaient les dépendances **non pinnées**). `src/bin/calaos_mcp/pyproject.toml` devient la **source unique** : les deux stages du `Dockerfile`, le devcontainer et un job CI neuf (`mcp-sidecar-deps`) l'installent via `scripts/pyproject-requirements.py`, en **une seule passe de résolveur** (mcp/fastapi/starlette sont couplés). `configure.ac` sonde désormais l'**API réellement importée**, relevée sur les sources (+`starlette`, **`websockets`** — jamais sondé ; `pydantic` **délibérément retiré**, son seul importeur `models.py` est du code mort) et **rejoue la séquence de `server.py`** jusqu'à l'API **privée** `mcp._session_manager`, au lieu de `import mcp, uvicorn, fastapi` qui passait avec mcp 2.0.0. Négatif mesuré sur l'image cassée : ancienne sonde **exit 0**, nouvelle **exit 1**. La sonde reste **non fatale** (`AC_MSG_WARN`) : sans les modules, `HAVE_PYTHON_MCP=no` et `./configure` passe. **Image reconstruite et sidecar démarré dedans** : `/healthz` 200, `POST /mcp` 401 sans Bearer / 200 avec, pins honorés au numéro près, stages `dev` et `runner` identiques. Corrige **F-DEP-1**, prérequis de **T3.22**. ✅ **Le `.deb` est couvert** (vérifié) : `build_deb.yml` produit un simple wrapper podman (`ExecStart=… podman run --pull=never ${IMAGE_SRC}`, image tirée au `postinst`) — il n'embarque **ni image ni site-packages**, corriger le `Dockerfile` suffit. ⚠️ **Détection, pas blocage** : `docker-publish-dev.yml` est un workflow séparé sans `needs:` et `master` n'est pas protégée → un job rouge ne bloque aucune publication (décision de configuration de dépôt). Ouverts : **F-DEP-2..F-DEP-7** | fix | F-DEP-1 | ✅ |
| [E4.0](E4.0.md) | 4 | Caractérisation de l'API JSON (préalable dur à E4.1) — **7/7 sous-tickets livrés (a→g)** : 6 de caractérisation + le correctif structurel du harnais. **Épique close, E4.1 débloquée** | epic | — | ✅ |
| [E4.0a](E4.0.md) | 4 | Fondation du filet de caractérisation (harnais in-process, oracle sémantique, goldens) | tests | — | ✅ |
| [E4.0b](E4.0.md) | 4 | Caractérisation du modèle et de l'état (9 commandes × 2 transports) | tests | E4.0a | ✅ |
| [E4.0c](E4.0.md) | 4 | Caractérisation des plages horaires et autoscénarios (9 opérations, 52 cas, 9 goldens) | tests | E4.0a | ✅ |
| [E4.0d](E4.0.md) | 4 | Caractérisation des events temps réel (23 `type_str`, numérotation de l'enum, enveloppe WS, `poll_listen`, 57 cas, 15 goldens) | tests | E4.0a | ✅ |
| [E4.0e](E4.0.md) | 4 | Caractérisation de la session, des enveloppes et des chemins d'erreur (7 refus `scopeDenied`, silences, 400/404, pièges de bascule, 102 cas, 29 goldens) | tests | E4.0a | ✅ |
| [E4.0f](E4.0.md) | 4 | Caractérisation du payload audio et base musicale (contrat de `processDbResult()`, `Utils::to_string(double)`, asymétrie WS/HTTP, 24 cas, 20 goldens) **+ réécriture de `docs/08_http_api.md` et `docs/10_events_notifications.md`** | tests | E4.0a | ✅ |
| [E4.0g](E4.0g.md) | 4 | Harnais : drainer les events de destruction du fixture (`pumpEventLoop()` **après** `CoreFixture::TearDown()`) — 6 rustines retirées, contrat de file vide posé | tests | E4.0d, E4.0e, E4.0f | ✅ |
| [E4.1](E4.1.md) | 4 | Single JSON library — **découpée (2026-08-24) en 17 sous-tickets `a`→`x`, 10 vagues** ; vague 1 = 10 tickets parallèles, vagues 2-9 = chaîne API **sérialisée** (même 4 fichiers). **`a` ✅ : le pont `Params` est COUPÉ** (`Params.h` n'inclut plus jansson) et la dette transitoire est **bornée à 99 appels de `jansson_from_params()` dans 13 fichiers** — `grep -rn jansson_from_params src tests` **est** la liste exacte de ce que b→x doivent convertir ; `Jansson_Addition.h` disparaît quand elle est vide. ⛔ **La clôture (`E4.1x`, `configure.ac`) attend E4.6b/E4.6d** à cause de l'exclusion `IO/Scenario.cpp`. 5 questions ouvertes en fin de `E4.1.md` | epic | ~~**E4.0**~~ ✅ satisfait | 📋 |
| [E4.6](E4.6.md) | 4 | **AutoScenario : refonte complète (API + modèle interne)** — rupture assumée, aucun consommateur (user decision 2026-08-24). Absorbe T3.20. **1/8 livré (a)** ; b→h restent, ⛔ **après E4.1** (plus de jansson dans le code neuf) | epic | E4.1 | 📋 |
| [E4.6a](E4.6.md) | 4 | Caractérisation du modèle AutoScenario avant la refonte (`core/AutoScenarioMigration_test`) : le balayage orphelin (`ListeRoom.cpp:324`) **détruit définitivement** les règles d'un scénario démarqué (`checkScenarioRules()` ne les recrée pas — seule création `AutoScenario.cpp:859`, API-only) ; le cas d'usage de référence (`get`→`modify`→`reenable` relance un scénario amputé, deux `success:true`) ; l'amputation muette par rechargement de config ; la sauvegarde **avant** écrasement de `config put` ; les **quatre états** `disabled`/`disabled_missing_io`/`broken` distingués (trou trouvé en revue, comblé) ; renumérotation partielle (3 des 4 numérotations, `auto_scenario_step` jamais réécrit). **Tests seuls, zéro `src/`, zéro golden touché** | tests | E4.6 | ✅ |
| [E4.6b](E4.6.md) | 4 | Le modèle : `AutoScenarioDef` (étapes à id opaque, actions par **id d'IO**), **encodé dans les params de l'IO scénario** au sein d'`io.xml` (user decision — invariant « deux fichiers ») ; codec avec percent-encoding, marqueur `autoscenario_uid`, uid non recyclable. **Aucun nouveau fichier de config** | refactor | E4.6a | 📋 |
| [E4.6c](E4.6.md) | 4 | Le générateur : `rebuildRules()` détruit-puis-régénère depuis la définition. Retraits — pattern-matching, `RuleRef`/`isDangling()`, `purgeDeadSteps()`, `stepRuleDestroyed`, `END_STEP`, back-pointers, balayage orphelin, `auto_sc_mark` (Rule+IOBase), `IOBase::ascenario`, `get_new_scenario_id()` | refactor | E4.6b | 📋 |
| [E4.6d](E4.6.md) | 4 | La surface d'API : payload **symétrique** (ce que `get` rend = ce que `modify` accepte), `final_step` séparé (fin de `steps_count + 1`), actions **jamais escamotées** (`resolved`), validation **avant** mutation. 6 goldens `e40c_*` régénérés | refactor | E4.6c | 📋 |
| [E4.6e](E4.6.md) | 4 | Dispatch : `autoscenario` sous `scopeDenied()` (asymétrie E4.0c #3) ; sous-commande inconnue → **erreur** au lieu du silence, socket HTTP plus laissée ouverte | fix | E4.6c | 📋 |
| [E4.6f](E4.6.md) | 4 | Périphérie : marqueur re-clé dans `buildJsonIO()` (`JsonApi.cpp:261`), alerte de configuration re-clée (`CalaosConfig.cpp:422`). 4 goldens `get_home`/`get_io` régénérés | fix | E4.6d | 📋 |
| [E4.6h](E4.6.md) | 4 | Défense en profondeur : prouver/documenter la sauvegarde avant écrasement (`JsonApiHandlerHttp.cpp:624`, déjà en place), **détecter et alerter** qu'un téléversement a fait disparaître un scénario connu. ⛔ jamais de refus | fix | E4.6c | 📋 |
| [E4.6g](E4.6.md) | 4 | Doc : `docs/04_scenarios.md` et `docs/03_rules_engine.md` réécrits contre le modèle neuf (E4.5b les avait écrits contre l'ancien) + `RELEASE_NOTES.md` | doc | E4.6d, E4.6e | 📋 |
| [E4.5](E4.5.md) | 4 | Remettre `docs/*.md` en phase avec le code (user request) — **6/6 livrés (a→f)**. Bilan : la doc n'était pas obsolète, elle était **fausse** — noms de paramètres erronés pour **tous** les drivers (une config écrite d'après la doc était sans effet), **5 types XML de caméra fantômes** (aucune caméra ne se chargeait), **14 des 15 fonctions Lua inexistantes**, `Timer` documenté à l'envers, `relay_num` 1-based donné pour 0-based, framing `ExternProc` inventé, exemple MQTT fabriqué, et **36 groupes de références sur 329** ne pointant plus sur rien dans `08` (jusqu'à **190 lignes d'écart**) | epic (doc) | — | ✅ |
| [E4.5a](E4.5.md) | 4 | `docs/00_overview`, `docs/01_core_data_model`, `docs/11_config_persistence` réécrits contre le code : ownership E4.2a→f, pugixml (E4.4), robustesse config T2.4, fidélité XML (E4.4cd) ; jansson laissé tel quel (E4.1 non faite) | doc | E4.5 | ✅ |
| [E4.5b](E4.5.md) | 4 | `docs/03_rules_engine`, `docs/04_scenarios` réécrits contre le code et les goldens : règle désactivée (E4.2e), scénario désactivé + drapeau collant + `autoscenario reenable` + 3 clés de payload (T3.18), plages nocturnes wrappantes (T3.13), invariant `len(steps) == steps_count + 1` | doc | E4.5 | ✅ |
| [E4.5c](E4.5.md) | 4 | `docs/02_io_drivers`, `docs/05_audio`, `docs/06_ipcam` réécrits contre le code — ce n'était pas de l'obsolescence mais de la **fausseté** : **presque tous les noms de paramètres des drivers étaient faux** (MQTT `topic`/`topic_set` → `topic_sub`/`topic_pub`, GPIO `gpio_number`/`inverted` → `gpio`/`active_low`, KNX `address`/`feedbackAddress`/`datatype` → `knx_group`/`listen_knx_group`/`eis`, Hue `api_key`/`light_id` → `api`/`id_hue`, Squeezebox `playerid` → `id`) — une config écrite d'après la doc était **sans effet** ; les **5 types XML de caméra étaient fantômes** (0 occurrence) donc **aucune caméra ne se chargeait** ; `relay_num` est **1-based**. Drivers retirés (T2.12, T3.6), `insecure` par IO (T2.19), `debounce_time` (T2.13), OneWire (T1.17), Synology (T3.3), refus `audio_db` (T3.19). ~95 affirmations rouvertes au source sur **252 références mécaniquement contrôlées** ; revue : **24/24** contrôles byte-identiques rejoués. **`RELEASE_NOTES.md` corrigé** : pour MySensors/Gadspot retirés on annonçait « la config démarre, l'IO inconnu est ignoré » — vrai au boot mais rassurant à tort, `main.cpp:196` planifie `checkAutoScenario()` **0,1 s après le boot** et celle-ci finit par `SaveConfigIO()`, donc **un simple redémarrage efface ces entrées d'`io.xml`** | doc | E4.5 | ✅ |
| [E4.5d](E4.5.md) | 4 | `docs/12_extern_proc`, `docs/14_python_extern_proc`, `docs/15_mcp_server` réécrits contre le code et les goldens : le **framing `ExternProc`** documenté annonçait un octet `START = 0x02` **inexistant** et une longueur sur **2 octets** au lieu de 4 (réel : opcode `0x21` + longueur 4 octets big-endian, plafond 4 MiB) ; l'**exemple MQTT était inventé de bout en bout** ; auth du sidecar Python et identité X-Forwarded-For (T1.8), respawn Wago (décision utilisateur), portée `serviceScope` qui reçoit **tous** les events (E4.0d). **~95 affirmations rouvertes au source** ; revue : **19/19** contrôles byte-identiques rejoués | doc | E4.5 | ✅ |
| [E4.5e](E4.5.md) | 4 | `docs/07_remoteui`, `docs/09_lua_scripting`, `docs/13_utility_lib`, `docs/README` réécrits contre le code : **14 des 15 fonctions Lua documentées n'existent pas** (la table en porte **11**, en camelCase) et **`Timer` était documenté à l'envers** (il est **répétitif** et n'a **pas** de `stop()`) ; `device_info` (T3.15), sandbox + watchdog (T1.15), split `Utils` (T2.2), SHA1 → OpenSSL (T2.3). **`16_config_options.md` délibérément non touché** : le relecteur a **exécuté** les deux scripts de garde — `check-config-docs` → « matches the generator », `check-config-options` → « 52 keys used, 57 in registry, 5 obsolete » — ce qui **prouve par exécution** qu'il est à jour. ~80 affirmations rouvertes au source ; revue : **16/16** contrôles byte-identiques rejoués | doc | E4.5 | ✅ |
| [E4.5f](E4.5.md) | 4 | `docs/08_http_api`, `docs/10_events_notifications` — **vérification, pas réécriture** : E4.0f les avait écrits contre le code et le fond tenait ; ce qui les avait périmés est T3.18/T3.19 et surtout la **dérive des numéros de ligne** — **36 groupes de références sur 329** ne pointaient plus sur ce qu'ils annonçaient, jusqu'à **190 lignes d'écart** dans `JsonApi.cpp`, sans qu'aucune ne rougisse. ⭐ La revue a trouvé un **bloquant** : la section « rien n'est retiré du payload » était une **inversion** — `Scenario::toJson()` fait `if (!sa.io) continue;` (`IO/Scenario.cpp:158` et `:181`) et **le golden que la section citait elle-même le prouvait** (2 actions à l'étape 2 dans `e40c_ws_autoscenario_get.json`, 1 dans `..._get_broken.json`, le test s'appelant littéralement `ABrokenStepSilentlyLosesItsActionFromThePayload`). Seconde réserve de la même famille : `WebSocket.cpp:314-315`, référence **existante et plausible** (elle contient bien un `429`) mais qui désigne l'**authentification RemoteUI**, pas le plafond par IP — lequel passe par `HttpClient.cpp:273-281`. ~60 affirmations rouvertes au source ; revue : **23/23** contrôles byte-identiques rejoués. Bugs de code découverts en écrivant la doc versés en FINDINGS, **aucun corrigé** (throttle de login indexant le pair TCP sur les deux transports → `T3.24`) | doc | E4.5 | ✅ |
| [E4.1a](E4.1a.md) | 4 | `Params` : **pont dual-API coupé** — `Params.h` n'inclut plus `<jansson.h>`, la classe n'expose plus qu'une face JSON (nlohmann) ; l'ancienne `Params::toJson()` jansson est déplacée hors de la classe, corps inchangé, dans l'adaptateur transitoire `jansson_from_params()` de `Jansson_Addition.h`. 15 fichiers `src/` (dont `JsonApi.cpp`, 70 sites), `ParamsJson_test` neuf — il porte le **tripwire d'échappement**, **seul garde-fou de toute la série** puisque les goldens comparent des **documents parsés**, pas des octets — et **2 lignes d'appel** dans 2 tests préexistants, **aucune assertion touchée**. **145 goldens intacts** (hash d'arbre git identique), **70/70** | refactor | E4.0 | ✅ |
| [E4.1b](E4.1b.md) | 4 | Les **trois invariants d'émission** posés avant toute migration : `ensure_ascii=true` + `error_handler_t::replace` sur les 8 `dump()` nlohmann **déjà en service** (les deux `sendJson(const Json&)` dument à nu aujourd'hui). ⛔ **Prérequis dur d'E4.1o** : c'est là que le `terminate` d'E4.0 devient atteignable | fix | E4.1a | 📋 |
| [E4.1c](E4.1c.md) | 4 | Résidus jansson qui ne produisent aucun JSON : includes morts (`IO/ExternProc.h`, `IO/Web/WebCtrl.cpp`) + macro de compat morte (`HttpClient.cpp:111-116`, jansson ≥ 2.5 la fournit) | cleanup | E4.1a | 📋 |
| [E4.1d](E4.1d.md) | 4 | Lecteurs de JSON **tiers en lecture seule** : `Audio/Squeezebox.cpp` (12), `IO/Hue/HueOutputLightRGB.cpp` (8) — n'émettent **aucun** JSON, le seul `json_dumps` va dans `cDebug()` | refactor | E4.1a | 📋 |
| [E4.1e](E4.1e.md) | 4 | Wire **KNX** (`KNXCtrl` ↔ `calaos_knx` ↔ `KNXExternProc_cli`, 5 fichiers) + `KNXValue::toJson/fromJson` — **déclaré 2 fois, défini 3 fois**, les trois copies bougent ensemble | refactor | E4.1a | 📋 |
| [E4.1f](E4.1f.md) | 4 | Wire **OLA** (`OLACtrl` ↔ `calaos_ola`) — ⛔ **le seul wire du dépôt à vrais entiers JSON** (`json_integer`), retyper en chaîne casse le pilotage DMX en silence | refactor | E4.1a | 📋 |
| [E4.1g](E4.1g.md) | 4 | Wire **MQTT** (`MqttCtrl` ↔ `calaos_mqtt`) — `payloadToJsonString()` est le **seul** traitement délibéré d'UTF-8 invalide du dépôt : préserver la **longueur** (octets nuls), U+FFFD remplace le repli maison | refactor | E4.1a | 📋 |
| [E4.1h](E4.1h.md) | 4 | Wire **Wago** (`WagoMap` ↔ `calaos_wago`, 52 sites) — ⛔ porte un **bug fonctionnel préexistant** (`write_multiple_bits/_words` n'émettent jamais `values`) : **le porter tel quel**, pas le corriger (Q2) | refactor | E4.1a | 📋 |
| [E4.1i](E4.1i.md) | 4 | Wire **Reolink** (`ReolinkCtrl` ↔ `ExternProcReolink_main.py`, Python **de ce dépôt**) — ⚠️ le message porte le mot de passe caméra en clair : aucune trace de debug neuve dessus | refactor | E4.1a | 📋 |
| [E4.1j](E4.1j.md) | 4 | Wire **Lua aval** : `ScriptExtern_main.cpp`, `ScriptBindings.cpp` (`ScriptExec.cpp` **exclu**, il part en E4.1m). Piège : `operator[]` sur un document d'entrée le mute | refactor | E4.1a | 📋 |
| [E4.1k](E4.1k.md) | 4 | Générateur **`io_doc.json`** : `IODoc.{h,cpp}`, `IOFactory.cpp` — ⚠️ seul site à `JSON_PRESERVE_ORDER` : l'ordre des sections devient alphabétique (Q3). Fichier hors ligne, pas un wire | refactor | E4.1a | 📋 |
| [E4.1l](E4.1l.md) | 4 | `CalaosEvent::toJson()` → `Json` (2ᵉ des 3 `toJson()` membres) + ses 4 appelants ; adaptateur transitoire `jansson_from_json` **à un seul appelant** pour `ScriptExec.cpp:182` | refactor | **E4.1b** | 📋 |
| [E4.1m](E4.1m.md) | 4 | `JsonApi` **modèle** : `buildJsonHome/IO/RoomIO/Cameras/Audio/StatusInfo/GetIO`, `buildFlatIOList`, `dumpJsonRedacted` + **`ScriptExec.cpp` migré intégralement** (l'adaptateur d'E4.1l disparaît) | refactor | **E4.1l** | 📋 |
| [E4.1n](E4.1n.md) | 4 | `JsonApi` **état** : `buildJsonState/States/Query`, `decodeSetState` + ⭐ **le pont jansson↔nlohmann de `RemoteUIWebSocketHandler.cpp:246` disparaît**. ⚠️ `buildJsonState` est async : les `decref` partent, **les gardes de vie NON** | refactor | **E4.1m** | 📋 |
| [E4.1o](E4.1o.md) | 4 | `JsonApi` **params + plages horaires** — ⛔ **c'est ici que le `std::terminate` d'E4.0 (`?param=%ff%80x`) devient atteignable** ; E4.1b doit être mergé. La paire n'est plus supprimée mais conservée avec U+FFFD (changement assumé, → `RELEASE_NOTES`) | refactor | **E4.1n**, E4.1b | 📋 |
| [E4.1p](E4.1p.md) | 4 | `JsonApi` **lecteur audio** : `decodeGetPlaylist`, `getNextPlaylistItem` (récursive+async), `audioGet*`, `processDbResult`. ⛔ **`Utils::to_string(double)` ne se corrige pas** (`1234.56789` → `"1234.57"`, épinglé) | refactor | **E4.1o** | 📋 |
| [E4.1q](E4.1q.md) | 4 | `JsonApi` **base musicale** : les 14 `audioDbGet*` jumelles (~75 sites). Séparé de E4.1p pour rester revuable ; contre-mutation exigée **entre deux domaines voisins** (albums ↔ artistes) | refactor | **E4.1p** | 📋 |
| [E4.1r](E4.1r.md) | 4 | `JsonApi` **autoscénarios** (9 fonctions) — ⚠️ **candidat à l'annulation (Q1)** : E4.6d les réécrit entièrement. Recommandation : migrer **mécaniquement**. Adaptateur unique vers `Scenario::toJson()`, retiré par E4.6d | refactor | **E4.1q** | 📋 |
| [E4.1s](E4.1s.md) | 4 | **Émetteurs + dispatch** : reste des deux handlers, suppression des surcharges `sendJson(json_t*)`, ⭐ **bascule du wire vers la FORME 3** (`ensure_ascii=true`, hex minuscule — **pas** les octets bruts) + déclaration de risque en `RELEASE_NOTES` | refactor | **E4.1r** | 📋 |
| [E4.1x](E4.1x.md) | 4 | **Clôture** : `Jansson_Addition.h` supprimé, tripwire réduit à la forme 3, **`configure.ac:52`** (`jansson >= 2.5`), renommage `toNJson`→`toJson`. Preuve : `grep` tree-wide à zéro **ET** build vert **sans le paquet installé** | cleanup | **E4.1s** ⛔ **+ E4.6b + E4.6d** | ⛔ |
| [E4.2](E4.2.md) | 4 | Ownership model (smart pointers / by-id) — **épique close : a→f tous livrés** (résolution par id, `Room` possède ses IOs en `unique_ptr`, Conditions/Actions par id, `Rule`/`ListeRule` possédants, règle désactivée si dépendance manquante, back-pointers de scénario en `weak_ptr`). **g abandonné après re-scope** (0/21 sites `IOBase*` de JsonApi dangereux) | epic | Phase 3 | ✅ |
| [E4.2a](E4.2a.md) | 4 | ListeRoom by-id resolution + lookup hardening | refactor | — | ✅ |
| [E4.2b](E4.2b.md) | 4 | Room owns unique_ptr<IOBase>, caches non-owning | refactor | E4.2a | ✅ |
| [E4.2c](E4.2c.md) | 4 | Conditions/Actions reference IOs by id (not IOBase*) | refactor | E4.2b | ✅ |
| [E4.2d](E4.2d.md) | 4 | Rule/ListeRule own their objects (unique_ptr) | refactor | E4.2c | ✅ |
| [E4.3](E4.3.md) | 4 | ~~Test coverage~~ (atteint à 99%: 40 binaires/411 cas — voir PHASE4.md) | epic | T0.3 | ✅ |
| [E4.3ab](E4.3ab.md) | 4 | Coverage gaps: TimeRange/InPlageHoraire + XML round-trip | tests | — | ✅ |
| [E4.3cd](E4.3cd.md) | 4 | `--enable-asan` option + CI coverage report | infra | — | ✅ |
| [T3.13](T3.13.md) | 3 | TimeRange: include guard, parse UB, midnight-wrap (user decision) | fix | E4.3ab | ✅ |
| [T3.14](T3.14.md) | 3 | Shutdown dangling read (static destruction order) | fix | — | ✅ |
| [T3.15](T3.15.md) | 3 | RemoteUI device_info does not round-trip (user decision) | fix | E4.4cd | ✅ |
| [T3.16](T3.16.md) | 3 | Fix CI format-check (pugixml exclude, git before checkout) | infra | — | ✅ |
| [E4.4](E4.4.md) | 4 | ~~TinyXML 2.5.3 → TinyXML2~~ → **pugixml** (user decision) | epic | Phase 3 | ✅ |
| [T3.12](T3.12.md) | 3 | ~~Patch 2 TinyXML CVEs~~ (abandoned: superseded by pugixml migration) | fix (sec) | — | ⛔ |
| [E4.4a](E4.4a.md) | 4 | Introduce pugixml (Debian pkg, XPath on) | infra | — | ✅ |
| [E4.4b](E4.4b.md) | 4 | WebCtrl: TinyXPath → pugixml XPath | refactor | E4.4a | ✅ |
| [E4.2e](E4.2e.md) | 4 | Rule with a missing dependency is disabled (user decision) | fix | E4.2c | ✅ |
| [E4.4cd](E4.4cd.md) | 4 | XML port on pugixml (sweep + core, atomic) | refactor | E4.4b | ✅ |
| [E4.4dbis](E4.4dbis.md) | 4 | Port ConfigStore.cpp (last TinyXML consumer) | refactor | E4.4cd | ✅ |
| [E4.4e](E4.4e.md) | 4 | Delete vendored TinyXML + TinyXPath | removal | E4.4d | ✅ |
| [E4.2f](E4.2f.md) | 4 | Scenario/AutoScenario back-pointers → ids or weak_ptr | refactor | E4.2d | ✅ |
| [E4.2g](E4.2g.md) | 4 | ~~JsonApi `IOBase*` sites~~ (re-scopé : 0/21 dangereux, abandonné) | refactor | E4.2c | ⛔ |
| [T3.17](T3.17.md) | 3 | Généraliser la garde alive aux ~42 sites async audio/caméra | epic | T2.15 | ✅ |
| [T3.17f](T3.17f.md) | 3 | `eventlog` : UAF non gardé sur les deux transports | fix (sec) | T3.17c | ✅ |
| [T3.19](T3.19.md) | 3 | `audio_db` : déréf. `nullptr` + `eventlog` : `per_page` divisé par zéro (SIGSEGV + SIGFPE, plantages à distance) | fix (sec) | T3.17c | ✅ |
| [T3.17a](T3.17.md) | 3 | Garder la chaîne récursive `get_playlist` (double mort : `apiAlive` + `AudioPlayer*`) | fix (sec) | T2.15 | ✅ |
| [T3.17b](T3.17.md) | 3 | Garder les 5 méthodes `audio*` mono-coup contre la mort du client (`JsonApi.cpp`) | fix (sec) | T2.15 | ✅ |
| [T3.17c](T3.17.md) | 3 | Garder les 15 méthodes `audioDbGet*` de la base musicale contre la mort du client (`JsonApi.cpp`) | fix (sec) | T2.15 | ✅ |
| [T3.17d](T3.17.md) | 3 | Garder l'instantané caméra `get_picture` contre la mort du client (`JsonApiHandlerHttp`) | fix (sec) | T2.15 | ✅ |
| [T3.17e](T3.17.md) | 3 | Audit du transport WS : le jeton `apiAlive` hérité suffit (aucun code de production changé) | audit | T2.15 | ✅ |
| [T3.18](T3.18.md) | 3 | Scénario amputé : désactiver le scénario entier, drapeau persistant + réactivation manuelle (`autoscenario reenable`) (user decision) | fix | E4.2f, **T3.17** | ✅ |
| ~~T3.18b~~ | 3 | ~~Commande d'API dédiée de réactivation~~ (annulé : réintégré dans T3.18) | feature | — | ⛔ |
| [T3.20](T3.20.md) | 3 | ~~Suites T3.18 : `autoscenario modify` refuse un payload citant un IO absent + `disabled_missing_io` en lecture seule~~ — **parké : absorbé par [E4.6](E4.6.md)** (refonte complète AutoScenario, décision utilisateur 2026-08-24). Analyse conservée, elle reste la référence. Durcissement `del_param` **extrait en T3.21** | fix | T3.18 | ⛔ |
| [I4.1](I4.1.md) *(dépôt `calaos_installer`)* | — | Les 4 `if (x)` sans `else` purgeaient silencieusement les entrées/sorties à id non résolu au chargement de `rules.xml` — **toutes les règles**, pas seulement les scénarios. **Corrigé** : l'id non résolu est **préservé** par un IO fantôme hors `Room` + rapport nominatif ventilé entrée/sortie ; **segfault** corrigé au passage (`Condition::output` non initialisé) ; 2ᵉ purge silencieuse du `val_var` à l'affichage corrigée ; params `autoscenario_*`/`as_*` protégés (**anticipe E4.6, mesuré inerte**). Mesuré : 449 règles / 621 conditions / 664 actions contre un `io.xml` vide → **0 → 621 / 0 → 664**. **Revu** (`MERGE`, réserves fermées) : ⭐ **R1 — la préservation avait créé une régression**, `get_new_id()` réattribuait un id qu'une règle pendante référence encore (adoption silencieuse d'une règle par un IO neuf) → ids pendants **réservés** ; R2 porte à sens unique dans `DialogListProperties` ; R3 rapport ventilé. **5 commits sur `master` local, ⛔ non poussés** | fix | — | ✅ |
| [T3.21](T3.20.md) | 3 | **Extrait de T3.20** : `buildJsonDelParam` (`JsonApi.cpp:724`) court-circuite `IOBase::del_param()` — la garde d'immuabilité de `"id"` n'est jamais atteinte depuis l'API. Correctif d'une ligne, **indépendant des scénarios**, aucun appelant cassé (seuls `InputString.cpp:37` et `InputAnalog.cpp:99` appellent `del_param` par ailleurs) | fix | — | 📋 |

---

## Wave / dependency graph

```
Phase 0 (infra):
  Wave 0a:  T0.1, T0.4, T0.6          (disjoint: ci.yml / configure.ac / Dockerfile+libquickmail+scripts)
  Wave 0b:  T0.2 ─▶ T0.3 ─▶ T0.5      (serialized: share tests/Makefile.am and/or ci.yml)
  E4.3 may begin after T0.3 and run continuously.

Phase 1 (critical fixes / security) — depends on Phase 0. Files fully disjoint → all parallel:
  T1.1  ListeRule/AutoScenario   T1.2  ConditionStd/Output   T1.3  ListeRoom
  T1.4  JsonApi*                 T1.5  Http/WebSocket/McpProxy/WebSocketFrame
  T1.6  RemoteUI/RemoteUI+HMAC+Manager                        T1.7  McpServerManager
  T1.8  calaos_mcp auth.py + calaos-python extern_proc/logger T1.9  IO/ExternProc
  T1.10 RemoteUI WS/OTA handlers T1.11 IOBase/IOFactory       T1.12 Utils(RNG/paths)+tcpsocket ⮕ blocks T2.2
  T1.13 IO/LAN + IO/Hue          T1.14 IO/Reolink              T1.15 LuaScript
  T1.16 calaos_mcp client/tools + Roon main                   T1.17 extern-proc *_main.cpp
  T1.18 ActionMail/ActionPush    T1.19 MySensors/Gpio/Web controllers

Phase 2 (structural) — depends on Phase 1:
  Wave 2a (SOLO):  T2.2  Utils split          (depends T1.12; run alone — Utils.h is included almost everywhere)
  Wave 2b:         T2.1, T2.3, T2.4, T2.5, T2.6, T2.7   (disjoint files; T2.7 after T2.1)

Phase 3 (driver dedup / fix) — depends on Phase 2. Disjoint files → parallel:
  T3.1 AVR*/AVReceiver   T3.2a KNX  T3.2b MySensors(sub)  T3.2c Mqtt(sub)
  T3.2d Web(sub)         T3.2e Wago(sub)  T3.2f Gpio(sub)
  T3.3 IPCam             T3.4 base64 (after T2.2)          T3.5 Squeezebox

Phase 4 (epics) — depends on Phase 3 + E4.3:
  E4.3 (ongoing from Phase 0) is the prerequisite net.
  E4.1 (JSON), E4.2 (ownership), E4.4 (TinyXML2) are SERIALIZED / split into per-module
  sub-tickets before execution — they all touch ListeRoom / ListeRule / CalaosConfig / JSON files.

E4.1 waves (2026-08-24) — vague 1 parallèle, le reste sérialisé:
  V1  E4.1b E4.1c E4.1d E4.1e E4.1f E4.1g E4.1h E4.1i E4.1j E4.1k   (10 en parallèle, fichiers disjoints)
  V2  E4.1l    V3 E4.1m    V4 E4.1n    V5 E4.1o    V6 E4.1p
  V7  E4.1q    V8 E4.1r    V9 E4.1s
  V10 E4.1x  <-- bloqué par E4.6b + E4.6d (IO/Scenario.cpp exclu d'E4.1)
```

Cross-phase sequential edges (same file, different wave — never same wave):
- `src/lib/Utils.cpp`: T1.12 (P1, RNG+paths) → T2.2 (P2, split).
- `WebSocket.cpp`: T1.5 (P1) → T2.3 (P2, SHA1 removal).
- `CalaosConfig.cpp`: T2.4 (P2) → E4.4 (P4, parser migration).
- IO controller dirs: T1.13/T1.19 (P1, base/ctrl files) vs T3.2b/d/f (P3, subclass files) — disjoint files even inside the same directory.
- `IO/Wago/`, `IO/Mqtt/`: T1.17 (P1, extern-proc mains) vs T3.2e/c (P3, subclasses) — disjoint files.
- `JsonApi.h/.cpp`, `JsonApiHandlerHttp.cpp`, `JsonApiHandlerWS.cpp`: E4.1b (P4, V1) → E4.1l (V2) →
  E4.1m (V3) → E4.1n (V4) → E4.1o (V5) → E4.1p (V6) → E4.1q (V7) → E4.1r (V8) → E4.1s (V9).
  **Neuf tickets, neuf vagues, jamais deux dans la même** — c'est la plus longue chaîne sérialisée
  du projet.
- `LuaScript/ScriptExec.cpp`: E4.1l (V2, un site via adaptateur) → E4.1m (V3, intégral).
  `LuaScript/ScriptExtern_main.cpp`/`ScriptBindings.cpp` = E4.1j (V1), fichiers disjoints.

---

## Quick wins (highest leverage first)

1. **T0.1** — CI `make check` (today nothing guards merges).
2. **T1.4** (path traversal `event_picture`) + **T1.1** (rule/IO UAF).
3. **T1.13** (ping heap overflow + Hue polling-timer UAF — two confirmed criticals).
4. **T1.7** (MCP token CSPRNG) + **T1.5** (pre-auth DoS caps).
5. **T3.1** (Onkyo reassembly iterator-invalidation UB — memory corruption on real traffic).

---

## Verification (per ticket & per wave)

1. **Build:** `./autogen.sh && ./configure && make` with no new warnings (+ `--with-mqtt --with-knx --with-owfs --with-ola` for drivers).
2. **Tests:** `make check` green; every "fix" ticket adds a regression test.
3. **Security (Phase 1):** path traversal rejected, login throttled, MCP token 256-bit CSPRNG, oversized WS frame rejected without OOM, ping/WOL/Onkyo inputs validated.
4. **Functional:** server started with real `io.xml`/`rules.xml`, IO state + rule execution verified via the JSON API (port 5454); `ExternProc` driver (MQTT/KNX) + MCP sidecar OK.
5. **Wave review:** `/code-review` on the cumulative diff before merge; ASan/valgrind on all lifecycle-fix tickets.
