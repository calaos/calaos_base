# Phase 4 — cartographie & décomposition (2026-08-15)

Établie par audit du tree **après** les phases 0→3 (les specs E4.x dataient d'avant et sont
partiellement périmées). Chaque sous-ticket est dimensionné comme ceux des waves 3→8 : un
agent, un build, un diff revuable, périmètre fichiers disjoint.

---

## E4.3 — Couverture de tests : **PRÉMISSE PÉRIMÉE, LARGEMENT ATTEINTE**

La spec parle de « 3 tests ». Réalité aujourd'hui : **40 binaires de test, 411 cas** + 3 TESTS
scripts (`check-config-options.sh`, `check-config-docs.sh`, `run-python-tests.sh` pilotant 7
modules pytest) + un vrai harnais d'intégration `tests/core/CalaosCoreFixture` (31 KB) avec
save/reload, et la CI lance `make check`. Tous les items prioritaires de la spec sont couverts
(règles, XML corrompu, lifecycle IO, WebSocketFrame, tcpsocket, ExternProc, Timer, base64,
Calendar, Params, Lua). SHA1 est sans objet (passé à OpenSSL).

→ **Fermer E4.3 comme atteint** et ne garder que 4 petits tickets :
| # | Sujet | Possède |
|---|---|---|
| E4.3a | `TimeRange`/`InPlageHoraire` : zéro test aujourd'hui | nouveau `tests/TimeRangeCalendar_test.cpp` + append Makefile.am |
| E4.3b | Round-trip XML save→reload (le fixture sauve mais n'assert pas l'égalité) | nouveau `tests/core/ConfigRoundTrip_test.cpp` |
| E4.3c | Option `--enable-asan` dans configure.ac (aucun wiring ASan aujourd'hui) | `configure.ac` |
| E4.3d | Job de couverture gcov/lcov en CI | `.github/workflows/ci.yml` |

---

## E4.1 — Bibliothèque JSON unique : direction valide, **volume doublé**

jansson : **36 fichiers src + 4 tests** (dont 10 includes directs, le reste hérité via
`JsonApi.h`, `Params.h`, `Jansson_Addition.h`, `IO/ExternProc.h`). nlohmann : **10 fichiers**
seulement, confinés à RemoteUI/OTA + `WebSocket.cpp` + `ConfigOptions`. La migration est donc
**unidirectionnelle** (jansson → nlohmann) et bien plus lourde que la spec ne le dit.
`src/lib/Params.h` inclut **les deux** : c'est le vrai pont.
Densité : `JsonApi.cpp` 102 appels, `JsonApiHandlerHttp.cpp` 81, `WagoExternProc_main.cpp` 38,
`IO/Scenario.cpp` 28, `JsonApiHandlerWS.cpp` 24, puis 20 fichiers ≤ 18.

Décomposition : (1) split dual-API de `Params` ; (2) mains ExternProc (feuilles autonomes) ;
(3) Ctrl/drivers feuilles ; (4) IODoc/IOFactory ; (5) Scenario + EventManager + Lua ;
(6) cœur JsonApi + handlers, puis retrait de jansson de `configure.ac:51`.

**⚠ RISQUE MAJEUR — format de fil** : jansson préserve l'ordre d'insertion des clés,
`nlohmann::json` **trie** par défaut → toute réponse de l'API 5454 change d'ordre de clés
(clients faisant des comparaisons de chaînes ou des diffs cassent silencieusement).
Parade : `nlohmann::ordered_json`. Autres risques : formatage numérique (`.0` final),
UTF-8 invalide (jansson ignore, nlohmann **throw** → nouveaux chemins d'exception).

---

## E4.2 — Modèle d'ownership : **PRÉMISSE INTACTE, PRÉREQUIS DÉSORMAIS SATISFAIT**

Rien n'a été fait sur le graphe d'ownership : `ListeRoom.h:37-43`, `Room.h:40`,
`ListeRule.h:44,47,50`, `Rule.h:37-38`, `AutoScenario.h:65` restent des conteneurs de pointeurs
bruts possédants. **177 sites `IOBase*` dans 25 fichiers**, 77 `Rule*`, 26 `Condition*/Action*`,
21 `Scenario*` ; `delete` manuels toujours à `ListeRoom.cpp:42,109,319`, `ListeRule.cpp:101,124`,
`Room.cpp:60`, `Rule.cpp:39,42,132`. Les smart pointers du tree (167 shared/56 unique/49 weak)
sont concentrés ailleurs (HttpClient, Timer, WebSocket…) — **zéro** dans le graphe IO/Rule.

Acquis incidents : le pattern alive-token (8 fichiers) élimine la classe UAF *callback async* ;
`Timer` supporte le delete-depuis-callback ; et les 8 binaires `tests/core/` (RuleLifecycle 18
cas, ListeRoomRobustness, IOIdIntegrity, ScenarioNullGuard) constituent le filet que E4.3
devait fournir → **le prérequis déclaré de E4.2 est levé**.

Décomposition : (1) helpers de résolution par id dans ListeRoom ; (2) `Room` possède des
`unique_ptr<IOBase>`, `io_table`/caches deviennent non-possédants ; (3) Condition/Action
référencent l'IO **par id** au lieu de `IOBase*` ; (4) `Rule` possède ses Condition/Action,
`ListeRule` possède ses `Rule` ; (5) back-pointers Scenario/AutoScenario → ids ou weak_ptr ;
(6) adaptation des 21 sites `IOBase*` de JsonApi.

**📍 État réel (2026-08-16) et renumérotation.** Les items **1 à 4 sont faits** : ils ont été
livrés comme **E4.2a** (helpers par id dans ListeRoom), **E4.2b** (`Room` possède ses
`unique_ptr<IOBase>`), **E4.2c** (Condition/Action référencent l'IO par id) et **E4.2d**
(`Rule` possède ses Condition/Action, `ListeRule` possède ses `Rule`) — tous ✅ au board.

⚠️ **Le fichier de ticket `E4.2e.md` n'est PAS l'item 5.** C'est un ticket issu d'une **décision
utilisateur** — « une règle dont une dépendance est manquante est désactivée » — qui ne figurait
pas dans le plan en 6 items ci-dessus. Pour que la numérotation cesse d'entrer en collision, les
items 5 et 6 d'origine reçoivent donc les numéros suivants :

- **E4.2f** (ex-item 5) — back-pointers Scenario/AutoScenario → ids ou weak_ptr. **Valeur
  concrète confirmée** : la revue d'E4.2d a localisé un **double-free latent réel exactement dans
  cette zone** (`RemoveRule(io)` détruisait une règle d'auto-scénario sans annuler `ruleStart`…,
  puis `deleteAll()` re-`delete`ait le pointeur périmé). E4.2d a neutralisé le crash côté
  ownership, mais les back-pointers bruts eux-mêmes subsistent — c'est ce que E4.2f doit traiter.

- **E4.2g** (ex-item 6) — adaptation des sites `IOBase*` de JsonApi. **À re-scoper avant tout
  lancement** : avec les accesseurs par id désormais en place (E4.2a/E4.2c), la valeur restante
  est vraisemblablement **cosmétique**. Mesurer ce qui reste réellement avant d'y consacrer un
  ticket.

**⚠ Risques** : l'ordre de `in_event` pilote l'ordre d'évaluation des règles (un réordonnancement
change quelle règle part en premier — silencieux) ; `delete_io(io, del=false)` est un transfert
d'ownership → doit devenir `release()`, pas `reset()` ; `getCameraList()/getAudioList()`
renvoient par valeur → changer le type d'élément change les payloads JsonApi.

---

## E4.4 — TinyXML 2.5.3 → TinyXML2 : intacte, **mais un bloqueur non listé**

Vendored : **12 818 LOC**, version confirmée `tinyxml.h:94-96`, 33 lignes dans
`src/lib/Makefile.am:72-104`. Consommateurs : 45 fichiers src, mais le poids est concentré —
`ConfigStore.cpp` 15 usages, `CalaosConfig.cpp` 15, `RemoteUI.cpp` 13, `InPlageHoraire.cpp` 11,
`Rules/*` 3-8 ; les ~30 autres n'ont que 1-2 signatures `TiXmlElement *node`.

**🚧 BLOQUEUR** : `IO/Web/WebCtrl.cpp:25,257` utilise **TinyXPath**
(`xpath_processor` / `S_compute_xpath`). **TinyXML2 n'a pas d'XPath.** Ce seul site décide de la
forme de l'epic : garder un shim XPath, ou réimplémenter la recherche de chemin de WebCtrl.

Décomposition : (1) introduire TinyXML2 sans toucher aux consommateurs ; (2) **supprimer la
dépendance XPath de WebCtrl (à faire en premier)** ; (3) sweep des signatures dans les ~30
en-têtes fins ; (4) cœur parse/serialize (CalaosConfig, ConfigStore, InPlageHoraire, RemoteUI) ;
(5) suppression de `src/lib/TinyXML/` + 33 lignes Makefile.

**⚠ Risques (tous silencieux)** : `Attribute()` vs `QueryXxxAttribute` — un portage naïf
transforme « attribut absent » en « attribut = 0 » et **réécrit la config de chaque IO à la
prochaine sauvegarde** ; politique d'indentation différente (`TiXmlPrinter` vs `XMLPrinter`) →
`io.xml`/`rules.xml` **reformatés intégralement** à la première sauvegarde ; TinyXML1 acceptait
des documents malformés que TinyXML2 rejette → des configs qui se chargeaient ne se chargent
plus (**casse d'ABI config**) ; CDATA/`TiXmlText` diffère.
Preuve exigée : charger un vrai `io.xml`/`rules.xml` de production, sauver, diffé
**sémantiquement** (jeu éléments/attributs) + diff binaire assumé consciemment pour le reformat.
