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
- **WAVE 6 TERMINÉE (9/9 mergés, 2026-08-15)** : T3.4, T3.2f, T3.3, T3.5, T3.2e, T3.2d,
  T3.2c, T3.1, T3.2a — tous ✅. Phase 3 partielle : restent les epics E4.x, le backlog
  T2.14-T2.18 et les follow-ups FINDINGS. **Rien en vol après ce merge.** Suite de
  référence : 39/39 tests (make check). Worktrees wave 6 nettoyés.
  (Historique wave 6 : worktrees `/tmp/claude-1000/calaos-wave6/t3.X`, branches
  `refactor/t3.*`, base `82887cc0`.)
  Contraintes de brief : **T3.2a garde son abstraction DANS IO/KNX/** (11 sous-classes, pas
  13) ; T3.2c/d/e/f = dédup LOCAL sans dépendre de T3.2a ; types XML (REGISTER_IO*) et ioDoc
  invariants ; T3.5 lignes recalées (`requestTimeout_cb` :468, `buffer_notif` :209-212) ;
  T3.3 Syno à :185, option conservatrice sur les items « policy » + flag validation ;
  T3.4 = base64.{cpp,h} + wrappers `StringUtils` SANS casser les signatures (pas d'édition
  de call sites). Suite de référence : 31/31. À la reprise : branches avec commits →
  revue (subagent) → merge (subagent, sérialisé) → board ✅ ; branches vides → relancer.
- **Wave 7 en cours** : **T3.6 ✅ mergé** (2026-08-15, `47badd28`, suppression Gadspot :
  Gadspot.{cpp,h}, Makefile.am, POTFILES.in, pot + 7 .po, docs ; 39/39 tests, ff-only,
  worktree `/tmp/claude-1000/calaos-wave7/t3.6` nettoyé). **T3.7 ✅ mergé**
  (2026-08-15, `340bf7cb`, garde pid > 0 sur les deux kill(SIGTERM) d'ExternProc.cpp
  — uv_kill(0) SIGTERMait le process group après un uv_spawn échoué ; API uvw vérifiée
  (pid() = uv_process_t.pid, reste 0 si spawn échoue) ; 39/39 tests, ff-only, worktree
  t3.7 nettoyé. Restent non gardés, en FINDINGS : McpServerManager.cpp:166,
  JsonApiHandlerHttp.cpp:45). **T3.8 ✅ mergé** (2026-08-15, `a22db7c4`, gardes
  aliveTag sur les callbacks async d'AVRRose.cpp + AVRRoseNotifServer.{cpp,h} ;
  39/39 tests, ff-only, worktree t3.8 nettoyé). **T2.17 ✅ mergé** (2026-08-15,
  `7bf503ad`, TLS vérifié par défaut dans UrlDownloader + opt-in `insecure` par appel
  (IPCam/Hue/mjpeg relay/ActionCameraDownload) + masquage des credentials d'URL dans
  les logs (StringUtils) ; rebase propre ×2 (master avait avancé de 2 commits docs-only :
  décision « insecure par défaut » qui sera appliquée par T2.19 rescopé), aucun conflit,
  ff-only ; 39/39 tests dont UrlDownloader_test 10/10 et IPCamUrl_test 16/16 ; worktree
  t2.17 nettoyé). **T2.15 ✅ mergé** (2026-08-15, `01089187`, garde du chain async
  audio de JsonApi::buildJsonState() contre UAF (destruction du JsonApi ou d'un
  AudioPlayer pendant les réponses squeezebox en vol) + fuite de ref json
  (json_object_set vs _new) + lookup de token RemoteUI en temps constant ;
  rebase propre sur master post-T2.17 (aucun conflit, le bloc tests/Makefile.am
  s'est appliqué seul, 29/29 if/endif) ; 40/40 tests dont le nouveau
  core/JsonApiAudioState_test ; ff-only ; worktree t2.15 nettoyé). **T2.16 ✅ mergé**
  (2026-08-15, `1735460c`, reset de l'état per-request entre requêtes keep-alive
  (request_headers/body/url ne fuient plus d'une requête à la suivante, headers CORS
  périmés droppés) + contrat bindParser corrigé aux sites de réinit de WebSocket.cpp
  (parser->data pointait sur l'owner au lieu du sous-objet state → bug d'offset) ;
  rebase propre sur master post-T2.17/T2.15, aucun conflit ; 40/40 tests dont
  TransportHardening_test 22/22 (17 antérieurs + 5 KeepAliveRequestReset) ; ff-only ;
  worktree t2.16 nettoyé). **T2.14 ✅ mergé** (2026-08-15, `ce6d91b9`, câblage des
  suites tests/python/ dans `make check` via le nouveau wrapper
  `tests/run-python-tests.sh` + section TESTS en tête de tests/Makefile.am ;
  rebase propre sur master post-T2.15/T2.16/T2.17 + wave-7 T3.x, aucun conflit,
  29/29 if/endif ; 41/41 tests dont run-python-tests.sh PASS ; ff-only ; worktree
  t2.14 nettoyé). **T2.19 ✅ mergé** (2026-08-15, `41184973`, TLS insecure PAR DÉFAUT
  pour toutes les URLs configurées par l'utilisateur — politique corrective décidée par
  l'utilisateur, supplante le défaut T2.17 sur ces chemins : `insecure` absent/vide/autre
  → insecure, exactement `"false"` → vérifié (`UrlDownloader::insecureParamEnabled` +
  `setInsecureFromParam`) ; appliqué à IPCam/Hue/Web/Lua requestUrl/DataLogger/
  Squeezebox/AVRRose/ActionCameraDownload/mjpeg relay ; les services calaos.fr codés
  en dur restent vérifiés ; rebase sur master avec 1 conflit tests/Makefile.am (bloc
  T2.19 réappliqué en fin de fichier après le bloc T2.15, 30/30 if/endif, câblage
  python intact) ; JsonApiHandlerHttp.cpp : la version T2.19 `camera->tlsInsecure()`
  supplante la ligne T2.17 ; 42/42 tests dont le nouveau TlsInsecureDefault_test ;
  ff-only ; worktree t2.19 nettoyé). **T2.18 ✅ mergé** (2026-08-15, `62739380`,
  audit null-guard des call sites createIO()/IOFactory::CreateIO() : un échec de
  factory (type inconnu, id interne squatté, room manquante) finit en erreur loggée
  + réponse API `{"error"}`, jamais en déréférencement null — JsonApi.cpp,
  ListeRoom.cpp, Scenario/AutoScenario.{cpp,h} + nouveau
  tests/core/ScenarioNullGuard_test.cpp ; rebase sur master avec 1 conflit
  tests/Makefile.am EOF (bloc T2.18 réappliqué après le bloc T2.19, 31/31 if/endif,
  câblage python intact) ; 43/43 tests ; ff-only ; worktree t2.18 nettoyé).
  **WAVE 7 TERMINÉE — Phase 2 backlog + Phase 3 étendue INTÉGRALEMENT vidés
  (T2.14-T2.19, T3.6-T3.8 tous ✅) ; rien en vol ; restent uniquement les epics E4.x
  + follow-ups mineurs FINDINGS ; tâche EXTERNE calaos_installer (option
  insecure=false nouveaux devices).**
- **Wave 8 en cours** : **T3.9 ✅ mergé** (2026-08-15, `e2e054e2`, garde `pid() > 0` sur les
  deux derniers kill(SIGTERM) de ProcessHandle — `McpServerManager::stop()` et
  `~JsonApiHandlerHttp()` — même forme que la référence T3.7 dans ExternProc.cpp,
  commentaire de contrainte inclus ; +6/-2, aucun header touché, lignes TLS T2.17/T2.19
  intactes ; grep tree-wide : les 5 sites kill() sont désormais tous gardés (ExternProc ×2,
  PingInputSwitch ×1 via `pingRunning`, ces 2) et Calendar.cpp/NotifManager.cpp possèdent
  des ProcessHandle sans jamais appeler kill() ; rebase propre sur master docs-only,
  43/43 tests, ff-only, worktree t3.9 nettoyé). **T3.11 ✅ mergé** (2026-08-15, `8db87507`,
  hygiène : `docs/13_utility_lib.md` + `po/POTFILES.in` purgés des entrées `src/lib/SHA1.{cpp,h}`
  supprimées par T2.3 — les mentions SHA1 restantes sont l'usage OpenSSL EVP légitime dans
  WebSocket.cpp et son test ; `configure.ac` : `AC_CHECK_PROG(HAVE_CURLBIN)` + sa ligne de résumé
  retirées, obsolètes depuis T2.5 (UrlDownloader linke libcurl) — grep tree-wide : plus aucun
  usage de HAVE_CURLBIN/CURLBIN_INFO, et libcurl reste une dépendance dure via pkg-config dans
  `requirements_calaos_common` et `requirements_calaos_server` ; `data/debug/package-lock.json`
  rafraîchi (npm update + audit fix, 11 → 3 advisories toutes enracinées dans `immutable`,
  devDependency only ; `npm ci` revérifié en conteneur node:20-slim = 438 paquets, lockfileVersion 3,
  resolved+integrity partout, package.json inchangé, `data/debug/dist/` non rebuildé) ; nouveau
  `.github/dependabot.yml` limitant Dependabot à `/data/debug` npm avec
  `allow: dependency-type: production` (le toolchain gulp/browser-sync ne ship jamais).
  Aucun `src/**` touché ; rebase propre sur master, autogen OK après l'édition configure.ac,
  43/43 tests, ff-only, worktree t3.11 nettoyé). **E4.2a ✅ mergé** (2026-08-15, `fa0dace2`,
  étape 1/6 de la série ownership E4.2, préparatoire et SANS changement de propriété : accesseurs
  de résolution par id sur ListeRoom — `findIO`/`hasIO`/`findIOAs<T>`/`findIOByIndex`/`findRoomOfIO`
  — plus `tests/core/ListeRoomIdResolution_test.cpp` (484 l.) qui épingle le contrat des accesseurs
  (id inconnu, id vide, id dupliqué, index hors bornes → nullptr ; un miss n'insère jamais dans
  `io_table`) et les trois invariants que les étapes 2-6 ne doivent pas casser : l'ordre d'itération
  des IO pièce par pièce, les signatures/contenus de `getCameraList()`/`getAudioList()`, et la
  sémantique de transfert de propriété de `delete_io(io, del)`. Périmètre exact : `ListeRoom.{h,cpp}`,
  le nouveau test, `tests/Makefile.am` (bloc `HAVE_GTEST` propre en fin de fichier, 32/32 équilibré) ;
  rebase sans conflit sur master, 44/44 tests, ff-only, worktree e4.2a nettoyé. Deux carry-overs
  consignés dans FINDINGS pour E4.2b). **T3.10 ✅ mergé** (2026-08-15, `98d02c5a`, promotion de
  `ThinIo.h` de `IO/KNX/` vers `IO/` (git mv, similarité 51 %) et harmonisation de 4 mixins
  driver : Mqtt/Web/Gpio (GpioInputBase, GpioOutputShutterBase, MqttIOBase, WebDocBase) rebasés
  sur le template générique ; `WagoIOBase` délibérément NON rebasé — son entrelacement alias/link
  ne rentre pas dans le contrat ThinIo sans en dénaturer la sémantique, seule une note de contrat
  y est ajoutée. Périmètre exact : `IO/**` (dont le rename), `src/bin/calaos_server/Makefile.am`,
  et `po/POTFILES.in` (une ligne de chemin déplacée dans son slot trié, les suppressions SHA1 de
  T3.11 préservées). Attention base : la branche partait de `0013e378` et non de `6559f8f0` —
  rebasée avant merge, donc aucune régression des lignes de board wave-8 ni des fiches
  T3.9/T3.11/T3.12 (vérifié après rebase). Master a bougé deux fois pendant l'intégration
  (E4.2a code, puis E4.2b docs) : build rejoué sur la base E4.2a, 44/44 tests, ff-only,
  worktree t3.10 nettoyé). **E4.4a ✅ mergé** (2026-08-16, `7cbdcda8`, ticket d'entrée de la
  migration pugixml : pugixml est désormais disponible et lié, sans qu'aucun consommateur ne soit
  touché. `configure.ac` préfère le paquet distro (`pkg-config pugixml >= 1.10`) et retombe sur la
  copie vendored `src/lib/pugixml/` (1.14, MIT, 4 fichiers dont LICENSE.md) quand il est absent ;
  les deux voies débouchent sur les mêmes `PUGIXML_CFLAGS/LIBS` propagés via `CALAOS_COMMON_CFLAGS`.
  `tests/PugiXml_test.cpp` est un smoke test de contrat de build : il prouve que l'en-tête est
  atteignable, que la bibliothèque link, et surtout que **XPath 1.0 est compilé** (`select_node()`/
  `select_nodes()`/`xpath_query` n'existent pas dans un build `PUGIXML_NO_XPATH`, donc le test ne
  compilerait pas contre un tel build). Dockerfile (stages dev + runtime), `.devcontainer/Dockerfile`
  et `.github/workflows/ci.yml` ajoutent `libpugixml-dev` pour que la voie système soit celle
  exercée en CI. Périmètre exact : `configure.ac`, `src/lib/Makefile.am`, `src/lib/pugixml/**`,
  `tests/PugiXml_test.cpp`, `tests/Makefile.am`, les 3 fichiers Docker/CI — aucun fichier TinyXML,
  aucun WebCtrl, aucun consommateur. Attention base : la branche partait de `f27c98ed`, rebasée sur
  master avant merge ; conflit unique et attendu en fin de `tests/Makefile.am` (E4.2a et E4.4a
  ajoutent chacun leur bloc `HAVE_GTEST` en EOF), résolu en gardant les deux blocs à la suite,
  33/33 `if`/`endif` équilibrés. Build d'intégration : 45/45 tests (44 de master + `PugiXml_test`),
  et comme l'image de dev ne contient pas encore `libpugixml-dev`, c'est **la voie vendored** qui a
  été exercée — la voie système reste couverte par la CI. ff-only, worktree e4.4a nettoyé.
  ⚠️ **Contrainte propagée dans les fiches E4.4b/c/d** : Debian 12 fournit pugixml **1.13**, la copie
  vendored est en **1.14**, et le plancher déclaré est **>= 1.10** — tout code consommateur doit
  rester sur la surface d'API de la 1.10, sinon la voie système casse là où la vendored fonctionne).
  En vol : E4.2b. Prochain de la série : E4.4b.
- **E4.3ab ✅ mergé** (2026-08-16, `9d2f5605`, test-only, **zéro fichier de production touché** —
  c'était la règle dure du ticket). Ferme les 2 derniers trous de couverture laissés par E4.3 :
  `tests/TimeRangeCalendar_test.cpp` (35 cas — premiers tests de `src/lib/TimeRange.h` et de
  l'évaluation de `IO/InPlageHoraire.cpp`, qui n'avaient aucune référence de test, plus les coins
  de `Calendar` non couverts par `CommonLib_test`) et `tests/core/ConfigRoundTrip_test.cpp`
  (10 cas — config → save → reload → égalité **sémantique** du modèle en mémoire). Les deux
  suites sont écrites pour **survivre à E4.4d** : rien ne regarde le XML, les horaires passent par
  l'API publique `AddMonday()/…` et le verdict se lit par `get_value_bool()` ; le round-trip compare
  `ListeRoom`/`ListeRule` en mémoire, jamais les octets des fichiers — c'est justement ce qui en fait
  l'arbitre de la reformattage légitime de `io.xml`/`rules.xml` par pugixml. Payloads volontairement
  hostiles : UTF-8 accentué, les 5 spéciaux XML, références numériques, valeurs vides.
  ⚠️ **Risque flakiness traité** : `InPlageHoraire::hasChanged()` lit l'horloge murale
  (`time(NULL)`/`localtime()`) sans aucun seam d'injection. Les cas qui dépendent de « maintenant »
  tournent dans une *fenêtre stable* (seconde-du-jour lue avant **et** après l'appel, réessai si
  l'horloge a tick, `GTEST_SKIP` après 50 tentatives) — encadrement correct car la grandeur mesurée
  est monotone dans la journée. Vérifié : **10 exécutions × 2 binaires = 0 échec, 0 skip**, plus un
  balayage 5 fuseaux (Kiritimati/Midway/UTC/New_York/Sydney) couvrant deux jours de semaine
  différents — aucune dépendance au jour ni au mois. Base : branche partie de `3d8e8b3f`, rebasée ;
  conflit unique et attendu en fin de `tests/Makefile.am` (patron regenerate : fichier master +
  les 2 blocs `HAVE_GTEST` de la branche en EOF), 35/35 `if`/`endif` équilibrés. Master a bougé
  pendant le build (docs E4.4a) → rebase rejoué, commit docs-only donc build conservé.
  Build d'intégration : **47/47**. ff-only, worktree e4.3ab nettoyé.
  5 bugs + 1 ambiguïté produit remontés (non corrigés) dans FINDINGS.md.
- **E4.2b ✅ mergé** (2026-08-16, `26977351`, étape **2/6** de la série ownership E4.2).
  `Room` possède ses IOs via `unique_ptr<IOBase>`, `ListeRoom` possède ses `Room` via
  `unique_ptr<Room>`, et `io_table`/`cameraCache`/`audioCache` deviennent des index **non
  possédants** — 5 `delete` manuels supprimés du chemin IO. Deux points de revue corrigés avant
  merge : `Room::RemoveIO` **sort le `unique_ptr` du vecteur avant l'`erase`**, pour que
  `~IOBase` (qui se désenregistre de `ListeRoom`) ne tourne jamais au milieu du décalage du
  vecteur ; et la clause `!id.empty()` de `createIO` (carry-over E4.2a, l'état « à moitié
  ajouté ») est remplacée par `isHashRegistered()`. Les deux **transferts** de propriété hors
  d'une pièce (`delete_io(io, del=false)` et `RemoveIOFromRoom()`) sont bien des `release()`,
  pinnés par le nouveau `tests/core/ListeRoomOwnership_test.cpp`. Périmètre exact :
  `Room.{h,cpp}`, `ListeRoom.{h,cpp}`, `IO/IOFactory.{h,cpp}`, le test, `tests/Makefile.am` —
  **`JsonApi.cpp` non touché** (volontaire : le null-deref `old_room` reste en carry-over).
  Base : branche partie de `e5dbd058`, rebasée sur master ; conflit unique et attendu en fin de
  `tests/Makefile.am` (patron regenerate : fichier master + le bloc `HAVE_GTEST` `# E4.2b` en
  EOF), 36/36 `if`/`endif` équilibrés. Build d'intégration : **48/48** (47 de master +
  `core/ListeRoomOwnership_test`). ff-only, worktree e4.2b nettoyé.
  2 bugs pré-existants remontés (non corrigés) dans FINDINGS.md : null-deref `old_room` en
  `JsonApi.cpp:1820`, et la fuite `addIOHash` inconditionnel d'`IPCam`/`AudioPlayer` sous
  `ScopedDocGen`. Étape 3/6 de la série E4.2 : **fiche pas encore découpée** (aucun E4.2c au
  board à ce jour).
- **E4.4b ✅ mergé** (2026-08-16, `d72377f7`, **l'étape à valeur sécurité** de la migration
  pugixml) : `WebCtrl::getValueXml()` passe de TinyXPath à `pugi::xpath_query` /
  `evaluate_string()`. C'est **le seul parse XML non fiable de calaos** (le document vient d'une
  URL configurée par l'utilisateur) **et** le seul site où l'expression XPath est elle-même de la
  config — donc le seul chemin où TinyXML 2.5.3 voyait de la donnée hostile. Après ce merge, cette
  entrée-là ne touche plus TinyXML du tout (le reste du parc TinyXML ne lit que des fichiers de
  config locaux, traités par E4.4c/d/e). L'expression invalide et le document malformé échouent
  proprement : chaîne vide, aucune `xpath_exception` qui s'échappe, aucun abort. Périmètre exact :
  `src/bin/calaos_server/IO/Web/WebCtrl.cpp`, `tests/WebCtrlXPath_test.cpp` (nouveau),
  `tests/Makefile.am` — rien d'autre. Base : branche partie de `8c227093`, rebasée sur master
  (qui avait pris E4.2b + des commits docs-only : T3.13.md, RELEASE_NOTES.md, lignes de board) ;
  conflit unique et attendu en fin de `tests/Makefile.am` (patron regenerate : fichier master +
  le bloc `HAVE_GTEST` `# E4.4b` en EOF), **37/37 `if`/`endif`** équilibrés, docs/refactoring
  intact côté master. Build d'intégration : **49/49** (48 de master + `WebCtrlXPath_test`).
  ff-only, worktree e4.4b nettoyé. **4e classe de divergence** trouvée en revue (arithmétique
  XPath : TinyXPath tronquait en int et débordait en int32) consignée dans FINDINGS.md — déjà
  couverte par RELEASE_NOTES.md. Prochain de la série : E4.4c.
- **T3.13 ✅ mergé** (2026-08-16, `462c9fcf`, 3 commits) : garde d'inclusion `TimeRange.h`,
  bornes invalides **définies** (une borne non parsable rend la plage inerte au lieu de
  retomber silencieusement sur 00:00:00), et surtout le **wrap de minuit** — une plage dont la
  fin précède le début (23:00 → 01:00, ou une borne solaire qui se met à croiser minuit au fil
  des saisons) est désormais évaluée deux fois : tête `[start, fin de journée]` le jour où elle
  est attachée, queue `[00:00, end]` le lendemain, sans jamais matcher le matin de son propre
  jour. Le jour précédent est calculé **DST-safe** (`mktime()` normalisé, vérifié sur le
  2025-03-30 Europe/Paris qui ne saute pas), et l'évaluation ne court-circuite plus (résultat
  identique, mais une plage qui wrappe est signalée même si une autre a déjà matché). Le log
  `logWrapOnce()` reste **une fois par plage** pour la durée de vie de l'objet et nomme
  désormais l'IO propriétaire + le jour d'attache (une queue vue le dimanche matin s'annonce
  `samedi`, c'est voulu). Périmètre exact : `src/lib/TimeRange.{h,cpp}`,
  `IO/InPlageHoraire.{cpp,h}`, `tests/TimeRangeCalendar_test.cpp` — **pas de `tests/Makefile.am`**
  (le binaire existait déjà depuis E4.3ab), donc aucun conflit EOF. Base : branche partie de
  `8c227093`, rebasée sur master (E4.2b/E4.4b + docs-only) **sans conflit**. Build
  d'intégration : **49/49**. ff-only, worktree t3.13 nettoyé. 2 suites non bloquantes
  consignées dans FINDINGS.md (`time2string_digit` sur durées négatives, cache `sun_rise_set`
  non peuplé au chemin d'échec polaire).
- **E4.2c ✅ mergé** (2026-08-16, `ff6c51c7`, étape **3/6** de la série ownership E4.2) :
  plus rien dans `Rules/` ne stocke un `IOBase*` — `Condition{Std,Output,Script}` et `ActionStd`
  ne gardent qu'un **id** et résolvent via les accesseurs E4.2a au point d'usage. Un IO manquant
  n'est plus un pointeur pendant : la condition vaut **false**, l'action est **sautée**, les deux
  sont loguées, et **la sauvegarde conserve l'id** (le rejet au chargement reste inchangé). La
  désinscription `in_event` est désormais pilotée par l'**enregistrement réel** au lieu d'une
  liste blanche de `gui_type`. Effet de bord corrigé au passage et consigné dans RELEASE_NOTES :
  les déclencheurs d'une `ConditionScript` étaient sérialisés dans l'**ordre de hash des
  pointeurs** (ASLR), d'où des `rules.xml` qui diffèrent entre deux sauvegardes d'une config
  inchangée ; l'ordre est maintenant celui du document. Périmètre exact : `Rules/Condition*.
  {h,cpp}`, `Rules/Action*.{h,cpp}`, `Rules/RulesFactory.cpp`, `ListeRule.{h,cpp}`,
  `ListeRoom.cpp`, `tests/core/RuleIoReference_test.cpp` (nouveau), `tests/Makefile.am` — rien
  d'autre. Base : branche partie de `838850a0`, rebasée sur master (T3.13 + docs-only) ; conflit
  unique et attendu en fin de `tests/Makefile.am` (patron regenerate : fichier master + le bloc
  `HAVE_GTEST` E4.2c en EOF), **38/38 `if`/`endif`** équilibrés, docs/refactoring intact côté
  master. Build d'intégration : **50/50** (49 de master + `core/RuleIoReference_test`). ff-only,
  worktree e4.2c nettoyé. 3 suites remontées par la revue (non corrigées) dans FINDINGS.md :
  la formulation « survit au save/reload » à corriger au ticket, `ConditionStd::getVarIds(vector
  <IOBase*>&)` devenu code mort, et le chemin de compat legacy audio/caméra de
  `ConditionStd::LoadFromXml` qui ne réaligne pas `id` (params/ops vides à l'évaluation).
  Prochain de la série : E4.2d.
- **E4.3cd ✅ mergé** (2026-08-16, `491a03b2`, infra pure, **aucun `src/**` touché**) :
  `configure.ac` gagne `--enable-asan` (ajoute `-fsanitize=address -fno-omit-frame-pointer
  -g -O1` à CFLAGS/CXXFLAGS/LDFLAGS + une ligne de résumé), et `.github/workflows/ci.yml`
  gagne un job `coverage` **informationnel**. Le bloc ASan est placé **délibérément en toute
  fin de `configure.ac`**, après chaque sonde compile/link (`AC_CHECK_LIB`,
  `PKG_CHECK_MODULES`, `EFL_CHECK_COMPILER_FLAGS`, `AX_CXX_COMPILE_STDCXX_20`,
  `ACX_PTHREAD`) : injecter `-fsanitize=address` plus tôt ferait linker chacune de ces sondes
  contre libasan et pourrait en changer le verdict. Désactivé par défaut — un `./configure` nu
  produit exactement les mêmes flags qu'avant. Le job CI est non bloquant **par construction**
  (`continue-on-error: true`, `make check || echo …`, upload `if: always()`) : son rôle est de
  publier la mesure, jamais de gater une PR sur un pourcentage. Périmètre exact : `configure.ac`,
  `.github/workflows/ci.yml`, `AGENTS.md`, `ORCHESTRATION.md` — rien d'autre, et surtout rien
  sous `src/` (E4.4cd, concurrent, en possède une large part). Base : branche partie de
  `a6ca2f2e`, rebasée **deux fois** (master a pris E4.4cd board, puis T3.14 + les pièges
  opérationnels ASan) — **aucun conflit** aux deux passes, y compris sur `ORCHESTRATION.md` :
  les notes ASan de master vivent dans la nouvelle section « Validation sur configs réelles »,
  la doc d'invocation de la branche dans « Workflow wave », les deux se complètent.
  ⚠️ Le worktree portait des objets `.o` d'un build `--enable-asan` antérieur : le premier build
  d'intégration a échoué au link (`undefined reference to __asan_report_load1` dans
  `llhttp/src/http.c`) parce que make les jugeait à jour alors que le link se faisait sans
  libasan. **Faux positif de propreté d'arbre** — après `make distclean`, build par défaut
  **50/50** tout vert. ff-only, worktree e4.3cd nettoyé. 2 nits cosmétiques de revue consignés
  dans FINDINGS.md (validation de `--enable-asan=<valeur>`, lcov CI non exercé).
- **E4.4cd ✅ mergé** (2026-08-16, `3d606eab`, **l'étape critique pour l'ABI de configuration** :
  tout le lecteur/écrivain de config passe de TinyXML à pugixml, 50 fichiers, +585/−475.
  Deux commits : step 0 (`override` explicite partout, aucune autre modification) puis le port.
  Périmètre exact vérifié après rebase : `src/bin/calaos_server/**` (43), `src/lib/{Makefile.am,
  Utils.h,XmlUtils.h}` (3, dont le nouveau `XmlUtils.h`), `tests/core/**` (4) — **rien** sous
  `configure.ac`, `.github/` ou `docs/`. Rebase sur master post-T3.14/T3.15 **sans aucun
  conflit** (pas de conflit EOF sur `tests/Makefile.am` cette fois : la branche n'y touche pas).
  Build d'intégration par défaut **50/50** tout vert, aucun résidu ASan.
  ⚠️ **`ConfigStore.cpp` est délibérément différé à E4.4d-bis** : il reste sur TinyXML, donc
  E4.4e (suppression du vendored TinyXML) est bloqué tant que E4.4d-bis n'est pas fait.
  Le **chemin pugixml vendored** est celui qui a été exercé par ce build (pas le paquet Debian).
  **Census `override`** (step 0) : **26** déclarations dérivées de `LoadFromXml`/`SaveToXml`,
  **toutes** prenant un `pugi::xml_node`, **toutes** désormais marquées `override` — c'est ce qui
  a rendu le changement de signature détectable à la compilation plutôt que silencieux.
  Deux réparations de fidélité de données visibles utilisateur (CDATA +18 caractères, perte de
  données sur `]]>`) consignées dans RELEASE_NOTES.md ; 3 suites de revue dans FINDINGS.md
  (offset en octets au lieu du numéro de ligne, `setAttribute` sur attribut dupliqué,
  divergences `hits` hors des six cas testés). ff-only, worktree e4.4cd nettoyé.
- **T3.15 ✅ mergé** (2026-08-16, `60793bb1`, **bug de fidélité de données visible utilisateur** :
  `RemoteUI::SaveToXml()` accrochait `<calaos:device_info>` au nœud qu'on lui passe — c'est-à-dire
  l'élément **pièce** (`Room::SaveToXml()` donne son propre élément à chaque IO qu'elle possède) —
  alors que `LoadFromXml()` l'a toujours lu **à l'intérieur** de `<calaos:remote_ui>`, ce que le
  format documente aussi (`RemoteUI/remote-ui.md`, « Structure in io.xml », l.534). Écrivain et
  lecteur ne se sont donc jamais rencontrés : ce qui était sauvé n'était jamais relu. E4.4cd avait
  reproduit le bug **verbatim et volontairement** ; T3.15 le corrige.
  **Migration : récupération plutôt qu'acceptation de la perte.** L'ancien écrivain déposait
  l'élément juste **avant** le `<calaos:remote_ui>` qu'il décrivait, `Room::LoadFromXml()` ignore
  les éléments inconnus (donc les orphelins sont toujours là), et rien d'autre dans le format
  n'écrit jamais un `<calaos:device_info>` sous une pièce : l'élément précédent le plus proche
  identifie l'ancien **sans ambiguïté**, y compris dans une pièce à plusieurs devices. ~15 lignes,
  one-shot (la valeur est réécrite au bon endroit et l'orphelin disparaît à la sauvegarde suivante).
  Périmètre exact après rebase : `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp`,
  `tests/core/RemoteUIDeviceInfo_test.cpp`, `tests/Makefile.am`, `RELEASE_NOTES.md` — **rien** sous
  `src/lib/` (`ConfigStore.cpp` appartient à E4.4dbis, concurrent). Rebase sur master (qui avait
  pris le commit board de lancement) **sans aucun conflit** — pas de conflit EOF sur
  `tests/Makefile.am` cette fois. Build d'intégration par défaut **51/51** tout vert.
  Tests via `Config`/`Room`/`IOFactory` (pas de nœuds fabriqués à la main) : round-trip identique
  valeur par valeur et deux fois de suite, adoption puis réécriture de la forme legacy, un device
  ne peut pas adopter l'orphelin de son voisin, un `device_info` vide n'écrit pas d'élément.
  ⚠️ La revue a d'abord rendu un **faux vert** en red-before-green à cause des surcharges
  `_DEPENDENCIES` de `tests/Makefile.am` (le binaire de test ne se relinke pas quand l'objet de
  production change) — piège méthodologique consigné dans FINDINGS.md. ff-only, worktree t3.15
  nettoyé.
- **E4.4dbis ✅ mergé** (2026-08-16, `1357ef7f`, **le dernier consommateur TinyXML** :
  `ConfigStore.cpp` / `local_config.xml` passe de TinyXML à pugixml. Périmètre exact après rebase :
  `src/lib/ConfigStore.cpp`, `src/lib/Utils.h`, `src/lib/XmlUtils.h` — **rien** sous
  `src/bin/calaos_server/IO/RemoteUI/**` (T3.15, concurrent, mergé entre-temps), rien sous
  `configure.ac`, `.github/` ou `docs/`. Rebasée **deux fois** (master a pris T3.15 puis le commit
  board de lancement d'E4.2d pendant le build) — **aucun conflit** aux deux passes. Build
  d'intégration par défaut **51/51** tout vert.
  ⚠️ **Conséquence majeure : TinyXML n'a plus AUCUN consommateur de code.** `grep -rn
  'TiXml\|TinyXPath' src/ --include='*.cpp' --include='*.h'` hors `src/lib/TinyXML/` ne rend plus
  que des **commentaires de prose** (15 lignes dans `ConfigStore.cpp`, `XmlUtils.h`,
  `CalaosConfig.cpp`, `WebCtrl.cpp` — elles documentent l'ancien comportement TinyXML pour
  justifier l'équivalent pugixml, aucune ne compile). **E4.4e (suppression du vendored
  TinyXML + TinyXPath) est donc débloqué.**
  2 divergences cosmétiques de revue (déclaration XML, BOM UTF-8) consignées dans FINDINGS.md —
  non bloquantes, sortie strictement mieux formée. ff-only, worktree e4.4dbis nettoyé.
- **E4.4e ✅ mergé** (2026-08-16, `93537ae4`) — **la migration pugixml est COMPLÈTE**
  (E4.4a / E4.4b / E4.4cd / E4.4dbis / E4.4e tous ✅). `src/lib/TinyXML/` (TinyXML 2.5.3 +
  TinyXPath) est supprimé : 48 fichiers, **−14 242 lignes** (12 818 LOC + ChangeLog/README/
  projets MSVC/XML d'exemple), commit 64 fichiers +26/−14 333. Purge des fichiers de build :
  `src/lib/Makefile.am` (33 entrées `TinyXML/*` + `-DTIXML_USE_STL`), `src/bin/calaos_server/
  Makefile.am` et `tests/Makefile.am` (`-DTIXML_USE_STL` + `-I$(top_srcdir)/src/lib/TinyXML`),
  `po/POTFILES.in` (34 entrées), `.github/workflows/ci.yml` (exclusion lcov + `:(exclude)`
  clang-format), `configure.ac`. Aucun `_LDADD`/`_SOURCES` séparé : ça compilait directement dans
  `libcalaos_common`. Les +26 lignes sont **uniquement** des commentaires/docs reformulés — aucun
  changement de code compilé. **CVE-2023-34194 (assert → `abort()` du serveur sur XML malformé) et
  CVE-2021-42260 (boucle infinie sur UTF-8 tronqué) sont désormais INATTEIGNABLES : le code
  vulnérable n'existe plus dans le dépôt.** Les 15 commentaires de prose qui citent l'ancienne API
  TiXml* sont **conservés** (ils justifient le code pugixml actuel), 5 lignes de cadrage reformulées
  pour qu'aucune ne pointe vers du code encore présent. `docs/refactoring/**` volontairement
  intouché (trace historique). Rebase sur master : **aucun conflit** (fast-forward direct). Build
  d'intégration distclean **51/51 PASS, 0 SKIP**. ff-only, worktree e4.4e nettoyé.
- **E4.2d ✅ mergé** (2026-08-16, `ac274657` + `7a400306`) — **4/6 de la série ownership**
  (E4.2a / E4.2b / E4.2c / E4.2d ✅). `Rule` possède ses `Condition`/`Action` et `ListeRule`
  possède ses `Rule` en `unique_ptr` ; les `delete` manuels de `Rule.cpp:39,42,132` et
  `ListeRule.cpp:101,124` disparaissent. Nouveau binaire `tests/core/RuleOwnership_test`
  → build d'intégration distclean **52/52 PASS, 0 SKIP** (51 + le nouveau). Périmètre strictement
  limité à `Rule.{h,cpp}`, `ListeRule.{h,cpp}`, `tests/core/RuleOwnership_test.cpp`,
  `tests/Makefile.am` — **aucun `src/lib/**`**, donc zéro interaction avec la suppression de
  TinyXML par E4.4e. Rebase sur master : **aucun conflit** (le conflit EOF redouté sur
  `tests/Makefile.am` ne s'est pas matérialisé).
  ⚠️ **Double-free latent corrigé au passage** : `RemoveRule(io)` pouvait détruire une règle
  d'auto-scénario sans annuler les back-pointers (`ruleStart`…) ; le `delete` ultérieur sur ce
  pointeur périmé dans `deleteAll()` était un **double free** — crash serveur possible en
  supprimant un IO référencé par un scénario. Le nouveau `Remove(Rule*)` refuse et logge au lieu
  de détruire un objet qu'il ne possède pas. Consigné dans RELEASE_NOTES.md (Fiabilité).
  2 non-bloquants de revue (gardes null de `AddCondition`/`AddAction`, comparaison de pointeur
  indéterminée dans un test) consignés dans FINDINGS.md. **Renumérotation** : les items 5 et 6 du
  plan E4.2 d'origine deviennent **E4.2f** et **E4.2g** (E4.2e était déjà pris par un ticket de
  décision utilisateur hors plan initial). ff-only, worktree e4.2d nettoyé.
- **T3.16 ✅ mergé** (2026-08-16, `23e6e2f8`) — **CI `format-check` réparé**, un seul fichier
  touché (`.github/workflows/ci.yml`, zéro C++). Deux défauts : (1) `src/lib/pugixml` manquait
  dans la liste d'exclusions du pathspec `git diff` alors que tous les autres tiers de
  `src/lib/**` y étaient — le tree pugixml importé par E4.4a était donc soumis à notre
  `.clang-format` ; (2) l'étape apt était **après** `actions/checkout@v4`, or l'image `debian:12`
  ne fournit pas `git` (vérifié : `docker run --rm debian:12 command -v git` ne renvoie rien) et
  l'action ne construit un vrai dépôt que si `git >= 2.18` est sur le PATH — sinon elle télécharge
  le tarball REST, ne laisse aucun `.git`, et le `git diff origin/<base>...HEAD` de l'étape
  suivante mourait en « not a git repository ». Corrigé en déplaçant l'apt (renommé « Install git
  and clang-format ») **avant** le checkout, `fetch-depth: 0` conservé ; `build-and-test` et
  `coverage` intouchés (ordre inchangé, mais ces jobs n'invoquent jamais git ensuite). Re-run
  local indépendant en `debian:12` (clang-format 14.0.6, même ligne de commande) : **31 329 →
  5 087 lignes**, 122 fichiers, **zéro ligne sous `src/lib/pugixml`** (delta 26 242 exactement
  attribuable à l'exclusion). Le reliquat est **notre propre dette de formatage**, consigné dans
  FINDINGS.md — il ne bloque pas les push (`format-check` est `pull_request`-only) et une PR de
  taille normale ne voit que ses propres lignes. Pas de build d'intégration (aucune entrée de
  build touchée). Rebase sur master : **aucun conflit**. ff-only, worktree t3.16 nettoyé.
- **E4.2e ✅ mergé** (2026-08-16, `e33cbbd5` + `fd2aba67`) — **décision utilisateur** : une règle
  qui référence un IO introuvable est **entièrement désactivée** au lieu de tourner **amputée**.
  L'ancien comportement rejetait la seule condition fautive et laissait la règle s'exécuter avec
  des critères incomplets, donc **plus permissifs** (`si absence ET après 22h` → `si après 22h`,
  déclenché tous les soirs). La règle reste **visible et intacte** en configuration (sauvegarde
  fidèle), est journalisée avec son nom et les ids manquants, **redevient active d'elle-même**
  quand l'IO réapparaît, et une **notification mail + push** (canal des configurations corrompues,
  plomberie `CalaosConfig.cpp`) le signale au démarrage. Le commit de suivi de revue traite l'**id
  vide** comme une dépendance manquante lui aussi — ce qui neutralise au passage le chemin de
  compatibilité audio/caméra qui appariait le **premier IO audio/caméra de la config** à une
  entrée sans id (cause : `Params::operator[]` renvoie `""` pour une clé absente). Nouveau binaire
  `tests/core/RuleDisabledMissingIo_test` (**21/21**) → build d'intégration distclean **53/53
  PASS** (52 + le nouveau). Périmètre : `Rules/**`, `Rule.{h,cpp}`, `ListeRule.{h,cpp}`,
  `CalaosConfig.cpp` (plomberie d'alerte), `tests/core/**`, `tests/Makefile.am` — **aucun
  `src/lib/**`** (T3.14 en vol) ni `.github/` (T3.16). Rebase sur master : **aucun conflit** (le
  conflit EOF redouté sur `tests/Makefile.am` ne s'est, là encore, pas matérialisé). Vérifié : les
  configurations réelles testées n'ont **aucune référence orpheline**, donc aucune règle n'y est
  désactivée. Consigné dans RELEASE_NOTES.md ; 3 suites non bloquantes dans FINDINGS.md
  (`Params::operator[]` latent, `RemoveCondition/RemoveAction` ne recalculent pas `missingIoIds`,
  `get_condition/get_action` sans garde de bornes). **Débloque** la passe `clang-format` de dette
  T3.16, qui attendait la libération de `ListeRule.cpp`. ff-only, worktree e4.2e nettoyé.
- **T3.14 ✅ mergé** (2026-08-16, `813943f7`) — **statiques immortels** : c'est l'option 1 du
  ticket qui a été retenue, celle qui supprime la **classe** de bugs au lieu d'un chemin. Les
  statiques de `libcalaos_common` réutilisés après leur propre destructeur (`_configBase`,
  `_cacheBase`, `configMutex`, `logger_domains`, `logger_hash`, `default_domain`) passent en
  singletons alloués sur le tas et **jamais détruits** — politique déjà écrite en `AGENTS.md:74`
  (« the server intentionally never frees a number of process-lifetime singletons »). Les **deux**
  instances tombent d'un coup : (1) la lecture pendante sur `_configBase` qui produisait à chaque
  arrêt `Parse error… <octets illisibles>/local_config.xml`, réamorcée par le `~_Hashtable` du
  cache de niveaux local à `maxLevelPrintable()` (vidé → `.empty()` vrai → tout le bloc d'init se
  ré-exécute), plus le `configMutex` verrouillé post-destruction ; (2) l'UAF ASan (`heap-use-after-free`,
  READ 8) sur `calaosLogger()` (`LogSetup.cpp:48`) atteint depuis `~ExternProcServer` ←
  `~WagoMap` ← `~WagoMapManager`, masqué en production par le seul `Utils::freeLoggers()` de
  `main.cpp:240`. Bilan mémoire **strictement meilleur** qu'avant : une fuite préexistante de
  **32 octets** disparaît. Nouveau binaire `tests/StaticLogShutdown_test` + son programme repro
  `StaticLogShutdown_helper` (`check_PROGRAMS` mais **pas** dans `TESTS` : l'instant intéressant
  est *après* le retour de `main()`, donc inobservable depuis un binaire gtest). Périmètre exact,
  7 fichiers : `src/lib/{ConfigStore,LogSetup,Logger}.cpp`, `tests/Makefile.am`, les 2 nouveaux
  tests, `.gitignore` — **aucun fichier E4.2e** (`Rules/**`, `Rule.*`, `ListeRule.*`,
  `CalaosConfig.cpp`) ni `.github/`. ⚠️ **Attention base** : la branche partait de `652e08a5` et
  master avait avancé de **11 commits** (E4.2e + toute la vague documentaire E4.0/E4.2f/E4.2g/
  T3.17) — rebasée avant merge. Conflit **unique et attendu** en fin de `tests/Makefile.am`
  (E4.2e et T3.14 ajoutent chacun leur bloc `HAVE_GTEST` en EOF), résolu **par régénération** :
  fichier complet de master + append du bloc `# T3.14` verbatim, aucun bloc existant touché,
  aucun réordonnancement → **42/42** `if HAVE_GTEST`/`endif` équilibrés (41/41 avant), et le
  préfixe de 1058 lignes vérifié identique à celui de master. Aucun conflit sur `docs/` et aucune
  fausse suppression de doc (les 93 fichiers de `docs/refactoring/` vérifiés identiques à master
  après rebase — l'artefact d'écart de base ne s'est pas matérialisé). Build d'intégration
  distclean : **54/54 PASS** (53 de master + `StaticLogShutdown_test`) ; le build dépasse 600 s,
  attendu par `docker wait` sur le conteneur retrouvé par son mount exact, **sans relance**.
  Consigné dans RELEASE_NOTES.md (plantage/sortie corrompue à l'arrêt = visible utilisateur) ;
  **4 suites non bloquantes** dans FINDINGS.md (garde-fou de l'instance 2 qui ne mord que sous
  `--enable-asan` ; `defaultCoutLogger()` qui peut allouer pendant atexit, contre l'intention
  affichée en `LogSetup.cpp:101-103` ; premier log tardif encore capable d'écrire brut sur `cout`
  si le cache est froid ; fuite préexistante sur double `initLogger()`, désormais invisible à
  LSan). ff-only, worktree t3.14 nettoyé.
- **E4.2f ✅ mergé** (2026-08-16, `faa952ea`) — **6/6, la série ownership E4.2 est CLOSE** (E4.2g
  ayant été abandonné après re-cadrage : 0/21 sites dangereux). Le dernier maillon tombe : les
  back-pointers bruts `Rule *` d'`AutoScenario` (`ruleStart`, `ruleStop`, `ruleStepEnd`,
  `rulePlageStart`, `rulePlageStop`, `vector<Rule *> ruleSteps`) passent par un `RuleRef` =
  `Rule *` + `weak_ptr<bool>` sur `Rule::aliveToken()`, **le jeton de vie qui existait déjà**
  (`Rule.h:111`, créé pour les callbacks de conditions script) — donc aucune nouvelle plomberie de
  durée de vie. E4.2d avait supprimé le **double-free** (`Remove(Rule*)` refuse de détruire ce
  qu'il ne possède pas) mais **pas le pointeur pendant** : `ListeRoom::deleteIO()` →
  `detachIOFromRules()` → `ListeRule::RemoveRule(io)` détruit **toute** règle citant cet id, y
  compris une règle d'étape dont l'IO cible avait été choisi via `addStepAction()`, sans annuler
  aucun back-pointer et sans relancer `checkScenarioRules()` (appelé **une seule fois**, au
  démarrage, `main.cpp:196`). Le `heap-use-after-free` était donc **atteignable depuis l'API
  JSON** : `scenario_del` d'un scénario B, puis le premier `get_scenarios`/`get_scenario`
  (`buildAutoscenarioList`, `JsonApi.cpp:1592-1598`) lisait en mémoire libérée via
  `Scenario::toJson()` → `getRuleSteps()`/`getStepPause()`. Le commentaire d'aveu de
  `RuleLifecycle_test.cpp:399-401` (« AutoScenario::ruleSteps still holds the freed pointer ») est
  devenu une assertion. Traité au passage : `IOBase.h:66` `AutoScenario *ascenario`, posé sur le
  seul `ioTimeRange` et **jamais remis à null**, relu en `JsonApi.cpp:1578,1581`. Périmètre exact,
  **5 fichiers** : `Scenario/AutoScenario.{h,cpp}`, `IOBase.h` (commentaire seul),
  `IO/Scenario.cpp` (2 gardes), `tests/core/RuleLifecycle_test.cpp` — **`tests/Makefile.am`
  intact**, **aucun binaire de test ajouté** (les cas nouveaux entrent dans le
  `core/RuleLifecycle_test` existant), et **aucune ligne** de `ListeRule.*`, `Rule.*`, `Room.*`,
  `ListeRoom.*`, `JsonApi.*`. Les back-pointers **mesurés inoffensifs** (`ioScenario`,
  `ioIsActive`, `ioScheduleEnabled`, `ioStep`, `ioTimer`, `ioTimeRange`, `roomContainer`,
  `ScenarioAction::io`, `Scenario::auto_scenario`) sont **délibérément non convertis** — cf. le
  finding de cadrage, pour qu'un futur passage ne « complète » pas la conversion par symétrie.
  **Aucun conflit** au rebase, ni sur `tests/Makefile.am` ni sur `docs/` ; l'artefact d'écart de
  base ne s'est pas matérialisé (94 fichiers de `docs/refactoring/` intacts). Rebasé **deux fois** :
  la branche partait de `a3334697`, master avait pris 3 commits de docs T3.18, puis un 4e
  (`262293d8`) est tombé **pendant** le build d'intégration — le second rebase ne ramène que des
  docs (`BOARD.md`, `T3.18.md`), l'arbre hors `docs/` étant **byte-identique** à celui validé.
  Build d'intégration distclean : **54/54 PASS**, compte inchangé comme attendu ; le build dépasse
  600 s, attendu par `docker wait` sur le conteneur retrouvé par son **mount exact**, **sans
  relance**. Consigné dans RELEASE_NOTES.md (plantage atteignable depuis l'API, section
  « comportements qui changent ») ; **2 suites non bloquantes** dans FINDINGS.md (`getRuleSteps()`
  purge **et** alloue, appelé dans la **condition de boucle** de `Scenario::toJson()` → O(n²),
  motif préexistant légèrement aggravé, cosmétique ; le `purgeDeadSteps()` d'`addStep()` est du
  **code mort** aujourd'hui, gardé en défense en profondeur). ff-only, worktree e4.2f nettoyé.
- **E4.0a ✅ mergé** (2026-08-16, `305b92a5` + `a8ac6c14`) — **la fondation du filet de
  caractérisation de l'API JSON est posée**, donc le préalable dur à la migration
  jansson → `nlohmann::json` (E4.1) est débloqué pour ses cinq sous-tickets b→f, qui sont
  **mutuellement indépendants** et parallélisables. **Zéro ligne de `src/`** : caractérisation
  pure, 6 fichiers de test + `tests/Makefile.am` + `docs/refactoring/E4.0.md`. Le harnais est
  **in-process** — oracle **sémantique** (les deux côtés sont parsés avec nlohmann et comparés
  par `operator==`, **jamais** de comparaison de chaînes sérialisées), goldens versionnés avec
  mode de mise à jour généré, session WS de test, requête HTTP one-shot sur un **vrai**
  `HttpClient` posé sur une socket non connectée, maison de référence, et pompage `run<NOWAIT>()`
  de la boucle uvw sans lequel aucun event n'est délivré. **34 cas**, nouveau binaire
  `core/JsonApiCharacterization_test`. Trois points ouverts du verdict **tranchés par la mesure**,
  et l'un contre la recommandation initiale du ticket : (1) **pas de stub** de `getClientIp()` —
  `JsonApiHandlerHttp.o` réclame trois symboles `HttpClient` dont `buildHttpResponse()`, qui **est**
  la logique de production que E4.0e doit épingler, donc la stubber reviendrait à caractériser le
  stub ; on linke la vraie fermeture, celle de `core/RemoteUIDeviceInfo_test` ; (2) la voie
  `JsonApiHandlerHttp` est couverte pour de vrai ; (3) `IPCam/StandardMjpeg.o` et
  `Audio/RoonPlayer.o` entrent dans la maison de référence pour **zéro objet de link
  supplémentaire** (mesuré au `nm -Cu` : chacun ne réclame que sa classe de base, déjà dans
  `CORE_SERVER_OBJECTS`) — `Squeezebox` écarté car il traîne `SqueezeboxDB`, `UrlDownloader`, les
  neuf objets AVR et linke `uv_tcp_connect`/`uv_write`, donc du réseau réel dans un test unitaire.
  Un **tripwire assumé** et **unique dans toute la série** :
  `TRIPWIRE_ExpectedRedInE41_JanssonEnsureAsciiEscapesTheWire` est le seul cas qui regarde les
  octets sur le fil ; il passera au rouge à la bascule sans que rien ne soit cassé, et E4.1 doit
  basculer l'attente **dans le commit même** qui change de bibliothèque. Aucun autre sous-ticket
  n'a le droit de copier ce motif. ⚠️ **Attention base** : la branche partait de `c10c12c9` et
  master avait avancé de **8 commits** (T3.14 + sa vague documentaire, T3.18, puis E4.2f et ses
  docs) — rebasée avant merge, puis **une seconde fois**, un commit de docs `DECISIONS.md`
  (`337ea975`) étant tombé pendant le build d'intégration ; ce second rebase ne ramène que des
  docs, l'arbre hors `docs/` étant **byte-identique** à celui validé. Conflit **unique et attendu**
  en fin de `tests/Makefile.am` (T3.14 déjà dans master et E4.0a ajoutent chacun leur bloc
  `HAVE_GTEST` en EOF), résolu **par régénération** : fichier complet de master (1079 lignes,
  préfixe vérifié identique) + append du bloc `# E4.0a` verbatim, aucun bloc existant touché,
  aucun réordonnancement → **43/43** `if HAVE_GTEST`/`endif` équilibrés (42/42 avant). Aucun
  conflit sur `docs/` et **aucune fausse suppression** : les 94 fichiers de `docs/refactoring/`
  intacts, et les **quatre corrections intentionnelles d'`E4.0.md`** ont bien survécu (propriété
  de fichier — `JsonApiHome_test.cpp` appartient à E4.0b, E4.0a possède
  `JsonApiCharacterization_test.cpp` ; recommandation de stub renversée ; extension
  caméras/player ; exception tripwire nommée). Build d'intégration distclean : **55/55 PASS**
  (54 de master + le nouveau binaire) ; le build dépasse 600 s, attendu par `docker wait` sur le
  conteneur retrouvé par son **mount exact**, **sans relance**. **Pas d'entrée RELEASE_NOTES.md** :
  aucun comportement utilisateur ne change. **2 suites** dans FINDINGS.md (le `dynamic_cast` de
  `buildJsonCameras()`/`buildJsonAudio()` est du **code défensif inatteignable**, E4.0b ne doit pas
  chercher à couvrir sa branche fausse ; le **backlog de la file d'events fuit d'un cas à l'autre**,
  un test de silence passe seul et échoue en suite complète — le drain de `TearDown()` est aussi
  porteur que celui de `loadReferenceHouse()`). ff-only, worktree e4.0a nettoyé.
- **T3.17a ✅ mergé** (2026-08-16, `11f7198a` + `98347915` + `1baaffc4`) — **premier sous-ticket
  de T3.17, et la preuve que la garde alive seule ne suffit pas**. La chaîne récursive
  `decodeGetPlaylist()`/`getNextPlaylistItem()` mourait de **deux** morts indépendantes, et le
  correctif traite les deux : (1) le **client se déconnecte** pendant qu'une réponse du lecteur est
  en vol → `weak_ptr` sur `apiAlive`, **une vérification par étage** (une chaîne de N callbacks
  demande N vérifications, N étant ici la longueur de la playlist), chaque sortie anticipée
  `decref`ant `jplayer`/`jplaylist` pour que la garde ne troque pas un UAF contre une fuite ;
  (2) le **lecteur est supprimé en vol** → l'`AudioPlayer*` brut ne traverse plus **aucune**
  frontière asynchrone, `getNextPlaylistItem()` prend désormais un `const string &playerId` et
  **re-résout l'IO par son id à chaque étape**, exactement le motif de `buildJsonState()` (T2.15).
  Le second UAF a été **confirmé réel par la revue** — adresses de tas distinctes (`0x612…` pour
  le chunk `AudioPlayer`, `0x60e…` pour le handler) et `WsTestSession` **encore vivante** au
  moment du crash, donc `apiAlive` **non expiré** : une garde par token seul aurait franchi
  `expired()` puis déréférencé le player libéré à la ligne suivante. C'est **la** leçon à porter
  dans T3.17b/c (consignée aussi dans FINDINGS.md). Réponse d'échec **factorisée** dans
  `playlistNoPlayerAnswer()`, partagée par le point d'entrée et par les étages async : un lecteur
  disparu en vol rend le **même** `{"success":"false"}` qu'un id inconnu — jamais une playlist
  silencieusement tronquée, et aucune forme d'erreur nouvelle inventée. Déclaration morte de
  `getNextPlaylistItem()` supprimée de `JsonApiHandlerHttp.h` au passage.
  **Discipline en deux commits respectée à la lettre** — c'est le cœur méthodologique du ticket :
  `11f7198a` pose le test de caractérisation **seul et vert avant toute modification de `src/`**
  (5 goldens `t317a_*.json`, voies WS et HTTP, playlist vide, id inconnu), `98347915` pose la
  garde, `1baaffc4` les corrections de revue (R1/R2/R3). Relu par un **relecteur indépendant** qui
  a reproduit toutes les mesures dans un clone reconstruit de zéro : verdict **MERGE**.
  ⚠️ **Le worktree avait servi à un build ASan pendant la revue** puis été reconfiguré sans : le
  `make distclean` en tête de commande n'était pas décoratif (piège des objets ASan périmés,
  documenté plus haut). Vérifié dans `config.log` que le build de validation est bien un
  `./configure` **nu** (0 occurrence de `fsanitize`, `CXXFLAGS = -g -O2`). Build d'intégration
  distclean : **56/56 PASS** (55 de master + le nouveau binaire `core/JsonApiPlaylist_test`) ; le
  build dépasse 600 s, attendu par `docker wait` sur le conteneur retrouvé par son **mount exact**,
  **sans relance**. **Aucun rebase nécessaire** : la branche partait déjà de `1b2567a5`, tête de
  master — donc **aucun conflit `tests/Makefile.am`**, le bloc `# T3.17a` s'ajoutant seul en EOF →
  **44/44** `if HAVE_GTEST`/`endif` équilibrés (43/43 avant). Aucune fausse suppression de docs :
  les 94 fichiers de `docs/refactoring/` intacts. Consigné dans RELEASE_NOTES.md (plantage
  atteignable depuis l'API, section « comportements qui changent ») ; **1 suite + 1 note** dans
  FINDINGS.md (R4 : fuite préexistante si l'objet de connexion meurt **sans jamais** rappeler,
  hors périmètre, à traiter au niveau de l'épique ; note T3.17b/c sur la double mort).
  **T3.17 reste 📋** — ses sous-tickets b/c/d/e ne sont pas faits. ff-only, worktree t3.17a nettoyé.
- **E4.0b ✅ mergé** (2026-08-16, `c562b084`) — **le modèle et l'état de l'API JSON sous filet** :
  les **9 commandes × 2 transports** (`get_home`, `get_io`, `get_state`, `get_states`, `query`,
  `get_param`, `set_param`, `del_param`, `set_state`), deuxième sous-ticket de la série E4.0 qui
  doit précéder la migration jansson → `nlohmann::json`. **Caractérisation pure : zéro ligne de
  `src/`**, vérifié **sur le commit** et pas seulement sur l'arbre — les 10 fichiers touchés sont
  tous sous `tests/`. Nouveau `tests/core/JsonApiHome_test.cpp` (1360 lignes), 4 goldens
  `e40b_*.json`, et **régénération assumée** des goldens `ws_get_home.json`/`http_get_home.json`
  d'E4.0a, conséquence directe de l'enrichissement de la maison de référence.
  **La réserve de fond de la revue mérite de survivre au ticket.** Le relecteur indépendant
  (verdict **MERGE AVEC RÉSERVES**) a démontré **par contre-mutation** que **6 clés de
  `buildJsonIO()` n'étaient jamais observées en présence** : la maison ne posait aucun des params
  optionnels, si bien que leur suppression de la production laissait la suite **verte** (52/52 sur
  du code muté) — un filet qui ne prouvait rien. L'implémenteur a fermé les réserves en enrichissant
  `HOUSE_ACCENTED` des **7 params optionnels** (`hits`, `chauffage_id`, `unit`, `auto_scenario`,
  `step`, `io_style`, `value_warning`) **en laissant les autres IOs volontairement pauvres** : c'est
  le contraste *à l'intérieur d'un même golden* qui épingle le contrat d'absence. Sous la même
  mutation la suite donne désormais **3 rouges dont un qui nomme la clé perdue**, et — effet de bord
  bénéfique — **le binaire d'E4.0a passe au rouge lui aussi** : l'enrichissement renforce le filet
  des deux tickets. Consigné en FINDINGS.md avec l'interdiction explicite d'« harmoniser » la maison
  plus tard.
  **Aucun conflit `tests/Makefile.am`** malgré l'attente : master n'avait pas touché le fichier
  depuis la base de branche, le bloc `# E4.0b` s'ajoutant seul en EOF (append pur de 55 lignes) →
  **45/45** `if HAVE_GTEST`/`endif` équilibrés (44/44 avant). Rebase de `41167db7` vers `2f9e3d9f`
  (3 commits **docs-only** : FINDINGS, ORCHESTRATION, T3.17) **sans conflit**, suivi du
  `make distclean` réglementaire — la variante FAUX ROUGE du piège `_DEPENDENCIES` documentée
  plus bas a été rencontrée **sur ce ticket précisément**. Build d'intégration distclean :
  **57/57 PASS** (56 de master + le nouveau `core/JsonApiHome_test`), `core/JsonApiCharacterization_test`
  d'E4.0a **toujours vert** sur les goldens régénérés. Le build dépasse 600 s, attendu par
  `docker wait` sur le conteneur retrouvé par son **mount exact**, **sans relance** et sans toucher
  aux 3 conteneurs voisins (t3.17b, t3.17d, t3.17e). Aucune fausse suppression de docs : les
  **94** fichiers de `docs/refactoring/` intacts. **Pas d'entrée RELEASE_NOTES.md** — E4.0b ne
  change aucun comportement utilisateur. **E4.0 reste 📋** — c/d/e/f ne sont pas faits. ff-only,
  worktree e4.0b nettoyé, **rien n'a été poussé**.
- **T3.17d ✅ mergé** (2026-08-16, `d4aa5970` + `200ac007`) — **l'instantané caméra `get_picture`
  gardé contre la mort du client**, seul site de la série T3.17 qui vit dans un **handler**
  (`JsonApiHandlerHttp::processCamera()`) au lieu de `JsonApi.cpp`, donc le seul non couvert
  transitivement par le token `apiAlive` de la classe de base. Périmètre tenu au cordeau :
  `src/bin/calaos_server/JsonApiHandlerHttp.cpp` **+27/−1**, nouveau
  `tests/core/JsonApiCameraSnapshot_test.cpp` (450 l., 9 cas), append sur `tests/Makefile.am` —
  **zéro ligne de `JsonApi.cpp`** (il appartient à T3.17b/c) et **aucun `.reset()`** ajouté.
  Revue indépendante (le relecteur a tout reconstruit dans ses propres copies) : **MERGE AVEC
  RÉSERVES, réserves documentaires uniquement, aucune sur le code**. Trace ASan reproduite
  (`heap-use-after-free`, READ de 8 octets, **48 octets dans une région de 272** libérée par
  `~JsonApiHandlerHttp()` `:53`) et **confirmée au gdb** (`sizeof(JsonApiHandlerHttp) = 272`,
  membre `httpClient` à l'offset 48). Discipline **red-before-green** vérifiée octet à octet : le
  fichier de test de HEAD commence par les **12242 octets** du commit 1 à l'identique, hunk unique
  en append — invariant **reconfirmé après rebase**.
  **Conflit `tests/Makefile.am` en fin de fichier, résolu par régénération** : git avait fusionné
  les corps `LDADD` identiques et ne laissait en conflit que les lignes d'en-tête, produisant
  **trois** hunks entrelacés. Recousus ligne à ligne ils auraient donné un `LDADD` chimérique ;
  résolution appliquée = **fichier complet de master + append verbatim des 51 lignes de la
  branche** (bloc `# T3.17d`), aucun bloc existant touché, aucun réordonnancement →
  **46/46** `if HAVE_GTEST`/`endif` équilibrés (45/45 avant).
  Rebase de `d7b4b70f` vers `60207b4d` suivi du `make distclean` réglementaire (variante FAUX
  ROUGE du piège `_DEPENDENCIES`). Build d'intégration distclean : **58/58 PASS** — 57 de master
  (54 binaires + les 3 entrées de scripts `check-config-options.sh`, `check-config-docs.sh`,
  `run-python-tests.sh`) **+ le seul nouveau binaire** `core/JsonApiCameraSnapshot_test` ; compte
  **déduit avant le build** puis confirmé. Le build dépasse 600 s : attendu par `docker wait` sur
  le conteneur retrouvé par son **mount exact**, **sans relance**, sans toucher aux 3 conteneurs
  voisins (t3.17b, t3.17e, e4.0c). Aucune fausse suppression de docs : les **94** fichiers de
  `docs/refactoring/` intacts.
  **Trois corrections documentaires portées dans le même merge, dont deux évitaient d'induire les
  sous-tickets en vol en erreur** : (1) les lignes citées par la section *Acquis T3.17d* de
  `T3.17.md` — donnée en **modèle de référence à T3.17e** — étaient celles d'**avant** le patch et
  décalaient de **+27** ; recalées sur le fichier réellement mergé (fonction `:1043-1081`,
  re-résolution par id `:1045-1047`, gardes `:1057` et `:1075`) **plus une note invitant à se
  repérer aux noms de symboles** ; (2) la raison 2 du « second UAF n'est PAS universel » affirmait
  qu'« une caméra détruite en cours de transfert ne rappelle **jamais** » — vrai sur le chemin
  nominal, **faux via la branche `isRunning()`** (`IPCam.cpp:124-128`, `Timer::singleShot(0, ...)`
  non annulable et sans jeton que `~IPCam()` n'annule pas), ce qui contredisait `FINDINGS.md` du
  même auteur ; **conclusion inchangée**, la raison 1 (la lambda ne nomme jamais `camera`) suffit
  seule et a été reproduite sous ASan ; (3) le « corollaire mesuré pour T3.17c » disait **15**
  méthodes appelant `processDbResult()` — c'est **14**, `audioDbGetTrackInfos` (`JsonApi.cpp:1529`)
  ne l'appelle pas — et surtout concluait que T3.17c était « le candidat le plus probable pour que
  le second UAF soit de nouveau réel », **ce que le code ne soutient pas** : aucun des 15 corps ne
  nomme `player`, le `this` capturé est l'UAF n°1 que `apiAlive` corrige. Laissée telle quelle,
  cette phrase envoyait T3.17c « corriger » 15 méthodes sans raison, transformant des réponses
  complètes et correctes en erreurs. Bilan par site mis en cohérence.
  **⚠️ Les lignes `JsonApi.cpp` inscrites dans T3.17.md sont celles de master au merge de T3.17d**
  (les 14 lambdas à `1096, 1129, 1162, 1195, 1228, 1261, 1293, 1325, 1357, 1389, 1422, 1455, 1487,
  1523`), **pas** celles du rapport de revue de T3.17b, qui étaient mesurées sur sa branche et
  décalées de ~+30 par ses propres insertions. Le décompte (14) et les noms de symboles, eux, sont
  stables ; le merge de T3.17b redécalera ce bloc.
  FINDINGS.md : 3 suites ajoutées à la section existante `## T3.17d — suites` (portée réelle du
  filet — `FakeSnapshotCamera` surcharge `downloadSnapshot`, donc les deux cas de mort de caméra
  épinglent le **handler** et pas la plomberie `IPCam` ; `EmptyDownloadAnswersTheFallbackPicture`
  n'assère ni `Content-Length` ni le corps, `camfail.jpg` étant absent de l'arbre de test) plus un
  finding **préexistant** sorti par la revue de T3.17b et qui attend T3.17c sur son chemin :
  `player->get_database()->...` sans contrôle de nullité en `JsonApi.cpp:951` **et dans les 15
  `audioDbGet*`**, alors que `AudioPlayer::database` vaut `nullptr` par défaut
  (`AudioPlayer.cpp:28`) et qu'**aucun** des deux handlers ne filtre sur `canDatabase()`
  (0 occurrence ; l'unique usage, `JsonApi.cpp:406`, ne fait que publier la capacité).
  Entrée RELEASE_NOTES.md ajoutée (plantage atteignable depuis l'API ; un client **encore
  connecté** reçoit toujours son image complète, y compris si l'IO caméra est supprimé en cours de
  transfert). **T3.17 reste 📋** — b/c/e ne sont pas mergés. ff-only, worktree t3.17d nettoyé,
  **rien n'a été poussé**.
- **T3.17b ✅ mergé** (2026-08-16, `f87e1489` + `4a844b02`) — **les 5 méthodes `audio*` mono-coup
  de `JsonApi.cpp` gardées par `apiAlive`** contre la mort du handler pendant l'aller-retour :
  `audioGetDbStats`, `audioGetPlaylistSize`, `audioGetTime`, `audioGetPlaylistItem`,
  `audioGetCoverInfo` — couvertes **transitivement sur les 10 sites d'appel** (les 5 méthodes × 2
  transports, `JsonApiHandlerWS.cpp:354/359/364/369/386` et
  `JsonApiHandlerHttp.cpp:692/697/702/707/787`). Périmètre tenu : `JsonApi.cpp` **+33**,
  `JsonApi.h` **+5/−3 (commentaire seulement)**, nouveau `tests/core/JsonApiPlayerState_test.cpp`
  (786 l.), **15 goldens** `t317b_*.json`, append sur `tests/Makefile.am` — **aucun handler édité**
  et **aucun `.reset()`** ajouté. Revue indépendante : **MERGE AVEC RÉSERVES, réserves
  documentaires uniquement, aucune sur le code** ; trace ASan du premier UAF reproduite **à
  l'adresse près** (`0x60e000000838`), rouge-avant-vert non-ASan reproduit (5
  `ApiGoneBefore*Answer` rouges puis 30/30), `detect_leaks=1` propre, `--gtest_shuffle` vert sur
  3 graines, goldens **générés** (régénération à churn nul).
  **Point de fond validé, à ne pas « corriger » plus tard** : la **re-résolution par id n'a pas été
  appliquée, et c'est correct**. Aucun des cinq corps de lambda ne nomme `player` ; un `[=]` ne
  capture que ce qu'il **odr-use**, donc le pointeur n'est même pas dans la closure. Mesuré en
  remettant la version pré-correctif sous ASan (IO détruite + callback tiré) : **zéro rapport,
  5/5 PASS**. L'appliquer aurait été du **code mort** *et* aurait transformé une réponse complète
  en erreur — violation de l'invariant T3.17 « les formes de réponse ne changent pas ».
  **Conflit `tests/Makefile.am` en fin de fichier, à nouveau résolu par régénération** : comme au
  merge de T3.17d, git avait fusionné les corps `LDADD` identiques et ne laissait en conflit que
  les en-têtes, produisant **trois hunks entrelacés** (`1198/1216`, `1240/1247`, `1276/1334`).
  Résolution = **fichier complet de master (1302 l.) + append verbatim du bloc `# T3.17b`
  (67 l.)**, vérifiée comme **append pur** (`diff` = +68/−0/~0, aucun bloc existant touché) →
  **47/47** `if HAVE_GTEST`/`endif` équilibrés (46/46 avant), 1370 lignes.
  ⚠️ **Le `LDADD` de T3.17b contient légitimement `Audio/AudioDB.$(OBJEXT)` en plus du gabarit** —
  rendu nécessaire par `AudioPlayer::database = nullptr` ; jugé **inerte** par la revue
  (`AudioDB.cpp` = ctor + dtor, aucun réseau/timer/process). Ne pas le « nettoyer ».
  Rebase de `d7b4b70f` vers `7fc4556d` **sans conflit dans `JsonApi.cpp`** (master n'y avait pas
  touché depuis T3.17a), suivi du `make distclean` réglementaire (variante FAUX ROUGE du piège
  `_DEPENDENCIES`). Build d'intégration distclean : **59/59 PASS**, 0 FAIL / 0 ERROR / 0 SKIP —
  58 de master **+ le seul nouveau binaire** `core/JsonApiPlayerState_test` ; compte **déduit avant
  le build** (`TESTS` = 56 `check_PROGRAMS` − 1 helper `StaticLogShutdown_helper` + 3 entrées de
  scripts) puis confirmé. Build > 600 s : attendu par `docker wait` sur le conteneur retrouvé par
  son **mount exact**, **sans relance**, sans toucher aux 3 conteneurs voisins (t3.17e, e4.0c,
  e4.0d). Aucune fausse suppression de docs : les **94** fichiers de `docs/refactoring/` intacts.
  **Documentation : la branche n'en livrait aucune** (réserve R2 de la revue), comblée dans ce
  merge — BOARD, 2 divergences gelées en FINDINGS, entrée RELEASE_NOTES, et **recomptage des
  lignes de `T3.17.md`** annoncé par le merge précédent : le décalage vaut **exactement +33**, les
  14 lambdas `audioDbGet*` passent à `1129, 1162, 1195, 1228, 1261, 1294, 1326, 1358, 1390, 1422,
  1455, 1488, 1520, 1556` et `audioDbGetTrackInfos` (15ᵉ, **n'appelle pas** `processDbResult`) à
  `:1562` (lambda `:1576`, corps `:1578`). Trois pointeurs **déjà périmés avant ce merge** corrigés
  au passage : `getAudioPlayer()` était annoncé `:856`, il est à **`:918`** (inchangé par T3.17b,
  vérifié pre/post), et l'inventaire d'origine « 22 méthodes sans garde » a reçu un encadré de
  péremption avec les 15 définitions remesurées. **T3.17 reste 📋** — c et e ne sont pas mergés.
  ff-only, worktree t3.17b nettoyé, **rien n'a été poussé**.
- **E4.0c ✅ mergé** (2026-08-16, `fa75299a`) — **plages horaires et autoscénarios sous filet** :
  `get_timerange`, `set_timerange` et les **sept sous-commandes `autoscenario`** → **9 opérations,
  52 cas, 9 goldens**, troisième sous-ticket de la série E4.0 qui doit précéder la migration
  jansson → `nlohmann::json`. **Caractérisation pure : zéro ligne de `src/`**, vérifié **sur le
  commit** (`git show --name-only`) et pas seulement sur l'arbre — les 11 fichiers touchés sont
  tous sous `tests/`. Nouveau `tests/core/JsonApiScenario_test.cpp` (2004 l.), 9 goldens
  `e40c_*.json`, append sur `tests/Makefile.am`. **Aucun golden existant régénéré.**
  **La réserve de fond de la revue mérite de survivre au ticket — c'est le mode de défaillance
  d'E4.0b, à l'identique.** Le relecteur indépendant a démontré **par contre-mutation** que `cycle`
  et `enabled` n'étaient **jamais observés en désaccord** (`false/false` dans cinq goldens,
  `true/true` dans le sixième) : **échanger les deux noms de clés laissait 52/52 vert**.
  L'implémenteur a fermé la réserve avec **deux témoins indépendants**, puis — et c'est le bon
  réflexe, il ne s'est pas arrêté à la paire signalée — a **balayé tout `Scenario::toJson()`** avec
  le même critère : `id`↔`schedule` → **13 rouges**, `category`↔`steps_count` → **15 rouges**,
  `step_pause`↔`step_type` → **13 rouges**, action `id`↔`action` → **13 rouges**. **Aucune autre
  paire aveugle.** Consigné en FINDINGS.md avec la règle : c'est le **désaccord** entre clés
  symétriques qui épingle le contrat, pas leur présence — ne pas « harmoniser » les goldens plus
  tard.
  **Aucun conflit `tests/Makefile.am`** : la branche était **déjà rebasée sur `97bddbfa`**, qui
  était encore la tête de master au moment du merge (aucun autre merge n'est passé entre-temps),
  donc `git merge --ff-only` **sans rebase et sans conflit**. Bloc `# E4.0c` ajouté seul en EOF
  (append pur de 65 lignes) → **48/48** `if HAVE_GTEST`/`endif` équilibrés (47/47 avant). Le
  `LDADD` reprend la **même clôture de lien que `core/JsonApiHome_test`** (E4.0b), pour la même
  raison mesurée : `JsonApiHandlerHttp.o` a besoin du **vrai** `HttpClient::buildHttpResponse()`,
  et les objets caméra/audio entrent dans la clôture par l'objet de harnais partagé. Ne pas le
  « nettoyer ».
  Build d'intégration distclean : **60/60 PASS**, 0 FAIL / 0 ERROR / 0 SKIP — 59 de master **+ le
  seul nouveau binaire** `core/JsonApiScenario_test` ; compte **déduit avant le build** (`TESTS` =
  57 `check_PROGRAMS` − 1 helper `StaticLogShutdown_helper` + 3 entrées de scripts = 59, dont la
  ligne `check-config-options.sh check-config-docs.sh` qui en apporte **deux** à elle seule) puis
  confirmé. Build > 600 s : attendu par `docker wait` sur le conteneur retrouvé par son **mount
  exact** (`/home/raoul/repos/calaos/calaos_base`), **sans relance**, sans toucher aux 3 conteneurs
  voisins (e4.0d, t3.17c, e4.0e). Aucune fausse suppression de docs : les **95** fichiers de
  `docs/refactoring/` intacts (**96** après ajout d'`E4.0g.md`).
  **7 divergences gelées** consignées en FINDINGS.md, toutes confirmées au source par la revue.
  Les trois qui comptent : (i) **le payload d'`autoscenario get` n'est pas ré-injectable dans
  `modify`** — un client qui renvoie ce qu'il vient de recevoir **renomme, masque et désactive** le
  scénario, avec `success:true` ; (ii) **l'index du tableau JSON sert de numéro d'étape**
  (`index_act = idx`), donc un step `end` mal placé fait **disparaître silencieusement** les
  actions des steps suivants ; (iii) **`autoscenario` échappe au `serviceScope`** alors qu'il
  **supprime** des scénarios, tandis que `set_timerange`, bien moins destructeur, y est soumis.
  S'y ajoutent le silence total sur `type` inconnu (socket HTTP **laissée ouverte**), l'asymétrie
  WS/HTTP sur un **troisième** périmètre (le `type` lui-même sous `data` en WS, à la racine en
  HTTP), les trois comportements de `set_timerange` (`ranges` absent ⇒ **tout effacé**, `months`
  court zéro-étendu, `day` hors 1..7 perdu) et `type_str` ≠ nom d'enum (`timerange_changed`).
  **Le constat (d) de T3.18 est mesuré ici pour la première fois** : une étape dont l'IO a disparu
  est rendue **sans son action et sans indication**, indistinguable d'une étape vide — épinglé par
  contraste entre `e40c_ws_autoscenario_get.json` et `e40c_ws_autoscenario_get_hot_deleted.json`.
  **Pas d'entrée RELEASE_NOTES.md** — zéro ligne de `src/`, aucun comportement utilisateur changé.
  **Nouveau ticket écrit dans ce merge : [`E4.0g`](E4.0g.md)** (📋 au board, sous l'épique E4.0),
  issu de la revue d'E4.0d : `JsonApiCharacterization.cpp:645-653` draine **avant**
  `CoreFixture::TearDown()`, alors que c'est `TearDown()` qui déclenche `clearCoreState()`
  (`CalaosCoreFixture.cpp:183-185`) et donc `~Room()`, qui lève **un `EventIODeleted` par IO**
  (`Room.cpp:77`). Ces events-là ne sont **jamais drainés** et fuient dans le cas suivant — d'où la
  rustine du drain en `SetUp()` **répétée par chaque sous-ticket**. Correctif = déplacer le
  `pumpEventLoop()` **après** `CoreFixture::TearDown()`, **à faire une fois E4.0d/e/f mergés**
  (trois agents travaillent sur ce harnais ; changer la sémantique du cycle de vie sous eux les
  casserait en silence), et à **valider en retirant les drains de `SetUp()` devenus superflus** —
  s'ils le sont vraiment, c'est la preuve que le correctif mord. ⚠️ Le commentaire du harnais est
  faux **sur la cause et sur le compte** (« un `EventIOAdded` par IO, 5 here ») : la maison porte
  **8** IOs, `Room::LoadFromXml()` est **muet**, et `EventIOAdded` n'existe qu'à
  `ListeRoom.cpp:466` — E4.0d corrige ces commentaires dans sa branche, E4.0g porte le **correctif
  structurel**, pas la doc.
  **E4.0 reste 📋** — d/e/f ne sont pas mergés, et g vient d'être ouvert. ff-only, worktree e4.0c
  nettoyé, **rien n'a été poussé**.
- **T3.17e ✅ mergé** (2026-08-16, `0303f323`) — **audit du transport WebSocket : le jeton
  `apiAlive` hérité suffit, aucun code de production changé.** Question posée par le ticket : le
  niveau de dérivation supplémentaire (`class WebSocket: public HttpClient`, `WebSocket.h:35`)
  ouvre-t-il un chemin que le jeton hérité de `JsonApi` ne couvre pas ? **Réponse mesurée : non.**
  Le handler est **créé** par `WebSocket::checkHandshakeRequest()` (`WebSocket.cpp:279`) mais
  **détruit par la base**, `~HttpClient()` (`HttpClient.cpp:162`) — `~WebSocket()`
  (`WebSocket.cpp:50-55`) ne touche pas à `jsonApi`. Propriétaire unique confirmé par grep
  exhaustif : **3 affectations** (`HttpClient.cpp:654`, `WebSocket.cpp:279`, `:288`) et **un seul
  `delete`** (`HttpClient.cpp:162`). Périmètre tenu et **littéralement zéro ligne de production** —
  ni `JsonApi.cpp` ni `JsonApiHandlerHttp.cpp`, **aucun `.reset()`**, **aucun golden**, **aucun
  `EXTRA_DIST`** : nouveau `tests/core/JsonApiWsTransport_test.cpp` (573 l., **7 cas**, oracle JSON
  sémantique, ids préfixés `t317e_`) + append de 48 l. sur `tests/Makefile.am`, vérifié **sur le
  commit**. Revue indépendante (worktree neuf, tout remesuré) : **MERGE, réserves documentaires
  uniquement**.
  **Ce que l'audit apporte au dossier T3.17** — c'est la « preuve par site » que l'`## Acceptation`
  de `T3.17.md` exige, et elle n'existait jusqu'ici que dans un message de commit ; elle est
  désormais écrite en clair dans `T3.17.md`, § *Acquis T3.17e*. Six points : propriétaire unique ;
  **branche d'échec d'auth `WebSocket.cpp:331`** (le ticket disait `:330`, **décalé d'une ligne**)
  où le handler est détruit **sans avoir jamais été rangé dans `jsonApi`** — ni
  `JsonApiHandlerWS::JsonApiHandlerWS` ni `RemoteUIWebSocketHandler::RemoteUIWebSocketHandler`
  n'écrivent `client->jsonApi = this`, donc **ni double destruction ni pendouillant** ; **troisième
  niveau de dérivation** (`RemoteUIWebSocketHandler`) qui a **son propre** `handlerAlive`
  (`RemoteUIWebSocketHandler.h:48`) et dont les **deux** sites async sont **déjà gardés** (`:95`,
  `:236`) ; **`JsonApi.h` déclare 26 méthodes à `std::function`, pas 25** — la 26ᵉ est
  **`buildJsonEventLog` (`JsonApi.h:164`)**, seule à prendre un `std::function<void(Json &)>`, ce
  qui explique qu'elle ait échappé aux inventaires, isolée en **T3.17f** ; **`buildJsonStates`
  (`JsonApi.cpp:600`) et `buildQuery` (`:641`) sont SYNCHRONES** — `result_lambda` appelé inline sur
  **toutes** les branches, **sorties d'erreur `wrong id` comprises** (`:610`/`:638`, `:651`/`:660`)
  — **rien à garder, un jeton y serait du code mort** ; et **`handleEvents` est sûr par
  `sigc::trackable`, pas par jeton**, ce qui est **structurellement plus fort ici** : pas de lambda
  capturant `this` mais un **slot** `sigc::mem_fun` (`JsonApiHandlerWS.cpp:37`), donc le différé est
  côté **émission** — la liste de slots est consultée **au moment de l'`emit`** et un slot
  déconnecté n'est **jamais appelé**, là où un jeton n'arrive qu'après ré-entrée. Déconnexion
  **double** (`evcon.disconnect()`, `JsonApiHandlerWS.cpp:40-43`, + `~sigc::trackable`), boucle
  monothread : **un jeton y serait redondant**.
  ⚠️ **Une affirmation du dossier d'audit a été mesurée FAUSSE à ce merge et corrigée, pas
  recopiée.** La « preuve incidente » annoncée — `~RemoteUIWebSocketHandler` →
  `removeWebSocketHandler` → `setOnline(false)` **lèverait un `EventIOChanged` pendant la
  destruction du handler** — n'existe pas : `RemoteUIManager::removeWebSocketHandler` appelle bien
  `remote_ui->setOnline(false)` (`RemoteUIManager.cpp:422`), mais `RemoteUI::setOnline`
  (`RemoteUI.cpp:356-361`) **n'émet rien** ; le seul émetteur est `emitChange()`
  (`RemoteUI.cpp:551-553`), appelé depuis `:516`/`:534`/`:545`, **aucun sur le chemin de
  destruction**. L'argument de fond tient sans elle — **c'est l'exemple qui était mauvais**. Vérifié
  deux fois, indépendamment.
  **Les réserves de la revue sont de vraies informations, versées en FINDINGS** (§ *T3.17e —
  suites*), et la première **corrige à la baisse** ce que l'audit croyait : **le trou de couverture
  était SOUS-déclaré**. `HttpTestRequest` construit un vrai `HttpClient` **sans jamais installer le
  handler dans `HttpClient::jsonApi`**, et un `grep -rn jsonApi tests/` restreint aux **sources** ne
  renvoyait **rien** avant ce merge — donc **`~HttpClient(){ delete jsonApi; }`, l'arête de
  propriété qui porte toute la démonstration de couverture transitive de T3.17, n'était exercée par
  AUCUN test, sur AUCUN des deux transports**, pas seulement sur WS. Trois autres réserves :
  **fenêtre d'ordre de destruction** — le sous-objet `JsonApi` meurt **strictement après** le
  sous-objet `WebSocket`, `jsonApi` est donc un instant **vivant au-dessus d'un transport à moitié
  détruit** et **`apiAlive` ne protège pas cette fenêtre** ; inatteignable aujourd'hui, mais tout
  futur `sendData.emit()` depuis un destructeur y ferait un **UAF** via le slot
  `[=]{ sendTextMessage(data); }` (`WebSocket.cpp:341-344`) ; **angles morts non déclarés** —
  `RemoteUIWebSocketHandler` n'est exercé par aucun cas, la branche `:331` est raisonnée mais non
  épinglée, `closeConnection` n'est pas câblé par le `WsTransport` de test alors que la production
  le câble (`WebSocket.cpp:345`) ; et **limite du test sur `handleEvents`** —
  `EventRaisedBeforeTheTransportDiesIsNeverDelivered` **ne distingue pas les deux mécanismes**,
  retirer `evcon.disconnect()` seul le laisserait **vert** (`trackable` prend le relais) : il épingle
  **l'invariant, pas le mécanisme**.
  **Conflit `tests/Makefile.am` en fin de fichier, résolu par régénération** — quatrième fois de
  suite, même signature : git fusionne les corps `LDADD` identiques et ne laisse en conflit que les
  en-têtes, en hunks entrelacés. Résolution = **fichier complet de master (1435 l.) + append verbatim
  du bloc `# T3.17e` (48 l.)**, vérifiée **octet à octet** dans les deux sens (les 1435 premières
  lignes identiques à `master:tests/Makefile.am`, les 48 dernières identiques au bloc de la branche)
  → **1483 lignes**, **49/49** `if HAVE_GTEST`/`endif` équilibrés (48/48 avant). ⚠️ **La branche de
  travail du relecteur `review/t3.17e-check` a été délibérément ignorée** : elle était rebasée sur
  `586f9b3b`, donc **antérieure à E4.0c** (`fa75299a`, +65 l. sur `tests/Makefile.am`) — son
  équilibre annoncé « 48/48 » était juste **sur sa base**, faux sur master. Un rebase propre a été
  refait ; la vérifier plutôt que lui faire confiance a évité de réintroduire un fichier amputé.
  Rebase `f957f022` → `0303f323` sur `84279c1a`, suivi du `make distclean` réglementaire (le
  worktree avait servi à un build **ASan** ; `configure` confirme **`AddressSanitizer
  (--enable-asan)..... no`**). Build d'intégration distclean : **61/61 PASS**, 0 FAIL / 0 ERROR /
  0 SKIP — compte **déduit avant le build** puis confirmé : **59 `check_PROGRAMS` − 1 helper
  (`StaticLogShutdown_helper`, jamais un test) + 3 entrées de scripts** (dont la ligne unique
  `check-config-options.sh check-config-docs.sh` qui en apporte **deux**) = **61 entrées `TESTS`**,
  soit les 60 de master **+ le seul nouveau binaire** `core/JsonApiWsTransport_test`. Build > 600 s :
  attendu par `docker wait` sur le conteneur retrouvé par son **mount exact** (`docker inspect`
  reconfirmé avant l'attente), **sans relance**, sans toucher aux **6** conteneurs voisins.
  Aucune fausse suppression de docs : les **97** fichiers de `docs/refactoring/` intacts, et les 4
  worktrees voisins (e4.0d, t3.17c, e4.0e, e4.0f) vérifiés intacts avant nettoyage.
  **Documentation : la branche n'en livrait AUCUNE**, comblée dans ce merge — BOARD, section
  *Acquis T3.17e* dans `T3.17.md`, 4 suites en FINDINGS, ce journal. **Pas d'entrée
  RELEASE_NOTES.md** — zéro ligne de production, aucun comportement utilisateur changé (le fichier
  ne liste que le visible utilisateur, les durcissements internes en sont exclus par son propre
  chapeau). **T3.17 reste 📋** — c et f ne sont pas mergés. ff-only, worktree t3.17e nettoyé,
  **rien n'a été poussé**.
- **E4.0d ✅ mergé** (2026-08-16, `d68e59f1`) — **caractérisation des events temps réel, la
  surface qui n'avait AUCUN test.** Quatrième sous-ticket de la série E4.0, sur le harnais E4.0a :
  nouveau `tests/core/JsonApiEvents_test.cpp` (1668 l., **57 cas**, ids préfixés `e40_`), **15
  goldens** `core/golden/e40d_*.json`, append de **67 l.** sur `tests/Makefile.am`, et corrections
  **de commentaires** dans `JsonApiCharacterization.{h,cpp}` + promotion additive de
  `member()`/`str()`. **Zéro ligne de `src/`, vérifié sur le commit** (`git diff --name-only
  master..HEAD -- src/` vide). Ce qui est épinglé : l'enveloppe WS (`msg:"event"` avec **`data`
  imbriqué dans `data`**, `type` ordinal **et** `type_str`, `event_raw`), la **numérotation des 24
  valeurs** de l'enum, l'encodage UTF-8 accentué, la stringification des 23 `type_str`, le gating
  `!loggedin`, la livraison **asynchrone** via l'idler `EventManager`, et l'équivalent HTTP
  `poll_listen` avec ses trois sous-actions. Revue indépendante : **MERGE AVEC RÉSERVES**, réserves
  **fermées** par l'implémenteur avant ce merge.
  **Ce ticket a corrigé une erreur du harnais que CINQ sous-tickets avaient lue.** Le commentaire
  de `loadReferenceHouse()` affirmait qu'un chargement de maison lève un `EventIOAdded` par IO,
  « 5 here » : **faux sur la cause ET sur le compte**. `EventIOAdded` n'a qu'**un** site,
  `ListeRoom.cpp:466` (chemin runtime de l'API) ; `Room::LoadFromXml()` est **muet** ; et la maison
  porte **8** IOs. Le vrai backlog vient de `~Room()` → `RemoveIO()` → `Room.cpp:77`, **après** que
  le fixture a pompé. Épinglé par `LoadingAHouseFromConfigRaisesNoEventAtAll`. ⚠️ **Le
  `pumpEventLoop()` n'a PAS bougé** — vérifié à ce merge en diffant `JsonApiCharacterization.cpp`
  **commentaires retirés** : **0 ligne de code changée**. Le correctif structurel reste
  [`E4.0g`](E4.0g.md), à faire une fois E4.0d/e/f mergés.
  **Cinq divergences gelées en FINDINGS** (§ *E4.0d — events*) : **5 types d'events morts** —
  `EventRoomAdded` (5), `EventRoomDeleted` (6), `EventRoomPropertyDelete` (8) sans aucun
  `create()`, `EventPushNotification` (22) qui n'existe que comme **tag** `HistEvent::event_type`
  (`ActionPush.cpp:105`), et surtout **`EventAudioPlaylistCleared` (18), mort par branche
  inatteignable** : son unique site (`Squeezebox.cpp:311`, sous `else if (p["2"] == "clear")` à
  `:306`) est **masqué** par `:290` qui consomme déjà `clear` — **un « playlist clear » rapporte
  donc `playlist_reload`** ; **11 des 23 `type_str` ne se déduisent pas du nom de la constante**
  (dont la faute `unkown`, gelée) ; **la numérotation de l'enum est du protocole** (`type` part sur
  le fil comme ordinal brut sur un enum non numéroté au-delà d'`EventUnkown = 0` : insérer une
  valeur au milieu décale tout pour les clients) ; **[SÉCURITÉ] une session `serviceScope`
  (sidecar MCP) reçoit TOUS les events** — `handleEvents()` (`JsonApiHandlerWS.cpp:53-60`) ne teste
  **que** `loggedin` et **jamais** `serviceScope`, alors que celui-ci est consulté sur **7**
  commandes du chemin requête/réponse (`:177-219`) : une session à qui `set_param`/`del_param`/
  `audio_db`/`set_timerange`/`eventlog`/`register_push`/`settings` sont refusés **reçoit tout le
  flux de la maison**, ids et valeurs compris. **Gelé, non corrigé — mérite son ticket.**
  **Limite de portée assumée, écrite noir sur blanc** : les 8 payloads audio et `io_status_changed`
  ne sont **pas opposables** (rien n'appelle Squeezebox/RoonPlayer/MqttCtrl dans la suite, les
  objets ne sont même pas liés) ; **4 payloads audio ont été corrigés** parce qu'ils gelaient des
  formes que la production n'émet pas — ce que ça achète n'est pas l'opposabilité mais que le
  golden **cesse d'affirmer une forme fausse**. Opposables : enveloppe, numérotation, encodage,
  stringification pour les **19** types atteignables ; formes de payload pour les **7** déclenchés
  par du vrai code de production.
  **Écart doc/code signalé mais NON corrigé ici** : `docs/08_http_api.md:210-221` décrit une
  enveloppe **jamais émise**, et sa liste `:224-231` contient 14 noms réels + 1 fantôme
  (`push_notification`, le code dit `push_notif`) en **omettant 9 types sur 23** ;
  `docs/10_events_notifications.md` ne documente **aucune** enveloppe de fil. ⚠️ **E4.0f est en
  train de corriger ces deux documents** — laissé intact pour éviter le double travail et un
  conflit inutile.
  **Conflit `tests/Makefile.am`** (fin de fichier, exactement le pattern documenté) : git avait
  **fusionné les corps `LDADD` identiques** et laissé **trois hunks entrelacés** ne portant que sur
  les en-têtes. Résolu **en régénérant** — fichier complet de master + append **verbatim** des 67 l.
  de la branche (vérifié : les 1483 premières lignes **byte-identiques** à master, les 67 dernières
  **byte-identiques** à la branche), **1550 l., 50/50 `if HAVE_GTEST`/`endif`**, zéro marqueur.
  Aucun conflit sur `JsonApiCharacterization.{h,cpp}` — master n'y avait pas touché depuis
  `fa75299a`. **62/62 tests** après `make distclean` + rebuild complet (piège `_DEPENDENCIES`
  neutralisé) : 61 statements `TESTS +=` mais **62 entrées** (la ligne
  `check-config-options.sh check-config-docs.sh` en porte deux), recoupé par 60 `check_PROGRAMS`
  − 1 (`StaticLogShutdown_helper`, pas un test) + 3 scripts = **62**. Les **7 binaires voisins**
  partageant le harnais revérifiés **verts** : `JsonApiCharacterization`, `JsonApiHome`,
  `JsonApiScenario`, `JsonApiPlaylist`, `JsonApiPlayerState`, `JsonApiCameraSnapshot`,
  `JsonApiWsTransport`. Aucune fausse suppression de docs : les **97** fichiers de
  `docs/refactoring/` intacts ; les 3 worktrees voisins (t3.17c, e4.0e, e4.0f) vérifiés intacts
  avant nettoyage. **Pas d'entrée RELEASE_NOTES.md** — zéro ligne de `src/`.
  **E4.0 reste 📋** — e, f, g ne sont pas mergés. Historique linéaire (cherry-pick sur master,
  pas de commit de merge), worktree e4.0d nettoyé, **rien n'a été poussé**.
- **T3.17c ✅ mergé** (2026-08-17, `14a9b2fb` + `ae5260e1`) — **les 15 méthodes `audioDbGet*` de la
  base musicale gardées ; dernier sous-ticket de garde de la série, l'épique T3.17 passe ✅.**
  Livraison : nouveau `tests/core/JsonApiMusicDb_test.cpp` (**78 cas**), **39 goldens**
  `core/golden/t317c_*.json`, append de **96 l.** sur `tests/Makefile.am`, `JsonApi.cpp` **+94/−0**
  (un bloc de commentaire partagé + 15 × `std::weak_ptr<bool> alive = apiAlive;` /
  `if (alive.expired()) return;`) et `JsonApi.h` **+4/−2 de commentaire seulement**. Périmètre
  vérifié sur le diff : **aucun handler édité** (`JsonApiHandlerHttp/WS` intacts), **aucun
  `.reset()` ajouté** (0 occurrence), 15/15 gardes et 15/15 tests d'expiration comptés au commit.
  Revue indépendante : **MERGE AVEC RÉSERVES**, réserves **fermées** avant ce merge.
  ⚠️ **Nit connu, délibérément NON corrigé** : le commentaire de `apiAlive` (`JsonApi.h:216-225`)
  écrit « *the remaining T3.17 work is the WS transport audit (T3.17e)* » — **faux depuis que
  T3.17e est mergé** (il l'était déjà avant cette branche). Non touché **pour ne pas invalider
  l'arbre validé par le build 63/63** ; à reprendre dans le prochain ticket qui édite ce fichier
  (T3.17f est le candidat naturel).
  **⚠️ Ce merge corrige LE critère de diagnostic de toute la série — il était FAUX.** La série
  décidait qu'une lambda avait besoin d'une garde en demandant « **est-ce que le corps odr-use
  `this` ?** », critère matérialisé par les avertissements `-Wdeprecated` de capture implicite.
  **Il produit des faux négatifs.** `audioDbGetTrackInfos` (corps
  `result_lambda(data.params.toJson())`) n'odr-use **ni `this` ni `player`**, n'émet donc **aucun**
  avertissement — et a **exactement le même** heap-use-after-free que les quatorze autres, parce que
  **`[=]` copie `result_lambda`**, un `std::function` qui **est** la lambda du dispatcher et détient
  le `this` du handler. Trace ASan reproduite, frame décisive
  `#6 std::function<void(json_t*)>::operator()` ← `#7 operator()` (`JsonApi.cpp:1578` avant ce
  merge, `:1671` après). **Corollaire du relecteur** : les 15 auraient le UAF **même sans**
  `processDbResult()` — la capture de `this` est un **second** objet libéré touché, pas le seul, et
  le décompte « 14/15 » **sous-compte par construction**. Le critère correct, écrit dans
  `T3.17.md` : *le danger est tout objet libéré atteignable depuis la closure, `this` **ou** un
  `std::function` capturé par valeur qui le détient ; l'absence d'avertissement `-Wdeprecated` ne
  prouve rien.* **Corrigé aux 4 endroits** où il était posé en critère de danger — `T3.17.md:174`
  (périmètre T3.17e), `:321` (bilan par site), `:336-346` (corollaire T3.17c, encadré neuf) et
  `:367-377` (sa conclusion) — plus une section dédiée `## 🛑 Correction du critère de diagnostic
  de la série` (`:386`). ⚠️ **Les numéros annoncés par la consigne de merge (`:208`, `:226`, `:252`)
  étaient périmés** ; seul `:174` tombait juste. Re-mesurer, ne pas recopier.
  **Ce qui reste vrai et ne doit PAS être rouvert, vérifié à ce merge et écrit explicitement dans
  `T3.17.md`** : **aucun sous-ticket n'a blanchi un site sur ce critère.** T3.17b a gardé **les 5**,
  dont **2 sans capture de `this`** ; T3.17d a **ajouté** sa garde (son « NON » porte sur le second
  UAF, le pointeur `IPCam*`) ; T3.17e s'appuie sur « **synchrone** » (`buildJsonStates`/`buildQuery`)
  et sur le **mécanisme de slot `sigc`** (`handleEvents`), pas sur les avertissements. **T3.17b,
  T3.17d et T3.17e ne sont pas entamés.**
  **Rebase obligatoire** : la branche était basée sur `d583322c`, master avait pris E4.0d depuis
  (`d68e59f1` + `6b534cd8`). Rebasée `82568aa1`/`a4e72755` → `14a9b2fb`/`ae5260e1`, puis
  `make distclean` réglementaire (piège `_DEPENDENCIES` / faux rouge neutralisé).
  **Conflit `tests/Makefile.am`** (fin de fichier) — **cinquième fois de suite**, même signature :
  git fusionne les corps `LDADD` identiques et ne laisse en conflit que les **en-têtes**, en hunks
  entrelacés. Résolu **en régénérant** : fichier complet de master (**1550 l.**) + append
  **verbatim** du bloc `# T3.17c` (**96 l.**), vérifié **octet à octet** dans les deux sens (diff
  vs master = `96 0`, et les 96 lignes ajoutées **identiques** au bloc extrait de la branche)
  → **1646 l.**, **51/51** `if HAVE_GTEST`/`endif` équilibrés (50/50 avant), zéro marqueur.
  **Aucun conflit sur `JsonApi.cpp`** — E4.0d n'y avait pas touché (vérifié, le commit 2 s'applique
  proprement).
  Build d'intégration distclean : **63/63 PASS**, 0 FAIL / 0 ERROR / 0 SKIP — compte **déduit avant
  le build** puis confirmé : **60 statements `check_PROGRAMS +=` → 61 binaires** (la ligne
  `tests/Makefile.am:1072`, `StaticLogShutdown_test StaticLogShutdown_helper`, en porte **deux**),
  **− 1** (`StaticLogShutdown_helper`, jamais un test) **+ 3 entrées de scripts** = **63**, recoupé
  côté `TESTS` : **62 statements → 63 entrées** (la ligne `check-config-options.sh
  check-config-docs.sh` en porte deux). Soit les **62** de master **+ le seul nouveau binaire**
  `core/JsonApiMusicDb_test`. **Sans ASan, vérifié dans `config.log`** : l'invocation est un
  `$ ./configure` **nu**, et `fsanitize` compte **0** occurrence dans `config.log`, `Makefile` et
  `tests/Makefile` — l'arbre laissé en configuration ASan par le relecteur avait bien été nettoyé.
  Build > 600 s : attendu par `docker wait` sur le conteneur retrouvé par son **mount exact**
  (`docker inspect` sur chaque conteneur, filtre `Source == /tmp/claude-1000/calaos-wave17/t3.17c`),
  **sans relance**, **sans jamais filtrer par image ni par ancêtre** ; les conteneurs voisins
  (e4.0e, et un conteneur de scratchpad tiers) n'ont pas été touchés.
  Aucune fausse suppression de docs : les **97** fichiers de `docs/refactoring/` intacts (dont
  `E4.0.md`, `E4.0g.md`, `T3.17.md`, `T3.17f.md`, `T3.18.md`, `T3.19.md`, `BOARD.md`,
  `FINDINGS.md`, `DECISIONS.md`, `RELEASE_NOTES.md`, `ORCHESTRATION.md`) ; worktrees voisins
  vérifiés intacts (`e4.0e` et `e4.0f` présents et **avancés** par leurs agents pendant ce merge ;
  `review-e40f` avait déjà été retiré par son propriétaire, pas par ce merge).
  **Documentation : la branche n'en livrait AUCUNE**, comblée dans ce merge — BOARD (**la ligne
  `T3.17c` n'existait tout simplement pas**, elle a été **créée** en ✅ à côté de a/b/d/e ; et
  l'épique **`T3.17` passe 📋 → ✅**, type corrigé en `epic` : son périmètre est **a+b+c+d+e**, les
  cinq sont mergés — **`T3.17f` est un ticket distinct**, avec sa propre ligne, son propre
  `T3.17f.md` et une dépendance *sur* T3.17c, ce n'est **pas** un sous-ticket de l'épique ; ceci
  débloque **T3.18**, qui dépend de `T3.17`), section *Correction du critère* + 4 corrections
  ponctuelles dans `T3.17.md`, **2 suites** en FINDINGS, **une entrée RELEASE_NOTES.md** (voir
  ci-dessous), ce journal.
  **Entrée RELEASE_NOTES.md, contrairement à T3.17e** : ce ticket corrige un **plantage atteignable
  depuis l'API JSON** sans manipulation particulière — interroger la base musicale pendant que le
  client se déconnecte. Placée dans « comportements qui changent », **par impact décroissant**,
  entre T3.17b (déclaré « le plus facile à déclencher ») et T3.17d.
  **Deux suites en FINDINGS.** (1) **Poche de fixture pauvre trouvée par la revue** : le macro
  `DB_METHOD_CASES` n'assérait **pas** `db->calls[0].nb` ; un **échange de clés** (`audioDbGetAlbums`
  lisant `"from"` au lieu de `"count"`) laissait la suite **78/78 verte**, donc **13 des 14 méthodes
  de liste pouvaient paginer avec la mauvaise taille de page** sans qu'un test ne bronche. Corrigé
  par un argument **`expNb`** assérté **sur les deux transports** (`:468` WS, `:485` HTTP) — le cas
  HTTP ne vérifiait que le nom de la méthode, or **le parsing de requête est du code par méthode et
  par transport, un transport ne prouve rien sur l'autre**. **Contrainte de maintenance consignée** :
  `from` et `count` doivent rester **deux nombres différents** (`2` et `7`), sinon les deux
  assertions passent même clés interverties et **la couverture disparaît en silence**.
  (2) **Bizarrerie gelée** : `processDbResult()` (`JsonApi.cpp:1079-1101`) fait
  `json_array_append_new(aret, p.toJson())` **sans exclure** le `Params` marqueur, qui ressort donc
  à la fois en `total_count` et dans `items`. ⚠️ **Nuance mesurée par E4.0f, consignée** : il ne
  **réordonne jamais**, donc `items[0]` n'est le marqueur **que sur les chemins où la source
  l'émet en premier** — `SqueezeboxDB::parseListAnswer()` traite `count` comme un **séparateur
  d'enregistrement** (`Audio/SqueezeboxDB.cpp:66-73`) et `getRandoms()` l'ajoute **en dernier**
  (`:774-775`). **Ne pas réécrire en « `items[0]` vaut toujours `{"count":"N"}` », c'est faux.**
  Merge **ff-only** (historique linéaire, pas de commit de merge), worktree `t3.17c` nettoyé par
  son **chemin exact** + `git worktree prune`, branche `refactor/t3.17c` supprimée, **rien n'a été
  poussé**.
- **E4.0e ✅ mergé** (2026-08-17, `31a4372c`) — **session, enveloppes et chemins d'erreur de l'API
  JSON**, le sous-ticket qui porte le **risque le plus concret** de la migration jansson →
  `nlohmann::json`. **102 cas, 29 goldens, zéro ligne de `src/`** (vérifié **sur le commit** : les
  31 fichiers sont sous `tests/`). Relu par un relecteur indépendant (verdict **MERGE AVEC
  RÉSERVES**, réserve **fermée et prouvée par mutation**).
  **LA correction de ce merge — la ligne la plus dangereuse du plan était fausse dans ses DEUX
  colonnes.** `E4.0.md` annonçait, sur l'UTF-8 invalide, « `json_dumps` renvoie `NULL` → HTTP
  **500** + fermeture » et, côté WS, « `jansson_to_string` rend une **chaîne vide** ». **Les deux
  branches sont du code mort**, pour la même raison : jansson refuse les octets **à la
  construction**, pas au dump — `json_string()` rend `NULL`, `json_object_set_new()` rend `-1` (sur
  valeur nulle **comme** sur clé invalide), et **aucun de ces codes de retour n'est testé**, ni dans
  `JsonApi.cpp` ni dans `Params::toJson()` (`src/lib/Params.cpp:134-147`). La paire est donc
  **silencieusement supprimée**, le conteneur reste bien formé et `json_dumps` **réussit** : **200 OK
  tronqué** sur HTTP, et sur WS **une enveloppe parfaitement formée à laquelle il manque un membre**
  — **plus insidieux** que la chaîne vide annoncée, rien ne signale l'absence. Audit exhaustif à
  l'appui : toute racine passée à `sendJson` est un `json_object()`, un `json_array()` ou le
  `json_pack` de `JsonApiHandlerHttp.cpp:271` dont les trois emplacements rendent inconditionnellement
  des tableaux ; `json_real()` n'est **jamais construit** dans tout `src/` ; l'unique `return nullptr`
  des 2110 lignes de `JsonApi.cpp` (`:2094`) est null-testé par son seul appelant (`:294-296`).
  **Ce qui arrivera à la bascule est l'INVERSE et plus grave** : nlohmann **accepte** ces octets dans
  l'arbre et **lève `type_error.316` depuis `dump()`** ; les deux `sendJson` dumpent **à nu** et
  **aucun des deux fichiers de handler ne contient un seul `try` ou `catch`** (vérifié : l'unique
  occurrence de `try` dans `JsonApiHandlerHttp.cpp` est le mot « trying » **dans une chaîne de
  log**, `:83`) → **`std::terminate` sur une connexion vivante**. On passe d'une réponse tronquée à
  un **abort de processus**, et **le canal d'injection est trivial** : `HfURISyntax::getQuery()`
  percent-**décode** (`hef_uri_syntax.cpp:363-368`) **avant** que `HttpClient.cpp:340-347` ne
  découpe, donc `?param=%ff%80x` met des octets arbitraires dans `paramsGET`,
  `JsonApiHandlerHttp.cpp:81-92` les recopie dans `jsonParam` et `buildJsonGetParam()` met le nom
  **fourni par le client** en **clé**. ⚠️ **Emplacements réellement corrigés** (le fichier avait été
  amendé plusieurs fois, les numéros de la consigne étaient à re-mesurer — ils tombaient juste ici) :
  **`E4.0.md:308`**, ligne « UTF-8 invalide » du tableau des pièges de bascule, **les deux colonnes
  réécrites** + l'arbitrage attendu de E4.1 (`error_handler_t::replace`/`ignore` **ou** `try/catch`
  sur tout dump de données influencées par le client, et choix explicite entre **« drop comme
  aujourd'hui »** et **U+FFFD**), avec renvoi au test
  `Utf8Trap_NlohmannDumpThrowsWhereJanssonDrops` qui met les deux bibliothèques **côte à côte sur les
  mêmes octets** et épingle **le code 316 exactement** ; et **`E4.0.md:349`**, ligne du tableau de
  découpage E4.0e, où **« les 8 refus `scopeDenied` » devient SEPT** — `JsonApiHandlerWS.cpp:177,
  182, 195, 202, 209, 214, 219`, le **8ᵉ résultat de grep étant la définition de la lambda**
  (`:159`) ; **`docs/08_http_api.md:276-278` listait déjà les sept bons, c'était le plan qui avait le
  compte faux**, la doc n'a pas été touchée. La mention « **le 500 sur UTF-8 invalide** » de cette
  même ligne 349 est corrigée dans la foulée.
  **Rebase obligatoire** : la branche était basée sur `d68e59f1`, master avait pris T3.17c depuis
  (`14a9b2fb` + `ae5260e1` + `1aa940fc`). Rebasée `494f2493` → `31a4372c`, puis `make distclean`
  réglementaire (piège `_DEPENDENCIES` / faux rouge neutralisé).
  **Conflit `tests/Makefile.am`** (fin de fichier) — **sixième fois de suite**, même signature : git
  fusionne les corps `LDADD` identiques et ne laisse en conflit que les **en-têtes**, en hunks
  entrelacés. Résolu **en régénérant**, jamais en recousant des fragments : fichier complet de master
  (**1646 l.**, 95022 o.) + append **verbatim** du bloc `#E4.0e` (**78 l.**, 5429 o.), vérifié
  **octet à octet dans les deux sens** (le résultat **commence** par le master verbatim, **finit**
  par le bloc verbatim, et `résultat == master + bloc` exactement) → **1724 l.**, **52/52**
  `if HAVE_GTEST`/`endif` équilibrés (51/51 avant), zéro marqueur de conflit.
  Build d'intégration distclean : **64/64 PASS**, 0 FAIL / 0 ERROR / 0 SKIP — compte **déduit avant
  le build** puis confirmé : côté `check_PROGRAMS`, **62 entrées − 1** (`StaticLogShutdown_helper`,
  qui n'est pas un test et partage sa ligne avec `StaticLogShutdown_test`) = **61 binaires**, **+ 3
  entrées de scripts** (`check-config-options.sh`, `check-config-docs.sh` — ces deux-là sur une seule
  ligne — et `run-python-tests.sh`) = **64**, recoupé côté `TESTS` : **64 entrées** exactement. Soit
  les **63** de master **+ le seul nouveau binaire** `core/JsonApiSession_test`.
  Build > 600 s : attendu par `docker wait` sur le conteneur retrouvé par son **mount exact**
  (`docker inspect` sur chaque conteneur, filtre `Source == /tmp/claude-1000/calaos-wave18/e4.0e`),
  **sans relance**, **sans jamais filtrer par image ni par ancêtre** ; les conteneurs voisins
  (`calaos-wave19/e4.0f`, `calaos-wave20/t3.17f`, et un conteneur de scratchpad tiers) n'ont pas été
  touchés.
  Aucune fausse suppression de docs : les **97** fichiers de `docs/refactoring/` intacts (dont
  `E4.0.md`, `E4.0g.md`, `T3.17.md`, `T3.17f.md`, `T3.18.md`, `T3.19.md`, `BOARD.md`, `FINDINGS.md`,
  `DECISIONS.md`, `RELEASE_NOTES.md`, `ORCHESTRATION.md`) ; worktrees voisins vérifiés intacts
  (`calaos-wave19/e4.0f` et `calaos-wave20/t3.17f` présents, en cours de build par leurs agents
  pendant ce merge).
  **Documentation : la branche n'en livrait AUCUNE**, comblée dans ce merge — BOARD (ligne `E4.0e`
  **créée** en ✅ sous `E4.0d` ; **l'épique `E4.0` reste 📋**, f et g n'étant pas mergés), les **deux
  corrections de `E4.0.md`** ci-dessus, une section `## E4.0e — session et chemins d'erreur` en
  FINDINGS (**5 suites**), ce journal. **Aucune entrée RELEASE_NOTES.md** : **zéro ligne de `src/`**,
  rien de ce que voit un utilisateur ne change.
  **Cinq suites en FINDINGS.** (1) **[CRASH, mérite son ticket] SIGFPE distant sur `eventlog`** :
  `JsonApi.cpp:2010` initialise `perPage = 100`, `:2013` appelle `Utils::from_string()` **dont le
  code de retour est ignoré** (`StringUtils.h:105-111`). Sémantique C++11 **vérifiée
  empiriquement** : chaîne **vide** → le sentry échoue **avant** `num_get`, 100 survit ; **non
  numérique** (`"abc"`, `"1,5"`, `"true"`) ou **`"0"`** → `num_get` s'exécute, échoue et **écrit
  0** ; **très grand** → **sature à `INT_MAX`**, inoffensif. `HistLogger::getEvents()` ne clampe pas
  et `HistLogger.cpp:268` fait `rowcount / ac->per_page` **dans le thread worker sqlite** →
  **SIGFPE, processus mort** ; le `try` de `:257` n'attrape rien, **un signal n'est pas une
  exception**. Déclenchable par `?action=eventlog&per_page=0` depuis **n'importe quel client
  authentifié, sur les deux transports**. **Non exercé délibérément** — le signal tuerait le binaire
  de test ; le mécanisme est épinglé par
  `FromStringWritesZeroOnFailureWhichIsWhyEventLogCanDivideByZero`. (2) **La ligne UTF-8 du plan
  était fausse dans ses deux colonnes** (résumé + renvoi vers la correction de `E4.0.md:308`).
  (3) **Fixture pauvre trouvée par la revue** : `id` et `created_at` de `HistEvent::toJson()` sont
  deux chaînes non déterministes et n'étaient assérées que par `is_string()`, donc
  **interchangeables** — **les échanger laissait 102/102 vert**, alors que `HistEvent::toJson()` est
  **réécrit en bloc par E4.1**. Corrigé par **rétention des uuids semés** et assertion **dans les
  deux sens** ; l'échange produit désormais **6 assertions rouges nommées**. (4) **Piège de harnais
  `HistLogger`** : le singleton capture `Utils::getCacheFile("events.db")` **dans son constructeur**
  et ouvre le fichier **dans un thread worker**, et `sqlite::database db(dbname)`
  (`HistLogger.cpp:190`) est **hors** du `try` de `:192` — un `cantopen` est un **throw non rattrapé
  dans un thread** → `std::terminate`. Résolu par `ensureHistLogger()` : répertoire à **durée de vie
  processus** créé **avant** `SetUp()`, avec un aller-retour synchrone qui **prouve** que le worker a
  ouvert la base **avant** que le chemin de cache ne change. (5) **Corroboration de T3.17f** : en
  montant ses mutations, le relecteur a **fait segfauter le binaire** en retirant la garde de portée
  d'`eventlog` — session détruite avec le callback `HistLogger` **en vol**. **L'UAF de T3.17f,
  reproduit en crash vivant**, ce n'est plus une lecture de code.
  Merge **ff-only** (historique linéaire, pas de commit de merge), worktree `e4.0e` nettoyé par son
  **chemin exact** + `git worktree prune`, branche `refactor/e4.0e` supprimée, **rien n'a été
  poussé**.
- **E4.0f ✅ mergé** (2026-08-17, `5c4c5f3f`) — **dernier sous-ticket de caractérisation de la
  série E4.0**. Deux moitiés : **24 cas / 20 goldens** sur le payload audio
  (`tests/core/JsonApiAudioPayload_test.cpp`) **et la réécriture de deux documents d'API** —
  `docs/08_http_api.md` (298 → 915 l.) et `docs/10_events_notifications.md` (153 → 465 l.),
  chaque exemple de payload marqué **capturé** (tracé à un golden) ou **dérivé** (tracé à un
  builder). **Zéro ligne de `src/`**, vérifié sur le commit (`git show --name-only` : 2 docs,
  `tests/Makefile.am`, 1 `.cpp`, 20 goldens = 24 fichiers, aucun `src/`).
  **Relu par un relecteur indépendant** : ~50 affirmations de la doc échantillonnées, **29
  revérifiées au source, zéro fausse** ; les 4 défauts trouvés étaient tous des **goldens cités
  infidèlement**, corrigés avant merge. Verdict MERGE AVEC RÉSERVES, **réserves fermées**.
  **Rebase obligatoire** : la branche était basée sur `6b534cd8`, master avait pris T3.17c **et**
  E4.0e depuis. Rebase → **un seul conflit**, `tests/Makefile.am` en fin de fichier
  (**septième occurrence du même pattern** : corps `LDADD` fusionnés, seuls les en-têtes en
  conflit, hunks entrelacés). Résolu en **« régénérer »** : fichier complet de master
  (**1618 l.**) + append **verbatim** du bloc `#E4.0f` (**4916 o.**), vérifié **octet à octet dans
  les deux sens** (`résultat == master + bloc`, `résultat[:len(master)] == master`,
  `résultat[len(master):] == bloc`). Équilibre **`if HAVE_GTEST` 53 == `endif` 53**.
  **Le bloc `member()`/`str()` a bien disparu du diff au rebase**, comme prévu : l'agent l'avait
  écrit **octet-identique** à celui d'E4.0d, au même ancrage, précisément pour que le rebase le
  résolve en no-op. Vérifié après merge : **une seule** `inline Json member(` et **une seule**
  `inline std::string str(const Json &j, const char *key)` dans `JsonApiCharacterization.h`.
  **`make distclean` avant tout chiffre** (variante FAUX ROUGE du piège `_DEPENDENCIES`, cf. plus
  bas), puis `autogen.sh && configure && make -j8 && make check` en conteneur.
  **Compte de tests : 64 → 65.** Raisonnement, pas comptage naïf : `check_PROGRAMS` passe de 62 à
  **63 entrées**, dont **1 n'est pas un test** (`StaticLogShutdown_helper`, sur une ligne à deux
  entrées avec `StaticLogShutdown_test`) → **62 binaires** ; `TESTS` porte ces 62 binaires **plus
  3 scripts shell** (la ligne `check-config-options.sh check-config-docs.sh` en porte deux, plus
  `run-python-tests.sh`) → **65**. E4.0f n'ajoute qu'**un** binaire,
  `core/JsonApiAudioPayload_test`.
  **`E4.0.md` : quatre décomptes alignés, dont un tranché comme convention.** Le dépôt portait
  **57/37/19/6** dans `E4.0.md` contre **56/36/18/4** dans les deux documents réécrits.
  **Trois sont des corrections de fait** (détail en FINDINGS §E4.0f) : **19 → 18 types d'events
  émis** (`EventAudioPlaylistCleared` inatteignable, masqué par la chaîne `else if` de
  `Squeezebox.cpp:290` ; nuance conservée : `EventPushNotification` ne passe **jamais** par
  `create()`, il est écrit en base par `ActionPush.cpp:105` et n'est visible que via `eventlog`) ;
  **6 → 4 opérations renvoyant des octets bruts** (`get_cover` de premier niveau et
  `get_camera_pic` finissent dans `exeFinished()`, `JsonApiHandlerHttp.cpp:574-591` → **JSON avec
  base64**) ; **`items[0]` n'est PAS toujours `{"count":"N"}`** (`processDbResult()` ne réordonne
  jamais ; `SqueezeboxDB::getAlbums_cb()` `:66-73` traite `count:` comme séparateur
  d'enregistrement et `getRandoms()` `:750-776` l'ajoute **en dernier**).
  **Le quatrième, 57 → 56 feuilles (et 37 → 36 sous-actions), est une CONVENTION, pas une
  erreur — tranchée explicitement dans `E4.0.md` pour que personne ne recorrige dans l'autre
  sens.** L'écart tient **entièrement** à `get_albums` (HTTP) / `get_album` (WS) : **deux noms de
  fil pour le même builder** `audioDbGetAlbums()`, ce que `E4.0.md` disait déjà lui-même.
  **Convention retenue : la colonne « Union » compte par identité de comportement, pas par
  orthographe de fil** — une opération présente sur les deux transports y est comptée **une
  fois**, ce qui est déjà le traitement des 17 commandes communes ; compter la paire deux fois
  ferait remonter dans l'union une **divergence d'orthographe entre transports** que les colonnes
  HTTP (53) et WS (44) portent déjà, chacune avec la sienne. Les colonnes par transport sont
  **inchangées**, seule l'union bouge, et tous les dénominateurs du document suivent désormais
  cette convention (`audio_db` union 17 → **16**, couverture « 2 sur 56 », doc « 6 sur 56 »,
  livraison « **36 des 56** », recoupée : 56 − 13 `audio_db` redondantes − 4 réponses binaires
  − 2 réponses base64 − `config/put` = 36).
  **Documentation** : BOARD (ligne `E4.0f` **créée** en ✅ — elle manquait ; **l'épique `E4.0`
  reste 📋**, `E4.0g`, correctif structurel du harnais, n'étant pas fait, et la ligne de l'épique
  le dit maintenant), les **quatre alignements de `E4.0.md`** ci-dessus, une section
  `## E4.0f — audio, doc d'API et décomptes corrigés` en FINDINGS, ce journal.
  **Aucune entrée RELEASE_NOTES.md** : **zéro ligne de `src/`**.
  **FINDINGS** — outre les trois corrections de fait : (1) **[BUG] `time_elapsed` perd de la
  précision sur le fil** — `Utils::to_string()` (`src/lib/StringUtils.h:112-118`) est un
  `ostringstream` **nu**, donc 6 chiffres significatifs puis scientifique : `1234.56789` devient
  `"1234.57"` (**3 décimales perdues**) et `123456789.0` devient `"1.23457e+08"`, qu'un `parseInt`
  naïf lit **1**. Épinglé par deux goldens, **gelé, pas réparé**. (2) **[BUG] `/api/v2` et
  `/api/v3*` passent le filtre de chemin sans handler** — `HttpClient.cpp:458-461` les laisse
  passer, `:653-659` logge « API version not implemented » et **`return` sans rien envoyer** :
  connexion **laissée en suspens**. (3) **[PIÈGE CLIENT] `event_raw` porte trois formes
  incompatibles sous le même nom de clé** — chaîne url-encodée plate (events live,
  `EventManager.cpp:190`), objet JSON imbriqué (`eventlog`, `HistLogger.cpp:93-100`), et
  `{message,pic_uid}` (`ActionPush.cpp:111-115`, sous-cas du second : contenu **stocké** ressorti
  par le conteneur d'`eventlog`). (4) **[PIÈGE CLIENT] `steps_count` ≠ longueur du tableau
  `steps`** — `IO/Scenario.cpp:95` ne compte que les étapes réelles, l'étape `step_type:"end"`
  synthétique est ajoutée **hors de la boucle** (`:125-142`) ; **invariant : `len(steps) ==
  steps_count + 1`, toujours** ; un client qui dimensionne sur `steps_count` **tronque les actions
  de sortie**. Contraste signalé : dans `get_playlist`, `count` **est** la longueur du tableau —
  **trois champs de comptage, trois sémantiques**. (5) **Fixture pauvre trouvée par la revue** :
  les 7 cas `processDbResult()` amorçaient **tous** un marqueur de count, si bien que remplacer
  `if (!scount.empty())` par `if (true)` laissait **62/62 vert** — alors que le contrat « aucun
  `count` → pas de `total_count` » est **publié aux clients** dans `08_http_api.md`. Comblé par
  `NoCountAnywhereMeansNoTotalCountKeyAtAll` + les deux cas de rejet croisé
  `get_albums`/`get_album`. (6) **L'ancien `10_events_notifications.md` était factuellement faux
  sur la configuration mail** — les vraies clés sont `notif/mail_sender`, `notif/mail_recipients`
  et `smtp_debug` (`ConfigOptions.cpp:676,686,693`), consommées par le binaire **hors-processus**
  `calaos_mail` (`NotifManager.cpp:106-117`) — et son exemple XML d'`ActionPush` était **inventé**.
  Merge **ff-only** (historique linéaire, pas de commit de merge), worktree `e4.0f` nettoyé par son
  **chemin exact** + `git worktree prune`, branche `refactor/e4.0f` supprimée, **rien n'a été
  poussé**. Voisin `/tmp/claude-1000/calaos-wave20/t3.17f` **vérifié intact** (worktree et
  conteneur de build), `docs/refactoring/` toujours à **97 fichiers**.
- **T3.17f ✅ mergé** (2026-08-17, `90e8883e`) — **dernier UAF de la série T3.17**, et le seul
  trouvé par un **audit qui s'était arrêté délibérément** plutôt que de le corriger à moitié
  (T3.17e). Deux commits : `f08930f6` (caractérisation, **aucun fichier de `src/`**) puis
  `90e8883e` (garde, **aucun golden**). Périmètre livré exactement comme cadré :
  `src/bin/calaos_server/JsonApi.cpp` **+30 l. dont 2 gardes**, `JsonApi.h` **commentaire
  seulement** (les 33 l. du diff sont toutes dans le bloc de commentaire au-dessus d'`apiAlive`),
  `tests/core/JsonApiEventLog_test.cpp` **20 cas**, **3 goldens** `t317f_*`, append sur
  `tests/Makefile.am`. **Aucun handler édité, aucun `.reset()` ajouté** (vérifié sur le diff).
  **Relu par un relecteur indépendant, quatre réserves fermées et prouvées par mutation.**
  **Pas de conflit à ce merge** : l'implémenteur avait **déjà rebasé** sur le master courant, la
  branche partait de `f20882cb` = `HEAD` de master → **fast-forward** sans résolution. Le pattern
  `tests/Makefile.am` a donc été **payé au rebase, pas au merge** ; l'append a tout de même été
  revérifié **octet à octet dans les deux sens** : résultat (**109 433 o. / 1850 l.**) ==
  master (**105 367 o. / 1792 l.**) + bloc `#T3.17f` (**4 066 o. / 58 l.**) **verbatim**, et
  `résultat == branche` à l'octet. Équilibre **`if HAVE_GTEST` 54 == `endif` 54**.
  **`make distclean` avant tout chiffre** (variante FAUX ROUGE du piège `_DEPENDENCIES`), puis
  `autogen.sh && configure && make -j8 && make check` en conteneur.
  **Compte de tests : 65 → 66.** Raisonnement, pas comptage naïf : `check_PROGRAMS` passe à
  **63 lignes**, dont **une porte deux entrées** (`StaticLogShutdown_test
  StaticLogShutdown_helper`) → **64 programmes**, moins `StaticLogShutdown_helper` qui **n'est pas
  un test** → **63 binaires** ; `TESTS` porte ces 63 binaires **plus 3 scripts shell** (la ligne
  `check-config-options.sh check-config-docs.sh` en porte deux, plus `run-python-tests.sh`) →
  **66**. T3.17f n'ajoute qu'**un** binaire, `core/JsonApiEventLog_test`.
  **LA tâche documentaire de ce merge — `T3.17.md`, l'argument le plus solide de la série pour
  avoir retiré l'ancien critère de diagnostic.** Écrit dans `T3.17.md`, **nouvelle sous-section
  `### 🔴 L'argument décisif, découvert par T3.17f : le critère ne se taisait pas, il MENTAIT`**,
  placée **dans** la section `## 🛑 Correction du critère de diagnostic de la série`, juste après
  le corollaire T3.17c et **avant** `### ⚠️ Ce que cette correction ne remet PAS en cause` (le
  chapeau de la section, qui disait « à lire […] avant T3.17f », est mis à jour en conséquence).
  Le fait mesuré : il existe **un seul** avertissement `-Wdeprecated` de capture implicite sur
  cette famille, à `JsonApiHandlerWS.cpp:495` (la lambda `[=](Json &j)` de `processEventLog`), et
  **aucun** à `JsonApiHandlerHttp.cpp:877` — dont la capture `[this]` **explicite** est muette,
  pour une raison de **style d'écriture**, pas de sûreté. **L'ancien critère n'était donc pas
  seulement aveugle au vrai danger : il désignait le mauvais fichier — le handler et non
  `JsonApi.cpp`, le seul point qui domine les deux transports — et un seul des deux transports.
  Le suivre aurait produit exactement le demi-correctif que T3.17e a refusé de faire en
  connaissance de cause, six tickets plus tôt, et avec l'air d'avoir raison** puisque
  l'avertissement aurait disparu.
  **Documentation** : BOARD (ligne `T3.17f` **📋 → ✅** ; l'épique `T3.17` était **déjà** ✅,
  T3.17f est un ticket distinct avec sa propre ligne), la sous-section de `T3.17.md` ci-dessus,
  une section `## T3.17f — suites` en FINDINGS, une entrée RELEASE_NOTES, ce journal.
  **FINDINGS** — quatre acquis, les trois premiers **réutilisables hors du ticket** :
  (1) **La couverture transitive est MESURÉE, pas raisonnée — première fois de la série.** La
  trace ASan du chemin **HTTP** montre que le bloc libéré **est l'objet `JsonApiHandlerHttp`
  lui-même**, libéré par **son propre destructeur** (`JsonApiHandlerHttp.cpp:53`), et que la
  lecture fautive tombe **à +48 dans ce même bloc** — là où vit `apiAlive`, membre du sous-objet de
  base. **Une garde écrite uniquement dans `JsonApi.cpp` neutralise donc un UAF dont le site de
  libération est dans un fichier jamais ouvert.** Ce n'est plus une inférence d'ownership, c'est
  une coïncidence d'adresses observée — elle ferme la réserve que T3.17e avait ouverte (« l'arête
  de propriété n'est prouvée par aucune mesure »).
  (2) **La sentinelle d'ordonnancement, validée sur DEUX étages par la revue.** Sans elle, les cas
  de durée de vie passeraient **même si le callback n'était jamais tiré**. Elle tient parce que
  (a) le worker `HistLogger` est un `ThreadedQueue` à **consommateur unique, FIFO strict**, **et**
  (b) chaque action alloue **son propre** `AsyncHandle` (`HistLogger.cpp:133`, `:150`),
  `uvw::Loop::resource()` faisant un **`QUEUE_INSERT_TAIL`** à la création, si bien que
  `uv__async_io` dépêche **dans l'ordre d'insertion** — le handle en vol passe **avant** celui de
  la sentinelle **même si les deux `send()` tombent dans le même tour de boucle**. C'est le second
  étage qui est le point non évident : le FIFO du worker seul ne dit rien de l'ordre de livraison.
  (3) **Septième récidive du « fixture pauvre », comblée.** **Aucune requête ne demandait jamais
  une page valide ≠ 0** (`page="9"` partait en erreur « page is out of range » avant la
  construction du document), si bien que figer l'écho `page`, figer l'écho `per_page` **et**
  annuler l'offset `int start = ac->page * ac->per_page` (`HistLogger.cpp:269`) laissaient
  **19/19 vert** — les trois mutations à la fois. Comblé par **un seul** cas,
  `TheSecondPageEchoesItsOwnCoordinatesAndCarriesTheSecondSlice`, qui vérifie la tranche **par
  provenance**.
  (4) ⚠️ **Le piège des TROIS jetons — ce n'est PAS un usage hors fichier**, formulation corrigée
  en revue après vérification : `apiAlive` n'apparaît, dans **tout `src/`**, que dans **deux
  fichiers** (`JsonApi.h` déclaration, `JsonApi.cpp` **24 méthodes gardées** sur 26, les deux
  restantes étant synchrones), **zéro usage de production ailleurs**. Le piège pour le prochain
  lecteur est la **confusion de trois jetons de rôle identique à trois niveaux d'objet** : les 5
  callbacks gardés de `JsonApiHandlerHttp.cpp` (`:462`, `:727`, `:936`, `:1057`, `:1075`) utilisent
  le jumeau **`handlerAlive`** (`JsonApiHandlerHttp.h:64`), et `RemoteUIWebSocketHandler` en a un
  **troisième**. Règle : un callback prend le jeton de **l'objet dont il touchera les membres**.
  **Non exercé délibérément** : le **SIGFPE `per_page`** (`per_page: "0"` → `rowcount /
  ac->per_page`, `HistLogger.cpp:268`, division par zéro à distance sur les deux transports) —
  **il tuerait le binaire de test**, donc tous les cas envoient un `per_page` numérique non nul.
  C'est un défaut de **validation d'entrée**, même famille que **T3.19** ; consigné en FINDINGS
  pour rattachement, **à ne pas laisser se perdre**.
  Merge **ff-only** (historique linéaire, pas de commit de merge), worktree
  `/tmp/claude-1000/calaos-wave20/t3.17f` nettoyé par son **chemin exact** + `git worktree prune`,
  branche `refactor/t3.17f` supprimée, **rien n'a été poussé**. Voisin
  `/tmp/claude-1000/calaos-wave21/e4.0g` **vérifié intact** (worktree et conteneur de build),
  `docs/refactoring/` toujours à **97 fichiers**.
- **E4.0g ✅ mergé (2026-08-17, `aacf2682`) — DERNIER MAILLON DE LA SÉRIE E4.0 : L'ÉPIQUE PASSE ✅
  ET E4.1 EST DÉBLOQUÉE.** Deux commits : `476fcaab` (le ticket revu) et `aacf2682` (la sixième
  rustine, découverte au rebase — voir plus bas). **Zéro ligne de `src/`, aucun golden touché**
  (vérifié **sur les commits**, pas seulement sur l'arbre) : ce ticket ne corrige **aucun défaut de
  production**, il change la **sémantique du cycle de vie** du harnais sous les binaires qui le
  partagent. Suite : **66/66**, **inchangée** — E4.0g n'ajoute **aucun** binaire et ne touche pas
  `tests/Makefile.am` (`make distclean` + reconfigure complet après rebase, cf. piège
  `_DEPENDENCIES` variante faux ROUGE).
  **Le correctif** : `pumpEventLoop()` déplacé **après** `CoreFixture::TearDown()`. C'est ce
  teardown parent qui déclenche `clearCoreState()` → `~Room()` → `RemoveIO()` → un `EventIODeleted`
  par IO (`Room.cpp:77`, **8** pour la maison de référence) ; ces events étaient donc **produits
  après** l'unique drain, fuyaient vers le cas suivant et y étaient livrés à sa première session.
  **6 rustines retirées** (5 du ticket + 1 trouvée au merge) et le contrat de **file vide** posé
  dans l'en-tête du harnais.
  **La validation qui compte n'est PAS `make check`** — un défaut de fuite d'un cas vers le suivant
  ne se voit qu'en **ordre aléatoire**. Re-vérifié à ce merge : `--gtest_shuffle` sur les **14**
  binaires `JsonApi*` × graines **7, 42, 101, 20260815** (celles qui exposaient le défaut) = **56
  runs, 0 échec, 0 code de sortie non nul**, aucun `uv__finish_close` / `bad_alloc` / segfault.
  ⚠️ **Surveiller le CODE DE SORTIE, pas seulement les cas rouges** : sur certaines graines le
  binaire **mourait** au lieu d'échouer proprement (`JsonApiSession_test` 6/8 rouge mais **8/8**
  non nul).
  ⚠️ **UNE SIXIÈME RUSTINE EST APPARUE PENDANT LA REVUE — mode de défaillance à retenir.**
  `JsonApiEventLog_test` **n'existait pas** à la base de rebase : **T3.17f l'a créé pendant la revue
  d'E4.0g**, contre l'**ancienne** sémantique, avec un `pumpEventLoop()` commenté « workaround
  **mandatory** » **citant E4.0g** comme défaut connu. Après merge ce commentaire était **faux** et
  le pompage **mort**. Mesuré **vert 8/8 graines sans lui** → retiré (`aacf2682`). **Les
  consommateurs du harnais sont donc passés de 11 à 12 pendant la revue**, et le douzième est
  exactement celui que l'implémenteur ne pouvait pas avoir mesuré. **Règle : un ticket qui change
  une sémantique partagée doit revérifier la liste de ses consommateurs AU MOMENT DU MERGE, pas au
  moment de la mesure.** (Le chiffre **13** qui a circulé était faux ; `JsonApiHardening_test` et
  `JsonApiAudioState_test` ne compilent pas le harnais.)
  **Documentation** : BOARD (`E4.0g` **📋 → ✅** ; **l'épique `E4.0` 📋 → ✅**, a→g tous mergés,
  vérifié ligne à ligne sur le board ; `E4.1` — dépendance dure **`E4.0` marquée satisfaite**,
  **statut inchangé 📋**, personne ne l'a commencée), une section `## E4.0g — clôture du harnais`
  en FINDINGS, ce journal + le bilan de série ci-dessous. **Pas d'entrée RELEASE_NOTES** — zéro
  ligne de `src/`, aucun comportement utilisateur changé.
  Merge **ff-only**, worktree `/tmp/claude-1000/calaos-wave21/e4.0g` nettoyé par son **chemin
  exact** + `git worktree prune`, branche `refactor/e4.0g` supprimée, **rien n'a été poussé**.
  Voisin `/tmp/claude-1000/calaos-wave22/t3.19` **vérifié intact**, `docs/refactoring/` toujours à
  **97 fichiers**.
- **T3.19 ✅ mergé** (2026-08-17, `3afc89ad`) — **DEUX PLANTAGES À DISTANCE PRÉEXISTANTS,
  atteignables par un client AUTHENTIFIÉ sur un appel d'API LÉGITIME**, pliés dans un seul ticket
  parce que c'est le même fichier et le même défaut (une entrée utilisée sans être validée) :
  **SIGSEGV** — `AudioPlayer::database` est un pointeur nu laissé `NULL` par le constructeur de
  base et dont `Squeezebox.cpp:81` est la **seule** assignation de l'arbre, déréférencé aux
  **16** sites `audio_db` ; **SIGFPE** — `per_page` divisé par zéro dans le **thread sqlite**
  d'`HistLogger` (`HistLogger.cpp:268`), où le `try` de `:257` n'attrape rien (un signal n'est
  pas une exception). Deux commits : `7fbba895` (caractérisation, **zéro ligne de `src/`**) et
  `3afc89ad` (les deux gardes). Suite : **66 → 67** (`core/JsonApiInputGuards_test`, 54 cas),
  **67/67 vert** après `make distclean` + reconfigure complet (piège `_DEPENDENCIES`, variante
  faux ROUGE). Recompté à la main dans `tests/Makefile.am` : 67 entrées `TESTS` = 64 binaires
  gtest (65 `check_PROGRAMS` moins `StaticLogShutdown_helper`, qui n'est pas un test) + 3 scripts
  shell (`check-config-options.sh`, `check-config-docs.sh`, `run-python-tests.sh`).
  **AUCUN HANDLER ÉDITÉ, et c'est l'arbitrage du ticket.** Filtrer sur `canDatabase()` dans les
  transports a été **mesuré et refusé** : ce n'est **pas la précondition** — constante par classe,
  **un seul lecteur non-commentaire dans tout l'arbre** (`JsonApi.cpp:406`, qui ne fait que la
  **publier**) — alors que ce qui est déréférencé est **le pointeur**. Les deux coins divergents
  sont épinglés : drapeau **vrai** / pointeur **nul** → **refusé** ; drapeau **faux** / pointeur
  **valide** → **servi**. Un filtre sur la capacité aurait laissé passer le premier **et** volé sa
  réponse au second. Les deux gardes sont posées **immédiatement avant leur consommateur**, jamais
  en tête : après la porte `from`/`count` pour `audio_db`, après la branche `uuid` pour `per_page`
  — placements **épinglés par des cas écrits dans le PREMIER commit**, avant que les gardes
  n'existent.
  **Revue indépendante : verdict MERGE**, réserves **documentaires uniquement**. Le relecteur a
  reproduit **les deux** crashs sur des sites **différents** de ceux de l'implémenteur (exit **139**
  sur `audioDbGetTrackInfos`, exit **136** en affaiblissant la garde **d'un caractère**), monté
  deux contre-mutations **par échange de valeurs** non testées par l'implémenteur (**4** et **2**
  échecs), et conclu qu'il n'y a **pas de huitième récidive du fixture pauvre** — la première fois
  de la série.
  **Deux corrections de commentaire appliquées au merge, COMMENTAIRES SEULEMENT** (diff vérifié
  mécaniquement : aucune ligne non-commentaire, build relancé vert après) : (1) le commentaire de
  la fuite du `LIMIT` négatif disait « *on an empty table* », or **la table vide est précisément le
  seul cas inoffensif** — réaligné sur « *a table small enough for the page check to pass* », la
  formulation déjà juste du message de commit et du test ; (2) **l'argument de sûreté de la garde
  `page` ABSENTE est désormais écrit** — `HistLogger.cpp:270-277` refuse `page < 0` et
  `page > total_page` **avant** que `start = page * per_page` ne serve, donc `start` ne peut pas
  déborder ; une lacune documentaire sur une garde absente est exactement ce qui pousse un lecteur
  ultérieur à l'ajouter « au cas où » ou à retirer celle qui existe en aval.
  **Documentation** : BOARD (`T3.19` **📋 → ✅**, libellé étendu aux deux crashs), **DEUX** entrées
  `RELEASE_NOTES.md` — la correction des deux plantages **et, déclaré à part, un changement de
  comportement client** : un `per_page` **négatif** partait au moteur, et **un `LIMIT` négatif
  signifie « pas de limite » en SQLite** (vérifié 3.51.2), donc la requête renvoyait **toutes** les
  lignes sous un document annonçant `per_page:-5` ; l'arithmétique entière de
  `HistLogger.cpp:268-273` laissait passer **tout `rowcount` ≤ 9 sauf 5** (re-vérifié au merge,
  au-delà de la plage 0-6 de la revue), et ce seul refus **nommait le mauvais paramètre**. Plus une
  section `## T3.19 — suites` en FINDINGS (les trois acquis + le tableau `rowcount`) et ce journal.
  Merge **ff-only** (branche assise directement sur `dd292e98`, **aucun conflit**,
  `tests/Makefile.am` s'est appliqué seul), **4 goldens `t319_*` en `A`, zéro `M`, zéro `D`**,
  worktree `/tmp/claude-1000/calaos-wave22/t3.19` nettoyé par son **chemin exact** +
  `git worktree prune`, branche `refactor/t3.19` supprimée, **rien n'a été poussé**.
  `docs/refactoring/` toujours à **97 fichiers**.
- **T3.18 ✅ mergé** (2026-08-17, `3c87a837`) — **dernier ticket de la file**, et le plus large de
  la série : **15 fichiers de production**. C'est une **décision utilisateur**, maintenue et
  **durcie** contre l'avis du cadrage initial (« *si un IO disparaît c'est un problème, on ne peut
  pas le résoudre sans intervention manuelle* ») : un scénario dont une étape a perdu son IO est
  **entièrement désactivé**, avec un paramètre **persisté et collant** (`disabled_missing_io`) et
  une **réactivation manuelle explicite** (`autoscenario reenable`, les deux transports). Trois
  différences assumées avec la désactivation des règles : l'état **survit au redémarrage**, remettre
  l'IO **ne suffit pas**, et une réactivation prématurée est **refusée en nommant les ids
  manquants**. Trois commits : `b1f5dcb2` (caractérisation, **zéro ligne de `src/`** — vérifié
  mécaniquement : 2 fichiers, tous deux sous `tests/`), `f005597f` (la décision) et `3c87a837`
  (les suites de revue R1/R2/R4).
  **Revue indépendante : verdict MERGE**, réserves « **mineures, aucune ne touche la décision
  utilisateur** ». Le relecteur a prouvé **par mutation** que la collance et le refus tiennent :
  drapeau rendu non collant → **5 cas rouges** ; constructeur ne relisant plus le param → **4 cas
  rouges**.
  **⭐ Le troisième commit a transformé une réserve documentaire en correction de fond.** R1
  signalait un `isDangling()` « non couvert » ; ce n'était **pas un trou de couverture, c'était un
  bug**, et il **vidait la décision utilisateur de sa substance**. `Scenario::toJson()` émet
  **`category` avant `broken`**, `getCategory()` appelle `purgeDeadSteps()`, et cette purge
  **efface l'entrée pendante** (compaction d'E4.2f) : le scan brut d'`isDangling()` répondait donc
  **faux** pour un scénario dont la règle d'étape venait d'être détruite — **sérialiser le scénario
  effaçait la preuve une clé plus tôt**. La porte 1 était **neutralisable par une simple lecture**,
  et *tous* les accesseurs le faisaient (`getCategory()`, `stepRule()`, `getRuleSteps()`).
  Correctif en **deux moitiés toutes deux nécessaires** : `purgeDeadSteps()` **mémorise** ce qu'elle
  retire (`stepRuleDestroyed`, `AutoScenario.cpp:62`) et `isBroken()` lit la mémoire **avant** le
  scan (`:185`, scan en `:189`). L'état reste **entièrement dérivé, jamais persisté**
  (`checkScenarioRules()` le remet à zéro, `:564`) et **aucun client ne peut l'écrire** — membre
  privé, ni accesseur en écriture, ni sérialisation. **Racine mesurée** : c'était la **troisième
  paire** de la trappe d'E4.0c — `broken` vrai avec `missing_ios` **vide** (une règle détruite ne
  laisse aucun id à nommer), paire qui s'accordait dans **tous** les autres cas, d'où les 5 binaires
  verts sur la mutation du relecteur.
  Suite : **67 → 68** (`core/ScenarioDisabledMissingIo_test`, 14 cas), **68/68 vert** après
  `make distclean` + reconfigure complet (piège `_DEPENDENCIES`, variante **faux ROUGE**).
  Décompte **recoupé à la main** contre l'attente initiale de 69, qui était fausse : la branche
  était déjà assise sur `86c3d131` (T3.19 **sous** elle), donc master = **67** et T3.18 ajoute
  **1 binaire**. Vérifié dans `tests/Makefile.am` : **66 lignes `TESTS +=` = 67 entrées** (la ligne
  `check-config-options.sh check-config-docs.sh` en porte **deux**), et le diff de T3.18 contient
  **exactement une** ligne `TESTS +=` ajoutée, **zéro** retirée, en **pur append**.
  **R3 et R5 laissés intacts sur instruction.** R3 est à **ouvrir en ticket de suivi** et pose une
  **question ouverte à l'utilisateur** : après un aller-retour `autoscenario modify`,
  `deleteRules()` **détruit la référence morte**, donc `isBroken()` devient faux et
  **`tryReenable()` réussit** sur un scénario ayant silencieusement perdu une action d'étape — le
  drapeau collant est levé **légitimement** sur un scénario **amputé**. Préexistant, et
  `missing_ios` prévient désormais **avant** le round-trip, mais le refus ne peut **rien voir
  après** ; `modify` doit-il **refuser** de reconstruire un scénario dont une étape référence un IO
  absent ? R5 (drapeau posé à la main sur un scénario **sain** : le booléen en mémoire n'est pas
  affecté, le scénario tourne jusqu'au reboot où il se retrouve désactivé) reste classé **DoS par
  client authentifié, déjà assumé**.
  **Deux pièges de test consignés.** (1) **Le canari devenu vraie commande** : E4.0c utilisait la
  chaîne littérale `"reenable"` comme sonde de « type autoscenario inconnu » ; livrer la commande
  **transformait le canari en commande valide**, et le test de silence aurait continué de passer
  **pour une raison entièrement différente**, sans jamais rougir. Sonde changée en
  `e40c_not_a_command`, plus une assertion positive — *une sonde de test doit être une valeur qui ne
  peut pas devenir valide*. (2) **Onze** tests de contrat modifiés, pas les six annoncés par le
  ticket, dont la liste était fausse **dans les deux sens** ; dont
  `SavingRulesAfterAnIoDeletionIsClean` **dont l'assertion s'inverse** — l'id mort doit désormais
  **survivre** dans `rules.xml`, conséquence directe du « le scénario reste intact ».
  **Documentation** : BOARD (`T3.18` **📋 → ✅**, libellé étendu au drapeau persistant et à
  `autoscenario reenable`), **une** entrée `RELEASE_NOTES.md` placée **en deuxième position** des
  « comportements qui changent », juste après son jumeau sur les règles (même famille, mais **plus
  strict** : c'est le seul changement de la série exigeant une **intervention manuelle**), signalant
  la **rupture de contrat d'API** — le payload de scénario gagne **trois clés** (`broken`,
  `disabled_missing_io`, `missing_ios`) ; une section `## T3.18 — suites` en FINDINGS (les six
  acquis, dont R3 en question ouverte) et ce journal.
  Merge **ff-only** après `git rebase master` (master avait avancé de `d472611c`, **docs seulement**,
  **aucun conflit**), **7 goldens `e40c_*` en `M`, zéro `A`, zéro `D`** — 145 goldens au total,
  inchangé. Worktree `/tmp/claude-1000/calaos-wave23/t3.18` nettoyé par son **chemin exact** +
  `git worktree prune`, branche `refactor/t3.18` supprimée, **rien n'a été poussé**.
  `docs/refactoring/` à **98 fichiers** (97 + `E4.5.md`, apporté par `d472611c`).
- **E4.5a + E4.5b ✅ mergés** (2026-08-24, `eb630d73` puis `15996ed2`) — **premiers sous-tickets
  de l'épique documentaire E4.5, qui passe à 2/6.** Périmètre **strictement docs-only**, vérifié
  mécaniquement **commit par commit** (`git show --name-only` sur les 5 commits de la plage
  `4e5fe9fc..master`) : **zéro ligne de `src/`, zéro test, zéro golden**, donc **aucun build,
  aucun `make check`** — conformément au brief. E4.5a : `docs/00_overview.md`,
  `docs/01_core_data_model.md`, `docs/11_config_persistence.md` (+755/−160) — modèle de propriété
  E4.2a→f, migration **pugixml** (E4.4/E4.4cd), robustesse de config T2.4 (préservation du fichier
  corrompu dans `<config>/backups/corrupt/`, marche des backups du plus récent au plus ancien avec
  parse préalable, notification agrégée mail+push différée de `CONFIG_ALERT_DELAY_SEC = 30.0`).
  E4.5b : `docs/03_rules_engine.md`, `docs/04_scenarios.md` (+1078/−149) — règle désactivée
  (E4.2e) avec ses **six portes de refus**, scénario désactivé T3.18 (**deux portes**, drapeau
  **persisté et collant**, `autoscenario reenable` sur les deux transports, trois clés neuves de
  payload), plages nocturnes wrappantes (T3.13). Les deux branches, dont les worktrees `/tmp`
  avaient disparu (`prunable`, nettoyés par `git worktree prune`), ont été **rebasées sur master**
  (`--autostash`, master avait avancé de 3 commits docs) puis mergées en **ff-only**, e4.5a
  d'abord, **aucun conflit** (fichiers disjoints, `tests/Makefile.am` jamais touché).
  **Revue indépendante : verdict MERGE pour les deux**, sur **~50 affirmations échantillonnées et
  revérifiées au source** (goldens ouverts et comparés octet par octet, références
  `Fichier.cpp:ligne` ouvertes à la ligne citée). **Trois défauts trouvés, tous corrigés en
  commits de suite avant merge.** ⭐ Le plus important est de la même famille que les 4 citations
  infidèles d'E4.0f : la citation de la **décision utilisateur T3.18** en `04_scenarios.md` était
  annoncée `(capturé, DECISIONS.md:37-40, intégral)` mais **tronquée** — la ligne 40 (« *et un IO
  dans Calaos ne se supprime pas comme ça.* »), c'est-à-dire **la justification même** de la
  décision, manquait, et la virgule finale avait été changée en point. Citation désormais
  **byte-identique** (`diff` sur les deux extraits). Les deux autres sont dans `01_core_data_model.md` :
  le diagramme de cycle de vie d'un IO attribuait `addIOHash()` à **`~IOBase(Params&)`**, le
  *destructeur* (c'est le constructeur ; le destructeur appelle `delIOHash()`), et le contrat de
  `detachIOFromRules(modify = true)` disait « les règles ne sont **pas** touchées du tout », ce
  qui laissait croire à un no-op complet alors que seul `ListeRule::RemoveRule()` est sauté —
  `ListeRule::Remove(io)` (retrait de la liste de scrutation) s'exécute **inconditionnellement**
  (`ListeRoom.cpp:378-402`). **Point de vigilance principal du brief, contrôlé et propre** :
  `steps_count` est bien documenté comme **longueur du tableau `steps` moins 1** — l'invariant
  `len(steps) == steps_count + 1` est énoncé, encadré d'un avertissement « un client qui
  dimensionne son tableau sur `steps_count` tronque », et **revérifié mécaniquement sur les 8
  payloads de scénario capturés** (7 fichiers `e40c_*autoscenario*`, dont `…_list.json` qui en
  porte deux) : 8/8 conformes, y compris le cas dégénéré `steps_count: "0"` → 1 élément.
  **Aucune anticipation d'E4.1** : les mentions de jansson décrivent l'état actuel (`00_overview`
  documente explicitement la cohabitation jansson/nlohmann et renvoie E4.1 à plus tard).
  Documentation : BOARD (**`E4.5a` et `E4.5b` ajoutés en ✅**, `E4.5` maintenue **📋** avec le
  libellé « 2/6 livrés (a, b) », et **`E4.2` 🔨 → ✅** — a→f tous livrés, g abandonné après
  re-scope, 0/21 sites dangereux), FINDINGS (section de tête **vidée** : les deux items
  « priorité » étaient périmés — `createIO()` sans garde nullptr **traité par T2.18**
  (`62739380`) et `getRemoteUIByToken` non constant-time **traité par T2.15** (`01089187`) ;
  **revérifiés au source** avant d'être déplacés dans une section « Résolus ») et ce journal.
  **Rien n'a été poussé.** **Reste de l'épique : E4.5c, E4.5d, E4.5e, E4.5f.**
- **🚀 MASTER POUSSÉ (2026-08-24, `9d8d37b5..b6b5a2b6`, 249 commits) — première publication depuis
  le début du refactoring.** Trois validations indépendantes ont précédé le push, dans cet ordre :
  **(1) Banc réel — validation UTILISATEUR sur son installation de production**, depuis une build
  locale (le paquet publié n'existait pas encore). ⚠️ **Le périmètre exact de ce qui a été exercé
  n'est PAS documenté** : la question a été posée (scénario amputé + `autoscenario reenable`,
  premier enregistrement sous pugixml, TLS insecure par défaut, drivers présents chez l'utilisateur)
  et **la réponse n'est pas encore arrivée**. **Ne pas cocher les sous-systèmes du §5c/5d comme
  validés** tant que la réponse n'est pas là — une case cochée à tort coûte plus cher qu'une case
  vide.
  **(2) Campagne de validation automatisée (subagent, SHA épinglé `4e5fe9fc`) — TOUT VERT, aucun
  écart aux chiffres de référence** : `make check` **68/68** exit 0 ; **ASan complet**
  (`distclean` + `--enable-asan` + `detect_leaks=0`) **68/68**, **zéro rapport AddressSanitizer** ;
  **shuffle** `--gtest_shuffle` sur les **15** binaires `JsonApi*` × graines **7, 42, 101,
  20260815** = **60 runs**, 0 échec, **tous rc=0**, aucun `uv__finish_close` / `bad_alloc` /
  segfault — puis **les 60 mêmes runs REJOUÉS SOUS ASan**, également verts (mesure bonus, non
  demandée) ; **configs réelles** avec un harnais linkant les **146** objets de `calaos_server`
  hors points d'entrée — **raoulh** 13 pièces / 213 IOs / 125 règles / 177 conditions / 318 actions
  et **solanora** 13 / 129 / 82 / 101 / 133, **identiques au caractère près** à la référence du
  2026-08-16, save+reload **1:1**, 0 type inconnu, 0 règle amputée, **684 destructions** (426 + 258)
  sans double-free ni UAF, en build par défaut **et** sous ASan.
  **Ce que cette campagne prouve et qui n'allait pas de soi** : les cinq tickets qui ont touché le
  cœur depuis la dernière référence (T3.17, T3.17f, T3.18, T3.19, E4.0g) **n'amputent ni ne
  désactivent quoi que ce soit** sur deux vraies maisons — **0 scénario `disabled_missing_io`** à la
  charge comme au rechargement. Les `[WRN] Rule ... is DISABLED` n'apparaissent que pendant la phase
  de suppression volontaire d'IOs, c'est-à-dire le comportement T3.18 attendu.
  **(3) Revue** : les merges E4.5a/E4.5b ci-dessus.
  ⚠️ **Le push est une PUBLICATION, pas une validation CI** (cf. DECISIONS 2026-08-16) : bump de
  version, tag git, `ghcr.io/calaos/calaos_base:dev` + tag versionné, dispatch `build_deb` vers
  `calaos/pkgdebs`. GitHub a signalé **13 vulnérabilités Dependabot** sur la branche par défaut
  (9 high, 4 moderate) au moment du push — **chiffre non recoupé** : T3.11 avait ramené `data/debug`
  de 11 à 3 advisories (toutes enracinées dans `immutable`, devDependency jamais shippée) et posé un
  `.github/dependabot.yml` limité à `/data/debug` en `dependency-type: production`. Soit le compte
  est antérieur au recalcul, soit il porte sur autre chose — **à vérifier, pas à croire**.
  **T3.20 n'est PAS dans ce push** (décision : ne pas diluer ce que le banc mesure en y glissant un
  changement de comportement du jour même) ; il est en implémentation sur `fix/t3.20`, worktree
  `/home/raoul/repos/calaos/.wave26/t3.20` — **sur disque réel**, les worktrees `/tmp` d'E4.5a/b
  ayant été perdus.
  **Trouvaille mineure à rattacher à un ticket, pas encore en FINDINGS** (FINDINGS est en cours
  d'édition par le subagent T3.20, l'entrée sera posée après son merge) : chaque binaire `core/*`
  se termine sur `[ERR] (CalaosConfig.cpp:552) Could not open <tmp>/cache/iostates.cache.tmp for
  write !` — le singleton `Config` **flushe son cache d'état APRÈS que `CoreFixture::TearDown()` a
  supprimé le répertoire temporaire**. Bénin (sortie de processus, aucun impact ASan), mais c'est
  **un écrivain qui survit au teardown**, exactement la classe de défaut qu'E4.0g a passé un ticket
  entier à refermer ailleurs. À connaître avant de durcir le harnais.

### 🏁 Bilan de la série E4.0 (close) — ce que E4.1 doit lire AVANT de démarrer

**Ce qui est désormais caractérisé** : l'API JSON est tenue par un filet de cas et de goldens sur
les deux transports (WS et HTTP) — modèle et état, plages horaires et autoscénarios, events temps
réel (23 `type_str` + numérotation de l'enum + enveloppe WS + `poll_listen`), session, enveloppes
et chemins d'erreur (7 refus `scopeDenied`, silences, 400/404), payload audio et base musicale. Le
harnais lui-même est **stable et documenté**, et son contrat de cycle de vie est **prouvé par
`--gtest_shuffle`**, pas supposé.

**E4.1 (jansson → `nlohmann::json`) doit impérativement lire, dans l'ordre :**

1. ⚠️ **La ligne UTF-8 corrigée du tableau des pièges de bascule — `E4.0.md:355`, LES DEUX
   COLONNES** (le numéro a glissé, elle était citée `:308`). C'est **le** piège de la migration, et
   les deux colonnes étaient fausses avant qu'E4.0e ne les corrige. Résumé : jansson refuse les
   octets invalides **à la construction** et **aucun code de retour n'est testé** → la paire est
   **silencieusement supprimée**, `json_dumps()` **réussit**, on répond **200 avec un payload
   tronqué**. nlohmann fait **l'inverse et pire** : il **accepte** les octets dans l'arbre et lève
   **`type_error.316` depuis `dump()`** ; les deux `sendJson` dument **à nu**, sans le moindre
   `try`/`catch` → **`std::terminate` sur une connexion vivante**. Le canal d'injection est
   **trivial et réel** (`?param=%ff%80x`, percent-décodé **avant** découpage, le nom fourni par le
   client finissant en **clé**). **E4.1 doit trancher explicitement** : `error_handler_t::replace`
   / `ignore` **ou** `try`/`catch` sur tout dump influencé par le client — **et**, séparément,
   choisir entre **« drop comme aujourd'hui »** et **U+FFFD**. Le cas
   `Utf8Trap_NlohmannDumpThrowsWhereJanssonDrops` met les deux bibliothèques côte à côte et épingle
   le code **316** exactement.
2. **Le contrat d'oracle sémantique** (posé en E4.0a) : on compare des **documents**, pas des
   chaînes. Corollaire mesuré en E4.0g : **une assertion d'ABSENCE n'a de valeur que si le canal a
   été flushé — *non livré n'est pas non levé*.** Un `EXPECT_EQ(0, ...)` sans pompage en amont est
   un **oracle mort**, et la série en a produit un vrai (`LoadingAHouseFromConfigRaisesNoEventAtAll`,
   vert même si le chargement s'était mis à lever des events).
3. **La règle de la contre-mutation PAR ÉCHANGE** : pour prouver qu'un cas mord, on **échange** la
   valeur produite (page ↔ page suivante, un `type_str` contre un autre), on ne se contente pas de
   la neutraliser. **Sept récidives du « fixture pauvre »** dans la série — un jeu de données trop
   pauvre pour distinguer deux champs interchangeables — **toutes trouvées par les relecteurs et
   jamais par les implémenteurs**. C'est le défaut le plus régulier de la série : **le prévoir dans
   le brief d'E4.1**, pas dans sa revue.
4. **Ne pas recopier un pompage qu'on n'a pas mesuré.** Sur 6 rustines accumulées contre la fuite
   du harnais, **3 n'ont jamais rien absorbé** : elles ont été copiées du voisin **en même temps
   qu'une explication fausse** (« le chargement lève un `EventIOAdded` par IO ») qui a circulé
   **six sous-tickets durant** avant qu'E4.0d ne la réfute. Un pompage défensif non mesuré est une
   dette qui se propage par mimétisme.

- **⭐ E4.6 OUVERT 📋 (2026-08-24) — AutoScenario : REFONTE COMPLÈTE, T3.20 PARKÉ ⛔.**
  **Décision utilisateur** : la fonctionnalité AutoScenario est refondue **entièrement, API ET
  modèle interne**. Rupture totale assumée, **aucune compatibilité ascendante, aucun convertisseur**.
  Ses mots : les 4 auto-scénarios de sa config de production étaient **des essais avec une vieille
  UI** ; « *les scénarios fonctionnent encore car ce sont des Rules classiques* » ; on **change les
  ids et le fonctionnement**, les actuels **ne sont plus considérés comme des auto-scénarios** ;
  « *on peut donc casser entièrement cette API* ». Entrée datée en tête de `DECISIONS.md`, ticket
  complet **[`E4.6.md`](E4.6.md)** (7 sous-tickets `a`→`g`, 5 questions ouvertes).
  **Aucune ligne de `src/` écrite, aucun test, aucun golden touché.**
  - **Sans consommateur, littéralement** : 6 dépôts voisins → 0 occurrence ; `calaos_installer` →
    **0 appel** (il téléverse `io.xml`/`rules.xml` entiers par `api.php`) ; passe-plat MCP câblé
    mais **appelé par aucun tool**.
  - ⛔ **LE PIÈGE, à ne jamais perdre** : `ListeRoom::checkAutoScenario()` (`ListeRoom.cpp:320-330`)
    **détruit toute règle portant `auto_scenario` qu'aucun AutoScenario n'a adoptée**, et
    `SaveConfigRule()` (`:341`) le persiste. `Params` matche **exactement**
    (`src/lib/Params.cpp:31-37`), donc renommer le marqueur laisse le prédicat **vrai** →
    **les 18 règles de `configs/raoulh/rules.xml` sont détruites au premier démarrage, en silence.**
    La promesse faite à l'utilisateur n'est vraie **que si ce balayage est re-clé ou supprimé**.
    E4.6a l'épingle **avant** toute ligne de `src/`.
  - ⭐ **Trouvaille tardive (revue T3.20) qui a déplacé la conception** : le vrai vecteur
    d'amputation est **hors API** — `calaos_installer` jette les actions à id non résolu au
    chargement (`projectmanager.cpp:1126-1133`) puis **régénère et téléverse `io.xml`/`rules.xml`
    entiers**. ⇒ la définition du scénario est placée dans un **`scenarios.xml` propre au serveur**,
    hors de portée de l'installeur. C'est la **question ouverte n°4**, la plus structurante.
  - ⛔ **Séquencement dur** : E4.6 vient **après E4.1** (décision du même jour : plus aucun code neuf
    en jansson ; les fichiers rouverts portent **41 %** du jansson du dépôt). **Arbitrage soumis
    (Q5)** : `IO/Scenario.cpp` — exclu du périmètre d'E4.1 et migré directement par E4.6, ou migré
    deux fois ?
  - **T3.20 ⛔ parké** : R3/R5 absorbés par la refonte. **Exception extraite : T3.21** — le
    durcissement de `buildJsonDelParam` (`JsonApi.cpp:724`, court-circuite `IOBase::del_param()`)
    est **indépendant des scénarios**, une ligne, aucun appelant cassé. À livrer seul.
  - **➡️ PROCHAINE ACTION CONCRÈTE** : faire trancher à l'utilisateur les **5 questions ouvertes**
    de `E4.6.md` §10 — en priorité **Q4** (où vit la définition : `scenarios.xml` séparé ou `io.xml`)
    et **Q5** (séquencement d'`IO/Scenario.cpp` entre E4.1 et E4.6), car les deux commandent le
    périmètre d'E4.6b. Q1/Q2/Q3 peuvent être tranchées plus tard, avant E4.6d.
    Ensuite : lancer **T3.21** (indépendant, non bloqué par E4.1) pendant qu'E4.1 démarre.
  - ⚠️ **Recalage de sites obligatoire** : la revue de T3.20 cite `IO/Scenario.cpp:206` et `:229`
    pour les gardes `if (!sa.io) continue;`. Sur **master `770e322f`** le fichier fait **195 lignes**
    et les sites sont **`:158`** et **`:181`** — l'écart vient du worktree `.wave26/t3.20`. Tout
    sous-ticket reprenant un site de cette revue doit le **recaler sur master**.
- **T3.20 ouvert 📋 (2026-08-24), NON IMPLÉMENTÉ** — les réserves **R3** et **R5** de la revue de
  T3.18 (`FINDINGS.md`, `## T3.18 — suites`) sont **tranchées par l'utilisateur**, deux entrées
  datées en tête de `DECISIONS.md`. **R3** : `autoscenario modify` doit refuser un **payload** qui
  cite un IO absent — le refus porte sur ce qu'on **écrit**, pas sur l'état d'avant, sinon on
  enferme l'utilisateur (réparer reste possible, blanchir devient impossible) ; point d'insertion
  obligatoire `JsonApi.cpp:2033`, **avant** `deleteRules()` (`:2034`). **R5** :
  `disabled_missing_io` passe en **lecture seule côté API**, ignoré + loggué façon
  `set_param("id")`, avec le routage de `buildJsonDelParam` (`JsonApi.cpp:724`) par la méthode
  virtuelle — sans quoi la garde n'est jamais atteinte. Ticket complet : `T3.20.md`.
  **Aucune ligne de `src/` écrite, aucun test, aucun golden touché.**
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
- ASan : depuis E4.3cd, plus de `CXXFLAGS` bricolés — utiliser l'option de configure.
  ```
  ./autogen.sh && ./configure --enable-asan && make -j12 && \
    ASAN_OPTIONS=detect_leaks=0 make check
  ```
  `--enable-asan` ajoute `-fsanitize=address -fno-omit-frame-pointer -g -O1` à
  CFLAGS/CXXFLAGS/LDFLAGS ; désactivée par défaut (un `./configure` nu est inchangé).
  `detect_leaks=0` coupe LeakSanitizer (singletons process-lifetime jamais libérés = bruit) ;
  la détection use-after-free / overflow / use-after-scope reste active.
  L'échec historique stack-use-after-scope `exprtk.hpp:15688` a été corrigé par T2.8 : la suite
  doit être verte sous ASan, tout échec est une vraie trouvaille à rapporter.

## Validation sur configs réelles (acquis 2026-08-16)

Deux configs de production sont disponibles hors dépôt :
`/home/raoul/repos/calaos/configs/{raoulh,solanora}/{io.xml,rules.xml}` (90+85 Ko et 49+43 Ko).
**Confidentialité** : ce sont de vraies maisons (mots de passe caméra, tokens) — monter en
`:ro`, copier avant toute écriture, ne **jamais** reproduire une valeur de credential dans un
rapport (`<redacted>`), ne rien committer.

Résultats de référence (master 2026-08-16, à re-vérifier après un gros portage comme E4.4cd) :
raoulh 13 pièces / 213 IOs / 125 règles / 177 conditions / 318 actions ; solanora 13 / 129 / 82 /
101 / 133. **Chargement 1:1, aucun type inconnu, aucune règle amputée**, round-trip sans perte
sémantique. Sous ASan : charge/parcours/évaluation/save/reload/suppression de **chaque** IO
(684 destructions) et destruction de pièces avec règles vivantes → **zéro** double-free ou UAF.

⚠️ **Pièges opérationnels ASan** (l'option `--enable-asan` existe depuis E4.3cd) :
- Les artefacts dépassent 20 Go — un worktree sous `/tmp` (tmpfs 32 Go) a été détruit en plein
  `make check`. **Faire les runs ASan sur disque réel** (ex. `~/repos/calaos/.validate/…`).
- **Le build dépasse désormais 600 s** (51 binaires de test) : l'appel docker synchrone peut
  excéder le timeout de l'outil et basculer en arrière-plan. **Ne pas relancer** — attendre le
  conteneur et lire son code de sortie + le résumé `make check`. Relancer double le temps et
  peut faire courir deux builds sur le même arbre monté.
- **Objets ASan périmés** : après un build `--enable-asan`, un build par défaut dans le même
  arbre échoue au link (`undefined reference to __asan_report_load1`) en réutilisant des `.o`
  instrumentés. `git status` dit « clean » (les artefacts sont gitignorés) → piège parfait.
  **Faire `make distclean` en changeant de configuration**, ne pas se fier à git.
Pour instancier les vrais types matériels (Wago/OneWire/Mqtt/Reolink/RemoteUI), il faut linker
**tous** les objets de calaos_server sauf les points d'entrée : les tests `core/` standards ne
voient que les IOs internes.

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

⚠️ **Variante HUNKS ENTRELACÉS — rencontrée aux merges de T3.17d puis de T3.17b, préférez-lui
d'emblée la « régénération ».** Quand les deux blocs réutilisent le **même gabarit `LDADD`** (cas
de tous les tests `core/JsonApi*`), git **fusionne les corps identiques** et ne laisse en conflit
que les **lignes d'en-tête**, produisant **trois** hunks entrelacés au lieu d'un. Recousus
fragment par fragment ils donnent un `LDADD` **chimérique** qui compile et linke sans broncher —
donc le piège ne se voit pas au build.

**Résolution fiable, indépendante de la forme du conflit** : ne pas éditer les marqueurs du tout.
Reconstruire le fichier = **`git show master:tests/Makefile.am` en entier + append verbatim du
bloc `# TX.Y` de la branche** (extrait par `git show <sha>:tests/Makefile.am` à partir de sa ligne
`# TX.Y`). Puis **prouver que c'est un append pur** : `diff master_full.am tests/Makefile.am` doit
donner **+N/−0/~0** — zéro ligne retirée, zéro ligne modifiée. Enfin l'équilibre
`^if HAVE_GTEST` == `^endif`. Cette recette ne dépend ni du nombre de hunks ni de leur imbrication.

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

## Règle — ne pas commiter sur master pendant une fenêtre de merge

Constat du merge E4.2f+E4.0a (2026-08-16) : l'orchestrateur a commité 3 commits de docs
(`d10457b7`, `262293d8`, `337ea975`) **pendant** que l'agent de merge buildait. Celui-ci a dû
rebaser deux fois. Aucun conflit — les commits étaient documentaires et hors de ses périmètres —
et il a rebasé plutôt que forcer, ce qui est le bon réflexe. Mais la consigne « master ne doit
jamais être manipulé par deux opérations concurrentes » que l'orchestrateur donne lui-même à ses
agents n'était pas respectée **par l'orchestrateur**.

**Règle** : tant qu'un agent de merge est en vol, l'orchestrateur ne commite rien sur master.
Les décisions utilisateur et mises à jour de board se mettent en attente et partent en un seul
commit après le rapport de merge. Si une décision doit absolument être consignée immédiatement,
elle va dans un fichier qu'aucun agent de merge n'écrit (jamais `BOARD.md`, `FINDINGS.md`,
`RELEASE_NOTES.md` ni `ORCHESTRATION.md`), et on l'assume explicitement.

## ⚠️ Piège `_DEPENDENCIES` — la variante FAUX ROUGE (découverte en E4.0b, 2026-08-16)

Le piège documenté jusqu'ici produisait des **faux verts** : on efface le `.o` de production, le
binaire de test ne se relinke pas (`<name>_DEPENDENCIES` est écrasé), et l'ancien binaire est
réexécuté sur du code muté.

**La même mécanique produit aussi des faux ROUGES, et c'est bien plus perfide.** Vécu en E4.0b :

Juste après un rebase, `core/JsonApiPlaylist_test` (T3.17a) **segfaultait de façon reproductible**
(3 fois sur 3). Le rebase amenait les sources `src/` de T3.17a, mais le `JsonApi.o` de l'arbre
datait d'**avant** le rebase et `_DEPENDENCIES` empêchait le relink. Le binaire de T3.17a testait
donc sa propre garde **contre un objet qui ne la contenait pas**. Après `make distclean` + rebuild
complet : **11/11**. Aucun bug de production.

**Pourquoi c'est pire que le faux vert** : un crash après rebase a toutes les apparences d'une
régression qu'on vient d'introduire. L'agent a d'abord soupçonné sa propre modification du harnais
et l'a écartée par mesure (en restaurant le harnais de master à l'identique, le segfault
persistait) — c'est le bon réflexe, mais ça coûte du temps et ça peut mener à « corriger » un code
sain.

**Règle** : **après tout rebase, `make distclean` avant de conclure quoi que ce soit** sur un
binaire voisin. À répercuter dans les briefs de sous-tickets.

## ⚠️ Outillage — sorties tronquées : `grep` hooké et `docker ps --format` (E4.0f, 2026-08-17)

**`grep` hooké — fiable pour compter, pas pour citer.** Le hook qui réécrit `grep` **tronque les
lignes longues avec `…` et fusionne/réordonne les résultats entre fichiers**. Les **comptages**
(`grep -c`, nombre de correspondances) sont **exacts** — comparés à une lecture Python directe du
fichier, ils coïncident. Mais le **texte** rendu ne l'est pas : une ligne longue revient amputée,
et l'ordre d'apparition n'est pas celui du fichier. **Conséquence opérationnelle** : pour vérifier
du **texte exact** — un bloc à recopier verbatim, une ligne de `Makefile.am`, une chaîne à comparer
octet à octet, une citation de golden — **lire le fichier** (`Read`, ou une lecture Python par
numéros de ligne). Ne jamais construire une résolution de conflit ni une citation de doc à partir
d'une sortie de `grep`.

⚠️ **Même famille, même piège, conséquence plus coûteuse** : **`docker ps --format '{{.Mounts}}'`
tronque le chemin de mount**. Un conteneur **encore en build** peut donc sembler absent de la
liste — et c'est exactement le mécanisme qui pousse à **relancer un build déjà en cours**, ce qui
double la charge machine et, si on « nettoie » ensuite, tue des builds voisins.
**Utiliser `docker inspect` et filtrer sur `Source`** :

```sh
for c in $(docker ps -q); do
  echo "$c $(docker inspect -f '{{range .Mounts}}{{.Source}} {{end}}' $c)"
done
```

**Règle** : un build qui dépasse le timeout de l'outil n'est **jamais** relancé — on retrouve le
conteneur par `docker inspect`/`Source` et on attend (`docker wait`). Et on ne filtre **jamais**
par image ni par ancêtre pour arrêter un conteneur : les worktrees voisins partagent la même image.
