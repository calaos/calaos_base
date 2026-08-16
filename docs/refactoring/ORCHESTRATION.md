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
