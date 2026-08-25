# Orchestration — état opérationnel & reprise

> Ce fichier est le **point d'entrée pour reprendre le travail** depuis n'importe quel
> Claude / n'importe quelle machine. Il est git-tracké : il voyage avec le repo.
> Lire dans l'ordre : ce bloc REPRISE → `DECISIONS.md` → `FINDINGS.md` → `BOARD.md`.

---

## 🔁 REPRISE — lire en premier

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
  - **Ce qui NE change pas, et qui revient à l'utilisateur** : le nouveau job CI améliore la
    **détection**, pas le **blocage**. `docker-publish-dev.yml` reste un workflow séparé **sans
    `needs:`** et **`master` n'est toujours pas protégée** → aucun *required status check*.
    **La fenêtre qui a laissé publier une image cassée reste donc ouverte** — c'est une décision de
    configuration de dépôt, pas de code.
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
