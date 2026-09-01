# Orchestration — état opérationnel & reprise

> Ce fichier est le **point d'entrée pour reprendre le travail** depuis n'importe quel
> Claude / n'importe quelle machine. Il est git-tracké : il voyage avec le repo.
> Lire dans l'ordre : ce bloc REPRISE → `DECISIONS.md` → `FINDINGS.md` → `BOARD.md`.

---

## 🔁 REPRISE — lire en premier

- **✅ [`T3.56`](T3.56.md) MERGÉE — 3 commits de la branche + 1 commit de doc (livrables documentaires trouvés **NON COMMITÉS** dans le worktree, commités avant rebase) + 1 commit de doc sur `master`, `merge --ff-only`, historique linéaire, 0 commit de fusion.** Tête de merge **`85619bc1`**.
  ⭐ **CE MERGE FERME LA VARIANTE PRODUIT DU DÉFAUT D'HORLOGE DE T3.49 — ET LE CORRECTIF N'EST
  AUCUN DES DEUX REMÈDES QUE LA FICHE PROPOSAIT.** `uv_timer_start()` ne lit pas l'horloge : il
  calcule son échéance depuis `loop->time`, le cache que seul `uv__update_time()` avance. Le
  correctif pose `loop->update()` dans les **trois** fonctions d'armement de `Timer` — `create()`,
  `Reset()` (les deux surcharges, la seconde **délègue** à la première) et `singleShot()` — plutôt
  que sur les sites appelants : **rien de l'arbre ne peut armer une échéance libuv sans y passer**,
  la propriété est structurelle et le prochain site écrit naît correct. `uv_update_time()` n'est
  **pas** une itération de boucle (il ne dispatche rien), donc sûr depuis un constructeur d'IO.

  **Revue : `approve`.** ⛔ **Et la MESURE, livrable n° 1 de la fiche, INFIRME LES DEUX VICTIMES
  DÉSIGNÉES** (`main.cpp` instrumenté, `calaos_server` réel, deux configs réelles 474/284 IO) :
  le décalage au `loop->run()` est de **39–64 ms** (134–142 ms avec l'hôte ralenti 5×), pas
  « plus de 100 ms » ; **le chargement lui-même ne coûte que 10–30 ms, le gros est le montage des
  services APRÈS** (`main.cpp:152-191`) — T3.49 l'attribuait à `LoadConfigIO`/`LoadConfigRule`.
  `IO/Wago/WagoMap.cpp:46` est **inoffensif même en tirant immédiatement** (socket liée **avant** le
  timer, le rappel n'envoie rien lui-même, et il **répète** toutes les 10 s) ; les **10 s** de
  `Audio/RoonPlayer.cpp:243` sont deux ordres de grandeur au-dessus. ⭐ **Quatre sites d'armement
  pré-boucle qu'aucun recensement n'avait listés** : `main.cpp:194`, `:195`, `:198` et
  `IO/Web/WebCtrl.cpp:115` via `Reset()`.

  ⛔⭐ **FAUX VERT DE VARIANTE NOUVELLE, consigné en `FINDINGS.md` — l'oracle dont la FIXTURE
  S'ÉVAPORE.** `uvw::Loop::getDefault()` met le wrapper en cache dans un **`weak_ptr`** : sans
  handle vivant, le `shared_ptr` rendu meurt en fin d'expression, `~Loop()` appelle
  `uv_loop_close()` qui remet `default_loop_ptr = NULL`, et le `uv_default_loop()` suivant refait
  **`uv_loop_init()`** — lequel appelle `uv__update_time()`. ⇒ **dans un binaire sans handle vivant,
  tout `uvw::Loop::getDefault()->x()` réinitialise silencieusement la boucle et rend une horloge
  FRAÎCHE.** La 1ʳᵉ rédaction des cas était donc **verte sur l'arbre non corrigé**. Les 4 cas
  **épinglent** la boucle pour toute leur durée **et vérifient que la fixture a bien produit une
  horloge périmée** (`loopClockStalenessMs()`). `calaos_server` n'est pas dans cette forme (le
  `Timer` du cache d'état, `CalaosConfig.cpp:206`, tient un handle) — ce qui rend le défaut **réel
  côté produit et invisible côté test**.

  ⚠️ **`master` avait avancé de `c287d700` à `e6ffe4d3` (E4.1n) ⇒ REBASE.** **Un seul conflit,
  `FINDINGS.md`**, appends des deux côtés, résolu par **régénération** (`git show
  master:…` en entier + append verbatim) : `diff` = **+104 / −0 / ~0**, `master` préfixe **STRICT**
  en octets (567 981 o, queue de 8 646 o), **0 marqueur de conflit**. ⭐ **`tests/Makefile.am` n'est
  PAS touché par ce ticket** (`core/Timer_test` existait déjà) ⇒ ni conflit ni entrée nouvelle.

  ⭐ **BUILD D'INTÉGRATION POST-REBASE, `make distclean` d'abord**, une seule invocation synchrone :
  **`rc=0`**, **0 `error:`**, **`# TOTAL: 103` / `# PASS: 102` / `# SKIP: 1` / `# FAIL: 0` /
  `# XFAIL: 0` / `# XPASS: 0` / `# ERROR: 0`** — entrées `TESTS` **103 → 103, inchangées**. Le `SKIP`
  est `run-python-tests.sh`, **normal**. `core/Timer_test` **PASS** (4 cas nouveaux, dont le
  **contrôle symétrique** qui borde le cas nominal des deux côtés pour qu'un « remède » armant tout
  loin dans le futur ne passe pas pour une amélioration).

  ⛔ **NOUVEAU À INSTRUIRE, demande de numéro (consigné en `FINDINGS.md`)** : **`RoonCtrl` ne se
  réabonne JAMAIS et l'abonnement peut être perdu SILENCIEUSEMENT.** `RoonPlayer.cpp:245` appelle
  `subscribeZone()`, qui envoie via `ExternProcServer::sendMessage()` — `if (client) { … }`
  (`IO/ExternProc.cpp:135`) : **sidecar pas encore connecté ⇒ message jeté sans un mot**. `RoonCtrl`
  n'écoute pas `processConnected`, donc **la zone ne remonte plus jamais son état**, et la perte se
  reproduit **après chaque respawn** de `calaos_roon`. Indépendant de l'horloge gelée.

  ⛔ **Non poussé.** Worktree `.wave71/t3.56` supprimé (via conteneur, artefacts root),
  `git worktree prune`, branche `fix/t3.56` supprimée.

- **✅ [`E4.1n`](E4.1n.md) MERGÉE — 4 commits de la branche + 1 commit de doc, `merge --ff-only`, historique linéaire, 0 commit de fusion.** Tête de la branche après rebase **`340a7f43`**.
  ⭐ **CE MERGE FERME L'ÉTAT DE `JsonApi` — ET, SURTOUT, LE PONT jansson↔nlohmann DE
  `RemoteUIWebSocketHandler`.** `buildJsonState/States/Query` rendent un **`Json` par valeur** (plus
  aucune référence à rendre sur aucun chemin, chemin d'abandon d'une chaîne async compris) ; les deux
  constructeurs de réponse de `set_state` passent à `Json`. **Aucun adaptateur transitoire** : la
  surcharge nlohmann de `sendJson()` existait déjà des deux transports. Les **6 lignes** de
  `json_dumps` + `Json::parse` de `RemoteUIWebSocketHandler.cpp` — une sérialisation ET une reanalyse
  complètes dont le seul rôle était de franchir la frontière entre les deux bibliothèques, à **chaque
  connexion d'appareil** — deviennent **un paramètre**. Ce `Json::parse` était aussi le **seul du
  fichier sans `try` au-dessus**, sur un callback de boucle : mine **latente** supprimée avant que
  l'épique ne l'arme. Appels jansson dans `src/` **734 → 677**.

  **Revue : `approve`.** Les trois invariants d'émission de la série E4.1 sont tenus **sans nouveau
  site d'émission** : les deux `sendJson(const Json &)` (`JsonApiHandlerHttp.cpp:273`,
  `JsonApiHandlerWS.cpp:105`) portent déjà `dump(-1, ' ', true, Json::error_handler_t::replace)`, et
  le ticket n'ajoute **aucun `dump()`** — RemoteUI hérite de la surcharge WS. **Aucun `int` ne devient
  un nombre JSON** : `TBOOL`/`TINT`/`TSTRING` et les cinq champs du joueur passent tous par
  `Utils::to_string()` ou une `std::string`, donc **tout sort en chaîne** (et `std::string` au lieu de
  `c_str()` ferme au passage la troncature silencieuse sur NUL embarqué). **La clé absente reste
  absente** : le joueur mort n'est pas écrit `null`, il est **omis** — épinglé par
  `PlayerDeletedMidFlightStillAnswers`. `Json::object()` et non `Json jret;` partout où l'objet vide
  est le contrat (`null` aurait été invisible pour les 145 goldens, qui comparent des documents
  parsés). Une couture de test assumée : un bloc de `RemoteUIWebSocketHandler.h` passe `private` →
  `protected`, zéro comportement, livrée dans un commit séparé.

  ⚠️ **`master` avait avancé de `75ed9cb9` à `15ecd096` (T3.49 puis T3.53, 11 commits) ⇒ REBASE.**
  **Deux conflits, tous deux des appends des deux côtés, et ZÉRO conflit dans `src/`** — conforme au
  recouvrement mesuré avant le merge (3 fichiers seulement : `BOARD.md`, `FINDINGS.md`,
  `tests/Makefile.am`).
  - **`tests/Makefile.am`** — recette **« régénération »** appliquée, **aucun marqueur édité** :
    `git show master:tests/Makefile.am` **en entier** + append **verbatim** du bloc `# E4.1n`.
    Prouvé **append pur** : `diff` = **+54 / −0 / ~0**, et `master` est un **préfixe STRICT** en
    octets (188 132 octets, queue de 3 769). Équilibre des marqueurs **en début de ligne** :
    `^if ` **88** == `^endif` **88** (dont `^if HAVE_GTEST` 87 + `if HAVE_LIBKNX` 1 — le fichier n'a
    **jamais** été à 88/87, master était déjà à 87/87), profondeur finale **0**, **minimum 0, jamais
    négative**. Entrées `TESTS` **102 → 103**, **toutes uniques**, **une seule ajoutée**
    (`core/JsonApiStateWireBytes_test`), **zéro retirée**.
  - **`FINDINGS.md`** — **les deux côtés gardés** : le bloc de `master` (T3.49/T3.55) d'abord, puis
    `## E4.1n —` appendu sous son propre titre `##`. Résultat = **`master` en préfixe strict** + une
    queue de **15 351 octets** ; **0 marqueur de conflit en début de ligne**.
  - **`BOARD.md`** — fusion automatique **vérifiée à la main** : les lignes **T3.55 et T3.56** de
    `master` survivent, la ligne `E4.1n` passe bien à ✅, et **le seul écart avec `master` est cette
    ligne**. Table à **6 colonnes / 7 `|`** sur les trois lignes concernées (les 5 lignes du fichier
    hors norme sont **préexistantes** : le tableau de synthèse à 5 colonnes en tête, et deux cellules
    de T3.35/T3.37 contenant un `|` littéral).

  ⭐ **BUILD D'INTÉGRATION POST-REBASE, `make distclean` OBLIGATOIRE d'abord** (piège `_DEPENDENCIES`
  en variante faux-rouge), puis `autogen.sh` + `configure` + `make -j32` + `make check -j16`, **une
  seule invocation synchrone** : **`rc=0`**, **0 `error:`**, et
  **`# TOTAL: 103` / `# PASS: 102` / `# SKIP: 1` / `# FAIL: 0` / `# XFAIL: 0` / `# XPASS: 0` /
  `# ERROR: 0`** — **= le recompte indépendant** des entrées `TESTS` (**103 uniques**). Le `SKIP` est
  `run-python-tests.sh`, **normal**. `core/JsonApiStateWireBytes_test` **PASS**, ainsi que
  `core/JsonApiAudioState_test` dont 3 assertions de refcount jansson ont dû être **remplacées** —
  l'acceptation « sans modifier d'assertion » de la fiche était **impossible** (il n'y a plus de
  compteur), et **la fiche le dit elle-même**, ce qui est le bon comportement.

  ⚠️ **Point légué à `E4.1s`, déjà consigné par l'auteur** : `processGetState()` garde
  `sendJson("get_state", nullptr, …)` sur la **surcharge jansson**, qui **omet** la clé `data` là où
  nlohmann écrirait `"data":null` — golden `e40e_ws_get_state_without_data` ⇒ **`get_state` a deux
  formes d'enveloppe** sur le websocket. C'est cohérent avec « zéro golden bougé » ; le jour où l'on
  unifie, **ce golden bouge et il faudra le déclarer**.

  ⛔ **Non poussé.** Nettoyage fait : worktrees `.wave70/e4.1n` et `.review70/e4.1n` supprimés
  (via conteneur, artefacts root), `git worktree prune`, branche `refactor/e4.1n` supprimée.

  ➡️ **PROCHAINE ACTION : [`E4.1o`](E4.1o.md)** — `JsonApi` **params + plages horaires**, c'est là
  que le `std::terminate` d'E4.0 (`?param=%ff%80x`) devient atteignable.
  ⚠️ **Et une décision utilisateur est en attente, NON TRANCHÉE ICI** : [`E4.1r`](E4.1r.md)
  (autoscénarios, 9 fonctions) est signalée **candidate à l'annulation** au profit d'**E4.6d**, qui
  les réécrit entièrement. **À arbitrer avant d'y arriver** ; l'agent de merge ne tranche pas.

- **✅ [`T3.53`](T3.53.md) MERGÉE — 4 commits de la branche + 1 commit de doc, `--ff-only`, historique linéaire, 0 commit de fusion.** Tête de merge **`0d445230`**.
  ⭐ **CE MERGE FERME LA PAIRE LA PLUS EXPOSÉE DU SOUS-SYSTÈME WAGO : celle dont les deux types
  sont le MÊME type.** `WagoMap.h:81-82` portaient `sigc::slot/signal<void, bool, string, string>` ;
  sur `master` la mutation par échange des deux `typedef` **ne peut même pas être ÉCRITE** (même
  texte des deux côtés), et les six permutations écrivables compilaient **`rc=0`, 0 `error:`**.
  ⇒ **aucun compilateur ne pouvait jamais rien en dire ; le typage par rôle était le seul remède.**

  ⚠️ **`master` avait avancé de `75ed9cb9` à `c287d700` (T3.49 mergée) ⇒ REBASE.** **Deux
  conflits, tous deux des appends des deux côtés.** ⭐ **Et le piège de l'`endif` ne s'applique
  PAS ici — c'est mesuré, pas espéré** : `tests/Makefile.am` a le **blob IDENTIQUE** sur
  `75ed9cb9` et `c287d700`, donc **aucun conflit**. Prouvé quand même sur le résultat : **préfixe
  STRICT** (`bytes.startswith`, jamais un `diff`) des **184 928 octets** de `master`, suivi d'une
  **queue de 49 lignes** qui est exactement le bloc du ticket ; `^if*`/`endif` **86/86 → 87/87**,
  profondeur finale **0**, **minimum 0, jamais négative** ; entrées `TESTS` **101 → 102**, toutes
  uniques, **une seule ajoutée** (`core/WagoUdpReply_test`).
  - **`FINDINGS.md`** — **les deux côtés gardés**, T3.49 (de `master`) puis T3.53 appendue sous son
    propre titre `##`. Contrôles : **0 marqueur en DÉBUT DE LIGNE**, `## T3.49 —` et `## T3.53 —`
    **une fois chacun**, résultat = **`master` en préfixe strict + 4 963 octets de queue**, et
    **0 ligne longue dupliquée dans la zone fusionnée** (les **4** doublons du fichier sont
    **préexistants sur `master`**, mesuré, aucun ne touche la queue).
  - **`BOARD.md`** — les deux côtés ajoutaient une ligne : T3.56 (`master`) et T3.55 (branche).
    **Tri par NUMÉRO** ⇒ T3.55 **avant** T3.56. ⛔ **Un défaut de table trouvé au passage** : la
    ligne `T3.55` livrée n'avait que **3 colonnes sur 6** (4 `|` au lieu de 7) — elle **cassait le
    tableau**. **Complétée au merge** (`| fix | — | 📋 |`).

  ⭐ **LES TROIS RÉSERVES ONT ÉTÉ REJOUÉES, PAS RELUES.**
  1. ⭐ **R3 — le contrôle sur lequel repose toute la méthode, rejoué avec le NOUVEAU harnais.**
     `M0`, `M5`, `M6`, `M7`, `M0-end` sur l'arbre **rebasé** (harnais de l'auteur, `ORDER` réduit,
     rien d'autre changé). ⇒ **`M0` et `M0-end` : `rc=0`, ensemble VIDE, aucune cible en échec,
     `CXXLD calaos_server` ET `CXXLD calaos_wago`** ; **`M5`/`M6`/`M7` : `rc=2`**, **une seule**
     unité en échec (`IO/Wago/WODaliRVB.o`), **3 verdicts non vides deux à deux DISTINCTS**,
     restauration **comptée 7/7** et vérifiée octet à octet.
     ⚠️ ⭐ **CE QUI PORTE RÉELLEMENT LA DISTINCTION, ET IL FAUT LE DIRE** : les ensembles de lignes
     `error:` de M5, M6 et M7 sont **IDENTIQUES au caractère près** — sigc++ nomme le foncteur, pas
     le site. **Seul** le `required from here` lu dans le journal **PROPRE** de l'unité les sépare :
     `WODaliRVB.cpp:73` · `:75` · `:77`. ⇒ **la distinction au grain du CAS tient, et elle tient
     ENTIÈREMENT par l'attribution par unité de compilation.**
     ⭐ **Et l'ANCIENNE règle est retombée dans son piège sous mes yeux** : le harnais enregistre en
     parallèle ce qu'aurait répondu « le dernier `required from here` du journal `-j8` », et il
     répond **`IO/Wago/WOVoletSmart.cpp:44`** — **une TU étrangère** — pour **M5, M6, M7 ET pour
     `M0-end` où RIEN n'échoue** (et `WOVolet.cpp:44` pour `M0`). **La réserve était fondée et la
     correction est la bonne.**
     ⚠️ **Ce que je n'ai PAS vérifié** : l'auteur déclare n'avoir jamais retrouvé le harnais
     d'origine — sa « règle naïve » est une **reconstitution**. L'écart 3-paires/0-paire est donc
     mesuré **sur cette reconstitution**, et je n'ai pas de moyen d'en juger autrement. Ce qui est
     établi, c'est que **la règle naïve telle que reconstituée désigne bien du bruit de journal**,
     ici comme chez l'auteur.
  2. **R1 — prémisse corrigée, revérifiée.** `docs/refactoring/RELEASE_NOTES.md` **existe**, est
     **suivi par git**, fait **1279 lignes** et **51 commits** le touchent. §7.6 est bien réécrit en
     **« aucune entrée due »**, la formulation de T3.46 §7.10 / T3.50 §7.7. Conclusion revérifiée :
     **2 `*_HEADERS` dans tout `src/`**, **les deux dans `src/lib/libquickmail/Makefile.am`** ;
     `src/bin/calaos_server/Makefile.am` n'en déclare **aucune** ⇒ signatures **internes**.
  3. **R2 — reformulée aux CINQ endroits, et les mesures sous-jacentes refaites.** Les quatre du
     ticket (`T3.53.md` §3 et §6.4, `FINDINGS.md`, `tests/Makefile.am`, `tests/core/WagoUdpReply_test.cpp`)
     **plus la ligne `BOARD.md`** qui portait encore l'ancienne phrase — **vérifiés un par un** :
     tous disent « **0 fichier nommé `*Dali*`** sous `tests/` » et « **aucun test du chemin de
     RÉPONSE UDP** », et **tous citent `WagoPortDefault_test.cpp:220`/`:228`**. La phrase « aucun
     test DALI » ne subsiste que là où elle est **explicitement corrigée**. Mesures refaites sur
     `c287d700` : **0** fichier nommé `*Dali*` sous `tests/` (**2** fichiers seulement mentionnent
     DALI), `:220` et `:228` sont bien `ADaliOutputWithABlankPortKeepsTheModbusDefault` et
     `ADaliRvbOutputWithABlankPortKeepsTheModbusDefault`, et ce fichier ne nomme **ni**
     `udpRequest_cb` **ni** `WagoUDPCommand` **ni** `SendUDPCommand` — **0 occurrence des trois**.

  ⭐ **[`T3.55`](T3.55.md) — VÉRIFIÉE AU MERGE, y compris le point qui décide de sa priorité.**
  **84 sites recomptés à l'identique** par balayage indépendant (commentaires blanchis) : **2**
  `typedef` (`Squeezebox.h:37`, `:38`), **27** déclarations (`:92`…`:137`), **26** définitions,
  **28** `sigc::mem_fun` sur **26** cibles distinctes, **1** émission (`Squeezebox.cpp:408`) —
  **56** à la convention de T3.53 §5 (sans les enregistrements) contre **13**, donc le « ~78 contre
  13 » du signalement est bien corrigé. Périmètre confiné à **2 fichiers** de tout `src/`, et la
  déclaration morte `get_album_cover_id_cb` (`:135`) est confirmée **déclarée, jamais définie**.
  ⭐ **Sonde `-Wunused-parameter` REJOUÉE dans le conteneur** : unité `rc=0`, témoin négatif **0**,
  ⇒ **26 avertissements `unused parameter 'request'` exactement aux 26 lignes de définition**,
  **0 pour `result`** ; sonde au site (`p.Parse(result)` → `p.Parse(request)` dans `get_album_cb`)
  ⇒ `result` apparaît **exactement à `Squeezebox.cpp:718`** et les `request` tombent **26 → 25** ;
  restauration ⇒ **0 / 26**. ⇒ ⭐ **la réponse est bien l'INVERSE de T3.53** : `result` lu par les
  26 implémentations, `request` par **aucune** — et la gravité reste **HAUTE**, puisque après
  permutation **les 26 analyseurs parsent la commande**.
  ⭐ **ET LE FILET EST BIEN ABSENT, MESURÉ** : `Squeezebox_test_LDADD` et
  `SqueezeboxWire_test_LDADD` **ne contiennent PAS** `Audio/Squeezebox.$(OBJEXT)` — leurs
  `_SOURCES` se réduisent au `.cpp` du test, leur `LDADD` à `libcalaos_common.la` + gtest ⇒
  ⛔ **`F-LINK-1` dans sa forme la plus littérale : « couvert » n'y veut pas dire « exercé »**.
  **C'est ce qui fixe la priorité de T3.55 : haute gravité ET aucun filet.**
  ⛔ **Deux chiffres de la fiche corrigés au merge** : « **deux** callbacks enregistrés **deux**
  fois » est faux — c'est **UN SEUL**, `get_playlist_info_cb`, enregistré **TROIS** fois
  (`Squeezebox.cpp:1292`, `:1309`, `:1315`) ; les totaux 28/26 sont justes. Et « **15** fichiers de
  `tests/` mentionnent Squeezebox » ⇒ **10** (13 en insensible à la casse).

  ⭐ **BUILD DE VALIDATION POST-REBASE, arbre NEUF, un seul conteneur attendu par `docker wait`**
  (`autogen.sh` + `configure` + `make -j32` + `make check -j8`, tout sur DISQUE) :
  **`AUTOGEN_RC=0 CONFIGURE_RC=0 MAKE_RC=0 CHECK_RC=0`**, **`# TOTAL: 102`** — **= mon recompte
  indépendant** des entrées `TESTS` de `tests/Makefile.am` (**102 uniques**, **96** binaires gtest
  + **6** scripts) —, **`# PASS: 101` / `# SKIP: 1` / `# FAIL: 0` / `# XFAIL: 0` / `# XPASS: 0` /
  `# ERROR: 0`**, **un seul `Testsuite summary`**, **0 `error:`** dans les deux journaux,
  **108 lignes `CXXLD`** (regex **ancrée** `^  CXXLD +\S+$`, **double espace**) dont
  **`calaos_wago` 1**, **`calaos_server` 1** et **`core/WagoUdpReply_test` 1**. **102 `.trs` =
  101 `PASS` + 1 `SKIP`** — le `SKIP` est `run-python-tests.sh`, **vérifié dans son propre `.trs`**,
  **normal** ; **103 `.log`**, le 103ᵉ étant l'agrégat `test-suite.log`.
  ⭐ **Les CAS et les DURÉES sont recomptés, pas recopiés** : **1668 cas exécutés**, **1667 `OK` +
  1 `SKIPPED`**, **aucune suite à 0 cas**. ⚠️ **1668 et non les 1667 de l'auteur, et l'écart est
  EXPLIQUÉ, pas arrondi** : `master` a bougé entre-temps et **T3.49 a ajouté un témoin à
  `ShutterImpulse` (19 → 20 cas)**. Le `SKIPPED` est
  `ConfigRobustnessTest.FailedSaveKeepsThePreviousConfigIntact`, **préexistant**, sans rapport.
  `core/WagoUdpReply_test` rend **`9 tests from 2 test suites`**, **`[  PASSED  ] 9 tests.`**,
  **112 ms**. **88,7 s** de gtest cumulé ; les deux cas les plus longs sont **exactement
  `5000 ms`** — `LuaSandbox.InfiniteLoopIsAbortedByTheWatchdog` et
  `…HotNumericLoopIsAbortedByTheWatchdog` —, c'est-à-dire `SCRIPT_MAX_EXEC_TIME 5.0`
  (`LuaScript/ScriptManager.h:31`) : ⭐ **le plateau EST la fenêtre du chien de garde, il n'y a
  RIEN à instruire** ; le 3ᵉ est à **2751 ms**.
  ⚠️ ⭐ **`F-FLAKY-1` PAS VU** : `core/ShutterImpulse_test` rend **`[  PASSED  ] 20 tests.`** et
  **0 ligne `[  FAILED  ]`**, **aucune relance**. **Et depuis T3.49 ce ne serait plus « le
  flottement connu » : si ce test rougit désormais, c'est une régression à INSTRUIRE.**
  ⭐ **145 goldens, arbre `tests/core/golden` = `d4ebc61fb2b1876f587d075a0cb050750dc1876f`,
  IDENTIQUE à `master`, aucun bougé — vérifié APRÈS le build, APRÈS la campagne de mutation ET
  APRÈS la sonde T3.55.** `git status -uall` **VIDE** hors ce commit de doc. ⛔ **Non poussé.**

  ⚠️ **CE QUE L'AUTEUR DÉCLARE NON FAIT — jugé, et je confirme que la fiche le dit ainsi.**
  - **Le harnais original n'a pas été retrouvé** : la « règle naïve » est une **reconstitution**
    (voir R3 ci-dessus). **Non réparable a posteriori** ; ce qui compte — que l'attribution par
    unité de compilation soit, elle, correcte — **est mesuré**.
  - ⭐ **`T3.55` n'a subi AUCUNE mutation compilée**, et **la fiche l'écrit noir sur blanc** (§6,
    *« je l'affirme par analogie de forme avec T3.53 — c'est un raisonnement, pas une mesure, et le
    ticket devra le mesurer »*). **Vérifié : la fiche ne surestime pas ce qu'elle a fait.**
  - **La phase A (côté `master`) de la campagne n'a pas été rejouée**, seulement la phase B —
    **exact**, et je n'ai pas rejoué la phase A non plus : ce que j'ai mesuré, c'est que **l'arbre
    LIVRÉ refuse les permutations**, pas que `master` les acceptait.
  - **Les 24 cas des tests Squeezebox sont un comptage de `^TEST(`** — **revérifié : 7 + 17 = 24**,
    et **ces binaires n'ont toujours pas été exécutés isolément**. Sans importance ici, puisque le
    point décisif est qu'ils **ne lient pas l'objet de production**.


- **✅ [`T3.49`](T3.49.md) MERGÉE — 5 commits de la branche + 1 commit de doc, `--ff-only`, historique linéaire, 0 commit de fusion.**
  ⭐ **CE MERGE ENLÈVE LE SEUL ÉCHEC CONNU DE `master` : `master` redevient VERT AU SENS STRICT.**
  `F-FLAKY-1` — l'horloge **en cache** de `libuv` — est fermée, et le diagnostic d'origine de la
  fiche (*« l'ordonnanceur vole du temps à `pumpLoopFor()` »*) reste **INFIRMÉ**.
  ⭐ **`master` était sur `75ed9cb9` = EXACTEMENT la base de la branche** (le 3ᵉ rebase de l'auteur
  l'y avait déjà posée) ⇒ **ni rebase ni conflit**. `tests/Makefile.am` : **blob IDENTIQUE à
  `master`**, donc le piège de l'`endif` — qui a mordu trois fois ailleurs — **ne s'applique pas
  ici** ; vérifié quand même en `python3` sur les marqueurs **en début de ligne** : **86 `if` / 86
  `endif`**, profondeur finale **0**, **minimum 0, jamais négative**. **`git diff --numstat` sur les
  5 commits : ZÉRO fichier de `src/`** — 3 fichiers de `tests/core/` et 3 de `docs/refactoring/`.

  ⭐ **Les trois réserves ont été REJOUÉES, pas relues.**
  1. ⭐ **R1 — la sonde `D=95` est VERTE, remesurée dans l'image** (`libuv 1.44.2`, sonde autonome de
     l'auteur rejouée telle quelle). En **phase *check*** — la queue **post-sondage**, la seule
     fenêtre résiduelle réelle — échéance 182 / sonde 91 : **ordre d'avant ⇒ 87, 20/20 ROUGES** ;
     **ordre livré ⇒ 182, 0/20**. Couple de `:384` (146 / 73) : **51 contre 146**, **20/20 contre
     0/20**. Balayage `D = 0 / 40 / 95 / 200` ⇒ **182 / 142 / 87 / 0** contre **182 / 182 / 182 /
     200**. ⭐ **Et les deux lignes ont été vérifiées une à une sur les 12 sites** (7 dans
     `ShutterImpulse_test`, 5 dans `IoLifetimeTimer_test`) : **12/12 avec l'origine prise AVANT
     `freshenLoopClock()`, 0 mauvais**.
     ⭐ **La phrase est bien réécrite en INÉGALITÉ PAR CONSTRUCTION** — `loop->time ≥ origine` ⇒
     `échéance ≥ origine + délai` — et l'ancienne (« `gap > marge` devient inatteignable ») n'est
     plus affirmée nulle part : elle ne subsiste que dans les deux passages qui la **corrigent
     explicitement** comme ayant été *trop forte*.
     ⭐ **`libuv` relit bien l'horloge DEUX fois par itération**, et c'est **remesuré ici** : les
     mêmes `D=95` brûlés **avant** le sondage rendent **182 et 0/10 rouge**, la même brûlure
     **après** rend 87 et 20/20 ⇒ la fenêtre résiduelle est bien **la seule queue post-sondage**.
     ⚠️ **C'est aussi pourquoi la 1ʳᵉ livraison passait quand même** : cette queue vaut
     *normalement* des microsecondes — **et « normalement » n'est pas une borne.**
  2. **R2 — le lien avec le rouge 1-sur-80 de [`T3.40`](T3.40.md) est bien AFFAIBLI EN HYPOTHÈSE**,
     dans la fiche **et** dans `FINDINGS.md` : *« le mécanisme suffirait … mais il ne se reproduit
     pas »*, **240 exécutions ⇒ 0 rouge**. ⭐ **`FINDINGS.md` ne monte plus d'un cran de certitude**,
     et la leçon de rédaction est écrite noir sur blanc : *« un report d'une fiche vers le journal
     ne doit jamais monter d'un cran dans l'échelle de certitude ; c'est le sens de la marche qui
     trahit, pas le mot choisi. »*
  3. ⭐ **R3 — la seule partie qui touche le PRODUIT. Les trois maillons du « le processus
     REDÉMARRE » sont vérifiés AU SOURCE, pas crus** : (a) `LoadConfigIO` et `LoadConfigRule` n'ont
     **qu'un seul site d'appel chacune** (`main.cpp:150` / `:151`) — les seules autres occurrences de
     l'arbre sont leur **définition** (`CalaosConfig.cpp:262` / `:353`) et leur **déclaration**
     (`CalaosConfig.h:61` / `:62`) ; (b) `setNeedRestart(true)` (`JsonApiHandlerHttp.cpp:707-708`) et
     `uvw::Loop::getDefault()->stop()` (`HttpClient.cpp:480`) sont les **seules** occurrences de
     `src/` ; (c) la création d'IO à chaud est bien **dans** `uv_run()` — c'est
     `JsonApi::buildAutoscenarioCreate`, appelée par le handler JSON. ⇒ **risque CANTONNÉ AU
     DÉMARRAGE, une fois par vie du processus.**
     ⛔ **Une correction de fait apportée au merge** : `FINDINGS.md` écrivait `JsonApi.cpp:1973` pour
     ce dernier maillon ; `:1973` est en réalité `buildAutoscenarioGet` → `get_io()`. Le vrai site
     `ListeRoom::createIO` est **`JsonApi.cpp:2004`**. **La substance tient, le numéro était faux —
     rectifié dans `FINDINGS.md` et repris dans la fiche neuve.**
     ⭐ **Les dix armements pré-boucle re-vérifiés au source, valeurs comprises** : `WagoMap.cpp:46`
     **0,1 s** et `:47` 10 s · `HueOutputLightRGB.cpp:47` 2 s · `KNXIo.h:78` 1,5 s ·
     `RoonPlayer.cpp:243` 10 s (le commentaire *« wait for the process to start »* est bien là,
     ligne 238) · `CalaosConfig.cpp:232` 30 s (`CONFIG_ALERT_DELAY_SEC = 30.0`) · `AVRRose.cpp:67`
     30 s (`POLL_INTERVAL = 30.0`) · `InputAnalog.cpp:71` 4 h
     (`IOBase::TimerChangedWarning = 60*60*4`) · `InputAnalog.cpp:63` où `Utils::getMainLoopTime()`
     (`Utils.cpp:136`) rend bien `loop->now()` · et `CalaosConfig.cpp:206` **n'est PAS une victime :
     c'est l'ORIGINE**, il crée la boucle. **La conséquence est écrite comme demandé** : le décalage
     vaut **la durée du chargement de la configuration**, ces attentes sont **plus courtes
     qu'annoncé**, et **celles sous ce temps tirent immédiatement**.
     ⭐ **[`T3.56`](T3.56.md) CRÉÉE** pour les deux sites qui le méritent — `WagoMap.cpp:46` (0,1 s,
     le plus court de l'arbre) et `RoonPlayer.cpp:243` (10 s, dont le délai **est** la sémantique).
     Ligne `BOARD.md` insérée **triée par numéro**, après T3.54. **Aucun autre numéro ouvert.**

  ⭐ **LE TÉMOIN QUE L'AUTEUR DÉCLARAIT MANQUANT A ÉTÉ AJOUTÉ, et il est POSITIF.** La campagne
  post-R1 de l'auteur (24 exécutions, 0 rouge) n'avait **pas** de témoin : sans binaire `master`
  reconstruit, elle ne pouvait pas établir que la sonde **voit encore**. Refaite ici de bout en
  bout, dans le même conteneur, **96 brûleurs**, **alternée**, **48 exécutions de chaque côté** :
  binaire `master` **reconstruit** depuis un source **byte-identique au blob de `master`**
  (`sha256 c52c25c3e9a027df…`, comparé au `git show 75ed9cb9:`), `rm -f` des chemins exacts
  (binaire + `.o` du test + les deux `.o` serveur) et **`CXXLD` ancré exigé à chaque relink**.
  ⇒ ⭐ **`master` : 1 rouge / 48**, sur `PlainImpulseDownWithoutImpulseTimeStillHonoursTheDuration`
  — le cas de `:384`, **la marge la plus courte du tableau corrigé**, exactement celui que l'auteur
  voyait tomber ; **forme livrée : 0 rouge / 48**. **La sonde voit encore, et la forme livrée y est
  insensible.**
  ⚠️ **1/48 ici contre 5/48 chez l'auteur** — hôte moins chargé. **C'est la leçon déjà écrite** :
  un taux de flottement n'est pas une constante, **c'est l'ORDRE qui est prédit, pas le TAUX**.
  *(⚠️ Un premier essai à N=24 avait rendu **0/24 des deux côtés**, donc **non concluant** : à ce
  taux, ne rien voir en 24 tirages n'a rien d'improbable. C'est pourquoi le protocole complet à 48
  a été refait — et c'est la raison pour laquelle la campagne de l'auteur ne prouvait rien.)*

  ⭐ **`M0` et `MU-1` rejouées**, mutation appliquée à la main sur ancre unique, avec **preuve de
  non-débordement** (`OutputShutterSmart.cpp` recomparé octet pour octet), restauration **sans
  préservation des dates** et **fichiers comptés** (3/3) : **`M0` (ensemble VIDE) ⇒ `rc 0`, 20/20
  OK, aucune ligne `FAILED`** ; **`MU-1` ⇒ `rc 1`, 13 OK et EXACTEMENT 7 rouges, TOUS `Plain*`** —
  la liste de la fiche **au cas près** (`PlainImpulseDownKeepsMovingUntilTheRequestedDuration`,
  `…AfterAnIdleLoopGap`, `…Publishes…`, `…Receives…`, `…WithoutImpulseTimeStillHonoursTheDuration`,
  `PlainImpulseUpAndDownAgreeOnTheSameDuration`, `PlainOutOfRangeImpulseLeavesNoTimerArmedForEver`).
  Après quoi l'arbre a été **restauré et reconstruit** : `tests/` et `src/` rendent **0 ligne** de
  `git status --porcelain -uall`.

  ⭐ **Build de validation** (image du dépôt, `autogen` + `configure` + `make -j32` + `make check
  -j8`, tout sur DISQUE, `rm -f` préalable des chemins exacts) : **`MAKE_RC=0`, `CHECK_RC=0`**,
  **`# TOTAL: 101`** — **= mon recompte indépendant des entrées `TESTS` de `tests/Makefile.am`**
  (**95 binaires gtest + 6 scripts**) —, `PASS 100 / SKIP 1 / FAIL 0 / XFAIL 0 / XPASS 0 /
  ERROR 0`, **un seul `Testsuite summary`**, **0 `error:`**, **107 `CXXLD`** en regex ancrée à
  double espace (dont les **trois** tests touchés, chacun réellement relinké), **1659 cas exécutés
  sur 95 binaires**. Le `SKIP` est `run-python-tests.sh` — **normal**.
  ⭐ **Et les DURÉES, cas par cas, sont toutes AU-DESSUS de leur échéance** — c'est ce qui interdit
  qu'un cas soit **creux** : `205 / 325 / 347 / 174 / 383 / 486 ms` mesurés contre des échéances de
  `182 / 302 / 318 / 147 / 360 / 462 ms` pour les six oracles de durée (le 2ᵉ inclut bien les
  `kIdleGapMs = 120` du cas d'écart oisif) ; `PlainOutOfRange` **1175 ms** ;
  `ProcessExitedStillFiresWhileTheServerIsAlive` **127 ms** contre 100. Suites :
  `ShutterImpulse` **20/20 en 5943 ms**, `IoLifetimeTimer` **16/16 en 7259 ms**, `Timer` **8/8 en
  366 ms**.
  ⭐ **Goldens vérifiés APRÈS le build et APRÈS la campagne de mutation** : **145 fichiers**, arbre
  `tests/core/golden` = **`d4ebc61fb2b1876f587d075a0cb050750dc1876f`**, **zéro golden bougé**.
  `git status -uall` **vide** hors ce commit de doc. ⛔ **Non poussé.**

  ⚠️ **Ce qui reste NON ÉTABLI, et que le merge ne prétend pas avoir fermé** — repris de la
  déclaration de l'auteur, confirmé :
  - **Rien n'est observé sur un VRAI démarrage** : la durée du chargement de configuration n'est
    **pas mesurée**, aucun tir prématuré n'a été **vu**. R3 est de la **lecture de source**, et
    [`T3.56`](T3.56.md) fait de cette mesure son **premier livrable**.
  - **Le classement pré-boucle porte sur 11 sites sur 99.** Les 88 autres **ne sont pas classés** —
    consigné dans la fiche neuve comme un livrable, pas comme un détail.
  - **La sonde `order_probe` FABRIQUE la queue** avec un `uv_check_t`. Elle prouve **que la fenêtre
    existe et que l'ordre livré la ferme** ; elle ne prouve **pas** que cette queue atteint 95 ms
    dans la suite réelle. Ce que la suite réelle établit, c'est le **témoin ci-dessus**.

- **✅ [`E4.1m`](E4.1m.md) MERGÉE — `69fdc3c6`, 8 commits, `--ff-only`, historique linéaire, 0 commit de fusion.**
  ⭐ **`master` était IMMOBILE sur `7667838f`** = exactement la base de la branche (le 3ᵉ rebase de
  l'auteur l'y avait déjà posée) ⇒ **ni rebase ni conflit au merge**. `tests/Makefile.am` : la
  propriété *« `master` est un **PRÉFIXE STRICT** du résultat, **+3597 octets exactement** »*
  **reprouvée en `python3`**, **86 `if` / 86 `endif`** (master 85/85), profondeur finale **0**,
  minimum **0**. **`git diff-tree` sur les 8 commits : UN SEUL touche `src/`** (`d79f6459`,
  6 fichiers), **les 7 autres à 0**. Build de validation en distclean : **rc 0**,
  **`# TOTAL: 101`** (master **100**) **= mon recompte indépendant du `tests/Makefile.am`**
  (**95 gtest + 6 scripts**), `PASS 100 / SKIP 1 / FAIL 0 / ERROR 0`, **un seul `Testsuite
  summary`**, **0 `error:`**, **107 `CXXLD`** (regex ancrée), **1658 cas** comptés **deux fois et
  concordants**, **aucun binaire à 0 cas**, **88,7 s** de mural gtest cumulé, **145 goldens / arbre
  `d4ebc61f` inchangé** (vérifié **APRÈS**), `git status -uall` **vide**. ⛔ **Non poussé.**

  ⭐ **Les trois réserves ont été REJOUÉES, pas relues :**
  1. ⭐ **R1 tient, et `X2` est chirurgicale.** Mutation réappliquée à la main : `cmp` rend
     **« differ: byte 7424, ligne 240 »**, *le chiffre exact de la fiche*. Protocole complet
     (`rm -f` du `.o` serveur **et des 95 binaires** — 95/95 réellement supprimés — build depuis la
     **RACINE**, **96 `CXXLD`**) : sur les **6** cas de `JsonApiRedact`, **seul le cas neuf tombe**,
     sur **exactement** ses 4 assertions `:187 :190 :193 :196`, **les 5 autres verts**. ⭐ **Et les
     assertions sont bien des PAIRES** : `:187`/`:193` constatent l'**absence du secret**,
     `:190`/`:196` la **présence du champ masqué** (`"CN_Pass": "***"`) — les deux moitiés tombent
     ensemble, donc le cas distingue vraiment *« masqué »* de *« supprimé »*. L'assertion sur la clé
     non-credential (`"Action"`, `:201`) **reste verte** sous mutation, comme elle doit.
  2. ⭐ **R2 vérifiée au SOURCE, pas seulement dans les fiches.** La mine est bien désamorcée
     **aux 4 endroits** (`E4.1m.md` accept. n° 6 + § R2, `E4.1o.md` § *Pièges propres* **en tête**,
     `FINDINGS.md`, `BOARD.md`). Mécanisme reconfirmé sur l'arbre mergé : `ScriptExec.cpp:131` porte
     bien `if (!jsonApi->buildJsonSetParam(p))`, `JSON_USE_IMPLICIT_CONVERSIONS` = **1**
     (`json.hpp:2813`), et **0 `try` réel** dans les trois fichiers (l'unique occurrence du mot dans
     `ScriptExec.cpp` est le sous-mot d'« en**try** » dans un commentaire — vérifié à la limite de
     mot). ⭐ **Le bonus est vrai aussi** : `JsonApi.cpp:709-737`, **les deux branches** rendent
     `jansson_from_params(ret)` (`{"error": "wrong io/param"}` ou `{"success": "true"}`), **jamais
     `NULL` hors OOM** ⇒ **la garde est DÉJÀ inerte** et le `cWarningDom("lua")` n'a jamais été
     imprimé.
  3. ⭐ **R3 tient, et la nuance a été REMESURÉE.** Sonde `g++ -std=c++17` sur le `json.hpp` du
     dépôt : nlohmann rend `0.1` / `3.14159` / **`1e+50`** / **`1e-07`** là où `%.17g` rend
     `0.10000000000000001` / `3.1415899999999999` / `1.0000000000000001e+50` /
     `9.9999999999999995e-08` — **exposant compris** — et `42` / `1.5` sont **identiques**. La
     nuance *« les deux bibliothèques parsent et REFUSENT le document, le journal reçoit `""` des
     deux côtés »* est bien **écrite et gardée** (`FINDINGS.md`) : elle **réduit la portée réelle**
     de la 3ᵉ famille au document **construit en mémoire**.

  ⭐ **Contre-mutations rejouées de bout en bout** : **témoin `M0` = ensemble VIDE** (9/9 verts), et
  **`M1` rend 5 cas rouges** — **au grain du CAS, pas du binaire** (2 binaires / 5 cas) — **dont
  les DEUX goldens `get_home`** (`WsGetHomeMatchesGolden`, `HttpGetHomeMatchesGolden`), plus
  `W_RoomsAndIosKeepTheirDeclarationOrder`, `WsGetHomeKeepsRoomAndIoOrder` et
  `CamerasAndPlayersAreNotVisibleRoomItems`. **Le filet des goldens est PORTEUR.** *(Ma mutation a
  été réécrite indépendamment de celle de l'auteur et rend le même ensemble — c'est le contrôle.)*

  ⚠⭐ **`F-FLAKY-1` VUE, et NON RELANCÉE.** Elle a mordu pendant l'exécution `X2` :
  `core/ShutterImpulse_test.cpp:297`,
  `ShutterImpulseTest.PlainImpulseDownKeepsMovingUntilTheRequestedDuration`, forme
  `EXPECT_FALSE(sh.isStopped())` après `pumpLoopFor(stillMovingProbeMs(…))` — **le site le plus
  serré des 6** de la famille, et **le même** que celui de l'auteur. Identifiée par **FICHIER ET
  FORME**, **aucune relance dédiée**, la suite est repassée verte à l'exécution suivante du
  protocole. ⚠ **Le contrôle `nm` ne s'applique pas** (cette suite **lie** la clôture serveur) :
  ce qui exclut la causalité, c'est que le fichier contient **0 occurrence** de `Json`, `json`,
  `JsonApi` — *compté* — et que le marqueur mesure **0 passage**.

  ⭐ **Ce que ce merge ajoute pour les suivants :**
  1. ⛔⭐ **Le « faux vert de relink » a une forme INVERSE : le faux ROUGE de relink — et je suis
     tombé dedans.** Après avoir restauré la pristine, j'ai rebuildé en ne supprimant que
     `src/bin/calaos_server/JsonApi.o` : `make -j16` l'a bien recompilé, mais **`make check` a rendu
     `# FAIL: 1` sur `JsonApiHardening_test`, aux 4 mêmes assertions** — *sur un arbre pourtant
     `cmp`-identique à la pristine et `git status` vide*. **`CXXLD` pendant le `make check` : 0.**
     Le binaire de test n'avait **pas** été relié et exécutait encore le **mutant**. ⭐ **La cause
     est ÉCRITE DANS L'ARBRE et elle est DÉLIBÉRÉE** : `tests/Makefile.am` passe les objets serveur
     par `CORE_SERVER_OBJECTS` dans `LDADD` avec le commentaire *« They are not repeated in
     `_DEPENDENCIES` on purpose: they are built by another Makefile and make has no rule for them
     here »* ⇒ **aucune dépendance make ne relie un binaire de test à un `.o` serveur modifié.**
     ⇒ **Le `rm -f` des binaires n'est pas une cérémonie du protocole, c'est sa CONDITION DE
     VALIDITÉ** — dans les **deux** sens : sans lui, une mutation peut se lire verte, **et une
     restauration peut se lire rouge**. ⭐ *Le seul oracle reste le compte de `CXXLD`, jamais le
     `# PASS` ni le `# FAIL`.* Vérification finale rejouée **avec** le `rm -f` des 95 binaires :
     `rc=0`, **101 / PASS 100 / SKIP 1 / FAIL 0**, 1658 cas, 86,7 s, goldens `d4ebc61f`, arbre
     propre.
  2. ⛔⭐ **UNE PHRASE DE FICHE DÉMENTIE PAR LE MERGE, et c'est la 4ᵉ « portée trop large » de la
     série.** La ligne `E4.1m` de `BOARD.md` se terminait par *« la requalification d'`E4.1n` en
     documentation pure est **revérifiée vraie depuis ici** »*. **FAUX, vérifié au source sur
     l'arbre mergé** : le pont **`json_dumps` + `Json::parse`** est **toujours là** dans
     `RemoteUI/RemoteUIWebSocketHandler.cpp` — callback `(json_t *jret)` **:236**,
     `json_dumps(jret, JSON_COMPACT)` **:246**, `json_decref` **:247**, `Json::parse(json_str)`
     **:250**. La requalification d'`E4.1l` ne valait **QUE POUR LES EVENTS** ; l'étendre à tout
     `E4.1n` était exactement le motif que R3 venait de restreindre, **réapparu deux paragraphes
     plus loin dans la même fiche**. **Ligne corrigée.** ⭐ *(La ligne `E4.1n` de `BOARD.md`, elle,
     portait DÉJÀ la bonne précision — les deux se contredisaient à 1 ligne d'écart : **quand une
     fiche corrige une portée, relire les lignes VOISINES qui répètent l'ancienne**.)*
  3. ⚠ **Bonus repéré en vérifiant le pont** : `RemoteUIWebSocketHandler.cpp:250` fait
     `Json::parse(json_str)` — **la forme qui LÈVE** (ni `nullptr, false`) — dans un **callback
     async** sous `uvw`. Même figure que la mine d'`E4.1o`, un cran plus bas. **À traiter par
     `E4.1n` en même temps que le pont**, pas séparément.
  4. ⚠ *Note d'outillage, sans rapport avec le ticket* : `tests/core/JsonApiModelWireBytes_test.cpp`
     est vu **binaire** par git (`Bin 0 -> 32389 bytes`) parce qu'il **contient un octet `NUL`**
     (mesuré ; par ailleurs **100 % ASCII et UTF-8 valide**). C'est **volontaire** — le fichier teste
     des octets bruts — mais ⚠ **`git diff` n'affichera jamais son contenu** : un futur relecteur
     doit le lire au fichier, pas au diff.

  ⭐ **→ [`E4.1n`](E4.1n.md) est DÉBLOQUÉE, et elle N'EST PAS DOCUMENTAIRE : c'est un ticket de
  CODE** (`buildJsonState/States/Query`, `decodeSetState`, **plus le pont
  `RemoteUIWebSocketHandler.cpp:236-252`**). 3ᵉ maillon de la chaîne sérialisée `l`→`s`.
  **`E4.1` passe à 13/17** (`a`→`m`).

- **✅ [`T3.50`](T3.50.md) MERGÉE — `544aa0f8`, 6 commits, `--ff-only`, historique linéaire.**
  ⭐ **`master` était IMMOBILE sur `bdc13081`** = exactement la base de la branche ⇒ **ni rebase ni
  conflit au merge** (les deux rebases et le conflit `tests/Makefile.am` de la fiche sont ceux de
  l'auteur). La **moitié LECTURE** du chemin retour Wago est typée : 2 `typedef`, 6 implémentations
  vivantes, 4 sites d'émission. `make check` **100/100** (`# TOTAL: 100 / PASS 99 / SKIP 1 /
  FAIL 0 / ERROR 0`, rc 0), **1640** cas sur **94 gtest + 6 scripts**, **0 `error:`**, un seul
  `Testsuite summary`, **145 goldens / arbre `d4ebc61f` inchangé** (vérifié APRÈS),
  `git status -uall` vide. **`F-FLAKY-1` non rencontrée, rien relancé.** ⛔ **Non poussé.**

  ⭐ **Ce que ce merge a ajouté, et qui sert aux suivants :**
  1. ⭐ **La 15ᵉ variante de faux vert a une sœur : « le balayage n'a trouvé AUCUNE unité ».**
     L'auteur avait été pris par « 0 avertissement parce que rien ne compilait » (`rc=1`), et son
     script exige désormais `rc=0`. ⚠️ **Le mien est tombé un cran plus tôt** : ma 1ʳᵉ extraction
     des commandes de compilation a rendu **0 commande**, donc **0 unité, 0 avertissement, 0
     échec** — *tous les gardes au vert, et le chiffre attendu était « 0 »*. Ce qui l'a arrêté est
     le seul compteur qui ne pouvait pas mentir : **`nb_units`**. ⭐ **Publier le CARDINAL DE
     L'ÉCHANTILLON à côté du résultat** — un instrument qui n'a rien mesuré et un instrument qui a
     mesuré zéro rendent le même chiffre. *(Cause : les lignes `V=1` d'automake se terminent par
     `&&\` et portent un `$depbase` non substituable — une regex ancrée sur `\.cpp$` ne matche
     rien.)*
  2. ⭐ **Une contre-mutation se réécrit APRÈS le correctif, sinon elle ne mute rien.** Ma première
     permutation cherchait `multiBits_cb(status, address, count, …)` — la forme d'AVANT le typage :
     **0 occurrence** sur l'arbre livré. Le garde `assert n > 0` a arrêté la chaîne au lieu de
     publier « 0 mutation survivante », **qui se serait lu comme un succès**. Réécrite contre la
     forme livrée (`WagoTypes::Address(address), WagoTypes::Count(count)`) : **4 sites, 4/4
     `rc=1`, 0 survivante**, témoin sain `rc=0`, restauration **1 fichier**, sha256 identique.
     ⭐ *Une campagne « 0 survivante » doit publier le nombre de sites MUTÉS.*
  3. ⚠️ **La seule correction de fiche du merge est une PORTÉE, pas un chiffre — et c'est la
     troisième de la nuit.** R1 tient (**12 avertissements uniques, 6 `address` + 6 `count`,
     `WagoIOBase.h:104:82` colonne 82 confirmée, témoin à 0, 0 unité en échec sur 14**, plus le
     bonus `WagoMap.cpp:172:126` `values`). Mais **« ZÉRO ligne `-Wshadow` » ne vaut que pour les
     six unités de l'auteur** : sur les 14, `WagoIOBase.h:104:82` **émet bien** un `-Wshadow`, dans
     **une seule** unité (`WIDigitalTriple.cpp`, la seule dont la base porte un membre `count`), et
     **c'est là, et là seulement, que `count` perd son `unused parameter`**. ⇒ **l'intuition de la
     1ʳᵉ rédaction était juste pour 1 unité sur 5**, la conclusion corrigée reste juste, et la
     phrase absolue était trop large. **Écrire la portée du balayage DANS la phrase qu'il porte** —
     exactement ce que R2 demandait pour « la paire *(address, count)* ».
  4. ⭐ **Les deux paires fichées sans numéro sont ATTRIBUÉES** : **[`T3.53`](T3.53.md)** = la paire
     DALI `(string command, string result)` — **types identiques ⇒ hors de portée de tout
     compilateur**, 5 décl. / 5 déf. / 4 `mem_fun`, **aucun test** ⇒ la plus exposée ;
     **[`T3.54`](T3.54.md)** = `setBufferBit(…, int bit, bool val)` — 1 déf. / 1 appel,
     **atténuée** par `packBits` (couvert par `tests/WagoBits_test.cpp`), ⚠️ **atténuation DÉDUITE
     et jamais mesurée**, ce qui en fait le premier livrable du ticket. **Comptes re-vérifiés au
     source par le merge, concordants. Aucun autre numéro ouvert.**
  5. ⭐ **Le raccourci de campagne de l'auteur est FONDÉ, et ça se vérifie en une commande** :
     l'arbre git `src/bin/calaos_server/IO/Wago` vaut **`758ee79c…`** sur `74d0c520`, sur
     `bdc13081` **et** sur le parent des deux commits de code ⇒ **le même objet**, la campagne
     mesurée avant les rebases porte donc sur les mêmes octets. *Vérifier l'identité d'objet plutôt
     que la liste des fichiers touchés : c'est plus court et ça ne se raisonne pas.*

- **✅ [`T3.40`](T3.40.md) MERGÉE — `81a7ff44`, 16 commits, `--ff-only` sur `74d0c520`, `master`
  immobile ⇒ **ni rebase ni conflit**, historique linéaire.** Le use-after-free généralisé est
  fermé : **16 sites sur 20**, **6 classes d'IO** atteignables par un rechargement de
  configuration. `make check` **99/99** (`# TOTAL: 99 / PASS 98 / SKIP 1 / FAIL 0`, le `SKIP` étant
  `run-python-tests.sh`), **1631** cas gtest, **0 `error:`**, un seul `Testsuite summary`, **145
  goldens / arbre `d4ebc61f` inchangé**, `git status -uall` vide. **`F-FLAKY-1` non rencontrée,
  rien relancé.** ⛔ **Non poussé.**

  ⭐ **Ce que ce merge a ajouté, et qui sert aux suivants :**
  1. ⭐ **Un mutant qui TUE LE BINAIRE peut être rendu ATTRIBUABLE.** La n° 5 de la liste canonique
     (*faux vert par mort du binaire*) se juge au **code de sortie** — mais le harnais peut aller
     plus loin **pour presque rien** : relever le cas resté **sans `OK` ni `FAILED`**, c'est-à-dire
     **celui qui tournait quand le binaire est mort**. Mesuré ici : **MU-G meurt sur
     `ProcessExitedDoesNotOutliveTheDeletedServer`**, **MU-E sur
     `ReadAtStartDoesNotOutliveTheDeletedIo`** — chacune sur l'oracle écrit pour elle. ⚠️ Sans ce
     relevé les deux ont la **même** signature (`exit 139`, rouge = `{censeur}`) et **ne se
     distinguent pas**. **À reprendre dans tout harnais de contre-mutation.**
  2. ⚠️ **Un résumé d'une ligne qui circule et qui est FAUX** : « les ensembles rouges ne sont plus
     disjoints, c'est leur **différence au censeur** qui l'est ». Vrai **uniquement pour
     MU-A…MU-D**. Pour **MU-E, MU-G et MU-J** cette différence est **∅** — elles ne sont séparées
     que par le **code de sortie**, puis par le cas mort. `T3.40.md` §17.5 l'écrit correctement ;
     c'est le raccourci qu'il ne faut pas propager.
  3. ⭐ **Le protocole de restauration de T3.40 est MEILLEUR que le nôtre et devient la
     référence** : à chaque tour, **compter les fichiers écrits** (jamais zéro), puis exiger que
     l'ensemble différant de la pristine soit **∅ avant** la mutation et **exactement `{le fichier
     muté}` après** — ce qui prouve d'un coup *appliquée*, *non débordée* et *rendue*. Vérifié une
     seconde fois, par un chemin indépendant, par un `git status -uall` **vide** en fin de
     campagne. (Rappel n° 12 : **jamais de `git checkout` de restauration dans le conteneur**, le
     `.git` d'un worktree est un *fichier* et la restauration échoue **en silence**.)
  4. ⭐ **La liste canonique passe à TREIZE** (n° 12 *restauration muette*, n° 13 *borne de sûreté
     devenue la contrainte active*). ⚠️ La **13ᵉ ne se voit QUE dans les DURÉES par cas** : valider
     un `make check` sur les seuls verts et le code de sortie est désormais **insuffisant**. Ici :
     suite **7 118 ms** (`make check`) et **7 104 ms** (témoin M0) contre les **1 042 ms** du faux
     vert. **Renvoi `DECISIONS.md` corrigé ONZE → TREIZE au passage.**
  5. ⭐ **Le lien `T3.40` ↔ [`T3.49`](T3.49.md) est désormais ÉCRIT DANS `T3.49.md` (§4.1)**, où il
     manquait — il n'existait que dans la ligne `BOARD`. `pumpLoopFor()` existe en **trois copies**
     (`ShutterImpulse_test.cpp:113`, `Timer_test.cpp:73`, `IoLifetimeTimer_test.cpp:161`) ; le faux
     **vert** de T3.40 et le faux **rouge** de T3.49 sont **le même défaut aux deux bouts du même
     axe**. Remède transposable versé : **l'assertion retournée en BORNE INFÉRIEURE**
     (`pumpUntil` + `EXPECT_GE`) — **troisième voie**, ni horloge injectable ni marge élargie, qui
     **conserve** la discrimination fermée par `T3.34`. ⚠️ Elle ne couvre que l'arrêt **trop tôt**
     (ce qui suffit aux six sites de T3.49) ; une borne **supérieure** redevient sensible à
     l'horloge. **Celui qui prendra `T3.49` doit lire §4.1 avant §4.**

- **⭐⭐ CINQ ARBITRAGES UTILISATEUR DU 2026-08-25 — PORTÉS DANS `DECISIONS.md`, LISEZ-LES AVANT
  DE JUGER QUOI QUE CE SOIT.** Quatre d'entre eux **retirent** ou **requalifient** une analyse
  déjà écrite : ce ne sont pas des ajouts, ce sont des **corrections**. Lot de **rédaction pure** —
  aucune ligne de `src/`, aucun test, aucun build.

  1. ⭐ **[`T3.26`](T3.26.md) REQUALIFIÉE — le downgrade OTA n'est PAS un défaut, c'est le
     comportement VOULU.** *« l'appareil installe seul, sans confirmation utilisateur, on peut
     vouloir un downgrade c'est accepté […] `calaos_server` fait juste son travail pour pousser la
     version qu'il a »*. Le serveur est un **distributeur, pas un arbitre** ; le fichier déposé
     dans `/usr/share/calaos/firmwares/<hardware_id>/` **est** l'opt-in. ⇒ options **A**
     (strictement supérieur — **casse l'usage**) et **B** (clé de manifeste — **négociation de
     format pour un besoin inexistant**) **écartées** ; **C** retenue en **confort de diagnostic**,
     forme **minimale** (nommer les deux versions, dire « change », **pas** de comparateur
     sémantique — il reviendrait par la porte du journal). ⛔ **La suite `OtaVersionOrder_test` est
     annulée**, aucune entrée `RELEASE_NOTES`. **Priorité ⬇️ dernière du lot des dix.**
     ⚠️ **Un ticket futur qui « corrigerait » `OtaFirmwareManager.cpp:249-253` en comparaison
     relationnelle RÉGRESSE le produit** — c'est précisément ce que la fiche s'apprêtait à faire.

  2. ⭐ **PROTECTION DE `master` : RECOMMANDATION RETIRÉE — et c'est une information
     D'ARCHITECTURE que plusieurs agents ont ignorée.** *« Pourquoi proteger master? […] On a 2
     canaux: push sur master fera des prerelease que seul moi ou les gens qui dev utilisent, et si
     je fait une release quand le code est clean, ca pousse pour tout le monde. »* ⇒ **`master` est
     le canal de PRÉVERSION**, les **releases** sont ce qui atteint les utilisateurs. La protéger
     **empêcherait son usage prévu** ; l'absence de *required status check* n'est **pas un trou**.
     **Requalification de l'épisode du sidecar MCP mort** : l'image `:dev` cassée était **une
     préversion cassée sur le canal fait pour ça**, **pas** un artefact livré ⇒ **la gravité
     tombe** ; [`T3.23`](T3.23.md) reste **entièrement juste**, seule sa **justification** change.
     **Barré (bloc ⛔ CORRECTION, jamais réécrit en silence) en trois endroits** : ce fichier
     (bloc T3.23, deux occurrences) et `T3.23.md` §4. La phrase « la fenêtre qui a laissé publier
     une image cassée reste ouverte » est **périmée** : il n'y a pas de fenêtre, il y a un canal.
     Le garde-fou utile existe déjà — `DECISIONS.md`, « ⚠️ Pousser master publie des artefacts ».

  3. ⭐ **`listen_address` ÉCARTÉ, remplacé par [`T3.39`](T3.39.md) — confiance
     `X-Forwarded-For` conditionnée au PAIR TCP LOOPBACK.** L'utilisateur avait d'abord accepté
     `listen_address` ; **l'instruction l'a tué**, et deux fois plutôt qu'une :
     **(1)** la clé bind **AUSSI le serveur UDP** (`UDPServer.cpp:58-61`, ligne identique à
     `HttpServer.cpp:29-31`) — c'est-à-dire **la découverte** (`CALAOS_DISCOVER` → `CALAOS_IP <ip>`)
     **et** les trames **`WAGO INT`/`WAGO KNX`** (`:88-118`), un chemin d'IO vivant ;
     **(2)** ⭐ **la question « qui se connecte directement au 5454 ? » a une réponse : les clients
     LÉGITIMES** — firmware **RemoteUI** (`calaos_remote_ui/main/calaos_protocol.h:28`,
     `provisioning_requester.cpp:161` : découverte UDP **puis** `http://<ip>:5454/api/v3/provision/request`
     et WS 5454, en clair), **app mobile en LAN** (`calaos_mobile/src/CalaosConnection.cpp:307-308`),
     **auto-détection de l'installeur** (`dialogautodetect.cpp:74`). Seul le sidecar MCP est
     loopback. ⇒ **le port ouvert n'est pas le défaut : c'est le produit.**
     **Le vrai correctif** : lire l'en-tête **seulement si le pair TCP est `127.0.0.1`/`::1`** —
     la « liste de proxys de confiance » absente de tout l'arbre, **réduite à une entrée**.
     ⭐ **Le pair est DÉJÀ un argument de `TransportLimits::effectiveClientIp()`** (fonction pure,
     `HttpClient.h:144-158`) ⇒ garde de 3 lignes, pureté et testabilité intactes, et **les deux**
     appelants servis d'un coup — **instruit : le cap `max_connections_per_ip` veut la MÊME
     sémantique que le throttle**, dans les deux déploiements.
     ⚠️ **Deux pièges chiffrés dans la fiche** : `ForwardedForLine.LastRepeatedHeaderLineWins` et
     `…ClientSuppliedListIsDiscardedWholesale` sèment un pair **`10.0.0.254`** ⇒ **FAUX ROUGES**
     à re-semer sur `127.0.0.1` ; et les **7 cas** de `core/JsonApiThrottleIdentity_test` tournent
     sur un handle **jamais connecté** ⇒ pair `"unknown"` ⇒ **tous rouges**, il faut un **joint de
     test**, ⛔ **pas** élargir la garde à `"unknown"`.
     `FINDINGS.md`/F-XFF-1 et `T3.24.md` **mis à jour** (bloc ⛔ CORRECTION, ancien texte barré).

  4. ⭐ **[`I4.1`](I4.1.md) EST VALIDÉ SANS RÉSERVE SUR UNE CONFIGURATION DE PRODUCTION RÉELLE, et
     [`I4.2`](I4.2.md) est ✅ CLOSE SANS AUCUN DÉFAUT.** Ouverture + sauvegarde **par le GUI** d'une
     vraie maison : **125 règles / 177 conditions / 318 actions**, `io.xml` **identique OCTET POUR
     OCTET**, **multiensemble global des lignes de `rules.xml` IDENTIQUE** ⇒ **aucune perte**.
     C'était exactement le « le chemin GUI n'a jamais été exercé » du §4 : **comblé**.
     **Seul écart : 12 lignes**, toutes des `<calaos:input>` dans les **deux** conditions `script`.
     - ⛔ **La prémisse d'ouverture d'`I4.2` — « c'est le jumeau installeur du défaut corrigé côté
       serveur » — était FAUSSE**, et elle est **barrée au §0bis de la fiche** (formulation
       d'origine conservée, pas de réécriture silencieuse). `Calaos/Condition.h:49` est un
       **`std::vector<IOBase*>`**, la lecture est en ordre de document (QDom,
       `projectmanager.cpp:1184-1206`) et l'écriture est **indexée** (`:384-392`) ⇒
       **ordre-préservant par construction**. Il n'y a **pas de jumeau**, et il n'y en a jamais eu.
     - ⭐ **La forme de la permutation avait désigné le coupable AVANT le fait décisif** : un
       **quasi-renversement** (exact sur la liste de 2, à une transposition adjacente près sur celle
       de 10) est la **signature du `_Hashtable` de libstdc++** — insertion en tête, collision de
       seau — **pas** celle d'un brassage. C'est la seule empreinte qui sépare « vecteur mal
       ordonné » de « conteneur non ordonné ».
     - ✅ **LE FAIT DÉCISIF, obtenu de l'utilisateur** : la configuration a été **chargée depuis le
       SERVEUR EN MARCHE**, pas depuis un fichier local. Ce serveur est **antérieur à `E4.2c`** :
       il a re-sérialisé les déclencheurs en **ordre de hash des pointeurs**
       (`unordered_map<IOBase*,IOBase*>`, donc ASLR) **avant de les envoyer**. Les deux fichiers
       comparés sont **deux sérialisations du serveur**, sous deux ASLR ; l'installeur n'a fait que
       **transporter**. ⇒ **le bug serveur capturé EN TRANSIT.**
     - ⭐ **RIEN À FAIRE, ET C'EST LA CONCLUSION** : `E4.2c` (`ff6c51c7`) est **déjà dans `master`**
       depuis le 2026-08-16 ⇒ **la prochaine version déployée arrête ces diffs**, sans aucune
       action. ⭐ **Dette de DÉPLOIEMENT, pas de code.** ⛔ Ne ticketez rien, et surtout n'ajoutez
       **pas** de tri dans l'installeur : ce serait **introduire** un réordonnancement permanent
       pour masquer un défaut situé ailleurs.

  5. ⭐ **`T3.36` (relink des suites) : PRIORITÉ HAUTE, mais PLANIFIÉE APRÈS `E4.1x`.**
     **47 suites sur 80** peuvent répondre **vert sans avoir relié le code modifié** — cause racine
     des cinq variantes de faux vert/rouge. **Après E4.1x** parce que la réparation touche
     `tests/Makefile.am`, où **huit tickets sérialisés (`E4.1l`→`E4.1s`) appendent chacun leur
     bloc** : la faire maintenant produirait un conflit à chaque merge, sur le fichier dont la
     résolution naïve **perd le `endif` extérieur** et casse `automake`. ⚠️ **Piège de comptage** :
     `CORE_TEST_LDADD` **contient** `CORE_SERVER_OBJECTS` ⇒ chercher `$(CALAOS_SERVER_BUILDDIR)` en
     toutes lettres donne **27** au lieu de 47. **Contrepartie assumée** : d'ici là la protection
     **repose sur la discipline** (chaque brief doit porter `rm -f` binaire **et** `.o` serveur,
     puis exiger `CXXLD` **et** le code de sortie) — ce n'est **pas** une garantie. **Risque de la
     réparation** : son premier `make check` **peut rougir**, et **ces rouges seront des
     trouvailles**.
     ⚠️ **`T3.36.md` et sa ligne de `BOARD.md` N'EXISTENT PAS ENCORE sur `master`** — la fiche vit
     dans le worktree `.wave48/t3.29` (lot `T3.35`→`T3.38`), **merge en cours**. ⛔ **Je ne l'ai
     donc ni créée ni éditée** ; l'agent qui mergera `T3.29` doit **porter « priorité haute,
     planifiée après `E4.1x` » sur la ligne `T3.36` du board** au moment du merge, faute de quoi
     quelqu'un la prendra pour une fiche en attente et la lancera au mauvais moment.

  - **Numéros attribués** : **`T3.39`** (T3.35→T3.38 étaient pris par le lot de `T3.29`, worktree
    `.wave48/t3.29`) et **`I4.2`** (dépôt `calaos_installer`).
  - ➡️ **PROCHAINE ACTION** : rien de ces cinq points n'est bloquant. Le chemin critique reste
    **`E4.1j`** (dernier de la vague 1), puis la chaîne sérialisée `E4.1l`→`E4.1s`, puis `E4.1x`
    — **et c'est seulement là que `T3.36` se lance**. Côté fixes, `T3.25` reste la première
    (atteignable à distance, débouche sur du matériel), `T3.34` **avant** elle (deux caractères,
    mêmes lignes). **`T3.39` est prêt à lancer et indépendant.** ✅ **Côté installeur, plus rien** :
    `I4.1` est validé sans réserve et `I4.2` est close sans défaut.

- **⭐ LES FINDINGS DE LA NUIT SONT TICKETÉS — dix fiches, `T3.25` → `T3.34`, toutes 📋
  (2026-08-25).** Aucune ligne de `src/`, aucun test, aucun build : ce lot est de la **rédaction**.
  Chaque finding d'origine renvoie désormais à sa fiche dans `FINDINGS.md`, l'analyse est
  conservée. Numéros : **T3.20 est parké ⛔, T3.21-T3.24 étaient pris**, T3.25 était le premier
  libre.

  - ⭐⭐ **LA PREMIÈRE À FAIRE EST `T3.25`, et de loin — parce qu'elle est la seule dont le chemin
    est ATTEIGNABLE À DISTANCE par un compte API ordinaire et débouche sur du matériel.**
    `Utils::from_string("")` rend **`true` sans rien écrire** (`StringUtils.h:104-111` — plus
    `Utils.h`, T2.2 l'a déplacée). **Il n'y a pas de « 0 en cas d'échec », il y a deux régimes** :
    `""` **et toute chaîne blanche** → `ret=1`, **dest inchangée** ; `"true"` → `ret=0`, dest 0 ;
    `"12abc"` → `ret=0`, dest **12**. Mesuré au `g++`, dest pré-semé à `0x5555` = **21845** — la
    valeur exacte des canaux DMX qui ont tué `calaos_ola`. **Et `is_of_type<int>("")` rend `true`
    aussi** (son `T tmp;` est lui-même non initialisé) ⇒ **cette garde ne garde pas**.
    **Balayage `python3`, `src/` : 319 sites, 310 ignorent le retour (97 %), 165 passent une
    locale sans initialiseur, 71 un membre sans initialiseur en-classe.** Un second balayage
    indépendant (revue parallèle) donne **320 / 157 / dont 112 sans garde `.empty()`** — les deux
    s'accordent à ~5 %, l'écart vient de la résolution des déclarations multiples `int a, b, c;`.
    ⭐ **La chaîne atteignable, maillon par maillon** : `set_state` n'a **AUCUN `scopeDenied`**
    (`JsonApiHandlerWS.cpp:170-231` ; les sept protégés sont `set_param`, `del_param`, `audio_db`,
    `set_timerange`, `eventlog`, `register_push`, `settings`) → `JsonApi.cpp:774/777` passe la
    **chaîne cliente brute** à `set_value(string)` → `{"value":"impulse up "}` atteint
    `IO/OutputShutter.cpp:110-116`, où `erase(0,11)` laisse `""`, `from_string` rend `true` sans
    écrire, et **`int v;` indéterminé part en durée d'impulsion**. Conséquence lue au source
    (`:216-227`) : sur une **grande** valeur, `impulse_action_time + impulse_time < time * 1000`
    est **faux** ⇒ **aucune minuterie d'arrêt n'est armée**, l'impulsion dégénère en **course
    complète du volet** ; sur une valeur négative, `_t` négatif et **débordement `int` possible**.
    Dans tous les cas la valeur arbitraire est **publiée dans l'état de l'IO** (`cmd_state` +
    `updateCache()`). Même famille : `OutputShutterSmart` ×7, `OutputLightDimmer` ×8 (2 clampés
    `[0,100]`, 6 non), `OutputLightRGB` ×10, `OutputLight`, `IntValue`, `JsonApi.cpp:1964/2057`.
    ⭐ **Le défaut OLA était le seul cas INATTEIGNABLE de la famille.**
    **Correctif recommandé, pas imposé** : retour honnête (`!iss.fail() && iss.eof()`, **1 ligne**,
    **9** sites à auditer) **+** initialisation des destinataires **par lots**, lot **L1** =
    le chemin ci-dessus, livré seul et en premier. ⛔ **PAS** un « 0 forcé » : il fermerait 310
    sites d'un coup **et casserait 38 appelants nommables** dont le destinataire porte un défaut
    utile (`port = 1883`, `keepalive = 120`, `interval = 15000`, `brightness = 100`,
    `perPage = 100`, `eis = EIS_Autodetect`, huit `step = 1.0`, trois `a = 1.0`).

  - ⛔ **TROIS FINDINGS DE LA NUIT SONT INEXACTS AU SOURCE — corrigés en place dans `FINDINGS.md`,
    ne pas les réécrire.** ⚠️ **Le n°3 ci-dessous a lui-même été corrigé** : ma conclusion était
    fausse, seule l'observation tenait. Lire la puce entière avant d'agir.
    **(1)** « `from_string("")` retourne true **avec dest zéro-initialisée** » (:615-617) : `dest`
    **n'est pas écrite du tout**, et le site a déménagé en `StringUtils.h`.
    **(2)** « `RoonPlayer` : `from_string("")` laisse `port` à **0** » (:2458) : il est
    **INDÉTERMINÉ** (`RoonPlayer.h:214`, `int port;` sans initialiseur, hors liste d'init). Et
    **« il est probable que Roon soit inutilisable » est trop fort** : avec `host` vide — le mode
    par défaut annoncé — `args` reste **vide**, aucun `--port` n'est passé, **l'autodétection
    fonctionne**. Seule la configuration à **hôte statique** est cassée. Les lignes citées ont
    dérivé de **+6** (bloc de commentaire E4.1g à `:159-164`) : c'est `:180` et `:185`.
    **(3)** ⚠️ **CE POINT ÉTAIT LUI-MÊME FAUX ET IL EST CORRIGÉ (2026-08-25).** J'avais écrit
    que `LmsHost{}` / `LightState` / `RedChannel` « n'existent nulle part ». **Le balayage était
    juste, la conclusion fausse : il portait sur `master`.** Ces types vivent sur **deux branches
    livrées et revues, NON MERGÉES** : `refactor/e4.1d` (`689e26b0`) — `LmsHost`
    (`Audio/SqueezeboxWire.h:188-191`) et `LightState` (`IO/Hue/HueWire.h:107-114`) — et
    `refactor/e4.1f` (`4e238f2c`) — `DmxChannel`/`DmxLevel`/`DimmerPercent` +
    `RedChannel`/`GreenChannel`/`BlueChannel` (`IO/OLA/OLAWire.h:91-124`), sites d'appel
    `OLACtrl.cpp:61-62` et `:81-84`. La permutation y **ne compile pas**
    (`could not convert 'OLAWire::DimmerPercent(value)' … to 'OLAWire::DmxChannel'`).
    ⇒ **le compte de récidives est CINQ : 3 ouvertes sur master (E4.1g/h/i), 2 fermées sur
    branches en attente (E4.1d, E4.1f).** Sur master, seul précédent : `enum class
    RuleDetachPolicy` (T3.18).
    ⭐ **Et l'apport qui reste valable, devenu le cœur de `T3.31`** : `CameraRegistration`
    (`ReolinkEventRegistry.h:52-58`) **existe et n'a rien fermé** — `ReolinkCtrl.cpp:100` le
    construit en **brace-init positionnel**. C'est **le même défaut** que le relecteur d'E4.1d a
    trouvé sur `HueWire::LightState` (agrégat de 3 `int` + 2 `bool`, `-fsyntax-only`, permute
    `sat`/`bri` en silence) — **latent** là-bas, **réalisé** ici ⇒ **motif récurrent, pas
    accident**. ⭐ **Règle de série : un type nommé ne ferme rien s'il reste un agrégat
    initialisable positionnellement.** La forme qui ferme est celle d'`OLAWire.h` —
    **constructeur `explicit`**, donc pas d'agrégat ; les désignateurs C++20 rendent la
    permutation *visible*, pas *impossible*.
    ⚠️ **Leçon d'outillage générale : toujours dire sur quelle référence on a mesuré.** Un
    balayage `master` ne voit pas les branches livrées.
    ⛔ **Résiduel n°1** (emballer la mauvaise variable : `RedChannel(channel_blue)`,
    `buildCoverUrl(aurl, LmsHost{aurl})`) : **limite INTRINSÈQUE**, verdict du relecteur d'E4.1f —
    **à déclarer, jamais à promettre**.
    ⚠️ **Un quatrième, venu d'un balayage parallèle et infirmé ici** : les « accès `tokens[1]`
    hors bornes » de `KNXExternProc_main.cpp` **n'existent pas** — `Utils::split` **pade**
    (`StringUtils.cpp:210`, `while (tokens.size() < max) push_back("")`). Le vrai défaut est que
    le remplissage `""` laisse `b`/`c` **indéterminés** ⇒ **adresse de groupe arbitraire sur le
    bus KNX**, sans erreur.

  - ⭐ **UN DÉFAUT NEUF, TROUVÉ EN VÉRIFIANT — `T3.34`, deux caractères, à livrer AVANT `T3.25` L1**
    (mêmes lignes). `IO/OutputShutter.cpp:117-123` fait `compare(0, **13**, "impulse down ")` puis
    **`erase(0, 11)`** : il reste `"n "` collé devant la valeur, `from_string("n 500")` échoue,
    **`ImpulseDown(0)` est appelé quelle que soit la durée demandée**. **`impulse down` n'a jamais
    fonctionné.** Idem `IO/OutputShutterSmart.cpp:171`. La branche `impulse up ` juste au-dessus
    est correcte ; la faute vient de la longueur **écrite deux fois** sans rien qui les lie, alors
    que `Utils::strStartsWith()` est utilisé **douze lignes plus bas** (`:124`). La trace est
    visible dans l'API **depuis toujours** : `cmd_state = "impulse down 0"` (`:169`).

  - **L'ORDRE RECOMMANDÉ** : **`T3.34`** (2 caractères, zéro risque, mêmes lignes que T3.25 L1) →
    **`T3.25`** étage 1 + lot L1 → **`T3.29`** (visible par tous les utilisateurs, sans décision) →
    **`T3.28`** → **`T3.26`** (⚠️ **bloqué sur décision utilisateur**, 3 options pesées, plus une
    question ouverte : l'appareil installe-t-il seul ? c'est un **autre dépôt**) → **`T3.33`**
    (après T3.25) → **`T3.32`** → **`T3.31`** → **`T3.30`** (piège armé, aucun appelant) →
    **`T3.27`** ⚠️ **APRÈS `E4.1j`**, dont `ScriptBindings.cpp` est le périmètre exclusif et qui
    est **en cours en ce moment**.

  - ⚠️ **CROISEMENTS DE PÉRIMÈTRE À RESPECTER** : `T3.27` et le site `ScriptBindings.cpp:230` de
    `T3.25` attendent **E4.1j** · `T3.33` touche `WagoExternProc_main.cpp` et
    `OLAExternProc_main.cpp`, **3 des 6 casseurs de la liste d'`E4.1x`** ⇒ ne pas l'ouvrir
    pendant une fenêtre E4.1x · `T3.29` touche `IO/Mqtt/MqttCtrl.cpp` et les 9 catalogues `po/`.

  - **NON VÉRIFIÉ, à ne pas surestimer** : **rien n'a été construit** (trois agents buildaient en
    parallèle), **rien sous ASan**, **aucun volet, aucun automate, aucun core Roon réel**. La
    valeur exacte prise par `v` dans le chemin `impulse up ` **n'a pas été mesurée sur machine** :
    c'est le code qui la consomme qui a été lu. Le seul programme exécuté est un `g++` autonome de
    12 lignes sur `from_string`/`is_of_type`.

- **👀 E4.1m LIVRÉE, branche `refactor/e4.1m`, `7667838f` + 7 commits — NON MERGÉE, RIEN POUSSÉ**
  (2026-08-25, puis ⭐ **passe de correction des trois réserves de revue le 2026-08-26**).
  ⭐⭐ **E4.1n est débloquée.**

  - ⭐ **DEUX REBASES DE PLUS — `master` a bougé DEUX FOIS pendant la passe** :
    `74d0c520` → **`bdc13081`** (T3.40) → **`7667838f`** (T3.50). **Base livrée : `7667838f`**, et
    ⚠️ **tous les chiffres ont été REMESURÉS sur elle** — recopier ceux de la base intermédiaire
    aurait publié un faux sans qu'aucun test ne rougisse (la leçon de T3.31, rejouée).
    **Mêmes 2 conflits aux deux passes**, aucun dans `src/`, et **aucun des 6 fichiers `src/` du
    périmètre n'est touché par `master`** (T3.40 : `Timer.h`/`IOBase.h`/`ExternProc.*`/… ;
    T3.50 : l'arbre `IO/Wago/`). `tests/Makefile.am` **reconstruit** — les deux côtés prouvés
    **appends purs** en `python3` (`startswith(base)` vrai des deux ; sur `7667838f` :
    +2892 o / 43 lignes côté `master`, +3597 o / 51 lignes côté branche), résultat = base + queue
    de `master` + ma queue, avec le contrôle qui ferme : *le fichier de `master` est un **préfixe
    strict** du résultat* (**+3597 octets exactement**). **86 `if` / 86 `endif`** (master : 85/85),
    **profondeur finale 0, minimum 0**, **0 marqueur de conflit en début de ligne**.
    `FINDINGS.md` : **les deux côtés gardés**, zéro contexte partagé dupliqué (la branche est un
    **append pur**, `cp(base, theirs) == len(base)` vérifié). ⚠️ **La section `T3.40` de `master`
    cite des marqueurs de conflit EN PROSE** — la recherche doit être **ancrée en début de
    ligne**, sinon faux positif.

  - ⛔⭐ **R1 — le masquage des mots de passe ne tenait à RIEN, et c'est fermé.** La mutation `X2`
    du relecteur (retirer `Utils::str_to_lower()` du test de clé de `dumpJsonRedacted()`) laissait
    le `make check` **COMPLET au vert** ⇒ `"CN_PASS"`, `"Password"`, `"Authorization"` cessaient
    d'être masqués **en silence**. **Préexistant**, mais sur exactement la fonction que l'incident
    `M3` avait mutée. Fermé par `JsonApiRedact.HidesCredentialFieldsWhateverTheKeyCase`
    (`"CN_Pass"` **et** `"Authorization"`, **assertions appariées** : le secret **absent** *et* la
    paire **encore là, masquée**). ⭐ **`X2` rejouée sur la base finale : elle rougit**, et
    **chirurgicalement** — `# TOTAL: 101 / PASS: 99 / FAIL: 1`, rouge =
    **`core/JsonApiHardening_test` seul** ; le seul cas neuf tombe, sur ses 4 assertions
    appariées, et les 5 autres cas de `JsonApiRedact` restent verts.

  - ⛔⭐ **R2 — une mine posée SIX TICKETS à l'avance, désamorcée.** Mon acceptation n° 6 promettait
    qu'`E4.1o` casserait `ScriptExec.cpp:131` **à la compilation**. **FAUX, mesuré** :
    `JSON_USE_IMPLICIT_CONVERSIONS` vaut **1** (`src/lib/json.hpp:2813`, jamais surchargé, zéro
    `-D`) ⇒ **`!Json` compile sans un avertissement** et **lève `type_error.302` à l'exécution**,
    pour les **trois** formes de retour, **dans le callback de lecture d'un `ExternProc`** où
    **rien n'attrape** (0 `try`/`catch` dans `ScriptExec.cpp`, `ExternProc.cpp`, `ExternProc.h`)
    ⇒ **`std::terminate` de `calaos_server`** sur le chemin **nominal** d'un `set_param` de script.
    ⭐ **C'est le précédent KNX** (`FINDINGS.md § E4.1e`). Réécrit dans **`E4.1m.md`**,
    **`E4.1o.md`** et **`FINDINGS.md`** ; `BOARD.md` porte la ligne courte sur `E4.1o`.
    ⇒ **Leçon générale** : *« ça ne compilera pas »* est une **prédiction**, pas une mesure — sur
    `nlohmann::json` avec les conversions implicites actives, **presque tout compile**.

  - ⚠️⭐ **R3 — une phrase plus généreuse que la mesure, restreinte.** « Zéro octet ne bouge sur le
    journal » devient « **aucun octet ne bouge du fait de ce choix-là** », et les **trois familles
    qui bougent quand même** sont nommées et reproduites (ordre des clés · rendu des nombres ·
    UTF-8 invalide, cette dernière **non atteignable par les deux appelants de production**).
    L'exception `ensure_ascii = false` est **saine et gardée**.

  - **Validation finale, chiffres recomptés sur la base `7667838f`** (`make distclean` +
    `autogen` + `configure` + `make -j16` + `make check -j8`, **un seul build à la fois**, attendu
    par `docker wait`) : **rc 0**, `# TOTAL: 101` **= 95 binaires gtest + 6 tests de script**
    (master **100**), `PASS: 100`, `SKIP: 1`, `FAIL: 0`, `ERROR: 0`, **UN seul
    `Testsuite summary`**, `CXXLD` à la regex **ancrée** : **107** au distclean, **96** aux deux
    exécutions de campagne, **1658 cas gtest** comptés **deux fois et concordants**
    (1657 `[ OK ]` + 1 `SKIPPED` à l'exécution, **aucun binaire à 0 cas**), **88,7 s** de temps
    mural cumulé (87,7 s à la clôture), **145 goldens intacts** (`d4ebc61f`, vérifié **après**).
    ⚠️ ⭐ **`F-FLAKY-1` VU une fois et NON RELANCÉ** — la fiche disait « pas vu du tout »,
    **corrigé** : au distclean de la base intermédiaire `bdc13081`,
    `core/ShutterImpulse_test:297`, sonde 91 ms contre échéance 147 ms ⇒ marge **56 ms**, la plus
    serrée des 6 sites ; identifié par **fichier ET forme**, **sans relance dédiée** ; les
    5 exécutions suivantes ne l'ont pas revu, et l'exécution rouge reste rapportée.

  - ⚠️ **Le rebase, et les trois conflits — aucun dans `src/`.** `tests/Makefile.am` résolu par
    **RECONSTRUCTION** et non par « garder les deux côtés » : version de `master` prise **telle
    quelle**, mon bloc `if HAVE_GTEST … endif` (**autonome, 1 `if` / 1 `endif`**) ré-ajouté **en
    queue**, et la propriété *« la base est un PRÉFIXE STRICT du résultat »* **prouvée en
    `python3`** avant validation. Contrôles : master **98 `TESTS`, 83/83** ; branche **99, 84/84**,
    **profondeur jamais négative**, **tous préfixes `^if*`** (il n'y a que `if` dans ce fichier).
    `FINDINGS.md` : **les deux côtés gardés**. `RELEASE_NOTES.md` : ma phrase de clôture remplace
    l'ancienne, la section « Empaquetage » de `master` est **conservée intégralement**.

  - ⛔⭐ **UN DÉFAUT DE LIVRABLE QU'AUCUN TEST NE POUVAIT VOIR, trouvé et corrigé.** Le commit de
    documentation avait été fait avec **`git add -A` pendant que la campagne tournait** : il a
    ramassé la mutation `M3` en vol et **livré dans `src/` un affaiblissement de
    `dumpJsonRedacted()`** (`"cn_pass"` → `"cn_user"`, c'est-à-dire le mot de passe qui cesse
    d'être masqué dans les journaux). ⚠️ **Rien dans la discipline de test ne pouvait l'attraper** :
    la campagne restaure depuis la pristine à chaque itération, donc **l'arbre construit était
    toujours correct** — `make check` vert, contre-mutations rouges, témoin vide, tout était
    compatible avec ce commit. Ce qui l'a attrapé est la comparaison **pristine ↔ `HEAD`**
    (`pristine == worktree` mais `pristine != HEAD`). Corrigé par `--amend`. ⇒ **`git add -A` est
    interdit tant qu'un harnais peut écrire dans l'arbre**, et `git diff-tree … -- src` doit être
    passé sur **chaque** commit qui n'est pas censé toucher `src/`. Versé à `FINDINGS.md`. Les 9 constructeurs du **modèle** de `JsonApi`
  rendent un `Json` ; **`LuaScript/ScriptExec.cpp` est migré INTÉGRALEMENT**, lecture comprise, et
  **l'adaptateur `jansson_from_json()` d'E4.1l disparaît** avec ses 4 cas. **E4.1 passe à 13/17.**

  - ⭐ **Le retrait de l'adaptateur ne coûte pas 4 lignes, il en RETIRE** : `ScriptWire.h`, écrit et
    couvert par **E4.1j** (32 cas) précisément pour ce moment, fournit `dumpJson`, `parseMessage`,
    `stringGet` et `decodeObject`. ⚠️ `Params::fromNJson()` n'est **pas** un substitut de
    `decodeObject()` — il lève `type_error.302` sur toute valeur non-chaîne, et `ScriptWire.h` le
    disait déjà.

  - ⭐⭐ **LA LEÇON D'OUTILLAGE DU TICKET, et elle dépasse E4.1** : `make -j12 && make check` après
    une modification confinée à `src/bin/calaos_server/*.cpp` **ne relie AUCUN binaire de test**.
    Mesuré sans ambiguïté : 14 `fprintf` compilés dans `JsonApi.o` (`grep -c` = 9 dans le seul
    `JsonApi.cpp`), **0 occurrence dans le binaire**, **0 marqueur dans les 92 `.log`**,
    **1 seule ligne `CXXLD`**, et un `# PASS: 96` qui décrit **le code d'avant**.
    ⇒ **Le compte de lignes `CXXLD` est l'oracle, pas le `# PASS`.** Remède : `make distclean`, ou
    `find tests -type f -name '*_test' -perm -u+x -delete` puis `make check`, et vérifier que
    `CXXLD` égale le nombre de binaires. C'est la variante **n° 2** de la liste canonique, à une
    échelle que personne n'avait décrite. **Ce ticket n'a conclu qu'après un relink des 92.**
    ⚠️ **Ce qui a sauvé les tickets précédents** : `libcalaos_common.la` est dans le `LDADD` de
    tous, donc dès qu'un ticket touche `src/lib/` tout se relie (93 relinks mesurés).

  - ⭐ **`F-LINK-1`, 10ᵉ mesure.** Marqueur sur les **14** sites, `make check` complet **avec
    relink forcé** : **10 atteints par 20 suites, dont 19 PRÉEXISTANTES** (`dumpJsonRedacted`
    **748** passages), **4 jamais atteints et cohérents entre eux** — `buildFlatIOList` et les
    **3** sites de `ScriptExec.cpp`, dont le seul appelant est un lambda branché **après le spawn
    d'un vrai `calaos_script`**. ⇒ **gardés par le compilateur seul, déclaré ; aucune « réplique »
    du site d'appel écrite** (elle n'aurait testé que la réplique).

  - ⭐ **Cinq deltas d'octets, PAS SIX.** Sonde compilée contre les deux bibliothèques sur la chaîne
    de ce ticket : **566 sondes, 315 DIFF / 251 SAME**, dont **les 256 octets en VALEUR et les 256
    en NOM de champ**. Les **quatre catégories qu'E4.1l laissait « non balayées » sont refermées et
    NÉGATIVES** : chaînes longues, doublons de clés, profondeur (jusqu'à 1024), et ⭐ **tri sur
    clés non ASCII — l'ORDRE est IDENTIQUE**, les deux bibliothèques ordonnent sur les octets.

  - ⭐ **Deux conséquences nommées du delta 3, plus lourdes que leur cause** : (a) un **équipement
    pouvait PERDRE SON NOM** dans `get_home`/`get_io` — `buildJsonIO()` ne testait ni le retour de
    `json_string()` ni celui de `json_object_set_new()` ⇒ paire supprimée en silence, appareil
    indistinguable d'un appareil sans nom ; le cas de `core/JsonApiSession_test` qui épinglait ce
    défaut est **RETOURNÉ** ; (b) un **script Lua contenant un octet mal encodé ne partait pas du
    tout** — `ScriptWire.h` (E4.1j) l'avait **prédit** : « ce canal s'OUVRE avec E4.1m ». Il
    s'ouvre. **Changement de comportement, déclaré en `RELEASE_NOTES.md`.**

  - ⛔⭐ **UNE PRÉDICTION DE MOI, DÉMENTIE PAR LA MESURE — la 11ᵉ affirmation renversée.** J'avais
    annoncé `M8b` (échange de `playlist` et `database` dans `buildJsonAudio`) **inerte**, parce que
    le seul lecteur de la maison de référence répond `false` aux deux capacités. Chaque maillon du
    raisonnement est vrai, la conclusion est fausse : `M8b` rend **1 rouge**, car
    `core/JsonApiInputGuards_test` (**T3.19**) **ajoute ses propres lecteurs**, dont un porte
    `database="true"` / `playlist="false"` — **et son commentaire dit qu'il est là exactement pour
    qu'un échange de ces deux clés ne passe pas inaperçu**. C'est `F-LINK-1` transposé de
    l'atteignabilité vers l'**oracle** : *raisonner sur UNE fixture quand plusieurs suites en
    construisent d'autres.* ⭐ **Ce qui a sauvé la fiche : la mutation prédite inerte a été JOUÉE
    au lieu d'être sautée.** ⇒ **règle : jamais sauter une contre-mutation qu'on croit inerte.**

  - ⭐ **Une VRAIE inertie, mesurée et expliquée** : `M6` — échanger deux identifiants demandés dans
    `buildJsonGetIO()` — rend un ensemble **VIDE**. Ce n'est pas un trou : **c'est ce ticket qui a
    supprimé l'information**. La réponse de `get_io` est un objet indexé par id ; sous jansson elle
    gardait l'ordre de la **requête**, sous `nlohmann` elle sort **triée**, donc l'ordre demandé
    n'est plus observable **par personne**. Déclaré, comme `M8` l'avait été par E4.1l.

  - ⚠️ **Une exception assumée à l'invariant 3, et elle est FALSIFIABLE** : `dumpJsonRedacted()`
    dumpe en `ensure_ascii = FALSE`. C'est un **journal**, pas un fil, et il n'a **jamais** été
    ASCII (`JSON_INDENT(4)` seul). **Mesuré** : à `ensure_ascii = false` les deux dumps sont
    identiques à l'octet sur ASCII, `U+00E9` **et `U+007F`** ⇒ **zéro octet ne bouge**.
    `error_handler_t::replace` est appliqué. Les deux choix sont épinglés par deux cas neufs de
    `core/JsonApiHardening_test` : retourner le `false` en `true` rougit.

  - ⚠️ **`E4.1n` n'est PAS un ticket documentaire.** La requalification d'E4.1l est vraie **des
    events seulement** ; le pont jansson↔nlohmann de `RemoteUIWebSocketHandler.cpp:236-252` est
    **toujours là**, vérifié au source, et `buildJsonState/States/Query` + `decodeSetState`
    l'attendent. La ligne `BOARD.md` a été **restreinte** en conséquence.

  - **Le découpage `m` → `s` TIENT.** Le seam décrit par les fiches existe et fonctionne : migrer
    un constructeur = changer son type de retour et laisser ses appelants tomber sur la surcharge
    `Json` de `sendJson()`. **Aucun adaptateur écrit, aucune dépendance croisée découverte.**

  - **Contrôles, tous rejoués APRÈS le rebase** : **145 goldens intacts** (arbre `d4ebc61f`,
    `git status` vide sur le répertoire), `make check` **en DISTCLEAN complet** —
    **`# TOTAL: 99` / `# PASS: 98` / `# SKIP: 1` / `# FAIL: 0` / `# ERROR: 0`**, **un seul
    `Testsuite summary`**, **105 lignes `CXXLD`** —, **1632 cas gtest** sur **93** binaires
    comptés **trois fois** et concordants. Filet neuf **rejoué contre l'arbre jansson de
    `74d0c520`** (sources de `master` réécrites par contenu, `.o` et binaire supprimés, ligne
    `CXXLD` exigée) : **19 cas, 13 ROUGES, 6 VERTS — exactement les six témoins `W_`**.
    Le commit de caractérisation touche **0 fichier de `src/`** (`git diff-tree`).
    Marqueur **rejoué après rebase et identique à l'unité près** : 10 sites atteints, 20 suites,
    4 zéros. ⚠️ **`F-FLAKY-1` n'a mordu sur AUCUNE campagne de ce ticket.**

  - **NON VÉRIFIÉ, à ne pas surestimer** : rien sous ASan, aucun bout-à-bout avec un vrai
    `calaos_script`, aucun client tiers, et le changement de comportement du script Lua est mesuré
    **au niveau du message construit**, pas sur un script réellement exécuté.

- **🔒 E4.1l ✅ MERGÉ (`f36ff138`, **6** commits, `git rebase --onto 44657407 df2851d0` +
  `merge --ff-only`, historique linéaire, **0 commit de fusion**)** — ⭐⭐ **E4.1m EST DÉBLOQUÉE** :
  c'était le **premier de la chaîne sérialisée `l`→`s`**, et **sept tickets héritent de ce qui passe
  ici**. `CalaosEvent::toJson()` rend un `Json` ; ses **trois** wires sérialisants suivent (push WS,
  `processPolling()`, **la ligne d'historique** `EventManager.cpp:79`) ; adaptateur transitoire
  `jansson_from_json()` à **un seul appelant**, que `E4.1m` supprime. **E4.1 passe à 12/17.**
  **RIEN POUSSÉ.**

  - ⭐ **LE PARCOURS EST LA LEÇON : le `src/` était SAIN et le RÉCIT était FAUX** — retour à
    l'auteur pour cela seul. Le correctif ne touche **aucune ligne de CODE** : revérifié au merge en
    `python3`, comments dépouillés (chaînes et littéraux respectés) sur les deux fichiers du
    correctif — `EventManager.cpp` **4302 → 4302 o de code**, `EventWireBytes_test.cpp`
    **17292 → 17292 o**, **byte-identiques**, seuls les commentaires bougent.

  - ⭐⭐ **LE MARQUEUR A ÉTÉ REJOUÉ, PAS CRU SUR PAROLE, ET IL TOMBE À L'UNITÉ PRÈS.** `fprintf`
    posé sur la ligne du dump, campagne `make check` complète, comptage **par fichier `.log`** :
    **17 passages**, dont **13 dans TROIS suites PRÉEXISTANTES** — `core/ImpulseGarbageIo_test`
    **10**, `core/WagoPortDefault_test` **2**, `core/SetStateGarbage_test` **1** — plus **4** du
    filet neuf `core/EventWireBytes_test`. Cause revérifiée : **7 classes d'IO** posent
    `log_history="true"` par défaut **dans leur propre constructeur** (`OutputLight`,
    `OutputShutter`, `OutputShutterSmart`, `OutputLightDimmer`, `OutputLightRGB`, `OutputAnalog`,
    `Scenario`), et la chaîne de garde `EventManager.cpp:61-77` est **byte-identique
    master↔branche — 491 octets des deux côtés**, donc **tout aussi ouverte avant**. ⇒
    l'affirmation *« n'était exercé par AUCUN test »* est bien **morte** ; celle qui la remplace,
    **« aucun oracle ne regardait ces octets »**, est vérifiée aux **6 endroits** annoncés
    (`FINDINGS.md`, `E4.1l.md`, `BOARD.md`, en-tête d'`EventWireBytes_test.cpp`, **+ les 2
    commentaires de `EventManager.cpp`**). ⭐ **`core/ShutterImpulse_test.log` porte 0 marqueur** —
    le site migré n'est pas sur son chemin, mesuré et non supposé.

  - ⭐⭐ **LES CINQ DELTAS D'OCTETS SONT REPRODUITS SUR SONDE COMPILÉE** contre le vrai `jansson` et
    le vrai `json.hpp`, **hexadécimal à l'appui** — c'est ce dont les sept tickets suivants héritent :
    **(1)** ordre des clés — l'objet event `event_raw,type,type_str,data` → **`data,event_raw,type,
    type_str`**, l'enveloppe `msg,data` → **`data,msg`** ; **(2)** `\u00E9` → `\u00e9` ;
    **(3)** paire en UTF-8 invalide **supprimée** (`{}`, 2 o) → **conservée** en U+FFFD, **en valeur
    comme en clé** ; **(4)** ⭐ **DEL** — `7b2263223a22617f62227d` = `{"c":"a<7f>b"}` **11 o** →
    `{"c":"a\u007fb"}` **16 o** : ⚠️ **le `Content-Length` change** ; **(5)** ⭐ **`0x00` embarqué** —
    `json_string()` **tronquait à la chaîne C** : `{"c":"a"}` **9 o** au lieu de `a\0b`, et une clé
    `k\0z` était servie sous le nom **`"k"`, en silence**, avec sa valeur → `{"k\u0000z":"v"}`.
    Atteignables par `%7f` / `%00` : `Utils::url_decode` (`StringUtils.cpp:57-73`) fait
    `ret += (char) htoi(...)`, l'octet entre **tel quel**, **sans traverser aucun parseur JSON**
    (revérifié ; ⚠️ **pas** rejoué de bout en bout contre un `calaos_server` réel — l'auteur l'écrit).
    Balayages reproduits à l'identique : `0x01`–`0x1F` ⇒ **9 diffs, toutes de casse**, aux octets
    **`0B 0E 0F 1A 1B 1C 1D 1E 1F`** ; `0x80`–`0x9F` bruts ⇒ **32/32** ; `U+0080`–`U+009F` bien
    formés ⇒ **12** ; ⭐ **`U+2028`/`U+2029` IDENTIQUES** ; `"` et `\` identiques. Et **`json_pack()`
    avec un `%s` NULL rend bien `NULL` pour l'objet ENTIER** — le commentaire `EventManager.cpp:198`
    et l'énoncé de fiche qui disaient « une paire perdue » sont bien corrigés, la perte venait de
    `jansson_from_params()`.

  - ⭐⭐ **L'INVARIANT 3 D'`E4.1.md` EST CORRIGÉ SUR PLACE** — c'est le point qui comptait le plus,
    parce que c'est le document de l'épique : *« seule la casse de l'hexadécimal change »* a été
    **retiré de l'énoncé** et remplacé par un bloc ⚠️ daté qui nomme **DEL** et **`0x00`** et écrit
    que **tout sous-ticket de la série hérite de ces deux deltas**. Un invariant faux qui survivait
    ici se serait propagé **sept fois**.

  - **Contre-mutations rejouées au merge, arbre reconstruit depuis une copie pristine hors du dépôt
    et hors de `/tmp`** (réécriture du contenu, **jamais `cp -p`**), **fichiers restaurés comptés,
    jamais nul**, `cmp` d'application exigé (**F-HARN-1**), **binaires de test supprimés avant chaque
    passe** (le piège `_DEPENDENCIES` : les trois binaires portent
    `_DEPENDENCIES = libcalaos_common.la` et ne se relient pas tout seuls), et la ligne **`CXXLD`
    exigée par regex ANCRÉE à double espace** :
    - **témoin `M0`** (aucune mutation) ⇒ **ensemble VIDE, 0 rouge**, `# TOTAL: 96 / FAIL: 0`,
      **1609 cas exécutés** — donc **aucun binaire mort**, pas de faux vert par mort de binaire ;
    - **`M3`** (`typeToString()` : `room_added` ↔ `room_changed`) ⇒ **8 rouges**, exactement les
      cinq annoncés (`EveryEmittedTypeHasItsEnvelope`, `EventRawEncodesWhatDataKeepsRaw`,
      `RoomChangedOnTheFourRoomMutators`, `TheFourDeadTypesKeepTheirNumbersAndStrings`,
      `TypeToStringCoversTheTwentyThreeNamedTypes`) **plus 3 du filet d'octets**, et ⭐ **3 des 8
      sont ADOSSÉS AUX GOLDENS `e40d_*`** (`e40d_ws_event_accented`, `e40d_scenario` /
      `e40d_ws_event_catalog`, `e40d_ws_room_changed`) : **le filet des goldens reste PORTEUR**.

  - **Build d'intégration post-rebase, `distclean` complet** (`autogen` + `configure` + `make -j32` +
    `make check -j8`, **un seul build**, attendu par **`docker wait`**, sans relance) : **`# TOTAL: 96`
    = le compte d'entrées `TESTS` recompté en `python3`** (master **95**, **+1**
    `core/EventWireBytes_test`), **`# PASS: 95 / FAIL: 0 / ERROR: 0 / XFAIL: 0 / XPASS: 0`**,
    **`# SKIP: 1` = `run-python-tests.sh`** (pytest absent de l'image ⇒ `exit 77`, **le comportement
    voulu de T3.44**, pas une régression), **un seul** bloc `Testsuite summary`, **0 `error:`**,
    **`^  CXXLD    calaos_server$`** (regex ancrée, double espace) présent. **Cas réellement exécutés
    comptés sur les lignes `[ RUN ]` : 1609 sur 91 binaires gtest** (+ 5 suites-scripts = 96) ;
    périmètre : `core/EventWireBytes_test` **10**, `ParamsJson_test` **19**,
    `core/JsonApiEvents_test` **57**. ⚠️ **Écart de récit, non bloquant** : la fiche annonçait
    **1628 sur 92** ; l'écart vaut **exactement 19 cas et 1 binaire**, soit un `ParamsJson_test.log`
    compté deux fois côté auteur. **Les chiffres par suite, eux, tombent juste.**
    **145 goldens, arbre `d4ebc61f`** identique à `master`, **aucun bougé**, `git status -uall`
    **vide** après restauration.

  - ⚠️ **`F-FLAKY-1` n'a PAS mordu au merge** : `core/ShutterImpulse_test` est **vert sur les quatre
    campagnes** (validation, marqueur, `M0`, `M3`), `# FAIL: 0` partout. Rien n'a été relancé pour
    l'obtenir. Le finding reste **entièrement valable** — il dépend de la charge, et cette campagne a
    été moins contendue que celle de l'auteur.

  - **Corrigé au merge, mesuré** : l'énoncé d'acceptation n°1 disait que le mot `jansson` restait
    « **1 fois dans chaque fichier** » ; **recompté, il est 3 fois dans `EventManager.cpp`**
    (`:90`, `:200`, `:203`) et **1 fois dans `EventManager.h`** — le correctif de revue en a ajouté
    deux. Toutes en **commentaire** ; le critère mesurable (**0 `json_t` / 0 `json_*(` dans le
    CODE**) est vérifié, et le périmètre passe bien de **181 à 166** appels `json_*(` hors
    commentaires. `E4.1n` est bien **requalifiée en tâche DOCUMENTAIRE aux 4 endroits** (⛔ *ne pas
    ouvrir de ticket vide*), et `F-LINK-1` a bien son **4ᵉ membre** : *« non exercé » se mesure en
    INSTRUMENTANT LE SITE*.

  - **Conflit de rebase : un seul, attendu, `FINDINGS.md`** — appends des deux côtés (T3.44 pour
    `master`, E4.1l pour la branche). Résolu **en gardant les deux côtés**, chacun **sous son propre
    titre `##`**, par régénération : fichier complet de `master` + append du bloc de branche
    **verbatim** (la branche est un append **pur** sur la base, vérifié : `base` est préfixe exact de
    la version de branche, **8506 octets** ajoutés). **0 marqueur de conflit résiduel**, et les
    **149 fichiers** de `docs/refactoring/` intacts. ⚠️ **Le piège de l'`endif` n'a pas mordu** :
    `tests/Makefile.am` = **82 `^if*` / 82 `endif`** (master **81/81**), **profondeur jamais
    négative**, minimum 0, final 0. **`BOARD.md` reste trié par NUMÉRO** — aucune ligne ajoutée,
    seules les lignes `E4.1` et `E4.1l` réécrites sur place.

- **🔒 T3.48 ✅ MERGÉ (`931cb6ff`, **7** commits, `git rebase 2861512d` + `merge --ff-only`,
  historique linéaire, **0 commit de fusion**)** — ⭐⭐ **`make distcheck` A ÉTÉ REJOUÉ APRÈS LE
  REBASE, et c'est la décision centrale de ce merge.** L'auteur ne l'avait pas refait (43 min) et
  master avait ajouté **6 fichiers sous `tests/`** ⇒ le résultat livré ne portait plus sur ce qui
  est mergé. Rejoué **une seule fois**, attendu par `docker wait` : **RC 0 de bout en bout**, et
  ⭐ **la suite COMPLÈTE tourne depuis l'archive dépliée** — `# TOTAL: 98 / PASS 97 / SKIP 1 /
  FAIL 0`, **un seul bloc `Testsuite summary`**, **0 `error:`**, **104 `CXXLD` ancrées**.
  **RIEN POUSSÉ.**

  - ⭐ **7ᵉ COMMIT AJOUTÉ AU MERGE — la salissure `mktemp` n'est plus fichée, elle est CORRIGÉE.**
    Le §6.16 la déclarait « laissée volontairement », au seul motif que la corriger invaliderait le
    `distcheck` validant. **Le rebase rendait ce distcheck caduc de toute façon** ⇒ l'argument
    tombe. `trap 'rm -rf "$tmpd"' 0` ajouté juste après le `mktemp -d` (**une ligne**, les `rm -rf`
    explicites conservés, redondants et inoffensifs), et **c'est ce hook-là — trap compris — que le
    `distcheck` du merge valide**. Fiche : bloc ⛔ **CORRECTION**, ancien texte **barré**.

  - ⭐ **La garde des non-suivis (R3) a été REJOUÉE EN VRAI, pas relue.** Piège d'outillage à
    retenir : dans un worktree git, `.git` est un **fichier** qui pointe **hors du montage** ⇒ dans
    le conteneur habituel (`-w /workspaces/calaos_base`) `git rev-parse` échoue et **la garde se
    met en retrait sans jamais être exercée** — c'est très probablement le cas de l'auteur. Montage
    utilisé ici : le worktree **à son vrai chemin hôte** + `…/calaos_base/.git` en `:ro` +
    `safe.directory=*` ⇒ **`GITPROBE_RC=0`, garde ACTIVE**. Résultat : `src/lib/uvw/stray-merge63.user`
    posé ⇒ `make dist` **RC 2**, `dist-hook: refusing to ship files git does not track:` puis
    **`dist-hook:     src/lib/uvw/stray-merge63.user`** — **nommé** ; retiré ⇒ **RC 0**. ⭐ **La
    correction de locale tient** : aucun `comm: file 1 is not in sorted order`. ⚠️ **Et le `distcheck`
    complet a tourné garde ACTIVE au premier `dist`** : `untracked-file gate stood down` apparaît
    **exactement une fois**, au `dist` imbriqué — le test d'égalité sur la RACINE fait ce qui est écrit.
    Limite déclarée au §6.16 (« sans `git`, retrait silencieux ») : **présente et exacte**.

  - **`H1` et `H3` de l'oracle rejoués** (worktree jetable `.merge63/oracle-probe`, **1 fichier
    restauré**, `git checkout` donc **dates non préservées**, oracle revenu vert après) :
    `dist-hook: @true` ⇒ **RC 2**, et ⭐ la neutralisation **par ADDITION** ⇒ **RC 2** avec
    `src/lib/Makefile.am defines 2 dist-hook rules with a recipe; make keeps the last one`. Les
    messages nomment le **mécanisme**, jamais un fichier. La limite résiduelle (« nommer la variable
    et copier mal passe les trois contrôles ») est écrite **dans la docstring ET dans la fiche**.

  - ⭐ **`tar --format=ustar -chf` DÉRÉFÉRENCE — confirmé par sonde, pas par lecture.** `cp -pR`
    conserve le lien (`lrwxrwxrwx`), puis l'archive le rend en **fichier régulier** portant le
    contenu, la cible devenant une entrée **`hard link`**. `am__tar` du `Makefile` généré :
    `tar --format=ustar -chf - "$$tardir"`. `find \( -type f -o -type l \)` **2**, `find -type f`
    **1**, sur la même sonde.

  - **Conflits et résolution.** `tests/Makefile.am` **s'auto-fusionne** (le bloc T3.48 s'insère
    avant `if HAVE_GTEST`, il n'appende pas la queue) ⇒ **le piège de l'`endif` n'avait rien à
    mordre**, vérifié quand même : **83 `^if*` / 83 `endif`** (master **83/83**, la branche
    n'ajoute aucun bloc conditionnel), **profondeur jamais négative**, min 0, final 0.
    Trois conflits **documentaires**, tous des appends : `FINDINGS.md` et `RELEASE_NOTES.md`
    **« garder les deux côtés »**, chaque bloc **sous son propre titre `##`** (77 `##`, **aucun
    doublon**) ; `BOARD.md` recomposé en `python3` et **retrié par NUMÉRO** — T3.46 · T3.47 ·
    **T3.48** · **T3.49** · T3.50.

  - ⭐ **Le lien mort de la ligne `T3.46` est RÉPARÉ par ce merge** : elle nommait `T3.49.md`, que
    cette branche crée. Balayage complet des liens `*.md` de `docs/refactoring/` : **il ne reste que
    `T3.41.md` et `T3.42.md`**, tous deux cités par la ligne `T3.47` et **déjà morts sur `master`** —
    **pas de ce merge**.

  - **Recomptes faits ici, pas repris.** `211` non couverts sur le commit de caractérisation
    **rebasé**, arbre vierge (le sujet du commit dit toujours `212` : **dit dans la fiche**, pas
    réécrit). Oracle sur l'arbre mergé : **899 balayés / 219 couverts / 0 non couvert** (895/219
    avant rebase). Archive réelle : **1085 entrées** (1009 fichiers, 76 répertoires), **11/11
    licences** — ⚠️ **`uri_parser` n'en a AUCUNE dans le dépôt**, nuance écrite dans
    `RELEASE_NOTES.md` §📦 · **209 fichiers suivis absents, dont 0 sous `src/` ou `tests/`**.
    Suite : **98 journaux = 98 `.trs` = 98 entrées `TESTS`**, **1615 cas gtest** sur **92** binaires
    (1612 OK, **3** `SKIPPED` internes à gtest, 2 rouges), **93 `CXXLD` ancrées** au `make check`.
    ⚠️ **`# SKIP: 1` = `run-python-tests.sh`, NORMAL** (pas de `pytest` dans l'image, voulu par T3.44).

  - ⚠️ **`F-FLAKY-1` VU, et il a APPRIS quelque chose.** `# FAIL: 1` au `make check` d'intégration,
    **un seul binaire**, `core/ShutterImpulse_test`, **deux cas** : **`:297`** (marge 56 ms, connu)
    **et ⭐ `:343`** (marge 124 ms, *`PlainImpulseUpKeepsMovingUntilTheRequestedDuration`*) — même
    fichier, **même forme** (`pumpLoopFor(stillMovingProbeMs(…))` + `EXPECT_FALSE(isStopped())`),
    même message. ⭐ **`:343` est une PREMIÈRE observation** : le tableau de `T3.49` §1.1 le donnait
    « jamais vu rouge ». **Cela VALIDE la lecture par la marge au lieu de l'infirmer** — c'est le
    **suivant sur la liste** qui tombe, pas un site au hasard. **Non relancé** (consigne, et
    relancer effacerait l'information) ; `T3.49.md` §1.1 et sa ligne de `BOARD` **mises à jour**.
    **0 FAIL dans `distcheck`**, sur la même suite, depuis l'archive.

  - **Marges de `T3.49` recalculées en `python3` depuis la source** : 56 · **73** · 124 · 139 ·
    190 ms et ≈29,8 s. ⭐ **Le `73` de `:384` est le bon et mon premier calcul était faux** : sans
    `impulse_time`, le commentaire du test dit l'échéance à **`requested - 1`** = 146, pas 147 —
    la fiche est plus soigneuse que le recompte naïf. `:384` **n'est donc pas** la marge la plus
    étroite, l'inférence corrigée par l'auteur **tient**.

  - **Goldens `d4ebc61f`, 145 fichiers, AUCUN bougé — vérifié APRÈS le build** (arbre identique à
    la base `df2851d0` **et** à `master`). `git status -uall` **vide** : les **8** `po/`
    régénérés par le build ont été **restaurés sans préserver les dates** (`git checkout -- po/`).

  - ⚠️ **Chiffres de la fiche périmés par MON rebase, laissés tels quels et signalés ici** :
    §6.12 dit `TESTS` **95** et `if`/`endif` **81/81** (mesures pré-rebase) ; l'arbre mergé donne
    **98** et **83/83**. Idem `1081` entrées d'archive → **1085**, `205` restants → **209**,
    `895` balayés → **899**. Les SHA de commits cités dans `T3.48.md`/`FINDINGS.md`
    (`206d66fa`, `7fbecfdc`, `07106292`) sont **ceux d'avant rebase**, et il y a **deux
    générations** : `206d66fa`/`7fbecfdc` sont l'incarnation d'avant le rebase de l'AUTEUR,
    `07106292`/`791d6f13` celle d'après. Tous portent les **mêmes sujets** ; l'équivalence sur
    `master` est **caractérisation → `de759073`** et **correctif → `3428adcc`**. Convention du
    dépôt : les fiches citent les SHA de branche, réécrits à chaque rebase.

- **🔒 T3.46 ✅ MERGÉ (`ec0bdfe1`, **10** commits, `git rebase --onto c6c7c0d3 44657407` +
  `merge --ff-only`, historique linéaire, **0 commit de fusion**)** — ⭐⭐ **[T3.50](T3.50.md) EST
  OUVERTE ET C'EST LE PROCHAIN DE SA CHAÎNE, pas « plus tard »** : la moitié **LECTURE** du chemin
  RETOUR de Wago, **13 sites nommés / 12 vivants**. C'est la **CONDITION** à laquelle l'inversion
  d'ordre de T3.46 (écriture avant lecture) a été acceptée. **RIEN POUSSÉ.**

  - ⭐⭐ **LE PIÈGE DE L'`endif` A MORDU — et il a été résolu par RÉGÉNÉRATION, pas par « garder les
    deux côtés ».** `E4.1l` avait appendu son bloc `core/EventWireBytes_test` en fin de
    `tests/Makefile.am` pendant que la branche restait sur `44657407` : le premier commit de la
    branche est entré en **CONFLIT** sur cette queue. Résolution mécanique, en `python3` :
    `git show c6c7c0d3:tests/Makefile.am` **+ append verbatim** des 25 lignes du bloc `# T3.46`
    (prouvé append pur : les deux côtés sont `base(2824) + queue`), **jamais** une fusion de
    marqueurs. Résultat **+25 / −0** contre `master`, puis **+2** par le commit du tripwire ⇒ **+27
    / −0** au total, le chiffre annoncé par la fiche. **Contrôle après merge : `^if*` / `endif`
    = 83 / 83** (master 82/82 + le bloc T3.46), **profondeur jamais négative, minimum 0, final 0**.
    ⚠️ **Leçon confirmée pour la Nᵉ fois : sur cette queue de fichier, un `endif` par bloc ; « garder
    les deux côtés » y fabrique un `endif` orphelin qui ne se voit pas au `git diff`.**

  - **Deuxième conflit, `FINDINGS.md`, résolu « garder les deux côtés »** — la section `E4.1l` de
    `master` d'abord, la section `T3.46` **appendue** ensuite, **chacune sous son propre titre
    `##`**. `BOARD.md` a fusionné **sans conflit** (la ligne `T3.46` réécrite, la ligne `T3.50`
    insérée), et **reste triée par NUMÉRO** : T3.45 · T3.46 · T3.47 · T3.48 · **T3.50**.
    ⚠️ `T3.49` **n'existe pas dans cet arbre** (propriété d'un autre agent) — **aucun doublon créé
    par ce merge**, T3.46 se contente de la nommer.

  - ⭐⭐ **L'INFIRMATION N°3 EST RÉÉCRITE AUX QUATRE ENDROITS, le 4ᵉ compris.** La revue n'avait vu
    que trois porteurs de la phrase fausse *« la paire permutable n'est pas dans les six commandes
    publiques »* ; le **`BOARD`** était le quatrième. Vérifié un par un ici — `T3.46.md` §7.5(b),
    `FINDINGS.md` `F-WAGO-9`, l'en-tête de `tests/WagoWriteReply_test.cpp` et la ligne `BOARD`
    portent **tous** : `mbus.h:109-127`, **les 6 commandes appelées portent TOUTES la paire
    adjacente**, `M5` permute justement une **signature publique**, et ce qui porte réellement le
    refus est **`mbus_cmd_addr_wdata`** (`mbus_cmd.c:78`), **5 sites** (`:254 :292 :332 :368 :405`),
    **3 paires de rôles** ⇒ impossible sans **scinder**. ⭐ **Le refus tient sur le COÛT, pas sur
    l'absence de la paire.** Les trois occurrences résiduelles de l'ancienne phrase sont toutes
    **citées pour être démenties**, aucune ne subsiste comme affirmation.

  - ⭐⭐ **LE TRIPWIRE DURCI : LA TABLE 4/5 → 1/5 EST REPRODUITE INDÉPENDAMMENT.** Les deux
    analyseurs extraits — celui de `7d957874` et celui livré — dans **une même sonde `g++
    -std=c++17`**, **chacun jugé avec SA propre assertion** (`address` avant, `addr` après ; les
    confondre fait disparaître le faux rouge `R2`). Verdicts : `base` VERT/VERT · **`R1` espace
    avant `(` 🔴→✅** · **`R2` renommage `address`→`addr` 🔴→✅** · **`R3` cast sorti en variable
    🔴→🔴 (subsiste, déclaré)** · ⭐ **`R4` COMMENTAIRE DE DOC 🔴 (`argc=2`) → ✅ VERT — le pire cas,
    rejoué en priorité et bel et bien fermé** · `R5` trois lignes VERT/VERT · **`M6`/`M7` 🔴/🔴 des
    deux côtés, les vrais rouges sont CONSERVÉS**. Trois cas supplémentaires non demandés confirment
    le blanchiment : **commentaire `//` 🔴→✅**, **littéral de chaîne 🔴→✅**, identifiant plus long
    VERT/VERT. *Un garde-fou qui rougit parce que quelqu'un a écrit un commentaire est un garde-fou
    que le premier venu désactive* — il ne rougit plus.

  - ⭐ **`M6`/`M7` REVÉRIFIÉS SUR LE BINAIRE LIVRÉ, un seul cas rouge chacun.** Le tripwire lit
    `WagoCtrl.cpp` **à l'exécution** (`CALAOS_TOP_SRCDIR`) ⇒ **aucune reconstruction**, donc le faux
    vert « arbre déjà construit » est **exclu par construction**. Mesuré : **témoin `rc=0`, 6 cas,
    6 PASS** · **`M6` `rc=1`, 6 ran / 5 PASS, seul rouge
    `TheTwoUntypedLibmbusWriteCallsStillPassAddressBeforePayload`** · **`M7` `rc=1`, idem** ·
    **contrôle après restauration `rc=0`, 6/6**. Restauration **comptée `1` fichier** à chaque passe,
    vérifiée par `filecmp.cmp`, `os.utime` forcé.

  - ⭐⭐ **`MXD` REJOUÉ — c'est la garantie la plus forte du ticket, et elle tient.** `WagoTypes::
    WordValue &` dans le `typedef` `SingleWord_cb` ⇒ **`rc=2`, 2 lignes `error:`**, la seconde étant
    `WagoMap.cpp:244:54: error: cannot bind non-const lvalue reference of type
    'sigc::slot3<void, bool, WagoTypes::Address, WagoTypes::WordValue&>::arg3_type_' … to an rvalue`.
    ⇒ ⭐ **`sigc++` refuse LUI-MÊME la référence** : sur `SingleBit_cb`/`SingleWord_cb`, l'angle mort
    **`F-TYPE-5` est INARMABLE en silence** — la tentative est un **échec de build**, pas une suite
    verte. Ce n'est plus une discipline d'auteur, c'est une propriété de la bibliothèque.
    ⚠️ **Portée à ne pas élargir** : propriété de `sigc::slot`, **pas** de la sonde `is_invocable_v`.
    **Les trois `MX` seulement CITÉS par la revue ont été rejoués aussi** (pas un seul, les trois) :
    **`MXA`** `status`↔`BitValue` **`rc=2`, 4 lignes** · **`MXB`** même enveloppe deux fois **`rc=2`,
    2 lignes** · **`MXC`** `static_cast` **`rc=2`, 2 lignes** (`no matching function for call to
    'WagoTypes::Address::Address(WagoTypes::WordValue)'`). **Témoin `M0` `rc=0`, ensemble VIDE**, et
    **passe de contrôle finale revenue à `rc=0`, ensemble VIDE** ; restauration **9 fichiers**
    comptés et vérifiés octet à octet à chaque passe.

  - **Les deux trous de l'oracle, vérifiés séparément.** (1) ✅ **Comblé** :
    `EXPECT_FALSE(is_base_of_v<Address, WordValue>)` a désormais son **contrôle positif** —
    `struct AProbeBase` / `struct AProbeDerived: AProbeBase` et un `EXPECT_TRUE(is_base_of_v<…>)`
    avec le message *« is_base_of_v answers FALSE for everything here — the W4 line above is passing
    for free »*. (2) ⚠️ **Déclaré, pas comblé** : *« les 4 implémentations ne sont sondées par aucun
    cas »* est écrit **en tête de `WagoWriteReply_test.cpp`** (« ⚠️ HOLE, NAMED … Said, not hidden »)
    **et** au **§7.11** de la fiche, avec la même phrase clé : ce qui les ferme est la **compilation**
    plus `M1`/`M2`/`M3`, et c'est **strictement plus faible qu'un oracle exécuté**.

  - **Build de validation post-rebase, sur l'arbre mergé** : `# TOTAL: 97` · `# PASS: 95` ·
    `# SKIP: 1` · `# FAIL: 1` · `# XFAIL/XPASS/ERROR: 0`, **un seul bloc `Testsuite summary`**,
    **0 `error:`**, `CXXLD    calaos_wago` **et** `CXXLD    calaos_server` présents (regex ancrée).
    ⭐ **Le `97` est RECOMPTÉ, pas cru** : **97 fichiers `.trs`** sur l'arbre, et
    `TESTS` = **99 entrées** dont 2 éteintes par condition ⇒ `96` sur `master` **+ 1**
    (`WagoWriteReply_test`, **6 cas, 6 PASS**). **1634 cas gtest exécutés** sur **93** suites,
    **aucune à 0 cas** (le compte monte depuis les ~1601 de la branche : `E4.1l` a apporté ses cas).
    **`# SKIP: 1` = `run-python-tests.sh`**, comportement **voulu** installé par `T3.44` — aucune
    image n'a `pytest` ⇒ `T3.47`. **`git status -uall` VIDE.**

  - ⭐ **`F-FLAKY-1` VU au run de validation, et VÉRIFIÉ DE CETTE FAMILLE avant d'être écarté** —
    pas seulement « c'est le flake connu ». `core/ShutterImpulse_test.cpp:297`,
    `PlainImpulseDownKeepsMovingUntilTheRequestedDuration`, `Value of: sh.isStopped() / Actual: true
    / Expected: false` : **le fichier ET la forme d'assertion** de la famille des **6** (`:297 :343
    :384 :450 :498 :664`, relues une par une sur l'arbre mergé, toutes `EXPECT_FALSE(sh.isStopped())`
    après un `pumpLoopFor` en deçà de l'échéance). Arithmétique refaite **au source** :
    `kPlainImpulseTimeMs = 35` (`:213`… `:215`), `kPlainDownMs = 147`,
    `stillMovingProbeMs = (impulse + requested) / 2` ⇒ sonde à **91 ms** pour une échéance à
    **147 ms** = ⭐ **56 ms de marge**. **AUCUN `make check` relancé pour laver le rouge** ⇒ `T3.49`.

  - **Goldens vérifiés APRÈS les campagnes de mutation, pas avant** : **145** fichiers, arbre
    `d4ebc61fb2b1876f587d075a0cb050750dc1876f`, **aucun bougé**, `git status` sur `tests/core/golden`
    **vide**. ⭐ **`src/` strictement inchangé depuis l'état revu** — vérifié en diff ciblé
    `dbf4fbc1..HEAD` sur `src/bin/calaos_server/IO/` et `tests/WagoWriteReply_test.cpp` : **vide**.

  - ⚠️ **Ce dont je ne suis pas sûr.** (a) Le commentaire d'en-tête du test compte *« deux des quatre
    corrigées »* là où la fiche compte *« 4/5 → 1/5 »* : les deux disent la même réalité mesurée
    (`R2` est traitée dans le point (3) du commentaire, comme acceptation du préfixe `addr`), mais la
    **comptabilité est formulée différemment aux deux endroits** — cosmétique, non bloquant.
    (b) L'en-tête du test nomme `mbus_cmd_addr_wdata` et ses **5** sites mais **pas** sa ligne de
    définition `mbus_cmd.c:78`, présente aux trois autres endroits. (c) La ligne `BOARD` de T3.46
    cite `T3.49`, **dont la fiche n'existe pas encore** dans cet arbre : lien mort tant que l'agent
    propriétaire n'a pas livré. (d) Rien n'a tourné sous **ASan**, ni `make distcheck`, ni contre un
    automate Wago réel — la preuve sur ce chemin reste **la compilation, la mutation et le tripwire**.

- **🔒 T3.44 ✅ MERGÉ (`159202b3`, **16** commits, `master` **immobile** sur `df2851d0` ⇒ **rebase
  inutile**, `merge --ff-only`, historique linéaire, **0 commit de fusion**)** — `make check` ne dit
  plus `PASS` sur des suites qui n'ont pas tourné : `tests/run-python-tests.sh` délègue à
  `tests/python-suite-runner.py`, qui **compte ce que `tests/python/` DÉCLARE, compte ce qu'il a
  EXÉCUTÉ, publie les deux** (`run-python-tests: suites=N/M cases=N/M`) et sort **77** (`SKIP`
  visible) plutôt que **0** quand les deux désaccordent. **RIEN POUSSÉ.**

  - ⭐⭐ **LES QUATRE RÉSERVES DE LA 2ᵉ REVUE ONT ÉTÉ REJOUÉES AU MERGE, PAS CRUES SUR PAROLE.**
    Copie **pristine** et scripts de mesure sous un chemin **qui nomme l'agent de merge**
    (`…/scratchpad/merge58-fpytest1/`) — le scratchpad de session n'est pas privé. Reconstruction
    de l'arbre de travail **avant chaque mutant**, par recopie **sans préserver les dates**
    (`shutil.copy`, jamais `copy2` — variante n° 11), **fichiers restaurés COMPTÉS : 1153 à chaque
    passe, jamais nul** (une restauration qui échoue en silence rejoue les mutations sur un arbre
    non restauré et rend **vert**), et **refus si le motif de mutation est absent ou ambigu**.

    - ⭐ **`R1` — la fixture du relecteur ROUGIT, et la correction porte bien sur le REPLI.**
      Rejouée sur l'arbre réel (`debian:12` + `python3-pytest` 7.2.1 + `fastapi`/`httpx`/`colorama`,
      les quatre paquets) : un cas de `tests/python/test_logger.py` marqué `@pytest.mark.skip`, plus
      un paquet `tests/python/regress/` portant un `test_logger.py` **de même basename** déclarant
      un cas **du même nom pointé**. Publié : **`suites=6/7 cases=42/43`**, `NOT RUN: test_logger.py
      (3 of 4 declared cases executed; missing: test_default_level_4_logs_debug)`, **RC 77** —
      exactement la colonne « après » de la fiche.
      ⭐ **Le mécanisme d'origine a été RECONSTITUÉ, pas seulement raconté** : déclaration remise
      **plate** (`os.listdir`) **ET** `classname` résolu par sa **première** composante « module »
      ⇒ **`suites=6/6 cases=42/42`, `PASS`, RC 0, sans `NOT RUN` ni `UNDECLARED`** — le faux vert
      d'origine, reproduit.
      ⭐ **La mesure qui a corrigé la correction est confirmée** : la famille junit **par défaut
      n'écrit AUCUN `file=`** (revérifié sur pytest 7.2.1 : sonde `--junit-xml` nue, `file=` absent)
      ⇒ le chemin vivant est le **repli** sur le `classname`, pas `basename(file)`. **Démontré par
      deux mutations opposées** : `MU_D` (clé par basename **dans le lecteur de `file=`**) laisse la
      fixture **ROUGE** — il est inatteignable ; `MU_J` (repli par la **1ʳᵉ** composante) est **tué
      par `C5f`**, ensemble rouge **exactement `{C5f}`**. ⚠️ **Nuance mesurée, à ne pas gommer** :
      **aucune des deux moitiés du correctif ne suffit à elle seule** à produire le faux vert
      (`MU_E` seul ⇒ `5/6 41/42` + `UNDECLARED` ; `MU_J` seul ⇒ `6/7 42/43`) — **il faut les deux**.
      La fixture sur arbre réel ne discrimine donc **pas** laquelle des deux moitiés a été réparée ;
      **c'est l'oracle qui le fait** (`C5f` pour le repli, `C5m` pour le lecteur de `file=`).
      ⭐ **`C5f`/`C5g` sont bien ASYMÉTRIQUES** : `test_dup.py` déclare `{test_shared_name (skip),
      test_only_here}` et `sub/test_dup.py` `{test_shared_name, test_only_there}` — honnête **3/4**
      sur **1/2** suites, clé par basename **2/3** sur 1 fichier : **les deux comptabilités
      DIVERGENT NUMÉRIQUEMENT**, ce qu'une paire symétrique ne faisait pas.

    - ⭐ **`R2` — `MU_A` et `MU_B` rougissent ; `MU_C` est bien ÉQUIVALENT, `MU_I` bien réel.**
      `MU_A` (motif `*_test.py` retiré du déclarateur) ⇒ **`{C5h, C5i}`**, exactement la table.
      `MU_I` (`_PARAM_SUFFIX` **lazy ET non ancré**) ⇒ **`{C5c}`**. `MU_B` (`ran_files += 1`
      inconditionnel) ⇒ **12 cas rouges** `{C2, C5a…C5i, C5l, C5m}`, la table à l'unité près.
      `MU_C` (lazy **ancré**) **survit, oracle vert** — et sa preuve d'équivalence a été **rejouée à
      l'identique** : **87 381** chaînes exhaustives sur `{'[', ']', 'a', '\n'}` longueurs 0–8 et
      **400 000** aléatoires, **0 différence**. ⭐ **La preuve tient, et elle est falsifiable** : la
      **même** campagne aléatoire montre que la variante **non ancrée** diffère sur **175 384** des
      400 000 chaînes, et le contre-exemple publié se vérifie (`test_p[a]b]` → `test_p` avec l'ancre,
      `test_pb]` sans). ⇒ l'ancre `$` cloue bien le `]` en fin de chaîne : greedy et lazy consomment
      le même segment. **La moitié `suites=` est effectivement assertée** — numérateur **et**
      invariant de cohérence dans les deux sens (`check_accounting`), plus les deux moitiés dans
      chaque `c5_run` ; c'est ce qui tue `MU_B`.

    - **`R3` — l'arbitrage `xfail` est consigné dans `DECISIONS.md` et il est JUSTE.** Un `xfail`
      **a tourné** : son corps s'est exécuté et a levé, ce qui est son issue attendue ; la
      classification `<skipped type="pytest.xfail">` du junit est un **artefact de rapport**, pas un
      énoncé sur l'exécution. La comptabilité de ce lanceur existe pour distinguer « n'a pas PU
      s'exécuter » — l'`xfail` n'est pas de ceux-là. L'exception `xfail(run=False)` / `[NOTRUN]` est
      la bonne, et la divergence résiduelle sur le **verdict** d'un *xpass* est correctement
      identifiée comme une divergence de verdict, non de comptabilité. Le **mixin** reste
      **documenté et NON épinglé**, ⭐ **et la raison est écrite** : l'épingler figerait un **faux
      `SKIP`** bruyant ; restreindre le parcours `ast` aux classes « d'allure collectable »
      échangerait cette erreur bruyante et sûre contre une erreur **silencieuse et fausse**.

    - ⚠️ ⭐ **`R4` — LA LISTE CANONIQUE : ONZE au moment de ce merge, et la DOUZIÈME N'Y EST PAS.**
      **Vérifié à l'instant du merge** : ni `master` `df2851d0` ni l'arbre de `fix/t3.40`
      (`e4af03fc`) ne portaient d'entrée pour la douzième variante — *une restauration de sources
      qui échoue silencieusement ⇒ mutations rejouées sur un arbre non restauré ⇒ vert*.
      ⇒ **la place est LAISSÉE, rien n'a été écrit ici** : ajouter une n° 12 depuis ce merge et une
      autre depuis `fix/t3.40` ferait le doublon que `F-LINK-1` a déjà subi cette nuit.
      ⭐ ⚠️ **ET ELLE EST ARRIVÉE PENDANT CE MERGE — écrit ici pour que le prochain n'ait pas à le
      redécouvrir.** Contrôlé une seconde fois juste après le `ff-only` : `fix/t3.40` porte
      désormais, à `90f90bf8`, une section **« ⛔ Douzième variante de faux vert — la restauration
      qui ne restaure RIEN »** (`FINDINGS.md` ~`:5399`). ⚠️ **Son arbre est encore basé sur un
      `master` d'avant ce merge et ne contient donc PAS la liste canonique** (`LISTE CANONIQUE`
      absente de son `FINDINGS.md`). ⇒ **CE QUE DOIT FAIRE CELUI QUI MERGERA `T3.40`** : après
      rebase, `FINDINGS.md` sera en conflit — **garder les deux côtés, chaque bloc sous son propre
      titre `##`** —, puis **rattacher la section « Douzième variante » à la liste canonique en
      n° 12**, et **reprendre les mentions de compte et de rang** (`onze` → **douze**, `dix autres`
      → **onze autres**, le « fil commun des onze », et le titre de la liste elle-même).
      ⛔ **Ne PAS écrire une seconde section pour le même défaut** : celle de `fix/t3.40` est la
      bonne, elle a été écrite par qui l'a mesurée.
      **Chiffres recomptés `python3` sur l'arbre mergé, tous confirmés** : **93 `.cpp`** dans
      `tests/` = **90 `*_test.cpp` + 3 auxiliaires** ; `TimeRangeCalendar_test` = **11** cas
      atteignables (**1** `GTEST_SKIP` `tzdata` à `:501` **+ 10** `TEST_F` appelant le helper
      `evalInStableWindow` à `:584` — la définition à `:565` fait la 11ᵉ occurrence, pas un cas) ;
      **plafond 24/1595** = 11 + 7 + 3 + 1 + 1 + 1 sur **6 binaires** portant un `GTEST_SKIP`, **0
      `DISABLED_`** dans tout le dépôt ; `UrlDownloader_test` = **7** cas derrière `REQUIRE_CURL()`
      sur 10 (la 8ᵉ occurrence est bien la ligne `#define`, `:64`). **Dette n° 10 payée**
      (`F-TYPE-3` reclassée, « sixième » retiré) ; **dette n° 3 toujours sans section `FINDINGS`**,
      écrite comme dette — les deux vérifiées dans le texte.

  - ⭐ **QUATRE INCOHÉRENCES DE DOC TROUVÉES AU MERGE ET CORRIGÉES ICI** (aucune ne touche le code) :
    1. ⛔ **`BOARD.md` réécrivait le SHA de rebase de `T3.31`** : le commit de rebase de la branche a
       passé un `180c4b87` → `df2851d0` de trop, et la ligne de `T3.31` annonçait
       `git rebase df2851d0` — **impossible**, `df2851d0` est le commit de **journal** qui SUIT la
       fusion `2c7e4892`, et son propre message dit « rebase sur 180c4b87 ». **Restauré.**
       ⇒ ⚠️ *leçon : un remplacement global de SHA au moment d'un rebase mord les lignes des AUTRES
       tickets ; borner la substitution à sa propre ligne.*
    2. `T3.44.md` §9.1 disait encore « aucune des **neuf** variantes » — la seule mention restée
       hors de l'alignement (qui n'avait porté que sur `FINDINGS.md`). ⇒ **onze**.
    3. `DECISIONS.md` portait encore `90/90 PASS` → `89 PASS / 1 SKIP` et « 89 sur master, 90 sur la
       branche, 89 en CI » : comptes de l'ère `701a98e4`, jamais réalignés après les merges de
       `T3.25`/`T3.45`/`T3.48`. ⇒ **94 / 95 / 94**, mesurés.
    4. `check-extra-dist.sh` était attribué à **`T3.48`** (`T3.44` §8.1 et `T3.47` §2.4) : il vient
       de **`T3.45`**, ajouté par `3aa67056` — `T3.48` **n'est pas mergée**. ⇒ corrigé des deux côtés.
    ⚠️ **Résidu laissé tel quel, signalé et non corrigé** : l'index des cas en tête de
    `tests/check-python-tests-reporting.sh` s'arrête à `C5l` et **ne mentionne pas `C5m`**, ajouté
    plus tard. Ne pas toucher au fichier après la validation des deux images pour un commentaire ;
    **au prochain qui l'ouvre.**

  - **`T3.47` porte bien son AVERTISSEMENT en §0** : *ce ticket ACTIVE un chemin que rien n'exécute*
    — `run_with_pytest` et ses auxiliaires sont **du CODE MORT dans les deux images** (dev :
    `ModuleNotFoundError: No module named 'pytest'`, **revérifié** ; CI : **aucun `python3`**,
    **revérifié** `which python3` vide dans `debian:12` + la liste `apt` exacte de `ci.yml`) — avec
    `R1`/`R2`/`R3` en **tableau de dette d'entrée**. L'avertissement sur `check-extra-dist.sh` qui
    **se réveillera** quand `python3` sera installé, *et dont rien ne dit qu'il sera vert*, y est
    aussi. La nuance `UNDECLARED:` est écrite noir sur blanc dans `FINDINGS.md` : la note n'est
    imprimée **que si un back-end RAPPORTE** un inconnu, **une suite que personne ne collecte n'est
    rapportée par personne** ⇒ « le milieu honnête » est vrai **à 90 %, pas à 100 %**.

  - **BUILD DE VALIDATION DANS LES DEUX IMAGES, post-merge** (`./autogen.sh && ./configure && make`
    puis `make check`, **un seul build par image**, attendu par `docker wait`) :

    | image | `python3` | `libknx` | `# TOTAL` | `# PASS` | `# SKIP` | `# FAIL` | RC | blocs `Testsuite summary` |
    |---|---|---|---|---|---|---|---|---|
    | dev (`vsc-calaos_base-…`) | 3.11.2, **sans** `pytest` | oui | **95** | 94 | **1** | 0 | **0** | **1** |
    | CI (`debian:12` + `apt` de `ci.yml`) | ⛔ **aucun** | non | **94** | 91 | **3** | 0 | **0** | **1** |
    **CI, les 3 `SKIP` relevés dans les `.trs`** : `run-python-tests.sh`,
    `check-python-tests-reporting.sh` (tous deux `SKIP: no python3 interpreter detected at configure
    time`) et `check-extra-dist.sh` — **le 3ᵉ vient de `T3.45`, pas de nous** ; **89 binaires gtest
    / 1577 cas / 1576 passés** (un binaire de moins : `KNXExternProcWire_test` sans `libknx`), **0
    `error:`** à la compilation, `  CXXLD    calaos_server` présent (regex **ancrée**). Le seul cas
    gtest sauté est le même dans les deux images :
    `ConfigRobustnessTest.FailedSaveKeepsThePreviousConfigIntact` (`geteuid() == 0`, conteneur root).
    ⚠️ **Incident d'outillage, sans rapport avec le ticket, à connaître** : le premier essai de
    l'image CI a échoué en `/usr/bin/ld: final link failed: No space left on device` — la copie
    jetable avait été posée sur `/tmp`, **un tmpfs de 32 Go partagé par tous les agents** que les 90
    binaires de test saturent. ⇒ **poser les arbres de build jetables sur le disque**, jamais sur
    `/tmp` ; l'espace a été rendu par un `busybox` monté sur **le chemin exact**.


    ⭐ **Le `# TOTAL` de chaque image égale MON PROPRE compte d'entrées `TESTS`**, recompté `python3`
    sur `tests/Makefile.am` (continuations recollées, `if`/`else`/`endif` empilés) : **95** =
    5 scripts shell hors condition + 89 binaires sous `if HAVE_GTEST` + **1** imbriqué
    `if HAVE_GTEST` → `if HAVE_LIBKNX`, **zéro doublon** ; **94** en CI, où `KNXExternProcWire_test`
    quitte `TESTS` sans un mot faute de `libknx`. **`^if*` 81 / `endif` 81, profondeur finale 0,
    jamais négative, maximum 2 ⇒ une seule imbrication** — inchangé par ce ticket.
    ⭐ **Cas EXÉCUTÉS comptés, pas lus** : **95 `.trs`** dans l'image de dev (94 `PASS` + 1 `SKIP`,
    le `SKIP` étant `run-python-tests.sh`), et **90 binaires gtest / 1595 cas exécutés / 1594
    passés** relevés sur les `.log`. `git status` **vide**, arbre goldens `tests/core/golden`
    **`d4ebc61f…` inchangé, 145 fichiers**, **zéro ligne de `src/`** (10 fichiers touchés : 6 docs,
    `configure.ac`, `tests/Makefile.am` et les 2 scripts Python/shell).
    ⭐ **Le correctif se voit en vrai dans le journal de dev** : `SKIP: run-python-tests.sh`,
    `exit 77`, et la ligne `run-python-tests: suites=3/6 cases=23/42` suivie des trois `NOT RUN` qui
    **nomment les 19 cas** — là où `master` écrivait `PASS` sans un mot.
    Ligne de `configure` relevée dans les deux images : `tests/python/ (pytest + sidecar deps):
    install pytest, fastapi, httpx and colorama to measure tests/python/ (make check will report
    SKIP)`.

  - ⚠️ ⭐ **`F-FLAKY-1` — VU, et il faut le dire : ce n'est plus une observation unique.**
    Le **premier** `make check -j8` de l'image de dev a rendu **`# FAIL: 1`, RC 2** sur
    `core/ShutterImpulse_test`, cas
    `ShutterImpulseTest.PlainImpulseDownKeepsMovingUntilTheRequestedDuration`
    (`core/ShutterImpulse_test.cpp:297`, `sh.isStopped()` **true** alors qu'on l'attend `false` :
    « the shutter stopped after impulse_time instead of the requested duration », 137 ms).
    ⭐ **C'est un cas DIFFÉRENT de celui vu par l'auteur** (`SmartImpulseUp…` sous `-j16`), **de la
    même famille « durée »**. ⇒ **deux observations indépendantes, deux cas différents, deux niveaux
    de parallélisme** : le classement « flottement sous charge » cesse d'être un jugement isolé.
    ⛔ **Le binaire n'a PAS été rejoué jusqu'au vert.** Exclusion de causalité, dans l'ordre :
      1. **diff** — la branche ne touche **aucun `.cpp`, aucun `.h`** ; les trois hunks de
         `tests/Makefile.am` sont en lignes **22-40**, les règles de `ShutterImpulse_test` en
         **2639-2649** ; `configure.ac` n'ajoute qu'une variable de résumé, **aucun `CFLAGS` /
         `CXXFLAGS` / `LDFLAGS`**. ⇒ **aucune entrée de ce binaire ne diffère de `master`.**
      2. **`nm`** — le symbole du cas rouge est bien dans ce binaire
         (`ShutterImpulseTest_PlainImpulseDownKeepsMovingUntilTheRequestedDuration_Test::TestBody()`,
         `T` à `0x46cb0`), `sha256 e4fb6acb…` ; les **80** unités de traduction qu'il cite sont
         **toutes** hors du diff de la branche.
      3. **isolation, sans charge** — le cas seul **6/6 verts**, le binaire entier **3/3 verts** :
         **0 rouge sur 9 passes**.
      4. **autre point de charge** — `make check -j6` (la configuration publiée par la fiche) :
         **RC 0**, `# FAIL: 0`, **un seul** bloc `Testsuite summary`.
    ⚠️ **Ce qui reste vrai et qu'aucune de ces quatre mesures ne prouve** : qu'aucune régression
    n'existe. Elles prouvent que **ce ticket** n'en est pas la cause, et que la rougeur dépend de la
    **charge**, pas du contenu. ⇒ ⚠️ **`core/ShutterImpulse_test` est un test de DURÉE sans horloge
    injectable : il rougira encore, sur n'importe quelle branche, dès que la machine est chargée.**
    **À traiter comme un défaut de test à part entière** (horloge simulée ou marge), **pas comme du
    bruit à relancer** — et surtout **à ne pas imputer au prochain ticket qui le croisera**.


- **🔒 T3.31 ✅ MERGÉ (`2c7e4892`, **9** commits, `git rebase 180c4b87` + `merge --ff-only`,
  historique linéaire, **0 commit de fusion**, `./autogen.sh && ./configure && make -j32 &&
  make check -j16` ⇒ **`# TOTAL: 94 / PASS: 94 / FAIL: 0 / SKIP: 0 / XFAIL: 0 / XPASS: 0 /
  ERROR: 0`**, **un seul** bloc `Testsuite summary`, `exit 0`, **0 `error:`**, `CXXLD  calaos_server`
  **et** `CXXLD  calaos_wago`)** — fermer par le **TYPAGE** le trou des arguments positionnels sur
  les chaînes Wago et Reolink. **RIEN POUSSÉ.**

  - ⭐⭐ **LA CAMPAGNE RA/RB A ÉTÉ REJOUÉE AU MERGE, PAS CRUE SUR PAROLE — 8 passes, et les 8
    rendent le verdict annoncé.** Harnais privé (`/harness`, hors du worktree), copie **pristine**,
    restauration par `open('wb')` **sans préserver les dates** (jamais `cp -p` / `shutil.copy2` :
    l'horodatage préservé laisse des `.o` périmés et fabrique un rouge sur des sources identiques),
    **`cmp` d'application avant chaque compilation**, **`rm -f` de l'objet ET des deux binaires**
    (`calaos_server`, `calaos_wago`) avant chaque passe, et **assertion que l'objet a bien disparu**.

    | # | Site | rc attendu | rc mesuré | message |
    |---|---|---|---|---|
    | **M0** ⭐ témoin, **ensemble VIDE** | — | 0 | **0** | objet reconstruit — sans lui les rouges seraient ininterprétables |
    | **RA** `cameraKey(reg.event_type, reg.hostname)` | `ReolinkCtrl.cpp:117` | 2 | **2** | `no matching function for call to 'ReolinkEventRegistry::cameraKey(const std::string&, const std::string&)'` |
    | **RA′** emballée mais permutée | `ReolinkCtrl.cpp:117` | 2 | **2** | `…cameraKey(ReolinkTypes::EventType, ReolinkTypes::Hostname)` |
    | ⚠️ **RA″** résiduel **W3** | `ReolinkCtrl.cpp:117` | **0** | **0** | ✅ **compile — ATTENDU** : emballer la MAUVAISE variable type-vérifie et le fera toujours |
    | **RB** `dispatch(event_type, hostname, event_data)` | `ReolinkCtrl.cpp:86` | 2 | **2** | `cannot convert 'std::string' to 'const ReolinkTypes::Hostname&'` |
    | **RB′** emballée mais permutée | `ReolinkCtrl.cpp:86` | 2 | **2** | `cannot convert 'ReolinkTypes::EventType' to 'const ReolinkTypes::Hostname&'` |
    | **RC** corps du callback | `ReolinkInputSwitch.cpp:104` | 2 | **2** | `cannot convert 'const ReolinkTypes::EventType' to 'const ReolinkTypes::Hostname&'` |
    | **RD** signature de la lambda | `ReolinkInputSwitch.cpp:98` | 2 | **2** | conversion en `ReolinkCtrl::EventReceivedSignal` refusée |

    ⭐ **`RA″` est bien écrit comme RÉSIDUEL** — au source (`ReolinkTypes.h`, en-tête de
    `ReolinkRegistry_test.cpp`) **et** dans la fiche (§7.6bis) : *mesuré, pas supposé, et déclaré
    plutôt que caché*. Ce qui est fermé est l'**ORDRE**, à tous les sauts ; ce qui reste ouvert est
    le **NOMMAGE**, sur une ligne adjacente au champ qu'elle nomme.
  - ⭐ **LA 12ᵉ VARIANTE DE FAUX VERT (restauration qui échoue en silence) EST DÉSARMÉE PAR UN
    COMPTE, pas par une intention** : chaque restauration relit le fichier et le compare à la copie
    pristine, et le harnais sort en `rc=97` si un seul octet diffère. **Total mesuré : 18 fichiers
    effectivement restaurés** (9 passes × 2 fichiers) — **non nul**, donc les mutations ont bien été
    jouées sur un arbre restauré.
  - ⭐ **La surcharge à UN argument : TOUS les appelants internes qui tiennent une
    `CameraRegistration` l'utilisent** — `ReolinkCtrl.cpp:117`, `:159` et
    `ReolinkEventRegistry::add()` (`:135`). Les **trois** appels à deux arguments qui restent
    (`dispatch` `:189`, `hasCamera` `:208`, `callbackCount` `:214`) transmettent des **paramètres
    déjà typés** : là non plus il n'y a **ni ordre ni emballage** à se tromper. Le **seul** site
    d'emballage de la classe est le corps de la surcharge (`:129-130`), sur les deux lignes qui
    nomment les deux champs.
  - ⭐ **Les DEUX angles morts de la sonde sont écrits AVEC la raison pour laquelle aucun ne mord
    ici** (`tests/ReolinkRegistry_test.cpp`, cas `TheAggregateProbesActuallyDiscriminate`) :
    **(a) référence lvalue** — `LieRef(std::string &, std::string &)` : les deux sondes répondent
    **false** (« fermé ») alors que `is_constructible<LieRef, string&, string&>` vaut **true** ;
    ⇒ *ne mord pas ici* car **toute enveloppe livrée prend sa charge PAR VALEUR**, et c'est
    **vérifié** (`is_constructible_v<Hostname, string&>`), pas affirmé en prose.
    **(b) rétrécissement** — **ni** les accolades **ni** la direct-init ne distinguent
    `Narrowing(unsigned short)` de `NoNarrowing` explicite ; **seul `is_convertible_v`** le fait ;
    ⇒ *ne mord pas ici* car la sonde à accolades n'est pointée que sur les enveloppes `std::string`,
    et les enveloppes numériques Wago sont épinglées par `is_invocable_v`/`is_convertible_v` dans
    `WagoWire_test.cpp`.
  - ⭐ **Les 11 signatures Wago du chemin RETOUR sont NOMMÉES une par une**, aux trois endroits
    promis — `IO/Wago/WagoTypes.h:97-106`, `T3.31` §7.11.5 et **`T3.46` partie B §6.2** (tableau
    numéroté 1→11, fichier et ligne, paire permutable) : `WagoMap::WagoModbusReadHeartbeatCallback`,
    `WIAnalog::`/`WITemp::`/`WOAnalog::`/`WODigital::`/`WIDigitalBase::`/`OutputAnalog::WagoReadCallback`,
    ⭐ **`WOAnalog::WagoWriteCallback`**, `WODigital::`/`WOVoletBase::`/`OutputAnalog::WagoWriteCallback`.
    **Report justifié par le PÉRIMÈTRE** (10 fichiers, dont `IO/OutputAnalog.h`, base **générique
    hors arbre Wago**) **et par la mesure** : les écritures portent une valeur **constante** au seul
    endroit qui l'émet (`WagoMap.cpp:219` littéral `false`, `:242` littéral `0`) ⇒ une permutation
    y substitue un **mannequin**. **Traiter les lectures d'abord.**
  - ⚠️ ⭐ **`F-FLAKY-1` NON RENCONTRÉ à ce merge** — `make check -j16` rend **94/94, FAIL 0** du
    premier coup, **aucune relance**. Et l'exclusion de causalité est reproduite **plus fortement
    que par `nm`** : `core/ShutterImpulse_test` se lie à **35** objets `CORE_SERVER_OBJECTS` + ses
    3 sources, et **aucun** ne correspond aux **21** fichiers `src/` du diff (appariement `python3`
    par nom de base) ; `nm -C` confirme **0 symbole** du ticket sur **6938**.
    ⭐ **Et le point que l'auteur n'avait PAS montré est mesuré ici** : puisque **ni le binaire ni
    aucune de ses entrées de lien ne diffèrent de `master`**, ce binaire **EST** celui de `master`.
    Exercé **20 fois de suite sous contention artificielle** (12 boucles occupées + 3 autres agents
    en train de builder) : **20 PASS / 0 FAIL**. ⇒ le flottement est **rare**, il appartient à
    `master` (`ShutterImpulse_test:384`, course d'**horloge murale**, ticket T3.34) et **pas** à
    T3.31 — mais **il n'est pas reproduit ici**, donc il n'est ni confirmé ni infirmé sur `master` nu.
  - **Conflits : DEUX, tous les deux dans la doc** (le rebase de `src/` et de `tests/` passe seul).
    `FINDINGS.md` : master ajoute `## T3.45` et la branche `## T3.31` **au même endroit** ⇒
    **les deux côtés gardés, chacun sous son propre titre `##`**, séparés par `---` ; **71 titres
    `^## `** après résolution, **0 marqueur résiduel**. `BOARD.md` : les deux côtés ajoutent une
    ligne `T3.46` (master la version courte de `78c02589`, la branche la version longue « DEUX
    moitiés ») ⇒ **la version de la branche gardée**, insérée **triée par NUMÉRO** entre `T3.45` et
    `T3.48`. ⚠️ **`tests/Makefile.am` n'est PAS dans le diff** — le piège de l'`endif` avalé ne
    pouvait donc pas se produire, **recontrôlé quand même** : **81 `^if*` / 81 `endif`**, profondeur
    finale **0**, **minimum 0, jamais négative**, **94 entrées `TESTS` sans doublon**, et
    **`# TOTAL: 94` du build = ce compte**.
  - **Cas réellement EXÉCUTÉS, recomptés dans les `.log`** (un PASS ne prouve pas qu'une suite a
    tourné, `F-PYTEST-1`) : **1595 cas gtest sur 90 binaires**, **aucune suite à 0 cas**, **aucun
    `[  FAILED  ]`**. L'écart avec les **1585** de `master` est **exactement +10**, et il se
    décompose : `ReolinkRegistry_test` **8 → 13**, `WagoWire_test` **31 → 35**, `ReolinkWire_test`
    **17 → 18**. **95 `.log`** = 90 gtest + 4 scripts `.sh` + `test-suite.log`, pour **94** entrées.
    **91 lignes `CXXLD` ancrées** (`^\s\sCXXLD\s+\S+$`, **double espace**, chemin **relatif à
    `tests/`**) dans le journal de `make check`, + **11** au build.
  - **Recomptes de la revue REVÉRIFIÉS après rebase** : `ReolinkRegistry_test` **13** cas `^TEST(`,
    **9** cas `…IsExactlyThisByteString` (8 dans `WagoWire_test` + 1 dans `ReolinkWire_test`) et
    ⭐ **les 9 corps sont octet pour octet identiques à ceux de `master 180c4b87`** (extraction par
    appariement d'accolades en `python3`, **9/9**), **35** objets `CORE_SERVER_OBJECTS`, **neuf**
    commits (`git rev-list --count`), board **📋 → 🚚 → ✅**.
  - **Goldens : `d4ebc61fb2b1876f587d075a0cb050750dc1876f`, 145 fichiers, identique à `master` et à
    la base — AUCUN bougé** (comparé par **hachage d'arbre**, pas par comptage). `git status -uall`
    du worktree de merge **vide** après la campagne et le rebuild.
  - ⛔ **Reste ouvert et déclaré, pas refermé en douce** : `T3.46` **📋** (partie A `libmbus`,
    partie B les 11 callbacks du chemin RETOUR), la cible 0 `HueWire::LightState`, les cibles 4 et 5,
    le décodeur typé de `WagoExternProc_main.cpp` (**aucun numéro attribué**), et l'écriture par
    champ de `CameraRegistration` une fois construite.
  - ⚠️ **CE DONT LE MERGE N'EST PAS SÛR** : rien n'a parlé à un automate Wago ni à une caméra
    Reolink réels (l'invariance d'octets vient des suites) ; rien n'a tourné sous ASan/valgrind ;
    `F-FLAKY-1` n'a **pas** été reproduit, donc son caractère « aussi présent sur `master` nu » reste
    **argumenté par l'identité du binaire**, pas par une observation d'échec ; et les mesures de la
    fiche antérieures au 4ᵉ rebase (93/93, blob `e2498c216dff`) n'ont pas été réécrites dans
    `T3.31.md` — elles restent vraies **de leur base**, le board porte les valeurs à jour.

- **🔒 T3.45 ✅ MERGÉ (`ac95e8e0`, **6** commits, `git rebase 55beb79b` + `merge --ff-only`,
  historique linéaire, **0 commit de fusion**, `./autogen.sh && ./configure && make -j32 &&
  make check -j32` ⇒ **`# TOTAL: 94 / PASS: 94 / FAIL: 0 / SKIP: 0 / ERROR: 0`**, **un seul** bloc
  `Testsuite summary`, `exit 0`, **0 `error:`**, `CXXLD  calaos_server`)** — `make dist` était mort
  depuis **18 mois** : `EXTRA_DIST` pointait un niveau trop profond dans `src/lib/calaos-python`, et
  **la CI ne lance ni `dist` ni `distcheck`**. **RIEN POUSSÉ.**

  - ⭐⭐ **UNE RÉSERVE TROUVÉE PAR LE MERGE, ET FERMÉE DANS LE MÊME LOT — l'oracle rendait un FAUX
    ROUGE, ce que ce ticket interdit précisément.** Le motif `[A-Za-z0-9_]*_SOURCES` attrapait
    **`BUILT_SOURCES`**, qui n'est pas une primaire. `src/bin/calaos_mcp/Makefile.am` déclare
    `BUILT_SOURCES += calaos_mcp` sous `if HAVE_PYTHON_MCP` ; `calaos_mcp` est **généré** par
    `config.status` depuis `calaos_mcp.in` et listé dans `CLEANFILES`. ⇒ dans **tout srcdir jamais
    construit** — un `git clone` frais, et le srcdir que `distcheck` monte en lecture seule —
    `check-extra-dist` sortait **RC 1**, `1 missing`.
    **Mesuré, pas argumenté** : `make distdir` **ne meurt pas** dessus, il le **construit**
    (`distdir: $(BUILT_SOURCES)` dans le `Makefile.in` généré) et **ne l'expédie jamais** — le
    distdir produit contient `calaos_mcp.in` et **pas** `calaos_mcp`, et `DIST_SOURCES` y vaut la
    chaîne vide. Rougeur sans défaillance derrière ⇒ **faux rouge caractérisé**.
    ⭐ **D'où venait le « 0 manquant » de la fiche** : la mesure d'origine a été prise dans un
    worktree **déjà configuré et construit**, où le fichier généré existait. C'est **exactement la
    même erreur d'arbre contaminé** que le `calaos-git/` du §3.4 que le ticket avait su corriger —
    mais par le **build** au lieu d'un `distcheck` échoué, donc invisible à la même vigilance.
    ⚠️ **Et cela falsifiait le §3.3 point 3** : `calaos_mcp` **était** le fichier généré,
    conditionnel et listé hors `nodist_*` que ce point décrivait comme un risque **théorique**.
    Le faux rouge n'était pas latent, il était **réalisé**.
    **Correctif** (`ac95e8e0`) : `NOT_PRIMARY = {'BUILT_SOURCES', 'DIST_SOURCES'}`, écartées avant
    le motif ⇒ **847 → 846** vérifiés, **402 → 401** conditionnels, **0 manquant**, et le compte est
    désormais **identique en arbre construit et en arbre jamais construit** — la propriété qui
    manquait. Arbre pristine (`git archive HEAD` + `tar -m`) : **RC 0**.

  - ⭐ **M1–M4 rejouées, les quatre rougissent (RC 1), et la correspondance de M3 est VÉRIFIÉE POUR
    DE VRAI** — c'est le point qui comptait, un oracle qui rougit sans que `distdir` meure ne
    vaudrait rien. Dans l'arbre construit : `make[7]: *** No rule to make target
    'calaos_extern_proc/absent.h', needed by 'distdir-am'.  Stop.`, **RC 2**. Messages relevés :
    `EXTRA_DIST … nosuchthing*.foo` (M1), `$(top_srcdir)/nope/gone.txt` (M2),
    `noinst_HEADERS … calaos_extern_proc/absent.h` (M3), `$(T345DIR)/absent.py` (M4).
  - **A0 témoin rejoué en arbre pristine JAMAIS CONSTRUIT** (et non seulement « propre ») :
    **RC 0, ensemble rouge VIDE**. `A1` et `A2` : **RC 1** chacun.
    ⛔ **La fiche annonçait que le message distinguait A1 de A2 par « `calaospython_PYTHON` vs
    `EXTRA_DIST` » : c'est FAUX** — les deux bras mutent `EXTRA_DIST`. **Le seul discriminant est le
    `Makefile.am` nommé**, et il suffit :
    `src/lib/calaos-python/Makefile.am:` vs `tests/Makefile.am:`. Corrigé dans la fiche et au board.
  - ⛔ **Autre erreur de rédaction corrigée** : la fiche **et** le board attribuaient la faute
    d'origine à `calaospython_PYTHON`. **C'était `EXTRA_DIST` depuis toujours** — vérifié sur les
    deux **seuls** commits du fichier, `86da9b1b` (2025-02-16) et `06d799fe`, où
    `calaospython_PYTHON` vaut bien `$(pythonsrcdir)/calaos_extern_proc/…`. La **conclusion** du
    ticket (18 mois, `dist` mort, correctif d'une ligne) est **inchangée** ; seul le nom de la
    variable l'était.
  - ⭐ **`_MANS` EST exercé sur ce dépôt**, contrairement à ce que la revue supposait :
    `src/lib/libquickmail/Makefile.am:65` porte `dist_man_MANS = man1/quickmail.1`, et le chemin
    existe. Restent **non exercés** faute de déclarant : `_TEXINFOS`, `_LISP`, `_JAVA` — dans le
    motif par conformité à automake, **pas** par mesure. C'est écrit dans la fiche.
  - **`$(PACKAGE)-*` : 13 `Makefile.am` et non 12** — l'ancrage au niveau supérieur **plus** la
    confirmation par le `configure.ac` du candidat préservent bien `src/lib/calaos-python`, qui
    commence pourtant par `calaos-`. Recompté nommément : les 13 chemins sont sortis un par un.
  - **Comptes recomptés en `python3`** (le hook `rtk` réécrit `git`/`grep`/`awk`) : **846** chemins
    (401 conditionnels, **3** sautés, **849** jetons appariés), **13** `Makefile.am`, **0** manquant.
    L'écart avec les **834/389** de la branche est **entièrement** `T3.25`, mergée entre-temps :
    **4 suites** de plus dans `tests/Makefile.am`, **12 `_SOURCES`** sous `if HAVE_GTEST`, d'où
    **+12** au total **et** au compteur conditionnel.
  - **Conflits : AUCUN.** Le rebase sur `55beb79b` passe seul sur `tests/Makefile.am`, `BOARD.md` et
    `FINDINGS.md`. ⚠️ **C'est précisément le cas où le piège de l'`endif` mord en silence** — donc
    recontrôlé plutôt que supposé : `tests/Makefile.am` porte **81 `^if*` / 81 `endif`** (une seule
    forme, `if`), profondeur finale **0**, **minimum 0, jamais négative**, **94** entrées `TESTS`
    **sans doublon** (93 de `master` + `check-extra-dist.sh`), **`# TOTAL 94`** au build : les deux
    moitiés de la règle de lecture concordent. `FINDINGS.md` : le bloc `## T3.45` s'ajoute **sous
    son propre titre `##`**, sans toucher ceux de `T3.25`.
  - **Cas réellement exécutés** (`.log`/`.trs` **effacés avant** le passage, donc aucun PASS de
    cache) : **1585 cas gtest** sur **90** binaires, **0 `[  FAILED  ]`**, **23 cas Python**
    (`Ran 23 tests`), **94 `.trs`** reconstruits. *(La fiche annonçait 1533 sur sa base : les
    **+52** sont les 4 suites de `T3.25`.)*
  - **Goldens : `d4ebc61f`, 145 fichiers, identique à `master` et à la base — AUCUN bougé.**
    `git status -uall` du worktree de merge **vide**, **aucun `calaos-git*` résiduel**.
  - ⚠️ **F-DIST-3 reconfirmé en passant** : le `make distdir` de la campagne a réécrit
    `po/calaos.pot` **et 7 `.po`** — `de`, `es`, `fr`, `hi`, `nb`, `pl`, `ru` — **`en.po` intact**,
    exactement le compte corrigé de la fiche. `git checkout -- po/` appliqué.
  - **[`T3.48`](T3.48.md) créée et ouverte** (archive inconstructible : `exprtk.hpp`, `uvw/*.hpp`),
    ligne au board **après** `T3.45`, tri par numéro respecté. **Aucun autre numéro n'a été
    ouvert** — `T3.46`/`T3.47` n'existent ni comme fiche ni comme référence.

  - **NON VÉRIFIÉ, à ne pas surestimer** : les chiffres d'archive du §4.2 — **415/126** absents,
    `sole.cxx` comme seul discriminant du 125↔126, **838** entrées de tarball, **145/145** goldens
    dans l'archive — **n'ont PAS été rejoués** : ils demandent un `make dist` complet, hors
    périmètre d'un merge. Ils restent ceux de la branche, sur la base `1c6ab7a9`. De même le
    **coût 33,7 ms** (n=20) n'a pas été remesuré, et le `distcheck` du §4.1 n'a pas été relancé —
    seul `make distdir` l'a été, ce qui suffisait à M3.

- **🔒 T3.34 ✅ MERGÉ (`d68aa1f3`, 7 commits, `merge --ff-only` sur `4e4b6226` — **master n'avait
  pas bougé depuis le rebase de l'auteur, donc ni rebase ni conflit**, historique linéaire,
  `./autogen.sh && ./configure && make -j12 && make check -j6` ⇒ **88/88**, `exit 0`, **0
  `error:`**, `CXXLD    calaos_server`)** — `impulse down <ms>` retirait **11** caractères d'un
  préfixe de **13**, plus l'échéance d'impulsion qui débordait aux deux bouts et un one-shot
  survivant à l'IO. **RIEN POUSSÉ.**

  - ⭐⭐ **LE PIÈGE DU `endif` N'A PAS MORDU AU MERGE — parce qu'il avait déjà été désamorcé.**
    L'auteur l'avait rencontré **à son rebase** (T3.27 venait d'appender `LuaCalaosApi_test`
    exactement à l'endroit visé, et le `endif` du dernier bloc est en contexte commun **au-dessous**
    des marqueurs : « garder les deux côtés » laisse la profondeur à **1** ⇒ `automake:
    unterminated conditionals`). **Master n'a pas rebougé depuis**, donc aucun conflit à rejouer.
    ⇒ **Ce qui a été vérifié est la RÉSOLUTION, pas le conflit** : recompté en `python3` sur
    **tous** les préfixes `^if*` (il n'y en a qu'un seul dans ce fichier, `if HAVE_GTEST`) —
    **76 `if` / 76 `endif`, profondeur finale 0, minimum 0 (jamais négative), maximum 2**, sur
    master **comme** sur la branche. La forme livrée est un bloc `if HAVE_GTEST … endif`
    **complet et autonome** appendu après celui de T3.27, **pas** une fusion des deux côtés.
    ⚠️ **La leçon reste armée pour le prochain ticket qui appendra en fin de `tests/Makefile.am`.**

  - ⭐ **CONTRE-MUTATIONS REJOUÉES AU MERGE** (protocole complet : `rm -f` des **deux** `.o` serveur
    **et** du binaire, `make` **à la RACINE** puis `make -C tests` — `F-TEST-2` —, ligne
    `CXXLD    core/ShutterImpulse_test` exigée par **regex** (double espace), **jugement au code de
    sortie, jamais aux lignes rouges**) :
    **M0 témoin ⇒ ensemble rouge VIDE, `exit 0`** · **M1 (longueur du jumeau sur `OutputShutter`)
    ⇒ 6 rouges, TOUS `Plain*`, `exit 1`** · **M2 (idem `OutputShutterSmart`) ⇒ 4 rouges, TOUS
    `Smart*`, `exit 1`** ⇒ **M1 ∩ M2 = ∅**, les deux sites jumeaux sont bien couverts par des
    ensembles disjoints · **garde de vie retirée ⇒ `exit 139` et ZÉRO ligne `FAILED`** — le faux
    vert par mort du binaire, reproduit tel quel.
  - ⭐ **La ⛔ CORRECTION du §9 de la fiche est CONFIRMÉE EN EXÉCUTION, pas seulement relue** : la
    garde a été retirée dans le fichier **`Plain`**, et le processus est mort dans un cas
    **`Smart`** (`t334_smart_down_timer`, dernière ligne du journal). ⇒ **le site de mort désigne le
    test en cours, jamais le code muté** ; la preuve primaire reste le couple *(code 139, quelle
    garde exactement a été retirée)*.

  - **L'UAF est dit comme une AGGRAVATION, jamais comme une régression** — vérifié aux deux
    endroits qui décident si c'est un défaut livré : `T3.34.md` §3 (« ⭐⭐ L'UAF **PRÉEXISTE SUR
    MASTER** — ce ticket l'élargit, il ne le crée pas », avec le chemin `impulse up 5000` +
    `deleteIO()` qui l'ouvre **déjà sur master**, la branche `up` ayant toujours eu la bonne
    longueur) et la ligne `BOARD.md`.
    ⚠️ **ÉCART TROUVÉ ET FERMÉ AU MERGE : `RELEASE_NOTES.md` n'en disait RIEN DU TOUT** — ni comme
    aggravation ni comme régression. La note couvrait la durée, les deux cas limites et la fuite de
    handle, mais **pas le plantage**, alors que le correctif le referme et que la note a toute une
    famille d'entrées « plus de plantage… ». **Paragraphe ajouté au merge**, formulé
    **explicitement comme préexistant** (« ce défaut n'a pas été introduit par la correction
    ci-dessus — il existait déjà »).

  - **Recomptes refaits, aucun cardinal recopié** (tout en `python3`, le hook `rtk` réécrivant
    `git`/`grep`/`awk`) : **88 entrées `TESTS`, aucun doublon**, 86 `check_PROGRAMS` ;
    balayage `compare(0, N, "littéral")` **master 50 / 3 fautifs** → **branche 46 / 1** (le résidu
    est bien `OutputLightRGB.cpp:107`, F-RGB-1, hors périmètre) ; **145 goldens**, arbre
    `tests/core/golden` = **`d4ebc61f`** identique sur master, sur la branche et après merge —
    **zéro golden bougé**.
  - **La vraie boucle uvw sous `CoreFixture` ne laisse rien derrière elle** : `git status` du
    worktree de merge **vide** après `make check` (**0 fichier suivi modifié ET 0 non suivi**),
    **0 ligne `HistLogger`/`sqlite`** dans les journaux, et la suite a été **rejouée 4 fois au
    total** (1 build + 3 passes) — **88/88, `exit 0`, aucun scintillement**.
  - **Réserves de revue vérifiées comme fermées** : `F-RGB-1` **barrée par un bloc ⛔ CORRECTION**
    (jamais réécrite en silence) et **versée à `F-LINK-1` comme apport n°6**, avec les trois entrées
    mesurées (`JsonApi.cpp:774`, `ActionStd.cpp:152`/`:207`, `ScriptBindings.cpp:191`) ; commentaire
    de `tests/core/CalaosCoreFixture.h` corrigé (« No libuv loop runs in the tests » était devenu
    faux). ⭐ **[`T3.40`](T3.40.md) ouverte** (UAF généralisé, **20 sites non gardés / 12 classes**,
    dont **5** dérivent d'`IOBase`), ligne `BOARD.md` présente et **triée par NUMÉRO** (après
    T3.39). **Numéro `T3.40` revérifié LIBRE en `python3`** — aucune autre branche ni aucun worktree
    vivant ne le revendique. Les **deux corrections de la revue** tiennent au source :
    `Foscam.cpp:125` **n'en est pas** (T2.19 a déjà sorti la capture dans un `bool insecure` local,
    la lambda `[=]` ne capture plus `this`) ⇒ **5 classes, pas 6** ; et filtrer sur « dérive de
    `trackable` » **sous-compte** (`trackable` ne couvre que les `mem_fun`, jamais une lambda).

- **🔒 T3.28b ✅ MERGÉ (`e135fd48`, 5 commits, `merge --ff-only` sur `b7a4c63d` — **master n'avait
  pas bougé depuis le rebase de l'auteur, donc ni rebase ni conflit, PAS MÊME sur l'append de
  `FINDINGS.md` que l'auteur avait rencontré à SON rebase**, historique linéaire, 0 commit de
  fusion, `./autogen.sh && ./configure && make -j12 && make check -j6` ⇒ **89/89**, `exit 0`,
  **0 `error:`**, `CXXLD    calaos_server`)** — les deux tripwires de source de `T3.28` étaient
  justifiées par une inatteignabilité **qui n'existe pas** ; elles sont doublées par de vrais
  tests d'exécution. **RIEN POUSSÉ.**

  - ⭐⭐ **`MR4` REJOUÉE AU MERGE : ELLE ROUGIT, ET ELLE SEULE.** Protocole complet à chaque passe
    (`cmp` d'application, `rm -f` de `Audio/RoonPlayer.o` **et des DEUX binaires**, `make` **à la
    RACINE** — `F-TEST-2` —, `CXX      Audio/RoonPlayer.o` **et** les deux `CXXLD` exigées par
    **regex** à double espace, jugement au **code de sortie** et au **nombre de cas exécutés**) :
    **`RoonCtrl::Instance(host, port)` ⇄ `Instance(host, RoonArgs::DefaultPort)` à
    `RoonPlayer.cpp:227` ⇒ `core/RoonArgs_test` **14/14, `exit 0`** (aveugle, comme annoncé) et
    `core/RoonSpawnViaPlayer_test` **`exit 1`**, un seul rouge nommé,
    `RoonSpawnViaPlayerTest.TheSidecarIsLaunchedWithTheCoreTheIoWasConfiguredWith`.
    ⇒ **le bug de terrain de `T3.28` déplacé d'un site est bien attrapé**, et le second binaire
    achète quelque chose qu'aucun oracle du premier n'avait.
  - **`MR4b` (`Instance(host, port)->subscribeZone`, `:232`) SURVIT — l'argument d'équivalence
    tient, VÉRIFIÉ AU SOURCE et pas cru sur parole** : `:227` est un appel **inconditionnel**
    exécuté avant l'armement du `Timer::singleShot` de `:230-233`, et `Instance()` est un static
    de fonction (`RoonPlayer.cpp:86`) ⇒ le second appel rend l'objet **déjà construit** et jette
    ses arguments. **Son point de rupture est écrit** dans la fiche (§5) : le jour où `:227`
    disparaîtrait ou passerait derrière une condition, `:232` deviendrait le créateur et la
    mutation cesserait d'être équivalente — et **`MR4` couvre exactement ce jour-là**.
  - ⭐ **RÉSIDU REMESURÉ INDÉPENDAMMENT, avec un TÉMOIN qui prouve que la sonde voit quelque
    chose** — sous **PID 1 = `sleep infinity`** (l'environnement de build des agents, celui où
    personne ne récolte les orphelins), 10 passes des **deux** binaires, comptage `python3`
    (répertoires `/tmp/calaos_roon_spawn_*`, sockets `/tmp/calaos_proc_*_roon_*`, états `Z` lus
    dans `/proc/*/stat`) :
    **livré ⇒ DELTA `0 / 0 / 0`** · **`teardown()` neutralisé ⇒ DELTA `+20 / +20 / +20`** pour
    20 exécutions, soit **+1/+1/+1 par exécution**, la fuite linéaire exactement telle que la
    fiche la décrit. ⇒ c'était bien **notre propre environnement de build** qui trinquait.
  - **`--gtest_repeat=2` REFERMÉ** : `core/RoonArgs_test` **14 + 14 OK, `exit 0`** et
    `core/RoonSpawnViaPlayer_test` **1 + 1 OK, `exit 0`** (itérations 1 et 2 comptées séparément,
    aucune ligne `FAILED`). Le piège est écrit **aux deux endroits** exigés — en-tête de
    `tests/core/RoonArgs_test.cpp` **et** en-tête de `tests/core/RoonSpawnHarness.h`, plus la
    fiche §3(c) — **avec sa limite** : les suites `core/` **restent non rejouables** dans un même
    processus (`CalaosCoreFixture.h`, singletons sans API de remise à zéro), le harnais ne ferme
    que **sa** contribution.
  - **Témoin à ensemble VIDE et cas COMPTÉS, jamais déduits d'un code de sortie** (`F-PYTEST-1`) :
    **15 cas exécutés à CHAQUE passe** (14 + 1) — M0, MR4, MR4b, sans exception — donc aucun
    binaire mort. M0 : **0 rouge, `exit 0`** sur les deux binaires.
  - **`src/` : prose SEULE, vérifié en `python3`** et non relu — les trois fichiers ont été
    dépouillés de leurs commentaires (machine à états chaînes/caractères comprise) et comparés
    à `b7a4c63d` : **25 / 315 / 169 lignes de code, IDENTIQUES des deux côtés**. Et
    **`startProcess(` reste à 1 / 1 / 0 occurrence**, inchangé — **aucune tripwire faussée**.
  - **`F-LINK-1` v2 : les TROIS membres sont présents** dans `FINDINGS.md` (équivalence
    observationnelle légitime **seulement prouvée par mutation** · « on ne peut pas atteindre » à
    rejouer · ⭐ « atteignable, personne n'a regardé » **à rejouer LUI AUSSI**, le seul verdict
    qui autorise à *déplacer* une tripwire — et c'est exactement ce qui avait mordu la première
    livraison). La **7ᵉ affirmation** (wire KNX, remesurée indépendamment par le relecteur) est
    versée, et les **trois non rejouées sont listées nommément** :
    `ReolinkCtrl::doRegisterCamera`, le `brace-init` de `ReolinkEventRegistry`, `WagoMap`.
  - **Recomptes refaits en `python3`, aucun cardinal recopié** : **89 entrées `TESTS`** (aucun
    doublon ; 86 binaires + 3 scripts `.sh`), **87 `check_PROGRAMS`**, et le `# TOTAL` du harnais
    automake dit **89** — les deux chiffres coïncident. `tests/Makefile.am` : **77 `^if*` / 77
    `endif`**, **profondeur finale 0, jamais négative**, et **un seul préfixe `if` dans tout le
    fichier** (ni `ifdef` ni `ifeq`) ; le bloc du ticket est un `if HAVE_GTEST … endif` **complet
    et autonome**, appendu après celui de `T3.34`, **pas** une fusion avec le voisin.
  - **145 goldens, arbre `d4ebc61f…` IDENTIQUE à master, aucun bougé** ; `git status` du worktree
    de merge **vide** en fin de campagne (**0 fichier suivi modifié ET 0 non suivi**), sources
    restaurées depuis la pristine **et rebâties** (`F-TEST-2`).
  - ⚠️ **Ce dont je ne suis pas sûr, dit franchement** : les deux cas d'exécution **`spawn` un
    processus** et sont donc les premiers suspects si `make check` devient flottant sous forte
    parallélisation — la revue avait passé 215 exécutions sous charge sur la version à **un**
    binaire, **le second binaire n'a jamais été soumis à cette campagne-là** (ni par l'auteur, ni
    ici : mes 10+10 passes de résidu ne sont pas une campagne de charge). Et **aucun essai sur un
    core Roon réel** n'a été fait, ici comme dans `T3.28`.

- **🔒 T3.37 ✅ MERGÉ (`cab9e0a8`, 6 commits, `merge --ff-only` sur `701a98e4` — **master n'avait
  pas bougé depuis le rebase de l'auteur : ni rebase ni conflit, PAS MÊME sur l'append de
  `FINDINGS.md`**, historique linéaire, **0 commit de fusion**, `./autogen.sh && ./configure &&
  make -j12 && make check -j6` ⇒ **`# TOTAL: 89 / PASS: 89 / FAIL: 0`**, **un seul** bloc
  `Testsuite summary`, `exit 0`, **0 `error:`**, `CXXLD    calaos_server`)** — le parseur de
  `path` JSON, copié entre `MqttCtrl` et `WebCtrl`, n'existe plus qu'une fois dans
  `IO/JsonPath.h`. **RIEN POUSSÉ.**

  - ⭐⭐ **LES TROIS MUTATIONS DEMANDÉES REJOUÉES AU MERGE, ET L'EXCLUSIVITÉ EST STRICTE — vérifiée
    NOM PAR NOM, pas sur un cardinal.** Protocole complet à chaque tour (`cmp` d'application — le
    motif exactement **une** fois et le fichier sur disque **différent** du pristine posé dans le
    conteneur —, `rm -f` du binaire **et** de `tests/JsonPathSyntax_test-JsonPathSyntax_test.o`,
    `make -j12` **à la RACINE** puis `make -j6 check TESTS=`, ligne **`CXXLD    JsonPathSyntax_test`**
    exigée par **regex à double espace**, verdict au **code de sortie du binaire lancé directement**
    et au **nombre de cas exécutés**) :
    **M1** (corps **partagé**, `val.back() != ']'` ⇄ `val.front() != '['`) ⇒ **7 rouges des DEUX
    côtés** — `AnIndexMissingItsClosingBracketIsRejected`/`…IsLogged` **×2**, `EveryFailure…` **×2**,
    `JsonPathResolve.TheFrozenIndexGrammarIsUnchanged` ; **M6** (enveloppe **MQTT**) ⇒ **3, TOUS
    `Mqtt*`** ; **M7** (enveloppe **Web**) ⇒ **14, TOUS `WebJsonPathTest.*`**.
    ⭐ **M6 ∩ M7 = ∅** : aucun cas Web dans M6, aucun cas MQTT dans M7. Les deux appelants sont donc
    câblés **séparément et tous les deux**, ce qu'aucune des deux moitiés n'établirait seule.
  - **Témoin M0 à ensemble VIDE, joué trois fois** (avant campagne, après, et en tête de la reprise
    de la sonde de relink) : **63 cas, 6 suites, 0 rouge, `exit 0`** à chaque fois — et **63 cas
    comptés**, jamais déduits d'une absence de `FAILED`.
  - ⭐⭐ **LE FAUX ROUGE DE RELINK EST REPRODUIT À L'IDENTIQUE, et la protection par `.deps` est
    CONFIRMÉE — les deux mesures, pas une.** `JsonPathSyntax_test_DEPENDENCIES` reste écrasé à
    `libcalaos_common.la` seul :
    **(1)** mutation M6 de `MqttCtrl.cpp` **+ `rm -f`** ⇒ `CXXLD` **1**, `MqttCtrl.o` recompilé,
    `exit 1`, **3 rouges** — honnête ;
    **(2)** ⭐ **restauration PRISTINE, SANS aucun `rm -f`** ⇒ `CXXLD` **0**, `MqttCtrl.o`
    **pourtant recompilé**, `.o` de test **non** recompilé, **les 3 MÊMES rouges sur un arbre
    PROPRE** — **FAUX ROUGE**, variante symétrique et plus traître du faux vert ;
    **(3)** même source pristine **avec `rm -f`** ⇒ `CXXLD` **1**, **0 rouge**.
    **(4)** ⭐ **mutation de `JsonPath.h` SANS aucun `rm -f`** ⇒ `.o` de test **recompilé**, `CXXLD`
    **1**, `exit 1`, **2 rouges, un par copie de l'appelant** ⇒ **l'en-tête n'est PAS dans le trou**,
    la revue avait le mécanisme à l'envers et l'auteur a raison de la corriger.
    ⭐ **Le contraste (2) / (4) isole la cause** : dans les deux tours le `.o` serveur est recompilé,
    et seul (4) relie — parce que seul (4) recompile le `.o` **de la cible**, `JsonPath.h` étant dans
    son `.deps` par l'`#include "JsonPath.h"` de `tests/JsonPathSyntax_test.cpp`.
    ⚠️ **Donc la protection est bien INCIDENTE, pas conçue** : retirer cet `#include` — ce que le §4
    de la fiche pousse à faire — y précipite l'en-tête **sans rien signaler**. Conclusion pratique de
    la revue **maintenue**, mécanisme **corrigé**.
  - ⭐ **M8 EST UN MUTANT ÉQUIVALENT LÉGITIME, ET C'EST PROUVÉ PAR SON CONTRÔLE, pas affirmé.**
    L'**initialiseur** `int idx = 0` → `int idx = 7` ⇒ **0 rouge, `exit 0`** ; le **contrôle** sur
    l'**affectation gardée** `idx = 0` → `idx = 7` ⇒ **13 rouges des DEUX côtés** (6 `Mqtt*`,
    6 `Web*`, plus `TheFrozenIndexGrammarIsUnchanged`). ⇒ **la suite observe bel et bien `idx`** :
    le 0 de M8 mesure l'**inatteignabilité** de l'initialiseur, pas l'aveuglement de la suite.
    C'est ce que la fiche §5.5 affirme, et le contrôle qui le démontre est **M3** de sa propre table
    (`idx = 0` ⇄ `idx = 1`, **13 rouges**). ⚠️ **Seul écart de rédaction relevé** : la fiche ne
    **relie pas explicitement** M8 à ce contrôle — les deux lignes sont dans la même table, la
    déduction est laissée au lecteur. **Rédactionnel, aucune mesure en défaut.**
  - **`tests/Makefile.am` — le piège du `endif` est sans objet et le reste après recompte** :
    **31 lignes ajoutées, TOUTES des commentaires** (vérifié en `python3` : 31 `+`, **0 `-`**, zéro
    ligne ajoutée ne commençant pas par `#`), **aucune règle, aucune entrée `TESTS`, aucun
    `if`/`endif`** ; ⭐ **`_DEPENDENCIES` NON touchée** — vérifié : le mot n'apparaît dans aucune
    ligne ajoutée ni supprimée. Invariants recomptés **tous préfixes `^if*` confondus** :
    **77 `if*` / 77 `endif`**, profondeur finale **0**, **minimum 0**, maximum 2 ; **89 entrées
    `TESTS`** (3 hors condition + 85 `HAVE_GTEST` + 1 `HAVE_GTEST && HAVE_LIBKNX`), **89 uniques,
    zéro doublon** — et **`# TOTAL: 89`** du build de validation **égale ce compte**, les deux
    moitiés de la règle de lecture réécrite par ce ticket-même.
  - **`src/` : ZÉRO ligne depuis la revue**, mesuré en `python3` — `git diff --name-only -- src/`
    **vide** depuis `2eed5a44` (le commit d'extraction) jusqu'à `cab9e0a8`. Les quatre commits de
    rédaction et de correction de réserves ne touchent que `docs/` et `tests/`. Corps du parseur
    dans `src/` : **1** occurrence (quatre sondes textuelles indépendantes, toutes dans
    `IO/JsonPath.h`). `getValueXml()` : **cinq** retours d'échec confirmés au source
    (`WebCtrl.cpp` 254, 266, 284, 292, 298).
  - **Réserves de revue vérifiées comme fermées, une par une** : règle de lecture de `make check`
    **réécrite** (code de sortie `0` **ET** `# TOTAL` = compte attendu, aucune des deux suffisante) ·
    récit de `F-BUILD-1` **corrigé** (les deux blocs disaient `TOTAL: 87` alors que le commit
    déclarait **88** ⇒ l'épisode cachait **aussi** une suite non exécutée, famille `F-PYTEST-1`, et
    l'hypothèse « redémarrage de `make` » est **infirmée**) · nuance de journal **ajoutée aux
    `RELEASE_NOTES`** (un IO **Web** et un IO **MQTT** sur le **même** `path` sont désormais
    **indiscernables** au journal) · préfixe `(MqttCtrl.cpp)` **corrigé en `(JsonPath.h)`** dans
    l'en-tête de `tests/JsonPathSyntax_test.cpp` · doublon « Versé à `FINDINGS.md` » **retiré** de
    `T3.35.md`.
  - **Goldens : 145 fichiers, arbre `tests/core/golden` = `d4ebc61f`** — identique sur `master`
    avant merge, sur la branche, et après. **ZÉRO golden bougé.** ⭐ Et **`git status` du worktree de
    merge est VIDE après la campagne complète** (`--untracked-files=all` : **0 fichier suivi
    modifié, 0 non suivi**) : dix-huit cycles de build, neuf mutations et leurs restaurations n'ont
    rien laissé derrière eux, et l'arbre est **byte-identique** à `cab9e0a8`.
  - ⭐ **Un préexistant corrigé au passage, hors périmètre du ticket** : les lignes `BOARD.md` de
    **T3.35** et **T3.37** contenaient des `|` **non échappés** dans du code en ligne
    (`` `val.empty() || !from_string(val, idx)` `` et `` `val.size() < 2 || val.back() != ']'` ``) ⇒
    ⇒ **9 barres NON échappées de chaque côté pour un tableau qui en attend 7** (T3.35 en portait
    **11** au total, dont deux déjà correctement échappées ; T3.37 **9**, aucune échappée) : les
    deux lignes **cassaient le rendu du tableau**.
    Échappées en `\|\|` — la convention que la ligne T3.35 utilisait **déjà** quinze cents
    caractères plus tôt. **Toutes les lignes de cette table ont désormais exactement 7 barres non
    échappées.** Ce n'était le fait ni de l'auteur ni de ce ticket.
  - ⚠️ **Ce dont je ne suis pas sûr, dit franchement** : **rien n'a été mesuré sur un vrai courtier
    MQTT ni un vrai serveur web**, et les quatre appelants de `WebCtrl::getValue()` restent
    **inatteignables par test** — le drapeau s'arrête à `getValueJson(path, filename, bool &err)` et
    la suite est **nommée** (`F-WEB-1`), pas faite. **`F-BUILD-1` reste NON REPRODUIT** : ni pendant
    ce merge (un seul bloc `Testsuite summary`, une seule invocation `check-TESTS`) ni pendant les
    dix-huit exécutions antérieures — sa cause est toujours **inconnue**. Et je n'ai **pas** rejoué
    M2, M3, M4, M5 ni M9 : la revue n'en demandait pas la reprise, je m'en remets à la mesure de
    l'auteur pour ces cinq-là.

- **🔒 T3.25 ✅ MERGÉ (`16c4aaba`, **14** commits, `git rebase master` + `merge --ff-only` sur
  `1c6ab7a9`, historique linéaire, **0 commit de fusion**, `./autogen.sh && ./configure &&
  make -j12 && make check -j6` ⇒ **`# TOTAL: 93 / PASS: 93 / FAIL: 0`**, **un seul** bloc
  `Testsuite summary`, `exit 0`, **0 `error:`**, `CXXLD    calaos_server`)** —
  `Utils::from_string("")` rendait `true` **sans rien écrire**, et un compte API ordinaire
  atteignait le trou sur des volets et des variateurs. **RIEN POUSSÉ.**

  - ⭐⭐ **DÉBLOQUE LA CHAÎNE SÉRIALISÉE `E4.1l`→`s`**, qui touche les mêmes lignes de
    `JsonApi.cpp`. C'était la raison de sérialiser ce merge ; la file peut repartir.
  - ⭐⭐ **`CM-15` ROUGIT — c'était LA réserve de la seconde revue, et elle est levée par la
    mesure.** Avant, les lignes corrigées des six familles n'étaient épinglées par **rien** : les
    muter toutes en bloc laissait **89/89 et un ensemble rouge VIDE**. Rejouée ici sur l'arbre
    rebasé : **sortie 2**, ensemble rouge **11 cas / 2 binaires**, **une rougeur par ligne** —
    **9** rouges pour les **9** lignes `port` des six classes Wago (`WIAnalog` ×2, `WITemp`,
    `WOAnalog` ×2, `WODigital` ×2, `WODali`, `WODaliRVB`) et **2** pour l'unique ligne
    `relay_num` (formes **absente** et **blanche**, qui l'atteignent toutes les deux faute de
    garde `Exists()` — c'est justement ce qui distingue `relay_num` des Wago). **Les 3 témoins
    restent VERTS** (`AnAbsentPortParameterKeepsTheModbusDefaultToo`,
    `AConfiguredPortIsStillTheOneUsed`, `AConfiguredRelayNumIsStillTheOneUsed`) ⇒ les 11 cas ne
    peuvent pas être satisfaits en « ne lisant jamais le paramètre ».
  - **`CM-OVF`** : sortie **2**, **30 cas / 6 binaires**. ⚠️ **L'auteur en annonçait 28** : l'écart
    est **entièrement expliqué** par `T3.37`, mergée entre-temps sur `master`, qui ajoute **2** cas
    à `JsonPathSyntax_test` (18 → **20**) ; **l'ensemble des binaires est identique**.
    **`CM-KEEP`** : sortie **2**, **4 cas / 3 binaires**, oracle KNX
    (`AnOverflowingValueIntKeepsTheDefaultInsteadOfSaturating`) compris.
  - ⭐ **LES TROIS ENSEMBLES SONT DEUX À DEUX DISJOINTS, vérifié AU NIVEAU DU CAS et nom par nom**,
    jamais sur les cardinaux. Piège évité : `CM-OVF` et `CM-KEEP` **partagent le binaire**
    `StringUtilsFromString_test` mais **aucun cas** (`IsOfType*` d'un côté, `FromStringOrKeep*` de
    l'autre) — un jugement pris sur les binaires aurait conclu à tort au recouvrement. ⇒ les
    **trois** écritures de la primitive sont **séparément** épinglées.
  - **Témoin M0 joué AVANT et APRÈS la campagne** : sortie **0**, **93/93**, ensemble rouge
    **VIDE** les deux fois. ⭐ **1585 cas gtest exécutés, IDENTIQUE aux quatre passes**
    (90 suites gtest + 3 scripts shell = 93) ⇒ **aucune suite silencieusement sautée** (`F-PYTEST-1`).
  - **Protocole, appliqué aux quatre passes** : pristine restaurée **sans préserver les dates**
    (`shutil.copy` puis `os.utime`, jamais `copy2` — c'est la **neuvième variante de faux rouge**
    que l'auteur a consignée, et elle ne s'est **pas** produite ici), `cmp` d'application sur
    chaque fichier, purge de **tous** les `tests/**/*.o`, `*.log`, `*.trs` **et** des **91**
    binaires de test avant chaque reconstruction, `CXXLD` compté par **regex à double espace** :
    **102** aux quatre passes, `calaos_server` compris.
  - ⭐ **La production n'a bougé QUE de ce qui était annoncé**, recompté en `python3` hors
    commentaires : **+97 / −46 lignes de code** sur `src/`, dont **une seule** ligne ajoutée pour
    la testabilité — `int getRelayNum() const { return relay_num; }`, **additive et `const`**,
    appelée par **rien** dans `src/`. **Le point d'accès Wago coûte ZÉRO ligne de production** :
    `WagoMap::get_maps()` était **déjà public**. `WagoConfigParse.h` et `TimeRange.cpp` ne changent
    **que des commentaires** (0 ligne de code). **Aucun `friend`, aucun `#ifdef` de test, aucun
    `private:` déplacé.**
  - **Les 5 lignes de `CM-15` restées NON ÉPINGLÉES sont bien FICHÉES ET NOMMÉES**, une par ligne,
    dans un tableau dédié de `T3.25.md` §10.6 avec la **raison mesurée** : `AVReceiver.cpp:62`
    (`port`), `:275` (`zone`), `InputAnalog.cpp:122` (`precision`), `InPlageHoraire.cpp:253`/`:270`
    (`start_offset`/`end_offset`). **Les cinq numéros de ligne sont exacts sur l'arbre mergé**,
    vérifiés un par un. 10 épinglées + 5 fichées = **15**, et il y a bien **15** occurrences de
    `from_string_or_keep` dans les 10 fichiers visés.
  - ⛔ **`ColorUtils.cpp:245` : l'auto-démenti de l'auteur est CONFIRMÉ PAR LA MESURE.**
    `ColorValue_test.ADecimalStringPastIntMaxIsNotAColour` **n'apparaît pas** dans l'ensemble rouge
    de `CM-OVF` ⇒ l'oracle était **vide**, l'audit avait lu `setAlpha()` (qui borne) au lieu de
    `setRgb()` (qui **sort**). Le cas est **conservé en témoin et étiqueté comme tel dans son
    propre commentaire** ; le tableau du §10.2 et la note de version sont corrigés — **aucune
    affirmation de changement de couleur n'y subsiste**.
  - **`OLAWire` est réparé sur la tête** : le cas s'appelle désormais
    `AnEmptyStringIsRefusedByFromStringAndNoLongerLeavesTheDestinationAlone`, ses deux assertions
    sont les **exactes opposées** des anciennes, la sentinelle `0xA5A5A5A5` est conservée, et la
    prémisse périmée d'`E4.1f` n'est plus **invoquée** — elle est **recadrée** (« *E4.1f avait
    raison POUR E4.1f* »). La première revue avait relu un tip rouge ; ce n'est plus le cas.
  - ⚠️ **Conflits et résolution, dits en clair** : **2 fichiers, 3 hunks**. ⭐ **`tests/Makefile.am`
    — LE PIÈGE DU `endif` A MORDU, comme annoncé** : les deux versants du conflit s'arrêtent
    **avant** l'`endif` partagé situé **après** le marqueur `>>>>>>>`, si bien que « garder les
    deux côtés » tel quel laisse **un seul `endif` pour deux `if`**. Résolu en **régénérant un
    `endif` par bloc** (celui du versant `master`/`T3.28b` `RoonSpawnViaPlayer`, le partagé
    refermant le bloc `ImpulseGarbageIo` de `T3.25`). Recompté **tous préfixes `^if*` confondus** :
    **81 `if` / 81 `endif`**, profondeur finale **0**, **minimum 0**, **jamais négative**, zéro
    marqueur résiduel. **`FINDINGS.md` ×2 : les deux côtés gardés intégralement**, les blocs
    `T3.25` placés **avant** le titre `## T3.37` de `master` pour qu'ils restent sous le `##` qui
    était le leur, et non avalés par une section voisine — vérifié section par section.
  - **Tests RECOMPTÉS, pas recopiés** : `tests/Makefile.am` porte **93** entrées `TESTS`
    (89 sous `HAVE_GTEST` + 3 hors condition + 1 sous `HAVE_GTEST && HAVE_LIBKNX`), **0 doublon**,
    et **91** `check_PROGRAMS`. `master` en portait **89** ⇒ **+4** :
    `StringUtilsFromString_test`, `core/SetStateGarbage_test`, `core/ImpulseGarbageIo_test`,
    `core/WagoPortDefault_test`. **`# TOTAL: 93` du build = ce compte** ⇒ les **deux** moitiés de
    la règle de lecture sont satisfaites. ⚠️ **L'auteur annonçait 92/+4 sur `88`** : c'était juste
    **sur sa base** `b7a4c63d` ; le nombre attendu se **recompte** après rebase.
  - **145 goldens, arbre `d4ebc61f`, IDENTIQUE à `master`, ZÉRO bougé** ; `git status -uall` du
    worktree de merge **vide** après la campagne.
  - ⚠️ **Écarts de rédaction relevés, tous NON BLOQUANTS et aucun n'invalide une conclusion** —
    ils sont laissés tels quels plutôt que corrigés en silence, mais ils sont fichés ici :
    **(a)** `T3.25.md` §9.3 et `BOARD` annoncent **84** sites `is_of_type` en **22** fichiers ;
    mesuré en `python3` sur l'arbre : **86** en **23** fichiers (**60** `<int>`, **26** `<double>`)
    — l'écart est `LuaScript/ScriptBindings.cpp`, qui en porte **2** et que le balayage a manqué ;
    **(b)** `DECISIONS.md` écrit « `from_string_or_keep()` utilisée à **20 sites** » : mesuré
    **33** sites de code hors `StringUtils.h` ; **(c)** `DECISIONS.md` dit **312** appelants
    ignorant le retour là où la fiche dit **310** ; **(d)** §9.4 écrit que « la **ligne 97** fait
    `set_param("period", …)` » — c'est la **98** ; le trio de lignes `97/105/109` est **exact**, et
    le raisonnement (membre `double frequency;` **sans initialiseur**, `InputAnalog.h:35`, qu'un
    `_or_keep` sérialiserait dans `io.xml`) est **juste et vérifié au source** ; **(e)** §8.7
    affirme encore « **`T3.34` n'est PAS mergé sur `master`** » — c'était vrai à l'écriture, mais
    `T3.34` **est** la base `b7a4c63d` de la branche ; **(f)** `T3.25.md` §10.6 renvoie les 5
    lignes non épinglées « → `T3.25a` », or **`T3.25a.md` ne porte aucune section pour elles** —
    elles ne sont perdues nulle part (tableau du §10.6 + ligne `BOARD` de `T3.25a`), mais le
    renvoi est **pendant**. ⇒ à verser dans `T3.25a` quand elle sera reprise.
  - ⚠️ **Ce dont je ne suis PAS sûr, dit franchement** : **rien sous ASan**, **aucun volet, aucun
    automate Wago, aucun bus KNX réel** — tout est mesuré sur suites unitaires. Je n'ai **pas**
    rejoué les mutations **M0…M7** du §8.3 ni les campagnes de la **première** revue : le mandat
    portait sur `CM-15`, `CM-OVF` et `CM-KEEP`, et je m'en remets à la mesure de l'auteur pour les
    autres. Les **deux assertions non déterministes** nommées au §8.7
    (`AnImpulseWithNoDurationIsDefaultedToZero`, `ADimmerSetWithNoPercentDoesNotMoveTheLight`)
    sont **vertes ici**, mais leur caractère non déterministe est une propriété du compilateur et
    de la pile : **mon vert ne prouve rien pour elles**, et l'auteur le dit déjà. `F-BUILD-1`
    **reste non reproduit** (un seul bloc `Testsuite summary` aux quatre passes).

- **🔒 T3.27 ✅ MERGÉ (`ed9fc58e`, 5 commits, `git rebase master` + `merge --ff-only`, historique
  linéaire, `./autogen.sh && ./configure && make -j12 && make check -j6` **87/87**, `exit 0`, **0
  `error:`**, `CXXLD    calaos_server`)** — `setIOParam()`/`waitForIO()` déclaraient `return 1`
  sans rien empiler et rendaient au script **son propre dernier argument**. **RIEN POUSSÉ.**

  - ⭐ **LES TROIS MESURES DE LA REVUE, REJOUÉES AU MERGE** (protocole complet à chaque passe :
    `cmp` d'application — refus de scorer une source identique à l'original —, `rm -f` du `.o`
    **et** du binaire, `CXX      LuaScript/ScriptBindings.o` et `CXXLD    LuaCalaosApi_test`
    exigées à **1** chacune, jugement **au code de sortie**, et comparaison des **ENSEMBLES** de
    rouges au témoin, jamais de leurs cardinaux) :
    - **Témoin, aucune mutation** : `CXX`=1, `CXXLD`=1, **0 rouge**, **sortie 0**.
    - ⭐ **`MR1` rejouée** (`lua_toboolean(L, 3)?"true":"false"` ⇄ `?"false":"true"`) : **ROUGIT —
      1 rouge, `ABooleanValueIsWrittenAsTrueOrFalse` exactement, sortie 1.** Elle **survivait**
      avant correction (0/12) : aucun cas ne passait de booléen Lua. ⚠️ **Le cas envoie bien les
      DEUX booléens** — vérifié dans la source : `flag_on`/`true` **et** `flag_off`/`false`, avec
      `EXPECT_EQ("true", on)` **et** `EXPECT_EQ("false", off)` ; avec `true` seul, la moitié du
      domaine resterait indistinguable **pour le contrat de retour**.
    - **État de caractérisation rejoué** (les deux `return 0` remis à `return 1`, c'est-à-dire
      l'état de `38a49017`, dont le diff ne porte **zéro ligne de `src/`** — vérifié :
      `tests/LuaCalaosApi_test.cpp` et `tests/Makefile.am` seuls) : **7/13 rouges, sortie 1**,
      dont le nouveau cas booléen. Les trois ensembles sont **strictement emboîtés et distincts**.

  - ⭐⭐ **CONFLIT DOCUMENTAIRE ANNONCÉ, RÉSOLU PAR FUSION — `FINDINGS.md`.** `T3.35` et `T3.27`
    ont appendu **le même jour** une section « dette méthodologique » en fin de fichier ⇒ deux
    titres `##` pour le même sujet. **Fusionnés sous le seul titre de T3.35**, *les deux contenus
    intégralement conservés* : le tableau des motifs devient l'**index** (M-1..M-5 de T3.35,
    **plus M-6 = `F-LINK-1`** — la déclaration d'absence écrite après coup — **et M-7 =
    `F-HARN-1`** — un remède correct généralisé à une famille étrangère), et l'apport de T3.27
    devient la sous-section `### T3.27 — la dette des déclarations « objet non lié »`, avec sa
    méthode de mesure, ses cinq déclarations rejouées et son tableau `_DEPENDENCIES`.
    ⚠️ **Rien n'a été supprimé** : contrôle `python3` ligne à ligne contre les **trois** étages du
    conflit — les seules lignes absentes du résultat sont (a) les deux titres remplacés et (b) les
    lignes que les blocs `⛔ CORRECTION` **barrent** (`~~…~~`), jamais effacent.

  - **Les 8 blocs `⛔ CORRECTION` sont des INSERTIONS LOCALES** — `E4.1j.md` · ce fichier ×2
    (journaux E4.1j **et** E4.0d) · `FINDINGS.md` ×2 · `T3.30.md` · `T3.28.md` · `BOARD.md` ·
    **et le commentaire de `tests/Makefile.am`**. Mesuré sur le diff `4295d1f3..ed9fc58e` :
    **40 lignes supprimées** en tout dans la doc et le `Makefile.am`, pour **1047 insérées** —
    et **chacune** des suppressions est soit une ligne **rendue barrée `~~…~~`** au-dessus de son
    bloc (**22** lignes ajoutées portent `~~`), soit un **recalage de numéro de ligne** après
    E4.1j (`02_io_drivers.md`, les refs `ScriptBindings.cpp:` de `09_lua_scripting.md`), soit la
    ligne `BOARD` du ticket lui-même. **Aucune section entière n'est réécrite, aucun paragraphe
    d'un autre agent n'est perdu.** Le bloc de `tests/Makefile.am` ne touche **que des lignes
    `#`** : **zéro effet automake**, vérifié au diff hunk par hunk.

  - **Conflits du rebase** : **un seul**, `FINDINGS.md` (ci-dessus). ⚠️ **`tests/Makefile.am` et
    `BOARD.md` n'ont PAS conflicté** — contrairement au rebase de l'auteur contre `fb9d064c` :
    `T3.35` **ne touche pas** `tests/Makefile.am` (74/74 et 86 `TESTS` identiques sur `fb9d064c`
    et `4295d1f3`) et sa ligne `BOARD` est ailleurs. **Recompté après merge, pas recopié** :
    `tests/Makefile.am` **75 `^if*` / 75 `endif`**, profondeur finale **0**, **jamais négative** ;
    **87 entrées `TESTS`** (3 scripts shell), **85 `check_PROGRAMS`**. `BOARD.md` reste trié par
    numéro (T3.27 · T3.28 · T3.28a · **T3.28b** · T3.29).

  - **`make check` 87/87, 0 FAIL**, **145 goldens**, arbre `tests/core/golden`
    **`d4ebc61fb2b1876f587d075a0cb050750dc1876f`** — **identique à master, aucun n'a bougé** —
    et **0 fichier suivi modifié** après `make check`.

  - **Corrections transverses vérifiées** : `F-LUA-5` → **`F-LUA-7`** (`F-LUA-1..7` **uniques**,
    un seul `[F-LUA-n]` par identifiant) · **`F-HARN-1`** consignée comme **6ᵉ variante de famille
    distincte** · `DECISIONS.md` porte le bloc `⛔ CORRECTION` qui requalifie « cause racine des
    **CINQ** variantes » en « cinq de la famille `_DEPENDENCIES`, **plus une sixième d'une autre
    famille** » ⚠️ *(la phrase d'origine n'est pas barrée, elle est laissée intacte et contredite
    juste en dessous — insertion locale, rien d'écrasé)* · `docs/09_lua_scripting.md` : `waitForIO()`
    **retiré** de « non couverts par un test », `tests/LuaCalaosApi_test.cpp` **ajouté** au tableau,
    et **7 des 13 cas** l'exercent — **recompté en `python3` sur les corps de `TEST_F`, exact**.

  - **Ouvert par ce merge, à ne pas perdre** : **[`T3.28b`](T3.28b.md)** (📋) — les deux tripwires
    de source de `T3.28` sont justifiées par une limite (« seule suite à lier `RoonPlayer.o` ») qui
    est **fausse** ; les trois écritures fautives sont déjà corrigées, l'instruction ne l'est pas.

  - **Nettoyage** : worktrees `.wave52/t3.27` et `.merge52/t3.27` supprimés par **chemin exact**,
    branche `fix/t3.27` supprimée, `git worktree prune` (l'enregistrement `.review52/t3.27` avait
    déjà disparu du disque). **Voisins vivants intacts.**


- **🔒 T3.35 ✅ MERGÉ (`555b1d02`, 8 commits, `merge --ff-only` sur `fb9d064c` — **rebase inutile,
  aucun conflit**, historique linéaire, `make -j12 && make check -j6` **86/86**, `exit 0`, **0
  `error:`**, `CXXLD    calaos_server`) — ⭐⭐ **la revue avait rendu « merge REFUSÉ » sur UNE
  réserve, et elle avait raison : le §6.4 de la fiche déclarait clos un trou qui ne l'était pas.**
  Fermée avant ce merge, puis tout revalidé.**

  - ⭐ **LA RÉSERVE, MESURÉE.** La garde d'index livrée par T3.35b,
    `val.empty() || !Utils::from_string(val, idx)`, **se lit** « un index qui n'est pas un nombre est
    refusé » et **ce n'est pas ce qu'elle fait**. `Utils::from_string()` rend **`iss.eof()`**, et un
    flux qui n'a consommé que des **blancs** — ou qu'un **signe** — a bien atteint sa fin : elle
    annonce donc un **SUCCÈS** sur **cinq** jetons qui ne portent aucun nombre. `val.empty()` n'en
    attrapait **qu'un**.

    | jeton | `from_string` | écrit `idx` ? | T3.35b | T3.35c |
    |---|---|---|---|---|
    | `[]` | `true` | **NON** | élément 0 + avertissement | **inchangé** |
    | `[ ]` `[\t]` | `true` | **NON** | ⛔ élément 0 **en silence**, `idx` **jamais assigné** | élément 0 **+ avertissement** |
    | `[+]` `[-]` | `true` | oui, `0` | ⛔ élément 0 **en silence** | élément 0 **+ avertissement** |
    | `[zz]` `[5 ]` | `false` | oui | élément 0 + avertissement | **inchangé** |
    | `[ 5]` `[+2]` | `true` | oui | **résolvent** (élém. 5 / 2) | **inchangé** |
    | `[-1]` | `true` | oui, `-1` | `""` + *index not found* (`at()`) | **inchangé** |

  - ⭐⭐ **CE QUE CE MERGE A APPRIS ET QUI VAUT AU-DELÀ DU TICKET : `0 rouge` sur une mutation
    d'INITIALISEUR ne prouve rien sans sa VARIANTE POISON.** La fiche concluait de `R1`
    (`int idx = 0` → `int idx;`, **0 rouge**) que l'initialiseur était **mort**. Il était **VIVANT** :
    sur `[ ]` / `[\t]` la branche `idx = 0` n'est pas prise et `from_string()` n'écrit rien, donc
    `parent.at(idx)` lisait le **déclarateur**. **`0 rouge` ne disait pas « équivalent », il disait
    « trou non vu » — et les deux se ressemblent exactement.** Ce qui les sépare est **`R1s`**,
    l'initialiseur remplacé par une valeur **fautive** (`int idx = 7`) : un mutant vraiment
    équivalent ne peut pas la voir, un trou la voit.

    | | `R1` (`int idx;`) | **`R1s` (`int idx = 7`)** |
    |---|---|---|
    | **avant** | 0 rouge → *conclusion tirée : équivalent* | ⛔ **4 rouges**, `array index 7 is out of range`, **les deux parseurs** |
    | **après** | 0 rouge | ⭐ **0 rouge** — équivalence **prouvée**, plus supposée |

    ⇒ **RÈGLE versée à la « dette méthodologique » de `FINDINGS.md`** (section neuve, **à APPENDRE,
    jamais à réécrire** ; conflit ⇒ **garder les deux côtés**). Une suppression d'initialiseur ne
    peut pas rougir un test **portable** ; une valeur fautive, si.

  - **LE CORRECTIF — une ligne par parseur** : `val.find_first_of("0123456789") == string::npos`.
    Il **subsume `val.empty()`** (une chaîne vide n'a pas de chiffre non plus) ⇒ **aucune
    sous-condition morte**, et il établit l'**invariant** qui rend `R1` légitimement équivalent :
    garde passante ⇒ `val` porte un chiffre ⇒ la sentinelle réussit ⇒ **`num_get` tourne** et, en
    C++11, **écrit toujours** ; garde déclenchée ⇒ `idx = 0` assigné. **Tout** chemin vers
    `parent.at(idx)` écrit `idx`.

    ⛔ **La forme proposée en revue, `find_first_not_of("0123456789")`, a été ÉPROUVÉE ET ÉCARTÉE** —
    elle testait faux de **trois** façons : une chaîne **vide** n'a pas de **non**-chiffre non plus
    ⇒ **`[]` repassait NON GARDÉ** (le piège même du ticket) ; `[+2]` cessait de résoudre ; `[-1]`
    s'entendait dire *« is not a number »*, **un mensonge sur `-1`**. ⇒ **le signe n'est PAS rejeté,
    délibérément** : un index négatif est déjà refusé **là où c'est juste**, par `at()`, avec
    *index not found* — autre message pour autre faute. `APaddedOrSignedNumberIsStillReadAsANumber`
    (×2) est le cas qui **refuse ce troc**, **vert avant comme après**, et il est là exprès.

  - ⚠️ **DEUXIÈME « limite vraie pour une raison fausse » — motif `F-LUA-3`, désormais compté trois
    fois dans la série.** Le §6.8 disait les lambdas de `subscribeStatusTopics()` intestables parce
    qu'`IOBase`/`EventManager` « ne sont pas dans la clôture de liaison ». **Ils y sont** :
    `JsonPathSyntax_test_LDADD` finit par `$(CORE_TEST_LDADD)`, qui **commence** par
    `$(CORE_SERVER_OBJECTS)`, lequel liste `IOBase.$(OBJEXT)` et `EventManager.$(OBJEXT)` ;
    `nm -C --defined-only` donne `IOBase::setStatusInfo` et `EventManager::create` en **`T`** et les
    **6 lambdas** définies dans `MqttCtrl.o`. **La vraie raison est un DISPATCH absent** :
    `subscribeCb` est **privé** (`MqttCtrl.h:78`) et **le seul code qui le parcourt** est la lambda
    `process->messageReceived` du **constructeur**, pilotée par la boucle `uvw` de `calaos_mqtt` que
    la fixture ne fait jamais tourner ; `storeMessage()` **ne fait que stocker**.
    ⚠️ **Le coût de la fausse raison** : elle désigne « ajouter des `.o` au `LDADD` » comme sortie —
    **ce qui ne changerait rien**. La vraie désigne une **couture de dispatch**, ou l'extraction
    `resolveJsonPath()` de **`T3.37`**.

  - ⚠️ **`MqttInputSwitch::readValue()` REND `false`, il ne « saute » pas** (sa signature n'a pas de
    troisième réponse) — **mais le changement y est NUL** : l'ancien chemin (`err = false`,
    `sv == ""`) ne correspondait ni à `on_value` ni à `off_value` et tombait **déjà** sur le
    `return false` final. ⇒ **l'argument des 9 lecteurs de `err` tient**, il faut seulement lire
    cette ligne du tableau §6.2 comme *« rend `false` »*.

  - ⛔ **DIVERGENCE LAISSÉE OUVERTE, MAINTENANT AU BOARD ET PLUS SEULEMENT DANS LA FICHE §6.8** :
    **`WebCtrl` n'a AUCUN drapeau d'erreur**. `WebCtrl::getValue()` rend une chaîne, aucun de ses
    trois appelants n'en demande, lui en ajouter un serait de l'**API morte**. Les corrections de
    **parseur** sont portées **à l'identique** sur les deux copies ; **seul le drapeau diverge**.
    **À refermer par [`T3.37`](T3.37.md)**.

  - **CHIFFRES, tous RECOMPTÉS en `python3` sur l'arbre livré — aucun cardinal recopié** :
    `JsonPathSyntax_test` **51 → 57** cas · entrées `TESTS` **86**, **0 doublon** (la fiche
    annonçait **83**, chiffre pris avant deux merges de `master`) · `^if` / `endif` **74 / 74**,
    profondeur finale **0**, minimum **0** · goldens **145**, arbre **`d4ebc61f`**, **aucun n'a
    bougé** · **0 fichier suivi modifié** après `make check`. `tests/Makefile.am` **n'est pas
    touché** par la branche ⇒ le piège du `endif` consommé était **sans objet** ici.

  - **NON VÉRIFIÉ, à ne pas surestimer** : aucun bout-à-bout avec un vrai courtier MQTT ni un vrai
    serveur web ; rien sous ASan ; `R4` (garde de **forme** ramenée à `val.size() < 2`) **n'est pas
    implémentée dans `mutate.py`** et **n'a pas été rejouée** — son **5** est le chiffre de la
    campagne T3.35b, et le correctif T3.35c ne touche pas la garde qu'elle vise. **RIEN N'A ÉTÉ
    POUSSÉ.**

- **🔒 T3.28 ✅ MERGÉ (`1a7e7d73`, 6 commits, `git rebase master` + `merge --ff-only`, historique
  linéaire, `make check` **86/86**) — ⭐ **la revue avait rendu « merge sous réserve » et les DEUX
  réserves étaient dans le FILET, pas dans `src/` : deux tripwires source qui promettaient plus
  qu'elles ne gardaient. Corrigées avant ce merge, et rejouées ici.****
  Périmètre réel : l'**en-tête neuf de production** `Audio/RoonArgs.h`, `Audio/RoonPlayer.{cpp,h}`,
  **1 ligne** de `src/bin/calaos_server/Makefile.am`, `tests/{Makefile.am,core/RoonArgs_test.cpp}`
  et trois fiches. **Aucun débordement.** Goldens : arbre `d4ebc61f…`, **145 fichiers**,
  **identique à `master`**. Suites **85 → 86** (recompté en `python3` ; unique ajout
  `core/RoonArgs_test`) — ⚠️ **la fiche annonce 82 → 83, chiffre pris AVANT le merge de `T3.30`**.
  `tests/Makefile.am` : **74 `^if*` / 74 `endif`** tous préfixes confondus (**73/73** sur
  `master`), profondeur **jamais négative**.

  - ⭐ **CE QUE LA REVUE AVAIT TROUVÉ, ET POURQUOI C'ÉTAIT GRAVE MALGRÉ « aucune ligne de `src/`
    en cause ».** Une tripwire qui compte `startProcess(` épingle l'**EXISTENCE** de l'appel, pas
    ses arguments : échanger `startProcess(exe, "roon", procArgs)` contre
    `startProcess(exe, "roon", std::string())` laissait la suite **entièrement VERTE** — c'est-à-dire
    **le défaut même du ticket réintroduit sans un seul rouge**, le sidecar reparti sans `--host`
    ni `--port`. Jumelle : `int port =` épinglait la **PRÉSENCE** de l'initialiseur pendant que le
    message d'échec promettait « *has no in-class initialiser* » ⇒ `int port = 0;` survivait, et
    sur l'early-return que cet initialiseur existe précisément pour couvrir, `--port 0` serait
    parti au sidecar. **Un message qui surpasse sa garde est pire qu'une garde qui admet ses
    limites** : le lecteur suivant croit le message.

  - ⭐ **REJOUÉES AU MERGE, ET ELLES ROUGISSENT** — protocole du brief à la lettre : `rm -f` du
    **binaire** `tests/core/RoonArgs_test` **et** des `.o` (serveur `Audio/RoonPlayer.o` + test),
    reconstruction, **`CXXLD    core/RoonArgs_test` exigée à double espace** (présente aux quatre
    passes) **et code de sortie du binaire**, jamais un comptage de `FAILED`.

    | # | Échange | `CXXLD` | Code | Cas rouges |
    |---|---|---|---|---|
    | **M0** | *témoin, aucune mutation* | ✔ | **0** | **∅** (13/13 verts) |
    | **N3** | `startProcess(exe, "roon", procArgs)` ⇄ `…, std::string())` | ✔ | 1 | `TripwireSource_TheRespawnLaunchesThroughTheSameCallSite` |
    | **N2** | `int port = RoonArgs::DefaultPort;` ⇄ `int port = 0;` | ✔ | 1 | `TripwireSource_ThePortMemberIsInitialisedToTheDefaultPort` |
    | **N4** | reformatage `startProcess( exe , "roon" , procArgs );` | ✔ | 1 | `TripwireSource_TheRespawnLaunchesThroughTheSameCallSite` |

    **N3 et N2 rougissent sur des ensembles DISJOINTS**, un cas chacun, témoin à **0**.

  - ⚠️ **LE PRIX DE L'AIGUILLE LONGUE EST PAYÉ, ET IL EST DU BON CÔTÉ.** `collapseWhitespace()`
    ramène toute suite de blancs à un espace mais **ne normalise pas les espaces autour de la
    ponctuation** ⇒ `startProcess( exe , … )` **manque l'aiguille**. **N4 le mesure** : faux rouge,
    message nommant l'épellation exacte attendue (`process->startProcess(exe, "roon", procArgs);`).
    **Bruyant, jamais silencieux** — et c'est structurel, pas chanceux : la forme
    `EXPECT_EQ(1, countOccurrences(…))` **ne peut pas verdir sur aiguille absente**, elle ne peut
    que rougir. Un faux rouge est acceptable, un faux vert ne l'est pas. ⛔ **Ne « réparez » jamais
    ce rouge en reformulant l'aiguille** : les deux tripwires lisent la source **privée de ses
    commentaires** (`stripComments`), et `RoonPlayer.cpp` évite délibérément d'épeler l'appel dans
    sa prose — une explication qui recopie l'appel casserait le compte.

  - **Les modifications de `src/` du commit de correction sont des COMMENTAIRES SEULS, vérifié
    mécaniquement** : `RoonArgs.h` et `RoonPlayer.cpp` passés à un dépouilleur de commentaires
    (`python3`, chaînes et caractères respectés) ⇒ **25 et 315 lignes de code, identiques avant et
    après**, zéro changement de comportement. Le commit retire une **citation périmée**
    (`FINDINGS.md:2510-2518`, remplacée par un renvoi **par titre**) et corrige une phrase fausse
    sur `T3.25` (elle change **deux régimes sur trois**, pas un : le débordement passe de
    `true`/`INT_MAX` à `false`/`INT_MAX`).

  - **Conflits de rebase : UN SEUL, `tests/Makefile.am`** — les deux blocs `HAVE_GTEST` (T3.30 de
    `master`, T3.28 de la branche) appendus au même endroit. ⚠️ **Le piège annoncé s'est bien
    présenté** : les deux côtés du conflit s'arrêtent **avant** le `endif`, qui est **commun et
    hors du bloc marqué** ⇒ « garder les deux côtés » tel quel produit **un `endif` pour deux
    `if`** et `automake` répond *unterminated conditionals*. Résolu en **régénérant** la queue de
    fichier (bloc T3.30 + `endif`, ligne vide, bloc T3.28 + `endif`), puis **recomptage tous
    préfixes `^if*` confondus**. `RELEASE_NOTES.md` et `FINDINGS.md` **n'ont PAS conflité** —
    `git` a su fusionner les deux appends ; diff contre `master` : **+61/−0** et **+66/−1**, la
    seule suppression étant l'annotation d'une ligne de finding que la branche avait elle-même
    écrite.

  - **Ouvert par la revue et fiché, PAS corrigé : [`T3.28a`](T3.28a.md)** — un `host` contenant un
    **espace** met `calaos_roon` en boucle de relance à ~100 ms (`buildArgs()` concatène sans
    quoting, `startProcess()` redécoupe sur l'espace, `argparse` sort en `SystemExit(2)`, le
    respawn n'a **aucun backoff**). ⚠️ **Préexistant** : T3.28 a **déplacé** la concaténation, il
    ne l'a ni introduite ni aggravée — mais il l'a rendue **testable** pour la première fois.
    Entrée `FINDINGS.md` **appendue** dans la section E4.5d, aucune ligne d'autrui réécrite.

  - ⚠️ **CE DONT JE NE SUIS PAS SÛR** : **aucun core Roon réel, aucun sidecar lancé** — `RoonCtrl`
    reste inatteignable depuis `make check` (son constructeur lie un socket unix et spawn), donc le
    **site de lancement unique** et l'**initialiseur en-classe** sont gardés par des tripwires
    **de texte** et par rien d'autre ; elles ne peuvent pas dire que `procArgs` contient la bonne
    chaîne (ce sont les cas `buildArgs()` qui le font, sur du code de production). Et **§7/§8 de
    `T3.28.md` n'ont pas été remis à jour par le commit de correction** : le tableau M7 nomme
    encore `…ThePortMemberCarriesAnInClassInitialiser` avec l'échange `int port;`, alors que le cas
    s'appelle désormais `…ThePortMemberIsInitialisedToTheDefaultPort`. **Fiche à recaler, code
    juste.**

  ⚠️ **`fix/t3.28` n'a pas été déplacée puis supprimée par commodité** : le travail est sur
  `merge49/t3.28` et c'est `master` qui porte les six commits. **RIEN N'A ÉTÉ POUSSÉ.**


- **🔒 T3.30 ✅ MERGÉ (`206e82ff`, 6 commits, `git rebase master` + `merge --ff-only`, historique
  linéaire, `make check` **85/85**) — ⭐ **la revue de merge a rendu « merge sous réserve », et la
  réserve était juste : le ticket désamorçait un piège sur une chaîne MORTE et le réarmait un cran
  plus loin sur la chaîne VIVANTE.****
  Périmètre réel : l'**en-tête neuf de production** `IO/Wago/WagoBits.h`, `IO/Wago/WagoCtrl.{cpp,h}`,
  **1 ligne** de `src/bin/calaos_server/Makefile.am`, `tests/{Makefile.am,WagoBits_test.cpp}` et deux
  fiches. **Aucun débordement.** Goldens : arbre `d4ebc61f…`, **145 fichiers**, **identique à
  master**. Suites **84 → 85** (recompté en `python3`, continuations `\` comprises ; unique ajout
  `WagoBits_test`). `tests/Makefile.am` : **73 `^if*` / 73 `^endif`** tous préfixes confondus
  (**72/72** sur master), profondeur jamais négative.

  - ⭐ **F1, LA RÉSERVE BLOQUANTE, ET CE QUE MA PROPRE MESURE A CHANGÉ À SA CORRECTION.**
    `coilBufferSize()` écrase `nb <= 0` en **0** et **les deux LECTURES allouaient dessus**, sur le
    chemin qui s'exécute (battement de cœur modbus toutes les 10 s, tout scrutin Wago), alors que
    les écritures durcies par le ticket n'ont **aucun appelant**. **D'où sortent les 255** :
    `mbus_cmd.c`, `mbus_cmd_read_coil_status()` déclare **`mbus_ubyte byte_count`**
    (`unsigned char`, `mbus_conf.h`), le remplit **depuis la trame de réponse** et fait
    `while (byte_count--) MBUS_BYTE_WR(coils_data, *bufptr++)` — borné par la réponse et par la
    largeur d'un `unsigned char`, **par rien de ce que l'appelant a passé**.
    ⚠️ **La revue proposait deux voies et l'une des deux ne corrige rien** : retirer le clamp de
    `coilBufferSize()` laisse `(nb + 7) / 8` **tronquer vers zéro**, donc rendre `0` sur tout
    `[-8, 0]` — neuf valeurs, dont `nb == 0`. **Mesuré au `g++`, pas déduit.** Voie retenue : le
    prédicat nommé `WagoBits::countIsReadable()`, en tête de `read_bits()` **et** de `read_words()`
    (qui ne refusaient pas le même domaine), à côté de sa jumelle `countIsWritable()`.

  - ⭐ **RÉGRESSION OU DÉFAUT PRÉEXISTANT ? LES DEUX, ET ILS NE SE FICHENT PAS AU MÊME ENDROIT.**
    Décidable, donc mesuré sur `master` : `nb / 8 + nb % 8` est **négatif pour tout `nb < 0`**
    testé (`-1, -7, -8, -9, -40`) ⇒ `new mbus_ubyte[négatif]` **lève** ⇒ **`nb < 0` est une
    régression introduite par T3.30**, jamais livrée, **donc pas fichée comme défaut de
    production** (consignée dans `T3.30.md`). Mais `master` rend **déjà `0` pour `nb == 0`** ⇒
    **`nb == 0` est un défaut PRÉEXISTANT et LIVRÉ, sur les deux lectures** : addendum à
    **F-WAGO-7**. L'extraction de `coilBufferSize()` l'a rendu **visible**, elle ne l'a pas créé.

  - ⭐ **UN DÉFAUT DE PLUS, TROUVÉ EN REFERMANT F1, QUE LA GARDE NE FERME PAS — `F-WAGO-8`.**
    `byte_count` reste borné par la **réponse**, jamais par la demande ni par la taille du tampon.
    Le battement de cœur appelle `read_bits(0, 1, …)` : **un octet alloué**, une réponse annonçant
    255 le déborde de **254**. Idem côté registres (127 mots), et `mbus_rqst()` lit déjà le corps
    de trame sur `MBUS_LENGTH_L` (jusqu'à 255) dans les **254** octets libres de `mbus->buf`.
    **Préexistant, inchangé par ce ticket, ticket dédié recommandé** (`libmbus` est un tiers
    importé : le corriger change sa signature). ⛔ **Ne pas lire « F1 fermé » comme « le
    débordement de tas est fermé ».**

  - **L'ORACLE, ET LA CONTRE-MUTATION.** Commit de caractérisation `9597c897` : **zéro ligne de
    `src/`**, **5 rouges sur 19**, code de sortie **1** ; le correctif `3f663aba` les passe au vert,
    **19/19**, code de sortie **0**. Contre-mutations, binaire de test **et** `.o` serveur `rm -f`
    d'abord, **`CXXLD WagoBits_test` exigé à chaque tour**, verdict au **code de sortie** : témoin
    **0 rouge / exit 0** · M1 garde retirée de `read_bits()` → **1** · M2 garde retirée de
    `read_words()` → **1** · M3 `countIsReadable → nb >= 0` → **3** · M4 `→ true` → **3**.
    ⚠️ **M3 et M4 partagent leur ensemble** — deux forces de la même mutation du même prédicat, et
    je ne prétends pas le contraire. **M1 et M2 le partageaient aussi** tant que le fil de source
    était **un seul cas** : il a été **scindé en un cas par lecture** parce que le journal ne disait
    pas laquelle des deux avait perdu sa garde.

  - **F2/F3/F4/F6 fermés.** F2 : `F-WAGO-7` omettait **`UWord address;`**, nu dans les **mêmes
    quatre branches** que `count` (`:75/:131/:160/:217`) — et dans **deux branches de plus**
    (`:108`, `:193`, écritures unitaires), où une adresse de pile arbitraire part **en ÉCRITURE**
    vers l'automate ; `write_word` y ajoute un **`UWord value;`** nu (`:194`). F3 : « 82 → 83 »
    n'était pas faux, il avait **périmé** — `master` a gagné `JsonPathSyntax_test` et
    `ScriptWire_test` ⇒ **84 → 85**. F4 : « `&WagoMap::` = 8, **toutes** des `sigc::mem_fun` » ⇒
    **7 sur 8**, la 8ᵉ est le **type de retour** de `WagoMap &WagoMap::Instance(...)`
    (`WagoMap.cpp:96`) ; conclusion inchangée. F6 : `WagoBits.h` sans `<cstring>`.

  - **Build de validation rejoué en `distclean` complet après le rebase** (`./autogen.sh &&
    ./configure && make -j12 && make check -j6`, agents concurrents, attendu par **`docker wait`**) :
    **0 `error:`**, **`CXXLD calaos_wago`**, **`CXXLD calaos_server`**, **`CXXLD WagoBits_test`**
    (×1 chacun), **85 PASS / 0 FAIL / 0 SKIP / 0 ERROR**, code de sortie **0**.

  - ⚠️ **`fix/t3.30` n'a PAS été déplacée** : la branche est sortie dans le worktree voisin vivant
    `.wave50/t3.30` et y toucher aurait bougé son `HEAD` sous les pieds d'un autre agent. Le travail
    a été fait sur `merge50/t3.30` (worktree `.merge50/t3.30`) et c'est **`master` qui porte les six
    commits**. `fix/t3.30` pointe encore sur l'ancien `20341e90` : à supprimer avec son worktree.

  - **NON VÉRIFIÉ, à ne pas surestimer** : **rien sous ASan**, **aucun automate réel**, aucune trame
    modbus émise. Le débordement de F1 comme celui de F-WAGO-8 sont établis **au source et par un
    `g++` autonome**, pas observés en vol. Les sites d'appel de `WagoCtrl.cpp` restent couverts par
    **quatre fils de texte** et non par de l'exécution — plus faible, et dit comme tel.

  - **Rien n'a été poussé.**

- **🔒 E4.1j ✅ MERGÉ (`2abd16b4`, 6 commits, `git rebase master` + `merge --ff-only`, historique
  linéaire, `make check` **84/84**) — ⭐ le wire Lua aval, et **le merge qui CLÔT la vague 1
  parallèle d'E4.1** : l'épique passe à **11/17 livrés (`a`→`k`)**, il ne reste que la chaîne
  sérialisée `l`→`s` puis `x`.**
  Périmètre réel : `LuaScript/ScriptExtern_main.cpp`, `LuaScript/ScriptBindings.cpp`, l'**en-tête
  neuf de production** `LuaScript/ScriptWire.h` (**1 ligne** de `calaos_script_SOURCES`), plus le
  débordement déclaré de **2 lignes** dans `ScriptBindings.h` (deux déclarations privées
  `sendJson()` devenues mortes). `LuaScript/ScriptExec.cpp` **hors périmètre** (E4.1m) : **diff
  vide, vérifié**. Commit de caractérisation `c8654672` : **zéro ligne de `src/`** (2 fichiers,
  `tests/ScriptWire_test.cpp` neuf + `tests/Makefile.am`), et **verte seule contre le code
  jansson** — reconstruite au premier commit rebasé, binaire effacé puis **`CXXLD ScriptWire_test`
  pour de vrai**, **32/32 PASSED**. **Aucune assertion préexistante modifiée** : sur les 6 commits,
  les seuls `tests/` touchés sont ces deux-là. Goldens intacts : arbre `d4ebc61f…`, **145
  fichiers**, identique à master **et sur chacun des 6 commits**. Suite **83 → 84** (recompté en
  `python3`). `tests/Makefile.am` : conflit de rebase (le seul), résolu par **régénération**
  — **append pur octet pour octet prouvé en `python3`** (`branche == base + suffixe`, puis
  `nouveau == master + le MÊME suffixe`, **+2 094 octets / 36 lignes**, sha256 du suffixe
  `310ee00e4487…`) ; **72 `^if*` / 72 `^endif`** (**71/71** sur master), profondeur jamais
  négative. Build distclean rejoué **après le rebase de merge** (`git clean -xdff` puis
  `make -j16 && make check -j8`, 3 agents concurrents, attendu par `docker wait`) :
  **`CXX LuaScript/ScriptExtern_main.o`**, **`CXX LuaScript/ScriptBindings.o`**,
  **`CXXLD calaos_script`**, **`CXXLD ScriptWire_test`**, **`PASS: ScriptWire_test`**,
  **84 PASS / 0 FAIL / 0 SKIP / 0 ERROR**, **0 `error:`**, code de sortie **0**.
  *(Master a bougé pendant le build — `dd619482`, docs seuls ; rebase rejoué, et les arbres `src`
  et `tests` sont **identiques bit pour bit** à ceux qui ont été construits : `54597d12bf52` /
  `ae4be6724421`. Le contenu mergé est exactement celui qui a été exercé.)*

  - ⭐⭐ **LA DÉPENDANCE D'`E4.1x` EST VIDE — MESURÉE UNE QUATRIÈME FOIS, PAR LE MERGE.**
    Mutation **fidèle** d'`IO/ExternProc.h` (la seule ligne `#include "Jansson_Addition.h"`
    commentée : l'en-tête **cesse de déléguer** jansson à ses includeurs, `Jansson_Addition.h`
    **reste INTACT** — vérifié `git diff` vide dessus), **tous les `.o` supprimés** plus les 7
    binaires, `make -C src -j16 -k`. **Sur master (`b107a1e5`) : 34 `error:`, 2 TU casseurs —
    `LuaScript/ScriptExtern_main.cpp` et `LuaScript/ScriptBindings.cpp` — et `calaos_script` PAS
    PRODUIT (6/7).** **Avec la branche : RC=0 (relevé pour de vrai, pas via un `$?` mangé par le
    shell hôte), ZÉRO `error:`, 157 TU recompilés, 7 `CXXLD` et les 7 binaires présents dont
    `calaos_script`.** Cela reproduit la mesure de l'auteur (`db6770a7` → 4 casseurs,
    `d1462d9e` seul → 2, avec la branche → 0) : **plus aucun driver ne dépend de jansson**, il ne
    reste que la chaîne sérialisée `l`→`s`.
    ⚠️ **« Fidèle » est le mot** : retirer `<jansson.h>` de `Jansson_Addition.h` (qui utilise
    `json_t` dans son propre corps) casse **ce fichier** et a déjà produit **4 158 erreurs** et un
    faux chiffre de 10 unités.

  - ⭐ **PREMIER TICKET DE LA SÉRIE DONT LES ORACLES D'OCTETS SONT PORTEURS, PAS DÉFENSIFS — ET
    C'EST MESURÉ, PAS RAISONNÉ.** La revue a **écrit et exécuté le cas** : un programme liant le
    **vrai LuaJIT du conteneur**, avec la garde `lua_isstring`/`lua_tostring` **recopiée verbatim**
    de `Lua_Calaos::sendPushNotif`. **4 orthographes passent** la garde et livrent l'octet ;
    `j.dump()` nu lève alors `type_error.316 invalid UTF-8 byte at index 5: 0xFF`. Il n'y a **aucun
    try/catch** sur ce chemin (`messageReceived()` est appelé depuis le callback de lecture
    `ExternProc`) : **le précédent KNX s'applique en plein — sans le gestionnaire, `calaos_script`
    TERMINERAIT**, au milieu du script de l'utilisateur. Wago, OLA et Hue avaient tous conclu
    « défensif » parce qu'il fallait un équipement hostile ; **ici une ligne de Lua écrite par
    l'utilisateur suffit**.
    ⚠️ **La précision qui manquait, mesurée elle aussi** : l'orthographe « **octet brut dans le
    texte du script** » **ne survit PAS au transport aujourd'hui** — `ScriptExec.cpp:154-157`
    passe le script par `jansson_from_params()`, dont `json_string()` rend `NULL` sur l'octet
    invalide, et **la paire entière tombe** : `calaos_script` ne reçoit jamais ce script. Ce qui
    passe, c'est **`string.char(0xFF)` et `"\255"`**, **le script restant ASCII pur, l'octet
    naissant dans la VM**. **Le chemin brut s'ouvre avec `E4.1m`** (migration de `ScriptExec.cpp`)
    — ne pas le citer comme atteignable avant.

  - ⭐ **UNE QUATRIÈME FORME DE CONTOURNEMENT DU TYPAGE, DISTINCTE DES TROIS CONNUES —
    consignée `F-LUA-3`.** Le typage fort du ticket (`ScriptWire::IoId` / `ParamKey` /
    `ParamValue` / `PushAttachment`) fait bien de `buildSetParamMessage(ParamKey{}, IoId{}, …)`
    une **erreur de compilation**. Mais `io.set_param(value, key)` côté script, et la permutation
    des indices `lua_tostring(L, 1/2)` côté dépilement, **compilent tous les deux**. Les trois
    formes déjà connues portaient sur **un** argument mal rempli (« emballer la mauvaise
    variable », « agrégat positionnel ») ; **ici les deux valeurs typées sont construites au site
    d'appel, chacune bien typée, et la faute est dans l'appariement source → emballage**.
    Réductible **en principe** (typer `LuaIOBase::set_param()` et le dépilement Lua), **pas dans
    ce ticket**.

  - **La sentinelle ajoutée en suites de revue** (`23da0c0a`, tests seuls) : `decodeEvent` perdait
    son `ev.clear()` **sans qu'aucun cas ne rougisse** (0/32). Sentinelle **symétrique** de celle
    de `type_str`, posée sur **les deux chemins** (peuplé et vide) ⇒ la mutation `N2` passe de
    **0/32 à 2 cas / 4 assertions**, `N3` en rougit **1**, et **les ensembles sont distincts** :
    les deux sentinelles ne se remplacent pas.

  - **Un recalibrage d'honnêteté** : « sans ce détecteur `M7` serait restée verte » devient
    **« le CAS DU TICKET serait resté vert »** — détecteur retiré, `M7` reste rouge **1/32**, mais
    via un **fixture voisin** et par une **exception non rattrapée**, pas par une assertion.

  - **Un « VERT 0/32 » requalifié** : ~~il est **structurellement garanti** — `ScriptBindings.cpp`
    n'est lié dans **aucun** binaire de test ⇒ **limite de périmètre, pas trou de couverture**.~~
    - ⛔ **CORRECTION (2026-08-25, T3.27) — la prémisse est FAUSSE.** Mesuré en `python3`
      (`_SOURCES`/`_LDADD`/`_DEPENDENCIES` aplatis, variables Make développées) sur `d68e59f1`,
      `599fcea9`, `8bcffdc8` **et** `fb9d064c` : `LuaScript/ScriptBindings.$(OBJEXT)` est lié par
      **`LuaSandbox_test`** dans les quatre arbres — **jamais zéro**. Le vert venait de
      `LuaSandbox_test_DEPENDENCIES = libcalaos_common.la` : le mutant **n'était jamais relié,
      donc jamais exécuté**. **Faux vert de relink** ([`T3.36`](T3.36.md)), **pas** une limite de
      périmètre — et la différence compte, une limite se déclare, un faux vert se corrige avec
      `rm -f`. T3.27 ajoute `LuaCalaosApi_test` (2ᵉ cible) et la permutation **rougit**.
      ⚠️ Erreur **non isolée** : `FINDINGS.md` **F-LINK-1**, dette méthodologique transverse.
      Corrigé aux trois endroits : ici, `E4.1j.md` et `FINDINGS.md`/F-LUA-3.

  - **Pas d'entrée `RELEASE_NOTES`, argumenté et confirmé par la revue.** Avant, `json_string()`
    rendait `NULL` sur l'octet invalide, la paire tombait, et `decodeSetState` appelait quand même
    `set_value("")` ; après, il reçoit la chaîne à U+FFFD. **Les deux écrivent du garbage**,
    l'observable ne change pas de nature — et **l'utilisateur écrit lui-même le script** qui
    produit ces octets.

  - **Défaut connu recalé à l'octet** : `setIOParam` / `waitForIO` `return 1` **sans rien
    empiler** ⇒ `Lunar::thunk` laisse les arguments et **le script récupère son propre dernier
    argument**, donc **tout test de statut sur `waitForIO` lit vrai**. `ScriptBindings.cpp` passe
    de **495 à 484 lignes**, `:293`/`:333` → **`:294`/`:334`** (vérifié ici en `python3`).
    **`T3.27` peut s'appuyer dessus** — et sa dépendance `E4.1j` est levée. `BOARD.md` recalé.
    ✅ **`T3.27` LIVRÉ (2026-08-25, branche `fix/t3.27`)** : `return 0` sur les deux — **divergence
    assumée d'avec la recommandation « booléen » du §2 de sa fiche**, motivée par la convention
    mesurée des ~~9~~ ⛔ **13** `lua_CFunction` (recompté en revue : `Lunar.h` en porte **4** de plus,
    `thunk`/`new_T`/`gc_T`/`tostring_T`, **toutes correctes** ⇒ **0 suspect sur 13**, la conclusion
    survit) et par le fait qu'aucune des deux fonctions n'a d'information de
    succès à rendre (un booléen serait la constante `true`). ⚠️ **Rupture observable silencieuse**
    pour un script qui testait le retour → `RELEASE_NOTES`. ⛔ **`F-LUA-3` corrigé** :
    `ScriptBindings.cpp` **était** lié dans `LuaSandbox_test`, son vert venait du **faux relink**
    (T3.36), pas d'une limite de périmètre.

  - **`F-REO-1` confirmé mort** : ses deux sites `jansson_to_string` de `ScriptBindings.cpp`
    n'existent plus.

  - ⚠️ **Écart de comptage avec le brief de merge, donné tel que recompté ici** : à l'intérieur du
    fichier neuf, le commit de bascule remplace **11 énoncés d'assertion** et en ajoute **4** ; sur
    les 11, **10 changent la valeur attendue et portent toutes l'annotation `MOVED BY E4.1j`**
    (annotations posées **dès le commit de caractérisation**, cf. son en-tête ligne 50), la 11ᵉ ne
    change **que le message d'échec** (`EXPECT_EQ("", type_str)`, expression inchangée) et n'est
    donc pas annotée — c'est la « reformulation d'une sentinelle » du delta documenté. Le commit de
    suites en remplace **2** et en ajoute **2**. Total : **13 énoncés réécrits, 6 nets ajoutés**
    (144 → 150 assertions, 32 cas). Les 4 catégories du delta documenté (ordre `msg`/`data`, casse
    hexa, paire supprimée → U+FFFD, reformulation de sentinelle) **couvrent la totalité** des
    changements.

  - **NON VÉRIFIÉ, à dire tel quel** : rien sous ASan, aucun `calaos_script` réel, aucun
    bout-à-bout socket, et **`ScriptExec.cpp` n'a jamais été rejoué contre le nouvel émetteur —
    compatibilité RAISONNÉE (les deux bouts décodent avec un vrai parseur), pas EXERCÉE.**

  - **Rien n'a été poussé.**

  - ### ⭐⭐ SUITES DE REVUE `T3.27` (R2, 2026-08-25) — **la plus grosse trouvaille DÉPASSE le ticket**

    **La revue a rendu MERGE SOUS RÉSERVE, 6 réserves, aucune bloquante sur `src/`.** Elle **valide
    le correctif** : `waitForIO` ne peut pas revenir sur échec (`ScriptExtern_main.cpp:101-127` ne
    rend `true` que si `waitIds.find(id) != end()`, sinon boucle ou `abortScript()` → `lua_error`)
    ⇒ **`return 0` tient**, `lua_pushboolean(L, true)` serait bien la constante. **Rien n'a été
    défait.** Ce qui suit est **la reprise**, chaque point **remesuré**, aucun cardinal recopié.

    - ⭐⭐ **DETTE MÉTHODOLOGIQUE TRANSVERSE OUVERTE — `FINDINGS.md` `F-LINK-1`.** La correction de
      `F-LUA-3` était juste mais **portée uniquement dans `FINDINGS.md`** ; le texte faux restait
      **lisible et non contredit** dans `E4.1j.md` et dans ce fichier. ⛔ **Et il y a bien plus que
      `F-LUA-3`** : les **85 cibles** de `tests/Makefile.am` ont été balayées
      (`_SOURCES`/`_LDADD`/`_DEPENDENCIES` **aplatis** en `python3`, variables Make développées —
      ⚠️ `CORE_TEST_LDADD` **contient** `CORE_SERVER_OBJECTS`, le balayage textuel sous-compte).
      **Sur CINQ déclarations « objet non lié », TROIS sont fausses ou à moitié fausses** :

      | déclaration | verdict | mesure |
      |---|---|---|
      | `ScriptBindings.o` « aucun binaire de test » | ⛔ **FAUSSE** | **1** cible (`LuaSandbox_test`) sur `d68e59f1`, `599fcea9`, `8bcffdc8`, `fb9d064c` — **jamais 0** |
      | « Squeezebox/**RoonPlayer**/MqttCtrl pas même liés » | ⛔ **FAUSSE pour RoonPlayer** | **8** cibles **dans le commit E4.0d lui-même** (`d68e59f1`), **17** sur master. `MqttCtrl` vraie à la date, **périmée** (**1** aujourd'hui) |
      | `core/RoonArgs_test` « seule suite à lier `RoonPlayer.o` » (T3.28) | ⛔ **FAUSSE** | **17** cibles, dont **16 `core/JsonApi*` antérieures** |
      | `WagoCtrl.o` « non lié **et ne peut pas l'être** » (T3.30) | ⚠️ **MOITIÉ FAUSSE** | non-lié **vrai** (0 partout) ; « ne peut pas » **faux** — `WagoCtrl.cpp` ∈ `calaos_server_SOURCES` **et** `calaos_wago_SOURCES`, l'`.o` **existe**. Limite réelle : **exécution** (`!is_connected()`) |
      | `Squeezebox.o` (F-SQBOX-2) · `HueOutputLightRGB.o` (E4.1d) | ✅ **vraies** | **0** · **1** (`core/LanHue_test`) |

      ⭐ **La leçon est une distinction, pas un chiffre : « lié » ≠ « exercé ».** Les 16 suites
      `core/JsonApi*` **lient** `RoonPlayer.o` sans jamais l'appeler. Dire « pas lié » quand on veut
      dire « jamais appelé » **change la conclusion** — « pas lié » se lit *irréductible* et ferme
      le sujet, « lié mais jamais exécuté » se lit *un cas à écrire*. Et **« ne peut pas être lié »
      n'a été vrai dans AUCUN des cas examinés.**
      ⇒ **Blocs `⛔ CORRECTION` posés à CHAQUE endroit** où une déclaration fausse est écrite,
      précédent de l'arbitrage #2 respecté (barrage `~~…~~`, jamais de réécriture silencieuse) :
      `E4.1j.md` · **ce fichier ×2** (journaux E4.1j **et** E4.0d) · `FINDINGS.md` ×2 (F-LUA-3 et
      E4.0d) · `T3.30.md` · `T3.28.md` · `BOARD.md` · **et le commentaire de `tests/Makefile.am`**
      (comment seul, zéro effet sur automake).
      ⚠️ **`T3.28` VIENT D'ÊTRE MERGÉE et justifie ses tripwires de TEXTE par cette limite fausse**
      ⇒ **fiche de suivi ouverte : [`T3.28b`](T3.28b.md)** (numéro vérifié libre, aucune occurrence
      de `T3.28b` ni `T3.40` dans `docs/`). ⛔ **Non traitée ici** : hors du périmètre de T3.27, et
      ⛔ **ni le correctif ni le filet de T3.28/T3.30 ne sont touchés** — ce sont des **affirmations**
      qui sont corrigées.

    - ⭐ **UN TROU RÉEL DU FILET, TROUVÉ PAR LA REVUE ET REFERMÉ.** Mutation **`MR1`** —
      `lua_toboolean(L,3)?"true":"false"` ⇄ `?"false":"true"` — sortait **VERTE 0/12, sortie 0** :
      **aucun cas ne passait un booléen Lua comme `value`**, la branche `lua_isboolean` de
      `setIOParam` n'était donc **jamais prise**. Refermé par **un cas d'une ligne**,
      `ABooleanValueIsWrittenAsTrueOrFalse` (`4813c143`), qui envoie **les DEUX booléens** —
      ⚠️ avec `true` seul l'échange reste **indistinguable pour la moitié du domaine**.
      **Rejoué, protocole complet** (`rm -f` `.o` **et** binaire, `cmp` d'application,
      `CXX      LuaScript/ScriptBindings.o` = 1, `CXXLD    LuaCalaosApi_test` = 1, code de sortie) :
      **MR1 rougit — 1 rouge, ce cas exactement, sortie 1** ; **témoin 0/13, sortie 0** ; et le
      nouvel oracle est **ROUGE sur l'état de caractérisation** (`8547fb9d`, **zéro ligne de
      `src/`**) : **7/13, sortie 1**. `MR2` de la revue (`ioMap.find(id) == end()` → `!=`) fait
      **6 rouges** — le filet est porteur là où il compte.

    - ⛔ **LA SIXIÈME VARIANTE DE FAUX VERT EST CONSIGNÉE — `FINDINGS.md` `F-HARN-1` — ET SA CAUSE
      RACINE N'EST PAS CELLE DES CINQ AUTRES.** Les cinq viennent de `_DEPENDENCIES` (`T3.36`) ; la
      sixième est **un défaut de HARNAIS** : mutation **non appliquée** ⇒ le cas tourne **non muté**
      ⇒ vert. ⚠️ **Elle survit à tous les garde-fous de la série** : `rm -f` fait, les deux lignes
      de journal présentes, code de sortie cohérent — ce qui manque n'est pas le lien, c'est **le
      mutant**. ⭐ **Falsifiée en revue** : en mode laxiste, `LAX_M2` donne `CXX …ScriptBindings.o`=1,
      `CXXLD LuaCalaosApi_test`=1, `.o` relié, **0 `FAILED`, sortie 0** — **un faux vert parfait**.
      ⇒ **remède distinct** : **`cmp` d'application** + **comparaison des ENSEMBLES** au témoin.
      **`DECISIONS.md` corrigé** — il écrivait « la cause racine des **CINQ** variantes ».

    - ⛔ **Comptes et identifiants recalés.** **13** `lua_CFunction` et non 9 (`Lunar.h` en porte
      **4** : `thunk`/`new_T`/`gc_T`/`tostring_T`, **relues, toutes correctes** ⇒ **0 suspect sur
      13**, la conclusion du ticket **survit**) — corrigé ici, dans `FINDINGS.md`, `BOARD.md` et
      `T3.27.md`. **`F-LUA-5` était un DOUBLON** (l'identifiant était pris par « la bascule perd le
      texte d'erreur ») ⇒ **renuméroté `F-LUA-7`**, premier libre vérifié. **`_DEPENDENCIES`
      remesuré** — trois chiffres divergents circulaient (« 47 des 84 », « 49/85 », « 50/96 ») :
      **49 des 83 binaires de test** sur `fb9d064c` = **49/84 `check_PROGRAMS`** sur **86** entrées
      `TESTS` (3 scripts shell) ; **50/84** sur cette branche. **Le piège est réel, seuls les
      cardinaux étaient faux**, et **le ratio monte à chaque suite ajoutée** : toujours dire sur
      quel arbre on mesure.

    - ⛔ **Doc périmée introduite par le ticket lui-même, corrigée** : `docs/09_lua_scripting.md`
      disait encore « Non couverts par un test : … `waitForIO()` » et son tableau « Tests » ne
      listait pas `tests/LuaCalaosApi_test.cpp`, alors que **7 des 13 cas** exercent `waitForIO()`.
      Corrigé, **avec la limite qui reste** : la **boucle bloquante** et le chemin `abort` ne sont
      **pas** exercés (le slot répond vrai au premier `emit()`).

    - **SECOND REBASE, sur `fb9d064c` (T3.28 mergée).** Le « `merge-tree` = 0 » de la fiche était
      **périmé** : **deux** conflits. ⭐ **`tests/Makefile.am` : le piège s'est reproduit à
      l'identique** — l'`endif` extérieur est **commun et hors** des deux blocs marqués, « garder
      les deux côtés » donnait **2 `if` pour 1 `endif`** ⇒ **queue régénérée, un `endif` par bloc**.
      Contrôle **tous préfixes `^if*` confondus** : **75 / 75**, profondeur **jamais négative**.
      **`BOARD.md` : ordonné par NUMÉRO** (T3.27 branche · T3.28 master mergée · T3.28a master ·
      T3.28b neuve), aucun doublon. `FINDINGS`/`ORCHESTRATION`/`RELEASE_NOTES` ont fusionné seuls.
      Après résolution **`merge-tree` = 0**. Comptes : **87 `TESTS`**, **85 `check_PROGRAMS`**,
      **`make check` 87/87**, **145 goldens, arbre `d4ebc61f` — aucun n'a bougé** (identique à
      master). ⚠️ **`T3.35` mergeait en parallèle** : si elle atteint `master` d'abord, **tout
      compte de suites monte de +1** — **remesurer, ne pas recopier**.

    - **NE PAS MERGER, NE PAS POUSSER** — la file était occupée par `T3.35`. La branche est livrée.

- **🔒 T3.29 ✅ MERGÉ (`bbde6c06`, 4 commits, `git rebase master` + `merge --ff-only`, historique
  linéaire, `make check` **83/83**) — l'ioDoc enseignait une syntaxe d'index que le parseur ne
  comprend pas, et `calaos_installer` l'affichait à tout le monde.**
  Périmètre réel : **8 chaînes d'ioDoc** — **7 dans `IO/Mqtt/MqttCtrl.cpp`** (`path`,
  `battery_path`, `connected_status_path`, `wireless_signal_path`, `uptime_path`,
  `ip_address_path`, `wifi_ssid_path`) et **1 dans `IO/Web/WebDocBase.cpp`** (`path`, littéral
  concaténé sur 5 lignes) : `weather[0]/description` → **`weather/[0]/description`**, et elles
  portent désormais **la règle** (« *array indices are their own path segment* ») et pas seulement
  l'exemple. Le parseur n'est **pas** touché. Commit de caractérisation `d95b8905` : **zéro ligne
  de `src/`**, **476 insertions / 0 suppression** ⇒ **aucune assertion préexistante modifiée**.
  Reconstruite à l'état master + tests seuls et **relinkée pour de vrai** (`rm -f` puis `CXXLD
  JsonPathSyntax_test`), la suite rend **24 verts / 3 rouges**, et les 3 sont **exactement** les cas
  que le correctif ferme : `IoDocIndexSyntax.{MqttNeverTeachesAnIndexGluedToItsKey,
  WebNeverTeachesAnIndexGluedToItsKey, TheIndexRuleIsSpelledOutAndNotOnlyShown}`. Goldens intacts :
  arbre `d4ebc61f…`, **145 fichiers** (145 `.json`, **aucun** ne contient de description d'ioDoc),
  identique à master. Suite **82 → 83** (recompté en `python3`, continuations `\` recollées ; unique
  ajout `JsonPathSyntax_test`). `tests/Makefile.am` **71 `^if*` / 71 `^endif`** (**70/70** sur
  master), profondeur jamais négative, **append pur octet pour octet** (`cur.startswith(master)`,
  **+1 449 octets**) — le bloc ajouté a **son propre `if HAVE_GTEST`/`endif`**, ce qui est
  précisément ce qui rend l'append sûr. Build distclean rejoué **après le rebase de merge**
  (`make -j16 && make check -j8`, agents concurrents, attendu par `docker wait`) :
  **`CXXLD JsonPathSyntax_test`** *(précédé d'un `rm -f` du binaire — sans quoi le vert ne prouve
  rien, cf. T3.36)*, **`PASS: JsonPathSyntax_test`**, **83 PASS / 0 FAIL / 0 SKIP / 0 ERROR**,
  **0 `error:`**, code de sortie **0**.

  - ⭐ **CE QUE `calaos_installer` EMBARQUERA, MESURÉ** : `calaos_server --gendoc` avant/après donne
    **68 descriptions changées et RIEN D'AUTRE**. Même **70** types d'IO, **8 260** feuilles JSON,
    **jeux de chemins identiques**, et les 68 feuilles modifiées sont **toutes** sous
    `…/parameters/[N]/description`. `io_doc.json` **+68 / −68** lignes (525 588 → 529 600 o),
    `io_doc.md` **+76 / −76** (227 362 → 231 374 o) — le md en compte 8 de plus parce que la
    description Web est repliée. `weather[N]` **68 → 0**, `weather/[N]` **0 → 68**, dans les deux
    fichiers. Réparti sur **9 types MQTT × 7 paramètres + 5 types Web × 1**.
    ⚠️ `data/doc/` n'est **pas suivi par git** et n'existe pas dans un arbre propre : **rien à
    régénérer en dépôt**, la doc est produite au moment de l'empaquetage.

  - **`po/` — édition chirurgicale, choix validé par la revue.** **9 catalogues suivis**, **8
    changés** ; `po/en.po` est légitimement **intouché** (il ne contient aucune des 8 chaînes).
    ⚠️ *Le brief de merge disait « 8 catalogues suivis » : il y en a **9**, dont 8 concernés.*
    `weather[0]` **8 → 0** partout, **9 → 0** dans `fr.po` (8 `msgid` + **1 `msgstr` traduit**,
    corrigé dans le même geste). **0 entrée `fuzzy` créée** (`fr` reste à **6**), **0 traduction
    perdue** : 7 des 8 entrées n'étaient traduites nulle part, la 8ᵉ a suivi son `msgid` ; `fr.po`
    **404 / 6 / 24 — identique avant et après**. `msgfmt -c` **vert sur les 9 suivis** *et* sur
    `en@quot.po` / `en@boldquot.po`, qui sont **générés et non suivis**.
    L'auteur a **écarté `make -C po update-po`** — il régénérait `calaos.pot` depuis
    `POT-Creation-Date: 2025-06-29` (434 → 820 messages) et noyait les 8 lignes utiles dans
    **~19 000** (+2244 / −427 par catalogue), en marchant sur le périmètre des autres tickets.

  - ⭐ **L'ARBITRAGE A ÉTÉ FERMÉ PAR UNE MESURE, PAS PAR UN AVIS.** « Accepter aussi l'ancienne
    syntaxe » paraissait généreux et était **dangereux** : **`weather[0]` n'est pas un jeton inerte,
    c'est une recherche de clé d'objet parfaitement valide**. Sur une charge Zigbee2MQTT réelle
    (`{"action[0]":"single", …}`), `resolve("action[0]")` rend `"single"`. Découper aussi sur `[`
    **casserait ce cas en silence**, et les noms de clés d'un équipement tiers ne sont pas sous
    notre contrôle. La revue a **implémenté l'option B** pour vérifier, et la suite de
    caractérisation l'a **rejetée avec 2 rouges**. ⇒ **la fermeture est désormais activement gardée
    par les tests**, pas seulement argumentée. Cas témoin :
    `AGluedIndexStillMatchesAKeySpelledThatWay`.

  - ⭐ **PLANTAGE SERVEUR TROUVÉ AU PASSAGE → [`T3.35`](T3.35.md), priorité haute.** Un `path`
    valant `[` : `erase(0, 1)` vide la chaîne, `pop_back()` **sous-flue le `size_t`**, et le
    `Utils::from_string()` qui suit est **HORS du `try`** (`MqttCtrl.cpp:132`, `WebCtrl.cpp:202`,
    lignes vérifiées au source) ⇒ **`std::bad_alloc` s'échappe de `getValueJson()`**, aucun `catch`
    jusqu'à `main.cpp` ⇒ **`std::terminate()`**. **Une faute de frappe dans un paramètre de
    configuration fait terminer `calaos_server`.** ⛔ **Aucun vecteur distant** — le `path` n'est
    écrit que par `calaos_installer`. ⚠️ **La première rédaction disait « UB théorique » : elle
    sous-vendait le défaut.** Le mot juste est **plantage déterministe**, et la correction a été
    portée **aux trois endroits** (`FINDINGS.md`, la fiche, l'en-tête du test).

  - ⭐ **DÉFAUT DE HARNAIS À L'ÉCHELLE DU DÉPÔT → [`T3.36`](T3.36.md), PRIORITÉ HAUTE.** ⚠️ **Ce
    ticket-ci a été mordu POUR DE VRAI** : `JsonPathSyntax_test_DEPENDENCIES =
    $(top_builddir)/src/lib/libcalaos_common.la` retire les `.o` serveur des prérequis du binaire,
    donc **il ne se relinke pas quand un `.o` serveur change**. La revue a reproduit le faux vert —
    forme fautive réintroduite dans `MqttCtrl.cpp`, `make check` → **83/83 PASS, relink `CXXLD`
    = 0**. **Chiffres recomptés ici en `python3`, continuations `\` recollées, et ils tombent
    juste : 81 `check_PROGRAMS` dont 1 helper ⇒ 80 binaires** (`TESTS` = 83 = 80 + 3 scripts) ;
    **48 overrides `_DEPENDENCIES`**, dont **47 relient réellement des `.o` serveur** et **1 est
    inoffensif** (`JanssonResidues_test`) — donc **47 sur 80**, *et non 48/82*.
    ⚠️ **Piège de comptage à consigner** : `CORE_TEST_LDADD` **contient** `CORE_SERVER_OBJECTS`,
    donc chercher `$(CALAOS_SERVER_BUILDDIR)` en toutes lettres dans les `_LDADD` donne **27** au
    lieu de 47 — *reproduit à l'identique ici*. C'est la **cause racine des cinq variantes de faux
    vert / faux rouge** de la série. **Seul contrôle valable, jusqu'à T3.36 : `rm -f tests/<suite>`,
    puis exiger la ligne `CXXLD <suite>` ET le code de sortie.**

  - **L'OPTION C, RETENUE ET SÛRE** : journaliser « *did you mean `a/[0]/b` ?* » dans le `catch` de
    la **branche objet** (`MqttCtrl.cpp:151-155`, `WebCtrl.cpp:221-225`). ⭐ **Le faux positif est
    IMPOSSIBLE par construction** : on n'entre dans ce `catch` que si `parent.at(val)` a **déjà**
    jeté — donc une charge dont la clé s'appelle vraiment `action[0]` **n'y passe jamais**. C'est
    exactement ce qui rend C sûre là où B ne l'était pas. **Décision utilisateur : groupée avec le
    plantage dans `T3.35`** — même `catch`, même revue, un seul passage sur ces lignes.

  - **UNE IMPRÉCISION CORRIGÉE, ET ELLE COMPTE** : `cWarning()` **n'est pas** un domaine filtré
    (`LogSetup.h:29`) — le message `[WRN] … subpath not found` **part bien dans le log serveur par
    défaut**. Le vrai problème n'est pas la visibilité du message, c'est son **destinataire** : la
    faute se commet dans **`calaos_installer`**, le message atterrit dans le log de
    **`calaos_server`**. La même erreur figurait dans `RELEASE_NOTES.md` — corrigée aux deux
    endroits.

  - **LES DEUX PARSEURS JUMEAUX ONT DÉJÀ DIVERGÉ → [`T3.37`](T3.37.md).** Chemin vide : **MQTT rend
    la charge brute**, **Web rend `""`** — divergence **figée par test** ici, donc connue avant
    toute unification. Et **tous les défauts sont en double** : le plantage du `[`, l'index non
    numérique qui lit l'**élément 0 en silence** (famille T3.25), `from_string` hors du `try`, le
    message d'échec sans indication. ⇒ **une seule extraction fermerait les quatre**.

  - **HORS PÉRIMÈTRE, CONSIGNÉ → [`T3.38`](T3.38.md)** : `IO/OLA/OLAOutputLightRGB.cpp:35` déclare
    `channel_red` sur `0..9999` quand `:36`/`:37` déclarent green et blue sur `0..512` (valeur
    recopiée de la ligne `universe`), et `IO/Mqtt/MqttOutputLightRGB.cpp:43` dit « read the **x**
    value » pour `path_y`. **Non corrigé ici** : `.wave46/e4.1j` est **vivante sur `IO/OLA/`**,
    d'où la dépendance déclarée `E4.1j`.

  - ⛔ **NON VÉRIFIÉ** : aucun ASan/UBSan ; aucun broker MQTT ni téléchargement Web réel ;
    `calaos_installer` **n'a pas été construit** — le fait qu'il embarque `io_doc.json` est
    **déduit, pas observé**. **RIEN N'A ÉTÉ POUSSÉ.**

  - **LE MERGE (agent dédié).** `master` a avancé **pendant** les contrôles (`d1462d9e` →
    `79390a0d`, arbitrages utilisateur) : rebase rejoué, **un seul conflit, `BOARD.md`** — master
    ajoute la ligne `T3.39`, la branche ajoute `T3.35`-`T3.38`, **au même endroit du même tableau**.
    **Résolution : garder TOUTES les lignes, aucune n'est choisie** — ici le tableau est **trié par
    numéro de ticket**, donc l'ordre retenu est `T3.35`→`T3.39` plutôt que « master d'abord », ce
    qui préserve à la fois les 5 lignes **et** l'invariant de tri du fichier. Contrôle : **152
    lignes de tableau à la base, 154 sur master, 156 sur la branche, 158 après résolution** ; les
    **4** lignes ajoutées/modifiées par master **et** les **5** de la branche sont **toutes**
    présentes ; `^## ` **6 avant / 6 après** des deux côtés, `^### ` 1 / 1 ; aucun `---` non précédé
    d'une ligne vide. ⭐ **Le rebase n'a déplacé aucune ligne de code** : les arbres git de `src`,
    `tests`, `po`, `data`, `Makefile.am` et `configure.ac` sont **identiques** à ceux du commit
    construit (`bcfe9d2e`) — **le contenu mergé est donc exactement celui qui a été construit et
    exercé**. Le delta de rebase vaut **une ligne de `BOARD.md`**.
    ⚠️ **Et la règle « tant qu'un agent de merge est en vol, l'orchestrateur ne commite rien sur
    master » n'a de nouveau pas été tenue** — `79390a0d` touche `BOARD.md` et `FINDINGS.md`,
    exactement les deux fichiers qu'un agent de merge écrit. Le coût a été un rebase et un conflit
    de plus ; il était évitable.

- **🔒 E4.1f ✅ MERGÉ (`5ab5827a`, 5 commits, `git rebase master` + `merge --ff-only`, historique
  linéaire, `make check` **82/82**) — le wire OLA, le seul du dépôt à vrais entiers JSON, et le
  ticket qui a fait tomber une famille de défauts bien plus large que lui.**
  Périmètre réel : `IO/OLA/OLACtrl.cpp`, `IO/OLA/OLAExternProc_main.cpp`, l'**en-tête neuf de
  production** `IO/OLA/OLAWire.h` (inclus tel quel par les deux binaires **et** par le test), plus
  **2 lignes** de `_SOURCES` dans `src/bin/calaos_server/Makefile.am` — aucun débordement. Commit de
  caractérisation `4fcaeebb` : **zéro ligne de `src/`** (2 fichiers, tous `tests/`). **Aucune
  assertion préexistante modifiée** : sur les 5 commits, les seuls `tests/` touchés sont
  `tests/OLAWire_test.cpp` (neuf) et `tests/Makefile.am`. Goldens intacts : arbre `d4ebc61f…`
  (chemin réel `tests/core/golden`), **145 fichiers**, identique à master. Suite **81 → 82**
  (recompté en `python3`, continuations `\` comprises ; unique ajout `OLAWire_test`). Équilibre
  `tests/Makefile.am` **70 `^if*` / 70 `^endif`** tous préfixes confondus (**69/69** sur master),
  profondeur jamais négative ; **append pur octet pour octet** (`cur.startswith(master)`,
  **+2 313 octets**). Build distclean rejoué **après le rebase de merge** (`make -j16 &&
  make check -j8`, agents concurrents, attendu par `docker wait`) :
  **`Open Lightning Architecture support..: yes`** (le libellé n'est **pas** « OLA support »),
  **`CXX IO/OLA/OLAExternProc_main.o`**, **`CXXLD calaos_ola`**, `CXX IO/OLA/OLACtrl.o`,
  **`CXXLD OLAWire_test`**, **`PASS: OLAWire_test`**, **0 `error:`**, code de sortie **0**,
  **82 PASS / 0 FAIL / 0 SKIP**. ⚠️ `calaos_ola` est sous `if HAVE_LIBOLA` : **sans ces deux lignes
  la moitié du périmètre n'est pas compilée et le vert ne prouve rien**.

  - ⭐ **UN DÉFAUT RÉEL TROUVÉ PAR LA CARACTÉRISATION, CORRIGÉ DANS UN COMMIT SÉPARÉ**
    (`5c4d2874`). **`Utils::from_string("")` retourne `true` en n'écrivant RIEN** — le sentry échoue
    **avant** l'extraction, il n'y a **pas** de « 0 en cas d'échec » — et `calaos_ola` passait un
    **`unsigned int` non initialisé** à `DmxBuffer::SetChannel()`. Valeurs observées sur machine :
    **21845, 21942, 22007, 22069, 22072, 64**. Corrigé par `{0,0}` dans `decodeMessage` ;
    **`Utils::from_string` n'est PAS touchée ici**.

  - ⭐ **LE BALAYAGE QUI EN DÉCOULE DÉPASSE CE TICKET** : **~319-320 sites d'appel**, **310 ignorent
    le retour (97 %)**, **157-165 passent une locale non initialisée**, **112 sans aucune garde**.
    Trois blocs : **`WagoExternProc_main.cpp` (12 sites, jumeau EXACT du défaut corrigé ici)** ·
    **`KNXExternProc_main.cpp:144-147`/`:157-160`** — ⚠️ l'accès `tokens[1..2]` **hors bornes**
    annoncé est **FAUX**, `Utils::split` **pade** ; le vrai défaut est `b`/`c` **indéterminés** ⇒
    **adresse de groupe arbitraire sur le bus** — et un chemin **atteignable À DISTANCE** via
    `JsonApi.cpp:774/777` → `OutputShutter`/`ShutterSmart`/`LightDimmer`/`LightRGB`.
    **Le cas OLA était le seul INATTEIGNABLE de la famille**, ce qui valide l'absence d'entrée
    `RELEASE_NOTES` **pour ce ticket seul**. Ticket dédié **`T3.25`** ouvert, **en cours**.

  - ⭐ **LA LISTE BLOQUANT `E4.1x` TOMBE À DEUX**, mesuré **deux fois** par mutation **fidèle**
    (`ExternProc.h` cesse de **déléguer**, `Jansson_Addition.h` **intact**) : **`ScriptBindings.o` et
    `ScriptExtern_main.o`** — soit **exactement le périmètre d'`E4.1j`**, le dernier ticket ouvert de
    la vague 1. OLA **et** Wago compilent désormais sans jansson.

  - **TYPAGE APPLIQUÉ, 2ᵉ APPLICATION DE LA SÉRIE** (`DmxChannel`/`DmxLevel`/`DimmerPercent`/
    `RedChannel`/`GreenChannel`/`BlueChannel`, constructeurs `explicit` — donc **pas** des agrégats) :
    permuter les arguments de `buildSetValueMessage` **ne compile pas**, rouge↔bleu non plus.
    **Résiduel jugé LIMITE INTRINSÈQUE par la revue** — `RedChannel(channel_blue)` compile ; le
    fermer exigerait que l'enveloppe *produise* la valeur, ce qui **déplace le trou d'un cran**.
    **À recommander tel quel** (voir `T3.31`).

  - ⚠️ **LA LEÇON DES TROIS CHIFFRES FAUX.** Le § « Acceptation » de la fiche portait des mesures du
    **PREMIER build**, encore polluées par le cas que le commit de correction a fait tomber.
    ⭐ **Une mesure prise avant le correctif et conservée dans la fiche n'est pas fausse quand on
    l'écrit — elle le devient.** **Seul le tableau mesuré en distclean sur l'arbre final fait foi.**

  - **M9 : TROU DE MUTATION-KILL DÉCLARÉ, PAS TEST INSTABLE.** L'assertion est **déterministe après
    correctif** (le `{0,0}` la garantit) ; c'est la **détection du mutant** qui est indéterminée. Le
    mécanisme est épinglé de façon déterministe par un autre cas.

  - **LE COMMENTAIRE MYTHIQUE RETIRÉ** : `OLAWire.h` affirmait « *since C++11 a failed extraction
    stores 0* » — exactement ce que le correctif douze lignes plus bas réfute.

  - **HORS PÉRIMÈTRE, CONSIGNÉS** : `OutputShutter.cpp:120` fait `erase(0, 11)` contre un préfixe de
    **13** caractères ⇒ **`impulse down` inopérante** (ticket **`T3.34`**) · `OLAOutputLightRGB.cpp`
    documente `channel_red` sur `0..9999` contre `0..512` pour vert et bleu.

  - ⛔ **NON VÉRIFIÉ** : aucun test bout-à-bout avec un vrai `olad` ni gradateur DMX ; M9 sur un
    autre compilateur ; M1/M2/M3/M7/M8 non rejoués par la revue. **Rien n'a été poussé.**

- **🔒 E4.1d ✅ MERGÉ (`9a1a5499`, 6 commits, `git rebase master` ×2 + `merge --ff-only`, historique
  linéaire, `make check` **81/81**) — les deux lecteurs de JSON TIERS, et le ticket où la fiche
  prescrivait elle-même un `std::terminate`.**
  Périmètre réel : `Audio/Squeezebox.cpp`, `IO/Hue/HueOutputLightRGB.cpp`, les **deux en-têtes neufs
  de production** `Audio/SqueezeboxWire.h` et `IO/Hue/HueWire.h` (déclarés dans
  `calaos_server_SOURCES`, **2 lignes** de `src/bin/calaos_server/Makefile.am` isolées dans leur
  propre commit `a334e482`), plus `tests/Makefile.am` et les deux fichiers neufs
  `tests/{SqueezeboxWire,HueWire}_test.cpp`. Commit de caractérisation `7efa2ffe` : **zéro ligne de
  `src/`** (3 fichiers, tous `tests/`). **Aucune assertion préexistante modifiée** : sur les 6
  commits, les seuls `tests/` touchés sont ces trois-là. Goldens intacts : arbre `d4ebc61f…`,
  **145 fichiers**, identique à master. Suite **79 → 81** (recompté en `python3`, continuations `\`
  comprises ; les deux seuls ajouts sont `SqueezeboxWire_test` et `HueWire_test`). Équilibre
  `tests/Makefile.am` **69 `^if*` / 69 `^endif`** tous préfixes confondus (**68/68** sur master),
  profondeur jamais négative ; **append pur octet pour octet** (`cur.startswith(master)`, **+2 514
  octets**) — **aucun conflit** au rebase, sur aucun fichier. Build distclean rejoué **après le
  rebase de merge** (`make -j16 && make check -j8`, deux agents concurrents, attendu par
  `docker wait`) : `CXX Audio/Squeezebox.o`, `CXX IO/Hue/HueOutputLightRGB.o`,
  **`CXXLD SqueezeboxWire_test`**, **`CXXLD HueWire_test`** (×1 chacun), **0 `error:`**, code de
  sortie **0**, **81/81 PASS / 0 FAIL / 0 SKIP**.

  - ⭐ **LA FICHE PRESCRIVAIT UN `std::terminate`, ET L'AUTEUR A REFUSÉ SA RECOMMANDATION.**
    `E4.1d.md` disait « **Utilise `j.value("sat", 0)`** ». Mesuré : `j.value("sat", 0)` **lève**
    sur **4 des 7** formes qu'un pont peut renvoyer — `"77"`, `null`, `[77]`, `{}` — et côté
    booléens `j.value("on", false)` lève sur `1`, `0`, `"true"`, `null`, **là où jansson rendait
    `false`**. Le site est un callback `UrlDownloader` **sans `try`/`catch`**, sur un scrutin de
    **2 s** déclenché par le pont : la recommandation, appliquée telle quelle, armait un
    `terminate` répété. Écrits à la place : `integerOrDefault` / `booleanOrDefault`, **fidèles à
    jansson sur les 9 et 7 formes**. Bonus mesuré : sur `{"sat":99999999999999999999}`, `j.value`
    rend **−2147483648**, le helper rend **0**.

  - ⭐ **UNE DIVERGENCE D'ACCEPTATION, ÉPINGLÉE — UN CAS DE CHAQUE CÔTÉ.** Sur `\u0000` **ÉCHAPPÉ**
    (la séquence `\u0000` dans le texte JSON, six caractères), jansson **refuse**
    (`\u0000 is not allowed without JSON_ALLOW_NUL`, et « NUL byte in object key not supported »), **nlohmann accepte** et décode
    un vrai `0x00` ⇒ Hue passe de `Malformed` à `Ok`, Squeezebox quitte le **repli CLI** pour le
    document parsé. **Le NUL BRUT, lui, est refusé par les deux** ⇒ le passage `c_str()` →
    `std::string` **ne creuse aucun trou**.

  - **INVARIANTS D'OCTETS : DÉFENSIFS, ET MESURÉS COMME TELS.** Les deux bibliothèques refusent
    l'UTF-8 invalide **dès le parse** (0xff, `\xc3` tronqué, demi-surrogate, surlong `C0 80`, 0x80
    nu, et 0xff **dans une clé**) ⇒ **rien ne peut présenter un octet invalide au `dump()`**.
    **Ce n'est pas KNX.** `HueWire_test` **ne revendique aucun oracle d'octets** — il n'y a **pas un
    `dump()`** dans `HueOutputLightRGB.cpp` ; les seuls oracles d'octets du ticket sont dans
    `SqueezeboxWire_test`, sur l'unique `dump()` du périmètre (une trace `cDebug()`).

  - ⭐ **RÈGLE DE SÉRIE POSÉE DANS `DECISIONS.md`** : `dump(N, ' ', true, error_handler_t::replace)`
    **partout**, **aucune exception pour le gestionnaire**. Recensement `python3` : **38 sites
    réels** dans `src/` (la 39ᵉ occurrence est une **ligne de commentaire**, `KNXCtrl.h:97`),
    **0 nu**, **17 `true` / 21 `false`**. ⚠️ **La ligne de partage a TROIS côtés, pas deux** :
    journal machine (échappé, précédent `NotifManager.cpp:258`), **écriture de fichier sur disque**
    (`CalaosConfig.cpp:558`, `iostates.cache`), sortie humaine d'outil interactif
    (`calaos_config.cpp:347/:616`, `std::cout`). L'argument décisif : **dissocier les trois
    invariants site par site est ce qui finit par laisser passer un `dump()` nu dans un log**,
    c'est-à-dire le `terminate` de KNX **déplacé dans une trace**.

  - ⭐ **PREMIER TICKET À APPLIQUER LA MITIGATION PAR TYPAGE** — `LmsHost{}` et un `LightState`
    nommé rendent la permutation positionnelle **non compilable** (l'ancienne signature à deux
    `std::string` ne compile plus non plus). **DEUX RÉSIDUELS**, tous deux écrits dans les en-têtes :
    emballer la **mauvaise variable**, **et** `LightState` étant un **agrégat** de 3 `int` + 2
    `bool`, une initialisation **positionnelle** `{100,200,30000,true,true}` compile et **permute
    `sat`/`bri` en silence** — latent aujourd'hui, mais la fermeture ne tient que **tant que personne
    n'écrit d'agrégat**. **Trou des sites d'appel confirmé mesuré** :
    `updateHueState(update.color, !update.on)` permuté **compile** et laisse `LanHue_test`
    **10/10 vert** ; et **aucun binaire ne lie `Squeezebox.o`**.

  - **DEUX ACQUIS QUE L'AUTEUR A TROUVÉS CONTRE LUI-MÊME.** (a) La moitié `is_discarded()` de ses
    deux gardes est **redondante** — la retirer est un **mutant équivalent, 0 rouge** ; elle est
    **gardée mais annotée**, pour qu'on ne la croie pas porteuse. (b) ⚠️ **Son script de mutation
    restaure par `git checkout` et, lancé AVANT commit, a effacé toutes ses éditions d'en-tête** —
    retrouvées et réappliquées. **La parade est de COMMITTER AVANT la campagne**, pas de « faire
    attention » : à retenir pour les tickets restants.

  - **D'OÙ VIENNENT LES OCTETS** : Squeezebox lit `result.remoteMeta`, **métadonnée d'un flux
    distant** relayée telle quelle par LMS (titre, artiste, nom de webradio) ⇒ non-ASCII **réel** ;
    Hue lit `name` (saisi dans l'app) et des chaînes d'erreur du pont, **dont aucune n'atteint un
    `dump()`**.

  - **NON VÉRIFIÉ, à ne pas surestimer** : **aucun `make dist`/`distcheck` réel**, **rien sous
    ASan**, **aucun `calaos_server` exécuté**, **aucun vrai pont Hue ni vrai LMS**.
    **Rien n'a été poussé.**

- **🔒 E4.1c ✅ MERGÉ (`35ce4cbf`, 5 commits, `git rebase master` + `merge --ff-only`, historique
  linéaire, `make check` **79/79**) — le seul ticket de la série qui ne migre presque rien : il
  RETIRE TROIS CHOSES MORTES, `src/` = **−9 / +0, AUCUNE addition**.**
  Périmètre réel : `IO/ExternProc.h` (le `#include <jansson.h>` de la ligne 26),
  `IO/Web/WebCtrl.cpp` (son include), `HttpClient.cpp` (la macro de compat `json_array_foreach`,
  `:111-116`). **Aucun octet observable ne change** : les trois fichiers portent **0 `dump()`**,
  **0 émission nlohmann**, **0 appel jansson** ⇒ aucune entrée `RELEASE_NOTES`. Commit de
  caractérisation `9c920a31`, **zéro ligne de `src/`** — vérifié sur le commit : **2 fichiers**,
  `tests/Makefile.am` et le fichier neuf `tests/JanssonResidues_test.cpp`. **Aucune assertion
  préexistante modifiée** : sur les **5** commits, les seuls fichiers `tests/` touchés sont ces
  deux-là. Goldens intacts : arbre git `d4ebc61f…`, **145 fichiers**, identique à master. Suite
  **78 → 79** (recompté en `python3`, continuations `\` comprises ; le seul ajout est
  `JanssonResidues_test`). Équilibre `tests/Makefile.am` après merge : **68 `^if*` / 68 `^endif`**
  tous préfixes confondus (**67/67** sur master ; tous littéralement `if `, ni `ifeq` ni `ifdef`),
  profondeur **jamais négative**. Bloc **append pur, octet pour octet** (`cur.startswith(master)`,
  135 210 → 136 785 octets, +1 575).

  - ⭐⭐ **LE RÉSULTAT CENTRAL POUR `E4.1x` : LA LISTE EST 6, PAS 10. Trois chiffres, et l'écart
    expliqué.** **10** = artefact d'une **simulation infidèle** : retirer `<jansson.h>` de
    `Jansson_Addition.h` casse **ce fichier lui-même** (il utilise `json_t` dans son corps) —
    mesuré **4158 `error:`, 128 objets en échec**, ça scie *toutes* les branches, pas celles
    d'`ExternProc.h`. **8** = les 10 moins **deux mentions de PROSE** (`IO/KNX/KNXCtrl.cpp:310`,
    `IO/KNX/KNXExternProc_main.cpp:33` : `json_loads` **en commentaire seul**, 0 code). ⭐ **6** =
    mutation **fidèle** — `ExternProc.h` cesse de **déléguer**, `Jansson_Addition.h` **intact** —
    obtenue **deux fois indépendamment**, graphe `python3` **et** compilateur (`make -C src -j12 -k`
    → exactement 6 objets en échec, les mêmes 6 fichiers) : **`IO/OLA/OLACtrl.cpp`,
    `IO/OLA/OLAExternProc_main.cpp`, `IO/Wago/WagoMap.cpp`, `IO/Wago/WagoExternProc_main.cpp`,
    `LuaScript/ScriptBindings.cpp`, `LuaScript/ScriptExtern_main.cpp`** — **aucune KNX**.
    **L'écart 8 → 6** : `EventManager.cpp` et `IO/Scenario.cpp` **n'ont JAMAIS dépendu
    d'`ExternProc.h`** — ils tiennent `Jansson_Addition.h` par `EventManager.h`, et par
    `IO/Scenario.h → IOBase.h → EventManager.h`. La mutation infidèle les faisait tomber en sciant
    **la branche commune**. ⇒ la condition de casse est la **délégation** d'`ExternProc.h` vers
    `Jansson_Addition.h`, **jamais la ligne 26**.

  - ⭐ **LA LEÇON GÉNÉRALISABLE, à appliquer à toute la série** : *une simulation de suppression
    d'en-tête doit être **FIDÈLE** au ticket cible* — sinon on ne mesure pas les conséquences de la
    suppression qu'on prépare, mais celles **d'une autre**. Et *une sonde `#if defined(X)` doit
    garder **son propre corps** sous `X`* : sinon elle ne rougit pas, **elle casse le build**.
    C'est exactement ce que la revue a construit (en-tête écran `#include_next` + `#undef
    json_array_foreach`) et ce que le correctif `1410bbca` a refermé : avant, **RC=2, 1 `error:`,
    aucun `CXXLD`** ⇒ pas de binaire, message d'assertion jamais affiché ; après, **RC=0, `CXXLD`
    ×1, exactement 1 rouge**, le cas visé.

  - ⭐ **L'EXPOSITION D'`E4.1x`, MESURÉE — et la réserve initiale était fausse partout.** « OWFS
    non compilé sur un `./configure` nu » **ne tenait sur AUCUNE machine** : `OWCtrl.cpp` est dans
    `calaos_server_SOURCES` (`src/bin/calaos_server/Makefile.am:184`) et `calaos_1wire` dans
    `bin_PROGRAMS` (`:390`), **sans aucun garde `HAVE_OWCAPI`** ; et de toute façon ces deux
    fichiers portent **0 symbole jansson**. De plus **`--with-owfs` / `--with-mqtt` / `--with-knx` /
    `--with-ola` N'EXISTENT PAS** : `configure.ac` ne porte que **`--with-mcp`** et
    **`--enable-asan`**, tout le reste est **auto-détecté** (`owcapi.h`, `libola.pc`, `eibclient.h`,
    `mosquitto.h`). ⭐ **Le vrai trou conditionnel est ailleurs, et il vise E4.1x** : `calaos_ola`
    (`:400 if HAVE_LIBOLA`), `calaos_knx` (`:413 if HAVE_LIBKNX`) et `calaos_mqtt`
    (`:428 if HAVE_LIBMOSQUITTO`) **sont** sous conditions ⇒ **une machine sans `libola` ne compile
    JAMAIS `IO/OLA/OLAExternProc_main.cpp`**, l'un des **6** vrais casseurs. E4.1x ne peut donc pas
    conclure sur un seul environnement : soit il construit avec `libola`, `eibclient` **et**
    `mosquitto` présents, soit il **déclare** que sa mesure ne couvre pas `OLAExternProc_main.cpp`.

  - **`E4.1x.md` A ÉTÉ MIS À JOUR PAR CE TICKET** : `jansson >= 2.5` est à **`configure.ac:51`**
    (et non `:52`, cité faux par les deux fiches — ⚠️ **`BOARD.md` porte encore `:52` sur la ligne
    `E4.1x`, à corriger là-bas**) ; les `--with-*` inexistants remplacés par la vraie consigne ; un
    § portant la liste des **6** ; l'exposition conditionnelle ci-dessus ; et **l'ORDRE DE
    SUPPRIMER `tests/JanssonResidues_test.cpp`** — il appelle l'API C jansson **exprès** et
    remontera dans le `grep -rn 'json_t\|jansson' src tests` de son critère 1 d'acceptation ; la
    bonne réponse là-bas est de **le supprimer**, pas de le porter. Le tripwire
    `…StillDelegatesToJanssonAddition` **rougira mécaniquement** quand E4.1x retirera la
    délégation : **la dette se dénonce elle-même**.

  - **LA MACRO ÉTAIT MORTE DEPUIS TOUJOURS, pas « depuis jansson 2.5 »** : `HttpClient.cpp` voyait
    déjà `<jansson.h>` dès sa **ligne 21** (`RemoteUIProvisioningHandler.h → RemoteUIManager.h →
    EventManager.h → Jansson_Addition.h`) puis de nouveau en **23** (`HttpClient.h:27 →
    JsonApiHandlerHttp.h:24 → JsonApi.h:25`), donc son `#ifndef` de la ligne **111** était évalué
    **après** que jansson eut défini la macro : il **n'a jamais pu se déclencher**, quelle que soit
    la version. Ses **16 sites d'appel** de `json_array_foreach` vivent tous dans **d'autres unités**
    (`JsonApiHandlerHttp.cpp`, `JsonApiHandlerWS.cpp`) et sont **intacts** — la macro était dans un
    `.cpp`, elle ne pouvait fuir nulle part.

  - **LE RÉGIME DE PREUVE, ASSUMÉ ET ÉCRIT PAR L'AUTEUR** : **4 cas sur 6 lisent le TEXTE des
    sources** (via `-DCALAOS_TOP_SRCDIR`, sur le modèle de `CALAOS_GOLDEN_DIR`) — ce sont des
    **garde-fous de non-réintroduction, PAS des oracles** ; les deux prémisses exécutables le sont
    (`ExternProcHeaderAloneStillProvidesTheJanssonApi`,
    `JanssonProvidesArrayForeachNativelyAtTheConfiguredFloor`). ⇒ **le juge de ce ticket est le
    COMPILATEUR**, et il a jugé : build **distclean complet** rejoué après le rebase de merge, avec
    `CXX IO/ExternProc.o`, `CXX IO/Web/WebCtrl.o`, `CXX HttpClient.o`, `CXXLD JanssonResidues_test`
    (×1 chacun) et les **5 binaires** `CXXLD calaos_server / calaos_1wire / calaos_ola / calaos_knx /
    calaos_mqtt`, **0 `error:`**, code de sortie **0**. **5 contre-mutations de production → 5
    ensembles de rouges DISTINCTS**, témoin sans mutation **0 rouge**. ⭐ **M2 (échange
    `Jansson_Addition.h` ↔ `<jansson.h>`) est la preuve directe de la redondance** : l'échange
    laisse l'oracle d'exécution **VERT**, les deux lignes sont **interchangeables**.

  - **NON MESURÉ, à ne pas surestimer** : **rien sous ASan** ; **aucun build sans `libola` /
    `eibclient` / `mosquitto`** (donc l'exposition conditionnelle ci-dessus est **raisonnée, pas
    exercée**) ; **aucun `calaos_server` exécuté**. **Rien n'a été poussé.**

- **🔒 E4.1h ✅ MERGÉ (`63cf379a`, 6 commits, `git rebase master` **no-op** + ff-only, historique
  linéaire, `make check` **78/78**) — la bascule du wire Wago, ET le bug `values` CORRIGÉ contre la
  recommandation de la fiche, parce que la mesure a retiré le motif qui la justifiait.**
  Périmètre réel : `IO/Wago/WagoMap.cpp`, `IO/Wago/WagoExternProc_main.cpp` (`grep jansson` = **0**
  et **0** appel `json_*` sur les deux), le fichier **neuf de production** `IO/Wago/WagoWire.h`
  (déclaré dans **les deux** `_SOURCES` — `calaos_server_SOURCES` **et** `calaos_wago_SOURCES` — soit
  **2 lignes** de `src/bin/calaos_server/Makefile.am`, isolées dans leur propre commit `5451095a`),
  plus `tests/Makefile.am` et le fichier neuf `tests/WagoWire_test.cpp` (**31 cas**). **Aucun
  débordement.** Commit de caractérisation `b77043ad`, **zéro ligne de `src/`** — vérifié sur le
  commit : **2 fichiers**, `tests/Makefile.am` (+31/-0) et le fichier neuf (+1069/-0). **Aucune
  assertion préexistante modifiée** : sur les **6** commits, les seuls fichiers `tests/` touchés sont
  ces deux-là. Goldens intacts : arbre git `d4ebc61f…`, **145 fichiers**, vérifié identique à master
  **sur chacun des 6 commits**. Suite **77 → 78** (recompté en `python3`, continuations `\`
  comprises ; le seul ajout est `WagoWire_test`). Équilibre `tests/Makefile.am` : **67 `^if*` /
  67 `^endif`** tous préfixes confondus (**66/66** sur master — dont **66 `if HAVE_GTEST` +
  1 `if HAVE_LIBKNX`** côté branche, ne jamais compter que `HAVE_GTEST`), profondeur **jamais
  négative**. Le bloc est un **append pur, octet pour octet** : le fichier de la branche
  `startswith()` celui de master (133 432 → 135 213 octets, +1 781).

  - ⭐⭐ **LE BUG `values` EST CORRIGÉ (`e6ab8589`), ET VOICI POURQUOI C'ÉTAIT SÛR.**
    `WagoMap::write_multiple_bits/_words` construisaient la liste des valeurs puis émettaient un
    message qui ne la contenait pas. La fiche, `E4.1.md` (Q2) et ce journal recommandaient de **ne
    pas** corriger, au motif que « ça change ce que reçoit un automate réel ». **Ce motif est tombé
    à la mesure : ces deux méthodes n'ont AUCUN APPELANT dans tout l'arbre.** Confirmé par un
    balayage `python3` incluant les appels **indirects** : `&WagoMap::` donne **8 occurrences**
    (`WagoMap.cpp:45,46,55,95,167,444,467,522`), **toutes ailleurs**, aucune sur `write_multiple` ;
    **aucun `std::bind`** dans l'arbre ; aucune table de dispatch, aucun binding Lua, méthodes **non
    virtuelles**. Hors `WagoMap.h` (déclaration) et `WagoMap.cpp` (définition), les seules
    occurrences de `write_multiple_*` sont **l'autre bout** — `WagoCtrl` — appelé par
    `WagoExternProc_main.cpp:161/165/268/272` **uniquement** sur réception de
    `action:"write_bits"`/`"write_words"`, que **seul** `WagoMap` émet. ⇒ **la chaîne est morte de
    bout en bout**, `action:"write_bits"` **n'a jamais été émis**, **aucun automate n'a jamais vu ce
    message**, ⇒ **pas d'entrée `RELEASE_NOTES`** (rien d'observable par un utilisateur ne change).
    ⛔ **LA PRÉMISSE « PANNE SILENCIEUSE EN PRODUCTION » DE CE JOURNAL ÉTAIT FAUSSE** : elle a été
    **corrigée en place** à `ORCHESTRATION.md:594` et `E4.1.md:196` (§ Q2) par cette branche, et ces
    deux corrections ont été **vérifiées survivantes au rebase de merge**. Ne pas les réécrire.

  - ⭐ **`F-WAGO-2` ÉLARGI — et bien pire que « n'écrit rien ».** L'aval, `WagoCtrl`, recevait un
    vecteur **vide** avec le `count` annoncé : lire `values[0]` sur un `vector` vide **SEGFAUTE**
    (mesuré, **SIGSEGV 139**, `_M_start` nul) — donc **la mort de `calaos_wago`**, pas une panne
    muette. Et **`WagoCtrl::write_multiple_bits()` reste fonctionnellement FAUX même avec `values`
    livré** : (a) `setBit(*data, i, val)` prend une référence à **un SEUL octet** et fait
    `mot |= 0x01 << pos` ⇒ **seuls les bits 0-7 sont jamais écrits**, et `pos ≥ 32` est un **UB de
    décalage** ; (b) `new[nb/8 + nb%8]` avec `memset(.., nb/8)` laisse le **dernier octet non
    initialisé**. Aucun appelant aujourd'hui ⇒ **pas urgent**, mais **piège armé pour le premier qui
    en écrira un**. **Ticket dédié recommandé, priorité moyenne** : garde `count`/`values.size()`
    **plus** réécriture de la boucle de bits. Détail en `FINDINGS.md` **F-WAGO-2**.

  - ⭐ **`F-WAGO-4` — LA VOIE DE FERMETURE DU TROU DES SITES D'APPEL, À SA 3ᵉ RÉCIDIVE DANS LA
    SÉRIE.** Permuter `address` ↔ `nb` **au site d'appel** compile sans avertissement et laisse la
    suite verte. R3 tranché : **ce n'est PAS un problème de lien** — `CORE_TEST_LDADD` lie déjà **25
    objets serveur**, la porte est ouverte — mais l'impossibilité d'**appeler** ces méthodes sans
    construire un singleton qui **bind un socket UDP et lance `calaos_wago`**. ⇒ **la mitigation
    réelle est le TYPAGE** : `enum class` / struct nommé pour distinguer `address` de `count`, la
    permutation devenant une **erreur de compilation**, **sans une ligne de test**. **Recommandé
    pour toute la série.**

  - **9ᵉ RÉCIDIVE DU « FIXTURE PAUVRE » (`F-WAGO-6`)**, trouvée par la revue comme les huit
    précédentes, et à l'endroit le plus ironique : sur le **seul** champ où requête et réponse
    doivent différer, `FX_RCOUNT = 5` **égalait** la taille de la réponse ⇒ recalculer `count` au
    lieu de l'**écho** était invisible, **l'oracle était mort**. Corrigé (`FX_RCOUNT = 3` pour
    5 valeurs + `EXPECT_NE`), **mutation N1 rejouée : 3 rouges**.

  - **LE PRÉCÉDENT KNX NE S'APPLIQUE PAS ICI, et c'est écrit en tête du test.** **Aucun octet du bus
    modbus n'atteint jamais une chaîne JSON** : `WagoCtrl` rend des `vector<bool>` / `vector<UWord>`,
    **jamais un tampon** ; `action` est l'un de **8 littéraux**, `id` est un UUID, le reste est du
    `to_string` d'entiers. ⇒ sur ce wire les invariants d'octets (`ensure_ascii`, gestionnaire
    d'UTF-8 invalide) sont **défensifs, pas porteurs**. Le cadrage `ExternProc` a été instruit :
    **longueur-préfixée**, **purge complète au dépassement**, **pas d'épissure** ⇒ pas de message
    partiel qui se parse.

  - **Campagne de 17 mutations** (M1-M13 + N1-N4 de la revue), dont **11 dans l'en-tête de
    production** `IO/Wago/WagoWire.h`, la ligne **`CXXLD` exigée à chaque exécution**, témoin **0
    rouge avant ET après** chaque passe. Muter `"read_bits"` dans l'en-tête **livré** → **4 rouges**
    : le filet protège bien le produit, pas une copie. **Exactement 3 chaînes d'octets gelées
    bougent** sur toute la branche — la réponse de lecture (bascule) et les 2 requêtes d'écriture
    multiple (correction) ; **les 6 autres requêtes et la réponse de statut sont identiques à
    l'octet** (`Params` est un `std::map`, l'adaptateur jansson émettait déjà alphabétiquement). Les
    2 cas `..._BUG` du commit de caractérisation sont **flippés, pas supprimés**.

  - **HORS PÉRIMÈTRE, PRÉEXISTANT, À SAVOIR** : `ExternProcServer` **accepte n'importe quel
    connecteur local** et **écrase `client`** — **tous les drivers `ExternProc` sont concernés**, pas
    seulement Wago.

  - ⛔ **NON VÉRIFIÉ, à ne pas durcir** : **rien n'a tourné sous ASan**, **aucun automate réel ni
    aucun `calaos_wago` réel** n'a été sollicité, et les **permissions du socket `/tmp`** n'ont pas
    été examinées.

  - **RIEN N'A ÉTÉ POUSSÉ.**

- **🔒 E4.1i ✅ MERGÉ (`034d3915`, 5 commits, rebase sur `2955e84a` + ff-only, historique linéaire,
  `make check` **77/77**) — la bascule du wire Reolink, ET un use-after-free réel refermé dans un
  commit séparé.**
  Périmètre réel : **un seul `.cpp` de production**, `IO/Reolink/ReolinkCtrl.cpp` (appels
  `json_*`/`jansson_*` **16 → 0**, `grep jansson` = **0**), le fichier **neuf de production**
  `IO/Reolink/ReolinkWire.h` (listé dans `calaos_server_SOURCES`, inclus tel quel par le serveur
  **et** par le test), **1 ligne** de `src/bin/calaos_server/Makefile.am`, plus `tests/Makefile.am`
  et le fichier neuf `tests/ReolinkWire_test.cpp` (**17 cas**). Le bout python
  `ExternProcReolink_main.py` **n'est pas modifié**. Commit de caractérisation `2db0f7c9`, **zéro
  ligne de `src/`** — vérifié sur le commit : **2 fichiers**, `tests/Makefile.am` et le fichier neuf.
  **Aucune assertion préexistante modifiée** : sur les 5 commits, les seuls fichiers `tests/` touchés
  sont ces deux-là. Goldens intacts : arbre git `d4ebc61f…` **et** condensé SHA-256 du contenu
  recalculé au merge, identiques master/branche, **145 fichiers**. Suite **76 → 77** (recompté en
  `python3`, continuations `\` comprises ; le seul ajout est `ReolinkWire_test`). Équilibre
  `tests/Makefile.am` : **66 `^if*` / 66 `^endif`** tous préfixes confondus (**65/65** sur master —
  dont **64 `if HAVE_GTEST` + 1 `if HAVE_LIBKNX`**, ne jamais compter que `HAVE_GTEST`), profondeur
  jamais négative.

  - ⭐⭐ **LE DÉFAUT : `ReolinkCtrl::doRegisterCamera()` faisait un `json_decref()` sur un bloc déjà
    libéré** (commit séparé `43079284`). Mécanisme : `jansson_to_string()`
    (`src/lib/Jansson_Addition.h:150-165`) **vole la référence** — ses **deux** chemins de sortie
    appellent `json_decref(jroot)` — et `jroot` naissait à refcount **1** (`json_object()`), les
    `json_object_set_new()` ne volant que les références des **valeurs**. Le `json_decref(jroot)`
    qui suivait **relisait puis réécrivait `jroot->refcount` dans le bloc libéré**. **Portée** : une
    fois **par enregistrement de caméra**, **plus une par caméra à chaque (re)connexion** du process
    (`registerAllCameras()`), donc à chaque redémarrage de `calaos_reolink` — qui se relance en
    boucle.
    ⛔ **RÉSERVE À NE PAS DURCIR, accordée entre `FINDINGS.md`, `RELEASE_NOTES.md` et la fiche :
    l'UAF est DÉMONTRÉ AU SOURCE, PAS OBSERVÉ. Rien n'a tourné sous ASan, aucun bout-à-bout avec un
    vrai `calaos_reolink` ni une vraie caméra.** « Silencieux en pratique » décrit le mode *probable*
    (bloc encore dans le tcache, écriture qui n'abîme souvent que le canari), **pas une garantie** :
    dès que le bloc est repris par un objet vivant, l'écriture corrompt cet objet et le symptôme sort
    ailleurs et plus tard. Une entrée `RELEASE_NOTES.md` a été écrite (le symptôme est ce que
    l'utilisateur observe).

  - ⭐ **LE BALAYAGE EST REFERMÉ : exactement 2 sites doublent le décrément, pas de troisième.**
    `IO/Reolink/ReolinkCtrl.cpp:151` (celui-ci) et `IO/Mqtt/MqttCtrl.cpp:115` (E4.1g, déjà sur
    master). Établi par un **recompte indépendant avec analyseur de portée**, et **7 des 16 sites
    déclarés corrects ouverts et relus un par un**.
    ⚠️ **Mais les TOTAUX divergent selon les recomptes, et le désaccord n'est pas tranché** :
    `FINDINGS.md` publie **29** sites `jansson_to_string` dans `src/` sur `138c16ee`, un autre
    recompte a donné **27**, et **le recompte du merge en donne 30 jetons** — dont **1 est la
    définition** (`src/lib/Jansson_Addition.h:150`), soit **29 appels**, ce qui réconcilie avec
    `FINDINGS.md` mais **pas** avec 27. **L'accord porte sur les 2 sites fautifs et sur les 18
    arguments-variables** (18 = 2 + 16, vérifié au merge, liste identique) — **pas sur le total**.
    Ne pas recopier un total sans dire ce qu'il compte.

  - 📏 **LA CLASSE D'ERREUR QUI EXPLIQUE CES ÉCARTS — vraie pour toute la série.** Deux mentions
    **en prose** (`tests/Makefile.am:1996`, `tests/ParamsJson_test.cpp:59`) écrivent
    `jansson_from_params()` **parenthèses comprises, dans un commentaire** : tout compteur « jeton
    suivi de `(` » les compte comme des appels. **Même cause probable — NON VÉRIFIÉE — pour la
    divergence sur `jansson_to_string`** (la seule part démontrée au merge est la définition).
    ⇒ **`grep -rn` (jeton) et « sites d'appel » ne sont pas le même nombre.**

  - 📏 **Dette `jansson_from_params`** : sur `138c16ee`, **100 jetons = 96 sites + 1 définition +
    3 prose** ; sur `3f0cc074`, **94 jetons = 90 sites**. Le chiffre a valu **100 → 98 → 102 → 96 →
    90** selon ce qu'on comptait et l'état de master — **c'est l'argument, pas une anecdote : dire
    laquelle des deux valeurs on cite.** **E4.1i n'en résorbe aucune** (`ReolinkCtrl.cpp` ne
    l'utilisait pas).

  - ⭐ **LE FILET PROTÈGE LE PRODUIT** (3ᵉ ticket d'affilée où c'est vérifié, pas supposé) : les
    mutations sont faites dans `ReolinkWire.h`, **header de production** inclus par `calaos_server`,
    la ligne **`CXXLD ReolinkWire_test` est exigée aux 7 runs** (son absence invalide le résultat,
    vert comme rouge), avec `.o` du test + binaire + `ReolinkCtrl.o` + `calaos_server` effacés à
    chaque fois. **Témoin sans mutation : 0/17.** ⚠️ **Chiffre non reproduit au merge** : le mandat
    annonçait « 4 ensembles de rouges deux à deux distincts » ; la fiche en publie **6** (M1→M6 :
    3, 1, 4, 1, 7, 5 rouges), **deux à deux distincts** — je n'ai pas rejoué les mutations, je
    rapporte ce que la fiche mesure.

  - ⚠️ **LE TROU MESURÉ, à consigner tel quel** : permuter `username` ↔ `password` **au site
    d'appel** (dans `ReolinkCtrl.cpp`, hors du header) laisse la suite **VERTE 17/17**, alors que la
    **même** permutation **dans l'en-tête** rougit **5 cas** (M6). La frontière du filet est
    exactement l'entrée du header. **Trois relais de quatre `string` positionnelles**, aucun
    couvert : `ReolinkInputSwitch.cpp:83-86` (quatre `get_param()`) → `registerCamera(...)`
    (**`:92` sur master, la fiche écrit `:91`**) → `doRegisterCamera(...)`, plus le brace-init
    positionnel de `registry.add({hostname, username, password, event_type}, …)`. **Limite jugée
    acceptable** (couvrir demanderait d'instancier un singleton qui lance un processus externe) ;
    **mitigation nommée : un struct nommé**, et `ReolinkEventRegistry::CameraRegistration` **existe
    déjà** avec exactement ces quatre champs. **Ticket dédié, hors périmètre.**

  - 📏 **Trois corrections de fiche, à ne pas redécouvrir** : (1) `ReolinkCtrl.cpp` portait **16**
    appels, pas 9 (`E4.1.md` porte le même 9) ; (2) le critère d'acceptation « n'inclut plus
    `jansson.h` par aucun chemin » était **FAUX et impossible dans ce périmètre** —
    `ReolinkCtrl.cpp` → `ReolinkCtrl.h` → `IO/ExternProc.h` → `<jansson.h>` ; le critère réellement
    tenu est `grep -c jansson` **sur le `.cpp`** → **0**, et l'include mort est **renvoyé à E4.1c** ;
    (3) l'événement `detection` réel du driver porte **10 clés dont 5 non-chaînes ⇒ 5 levées**
    `type_error.302` si on substituait `Params::fromNJson()` à `jansson_decode_object()` — le
    « 9 levées sur 11 clés » d'abord publié décrivait une **charge composite de la sonde**, pas un
    message que le driver émet. Le contrat d'aujourd'hui (chaîne · booléen → mot · nombre →
    `Utils::to_string(double)` · **tout autre type → chaîne vide, clé ajoutée**) est donc réécrit à
    la main dans `decodeMessage()`, avec un **tripwire nommé** qui rougit si `fromNJson()` cessait
    de lever.

  - ℹ️ **F-REO-6 — la bascule change le MODE d'échec, et c'est assumé sans note de version** : avant,
    la paire `password` était **supprimée** et le bout python **refusait localement** (« *No username
    or password provided* »), **aucune connexion planifiée** ; désormais il reçoit un mot de passe à
    **U+FFFD**, donc **planifie réellement `connect_camera()`**, la caméra **refuse
    l'authentification**, et le `CircuitBreaker` et ses **retries** entrent en jeu. Les deux
    échouent et l'observable utilisateur est identique (la caméra ne marche pas), **mais les
    journaux et le profil réseau diffèrent**. **Pas d'entrée `RELEASE_NOTES` pour la bascule** —
    choix maintenu et argumenté.

  - 🧾 **Conflit de merge : `tests/Makefile.am` seul** (E4.1e/E4.1g/E4.1b et E4.1i appendent chacun
    en fin de fichier), résolu par **régénération** — `master:tests/Makefile.am` **intégral** +
    **append verbatim** du bloc `# E4.1i` (**27 lignes**, `if HAVE_GTEST` … `endif`) — **jamais**
    « garder les deux côtés », qui perd le `endif` extérieur du bloc précédent et fait échouer
    `automake` sur *unterminated conditionals: HAVE_GTEST_TRUE* (la revue y était tombée).
    **Append pur prouvé octet pour octet en `python3`** (`cur.startswith(master)` → `True`,
    **+1419 octets / +27 lignes**). `FINDINGS.md` et `RELEASE_NOTES.md` : **aucun conflit** — la
    branche **préfixe** ses blocs au lieu d'appender, donc git a fusionné seul ; **tous les blocs de
    master sont intacts** (titres `^## ` **59 → 60** et **5 → 6**, aucun disparu). ⚠️ **Défaut
    préexistant, non introduit ici et non corrigé** : `FINDINGS.md` porte un `---` **sans ligne vide
    avant** (master `:2821`, désormais `:2979`).

  - **RIEN N'A ÉTÉ POUSSÉ.** `master` local est à `034d3915`, en avance sur `origin/master`.

- **🔒 E4.1b ✅ MERGÉ (`8c74a380`, 4 commits, rebase + ff-only, historique linéaire, `make check` 76/76) — les
  trois invariants d'émission posés avant toute migration, et un `std::terminate` ATTEIGNABLE À DISTANCE
  par un compte authentifié ordinaire, présent dans l'artefact publié.**
  Périmètre réel : **les deux émetteurs de l'API** (`JsonApiHandlerHttp.cpp:261`, `JsonApiHandlerWS.cpp:90`,
  qui faisaient un `dump()` **nu**) + 6 autres charges serveur + les wires tiers, soit **12 fichiers `src/`**,
  `tests/Makefile.am` et le fichier neuf `tests/core/JsonApiEmissionBytes_test.cpp`. **Zéro appel jansson
  touché.** Commit de caractérisation `1ca4a4e0`, **zéro ligne de `src/`** — vérifié sur le commit :
  `tests/Makefile.am` (+55/-0) et le fichier neuf uniquement. **Aucune assertion préexistante modifiée** :
  sur les 4 commits, les seuls fichiers `tests/` touchés sont ces deux-là, et `tests/Makefile.am` est un
  **append pur octet pour octet** (128 140 o → 132 013 o, le résultat *commence par* le contenu master à
  l'octet près — conflit résolu par **régénération**, pas par « garder les deux côtés »). Goldens intacts :
  arbre git `d4ebc61f…` **et** condensé SHA-256 du contenu identiques à master, **145 fichiers**. Suite :
  **74 → 76** (la fiche annonçait 75 sur `3f0cc074` ; après rebase sur `dcbefd4a`, master était déjà à 75,
  donc **76**), recompté en python, continuations `\` comprises. Équilibre `tests/Makefile.am` :
  **65 `^if*` / 65 `^endif`** tous préfixes confondus (64 de master + 1), profondeur jamais négative.

  - ⭐⭐ **LE DÉFAUT : `std::terminate` de `calaos_server` atteignable à distance par TOUT compte API
    authentifié, en DEUX requêtes GET.** Chaîne vérifiée maillon par maillon :
    `JsonApiHandlerHttp.cpp:88` (`jsonParam = paramsGET` — repli GET qui livre des **octets
    percent-décodés**, **sans traverser aucun parseur JSON**, donc sans la validation UTF-8 que `json_loads()`
    et `Json::parse()` imposent) → `set_state` (**aucun contrôle de scope sur le dispatch HTTP, aucun
    `scopeDenied` sur `set_state`** ; les 7 refus existants sont WS-only) → `EventManager::appendEvent()`
    (`EventManager.cpp:93`, `e.io_state = ev.getParam()["state"]`) → `HistLogger` → `HistEvent::toJson()`
    (`HistLogger.cpp:82-103`, recopie sqlite → arbre nlohmann sans validation) → `buildJsonEventLog()` →
    `sendJson(const Json &)` → **`dump()` nu** → `type_error.316`, **aucun `catch`**, SIGABRT 134.
    **Sur les DEUX transports.** **Présent sur master avant ce merge, donc dans `4.4.3-dev.11`.**
    La fiche du ticket affirmait le contraire (« aucun payload client-influencé n'atteint ces deux
    surcharges ») : **c'est l'auteur qui l'a mesurée fausse**, en restaurant les deux émetteurs de master
    et en empoisonnant la base.

  - ⭐ **LA CONDITION DE PORTÉE — les deux moitiés ensemble, jamais une seule.**
    (1) **Aucun privilège requis** : tout compte API authentifié ordinaire suffit.
    (2) **Mais il faut un IO de type CHAÎNE journalisé** (`OutputString`/`InputString`) : c'est la seule
    famille dont `set_value()` accepte des octets arbitraires — sur une lumière, un volet, un variateur ou
    un scénario, `set_value(octets arbitraires)` **renvoie `false` et n'émet aucun event**, donc rien n'est
    persisté et la chaîne s'arrête à l'étape 1. ⚠️ **Ce n'est PAS `log_history` qui borne** : la revue l'a
    mesuré **ubiquitaire** sur les deux configs réelles (**78** IOs journalisés chez `raoulh`, **48** chez
    `solanora`, **tous** à `"true"`) — c'est le **type** qui borne. ⇒ **Les deux installations réelles
    vérifiées n'étaient pas exposées.** L'auteur s'était trompé de borne au premier jet (il avait écrit
    `log_history`) et l'a corrigé après la revue R1. Le durcissement reste **nécessaire** : E4.1o remet une
    clé fournie par le client directement dans l'arbre, **sans passer par aucun IO**.

  - ⭐ **LES DEUX ORACLES D'OCTETS SONT DÉLIBÉRÉMENT DISJOINTS — point de conception, à ne pas casser.**
    **A** (`InvalidUtf8InTheEventLogIsServedAsReplacementChar{OverHttp,OverWebsocket}`) est sensible au
    **gestionnaire seul** : U+FFFD se reparse à l'identique qu'il soit échappé ou brut, donc A **n'affirme
    jamais** que le fil est ASCII. **B** (`TheNlohmann{Http,Websocket}WireIsAsciiOnlyAndEscapesWithLowercaseHex`)
    est sensible à **`ensure_ascii` seul** : sa sonde `U+00E9` est **valide**, aucun gestionnaire ne la
    regarde. **Un cas par transport**, et ce n'est pas cosmétique : un cas unique couvrant les deux aurait
    donné le **même ensemble rouge** pour deux mutations différentes — signature même du piège
    `_DEPENDENCIES`. Campagne : **4 mutations par échange → 4 singletons DISTINCTS**, plus un contrôle sans
    mutation à **0 rouge**. ⚠️ Si quelqu'un fait un jour affirmer à A « le fil est ASCII », la séparation
    est détruite.

  - ⚠️ **LA REVUE A REPRODUIT LE FAUX VERT `_DEPENDENCIES` SUR CE TICKET MÊME** : un `make` nu laissait
    `CXXLD` à **0** et rapportait **tout vert sous chacune des quatre mutations** — les oracles n'avaient
    simplement jamais été relinkés. **La garde `CXXLD` est indispensable, pas décorative** : effacer le `.o`
    **et** le binaire, puis **exiger la ligne `CXXLD <binaire>` ET le code de sortie du binaire**. C'est le
    seul contrôle qui attrape les **cinq** variantes (faux ROUGE uniforme · faux VERT · faux ROUGE après
    rebase · faux VERT total avec `check_PROGRAMS` non construit · faux VERT par mort du binaire, exception
    non attrapée ⇒ **aucune ligne `FAILED`**). **À reprendre tel quel dans les 11 sous-tickets restants.**

  - **Portée mesurée du durcissement** : **30 `.dump()`** dans `src/bin`+`src/lib` (hors `json.hpp`),
    **0 sans gestionnaire** après le ticket — **8** en invariants 2+3 (`dump(-1, ' ', true, replace)`),
    **20** en gestionnaire **seul** (wires tiers déjà en service en UTF-8 brut : y ajouter `ensure_ascii`
    changerait les octets d'un wire que l'épique ne migre pas), **2** déjà conformes (`CalaosConfig.cpp:558`,
    `IO/IOFactory.cpp:117`). Le groupe « wires tiers » **n'a aucun oracle et ne peut pas en avoir** : aucun
    test n'observe leurs octets sortants, et le gestionnaire est un **no-op sur données valides** — donc
    aucune observation ne distingue l'avant de l'après. **C'est distinct du piège `_DEPENDENCIES`**
    (là, l'oracle existe mais n'est pas exercé) ; raisonnement validé par la revue.

  - ⚠️ **UN FAUX POSITIF APPARU AU REBASE, À NE PAS « CORRIGER »** : le critère d'acceptation 1 compte
    désormais **36** occurrences de `.dump(` (35 sur `3f0cc074`, +1 apportée par E4.1g), dont **une seule**
    sans `error_handler` — et c'est de la **prose dans un commentaire** : `IO/KNX/KNXCtrl.h:97`
    (« *…were put back to a naked `.dump()`…* »), posée par E4.1e. Les 5 sites neufs d'E4.1e sont **tous
    conformes**. E4.1s et E4.1x doivent **exclure les commentaires** ou reconnaître cette ligne.

  - **Build de merge rejoué en distclean complet après MON rebase** (image `vsc-calaos_base-1202…`,
    `make distclean && ./autogen.sh && ./configure && make -j32 && make check -j16`, attendu par
    **`docker wait`**) : `CXX JsonApiHandlerHttp.o`, `CXX JsonApiHandlerWS.o`,
    **`CXXLD core/JsonApiEmissionBytes_test`**, `CXXLD calaos_server` — **76/76 PASS**,
    0 FAIL / 0 ERROR / 0 SKIP, code de sortie **0**.

  - **RIEN N'A ÉTÉ POUSSÉ.**

- **🔒 E4.1g ✅ MERGÉ (`a66056fb`, 8 commits, ff-only, historique linéaire, `make check` 75/75) — le wire
  MQTT passe à `nlohmann::json`, et en le caractérisant on a trouvé **trois défauts réels**, dont un
  message entier jeté par notre propre bout.**
  Périmètre réel : `IO/Mqtt/MqttCtrl.cpp`, `IO/Mqtt/MqttExternProc_main.cpp`, le fichier **neuf**
  `IO/Mqtt/MqttWire.h`, et **2 lignes** de `src/bin/calaos_server/Makefile.am`. **Aucun débordement.**
  Appels `json_*` : **20 → 0** (`MqttCtrl.cpp`) et **15 → 0** (`MqttExternProc_main.cpp`) ; `grep jansson`
  = **0** sur les deux. Commit de caractérisation `1bdc716a` (`53ee2a2a` avant le dernier rebase),
  **zéro ligne de `src/`** — vérifié sur le commit : `tests/Makefile.am` (+24/-0) et le fichier neuf
  `tests/MqttWire_test.cpp` uniquement. **Aucune assertion préexistante modifiée** : sur les 8 commits,
  les seuls fichiers `tests/` touchés sont ces deux-là, et `tests/Makefile.am` est un **append pur octet
  pour octet** (126 956 o → 128 140 o, le résultat *commence par* le contenu master à l'octet près).
  Goldens intacts : arbre git `d4ebc61f…` **et** condensé SHA-256 du contenu identiques à master,
  **145 fichiers**. Suite : **74 → 75** (`MqttWire_test`, **37 cas** gtest), recompté en python,
  continuations `\` comprises. Équilibre `tests/Makefile.am` : **64 `^if*` / 64 `^endif`** tous préfixes
  confondus (63 `HAVE_GTEST` + 1 `HAVE_LIBKNX` d'E4.1e), profondeur jamais négative.

  - ⭐ **TROIS DÉFAUTS RÉELS TROUVÉS EN MIGRANT.**
    (1) **Un payload à octet nul était jeté par notre propre bout, topic compris.** `calaos_mqtt`
    l'émettait pourtant correctement (`json_stringn(data, len)` — tout le code amont existait pour ça),
    mais `json_loads()` **refuse cet échappement sans `JSON_ALLOW_NUL`**, et le drapeau n'était passé
    nulle part : `MqttCtrl.cpp:52-61` jetait **le message entier** avec un « Error parsing json ».
    ⭐ **Et la revue a montré que c'était plus profond** : même avec le drapeau, `json_string_value()`
    rend un `const char*` que `strlen` mesure à **1**, et l'ancien `publishTopic(…c_str())`
    **retronquait au retour**. **Les trois couches sont refermées** par la bascule ; entrée
    `RELEASE_NOTES.md` posée (changement visible utilisateur).
    (2) **Un `json_decref` de trop à chaque publication MQTT** — `jansson_to_string()` *vole* la
    référence (`json_decref` dans **les deux** branches) et `MqttCtrl.cpp:115-116` décrémentait encore :
    **use-after-free à chaque envoi**. Disparu avec la bascule.
    (3) **`IO/Mqtt/MqttExternProc_main.h` était listé dans `calaos_mqtt_SOURCES` et n'existe pas** —
    entrée morte qui aurait faussé `make dist` ; remplacée par `IO/Mqtt/MqttWire.h`, qui existe.

  - ⭐ **LE FILET PROTÈGE LE PRODUIT — contrairement à la première version d'E4.1e.** La leçon du
    test-miroir a été appliquée d'emblée : `MqttWire.h` est du **code de production**, inclus tel quel
    par `MqttCtrl.cpp`, par `MqttExternProc_main.cpp` **et** par `MqttWire_test.cpp` (vérifié : les trois
    portent le même `#include "MqttWire.h"`). Muter le code partagé **rougit** — 11 cas de mutation
    mesurés.
    ⚠️ **MAIS les 5 sites d'appel hors du header ne sont couverts par rien** : muter
    `MqttCtrl::publishTopic()` en **échangeant ses arguments** laisse la suite **32/32 verte**. Le code
    *partagé* est tenu, son *câblage* ne l'est pas, et **rien dans le dépôt ne peut le fermer** tant
    qu'aucun test ne lie les objets serveur. **Consigné, non fermable ici.**

  - ⭐ **`error_handler_t::replace` A DÉSORMAIS UN TÉMOIN**, et il épingle **la bonne des trois
    orthographes** : `replace` → `�` **par octet** · `ignore` → les octets **disparaissent** ·
    `strict` → **lève**. C'était le **deuxième des trois invariants d'émission** de l'épique livré
    **sans aucun oracle** ; il ne l'est plus. Le chemin est portant : `MqttCtrl::publishTopic()` envoie
    au `dump()` un payload **jamais assaini**.

  - ⚠️ **CINQUIÈME VARIANTE DU FAUX VERT, à porter dans le brief des sous-tickets suivants.** Sous
    `strict`, la suite rend **3 rouges au lieu d'avorter** *parce que gtest attrape le throw*. Sans ce
    filet, le binaire **mourrait**, **aucune ligne `FAILED` ne sortirait**, et un harnais qui compte les
    `FAILED` lirait **0 rouge**. ⇒ **tout harnais de mutation doit vérifier le CODE DE SORTIE du binaire
    de test**, pas seulement compter les rouges. (Les quatre autres variantes connues : faux ROUGE
    uniforme · faux VERT · faux ROUGE après rebase · **faux VERT total, contrôle compris**, quand
    `check_PROGRAMS` n'est pas construit par un `make` nu. Le **seul** contrôle valable partout reste
    **exiger la ligne `CXXLD <binaire>`**.)

  - **Nuance sur les contre-mutations, à corriger dans la règle héritée.** Deux paires de mutations
    partagent leur ensemble de rouges, et **ce n'est PAS le piège `_DEPENDENCIES`** : ce sont **deux
    orthographes d'un même défaut**, qu'un même cas attrape légitimement. La règle « rouges identiques
    = piège » ne vaut **qu'entre défauts indépendants**.

  - **L'arbitrage `?` vs U+FFFD, confirmé et non rouvert.** Le `?` est une **décision utilisateur déjà
    livrée** (entrée `RELEASE_NOTES` antérieure), et le remplacement **par octet** préserve la
    **longueur** — ce que ce wire doit avant tout préserver. Les deux chemins sont **distincts et tous
    deux épinglés** : `?` sur le payload **montant** (assainissement maison de `calaos_mqtt`),
    `error_handler_t::replace` sur le **topic venu du broker** au `dump()`.

  - ⚠️ **UNE DIVERGENCE NON RÉSOLUE, consignée honnêtement.** Deux recomptes indépendants, **tous deux
    hors du hook `rtk`**, donnent **27** et **29** sites d'appel de `jansson_to_string` dans `src/`. Ils
    s'accordent sur l'essentiel — **exactement 2 doublaient le décrément** : `ReolinkCtrl.cpp:151` (⛔
    **encore ouvert**, hors périmètre, l'agent d'E4.1i travaille dessus) et `MqttCtrl.cpp:115` (fermé
    ici) — mais **pas sur le total**. Un troisième comptage au merge (regex `\bjansson_to_string\s*\(`
    sur les blobs de `3f0cc074`) rend **26 occurrences brutes dans `src/` + 5 dans `tests/`**, dont
    **1 est la définition inline** (`src/lib/Jansson_Addition.h`). **Ne pas trancher** : le chiffre
    dépend de ce qu'on compte (déclaration, définition, commentaires) et l'écart n'a jamais été réduit.

  - **Dette `jansson_from_params` : ~90 sites d'appel** (87 `src/` + 3 `tests/`) **+ 1 définition** sur
    `3f0cc074`. ⚠️ Ce chiffre a valu **100 → 98 → 102 → 96 → 90** au fil de la nuit selon ce qu'on
    comptait et l'état de master ; le recomptage au merge donne **92 occurrences brutes** (88 `src/` +
    4 `tests/`, dont la définition et sa déclaration dans `Jansson_Addition.h`), **70 dans le seul
    `JsonApi.cpp`**. **C'est l'instabilité du chiffre qui est l'argument** : toute affirmation chiffrée
    de cette épique doit citer sa méthode et son SHA, sinon elle n'est pas comparable.

  - **Deux erreurs de méthode consignées par l'auteur, utiles aux suivants** : (1) une assertion
    comptant **15 octets pour un topic de 13** — attrapée par le contrôle sans mutation, pas par la
    relecture ; (2) **éditer le harnais pendant qu'il s'exécute** (`bash` lit son script **au fil de
    l'eau** : réécrire le fichier décale l'interpréteur, qui s'est mis à exécuter une ligne de C++ comme
    une commande shell) ; et (3) une **campagne tuée qui laisse le fichier muté dans le worktree**, ce
    qui **empoisonne la campagne suivante** (elle prend la version mutée pour référence). D'où la règle :
    **un harnais sème sa copie de référence depuis un montage EN LECTURE SEULE**, jamais depuis l'arbre
    de travail — et **on ne modifie jamais un harnais en cours d'exécution**.

  - **Build de merge rejoué en distclean complet** (image `vsc-calaos_base-1202…`, `./autogen.sh &&
    ./configure && make -j32 && make check -j16` sur un `git archive` de la branche) :
    `MQTT support (libmosquittopp)........: yes`, `CXX IO/Mqtt/MqttCtrl.o`,
    `CXX IO/Mqtt/MqttExternProc_main.o`, **`CXXLD calaos_mqtt`**, `CXXLD calaos_server`,
    `CXXLD MqttWire_test` — **75/75**.

  - **RIEN N'A ÉTÉ POUSSÉ.**

- **🔒 E4.1e ✅ MERGÉ (`72dfb068`, 7 commits, rebase + ff-only, `make check` 74/74) — le wire KNX passe à
  `nlohmann::json`, et en le caractérisant on a trouvé un plantage que du matériel ordinaire déclenche.**
  Périmètre réel : les **5 fichiers** annoncés (`IO/KNX/KNXCtrl.{h,cpp}`, `KNXExternProc_main.{h,cpp}`,
  `KNXExternProc_cli.cpp`), **aucun débordement**. `grep jansson` = **0** hors commentaires de prose sur les
  cinq ; les appels `jansson_from_params()` / `jansson_decode_object()` / `jansson_string_get()` du wire KNX
  sont **tous résorbés**. Commit de caractérisation `04652c3c`, **zéro ligne de `src/`** (vérifié commit par
  commit : seuls `cc428234` — 5 fichiers — et `b5619ac4` — 4 fichiers — touchent `src/`). Seul fichier
  `tests/` préexistant touché : `tests/Makefile.am`, en **append pur octet pour octet** (+2977 o, le fichier
  résultant *commence par* le contenu master à l'octet près). Goldens intacts (hash d'arbre git
  `d4ebc61f…`, identique à `master`, 145 fichiers). Suite : **72 → 74** (`KNXCtrlWire_test` 16 cas,
  `KNXExternProcWire_test` 18 cas), recompté en python continuations `\` comprises.

  - ⭐⭐ **LE FAIT QUI DÉPASSE CE TICKET : le plantage est atteignable depuis du matériel ordinaire, et la
    chaîne est vérifiée de bout en bout.** `KNXValue::setValue()` (`KNXExternProc_cli.cpp:311-317`),
    `case 6 / 13 / 14`, fait `value_int = data.at(1) & 0xFF` puis `value_char = value_float = value_int`.
    Or **EIS 6 est le scaling 0-255** — celui des gradateurs : **un gradateur à 78 % vaut 200**, soit
    l'octet `0xC8`, et `toJson()` sérialise `value_char` avec `Utils::to_string(unsigned char)` qui écrit
    le **caractère**, pas le nombre. **Tout octet ≥ 0x80 produit donc de l'UTF-8 invalide.** Chaîne
    complète : `EIBGetGroup_Src()` (`KNXExternProc_main.cpp:209`) → `setValue(0, buf)` (`:255`) →
    `knxEventMessage()` → `toJson()` → `dump()`. Sans `error_handler_t::replace`, `dump()` lève
    `type_error.316` — **et il n'y a aucun `catch` dans tout `IO/KNX/`** (mesuré : 0 `catch`, 0 `try` sur
    les 15 fichiers du répertoire) ⇒ **`std::terminate` de `calaos_knx`**. La mutation le reproduit
    littéralement : `C++ exception ... [json.exception.type_error.316] invalid UTF-8 byte at index 1`.
    ⇒ **La décision utilisateur UTF-8 n'était donc PAS une précaution de principe** : sur ce wire,
    `error_handler_t::replace` referme un plantage réel, déclenchable par un gradateur banal.
    ⚠️ **Une entrée `RELEASE_NOTES.md` est due et MANQUE** — voir la réserve en fin d'entrée.

  - ⭐⭐ **LA LEÇON DU TEST-MIROIR — généralisable telle quelle aux tickets `f/g/h/i/j`.** La **première**
    version du filet **ne protégeait pas le produit**. Les tests figeaient une **copie fidèle** de
    l'assemblage des messages, parce que les émetteurs réels sont inatteignables (`KNXCtrl` a un
    constructeur privé derrière un singleton qui lance deux `calaos_knx` ; `writeValue()`/`readValue()`
    finissent sur `process->sendMessage()` d'un `ExternProcServer` qui exige une boucle libuv vivante ;
    `monitorWait()` bloque dans `EIBGetGroup_Src()` sur une socket knxd). Résultat mesuré : **remettre les
    4 `dump()` de production à nu laissait la suite 34/34 VERTE** — c'est-à-dire que le filet restait vert
    en réintroduisant exactement le `std::terminate` que le ticket documente.
    - **Le remède tient en ~10 lignes** : extraire les 4 enveloppes en **fonctions libres** appelées par la
      production **et** par le test — `knxWriteMessage()`, `knxReadMessage()` (`KNXCtrl.h/.cpp`),
      `knxEventMessage()`, `knxDisconnectedMessage()` (`KNXExternProc_main.h`). `writeValue()`,
      `readValue()` et `monitorWait()` les appellent puis passent le résultat à `sendMessage()`. Aucun
      changement de comportement, aucun octet déplacé.
    - **Mesure après extraction, rejouée après le rebase sur `master` `138c16ee`** : la même mutation donne
      **2 rouges par binaire** (`NonAsciiDiffersOnlyByTheCaseOfTheHexEscape` et
      `RawNonUtf8BusBytesAreReplacedInsteadOfCrashing_DECLARED_DELTA`, dans chacun des deux binaires),
      soit **4 rouges au total**, contre **0 sur 34** avant.
    - ⇒ **RÈGLE À APPLIQUER PAR `E4.1f/g/h/i/j`** (tous des wires à processus externe, tous avec le même
      problème d'inatteignabilité) : *si le test construit lui-même le message qu'il gèle, il ne teste pas
      l'émetteur.* Un gel d'octets sur une copie est un gel de ce que **le test** fait. Extraire
      l'enveloppe en fonction libre est le prix minimal pour que le filet devienne **porteur**.

  - **8ᵉ récidive du « fixture pauvre » — et c'était un oracle MORT, trouvée par la revue** (comme les
    sept précédentes : **jamais par l'implémenteur**). Le cas `ABooleanFieldIsStringifiedTheJanssonWay`
    rangeait le booléen dans `value_int`, où `"true"` **et** `"false"` échouent **tous les deux** à
    `Utils::from_string` et laissent 0 : le cas ne pouvait **pas** les distinguer — **0/0 dans les deux
    binaires**. Réparé en déplaçant le booléen vers `value_string`, avec deux appels et deux résultats
    attendus différents : **1 rouge par binaire sur chacune des deux mutations**.

  - **Deux corrections à l'épique, à propager :**
    - **Il n'existe aucun `--with-knx`.** La fiche `E4.1e.md` en faisait une condition de compilation
      (« `KNXExternProc_cli.cpp` n'est pas compilé sans `--with-knx` »). Mesuré : **0 occurrence** de
      `with-knx`/`with_knx` dans `configure.ac`. Le support est **détecté par en-tête** :
      `configure.ac:135` `AC_CHECK_HEADERS([eibclient.h], [have_libknx="yes"])` → `AM_CONDITIONAL`
      `HAVE_LIBKNX`. Le contrôle à faire dans un build est donc la ligne de résumé
      **`Eib/KNX support (eibd ou knxd)…: yes`** (présente dans le build de merge), pas une option.
    - **`KNXValue::toJson()` est défini 2 fois, pas 3.** Sites réels : `KNXCtrl.cpp:143` et
      `KNXExternProc_cli.cpp:493`. Le « 3ᵉ » site cité par la fiche était un **appel**, pas une
      définition — et `E4.1.md:63` se contredisait **déjà** (il annonce « défini trois fois » tout en ne
      listant que deux sites). La classe reste **déclarée 2 fois** (`KNXCtrl.h:64`,
      `KNXExternProc_main.h:57`), ce qui est exact, et **n'a pas été dédupliquée** (hors périmètre).

  - ⚠️ **Le piège `_DEPENDENCIES` reste ARMÉ pour le prochain** : les deux nouveaux tests reconduisent le
    motif `*_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la` **seul**, alors qu'ils lient des
    `.o` du serveur. `make` n'a donc **aucune raison de relier** le binaire de test quand un `.o` du
    serveur change. La 1ʳᵉ campagne de contre-mutation de ce ticket a rendu **0 rouge** pour cette seule
    raison (variante « **faux VERT** »).
    - **Le seul contrôle valable, dans les quatre variantes connues** (faux ROUGE uniforme, faux VERT,
      faux ROUGE après rebase, **faux VERT total où `check_PROGRAMS` n'est même pas construit par un
      `make` nu**) : **exiger la ligne `CXXLD <binaire de test>`** dans la sortie de make. Son absence
      **invalide le résultat, vert comme rouge**. Procédure appliquée ici : effacer le `.o` du serveur
      **et** le binaire de test, puis construire les cibles **nommément**
      (`make KNXCtrlWire_test KNXExternProcWire_test`) et **lire les deux `CXXLD`** avant de croire le
      résultat.

  - **⚠️ RÉSERVE OUVERTE — l'entrée `RELEASE_NOTES.md` MANQUE.** `docs/refactoring/RELEASE_NOTES.md`
    existe sur `master` (403 lignes, 21 sections) et **ne contient aucune mention de KNX** ; **aucun des
    7 commits de la branche ne le touche**. Or ce ticket referme un **plantage observable par un
    utilisateur** (`calaos_knx` qui meurt dès qu'un gradateur EIS 6 passe au-dessus de 50 %), ce qui est
    exactement le critère d'entrée du fichier. **Non écrite ici délibérément** (le merge ne rédige pas la
    prose utilisateur à la place de l'auteur). **À rédiger pour un utilisateur**, dans la section
    « Comportements qui changent » ou « Fiabilité », en décrivant le symptôme observable, pas la
    bibliothèque JSON.

  - **Non couvert, tel quel — à ne pas croire acquis** : **pas d'ASan** sur les deux nouveaux binaires ;
    **aucune config réelle avec des IO KNX** n'a été exercée (le filet est du wire pur, hors `KNXIo`) ;
    et le comportement **jansson « avant »** n'a pas été mesuré sur le binaire d'origine mais par **sonde
    équivalente** (reconstruction du comportement de `json_dumps`/`json_string`), ce que les cas
    `_DECLARED_DELTA` documentent explicitement.

  - **Conflits du rebase, et leur forme** : **`tests/Makefile.am`** (les deux côtés ajoutent en fin de
    fichier) et **`docs/refactoring/FINDINGS.md`** (idem). Résolus par **régénération**, pas par édition
    de marqueurs — `git show master:<f>` en entier + append verbatim du bloc de la branche. Preuves :
    `tests/Makefile.am` **commence par le contenu master octet pour octet** et `^if` == `^endif` (63/63) ;
    `FINDINGS.md` **commence par le contenu master octet pour octet**, **51 → 57** titres `^## `
    (les 51 de master tous présents, 6 ajoutés), **ligne vide devant chaque `---`**.

  - **Rien n'a été poussé.** `master` local est à `72dfb068`, en avance sur `origin/master`.

- **🔒 E4.1k ✅ MERGÉ (`511a6103`, 5 commits, rebase + ff-only, `make check` 72/72) — le générateur
  `io_doc.json` passe à `nlohmann::json`, et la revue y a trouvé bien plus gros que le ticket.**
  Périmètre réel : `IO/IODoc.h`, `IO/IODoc.cpp`, `IO/IOFactory.cpp` **+ le helper `docParamNames()` de
  `tests/core/WebIO_test.cpp`** — appelant que la fiche n'annonçait pas et **sans lequel la bascule de
  signature ne compile pas** ; adapté dans un commit séparé, **0 assertion préexistante modifiée**.
  `jansson_from_params()` : **104 → 100** (compté sur les fichiers suivis de `src/` **et** `tests/` ;
  `src/` seul : 99 → 95), et `grep -c jansson` = **0** sur les trois fichiers migrés. Commit de
  caractérisation `8d7cbe1f`, **zéro ligne de `src/`**. Goldens intacts (hash d'arbre git
  `d4ebc61f…`, identique à `master`).
  - ⭐⭐ **LE FAIT QUI DÉPASSE CE TICKET : toute la suite était AVEUGLE au drapeau `ensure_ascii`.**
    La revue a muté `dump(4, ' ', true, …)` → `false` : `make check` **reste VERT**. Or
    `ensure_ascii = true` est l'**invariant que les 17 sous-tickets d'E4.1 appliquent** — il était donc
    posé partout **sans le moindre oracle**, parce que les goldens et les cas comparent des **documents
    parsés**, où `é` et l'octet UTF-8 brut se parsent en la **même** chaîne.
    - Le filet manquant tenait en **12 lignes**, et il n'avait **jamais été cherché** : la fiche
      affirmait qu'aucun cas synthétique n'était possible (« aucun IO de l'arbre ne porte de non-ASCII
      dans sa documentation »), **c'était faux**. `IOFactory::RegisterClass()` est **publique** et la
      fabrique publie le nom de type **d'origine** comme clé de premier niveau ⇒ enregistrer un type
      `"Acc\xc3\xa9ntedType"` **déléguant à `CreateIO("inputtimer")`** met du non-ASCII dans le
      fichier **et nulle part ailleurs**, sans nouvelle classe d'IO, sans nouveau fichier, sans toucher
      `src/`. Puis on asserte sur les **octets**.
    - Livré comme **16ᵉ cas**, `TheJsonFileStaysPureAsciiWhenATypeNameIsNot` : la mutation
      `ensure_ascii = false` est désormais **rouge sur ce cas SEUL** — ce qui **mesure exactement
      l'étendue de l'angle mort**. ⇒ **À réutiliser par tout sous-ticket d'E4.1 qui pose
      `ensure_ascii`** : sans un oracle d'octets, le drapeau n'est pas testé.
    - ⚠️ Le registre d'`IOFactory` est un **singleton de processus** et l'enregistrement est
      **définitif** ; il est purement **additif**, la **première** inscription gagne ⇒ le cas est
      idempotent sous `--gtest_shuffle`.
  - **⛔ L'avertissement que ce ticket avait posé pour E4.1c était FAUX — corrigé ET gardé visible**
    (bloc « ⛔ CORRECTION » dans `FINDINGS.md`, pas une réécriture silencieuse : l'erreur aurait fait
    renoncer E4.1c à un nettoyage sûr). Le fait exact : `IO/ExternProc.h:26` `#include <jansson.h>` est
    **totalement redondant** avec la **ligne 27** (`#include "Jansson_Addition.h"` → `<jansson.h>`).
    Ligne 26 supprimée, build **OK, zéro `error:`** ⇒ **E4.1c tel qu'écrit ne casse rien.** Trois autres
    chiffres corrigés : **9 fichiers / 10 objets**, pas « ~20 » ; **5 fichiers étaient mal attribués**
    (`MqttCtrl.cpp`, `ReolinkCtrl.cpp`, `IO/Scenario.cpp`, `EventManager.cpp`, `ScriptExec.cpp` —
    ils reçoivent jansson par une autre chaîne) ; et la vraie condition de casse est le retrait de
    **`Jansson_Addition.h` (ligne 27), donc `E4.1x`**, jamais celui de la ligne 26.
    - ⚠️ **Réserve à reporter telle quelle** : mesure faite sur un `./configure` **nu**, donc
      `OWCtrl.cpp` et `OWExternProc_main.cpp` **n'ont pas été compilés**. La conclusion vaut pour le
      **build par défaut**, **pas** pour `--with-owfs`.
  - **`calaos_installer` est indifférent** au changement d'ordre du premier niveau (Q3) : établi **au
    source**, il parse en `QJsonObject` — **déjà trié par Qt** —, réindexe en minuscules et ne fait que
    des **lookups par clé** ; les deux consommateurs (`FormActionStd.cpp:116`,
    `WidgetIOProperties.cpp:56`) lisent les **tableaux**, dont l'ordre est **inchangé sur les 70 types**.
    ⇒ **seul effet : un gros diff de permutation** à la prochaine régénération de
    `data/doc/{en,fr}/io_doc.json`.
  - **Preuve avant/après, mesurée** : `io_doc.md` **identique octet pour octet**, `io_doc.json` de
    **même taille**, `json.load(avant) == json.load(après)` sur les **70 types**, **0 feuille
    non-chaîne**, ordre des tableaux inchangé. À noter : `json_dumps()` était appelé **sans**
    `JSON_ENSURE_ASCII` ici (forme **2** du tripwire, UTF-8 brut), et l'artefact réel ne contient
    **aucun** octet ≥ 0x80 ⇒ `ensure_ascii` est un **no-op strict** sur le fichier d'aujourd'hui.
  - ⚠️ **Piège `_DEPENDENCIES`, variante FAUX ROUGE UNIFORME** (nouvelle, consignée en `FINDINGS.md`) :
    7 mutations sur 7 « RED » **avec exactement le même cas en échec**, celui de la **première** — les
    objets serveur sont retirés des prérequis, donc le binaire de test **n'est pas relié** et on exécute
    la mutation **précédente**. Le symptôme qui trahit est **plusieurs mutations rendant le même cas** ;
    la parade est d'**effacer le binaire de test ET les `.o` mutés** avant chaque reconstruction.
  - **Conflit de merge** : `tests/Makefile.am` seul (les deux côtés appendent en queue), résolu par
    **régénération** — `master:tests/Makefile.am` **entier** + **append verbatim** des 23 lignes de la
    branche ; append pur **prouvé octet pour octet**, `^if HAVE_GTEST` == `^endif` (**60 == 60**),
    **72 entrées `TESTS`** sans doublon. `FINDINGS.md` s'est auto-mergé en **append pur** (50 → 51
    titres `## `, aucun `---` sans ligne vide avant).
  - **Rien n'a été poussé** : `master` local seulement.

- **🔒 T3.24 ✅ MERGÉ (`dd0e7900`, 4 commits, ff-only, `make check` 71/71) — le throttle de login identifie enfin le
  client derrière haproxy.** `clientIp()` rendait le **pair TCP** sur **LES DEUX** transports
  (`JsonApiHandlerWS.cpp:45` **et** `JsonApiHandlerHttp.cpp:55` — le constat initial ne citait que
  WS) ⇒ **un seul seau `LoginThrottle` pour toute l'installation** : un attaquant verrouillait le
  login de tous les utilisateurs, et sa propre limite était effacée par le premier login réussi de
  n'importe qui. Les deux passent désormais par `HttpClient::getEffectiveClientIp()`, qui enveloppe
  `TransportLimits::effectiveClientIp()` — **le helper existait déjà** et était **déjà** utilisé dix
  lignes plus loin par `max_connections_per_ip` (`HttpClient.cpp:200`).
  - ⭐ **CE MERGE DÉBLOQUE `E4.1b`** et la chaîne sérialisée `E4.1l`→`E4.1s`, qui **possèdent**
    `JsonApiHandler{WS,Http}.cpp`. Elles travaillent sur `sendJson` (**WS:75**, **Http:253**),
    **sans recouvrement de lignes** avec `clientIp()` : les hunks de T3.24 sont **minuscules**
    (les trois lignes de `clientIp()` dans chaque fichier, plus une méthode inline dans
    `HttpClient.h`) et **hors des zones jansson** que la chaîne réécrit. Merger d'abord était
    quand même le bon ordre : cela a **évité 12 rebases** aux sous-tickets de la chaîne.
  - ⚠️⚠️ **RÉSERVE ASSUMÉE, consignée en F-XFF-1 et NON corrigée — et T3.24 la CRÉE, il ne
    l'hérite pas.** Une première rédaction affirmait l'inverse, **c'était faux, corrigé en revue** :
    avant T3.24 les deux `clientIp()` rendaient le **pair TCP**, donc `X-Forwarded-For` n'avait
    **aucun effet** sur `LoginThrottle`, dans **aucun** déploiement. `calaos_server` ne vérifie
    **jamais** que son pair est haproxy, et **`HttpServer.cpp:29-31` bind `0.0.0.0` par défaut**
    alors qu'haproxy ne vise que `127.0.0.1:5454` ⇒ **le port 5454 répond en direct depuis le LAN
    sur le déploiement standard**. En direct, l'attaquant gagne **deux capacités neuves** :
    s'exonérer du backoff (brute-force **sans limite**) et **throttler une victime ciblée**.
    **L'échange reste acceptable** (il retire un DoS de lockout non authentifié atteignable depuis
    le WAN et frappant tout le monde ; il ajoute un abus qui exige le LAN ; et le cap de connexions
    fait déjà confiance à l'en-tête) — **mais c'est un arbitrage, pas un gain gratuit**.
    - ⭐ **SUITE À OUVRIR, HORS DE CE DÉPÔT (calaos-os), et elle est gratuite** : poser
      **`listen_address = 127.0.0.1`**. L'option **existe déjà** et est documentée
      (`docs/16_config_options.md`) ; seul haproxy joindrait alors le port, ce qui rend la confiance
      en `X-Forwarded-For` **saine**. C'est le vrai correctif de fond de F-XFF-1.
    - Config de production **vérifiée, pas supposée** : `pkgbuilds/calaos-os-conf/PKGBUILD` épingle
      `33f794eb`, dont `conf/haproxy-calaos.cfg` porte **`option forwardfor` SANS `if-none`** ⇒
      haproxy ajoute **toujours** sa ligne, en queue. **Derrière le proxy, non contournable.**
  - **Nouveau binaire de test** `core/JsonApiThrottleIdentity_test` (**7 cas**, 2 transports,
    **0 golden**) **+ 3 cas** dans `TransportHardening_test.cpp` qui figent, **au vrai parseur
    llhttp**, que « **la dernière ligne `X-Forwarded-For` répétée gagne** » — l'invariant dont
    dépend tout l'argument de sécurité, et que le fixture (qui injecte `request_headers` à la main)
    ne pouvait pas prouver. Le harnais E4.0a est **réutilisé sans être modifié**. Commit de
    caractérisation `56dceb11`, **zéro ligne de `src/`**.
  - **La preuve est faite au VRAI parseur, pas au fixture** : en-tête `X-Forwarded-For` **répété**
    ⇒ **la dernière ligne parsée gagne**, parce que le callback **écrase**
    (`request_headers[lower(field)] = value`, `HttpClient.h:265`). Muté en `emplace`, la suite
    rougit **exactement** `LastRepeatedHeaderLineWins` et `ClientSuppliedListIsDiscardedWholesale`
    — **les 23 autres restent vertes**. C'est ce qui rend l'argument « derrière haproxy, non
    contournable » démontré plutôt qu'affirmé.
  - **Leçon de contre-mutation, à réutiliser** : échanger les *valeurs* de `kClientA`/`kClientB`
    donne **0 rouge** — le fichier est **symétrique**, la mutation est donc sans effet. Ce qui mord
    est l'échange des **identités entre les deux sessions d'un même cas**. De même, le cas du
    préfixe forgé à **2 entrées** ne figeait **pas** `rfind` (il passait aussi avec `find`) : il a
    fallu passer à **3 entrées** pour que la mutation rougisse.
  - **Non vérifié, noté tel quel** : haproxy 2.8 en **HTTP/2** frontend (déduit, non testé) ; le
    **request smuggling** à travers haproxy ; et la suite **non rejouée sous ASan**.
  - **Rien n'a été poussé** : `master` local seulement.

- **⭐ E4.1 DÉCOUPÉE (2026-08-24) — 17 sous-tickets `a`→`x`, 10 vagues, fiches écrites.**
  **Aucune ligne de `src/`, aucun test, aucun golden touché** par ce travail de conception.
  Lire **[`E4.1.md`](E4.1.md)** (empreinte remesurée, découpage, vagues, verdict wires,
  **5 questions ouvertes**) puis la fiche du sous-ticket qu'on lance.
  - **➡️ PROCHAINE ACTION CONCRÈTE** : **finir de merger E4.1a** (branche `refactor/e4.1a`,
    `a2a3150c`, revue en cours dans `.review27/e4.1a`), **puis lancer la VAGUE 1 : les 10 tickets
    `E4.1b`…`E4.1k` en parallèle**, périmètres de fichiers disjoints, un agent chacun.
    **Commencer par `E4.1b`** si un seul agent est disponible : c'est le prérequis dur d'`E4.1o`.
    En parallèle et hors E4.1 : **T3.21** (une ligne) et **E4.6b/E4.6c** (non bloqués).
  - ⚠️ **UN FAIT DU BRIEF INITIAL ÉTAIT FAUX, corrigé ici** : « les émetteurs d'API sont couverts
    par les 145 goldens, la bascule sera visible et arbitrable ». **NON.** Les goldens comparent
    des **documents parsés** (oracle sémantique E4.0a) : **aucun ne rougira sur un changement
    d'échappement**. La zone nue sur cette dimension, **c'est toute la migration**. Le **tripwire**
    d'E4.1a est le **seul** garde-fou, et chaque fiche distingue désormais **structure/valeurs**
    (couvert) de **forme d'octets** (nu).
  - ⚠️ **Second fait corrigé, mesuré par E4.1a** : les caractères de contrôle **divergent aussi**
    entre les deux bibliothèques. `U+001F` → `\u001F` (jansson) contre `\u001f` (nlohmann) ; la
    divergence apparaît dès que l'hexadécimal contient une **lettre**. `U+0001` ne diverge pas.
    Ce n'est donc **pas** « seulement le non-ASCII ».
  - ⭐⭐ **LE RÉSULTAT LE PLUS ATTENDU — les wires drivers, établis au source, fichier par fichier :
    AUCUN n'est exposé à un tiers en écriture.** Sur les 8 : **6 internes aux deux bouts**
    (Wago `WagoMap` ↔ `calaos_wago` — `WagoCtrl.cpp` ne contient **aucun** JSON ; OLA ; MQTT — le
    broker ne voit que `payload`, en passe-plat d'octets ; KNX, sur **trois** bouts ; Reolink, dont
    l'autre bout est `ExternProcReolink_main.py`, **du Python de ce dépôt** ; Lua) et **2 en lecture
    seule depuis un tiers** (Squeezebox — son unique `json_dumps` va dans `cDebug()` ; Hue — aucun
    `json_dumps`). Chaque extrémité décode avec un **vrai parseur JSON**, jamais par recherche de
    sous-chaîne. ⇒ **le risque « un parseur maison en aval » n'existe sur aucun wire driver.**
    Il ne subsiste que sur l'**API publique**, où il est déclaré en `RELEASE_NOTES` par `E4.1s`.
  - ✅ **DÉCISION UTILISATEUR (2026-08-24) : `ensure_ascii = true` partout** —
    `dump(-1, ' ', true, Json::error_handler_t::replace)`. **Delta minimal** : le wire reste ASCII
    pur, seule la **casse de l'hexadécimal** change (`\u00E9` → `\u00e9`). Octets bruts **écartés**.
    ⛔ **Le tripwire doit basculer vers la FORME 3, pas la 2** — le faire rougir dans la mauvaise
    direction ressemble à une réussite. Entrée datée en tête de `DECISIONS.md`.
  - ⭐ **Fait qui a structuré le découpage** : `nlohmann` **émet déjà sur l'API aujourd'hui**
    (`JsonApiHandlerHttp.cpp:253`, `JsonApiHandlerWS.cpp:75`), **à nu, sans gestionnaire d'erreur**.
    D'où **`E4.1b` en tout premier** : il pose les trois invariants d'émission sur les `dump()`
    existants. **`E4.1o` est le ticket où le `std::terminate` d'E4.0 (`?param=%ff%80x`) devient
    atteignable** — si `E4.1b` n'est pas mergé, on ne démarre pas `E4.1o`.
  - **Le seam qui rend le découpage possible, il existait déjà** : les deux transports ont
    **déjà** une surcharge `sendJson(const Json &)`. Migrer un constructeur = changer son type de
    retour et basculer ses appelants dessus. **Aucun adaptateur transitoire n'est nécessaire dans
    ce sens** — deux seulement dans la série, chacun à **un seul appelant** (`E4.1l` → retiré par
    `E4.1m` ; `E4.1r` → retiré par `E4.6d`).
  - ⛔ **RÈGLE ANTI-CONFLIT, la plus longue chaîne sérialisée du projet** : `E4.1b`, puis `E4.1l`
    → `m` → `n` → `o` → `p` → `q` → `r` → `s` touchent tous `JsonApi.h/.cpp` +
    `JsonApiHandler{Http,WS}.cpp`. **Neuf tickets, neuf vagues, jamais deux dans la même.**
    La vague 1 (`b`…`k`) est en revanche **entièrement parallèle**.
  - ⛔ **`E4.1x` (retrait de `jansson` de `configure.ac:52`) EST BLOQUÉ PAR `E4.6b`+`E4.6d`** —
    conséquence directe de l'exclusion de `IO/Scenario.cpp` (34 appels, Q5). **E4.1 reste 🚧 après
    le merge d'`E4.1s`** : afficher « 16/17 livrés, clôture en attente d'E4.6 », jamais ✅, pour
    qu'un lecteur pressé ne croie pas la double bibliothèque partie.
  - **Coût des 3 `toJson()` membres restants, évalué** : `KNXValue::toJson()` **faible** (9 lignes,
    mais **déclaré 2×, défini 3×** — les trois copies dans le même commit, `E4.1e`) ;
    `CalaosEvent::toJson()` **faible dans la fonction, moyen dans ses 4 appelants** (`E4.1l`) ;
    `Scenario::toJson()` **sans objet pour E4.1** — réécrit par `E4.6d`.
  - **Empreinte remesurée sur `refactor/e4.1a`, hors artefacts de build** : **476 appels dans
    `src/`** + **152 dans `tests/`**, `jansson_from_params` à **104 occurrences**. Les chiffres
    antérieurs (« 36 fichiers, 603 appels », `E4.6.md:105-114`) venaient d'un `grep -rl` qui
    incluait des `.o` et des `.Po` ; **classement et ordre de grandeur identiques**, écart signalé
    dans `E4.1.md`.
  - **Deux découvertes hors périmètre, tranchées dans les fiches** : (1) le **bug fonctionnel Wago**
    — `WagoMap::write_multiple_bits/_words` (`:328-337`, `:402-411`) construisent `values` puis
    émettent `p` à la place, donc **le tableau ne part jamais** (et la référence fuit).
    ⛔ **CETTE LIGNE DISAIT « l'écriture multiple n'écrit rien, en silence, depuis toujours » : LES
    DEUX MOITIÉS SONT FAUSSES**, corrigé par E4.1h (2026-08-25) et confirmé en revue par un
    balayage `python3` incluant les appels indirects. (a) **Le chemin est MORT** : les deux
    méthodes n'ont **aucun appelant** — `&WagoMap::` donne 8 occurrences, toutes dans
    `WagoMap.cpp`, aucune sur `write_multiple` ; aucun `std::bind`, aucune table de dispatch,
    aucun binding Lua, méthodes non virtuelles. `action:"write_bits"` n'a **jamais** été émis,
    donc **aucun automate n'a jamais vu ce message** et il n'y a **pas de panne en production**.
    (b) **Et ce ne serait pas silencieux** : `calaos_wago` passait à `WagoCtrl` un vecteur **vide**
    avec le `count` annoncé, et `values[0]` sur un vecteur vide **SEGFAUTE** (mesuré, SIGSEGV 139).
    ⇒ le motif de non-correction (« ça change ce que reçoit un automate réel ») **tombe** :
    `E4.1h` l'a donc **CORRIGÉ**, dans un commit séparé, **sans entrée `RELEASE_NOTES`** (rien
    d'observable ne change). Reste ouvert et **hors périmètre** : `WagoCtrl::write_multiple_bits()`
    est faux **même avec `values` livré** (`setBit()` n'écrit que le premier octet, `memset` plus
    court que l'allocation) → **ticket dédié recommandé, priorité moyenne**, détail en
    `FINDINGS.md` **F-WAGO-2**. (2) Les **fuites de `json_t`**
    (mêmes sites + `IODoc.cpp:163`) disparaissent **d'elles-mêmes** avec nlohmann : rattachées aux
    tickets de migration, aucun ticket séparé.
  - **5 questions ouvertes** en fin de `E4.1.md`, chacune avec sa recommandation : Q1 migrer les
    autoscénarios deux fois ou non · Q2 corriger le bug Wago ou non · Q3 l'ordre des clés dans
    `io_doc.json` · Q4 quand déclarer l'épique close · Q5 quand renommer `toNJson`→`toJson`.
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
  ne sont **pas opposables** (rien n'appelle Squeezebox/RoonPlayer/MqttCtrl dans la suite, ~~les
  objets ne sont même pas liés~~
  ⛔ **CORRECTION (2026-08-25, T3.27) : FAUX pour `RoonPlayer`.** Mesuré en `python3`
  (`_SOURCES`/`_LDADD`/`_DEPENDENCIES` aplatis) — `Audio/RoonPlayer.$(OBJEXT)` est lié par **8**
  cibles sur `d68e59f1`, **le commit E4.0d lui-même**, dont `core/JsonApiEvents_test` ; **17** sur
  master `fb9d064c`. Vraie pour `Squeezebox` (**0** partout) ; vraie **à la date** pour `MqttCtrl`
  (**0** sur `d68e59f1`) mais **périmée depuis** (**1**, `JsonPathSyntax_test`). ⭐ **La conclusion
  de la puce tient** — rien n'*appelle* ces classes dans la suite — mais **la raison invoquée était
  fausse**, et la nuance est opérationnelle : « non lié » se lit *irréductible*, « lié mais jamais
  exécuté » se lit *un cas à écrire*. Voir `FINDINGS.md` **F-LINK-1**) ;
  **4 payloads audio ont été corrigés** parce qu'ils gelaient des
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
    d'amputation est **hors API** — `calaos_installer` jette les entrées/sorties à id non résolu au
    chargement de `rules.xml` (**4 sites** : `projectmanager.cpp:1023`, `:1054`, `:1082`, `:1125`)
    puis **régénère et téléverse `io.xml`/`rules.xml` entiers** (`:922-933`,
    `dialogsaveonline.cpp:100-122`).
  - ✅ **LES 5 QUESTIONS SONT TRANCHÉES (2026-08-24)**, détail en `E4.6.md` §10 et entrées datées en
    tête de `DECISIONS.md`. **Q1** refus `modify` (T3.20/R3) → **abandonné**, il compensait une perte
    d'information que la refonte supprime. **Q2** payload → **tout en chaînes** (l'oracle des tests
    est type-strict, `3 != "3"`). **Q3** → **`final_step` en champ séparé**. **Q5** →
    `IO/Scenario.cpp` **exclu d'E4.1**, migré directement par E4.6 en nlohmann (note posée dans
    `E4.1.md`).
  - ⭐ **Q4 — l'utilisateur a choisi `io.xml`, contre la recommandation de l'agent ET de
    l'orchestrateur**, au nom de l'**invariant « deux fichiers »** (`io.xml`/`rules.xml` sont ce que
    tout l'outillage, les backups et l'installeur connaissent ; un 3ᵉ fichier n'aurait pas supprimé
    le risque, il l'aurait déplacé vers le premier outil qui l'ignore). Confirmé par le code :
    `JsonApiHandlerHttp.cpp:631-633` n'accepte au téléversement que ces 3 noms de fichiers en dur.
  - ⭐⭐ **ET LA CONTRAINTE SE RETOURNE EN GARANTIE — c'est le résultat le plus important de la
    reconception.** Mesuré au source de `calaos_installer` : les **params** d'un IO qu'il ne connaît
    pas sont **préservés intégralement** (lecture générique sans liste blanche
    `projectmanager.cpp:611-618` ; réécriture de tous les params `:197-204` ; preuve empirique :
    `cycle=` et **78** `log_history=` survivent dans `configs/raoulh/io.xml`), alors que les **nœuds
    XML enfants** sont **perdus** (`:653-658`, et `writeInput()` n'en réémet aucun hors cas spécial
    RemoteUI). ⇒ la définition est portée par des **params d'IO**, donc préservée **même par un
    installeur ANCIEN** — ceux qui resteront en circulation et qu'aucune mise à jour ne rattrapera.
    **La correction de `calaos_installer` cesse d'être une dépendance dure** : ticket **I4.1**,
    recommandé, non bloquant, pour un défaut qui concerne **toutes** les règles.
  - **Défense en profondeur côté serveur (D10)** : niveau 0 = **auto-réparation** (les règles
    générées sont régénérées depuis la définition, donc une amputation de l'installeur est écrasée
    au démarrage suivant) ; niveau 1 = **sauvegarde avant écrasement DÉJÀ EN PLACE**
    (`JsonApiHandlerHttp.cpp:624` → `Config::BackupFiles()`, `CalaosConfig.cpp:614`), à **prouver et
    documenter** ; niveau 2 = **détecter et alerter** (E4.6h) ; niveau 3 = **refus écarté** (même
    raison que Q1 : on ne refuse pas sur l'état d'avant).
  - ⛔ **Séquencement dur** : E4.6 vient **après E4.1** (décision du même jour : plus aucun code neuf
    en jansson ; les fichiers rouverts portent **41 %** du jansson du dépôt). **Arbitrage soumis
    (Q5)** : `IO/Scenario.cpp` — exclu du périmètre d'E4.1 et migré directement par E4.6, ou migré
    deux fois ?
  - **T3.20 ⛔ parké** : R3/R5 absorbés par la refonte. **Exception extraite : T3.21** — le
    durcissement de `buildJsonDelParam` (`JsonApi.cpp:724`, court-circuite `IOBase::del_param()`)
    est **indépendant des scénarios**, une ligne, aucun appelant cassé. À livrer seul.
  - **➡️ PROCHAINE ACTION CONCRÈTE** : plus rien à faire trancher. **Lancer E4.1** (elle bloque
    E4.6d/e/f/g) **en excluant `IO/Scenario.cpp` de son périmètre** (Q5, note déjà posée dans
    `E4.1.md`). **En parallèle, immédiatement** : **T3.21** (une ligne, indépendante, non bloquée
    par E4.1) et **E4.6a** (caractérisation, tests seuls, zéro `src/` — non bloquée non plus).
    **I4.1** peut partir quand on veut, sur le dépôt `calaos_installer`, sans coordination.
  - ⚠️ **Recalage de sites obligatoire** : la revue de T3.20 cite `IO/Scenario.cpp:206` et `:229`
    pour les gardes `if (!sa.io) continue;`. Sur **master `770e322f`** le fichier fait **195 lignes**
    et les sites sont **`:158`** et **`:181`** — l'écart vient du worktree `.wave26/t3.20`. Tout
    sous-ticket reprenant un site de cette revue doit le **recaler sur master**.
- **E4.6a ✅ mergé** (2026-08-24, `50741d6b`, ff-only, historique linéaire) — caractérisation
  pure du modèle AutoScenario **avant** la refonte : `tests/core/AutoScenarioMigration_test.cpp`
  (1922 lignes, **19 cas**) + bloc `HAVE_GTEST` propre en fin de `tests/Makefile.am` + `E4.6.md`
  et `FINDINGS.md`. **Zéro ligne de `src/` sur chacun des 3 commits** (vérifié commit par commit,
  pas seulement sur l'arbre final) ; **145 goldens intacts, hash d'arbre identique**
  (`tests/core/golden` = `d4ebc61f` sur master comme sur la branche — aucun ajouté, retiré ni
  modifié). Aucun conflit au rebase (la branche était déjà sur `aa4821f7`) ; `tests/Makefile.am`
  vérifié **append pur** (+55/−0/~0, les 1933 premières lignes byte-identiques à master) et
  équilibre `^if HAVE_GTEST` == `^endif` (57/57). Build docker complet : **69/69**
  (`TESTS` = entrées, pas lignes — 68 avant, +1).
  - ⭐ **Le trou trouvé par la revue et comblé.** Le test initial mutait
    `disabled_missing_io ← !missing_ios.empty()` : cette mutation **détruit la distinction même
    pour laquelle T3.18 existe** (un scénario `disabled` par l'utilisateur, un scénario
    `disabled_missing_io` par le système, un `broken`, un sain — quatre états, pas deux) et
    laissait la suite **0/18 vert** — verte sans rien attester. Le cas neuf la porte à **1/19**,
    et sa jumelle sur `broken` **mord avec un témoin distinct** : les deux ne se remplacent pas,
    il faut les deux.
  - ⚠️ **Avertissement structurel à porter jusqu'à E4.6d.** Les **seuls** témoins de cette
    distinction à quatre états vivent dans les fichiers que **E4.6d doit réécrire** : le
    sous-ticket qui a le plus besoin de la protection est exactement celui qui la démolit.
    À relire avant d'ouvrir E4.6d.
  - ⚠️ **14 pompages → 1.** Les 13 `pumpEventLoop()` retirés **n'absorbaient rien** (mesuré sur
    **5 graines**). Récidive de la règle « ne pas recopier un pompage non mesuré » — et elle
    récidive **dans le ticket qui énonce cette règle**. Le contrat de file vide (E4.0g) rend le
    pompage décoratif la norme, pas l'exception.
  - ✅ **Deux corrections de cartographie confirmées au source**, toutes deux contre RC1 :
    (1) `checkScenarioRules()` **ne recrée PAS** les règles d'étape — elles sont perdues
    **définitivement** ; la **seule** création est `AutoScenario.cpp:859`, **API-only**. Le défaut
    du balayage orphelin est donc **pire** que ce que décrivait RC1 (pas d'auto-réparation au
    démarrage : le niveau 0 de la défense en profondeur D10 n'existe qu'**après** E4.6c).
    (2) La renumérotation ne touche que **3 des 4** numérotations : le param
    **`auto_scenario_step` n'est jamais réécrit**.
  - ⭐ **Un point où la revue a eu tort et l'implémenteur a mesuré** — consigné tel quel, parce
    qu'une revue n'a pas raison par principe et que la trace doit le montrer : `modify` **ne
    nettoie pas** le drapeau (`ScenarioDisabledMissingIo_test::ModifyDoesNotClearTheDisabledFlag`
    l'épingle) ; c'est **`tryReenable()`** qui atteint `setDisabledMissingIo(false)` et lève la
    porte.
  - **Rien n'a été poussé.** Worktree `.wave28/e4.6a` nettoyé, branche `refactor/e4.6a` supprimée.
    **E4.6 reste 📋 — 1/8 livré (a)** ; b→h restent, et b→h sont ⛔ **après E4.1**.
- **E4.1a ✅ mergé** (2026-08-24, `c08d776e`, ff-only, historique linéaire) — **le pont
  dual-API de `Params` est coupé** : `src/lib/Params.h` n'inclut plus `<jansson.h>`, la classe
  n'expose plus qu'une face JSON (nlohmann). L'ancienne `Params::toJson()` **jansson** est
  déplacée hors de la classe dans l'adaptateur transitoire `jansson_from_params()` de
  `src/lib/Jansson_Addition.h`, corps inchangé. Périmètre : 15 fichiers `src/` (dont
  `JsonApi.cpp`, 70 sites), `tests/ParamsJson_test.cpp` neuf (520 lignes) et **2 lignes d'appel**
  dans 2 tests préexistants. Build docker complet (`autogen` + `configure` + `make -j32` +
  `make check`) : **70/70** (`TESTS` = **entrées**, pas lignes — 69 avant, +1 `ParamsJson_test`).
  **145 goldens intacts, hash d'arbre git identique** (`tests/core/golden` = `d4ebc61f` sur master
  comme sur la branche).
  - ⭐ **« Zéro octet observable » établi par TROIS preuves indépendantes**, pas par un `make check`
    vert. (1) Condensé **recalculé** : `sha256sum tests/core/golden/*.json | sha256sum` =
    `9788118b…` des deux côtés. (2) **Hash d'arbre git** identique — plus fort qu'un `diff --stat`,
    il prouve qu'aucun golden n'a été **ajouté, retiré ni modifié**. (3) **Dé-réécriture
    mécanique** : `sed 's/jansson_from_params(X)/X.toJson()/'` appliqué à tout le diff `src/` le
    ramène à master, à un résidu de **2 `#include`** (`Jansson_Addition.h` dans `IODoc.cpp`,
    `<jansson.h>` dans `IODoc.h`, qui l'obtenait gratuitement par `Params.h`), de commentaires, et
    de la fonction déplacée. **Aucune assertion d'un test préexistant modifiée**, vérifié
    explicitement : le diff `tests/` hors fichier neuf est **+2 / −2**, deux lignes d'**appel** au
    sérialiseur, **zéro `EXPECT`/`ASSERT` touché**. Un test dont l'assertion s'assouplit est une
    migration qui a neutralisé son propre témoin, et cela ne se voit **pas** dans un `make check`
    vert : le contrôle doit rester explicite à chaque sous-ticket E4.1.
  - **La justification `cbegin`/`cend` sans `begin`/`end`, mesurée.** `Params` expose une itération
    en lecture seule pour que l'adaptateur externe puisse le sérialiser. Ajouter une paire
    `begin()`/`end()` rendrait `is_compatible_array_type` **vrai** côté nlohmann : `Json j = params`
    produirait `[["k","v"]]` — un **tableau** — au lieu d'un objet. Piège réel, évité
    délibérément ; à ne pas « compléter » par confort dans un sous-ticket suivant.
  - **Dette transitoire bornée et adressable** : `jansson_from_params()` = **99 appels dans
    13 fichiers** (97 dans 11 fichiers de `src/`, dont `JsonApi.cpp` **70** et `WagoMap.cpp` **10** ;
    plus 2 dans 2 tests préexistants). **`grep -rn jansson_from_params src tests` EST** la liste
    exacte et courante de ce qui reste à convertir ; `Jansson_Addition.h` disparaît quand elle est
    vide. Le nouveau `ParamsJson_test.cpp` en ajoute 2, délibérés — il caractérise l'adaptateur.
  - ⭐⭐ **LE FAIT LE PLUS IMPORTANT POUR LA SUITE DE E4.1** : **les goldens ne couvriront PAS le
    changement d'échappement UTF-8**, ni pour les drivers **ni pour l'API**. Ils comparent des
    **documents parsés** — c'est le contrat d'oracle **sémantique** d'E4.0a — pas des octets.
    `\u00E9` et l’octet UTF-8 brut se parsent en la **même** chaîne : la suite reste verte pendant
    que le wire change. **Le tripwire est le seul garde-fou de toute la migration.** Il a été
    trouvé **défectueux** par la revue — il **minusculait le wire** avant de matcher, donc un port
    en `dump(ensure_ascii = true)` l'aurait laissé **vert** — et **corrigé** par les suites de
    revue : il épingle désormais les **trois** formes sur la **chaîne brute**, deux à deux
    différentes (jansson `JSON_ENSURE_ASCII` → `\u00E9`, hex **MAJUSCULE** ; nlohmann `dump()` nu →
    **octets UTF-8 bruts**, aucun échappement ; nlohmann `dump(ensure_ascii = true)` → `\u00e9`, hex
    **minuscule**), et il est **prouvé rouge par mutation** sur les **deux** ports réalistes.
  - **Une mesure qui corrige la revue** : la divergence ne se limite pas au non-ASCII. Les
    **caractères de contrôle divergent aussi** — `U+001F` sort `\u001F` sous jansson et `\u001f`
    sous nlohmann. La divergence apparaît **dès que l'hex contient une lettre** ; `U+0001` sort
    `\u0001` des deux côtés et **ne diverge pas**. Un tripwire bâti sur `U+0001` seul serait aveugle.
  - ⚠️ **Trois fuites `json_t` PRÉEXISTANTES consignées, NON corrigées** (hors périmètre : les
    corriger ici aurait brouillé la preuve de bascule mécanique) : `WagoMap::write_multiple_bits()`
    (`WagoMap.cpp:328-337`) et `WagoMap::write_multiple_words()` (`:402-411`) — le tableau `values`
    **n'est jamais émis**, c'est un **bug fonctionnel** en plus de la fuite, et c'est une **zone
    sans filet** (rien dans la suite n'appelle les drivers) ; `IODoc::genDocJson()`
    (`IODoc.cpp:163`) — `json_object_set` au lieu de `_new`.
  - **Deux rebases.** (1) Sur master post-E4.6a : **2 conflits, tous deux de fin de fichier,
    aucun arbitrage.** (2) Master ayant avancé pendant le build (`a85e38c2`, docs-only, découpage
    E4.1 b→x), **second rebase propre, zéro conflit** ; les arbres `src/` et `tests/` du commit
    **construit** et du commit **mergé** sont **identiques** (`03b48057` / `245c8ae2`), donc le
    70/70 porte bien sur ce qui est entré dans master.
    `tests/Makefile.am` reconstruit par **régénération** (master **en entier** + append verbatim du
    bloc de 20 lignes), **append pur prouvé byte-exact** (`head -1988 | cmp` contre
    `git show master:` → identique, et les 20 dernières lignes `cmp`-identiques au bloc de la
    branche : **0 ligne retirée, 0 modifiée**), équilibre `^if HAVE_GTEST` == `^endif` **58/58**.
    `FINDINGS.md` : **les deux blocs gardés dans l'ordre** (E4.6 de master, puis E4.1a), append pur
    prouvé de la même façon (1806 premières lignes byte-identiques à master). La note d'exclusion
    d'`IO/Scenario.cpp` d'`E4.1.md` est **préservée** — cohérente, E4.1a n'y touche pas et ce
    fichier n'a jamais appelé `Params::toJson`.
  - **Rien n'a été poussé.** Worktrees `.wave27/e4.1a` et `.review27/e4.1a` nettoyés, branche
    `refactor/e4.1a` supprimée. **E4.1 reste 📋** : `a` ✅, **b→x restent** (découpage posé par
    `a85e38c2`, 10 vagues) et ce sont eux qui migrent les 99 appels.
- **E4.5c, E4.5d, E4.5e ✅ mergés** (2026-08-24, `d002d2e8`, `23702a60`, `ebee5a3d`, ff-only,
  historique linéaire, **aucun commit de merge**) — trois sous-tickets de doc pure, **invariant
  tenu et vérifié mécaniquement commit par commit** (`git show --name-only` sur les **6** commits) :
  **zéro ligne de `src/`, zéro test, zéro golden**. `c` = `docs/02_io_drivers`, `05_audio`,
  `06_ipcam` (+ `RELEASE_NOTES`) · `d` = `docs/12_extern_proc`, `14_python_extern_proc`,
  `15_mcp_server` · `e` = `docs/07_remoteui`, `09_lua_scripting`, `13_utility_lib`, `README`.
  - ⭐⭐ **CE N'ÉTAIT PAS DE L'OBSOLESCENCE, C'ÉTAIT DE LA FAUSSETÉ — et à une échelle qu'aucun
    des trois briefs n'avait anticipée.** **Presque tous les noms de paramètres des drivers
    étaient faux** : MQTT `topic`/`topic_set` → **`topic_sub`/`topic_pub`** ; GPIO
    `gpio_number`/`inverted` → **`gpio`/`active_low`** ; KNX `address`/`feedbackAddress`/`datatype`
    → **`knx_group`/`listen_knx_group`/`eis`** ; Hue `api_key`/`light_id` → **`api`/`id_hue`** ;
    Squeezebox `playerid` → **`id`**. Un utilisateur qui suivait la doc écrivait une configuration
    **sans aucun effet** — pas une erreur, pas un avertissement : un silence. Pire encore sur les
    caméras : **les 5 types XML documentés étaient fantômes** (**0 occurrence** dans tout l'arbre),
    donc **aucune caméra ne se chargeait**. Côté Lua, **14 des 15 fonctions documentées n'existent
    pas** — la table réelle en porte **11**, en **camelCase** — et **`Timer` était documenté à
    l'envers** : il est **répétitif** et n'a **pas** de `stop()`. Ajouter : **`relay_num` est
    **1-based**, pas 0** ; le **framing `ExternProc`** annonçait un octet `START = 0x02`
    **inexistant** et une longueur sur **2 octets** au lieu de **4** ; et l'**exemple MQTT du `12`
    était inventé de bout en bout**. C'est exactement la leçon d'E4.0f, à plus grande échelle.
  - **L'ampleur de la vérification, parce que c'est le seul contrôle qui existe sur de la doc** :
    **~95 affirmations rouvertes au source** pour `e4.5d`, **~95** pour `e4.5c` (sur **252
    références mécaniquement contrôlées**), **~80** pour `e4.5e`. Les relecteurs ont **rejoué** les
    contrôles byte-identiques : **19/19**, **24/24**, **16/16**. Les trois revues indépendantes ont
    conclu `MERGE avec réserves` ; **toutes les réserves ont été fermées** par un commit de suite
    avant merge.
  - ⭐ **Une décision prouvée par exécution, pas par lecture** : le relecteur d'`e4.5e` a **exécuté**
    les deux scripts de garde — `check-config-docs` → « matches the generator » et
    `check-config-options` → « 52 keys used, 57 in registry, 5 obsolete ». Cela **prouve** que
    `docs/16_config_options.md` est à jour et **valide la décision de ne pas y toucher**, alors que
    le brief d'E4.5e l'incluait dans son périmètre. C'est la bonne forme : un script qu'on lance
    tranche mieux qu'une relecture.
  - ⭐ **LA CORRECTION LA PLUS IMPORTANTE APPORTÉE À NOS PROPRES NOTES DE VERSION.** Pour MySensors
    (T2.12) et Gadspot (T3.6) supprimés, `RELEASE_NOTES.md` annonçait « la configuration démarre
    normalement, l'IO inconnu est ignoré ». C'est **vrai au démarrage** — et **cela rassure à tort
    sur le fichier** : `main.cpp:196` planifie `checkAutoScenario()` **0,1 s après le boot**,
    laquelle se termine par `SaveConfigIO()`. Donc **un simple redémarrage, sans aucune action
    utilisateur, efface ces entrées d'`io.xml`**. La configuration n'est pas ignorée, elle est
    **amputée**. `RELEASE_NOTES.md` a été **corrigé en conséquence** ; si la préservation est le
    comportement voulu, c'est un ticket.
  - ⚠️ **Cinq bugs de code découverts au passage, versés en `FINDINGS.md`, aucun corrigé** (doc
    pure) : (1) ⭐ la **syntaxe d'index `path` est fausse dans la chaîne `ioDoc` elle-même** — donc
    `calaos_installer` l'affiche **à tous les utilisateurs** ; (2) **Roon reçoit `--port 0`**
    (`9330` passé dans le paramètre `bool mandatory`) **et perd `--host`/`--port` au respawn** :
    l'intégration est **probablement inutilisable** en hôte statique ; (3) l'**OTA compare les
    versions par égalité de chaînes** → un firmware **plus ancien** est proposé comme mise à jour ;
    (4) `setIOParam`/`waitForIO` **`return 1` sans push** → le script Lua récupère **son propre
    dernier argument**, donc **tout test de statut lit vrai** ; (5) le **throttle de login ne
    protège pas par client derrière un reverse-proxy**, sur **les deux transports** (ticket
    **`T3.24`**, en cours).
  - **Les trois rebases, et le conflit annoncé.** `FINDINGS.md` a conflité sur **les trois**
    branches (fin de fichier, chacune y ajoutant sa section pendant que master avançait) et une
    seconde fois sur le commit de suite d'`e4.5c`. **Résolution invariante : garder TOUS les blocs,
    dans l'ordre — master d'abord, branche ensuite**, jamais choisir. Contrôle systématique du
    nombre de titres `^## ` avant/après, qui ne doit qu'**augmenter** : **45 → 46**, **46 → 47**,
    **47 → 48**, et à chaque fois la liste des titres de master vérifiée **incluse en entier**
    (0 section perdue). Séparateur `---` **précédé d'une ligne vide** à chaque jonction — le piège
    du titre setext qui avait mordu le mergeur d'E4.1a. `RELEASE_NOTES.md` : **21 titres avant,
    21 après**, aucun perdu.
  - **Rien n'a été poussé.** Worktrees `.wave30/e4.5c`, `.wave31/e4.5d`, `.wave32/e4.5e` nettoyés,
    branches supprimées. **E4.5 reste 📋 — 5/6 livrés (a, b, c, d, e)** ; reste **`E4.5f`**
    (vérification de `08_http_api` et `10_events_notifications`), **en cours** sur `docs/e4.5f`.
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
- **🤖 PASSAGE DEPENDABOT (2026-08-24) — 7 PR instruites, 5 fermées, branche groupée prête, RIEN
  POUSSÉ, RIEN MERGÉ.** Branche livrée : **`chore/dependabot-2026-08-24`**, worktree
  `/home/raoul/repos/calaos/.wave29/dependabot`, **rebasée sur ce commit de docs, donc
  ff-only depuis `master`** (le SHA de tête bouge à chaque rebase — se fier au nom de branche).
  Deux commits, un par sujet, `src/bin/calaos_mcp/pyproject.toml` **seul fichier touché**.
  **`make check` VERT sur la branche** : `./autogen.sh && ./configure && make -j32 && make check -j16`
  dans le conteneur de build, sortie 0, **69/69 PASS**, 0 FAIL / 0 ERROR / 0 SKIP.
  - **Verdicts.** `#167` minimatch, `#169` picomatch, `#170` lodash, `#171` follow-redirects,
    `#168` immutable → **FERMÉES sur GitHub**, chacune avec un commentaire qui pose l'argument.
    Double motif : (1) **déjà appliqué** — le commit T3.11 `8db87507` a rafraîchi
    `data/debug/package-lock.json`, `master` porte déjà des versions ≥ celles proposées
    (immutable y est en **3.8.4**, la PR proposait **3.8.3** : c'était une *régression*) ;
    (2) **aucun chemin d'exposition** — les cinq sont des transitives `"dev": true` du toolchain
    gulp/browser-sync, et un grep de `data/debug/dist/` (bundles pré-buildés **commités**) n'y
    trouve trace d'aucune : `vendor.js` ne contient que jQuery/bootstrap/highlight.js, que
    `gulp-useref` concatène depuis le HTML. À noter : les 2 alertes `immutable` exigent **4.3.9**,
    inatteignable — `browser-sync@3.0.4` épingle la ligne `immutable@^3`. Elles sont à **écarter**
    (« vulnerable code is not actually used »), pas à corriger ; je ne l'ai pas fait moi-même,
    ça sort du mandat.
  - `#174` **mcp 1.16.0 → 1.28.1** → **retenue**, reprise dans la branche (1er commit).
    Ferme GHSA-9h52-p55h-vw2f, GHSA-jpw9-pfvf-9f58, GHSA-vj7q-gjh5-988w. `mcp.server.fastmcp` et
    `mcp.server.transport_security`, les deux seules portes d'entrée de `server.py`, sont
    inchangées en 1.28.1.
  - `#175` **starlette 0.46.2 → 1.3.1** → **À TRAITER AUTREMENT, et c'est la trouvaille du
    passage : la PR est INSTALLABLE NULLE PART.** `fastapi==0.115.12` exige
    `starlette>=0.40.0,<0.47.0` ; poser `starlette==1.3.1` à côté donne `ResolutionImpossible`.
    Dependabot monte le paquet vulnérable **en isolation** et ne voit pas le couplage. Repris en
    **montée coordonnée** dans la branche (2e commit) : `starlette 1.3.1` **+**
    `fastapi 0.115.12 → 0.141.1` (la première série fastapi qui accepte starlette 1.x est vers
    0.13x). Ferme les 7 alertes starlette. Les deux PR pip sont **laissées ouvertes** avec un
    commentaire expliquant l'intégration groupée — Dependabot les fermera au merge.
  - **Pourquoi grouper** : `.github/workflows/docker-publish-dev.yml` se déclenche sur **tout**
    push vers `master`, **sans `needs:` sur `build-and-test`** — il incrémente la version, crée un
    tag git, publie `ghcr.io/calaos/calaos_base:dev` + un tag versionné, et dispatche un
    `build_deb` vers `calaos/pkgdebs`. 5 merges = 5 publications.
  - **⚠️ Le vert de la CI des PR pip ne vaut RIEN, et il faut le savoir avant de rejuger.**
    `.github/workflows/ci.yml` n'a **aucune** étape Python. Le conteneur de build n'a ni `mcp`,
    ni `starlette`, ni `fastapi`, ni `pytest` : `tests/run-python-tests.sh` (T2.14) retombe sur
    `unittest discover -p 'test_t116_*.py'` et saute les 3 suites pytest. Et **aucune** des six
    suites de `tests/python/` n'importe `calaos_mcp.server`. `make check` ne couvre donc pas ce
    changement — c'est ainsi que #175, irrésoluble, est passée verte. **Vérification faite hors
    bande** : venv python3.11 dédié dans le conteneur, jeu complet installé, `create_app()`
    construit, `GET /healthz` → 200, `POST /mcp` initialize → 200 (protocole 2025-06-18),
    `POST /mcp` sans Bearer → 401, `tests/python` 34 passed / 2 skipped — sur les trois jeux
    (pins actuels en témoin, #174 seule, jeu coordonné). Plus le `make check` C++ complet sur la
    branche.
  - **🔴 F-DEP-1 — trouvaille hors périmètre, la plus grave du passage, consignée dans
    `FINDINGS.md`** : `Dockerfile:38` et `:69` installent les dépendances du sidecar **non
    pinnées** (`pip install "mcp[cli]" uvicorn fastapi websockets`). Le `pyproject.toml` que
    Dependabot surveille n'est utilisé par **aucun** chemin de build (il n'est même pas dans
    l'`EXTRA_DIST` de `src/bin/calaos_mcp/Makefile.am`). Mesuré : cette commande résout
    aujourd'hui vers **`mcp 2.0.0`**, où `mcp.server.fastmcp` **n'existe plus** — donc
    `server.py:25` échoue à l'import et **toute reconstruction de l'image publie un sidecar MCP
    qui ne démarre pas**. Le test `configure.ac:220` (`import mcp, uvicorn, fastapi`) **passe**
    quand même : `HAVE_PYTHON_MCP` ne rattrape pas la casse. **F-DEP-2** : les suites Python
    n'exercent jamais `server.py` (dont l'accès à l'API **privée** `mcp._session_manager`).
  - **T3.21 → renuméroté T3.22.** Le numéro T3.21 était **déjà pris** par l'extrait `del_param`
    de T3.20 (ligne de board existante). Le ticket Dependabot est donc écrit dans
    **`T3.22.md`** : il reste **pertinent** — le manifeste pip n'est déclaré nulle part, les deux
    PR pip ne viennent que des *security updates*, et c'est justement leur montée **en isolation**
    qui a produit #175 irrésoluble. Le cœur du ticket est la stratégie `groups:` (une PR mensuelle
    au lieu d'une par paquet, donc une publication au lieu de six) ; **F-DEP-1 en est le prérequis
    de fond**, sans quoi on surveillerait une fiction.
  - **➡️ PROCHAINE ACTION** : l'utilisateur décide du moment de la publication, puis merge
    `chore/dependabot-2026-08-24` (**ff-only**, déjà rebasée) — un seul cycle
    tag + image + `build_deb`. **Avant ou juste après**, traiter **F-DEP-1** : sans pin du
    `Dockerfile`, la montée du `pyproject.toml` ne change rien à l'image déployée, qui reste
    cassée par `mcp 2.0.0`.
- **I4.1 ✅ FAIT (2026-08-24) — dépôt `calaos_installer`, ⛔ NON POUSSÉ.** Ticket externe issu
  d'E4.6 §7bis, mais **autonome** : il concerne **toutes** les règles. Les quatre `if (x)` sans
  `else` de `projectmanager.cpp` (`:1023`, `:1054`, `:1082`, `:1125`) jetaient toute
  entrée/sortie à id non résolu au chargement de `rules.xml` ; comme l'installeur **régénère
  `rules.xml` en entier** à la sauvegarde, ouvrir puis sauvegarder un projet désaccordé
  **détruisait** conditions et actions, **sans un mot**. Mesuré sur `tests/test` (449 règles)
  contre un `io.xml` vide : **0 condition / 0 action** écrites avant, **621 / 664** après,
  fichier **octet-pour-octet identique** à l'original. Option **(a) préserver** retenue (IO
  fantôme portant l'id, **hors `Room`** → n'atteint jamais `io.xml`, la règle se remet à marcher
  seule si l'IO revient) **plus** l'avertissement de (b) (`ProjectManager::missingIOReport()`,
  affiché par `MainWindow::Load()` après le modal de progression). **Deux trouvailles au-delà du
  cadrage** : (1) un **segfault** — `Condition::output` n'était pas initialisé, une
  `<condition type="output">` à id pendant faisait déréférencer un pointeur indéterminé à la
  sauvegarde (reproduit, exit 139) ; (2) une **seconde purge silencieuse à l'affichage** —
  `formrules.cpp:1481/:1554/:1658` supprimaient le `val_var` mort depuis le remplissage de
  l'arbre des règles. Site 2 (`DialogListProperties`, préfixes `autoscenario_`/`as_`) fait dans
  **son propre commit**, car il **anticipe E4.6 non implémenté** — vérifié inerte (aucun param en
  usage ne porte ces préfixes). Site 3 (affichage/édition des scénarios) **non fait**, hors
  périmètre. Le dépôt **n'a pas de tests** (`tests/` = projets d'exemple) mais **se construit**
  (qmake6/Qt6) : construit avant et après, vérifié par un harnais lié sur `ProjectManager`
  **hors dépôt** (scratchpad, non commité). **Non vérifié : le rendu GUI réel.** Détail complet :
  [`I4.1.md`](I4.1.md).
- **I4.1 — SUITES DE REVUE ✅ (2026-08-24), verdict `MERGE`, réserves toutes fermées, +2 commits.**
  ⭐ **R1 : la préservation avait créé sa propre régression.** `ListeRoom::get_new_id()` ne balaie
  que les **rooms** ; un fantôme vit **hors** de toute room, donc l'id d'un IO manquant paraissait
  **libre** — créer un IO juste après l'ouverture d'un projet désaccordé lui donnait cet id exact
  et le **branchait silencieusement** sur les règles de l'IO disparu. Une destruction silencieuse
  échangée contre un **câblage silencieux** : meilleur défaut, mais bien plus dur à diagnostiquer
  et invisible dans le diff. **Corrigé** — ids pendants **réservés** dans `ListeRoom`,
  reconstruits à chaque chargement de `rules.xml` (donc la réservation dure autant que la
  référence pendante) et purgés par `clear()`. A/B en une exécution sur `project1` amputé :
  `input_0`/`output_39` **avant**, `input_57`/`output_48` **après** — soit exactement ce que rend
  un `io.xml` complet. ⚠️ Ce correctif-là a lui-même introduit un
  **`double free` à la sortie** (purge appelée depuis `~ListeRoom()`, pendant la destruction des
  statiques), reproduit 5/5 **et seulement en présence de fantômes** : ensemble de réservation
  rendu **immortel**, 7 configurations revérifiées sans abort. **R2** : `on_addButton_clicked`
  n'était pas gardé → un param `as_*` créé vide, écrit dans `io.xml`, **ni éditable ni
  supprimable** ; corrigé. **R3** : rapport ventilé entrée/sortie et pointant où retrouver les ids
  au-delà du plafond (pire cas mesuré : **21 lignes**, la revue tranche contre ma crainte de
  bruit). **Formulation corrigée** dans `I4.1.md` : « octet-pour-octet identique » vaut de
  l'**aller-retour**, pas de la première écriture (`project1` : 14,3 K → 18,4 K, puis point fixe).
  **Deux préexistants de l'installeur versés en `FINDINGS.md`**, non corrigés :
  `Action::duplicate()` perd `action_touchscreen_cam`, et `FormConditionStd::qitem` non initialisé.
  ⛔ **Trou de vérification connu** : la sauvegarde **déclenchée depuis le GUI** et l'ouverture par
  clic de `FormConditionStd`/`FormActionStd` sur un fantôme restent jugées sûres **à la lecture
  seulement**.
  **➡️ ACTION UTILISATEUR** : relire les **5** commits sur le `master` local de
  `calaos_installer` (`6cbd6f6`, `a84027c`, `1dfaed2`, `dac15cb`, `fa04c64`) et pousser lui-même.
- **⭐ E4.5f ✅ mergé (2026-08-24, `530db772`, 2 commits, doc pure) — ET L'ÉPIQUE `E4.5` EST CLOSE,
  6/6 (a→f).** Périmètre : `docs/08_http_api.md`, `docs/10_events_notifications.md`
  (+ `FINDINGS.md`). **Vérification, pas réécriture** : E4.0f les avait écrits contre le code et
  le fond tenait ; ce qui les avait périmés est T3.18/T3.19 et surtout la **dérive des numéros de
  ligne**. Revue indépendante : `MERGE` avec réserves, **les trois fermées** par le second commit.
  - ⭐⭐ **LE BLOQUANT, ET C'EST UN ANGLE MORT MÉTHODOLOGIQUE NEUF.** `08_http_api.md` affirmait
    « rien n'est retiré du payload ». **Inversion pure** : `Scenario::toJson()` fait
    `if (!sa.io) continue;` — **`IO/Scenario.cpp:158` et `:181`** (la revue avait écrit `:160`,
    l'auteur a **remesuré à `:158`**, c'est la bonne valeur). Ce qui rend le cas exemplaire :
    **le golden que la section citait elle-même le prouvait** — **2 actions à l'étape 2** dans
    `e40c_ws_autoscenario_get.json`, **1** dans `..._get_broken.json` — et le test s'appelle
    littéralement **`ABrokenStepSilentlyLosesItsActionFromThePayload`**. **Personne ne l'avait
    vu** : ni l'auteur d'E4.5f, ni E4.0f qui avait écrit la section, ni la revue d'E4.0f.
    ⇒ **LEÇON À RETENIR, distincte de la « fixture pauvre » et de la « citation infidèle » : on
    vérifie qu'une citation de golden est *fidèle*, jamais qu'elle *SOUTIENT* la phrase qu'elle
    illustre.** Une citation exacte peut démontrer le contraire de son paragraphe.
  - **La seconde réserve est de la même famille.** La référence `WebSocket.cpp:314-315` est
    **existante et plausible** — elle contient bien un `429` — mais elle désigne
    l'**authentification RemoteUI**, pas le plafond par IP, lequel passe par
    **`HttpClient.cpp:273-281`**. Une référence peut être vivante, vraisemblable, et **désigner
    autre chose**.
  - **Position retenue sur le script de contrôle des références** (candidat à ticket, **pas encore
    un ticket**) : il n'attrape la dérive **que si l'assertion est ancrée** — citer un **fragment
    attendu** à la ligne, pas seulement `Fichier.cpp:ligne`. **Aucun** des deux défauts de cette
    branche n'aurait été attrapé par un contrôle non ancré. Donc : **ancré**, en cible
    **`make check-docs` NON bloquante**, **hors** de `make check` — *un faux rouge sur de la doc à
    chaque refactoring finirait par être désarmé, et un contrôle qu'on désarme vaut moins que pas
    de contrôle* — et **obligatoire à chaque revue de doc**.
  - ⭐ **LE BILAN CHIFFRÉ DE L'ÉPIQUE E4.5, qui est le vrai résultat : la documentation n'était pas
    obsolète, elle était FAUSSE.** Noms de paramètres erronés pour **tous** les drivers (MQTT,
    GPIO, KNX, Hue, Squeezebox) → **une configuration écrite d'après la doc n'avait aucun effet** ;
    **5 types XML de caméra fantômes** (0 occurrence dans l'arbre) → **aucune caméra ne se
    chargeait** ; **14 des 15 fonctions Lua documentées n'existaient pas** ; **`Timer` documenté à
    l'envers** ; **`relay_num` 1-based** donné pour 0-based ; framing `ExternProc` **inventé**
    (octet `START` inexistant, longueur sur 2 octets au lieu de 4) ; **exemple MQTT entièrement
    fabriqué** ; et sur `08`, **36 groupes de références sur 329** ne pointaient plus sur rien,
    jusqu'à **190 lignes d'écart**. Côté vérification : **~95 / ~95 / ~80 / ~60** affirmations
    rouvertes au source par les relecteurs, contrôles byte-identiques **19/19, 24/24, 16/16,
    23/23**.
  - ⚠️ **Les bugs de CODE découverts en écrivant la doc, tous versés en `FINDINGS.md`, AUCUN encore
    corrigé** : (1) ⭐ **syntaxe d'index `path` fausse dans la chaîne `ioDoc` elle-même** — donc
    **affichée à tous les utilisateurs par `calaos_installer`** ; (2) **Roon reçoit `--port 0`**
    (`9330` passé dans le paramètre `bool mandatory`) **et perd `--host`/`--port` au respawn** ;
    (3) **OTA comparant les versions par égalité de chaînes** → un firmware **plus ancien** est
    proposé comme mise à jour ; (4) `setIOParam`/`waitForIO` **`return 1` sans push** → **le script
    récupère son propre dernier argument**, donc **tout test de statut lit vrai** ; (5) le
    **throttle de login indexe le pair TCP sur les deux transports** (ticket **`T3.24`**, en cours).
  - **Le rebase et son conflit annoncé.** `FINDINGS.md` a conflité une fois, en fin de fichier
    (la branche y ajoute sa section pendant que master avançait) — **le même conflit que les
    quatre du merge précédent**. **Résolution invariante : garder TOUS les blocs, dans l'ordre
    master-d'abord-branche-ensuite**, jamais choisir. Contrôle des titres `^## ` : **49 (master) +
    1 (branche) → 50**, et la liste des **49 titres de master vérifiée incluse en entier et dans
    l'ordre** (0 section perdue). Séparateur `---` **précédé d'une ligne vide** à la jonction —
    le piège du titre setext. Second commit appliqué sans conflit. **Pas de build : zéro ligne de
    code dans les 2 commits, contrôle docs-only mécanique sur chacun.**
  - **Rien n'a été poussé.** Worktree `.wave35/e4.5f` nettoyé, branche `docs/e4.5f` supprimée.
    **`E4.5` bascule 📋 → ✅ sur le board — 6/6 livrés (a→f).**
- **⭐ T3.23 ✅ MERGÉ (2026-08-24, `f3189a9b`, 1 commit, ff-only) — F-DEP-1 corrigé : l'artefact
  publié était cassé.** Le sidecar MCP ne démarrait **pas** dans `ghcr.io/calaos/calaos_base:dev`
  (digest `sha256:75485fc6…`) : `ModuleNotFoundError: No module named 'mcp.server.fastmcp'`, avec
  `mcp 2.0.0` installé. Cause : un `pip install` **non pinné** dans **les deux stages** du
  `Dockerfile`, doublé d'un `pyproject.toml` **surveillé par Dependabot mais lu par aucun chemin de
  build**. Branche **`fix/t3.23`**, ticket `T3.23.md` (T3.21 pris par l'extrait `del_param` de
  T3.20, T3.22 par la surveillance pip → **T3.23** est le premier libre). **RIEN POUSSÉ.**
  - **Le correctif : le manifeste devient la source, pas un second jeu de pins.** Épingler des
    numéros dans le `Dockerfile` aurait créé un deuxième endroit à maintenir. `pip install
    ./src/bin/calaos_mcp` a aussi été **écarté** : cela installerait le paquet `calaos_mcp` dans
    `site-packages` **en doublon** de `/opt/lib/calaos/calaos_mcp` (installé par `Makefile.am`,
    mis dans `PYTHONPATH` par `calaos_mcp.in`) — deux copies, dont une jamais mise à jour.
    Retenu : **`scripts/pyproject-requirements.py`** (`tomllib`, stdlib ≥ 3.11) émet
    `[project].dependencies` en requirements. Les **deux** stages du `Dockerfile`, le
    `.devcontainer/Dockerfile` et un job CI neuf copient **le même manifeste** et appellent **le
    même script**.
  - **Une seule passe de résolveur, sur tout** : `pip install -r requirements.txt roonapi
    reolink-aio`. mcp/fastapi/starlette sont couplés (c'est ce qui a rendu #175 irrésoluble) ;
    en passes séparées, une passe tardive écrase en silence ce qu'une passe antérieure a épinglé.
    **`roonapi`/`reolink-aio` n'entrent PAS dans le `pyproject.toml`** (extern procs de
    calaos_server, pas du sidecar) et restent **non pinnées comme avant** — mais partagent
    désormais la passe : une incompatibilité **casse le build** au lieu de dégrader l'image en
    silence. Mesuré après correctif : `roonapi 0.1.6`, `reolink-aio 0.21.11`, détectées par
    `configure`.
  - **Le garde-fou teste l'API, plus la présence.** `configure.ac` importe exactement ce que les
    sources importent (relevé sur `calaos_mcp/*.py`, pas de mémoire) + `FastMCP.streamable_http_app`.
    Cela ajoute **5 distributions** que l'ancienne sonde ignorait, dont **`websockets`**
    (`client.py:16`, jamais sondé — signalé par E4.5d en cours de route). Les 6 sont **toutes**
    dans le `pyproject.toml` : la bascule n'en fait disparaître aucune, **vérifié**. Sonde
    négative sur l'image cassée : ancienne sonde **exit 0** (le mensonge), nouvelle
    **ModuleNotFoundError**. En cas d'échec `configure` **imprime la trace d'import**.
  - **Job CI `mcp-sidecar-deps`** : `build-and-test` n'installe aucun Python, donc
    `HAVE_PYTHON_MCP` = no et le sidecar y est **entièrement sauté** (F-DEP-2). Le nouveau job
    installe le jeu du `pyproject.toml` (même script, même passe unique) et exerce l'API **privée**
    `FastMCP.streamable_http_app()` + `_session_manager` dont dépend `server.py:167-169`.
  - **`calaos_mcp --help` / `--version`** ajoutés, traités **après** les imports de module :
    la commande traverse toute la chaîne d'import sans socket ni config ni effet de bord. C'est
    le one-liner « est-ce que cette image peut démarrer ».
  - **VÉRIFICATION — image réellement construite.** `docker build` **exit 0** (5 min 42) ;
    log de `configure` : `checking for the Python API the calaos_mcp sidecar imports... yes`.
    Acceptation sur l'image, **sans montage** : `calaos_mcp --help` **exit 0** ;
    `python3 -c "from mcp.server.fastmcp import FastMCP"` **ok, exit 0**. Sidecar **démarré
    dedans** (UDS + `local_config.xml` de test) : `GET /healthz` **200**, `POST /mcp` sans Bearer
    **401**, `initialize` avec Bearer **200** (`serverInfo.name = "calaos"`). Versions installées :
    **mcp 1.16.0**, fastapi 0.115.12, starlette 0.46.2, uvicorn 0.34.2, websockets 15.0.1,
    pydantic 2.11.4 — les 6 pins honorés **au numéro près**. Stages `dev` et `runner` comparés
    paquet par paquet : **`diff` vide**.
  - **Composition avec `chore/dependabot-2026-08-24` : ZÉRO fichier en commun.** T3.23 ne touche
    **pas** `pyproject.toml` (délibéré) ; la branche Dependabot ne touche **que** lui. Les deux se
    mergent dans **n'importe quel ordre**, sans conflit — mais **Dependabot n'a d'effet sur l'image
    qu'une fois T3.23 mergé**, puisque avant T3.23 aucun build ne lisait ce fichier. Ordre
    recommandé : **T3.23 d'abord, Dependabot ensuite** (le pin est alors effectif dès sa première
    publication), ou les deux dans un même cycle de publication.
  - **fastapi 0.141.1 vs 0.135.0 — mesuré, pas déduit.** Le jeu Dependabot a été passé dans le
    nouveau pipeline, en deux variantes : `mcp 1.28.1 + starlette 1.3.1` avec **fastapi 0.141.1**
    et avec **fastapi 0.135.0**. **Les deux résolvent** (roonapi/reolink-aio compris) et **les deux
    passent** la sonde d'API, `_session_manager` inclus. La sonde ne les départage donc pas.
    **Recommandation : garder 0.141.1** — c'est la seule des deux qui ait été exercée de bout en
    bout (create_app, `/healthz` 200, `POST /mcp` 200, 401, 34 tests) par le passage Dependabot ;
    reculer sur 0.135.0 échangerait une version testée contre une version seulement importée.
  - **✅ LE `.deb` EST COUVERT — vérifié en revue, ce n'est plus un risque.** `gh` a lu
    `calaos/pkgdebs` : `docker-publish-dev.yml` → `repository_dispatch build_deb` (`image_src`) →
    `build_deb.yml` → un `Makefile` dont **tout** le `build` écrit **deux lignes** dans
    `container.source`. **Le `.deb` n'embarque ni l'image ni aucun `site-packages`** : c'est un
    wrapper podman (`ExecStart=/usr/bin/podman run --pull=never ${IMAGE_SRC}`, image tirée au
    `postinst`). **Corriger le `Dockerfile` suffit, aucune action côté `pkgdebs`.** Seul le contenu
    de `pull_calaos_image` reste non lu.
  - **SUITES DE REVUE (verdict `MERGE` avec réserves, aucun bloquant) — les deux réserves sont
    traitées, branche rebasée sur `530db772`** (un seul conflit, `ORCHESTRATION.md`, journal).
    **R1 — la doc enseignait encore le geste qui a cassé l'image** : `docs/15_mcp_server.md`
    portait toujours `pip3 install "mcp[cli]" uvicorn fastapi websockets pydantic` (E4.5d a
    réécrit ce fichier +452/−97 **sans** corriger la commande — il ne pouvait pas savoir), et sa
    description de la sonde + l'avertissement « ne couvre pas `websockets` » devenaient **faux au
    merge**. Les trois sont corrigés : la commande **renvoie au manifeste et au script** au lieu de
    re-lister des paquets (sinon on recrée la divergence dans la doc), la sonde est décrite en
    tableau, l'avertissement périmé est retiré. **R2 — la sonde `configure` promettait
    « the exact set » sans le tenir** : ajoutés **`mcp.settings.transport_security`**
    (`server.py:64`, module level) et **`mcp._session_manager`** (`server.py:171`, **privée** —
    exactement le genre d'API qui disparaît sans préavis, cf. `mcp.server.fastmcp`) ; **retiré**
    `pydantic.BaseModel/Field`, dont le seul importeur `models.py` **n'est importé par personne**.
    La sonde **rejoue désormais la séquence de `server.py`** au lieu de s'arrêter aux imports ;
    `ci.yml` aligné à l'identique. **Revérifié après R2** : `docker build` exit 0, `configure`
    « ...sidecar uses... yes », les deux critères d'acceptation exit 0, `/healthz` 200 / 401 / 200.
  - **⚠️ CE TICKET AMÉLIORE LA DÉTECTION, PAS LE BLOCAGE — décision utilisateur.** Le job CI
    **n'empêche pas** la publication : `docker-publish-dev.yml` est un workflow séparé **sans
    `needs:`**, et **`master` n'est pas protégée** (404 sur `/protection`, rulesets vides) → aucun
    *required status check*, un job rouge ne bloque rien. Rendre `mcp-sidecar-deps` bloquant est
    une **configuration de dépôt**, hors de portée du code.
    - ⛔ **CORRECTION (2026-08-25, décision utilisateur — voir `DECISIONS.md`, « DEUX canaux de
      publication »).** ~~Le constat ci-dessus était présenté comme un **trou à refermer**, et la
      protection de `master` comme le geste manquant.~~ **C'est faux, et la raison est
      d'architecture** : `master` **est le canal de préversion** (`ghcr…:dev`, `.deb` de
      préversion), destiné aux développeurs ; ce qui atteint les utilisateurs, ce sont les
      **releases**. **Protéger `master` empêcherait son usage prévu.** L'absence de *required
      status check* n'est donc pas un défaut de configuration : c'est la conséquence du rôle de la
      branche. Le constat factuel (workflow séparé, sans `needs:`) reste exact ; **c'est la
      conclusion qu'on en tirait qui est retirée**.
  - **Versé en FINDINGS (non traité ici, élargirait le ticket)** : **F-DEP-3** la divergence peut
    revenir — un `RUN pip install foo` en dur passerait toute la CI au vert ; le dépôt a pourtant
    le patron (`tests/check-config-docs.sh` dans `make check`), l'analogue manque → ticket proposé
    **`tests/check-pydeps-single-source.sh`**. **F-DEP-4** deux angles morts du script :
    `[project.optional-dependencies]` **ignoré** (dep sous un extra silencieusement perdue) et
    **aucun `==` exigé** (un futur `mcp>=1.0` re-flotterait sans bruit) ; extras et marqueurs
    PEP 508 passent en revanche **verbatim**, donc corrects. **F-DEP-5** incohérence de version :
    `__init__.py` dit `0.1.0`, `pyproject.toml` dit `1.0.0`, et **`/healthz` expose `0.1.0`**.
    **F-DEP-6** `pip show … | grep` masque un code retour (étape informative). **F-DEP-7**
    `models.py` est **du code mort** (importé par personne) — la raison pour laquelle `pydantic`
    figurait à tort dans la sonde.
  - **Reste ouvert** : **F-DEP-2** (aucun test n'exerce `create_app()` en CI — fait à la main ici) ;
    `pydantic-settings 2.15.0`, transitive **non pinnée** de `mcp`, émet un
    `IncompleteFieldDefinitionWarning` à chaque démarrage — cosmétique, mais même classe de défaut,
    non traité pour garder la branche disjointe du `pyproject.toml`.
  - **LE MERGE (agent dédié).** Rebase sur `c4fc8b78` (master avait avancé de E4.5f pendant la
    revue) : **un seul conflit**, `ORCHESTRATION.md`, journal — master et la branche ajoutent
    chacun leur puce au même endroit du bloc 🔁 REPRISE. **Résolution invariante : garder TOUS les
    blocs, master d'abord, branche ensuite**, jamais choisir. Contrôle des titres `^## ` : **9
    avant / 9 après** des deux côtés (0 section perdue), `^### ` : 2 / 2 ; aucun `---` non précédé
    d'une ligne vide (piège du titre setext). **Le rebase n'a rien déplacé de la chaîne de build** :
    les arbres git de `Dockerfile`, `.devcontainer/`, `scripts/`, `.github/`, `configure.ac` et
    `Makefile.am` sont **identiques** à ceux de `52a2089e` — donc **le contenu mergé est exactement
    celui qui a été construit et exercé** (et `Dockerfile`/`.devcontainer/`/`scripts/` n'ont
    d'ailleurs jamais bougé depuis le tout premier commit de la branche : seuls `.github` et
    `configure.ac` ont changé, au commit des suites R2). Le delta rebase vaut **59 insertions dans
    2 fichiers de doc**, soit exactement `c4fc8b78`.
  - **⚠️ LE RISQUE PRINCIPAL DU MERGE, LEVÉ : `./configure` reste vert sans les modules Python.**
    La sonde relevée rejoue la séquence de `server.py` et touche l'API **privée**
    `mcp._session_manager` ; le conteneur de build standard n'a **aucun** de ces modules. Mesuré
    dans l'image de build : `checking for the Python API the calaos_mcp sidecar uses... no`, puis
    trois `configure: WARNING:` (dont la trace `ModuleNotFoundError: No module named 'mcp'`) — et
    `configure` **poursuit jusqu'au bout** (`config.status: creating …`, `Python support: yes`).
    C'est bien `AC_MSG_WARN`, jamais `AC_MSG_ERROR` : **`HAVE_PYTHON_MCP=no` et le build passe**,
    exactement comme avant. Une sonde plus stricte qui aurait fait échouer `configure` aurait été
    une régression de build silencieuse — elle n'a pas lieu.
  - **Build de merge** : `make check` **70/70 PASS, 0 FAIL, rc 0** — le compte de master
    (`tests/Makefile.am` est **identique** entre master et la branche : 70 entrées `TESTS`, ce
    ticket n'ajoute aucun test C++). Arbre git des goldens `tests/core/golden` **inchangé** :
    `d4ebc61fb2b1876f587d075a0cb050750dc1876f` sur master, sur la branche et après merge.
    ⚠️ *Note d'outillage, sans rapport avec le ticket* : un `docker run` attaché dont le client est
    tué laisse `make` **tourner en boucle sur son stdout orphelin** après la fin des tests — relancé
    détaché avec sortie en fichier, `make check` rend la main en < 1 min avec rc 0.
  - **Ce qui NE change pas** : le nouveau job CI améliore la **détection**, pas le **blocage**.
    `docker-publish-dev.yml` reste un workflow séparé **sans `needs:`** et `master` n'est pas
    protégée → aucun *required status check*.
    ⛔ **CORRECTION (2026-08-25, décision utilisateur)** : ~~« la fenêtre qui a laissé publier une
    image cassée reste donc ouverte »~~ — **il n'y a pas de fenêtre, il y a un canal.** L'image
    `:dev` cassée était **une préversion sur le canal prévu pour les préversions** ; elle n'a
    **pas** atteint les utilisateurs. ⇒ **la gravité de l'épisode tombe**, le correctif T3.23 reste
    entièrement juste mais sa **justification** change : on corrige parce qu'une préversion cassée
    fait perdre du temps aux développeurs, **pas** parce qu'un artefact cassé aurait été livré.
    **La recommandation de protéger `master` est RETIRÉE** — voir `DECISIONS.md`, « DEUX canaux de
    publication : `master` = préversion, *release* = tout le monde ».
  - **➡️ SUITE IMMÉDIATE : merger `chore/dependabot-2026-08-24` maintenant.** L'ordre était
    **T3.23 d'abord, Dependabot ensuite** (zéro fichier en commun, vérifié deux fois) : avant
    T3.23 le `pyproject.toml` n'avait **aucun effet** sur l'image, donc merger Dependabot seul
    n'aurait **rien corrigé**. Il est désormais effectif.
  - **Worktree `.wave33/t3.23` nettoyé, branche `fix/t3.23` supprimée. RIEN POUSSÉ** — un push
    vers `master` est une **publication** (bump de version, tag, image ghcr, dispatch `build_deb`),
    et c'est précisément ce qui a publié l'image cassée.
- **⭐ DEPENDABOT 2026-08-24 ✅ MERGÉ (`a444f873`, 2 commits, ff-only) — la montée COORDONNÉE
  `mcp 1.16.0→1.28.1` + `fastapi 0.115.12→0.141.1` + `starlette 0.46.2→1.3.1`.** Branche
  `chore/dependabot-2026-08-24`, **`src/bin/calaos_mcp/pyproject.toml` seul fichier touché**.
  **RIEN POUSSÉ.** Ticket de suite : `T3.22.md` ; findings **F-DEP-1..7** dans `FINDINGS.md`.
  - **L'ORDRE ÉTAIT LE POINT, ET IL A ÉTÉ TENU : T3.23 d'abord, celui-ci ensuite.** Avant T3.23,
    ce `pyproject.toml` n'était lu par **aucun chemin de build** — merger Dependabot seul n'aurait
    **rien corrigé** dans l'image publiée. Depuis T3.23, `scripts/pyproject-requirements.py` en
    fait la **source unique** des quatre chemins (deux stages du `Dockerfile`, devcontainer, job CI).
    **Ce merge-ci est donc le premier qui a un effet réel sur l'artefact.** Zéro fichier en commun
    entre les deux branches (vérifié deux fois) : rebase sur `ff6b6ab8` **sans aucun conflit**,
    aucun bloc de journal à fusionner. Titres `^## ` d'`ORCHESTRATION.md` : **9 avant / 9 après**.
  - **⭐ VERSIONS EFFECTIVEMENT INSTALLÉES, relevées DANS L'IMAGE reconstruite** (`docker build`
    complet, exit 0, `calaos_dep2408:test`) — elles reflètent bien le bump, la source unique
    fonctionne : `mcp` **1.28.1** · `fastapi` **0.141.1** · `starlette` **1.3.1** · `uvicorn`
    **0.34.2** · `websockets` **15.0.1** · `pydantic` **2.11.4** · (transitives : `pydantic-settings`
    2.15.0, `sse-starlette` 3.4.8 ; non pinnées : `roonapi` 0.1.6, `reolink-aio` 0.21.11). Les deux
    stages `dev` et `runner` résolvent le **même** jeu (`mcp==1.28.1 / fastapi==0.141.1 /
    starlette==1.3.1` imprimés par `cat requirements.txt` dans les deux couches pip).
  - **⭐ LA SONDE `configure.ac` DE T3.23 A TENU FACE À `mcp 1.28.1`** — c'était le risque n°1 de ce
    merge, puisqu'elle touche l'API **privée** `mcp._session_manager`, exactement le genre de chose
    qui bouge entre 1.16 et 1.28. Dans le log de build : `checking for the Python API the calaos_mcp
    sidecar uses... yes`. Rejouée à part dans l'image finale : `probe ok`, exit 0.
  - **Acceptation, sidecar RÉELLEMENT démarré dans l'image** (socket UDS + `local_config.xml` de
    test, `curl --unix-socket`) : `GET /healthz` → **200** `{"status":"ok","version":"0.1.0"}` ·
    `POST /mcp` sans `Authorization` → **401** (`Auth failure from unknown: Missing Bearer token`) ·
    `POST /mcp` `initialize` avec Bearer → **200**, `serverInfo.name = "calaos"`. Plus les deux
    critères sans montage : `/opt/bin/calaos_mcp --help` **exit 0** et
    `python3 -c "from mcp.server.fastmcp import FastMCP"` **exit 0**.
  - **`make check` 70/70 PASS, 0 FAIL, rc 0** — le compte de master, ce lot n'ajoute aucun test C++.
    Arbre git des goldens `tests/core/golden` **inchangé** : `d4ebc61f…` sur master, sur la branche
    et après merge. *(Note d'outillage, reconfirmée : un `docker run` attaché dont le client est tué
    par un timeout laisse `make` tourner sur son stdout orphelin — relancé détaché, rc 0.)*
  - **LES 5 PR FERMÉES, et l'argument qui les ferme : le chemin d'exposition.** Toutes npm sur
    `data/debug`. Ce sont des **transitives `"dev": true`** du toolchain gulp/browser-sync, et
    `data/debug/dist/` est **pré-buildé et commité** : ces paquets ne sont **jamais shippés**, ni
    dans l'image, ni dans le `.deb`, ni dans le bundle servi. Cas remarquable : **#168 `immutable`
    3.8.3 était une RÉGRESSION** — master est déjà en **3.8.4**. Et les alertes `immutable` visent
    `<4.3.9`, **inatteignable** puisque `browser-sync` épingle `^3` : aucune montée ne les fermera.
  - **#175 `starlette` 0.46.2 → 1.3.1 n'était installable NULLE PART en isolation.**
    `fastapi==0.115.12` exige `starlette>=0.40.0,<0.47.0` → `pip` : `ResolutionImpossible`. Les
    *security updates* de Dependabot montent le paquet vulnérable **seul** et ne voient pas le
    couplage. D'où la fusion en une montée **coordonnée** avec `fastapi` (et `mcp`, couplé aux deux).
    ⚠️ **Et la CI ne l'a pas vu, et ne pouvait pas le voir** : `ci.yml` n'avait **aucune étape
    Python**, et rien dans le build ne résolvait jamais ce `pyproject.toml` — les deux PR étaient
    **vertes**, d'un vert qui ne portait aucune information. Corrigé par le job `mcp-sidecar-deps`
    de T3.23 ; c'est ce job qui donne enfin du sens à `T3.22` (entrée pip + `groups:`).
  - **ARBITRAGE TRANCHÉ : `fastapi 0.141.1` retenu, contre `0.135.0`.** Les deux résolvent et
    passent la sonde. Mais **0.141.1 est la seule exercée de bout en bout** (34 tests Python,
    `create_app()`, `/healthz`, `initialize`) ; reculer échangerait une version **testée** contre une
    version seulement **importée**. Le gain de conservatisme est nul, le coût est une vérification
    perdue.
  - **CE QUI RESTE OUVERT** : **F-DEP-2** — aucun test CI ne construit `create_app()` (le job
    `mcp-sidecar-deps` exerce l'API, pas l'app) · **F-DEP-3** — la divergence peut revenir : **rien**
    ne rattrape un `RUN pip install` ajouté en dur au `Dockerfile` (garde
    `check-pydeps-single-source.sh` proposée) · **F-DEP-5** — `__init__.py` déclare `0.1.0` et le
    `pyproject` `1.0.0`, et c'est **0.1.0 qu'expose `/healthz`**, donc la version lue sur le réseau
    n'est pas celle du paquet (reconfirmé ci-dessus) · `pydantic-settings 2.15.0` émet un
    `IncompleteFieldDefinitionWarning` (champ `lifespan`, référence avant non résolue) **au démarrage
    du sidecar** — bruyant, sans effet observé sur les trois appels d'acceptation, à surveiller.
  - ⚠️ **LES 12 ALERTES DEPENDABOT RESTENT OUVERTES.** Fermer une PR ne ferme pas l'alerte. Les
    **2 alertes `immutable`** devraient être ***dismissed*** avec le motif « vulnerable code is not
    actually used » — geste **non fait** (hors mandat de l'agent) qui **revient à l'utilisateur**.
  - **Worktree `.wave29/dependabot` nettoyé, branche `chore/dependabot-2026-08-24` supprimée.
    RIEN POUSSÉ** — un push vers `master` est une **publication** (bump de version, tag git, image
    ghcr, dispatch `build_deb`), et c'est exactement ce qui avait publié l'image au sidecar mort.
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
    bash -c "./autogen.sh && ./configure && make -j32 && make check -j16"
  ```
> ⚙️ **Parallélisme (corrigé le 2026-08-25).** La machine a **64 cœurs / 62 Go**. Le `-j12`
> historique n'en utilisait qu'un cinquième, et surtout **`make check` tournait EN SÉRIE**
> sur ~79 binaires — c'était la moitié du temps de chaque cycle. `serial-tests` n'est pas
> activé (`configure.ac:10`), donc le harnais parallèle d'automake s'applique.
> Utiliser désormais : **`make -j32 && make check -j16`** si l'agent builde seul,
> **`make -j16 && make check -j8`** quand 3-4 agents buildent en parallèle (cas normal en vague).
> **Plafond : ~4 agents build-lourds simultanés** — au-delà ils se volent le CPU.
> `-j16` et non `-j64` sur les tests : plusieurs binaires ouvrent des sockets et lancent des
> boucles libuv, les entasser risque des collisions de ports plutôt qu'un gain.
> ⚠️ Distinct de l'autre cause de lenteur : **plusieurs builds Docker concurrents**. Un seul
> build à la fois par agent, attendu par `docker wait`.

- ASan : depuis E4.3cd, plus de `CXXFLAGS` bricolés — utiliser l'option de configure.
  ```
  ./autogen.sh && ./configure --enable-asan && make -j32 && \
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
