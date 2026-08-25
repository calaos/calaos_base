# Findings bonus — backlog

> Découvertes faites **en marge** des tickets (hors périmètre du ticket en cours, donc **non
> corrigées**). Candidates à de futurs tickets. Sorti du job tmp éphémère → durable + partagé.

## E4.1j — wire Lua aval (2026-08-25)

- ⭐⭐ **[F-LUA-1] Un script Lua écrit par l'utilisateur peut injecter des OCTETS ARBITRAIRES dans
  le JSON du wire — ce wire n'est PAS comme Wago, OLA ou Hue.** Mesuré au source :
  `ScriptBindings.cpp` prend `lua_tostring()` et le met **directement** dans une valeur de chaîne
  JSON, sur **trois** entrées atteignables en une ligne de script :

  ```lua
  calaos.sendPushNotif(string.char(0xFF))          -- "message"
  calaos.setIOValue("io_x", string.char(0xFF))     -- "value"
  calaos.setIOParam("io_x", "k", string.char(0xFF))-- "value"
  ```

  Une chaîne Lua est une **chaîne d'octets** ; LuaJIT est du Lua 5.1, donc `string.char()` **et**
  l'échappement décimal `"\255"` existent. Le script arrive par `rules.xml` ou par l'API JSON.

  ⭐ **MESURÉ, PAS RAISONNÉ** (revue) : un programme liant le **vrai LuaJIT** du conteneur
  (`LUA_VERSION_NUM = 501`), avec la garde `lua_isstring`/`lua_tostring` **recopiée verbatim** de
  `Lua_Calaos::sendPushNotif`, montre que **quatre orthographes passent la garde et livrent
  l'octet** ; `ScriptWire::dumpJson()` rend `push-\ufffd-tail`, un `j.dump()` nu lève
  `type_error.316 invalid UTF-8 byte at index 5: 0xFF`.

  ⚠️ **PRÉCISION QUI VISE `E4.1m`** : une troisième orthographe — **un octet brut écrit dans le
  TEXTE du script** — **ne survit PAS au transport aujourd'hui**. `ScriptExec.cpp:154-157` fait
  passer le script par `jansson_from_params()`, dont `json_string()` rend `NULL` sur cet octet et
  **laisse tomber la paire entière** : `calaos_script` ne reçoit alors **pas ce script du tout**.
  Ce qui passe aujourd'hui, ce sont `string.char(0xFF)` et `"\255"`, parce que **le script reste
  ASCII pur et que l'octet naît dans la VM**. **Le chemin brut s'ouvrira avec `E4.1m`**, qui migre
  `ScriptExec.cpp` ; le gestionnaire posé par E4.1j le couvre déjà. **Ne pas citer ce chemin comme
  atteignable avant `E4.1m`.**
  ⇒ **Sur ce wire, `ensure_ascii` et `error_handler_t::replace` sont PORTEURS, pas défensifs.** Un
  `dump()` nu ici lève `type_error.316` **depuis le callback de lecture `ExternProc`**, où rien
  n'attrape : `std::terminate` de `calaos_script` au milieu du script de l'utilisateur. C'est
  exactement le précédent KNX (le driver tué par un variateur à 78 %).
  **Corrigé par construction dans E4.1j** : les trois invariants sont dans `ScriptWire::dumpJson()`
  et nulle part ailleurs.

- ℹ️ **[F-LUA-2] La bascule change le MODE d'échec sur UTF-8 invalide — assumé, sans note de
  version.** Avant : `json_string()` rendait `NULL`, `json_object_set_new()` rendait `-1`, **aucun
  code de retour n'était testé**, et la **paire entière** était supprimée — le serveur recevait
  `send_push_notif` avec un `data` **vide**, ou un `set_state` **sans `value`**. Après : la clé est
  là, l'octet fautif est devenu **U+FFFD**. Les deux issues sont du garbage pour l'utilisateur (la
  notification est vide dans un cas, illisible dans l'autre), l'entrée est **déjà cassée** dans les
  deux, et l'observable utilisateur ne change pas de nature. **Même arbitrage que F-REO-6 : pas
  d'entrée `RELEASE_NOTES`.** ⚠️ Si un relecteur juge l'inverse, c'est **ici** qu'est la mesure.

- ⚠️ **[F-LUA-3] LE TROU DES SITES D'APPEL, MESURÉ, et ce que le typage ferme vraiment.**
  Trois permutations jouées sur la branche, build complet + `CXXLD ScriptWire_test` exigé à chaque
  fois :
  | Permutation | Résultat |
  |---|---|
  | `ScriptWire::buildSetParamMessage(ParamKey{key}, IoId{id}, …)` | **NE COMPILE PAS** (`invalid initialization of reference of type 'const ScriptWire::IoId&'`) |
  | `io.set_param(key, value)` → `io.set_param(value, key)` (`ScriptBindings.cpp`) | **VERT 0/32** |
  | `buildPushNotifMessage(lua_tostring(L,1), PushAttachment{lua_tostring(L,2)})` → indices échangés | **VERT 0/32** |

  ⚠️ **LES DEUX « VERT 0/32 » SONT VRAIS MAIS NON INFORMATIFS — à ne pas lire comme un trou de
  couverture.** Ils sont **structurellement garantis** : `ScriptWire_test_SOURCES =
  ScriptWire_test.cpp` **seul**, ~~et `ScriptBindings.cpp` **n'est lié dans AUCUN binaire de
  test** de l'arbre~~ ⛔ **FAUX, voir le bloc ⛔ CORRECTION en fin d'entrée**. Le vert **constate
  que le site d'appel est hors d'atteinte**, il ne mesure pas la faiblesse du filet. C'est une
  **limite de périmètre**.

  ⇒ **Le typage ferme la permutation des ARGUMENTS de la fonction, pas celle de leurs SOURCES.**
  Tant qu'un site d'appel construit lui-même les deux valeurs typées (`IoId{a}, ParamKey{b}` vs
  `IoId{b}, ParamKey{a}`), la permutation reste compilable. La fermeture complète demanderait de
  typer `LuaIOBase::set_param()` et le dépilement Lua eux-mêmes, ce qui déborde du périmètre.

  ⭐ **C'est une QUATRIÈME FORME distincte du défaut de la série**, confirmée en revue : les
  précédentes (« emballer la mauvaise variable », « agrégat positionnel ») portaient sur **un**
  argument mal rempli ; ici les deux valeurs typées sont **construites au site d'appel**, chaque
  `X{…}` est **individuellement bien typé**, et la faute est dans l'**appariement source →
  emballage**. Réductible **en principe**, **pas ici**. (Précédents : Wago `F-WAGO-4`, Reolink
  `username`↔`password`.)

  ⛔ **CORRIGÉ PAR T3.27 (2026-08-25) — LA PRÉMISSE EST FAUSSE, ET LE TROU EST REFERMÉ.**
  La phrase « `ScriptBindings.cpp` **n'est lié dans AUCUN binaire de test** de l'arbre » est
  **inexacte** : `tests/Makefile.am` liait déjà
  `$(CALAOS_SERVER_BUILDDIR)/LuaScript/ScriptBindings.$(OBJEXT)` dans **`LuaSandbox_test`**
  (bloc T1.15), et `LuaSandbox_test.cpp` exécute déjà du Lua littéral contre un vrai `lua_State`
  (`calaos:getEnv("no_such_key")`). Le « VERT 0/32 » observé était **vrai pour `ScriptWire_test`**,
  dont `_SOURCES` est bien le seul `.cpp` — mais la généralisation à *tout* l'arbre ne tenait pas.
  ⚠️ **Ce que le vert mesurait vraiment** est plus intéressant que « hors d'atteinte » :
  `LuaSandbox_test_DEPENDENCIES = libcalaos_common.la` (comme **47 des 80 suites d'alors**,
  cf. [`T3.36`](T3.36.md)), donc muter `ScriptBindings.cpp` **ne relie pas** le binaire — le
  mutant n'était **jamais exécuté**. C'est un **faux vert de relink**, pas une limite de périmètre.
  ⇒ `tests/LuaCalaosApi_test.cpp` (T3.27) décode la trame `set_param` émise sur une **vraie socket
  AF_UNIX**, et la permutation `io.set_param(value, key)` **ROUGIT** (`TheParamIsActuallyWritten
  OnTheIo` + `WhatTheScriptActuallySeesComingBack`, `.o` et binaire effacés, `CXXLD` exigé).
  **La quatrième forme du défaut reste réelle** — le typage ne ferme toujours pas l'appariement
  source → emballage — mais elle est désormais **couverte par un test**, pas seulement déclarée.

- 📏 **[F-LUA-7] `docs/09_lua_scripting.md` : deux références mesurées périmées, hors périmètre
  T3.27.**
  ⛔ **RENUMÉROTÉ (2026-08-25, revue de T3.27) : cette entrée a d'abord été publiée sous
  `F-LUA-5`, identifiant DÉJÀ PRIS** par « la bascule perd le texte d'erreur du parseur »
  (même fichier, plus bas). `F-LUA-6` étant pris aussi, le premier libre était **`F-LUA-7`**
  (vérifié : aucune occurrence de `F-LUA-7` dans `docs/` avant ce renommage). T3.27 a recalé les **8** références qui pointaient au-delà de son point d'insertion dans
  `ScriptBindings.cpp` (+26 lignes), mais deux autres écarts ont été **mesurés au passage** et
  **non corrigés**, faute d'appartenir à ce ticket :
  - `:350` citait `ScriptBindings.cpp:185-190` pour le dispatch `set_value` ; le dispatch occupe
    **`:186-191`** (décalage **antérieur** à T3.27, hérité d'E4.5e). **Corrigé quand même**, la
    référence tombait à un caractère de sa cible.
  - `docs/02_io_drivers.md:162` cite `ScriptBindings.cpp:348,363` pour « Bindings Lua
    (`downloadFile`, POST) » ; ces deux lignes étaient (avant T3.27) des **commentaires**, les
    `dl->setInsecure()` étant à `:349`/`:364`. T3.27 les repointe sur les appels réels
    (`:375`/`:390`) plutôt que de propager l'écart.
  ⇒ **Matière pour [`T3.32`](T3.32.md)** (contrôle ancré) : les deux écarts étaient invisibles
  parce qu'ils tombaient sur une ligne **existante mais non pertinente**, exactement le motif que
  T3.32 décrit.

- 📏 **[F-LUA-4] RECALAGE OBLIGATOIRE — le défaut `setIOParam`/`waitForIO` a bougé de +1 ligne, et
  deux références d'un autre finding sont MORTES.**
  - Le défaut connu (« `return 1` sans rien empiler », § *Lua — quirks d'API* plus bas) est
    **inchangé et non corrigé** par E4.1j, comme demandé. Ses sites passent de
    `ScriptBindings.cpp:293` / `:333` à **`:294`** / **`:334`** (décalage d'exactement +1, dû au
    seul `#include "ScriptWire.h"` ajouté en tête). Le fichier passe de **495 à 484 lignes**.
  - ⛔ **`F-REO-1` cite `LuaScript/ScriptBindings.cpp:435,:494` comme sites `jansson_to_string`
    corrects : CES DEUX SITES N'EXISTENT PLUS.** Les deux corps `sendJson()` dupliqués ont été
    remplacés par les constructeurs nommés de `ScriptWire.h`. Tout recompte de
    `jansson_to_string` postérieur à E4.1j doit retirer ces deux-là. **Non corrigé en place dans
    `F-REO-1`** pour ne pas réécrire la mesure d'un autre ticket.

- ℹ️ **[F-LUA-5] La bascule perd le texte d'erreur du parseur dans un `cWarningDom`.** Le message
  « *Error parsing json from sub process* » citait `jerr.text` (position et cause de l'erreur de
  syntaxe) ; `Json::parse(msg, nullptr, false)` n'a pas d'équivalent non levant. Le **message brut
  reste journalisé** à côté, ce qui est ce qu'un lecteur exploite réellement. **Même choix que
  `WagoWire` et `ReolinkWire`.** Récupérer le texte demanderait un `try`/`catch` autour du
  **parse** — pas interdit par la décision utilisateur (qui vise le `dump()`), mais c'est de la
  surface ajoutée pour un journal. → ticket possible, non recommandé.

- 📏 **[F-LUA-6] La liste de dépendances d'`E4.1x` est passée de 4 à 2 puis à ZÉRO.**
  Mutation **fidèle** (`IO/ExternProc.h` cesse de déléguer, `src/lib/Jansson_Addition.h`
  **intact**), `make -C src -j16 -k`, mesurée **trois fois** :

  | arbre | RC | objets en échec | lignes `error:` |
  |---|---|---|---|
  | `db6770a7` (master avant `E4.1d`/`E4.1f`) | 2 | **4** — les 2 OLA + mes 2 | 62 |
  | `d1462d9e` (master actuel) **seul** | 2 | **2** — `LuaScript/ScriptBindings.cpp`, `LuaScript/ScriptExtern_main.cpp` | 34 |
  | ⭐ `d1462d9e` + `refactor/e4.1j` | **0** | **ZÉRO** | **0** |

  Sur la dernière ligne les **sept** binaires sont produits (`calaos_server`, `calaos_ola`,
  `calaos_knx`, `calaos_mqtt`, `calaos_wago`, `calaos_script`, `calaos_1wire`).
  ⇒ **`E4.1x` n'est plus bloqué par cette liste.** ⚠️ Mesure faite avec `libola`, `eibclient`
  **et** `mosquitto` présents (`HAVE_LIBOLA_TRUE=''`, `HAVE_LIBKNX_TRUE=''`,
  `HAVE_LIBMOSQUITTO_TRUE=''` dans `config.log`) : les trois binaires conditionnels sont
  **réellement compilés**. La réserve d'exposition conditionnelle d'`E4.1c` est levée **sur cette
  machine**, pas dans l'absolu.

## E4.1d — Squeezebox et Hue (2026-08-25)

> Périmètre : `Audio/Squeezebox.cpp`, `IO/Hue/HueOutputLightRGB.cpp`. **Hors périmètre, NON
> corrigé.** Tout ce qui suit préexiste à la migration jansson → nlohmann et lui survit à
> l'identique.

- **[F-HUE-1] Le log d'état de la lampe est faux depuis toujours : `sat` n'y figure pas, `hue` y
  figure deux fois.** `HueOutputLightRGB.cpp` (ligne du `cDebugDom("hue") << "State: "`) écrit
  `"State: " << on << " Hue : " << hue << " Bri: " << bri << " Hue : " << hue`. Le second `" Hue :
  "` devait manifestement être `" Sat : " << sat`. Conséquence : **la saturation lue sur le pont
  n'est jamais tracée**, et un lecteur du log croit voir deux champs alors qu'il en voit un deux
  fois. Purement cosmétique (un `cDebugDom`, coupé par défaut) mais trompeur pendant un
  diagnostic. **Gelé tel quel par E4.1d** — corriger un log n'a aucun rapport avec un changement
  de bibliothèque JSON, et le figer aurait demandé un test de log qui n'existe pas.

- **[F-SQBOX-1] La requête JSON-RPC de `get_album_cover()` est assemblée par concaténation, avec
  l'identifiant du lecteur interpolé SANS ÉCHAPPEMENT.** `Squeezebox.cpp:740-742` :
  `postData = "{\"id\":1,\"method\":\"slim.request\",\"params\":[\"" + id + "\",[…]]}"`.
  `id` vient de **`param["id"]`** (`Squeezebox.cpp:75`), donc de `io.xml` (une adresse MAC en
  pratique), mais
  **rien ne le contraint** : un `id` portant un guillemet ou une contre-oblique produit une requête
  **syntaxiquement invalide**, que LMS rejette — la pochette échoue alors en silence et le driver
  bascule sur le chemin CLI. Ce n'est pas une injection exploitable (l'`id` est de la config
  locale, pas une entrée réseau), c'est une **fragilité de construction**. ⚠️ **E4.1d ne l'a pas
  corrigée volontairement** : la migration porte sur la **lecture**, et construire cette requête
  avec `nlohmann` changerait les octets envoyés à un **tiers** (`{"id":1,…}` deviendrait
  `{"id":1,"method":…,"params":…}` **trié**), ce que l'invariant 3 de l'épique interdit sans
  déclaration. Ticket dédié recommandé, avec caractérisation d'abord.

- **[F-SQBOX-2] `Squeezebox.o` n'est lié par AUCUN binaire de test.** Mesuré : `tests/Makefile.am`
  ne nomme `Squeezebox.$(OBJEXT)` nulle part, et le commentaire de `tests/Makefile.am:1100`
  l'assume (« Squeezebox was rejected: it drags SqueezeboxDB, UrlDownloader and the nine AVR… »).
  Conséquence à connaître avant de croire un vert : **toute mutation du corps de
  `Squeezebox.cpp` est invisible pour la suite entière**, par construction et pas par manque de
  cas. C'est pourquoi E4.1d a sorti la lecture dans `Audio/SqueezeboxWire.h` — mais les **sites
  d'appel** de ce fichier restent, eux, hors de portée de tout test.

## E4.1h — Wago (2026-08-25)

- ✅ **[F-WAGO-1] — CORRIGÉ dans E4.1h** (commit `e6ab8589`, branche `refactor/e4.1h`), **contre la
  recommandation écrite de `E4.1h.md` et de `E4.1.md` § Q2**, sur instruction explicite de
  l'orchestrateur **et** parce que la mesure ci-dessous retire le seul risque invoqué.

  **`WagoMap::write_multiple_bits()` (`WagoMap.cpp:328-337`) et `write_multiple_words()`
  (`:402-411`) n'émettaient jamais leur tableau `values`, et fuyaient `jret`.** Mécanisme vérifié
  au source : `jansson_to_string()` (`Jansson_Addition.h:150-165`) **vole** la référence, et
  `jansson_from_params(p)` était appelé **deux fois** — la première dans `jret`, qui recevait le
  tableau, la seconde **dans l'appel à `sendMessage`**. Le tableau ne quittait donc jamais le
  processus, et `jret` (avec le tableau dont il avait pris possession) **n'était jamais rendu**.
  ⚠️ **Ce n'est PAS le motif « `json_decref` de trop »** de `ReolinkCtrl.cpp:151` et
  `MqttCtrl.cpp:115` : c'est une **référence jamais rendue**, la faute inverse. Ne pas confondre
  les deux dans un balayage.

  ⛔ **CE QUE LES FICHES DISAIENT EST FAUX, ET C'EST LE POINT IMPORTANT.** `E4.1h.md:32`,
  `E4.1.md:196` et `ORCHESTRATION.md:594` décrivent « **une panne silencieuse en production** » /
  « l'écriture multiple n'écrit rien, en silence, depuis toujours ». **Recompté ici par balayage
  exhaustif de l'arbre** (`.cpp/.h/.c/.py/.lua/.xml/.json`, artefacts de build exclus, en
  `python3`) : **`WagoMap::write_multiple_bits()` et `write_multiple_words()` n'ont AUCUN
  APPELANT** — `WagoMap.h:188` et `:194` (déclarations) plus les deux définitions, **rien d'autre**.
  Aucun IO Wago (`WODigital`, `WOAnalog`, `WODali`, `WODaliRVB`, `WOVolet`, `WOVoletSmart`,
  `WITemp`, …) ne les appelle. `calaos_server` n'a donc **jamais** émis `action:"write_bits"` ni
  `"write_words"` ; les branches `WagoExternProc_main.cpp:140-176` et `:245-283` **n'ont jamais été
  atteintes** ; **aucun automate n'a jamais reçu ce message**.
  ⇒ **Il n'y a pas de panne en production : la fonctionnalité est morte de bout en bout.**
  ⇒ **Aucune entrée `RELEASE_NOTES`** — rien d'observable par un utilisateur ne change, ni avant ni
  après la correction.
  ⇒ Et le motif de non-correction (« ça change ce que reçoit un automate réel ») **ne tient pas** :
  il n'y a pas d'automate à l'autre bout.

  **Épinglé par des tests** : `tests/WagoWire_test.cpp` gelait l'**absence** dans deux cas nommés
  `WriteMultiple{Bits,Words}RequestCarriesNoValuesArray_BUG` (octets exacts **et** nombre de clés,
  pas un `!contains()` nu) ; le commit de correction les **flippe** au lieu de les supprimer.
  Contre-mutation **M12** (re-supprimer le tableau du constructeur livré) → **3 cas rouges**.

- ✅ **[F-WAGO-2] — CORRIGÉ par T3.30** (2026-08-25, `51962e51` caractérisation + `55e75ff6`
  correction, branche `fix/t3.30`). **Les trois défauts ensemble**, `IO/Wago/WagoBits.h` est
  désormais du code de production appelé par `WagoCtrl.cpp` **et** par `tests/WagoBits_test.cpp`
  (14 cas, 12 rouges avant dont **2 SIGSEGV sans aucune ligne `FAILED`**). Sept contre-mutations,
  **sept ensembles rouges deux à deux distincts**, témoin à 0 ; M4 (retrait de la garde)
  **reproduit le SIGSEGV**. ⚠️ **Pas d'entrée `RELEASE_NOTES`** : l'absence d'appelant a été
  revérifiée (balayage `python3`, 628 fichiers, appels indirects compris — `&WagoMap::` = 8, aucune
  vers `write_multiple_*` ; `&WagoCtrl::` = 0 ; aucune sous-classe, aucune méthode virtuelle), la
  chaîne est morte des deux côtés du wire et **rien d'observable ne change**. ⛔ Restent NON faits :
  ASan, automate réel, et la **permutation `address`/`nb` au site d'appel**, qui laisse la suite
  verte (mesuré, mutation M6) et se ferme par le **typage** — T3.31. Analyse d'origine conservée
  telle quelle ci-dessous ;
  deux corrections de référence apportées par le ticket : `setBit()` est à **`WagoCtrl.cpp:51-60`**
  (et non `:50-58`), et la sur-allocation existe aussi dans `read_bits()` `:95` (bénigne, son
  `memset` couvre tout). **NON CORRIGÉ, hors périmètre. ⭐ TICKET DÉDIÉ RECOMMANDÉ, PRIORITÉ MOYENNE :
  `WagoCtrl::write_multiple_bits()` est fonctionnellement FAUX, et pas seulement à cause de
  F-WAGO-1.** Trois défauts distincts sur les mêmes dix lignes (`WagoCtrl.cpp:152-176`, jumeau
  `:220-242`), dont **deux trouvés par la revue et non par moi**.

  **(a) `count` n'est jamais confronté à `values.size()`.** `setBit(*data, i, values[i])` pour
  `i ∈ [0, nb)` et `data[i] = values[i]` de même : `nb` vient du message, `values` du tableau
  décodé. **Avec F-WAGO-1, `values` arrivait VIDE et `nb` valait le compte annoncé.**
  ⭐ **Mesuré par la revue, et c'est pire que ce que j'avais écrit : `values[0]` sur un
  `vector<bool>`/`vector<UWord>` vide SEGFAUTE** (SIGSEGV 139, `_M_start` nul). Ce n'était donc pas
  « l'écriture multiple n'écrit rien » : c'était **la mort de `calaos_wago`**, si le chemin avait
  été atteint. **Ma correction de F-WAGO-1 ne referme PAS ce défaut-là** : elle le rend seulement
  inoffensif pour un appelant qui passe `nb == values.size()`.

  **(b) ⭐ `setBit()` n'écrit jamais que le PREMIER octet.** `WagoCtrl::setBit(unsigned char &mot,
  int pos, bool val)` (`WagoCtrl.cpp:50-58`) prend une **référence à UN SEUL octet** et fait
  `mot = mot | (0x01 << pos)`. L'appelant passe `*data`, c'est-à-dire **toujours `data[0]`**, avec
  `pos = i` jusqu'à `nb - 1` — au lieu de `data[i / 8]` et `pos = i % 8`, ce que `read_bits()` fait
  correctement deux fonctions plus haut (`:100-101`). Vérifié : 16 `setBit` successifs sur un octet
  donnent `0xff`. ⇒ **seuls les bits 0 à 7 sont jamais écrits**, et **`pos ≥ 32` est un
  comportement indéfini de décalage**. Une écriture multiple de plus de 8 bits est donc fausse
  **même avec `values` livré**.

  **(c) Le dernier octet du tampon n'est pas initialisé.**
  `new mbus_ubyte[nb / 8 + nb % 8]` puis `memset(data, '\0', nb/8)` : le `memset` est **plus court
  que l'allocation**. Pour `nb = 17` → 3 octets alloués, **2 seulement mis à zéro**. (L'allocation
  elle-même sur-dimensionne parfois — `nb/8 + nb%8` au lieu de `(nb+7)/8`, ex. `nb=15` → 8 octets —
  donc ce n'est pas un dépassement de tampon, mais le dernier octet part vers l'automate avec de la
  mémoire arbitraire dedans.)

  ⇒ **Ce ticket doit contenir les deux choses** : la garde `count` / `values.size()` **et** la
  réécriture de la boucle de bits (`data[i / 8]`, `i % 8`, `memset` sur toute l'allocation).
  **Pas urgent — aucun appelant aujourd'hui — mais c'est un piège armé pour le premier qui écrira
  l'appelant**, et il ne se manifestera pas par un message d'erreur : par un segfault ou par des
  sorties fausses.
  ⚠️ **Le segfault est mesuré, les points (b) et (c) sont démontrés au source** ; rien n'a tourné
  sous ASan ni contre un automate réel, et le chemin est inatteignable en l'état.

- ⚠️ **[F-WAGO-7] — PARTIELLEMENT CORRIGÉ par T3.30 (voir l'addendum en fin d'entrée) : `int count;`
  ET `UWord address;` partent NON INITIALISÉS dans
  les quatre branches du dispatcher de `calaos_wago`.** `WagoExternProc_main.cpp:76`/`:81`,
  `:132`/`:137`, `:161`/`:166`, `:218`/`:223` déclarent `int count;` puis appellent
  `Utils::from_string(jsonData["count"], count)` **sans regarder son retour**.
  ⭐ **Correction de périmètre apportée par la revue de T3.30 : `count` n'est pas seul.** La ligne
  au-dessus de chacune des quatre déclarations de `count` est un **`UWord address;`** tout aussi
  nu (`:75`, `:131`, `:160`, `:217`), suivi du même `Utils::from_string(jsonData["address"],
  address)` dont le retour n'est pas lu davantage — et les **deux autres branches** du dispatcher
  (`:108`, `:193`, écriture d'un bit / d'un mot unique) déclarent elles aussi un `address` nu, sans
  `count` — et `write_word` y ajoute un **`UWord value;`** nu (`:194`), lui aussi rempli par un
  `from_string` dont le retour n'est pas lu. Une clé `address` absente envoie donc une **adresse modbus de pile arbitraire** à
  l'automate, et pour les écritures c'est une **écriture à une adresse quelconque**, ce qui est
  strictement pire qu'une lecture fausse. La correction est la même et de la même taille :
  `UWord address = 0;` / `int count = 0;`, et un test du retour de `from_string`. Or `from_string`
  rend **`true` sans rien écrire** sur une chaîne vide (c'est le défaut que **T3.25** corrige) :
  une clé `count` absente ou vide laissait donc `count` valant de la mémoire de pile arbitraire,
  et c'est **cette valeur-là** qui partait vers `WagoCtrl::read_bits()`, `read_words()` et les
  deux écritures multiples. ⭐ **T3.30 rend le cas inoffensif pour les deux écritures multiples**
  — un `count` arbitraire dépasse presque toujours `values.size()` et est refusé, un `count`
  négatif l'est aussi — **mais pas pour les deux LECTURES** (`:76` et `:161`), qui allouent
  `new mbus_ubyte[coilBufferSize(count)]` / `new mbus_uword[count]` sur ce même entier. **Ce qui
  manque est une ligne, pas une refonte** : `int count = 0;`, et un test du retour de
  `from_string`. ⚠️ **Ne pas le confondre avec T3.25** : T3.25 corrige `from_string`, ceci corrige
  ses **appelants**, qui resteraient fragiles même avec une `from_string` parfaite le jour où la
  clé est absente plutôt que vide.

  ⭐ **ADDENDUM T3.30 (revue de merge, 2026-08-25) — la moitié `nb <= 0` des DEUX LECTURES est
  fermée ; `int count = 0;` reste à faire.** La revue avait relevé que `coilBufferSize()` écrase
  `nb <= 0` en 0 et que `read_bits()` l'appelait sans garde. **Remesuré au `g++ -std=c++11` avant
  correction, et le partage entre régression et défaut préexistant est net — les deux moitiés ne
  se traitent pas pareil** :

  | `nb` | `master` (`nb / 8 + nb % 8`) | `fix/t3.30` avant cet addendum |
  |---|---|---|
  | `-1`, `-7`, `-8`, `-9`, `-40` | **négatif** ⇒ `new mbus_ubyte[négatif]` **lève `std::bad_alloc`** | `0` ⇒ `new mbus_ubyte[0]`, **aucune exception** |
  | `0` | `0` ⇒ `new mbus_ubyte[0]`, **aucune exception** | `0` ⇒ idem, **inchangé** |

  ⇒ **`nb < 0` était une RÉGRESSION INTRODUITE par T3.30** (un plantage bruyant devenu un tampon
  vide silencieux) : jamais livrée, jamais publiée, **donc pas un défaut de production** — elle est
  fermée dans la branche et consignée dans [T3.30](T3.30.md), pas ici. **`nb == 0` était et reste
  un défaut PRÉEXISTANT, livré, sur les DEUX lectures** (`read_words()` : `new mbus_uword[0]`) :
  c'est *lui* qui appartient à cette entrée, et c'est l'extraction de `coilBufferSize()` qui l'a
  rendu visible, pas elle qui l'a créé. Les deux moitiés sont closes par la même garde,
  `WagoBits::countIsReadable(nb)` en tête de `read_bits()` **et** de `read_words()`, épinglée par
  cinq oracles (`tests/WagoBits_test.cpp`) dont deux fils de source, un par lecture.
  ⚠️ **Ce qui NE l'est pas** : `int count = 0;` / `UWord address = 0;` eux-mêmes, et surtout
  **F-WAGO-8 ci-dessous, que cette garde ne ferme pas**.

- ⚠️ **[F-WAGO-8] — NON CORRIGÉ, PRÉEXISTANT, hors périmètre de T3.30, trouvé en refermant F-WAGO-7 :
  `libmbus` recopie la réponse d'après la RÉPONSE, jamais d'après ce que l'appelant a demandé.**
  `mbus_cmd.c`, `mbus_cmd_read_coil_status()` : `mbus_ubyte byte_count = MBUS_BYTE_RD(bufptr)` puis
  `while (byte_count--) MBUS_BYTE_WR(coils_data, *bufptr++)`. `byte_count` sort du **champ
  byte-count de la trame reçue**, il est plafonné par la seule largeur d'un `unsigned char` —
  **255** — et il n'est confronté **ni à `coils_num`, ni à la taille du tampon `coils_data`**.
  ⭐ **La garde `nb > 0` de T3.30 ne ferme PAS ce défaut-là et il ne faut pas le croire fermé** :
  le battement de cœur modbus appelle `read_bits(0, 1, …)` toutes les dix secondes, `nb == 1`
  alloue **un octet**, et une réponse annonçant 255 déborde ce tampon de **254 octets**. Même forme
  côté registres (`mbus_cmd_read_holding_registers()`, `data_count = MBUS_BYTE_RD(bufptr) / 2`,
  jusqu'à **127 mots**). En amont, `mbus_rqst()` lit le corps de la réponse avec
  `mbus_sock_read(mbus->sd, mbus->buf + MBUS_HDR_LEN, MBUS_HDR(mbus->buf, MBUS_LENGTH_L), …)` : la
  longueur vient elle aussi de la trame, jusqu'à **255**, dans un `mbus->buf` de
  `MBUS_HDR_LEN + MBUS_DATA_LEN` = **260** octets dont **254** disponibles après l'en-tête — donc
  **un dépassement d'un octet de `mbus_struct` avant même la recopie**. ⚠️ **Portée réelle** : il
  faut un pair modbus malveillant ou en panne sur le socket de l'automate, ce qui n'est pas rien
  mais n'est pas le cas nominal ; **rien n'a tourné sous ASan ni contre un automate réel**, tout
  est établi au source. `libmbus` est un tiers importé (`$Id: mbus_conf.h,v 1.1.1.1 2003/…`) :
  le corriger, c'est **borner `byte_count` par la taille demandée et passer cette taille en
  paramètre**, donc toucher sa signature — **⭐ TICKET DÉDIÉ RECOMMANDÉ**, à ne pas glisser dans un
  ticket Wago applicatif.

- ⚠️ **[F-WAGO-3] — NON CORRIGÉ (durcissement DÉCLARÉ, pas un report) : `string v =
  json_string_value(value)` était un déréférencement de `NULL`.**
  `WagoMap.cpp:215` / `:240` et `WagoExternProc_main.cpp:157` / `:262` construisaient un
  `std::string` directement depuis `json_string_value()`, qui rend **`NULL`** sur tout ce qui n'est
  pas une chaîne JSON. Un émetteur qui aurait un jour typé les valeurs en nombres ou en booléens
  réels **plantait le récepteur**, dans son propre processus. On ne caractérise pas un comportement
  indéfini : le décodeur migré (`WagoWire::decodeMessage`) aplatit ces entrées par les règles de la
  maison, la couture jansson du commit de caractérisation faisait la même garde, et le cas
  `NonStringEntriesInValuesDoNotCrashTheDecoder` l'épingle. **Le changement est donc réel et
  déclaré**, mais il ne peut convertir qu'un plantage en valeur définie.

- ⚠️ **[F-WAGO-4] — ✅ TICKETÉ (transverse) : [T3.31](T3.31.md)** (2026-08-25).
  ⚠️ **Correction d'une correction** : une première rédaction de T3.31 affirmait que `LmsHost{}`,
  `LightState` et `RedChannel` « n'existent nulle part dans l'arbre ». **Le balayage était juste,
  la conclusion fausse : il portait sur `master`.** Ces types existent bel et bien, sur **deux
  branches livrées et revues mais NON MERGÉES à la date du balayage** — `LmsHost`
  (`Audio/SqueezeboxWire.h:188-191`) et `LightState` (`IO/Hue/HueWire.h:107-114`) sur
  **`refactor/e4.1d`** (`689e26b0`), `RedChannel`/`GreenChannel`/`BlueChannel`/`DmxChannel`/
  `DmxLevel`/`DimmerPercent` (`IO/OLA/OLAWire.h:91-124`) sur **`refactor/e4.1f`** (`4e238f2c`).
  **Sur master, le seul précédent est `enum class RuleDetachPolicy` (T3.18).**
  ⇒ **le compte de récidives de la série est CINQ, pas trois** : trois **ouvertes sur master**
  (E4.1g, E4.1h, E4.1i) et deux **fermées sur branches en attente** (E4.1d, E4.1f).
  ⭐ **Leçon d'outillage** : *toujours dire sur quelle référence on a mesuré* — un balayage sur
  `master` ne voit pas les branches livrées et conclut à tort qu'une mitigation n'a jamais été
  appliquée.
  **Le trou des arguments positionnels, MESURÉ pour la troisième fois de la série.**
  Échanger `address` et `nb` **au site d'appel** de `WagoWire::buildReadWordsRequest()` dans
  `WagoMap.cpp` : **compile sans un seul avertissement** (`int` et `UWord` sont implicitement
  convertibles) et laisse la suite **31/31 VERTE**. Structurel et lisible dans le `Makefile`
  généré : `WagoWire_test_LDADD` ne contient que `libcalaos_common.la` et gtest, **aucun objet
  serveur** — la mutation n'est même pas compilée dans le binaire de test. **Les sites d'appel ne
  sont pas mal couverts, ils sont absents du filet par construction.** Même constat qu'en E4.1i
  (F-REO-5) et en E4.1g.
  **Atténuations faites dans E4.1h**, et elles réduisent réellement la surface : le littéral
  d'action est **dans** chaque constructeur, et les deux constructeurs de réponse prennent le
  **`Params` décodé** au lieu de quatre chaînes — l'écho de `id`/`action`/`address`/`count` ne peut
  plus être permuté au site d'appel. **Reste** : `address` ↔ `count`, deux entiers.

  ⭐ **LA VOIE DE FERMETURE, tranchée par la revue et à recommander pour TOUTE la série** : ce trou
  ne se referme pas par un test, il se referme par le **typage**. `address` et `count` doivent être
  **deux types distincts** dans les signatures de `WagoWire.h` — `enum class` ou struct nommé — et
  la permutation devient une **erreur de compilation**, sans une ligne de test.
  ⚠️ **Exiger le lien serait une fausse piste** : l'infrastructure existe (`CORE_TEST_LDADD` lie
  déjà 25 objets serveur), mais le blocage n'est pas le lien — c'est qu'on **ne peut pas appeler**
  `WagoMap::write_*` sans construire le singleton, dont le constructeur **bind un socket UDP et
  lance `calaos_wago`**. La revue a rejoué M5 (**4 rouges**) et M8 (**31/31 vert, aucun
  avertissement imputable**) et conclut : **limite acceptable, ne pas exiger le lien**.

- ⚠️ **[F-WAGO-6] — 9ᵉ récidive du « fixture pauvre » dans la série, trouvée par la revue.**
  Dans la première version de `tests/WagoWire_test.cpp`, `FX_RCOUNT` valait **5**, c'est-à-dire
  exactement `replyWordValues().size()`. Conséquence mesurée (**mutation N1**) : remplacer l'**écho**
  `jroot["count"] = request["count"]` par `Utils::to_string(values.size())` dans `buildReadReply()`
  laissait la suite **VERTE 31/31** — un oracle mort sur le **seul** champ où la requête et la
  réponse doivent pouvoir diverger (`calaos_wago` réécho ce qu'on lui a **demandé**, il ne recompte
  pas ce que l'automate a **rendu**). Le fichier soignait pourtant ce point partout ailleurs.
  **Corrigé** : `FX_RCOUNT = 3` avec **5** valeurs, plus un `EXPECT_NE` explicite entre `count` et
  `values.size()`, et la réponse de lecture de bits porte `count = 9` pour **4** valeurs.
  **N1 rejoué : 3 cas rouges.** Conséquence fonctionnelle réelle faible (les cinq
  `WagoReadCallback` ignorent `count` et se gardent par `if (!values.empty())`) — mais un oracle
  mort reste un oracle mort, et c'est **encore** un relecteur qui l'a vu.

- 📏 **[F-WAGO-5] — `--with-wago` n'existe pas.** Le point 6 de `E4.1h.md` propose de construire
  « avec `--with-wago` si l'option existe ». **Elle n'existe pas** : `calaos_wago` est un
  `bin_PROGRAMS +=` **inconditionnel** (`src/bin/calaos_server/Makefile.am:373`), contrairement à
  `calaos_ola` (`if HAVE_LIBOLA`) et `calaos_knx` (`if HAVE_LIBKNX`). Le build par défaut le
  construit toujours.

## E4.1i — Reolink (2026-08-25)

- ✅ **[F-REO-1] — CORRIGÉ dans E4.1i** (commit `bcd3c403`, branche `refactor/e4.1i`).
  **`ReolinkCtrl::doRegisterCamera()` faisait un `json_decref()` sur un `json_t` déjà libéré, à
  CHAQUE enregistrement de caméra** — use-after-free en lecture **et** en écriture.

  Mécanisme, vérifié au source : `jansson_to_string()` (`src/lib/Jansson_Addition.h:150-165`)
  **vole la référence** — ses **deux** chemins de sortie appellent `json_decref(jroot)`.
  `tests/ParamsJson_test.cpp:213` le dit noir sur blanc (« *steals the reference, no decref here* »).
  `jroot` naissait à **refcount 1** (`json_object()`), et `json_object_set_new()` vole les
  références des **valeurs** sans toucher celle de l'objet : le decref de `jansson_to_string()` le
  ramenait donc à 0 et `json_delete()` libérait le bloc. Le `json_decref(jroot)` qui suivait
  relisait `jroot->refcount` **dans le bloc libéré** (offset 8, soit la `key` du tcache glibc) puis
  la décrémentait. Silencieux en pratique (la valeur relue n'atteint pas 0, donc pas de second
  `json_delete`), **erreur dure sous ASan**, et corruption du canari de double-free du tcache.

  ⚠️ **Accord de ton avec `RELEASE_NOTES.md`** (relevé en revue) : « silencieux **en pratique** »
  décrit le **mode le plus probable** — l'écriture retombe sur un bloc que l'allocateur garde dans
  son tcache, où elle n'abîme souvent que le canari. Elle n'est **pas bénigne** pour autant : dès
  que le bloc a été **repris par un autre objet vivant**, l'écriture corrompt les données de cet
  objet, et le symptôme sort **ailleurs et plus tard**. Les deux textes décrivent le même défaut vu
  de deux endroits : ici la mécanique, là le symptôme que l'utilisateur observe.
  ⛔ **Aucune des deux n'est vérifiée à l'exécution : rien n'a été lancé sous ASan, et il n'y a eu
  aucun test bout-à-bout avec le vrai `calaos_reolink`.**

  **Portée** : un appel par `registerCamera()` d'une caméra pas encore enregistrée, **plus un par
  caméra à chaque (re)connexion du processus** via `registerAllCameras()` — donc à chaque
  redémarrage de `calaos_reolink`, qui se relance en boucle quand il sort.

  **Non épinglé par un test, et c'est assumé** : `doRegisterCamera()` est une méthode **privée**
  d'un singleton dont le constructeur **lance un processus externe**, donc inatteignable depuis un
  test ; et un UAF silencieux n'est pas observable par une assertion hors ASan. Le commit de
  bascule qui suit supprime l'émetteur jansson en entier, ce qui retire le motif de ce fichier.
  **Entrée `RELEASE_NOTES.md` écrite** (le symptôme — plantage, comportement erratique — est ce que
  l'utilisateur observe).

- 📏 **[F-REO-1b] Le balayage complet de `jansson_to_string`, recompté — le chiffre qui circulait
  était faux.** Fait en `python3` avec un analyseur **conscient de la portée des fonctions** (le
  hook `rtk` réécrit `grep` et rend des faux négatifs ; une fenêtre de N lignes, elle, rend des
  faux positifs en traversant les frontières de fonctions).

  Sur `master` `138c16ee` :

  | Mesure | Valeur |
  |---|---|
  | sites `jansson_to_string(...)` dans `src/` | **29** |
  | dont l'argument est une **variable nommée** (seuls candidats au double decref) | **18** |
  | dont l'argument est un temporaire `jansson_from_params(p)` (jamais décrémentable) | **11** |
  | **qui doublent réellement le decref** | **2** |

  Les deux : `IO/Reolink/ReolinkCtrl.cpp:151` (**celui-ci, corrigé**) et
  `IO/Mqtt/MqttCtrl.cpp:115` (**périmètre d'E4.1g**, corrigé par son ticket, **pas encore sur
  `master`**). **Les 16 autres sites à variable nommée sont corrects** :
  `JsonApiHandlerWS.cpp:78` · `Wago/WagoExternProc_main.cpp` ×6 · `Mqtt/MqttCtrl.cpp:41` ·
  `KNX/KNXCtrl.cpp:291` · `KNX/KNXExternProc_main.cpp:269` · `OLA/OLACtrl.cpp:55,:82` ·
  `LuaScript/ScriptExec.cpp:167,:183` · `LuaScript/ScriptBindings.cpp:435,:494`.
  ⇒ **rien à ouvrir hors périmètre**, mais l'affirmation « *j'ai relu les 38 autres appels du
  dépôt* » qui circulait était fausse sur les deux termes (38, et « aucun »).

- ⚠️ **[F-REO-2] `Params::fromNJson()` n'est PAS un substitut de `jansson_decode_object()`, et le
  substituer tuerait le driver à CHAQUE événement de caméra.** Mesuré, pas supposé — **piège commun
  à tous les sous-tickets d'E4.1 qui décodent un wire.**

  `Params::fromNJson()` fait `p.params[it.key()] = it.value();` — conversion implicite `Json` →
  `std::string` qui **lève `type_error.302` pour tout ce qui n'est pas une chaîne JSON**.

  ⚠️ **Chiffre corrigé après revue (R3)** : l'événement `detection` **réel** du driver
  (`ExternProcReolink_main.py:1292-1303`, qui est la fixture du test) porte **10 clés dont 5
  non-chaînes** — `channel` (int), `tcp_push_active` (bool), `callback_duration` (float),
  `async_callback` (bool), `adaptive_timeout` (int) — donc **5 levées**. Le « 9 levées sur 11
  clés » publié d'abord décrivait une charge **composite** de ma sonde de mesure (elle ajoutait un
  tableau et un `null`), **pas un message que le driver émet**. Le fond ne bouge pas : **une seule
  levée suffit**, et il y en a cinq à chaque détection.

  Or **tout** événement de `ExternProcReolink_main.py` porte `channel` (int),
  `tcp_push_active` (bool) et `callback_duration` (float) (`:1292-1303`), et la réponse
  `health_check` porte **deux objets imbriqués** (`circuit_breakers`, `memory_optimization`,
  `:1653-1674`). Le décodage a lieu dans un callback `ExternProcServer::messageReceived`, **sans
  aucun `try`/`catch` sur le chemin** → `std::terminate`.

  E4.1i garde donc **le contrat d'aujourd'hui**, écrit à la main dans
  `IO/Reolink/ReolinkWire.h::decodeMessage()` (chaîne telle quelle · booléen → mot `true`/`false` ·
  nombre → `Utils::to_string(double)` · **tout autre type → chaîne vide, clé quand même ajoutée**),
  et le test porte un **tripwire nommé** qui rougit si `fromNJson()` cessait de lever.

- 📏 **[F-REO-3] `E4.1i.md` et `E4.1.md` annonçaient « 9 appels » pour `ReolinkCtrl.cpp` : c'est
  16.** Recompté en `python3` sur `master` `138c16ee` : **14 appels `json_*`** (`json_loads` 1,
  `json_decref` 2, `json_object` 1, `json_object_set_new` 5, `json_string` 5) **+ 2 appels
  `jansson_*`**. Le classement des fichiers ne change pas. Corrigé dans `E4.1i.md`.

- 📏 **[F-REO-3b] La dette `jansson_from_params` — valeur de référence : `94` jetons = `90` sites
  d'appel sur `master` `3f0cc074`.** ⚠️ **Corrigé après revue (R1)** : j'avais publié « 98 sites »,
  en n'ayant vu **qu'une** des trois mentions en prose et en l'ayant mal située. **C'est 96 sites
  sur la base `138c16ee`, 90 sur `master`.**

  Recompté avec un classificateur qui distingue **définition / commentaire / appel** (`python3`,
  hors du hook) :

  | Rev | Jetons | **Sites d'appel** | Définition | Prose |
  |---|---|---|---|---|
  | `138c16ee` (base d'E4.1i) | **100** | **96** (93 `src/` + 3 `tests/`) | 1 | 3 |
  | `3f0cc074` (`master` actuel) | **94** | **90** (87 `src/` + 3 `tests/`) | 1 | 3 |

  Les **trois** mentions en prose : `src/lib/Jansson_Addition.h:39`, `tests/Makefile.am:1996`,
  `tests/ParamsJson_test.cpp:59`. Les deux dernières écrivent `jansson_from_params()`
  **parenthèses comprises, dans un commentaire** — c'est le piège : un comptage « jeton suivi de
  `(` » les compte comme des appels, et c'est exactement l'erreur que j'ai faite. La définition est
  `Jansson_Addition.h:52`.

  ⇒ **`grep -rn` (le jeton) et « sites d'appel » diffèrent de 4, systématiquement.** Quatre valeurs
  ont circulé (104, 100, 98, 96) : **utiliser `94 jetons = 90 sites` sur `3f0cc074`**, et **dire
  laquelle des deux on cite**. **E4.1i n'en résorbe aucune** : `ReolinkCtrl.cpp` n'utilisait pas
  `jansson_from_params`.

- ⚠️ **[F-REO-5] — ✅ TICKETÉ (transverse) : [T3.31](T3.31.md)** (2026-08-25), cible n°1.
  ⚠️ Le struct `CameraRegistration` existe bien (`ReolinkEventRegistry.h:52-58`) **mais n'a rien
  fermé** : `ReolinkCtrl.cpp:100` le construit en **brace-init positionnel**, donc la permutation
  reste écrivable. ⭐ **Et ce n'est pas un accident, c'est un motif** : le relecteur d'E4.1d a
  trouvé le même défaut sur `HueWire::LightState` (agrégat de 3 `int` + 2 `bool` ⇒
  `LightState{100, 200, 30000, true, true}` compile, mesuré `-fsyntax-only`, et **permute
  `sat`/`bri` en silence**) — latent là-bas, **réalisé** ici. ⇒ **règle de série : un type nommé
  ne ferme rien s'il reste un agrégat initialisable positionnellement.** La forme qui ferme est
  celle d'`OLAWire.h:91-124` — **constructeur `explicit`**, donc pas d'agrégat ; les désignateurs
  C++20 rendent la permutation *visible*, pas *impossible*. **Quatre `string` positionnelles de même type traversent trois relais que rien ne
  couvre — MESURÉ, plus supposé.** Le filet d'E4.1i tient `ReolinkWire::buildRegisterMessage()` ;
  il ne tient **pas** ce qu'on lui passe.

  **Mesure**, même protocole que les six contre-mutations (`.o` du test + binaire + `ReolinkCtrl.o`
  + `calaos_server` effacés, ligne `CXXLD ReolinkWire_test` exigée) : permuter `username` ↔
  `password` **au site d'appel**, dans `ReolinkCtrl.cpp`, laisse la suite **verte 17/17**. La
  **même** permutation **à l'intérieur** de l'en-tête extrait rougit **5 cas** (mutation M6). La
  frontière du filet est donc exactement l'entrée de l'en-tête.

  **Trois relais en amont**, tous en quatre `string` positionnelles dans le même ordre, aucun
  couvert : `ReolinkInputSwitch.cpp:83-86` (quatre `get_param()`) → `:91 registerCamera(...)` →
  `ReolinkCtrl::registerCamera()` → `doRegisterCamera(...)`, plus le brace-init positionnel
  `registry.add({hostname, username, password, event_type}, ...)`. Une permutation à n'importe
  lequel de ces points produit un message **parfaitement bien formé** et une caméra qui ne
  s'authentifie pas.

  **Limite jugée acceptable pour un ticket de bascule JSON** (couvrir demanderait d'instancier un
  singleton qui lance un processus externe), **mais la mitigation est évidente et bon marché** :
  un **struct nommé** au lieu de quatre `string` — `ReolinkEventRegistry::CameraRegistration`
  existe déjà, porte exactement ces quatre champs, et rendrait la permutation **impossible à
  écrire** sur trois des quatre sauts. **Candidat à un ticket dédié**, hors périmètre d'E4.1i.

- ℹ️ **[F-REO-6] Le delta « paire supprimée → U+FFFD » change le MODE d'échec, pas seulement le
  contenu du message.** Vérifié au source du bout python (`ExternProcReolink_main.py:1626-1627`).

  **Avant** : sans clé `password`, `if not username or not password:` → journal
  « *No username or password provided* », `return`. **Aucune connexion n'est planifiée.**
  **Après** : le mot de passe existe et vaut `p\ufffd…`, donc la valeur est vraie ; le driver
  **planifie réellement** `connect_camera()`, la caméra **refuse l'authentification**, et le
  `CircuitBreaker` et ses **retries** entrent en jeu.

  Les deux échouent, mais **les journaux et le profil réseau diffèrent**. Le cas d'entrée reste
  tordu (un octet UTF-8 invalide dans un mot de passe d'`io.xml`) et le résultat observable pour
  l'utilisateur — la caméra ne marche pas — est identique : **pas d'entrée `RELEASE_NOTES` pour la
  bascule**, choix assumé et maintenu. Consigné pour que personne n'ait à le redécouvrir.

- ℹ️ **[F-REO-4] Le message `register` transporte le mot de passe de la caméra EN CLAIR** — connu
  (`E4.1i.md` le signale), **hors périmètre, non touché**. Deux garde-fous posés sans changer de
  comportement : un commentaire en tête de `ReolinkWire::buildRegisterMessage()` et un au site
  d'émission. Le diff d'E4.1i n'ajoute **aucun** `cDebug`/`cInfo` portant ce message ; côté
  réception, la trace d'erreur de parsing ne journalise **que le nombre d'octets** — comme le fait
  déjà le bout python (`:1601-1602`, « *Never log the raw message: it may carry credentials* »).

## Sécurité / correctness à traiter en priorité

- ✅ **[F-DEP-1] — TRAITÉ par [T3.23](T3.23.md)** (branche `fix/t3.23`, non mergée à l'écriture).
  Le `pyproject.toml` est devenu la **source unique** : les deux stages du `Dockerfile`, le
  `.devcontainer/Dockerfile` et un job CI neuf (`mcp-sidecar-deps`) installent ce qu'il déclare
  via `scripts/pyproject-requirements.py`, en **une seule passe de résolveur**. `configure.ac`
  sonde désormais l'**API réellement importée** (+`starlette`, `pydantic`, **`websockets`**, jamais
  sondé jusque-là) au lieu de `import mcp, uvicorn, fastapi`. **Image reconstruite et sidecar
  démarré dedans** : `mcp 1.16.0` installé, `/healthz` 200, `POST /mcp` 401 sans Bearer / 200 avec.
  ✅ **Le `.deb` est couvert, c'est vérifié et non plus supposé** : `calaos/pkgdebs` a été lu en
  revue. `build_deb.yml` → un `Makefile` dont tout le `build` écrit **deux lignes** dans
  `container.source` ; le paquet n'embarque **ni l'image ni aucun `site-packages`**, c'est un
  wrapper podman (`podman run --pull=never ${IMAGE_SRC}`, image tirée au `postinst`). Corriger le
  `Dockerfile` suffit. Énoncé d'origine conservé ci-dessous.

  ---

  🔴 **[F-DEP-1] Le `Dockerfile` déployé installe les dépendances du sidecar MCP **non pinnées**,
  et la résolution du jour **casse le sidecar**.** Trouvé pendant le passage Dependabot du
  2026-08-24, hors périmètre (aucune PR Dependabot ne touche le `Dockerfile`).

  `Dockerfile:38` et `Dockerfile:69` (et `.devcontainer/Dockerfile:24`) font :
  ```
  RUN pip install "mcp[cli]" uvicorn fastapi websockets --break-system-packages
  ```
  Sans aucune borne de version. Or `src/bin/calaos_mcp/pyproject.toml` porte en tête le
  commentaire T1.8 « *All runtime dependencies pinned (the sidecar is security-sensitive […] no
  floating versions)* » — **ce pyproject n'est utilisé par aucun chemin de build**. Il n'est même
  pas dans l'`EXTRA_DIST` de `src/bin/calaos_mcp/Makefile.am`, qui installe les `.py` directement
  et laisse les dépendances au Python du système. Les pins sont donc de la **documentation**, pas
  un contrat.

  **Ce n'est pas théorique. Mesuré le 2026-08-24**, dans le conteneur de build (python 3.11.2),
  cette commande résout aujourd'hui vers **`mcp 2.0.0`**, où le module `mcp.server.fastmcp` a
  **disparu** :
  ```
  mcp 2.0.0
    FAIL mcp.server.fastmcp   ModuleNotFoundError: No module named 'mcp.server.fastmcp'
    OK   mcp.server.transport_security
  ```
  `src/bin/calaos_mcp/python/calaos_mcp/server.py:25` fait `from mcp.server.fastmcp import FastMCP`.
  **Toute reconstruction de l'image publie donc un sidecar MCP qui ne démarre pas** — l'import
  échoue avant même `create_app()`. `configure.ac:220` teste `import mcp, uvicorn, fastapi`, ce
  qui **passe** avec mcp 2.0.0 (le paquet existe, c'est le sous-module qui manque) : la détection
  `HAVE_PYTHON_MCP` ne rattrape pas la casse.

  **Correctif** : pinner le `Dockerfile` sur le jeu déclaré dans `pyproject.toml` — idéalement
  `pip install -r` / `pip install ./src/bin/calaos_mcp` pour que la déclaration devienne la source
  de vérité et que Dependabot surveille enfin ce qui est réellement déployé. Voir aussi
  [T3.22](T3.22.md), dont c'est le prérequis de fond.
  → **Fait par T3.23**, par génération d'un requirements depuis le `pyproject.toml` :
  `pip install ./src/bin/calaos_mcp` a été **écarté** car il installerait le paquet `calaos_mcp`
  dans `site-packages` en **doublon** de `/opt/lib/calaos/calaos_mcp`.

- 🟠 **[F-DEP-3] Rien n'empêche la divergence de revenir : un `RUN pip install foo` ajouté en dur
  au `Dockerfile` passerait toute la CI au vert.** T3.23 a fait du `pyproject.toml` la source
  unique, mais n'a posé **aucun garde-fou contre le contournement**. Le dépôt a pourtant déjà le
  patron adéquat — **`tests/check-config-docs.sh`**, câblé dans `make check` — et l'analogue
  manque. **Ticket proposé : `tests/check-pydeps-single-source.sh`**, qui refuse tout
  `pip install` du `Dockerfile`, du `.devcontainer/Dockerfile` et des workflows qui ne passe pas
  par `scripts/pyproject-requirements.py` (liste blanche explicite pour `roonapi`/`reolink-aio`).

- 🟡 **[F-DEP-4] Deux angles morts de `scripts/pyproject-requirements.py`.** (1) Il ne lit que
  `[project].dependencies` et **ignore `[project.optional-dependencies]`** : une dépendance rangée
  sous un extra serait **silencieusement perdue** de l'image. (2) Il **n'exige aucun `==`** : un
  futur `mcp>=1.0` dans le manifeste **re-flotterait sans bruit**, ce qui est exactement le défaut
  que T3.23 corrige. Les extras et les marqueurs PEP 508 passent en revanche **verbatim** à
  `pip -r`, donc corrects. À traiter avec F-DEP-3 (même script de garde).

- 🟡 **[F-DEP-5] `calaos_mcp` a deux numéros de version qui se contredisent, et c'est le mauvais
  qui est exposé.** `python/calaos_mcp/__init__.py` déclare `__version__ = "0.1.0"` alors que
  `src/bin/calaos_mcp/pyproject.toml` déclare `version = "1.0.0"`. **`GET /healthz` renvoie
  `{"status":"ok","version":"0.1.0"}`** — donc la version qu'un intégrateur lit sur le réseau n'est
  pas celle du paquet. À trancher (probablement : `__version__` lu depuis les métadonnées, ou une
  source unique comme pour les dépendances).

- ⚪ **[F-DEP-6] `pip show … | grep` masque un code retour** (`.github/workflows/ci.yml`, étape
  « Report installed versions » du job `mcp-sidecar-deps`) : le statut du pipe est celui de `grep`.
  L'étape est purement **informative**, donc sans conséquence aujourd'hui — mais à savoir avant de
  s'appuyer dessus.

- ⚪ **[F-DEP-7] `calaos_mcp/models.py` est du code mort.** Il définit des modèles pydantic et
  **n'est importé par aucun module** (ni par `calaos_mcp/*`, ni par `tests/python/*`). Trouvé en
  alignant la sonde `configure` de T3.23 sur les imports réels : c'est la seule raison pour
  laquelle `pydantic` figurait dans la liste des imports du sidecar. À supprimer, ou à câbler.

- 🟠 **[F-DEP-2] Les suites Python n'exercent jamais `calaos_mcp/server.py`, et le conteneur de
  build n'a ni `mcp` ni `pytest` — donc `make check` ne couvre ni l'un ni l'autre.** Même origine.
  **Partiellement entamé par [T3.23](T3.23.md)** : le job CI `mcp-sidecar-deps` installe le jeu du
  `pyproject.toml` et exerce l'API que `server.py` importe, **y compris** la privée
  `FastMCP.streamable_http_app()` / `_session_manager` (`server.py:167-169`). **Le reste tient** :
  ce job ne construit pas `create_app()` et ne frappe pas `/healthz` — la suite
  `tests/python/test_mcp_server.py` ci-dessous est toujours à écrire, et `make check` ne couvre
  toujours rien de Python dans le conteneur de build.

  `tests/run-python-tests.sh` est bien câblé dans `make check` (T2.14), mais dans le conteneur de
  build il n'y a ni `pytest`, ni `fastapi`, ni `starlette`, ni `mcp` : le script retombe sur
  `python3 -m unittest discover -p 'test_t116_*.py'`, et les trois suites pytest
  (`test_auth.py`, `test_extern_proc.py`, `test_logger.py`) sont **sautées**. Même en installant
  pytest, aucune des six suites n'importe `calaos_mcp.server` — elles couvrent `auth`, `client`,
  `config`, `tools.io`, et le logger. Le module qui construit l'application (montage MCP,
  extraction du `StreamableHTTPASGIApp` depuis `mcp._session_manager` — un accès à une API
  **privée** de `mcp`, cf. `server.py:167-169`) n'est **testé nulle part**.

  `.github/workflows/ci.yml` n'a par ailleurs **aucune** étape Python : le ✅ vert des PR
  Dependabot pip ne porte aucune information sur le sidecar (c'est ainsi que la PR #175,
  irrésoluble, est passée verte).

  **Correctif** : une suite `tests/python/test_mcp_server.py` qui construit `create_app()` et
  frappe `/healthz` + `POST /mcp` (initialize) via `TestClient`, plus une étape Python dans
  `ci.yml` installant le jeu de `pyproject.toml`. C'est cette vérification-là, faite à la main
  hors bande le 2026-08-24, qui a permis de valider la montée coordonnée
  mcp/starlette/fastapi — elle devrait être automatique.



## Résolus

- ✅ **[CORRECTNESS] Déréférencement `createIO()` sans garde nullptr** — **traité par T2.18**
  (`62739380`, « null-guard audit of createIO/CreateIO call sites »). Revérifié au source
  (revue E4.5a/b, 2026-08-24) : l'arbre ne porte plus que **deux** appelants de
  `ListeRoom::createIO()`, tous deux gardés — `JsonApi.cpp:1926-1936` (null ou
  `dynamic_cast<Scenario*>` qui échoue → erreur loggée + réponse d'erreur, et l'IO créé est
  détruit) et `AutoScenario.cpp:412-424` (null propagé à l'appelant, qui abandonne). Côté
  fabrique, `IOFactory::CreateIO()` n'a lui aussi que deux appelants,
  `ListeRoom::createIO()` et `Room::LoadFromXml()`, qui parquent le résultat dans un
  `unique_ptr` avant tout déréférencement.

- ✅ **[SÉCURITÉ] Le throttle de login n'identifiait pas le client derrière haproxy** —
  **traité par [T3.24](T3.24.md)**. `JsonApi::clientIp()` rendait le **pair TCP** sur **les deux**
  transports (`JsonApiHandlerWS.cpp:45-51` **et** `JsonApiHandlerHttp.cpp:55-61` — le constat
  initial ne citait que WS), donc l'adresse du proxy : `LoginThrottle` n'avait **qu'un seau pour
  toute l'installation**. Un attaquant qui épuisait la fenêtre **verrouillait le login de tous les
  utilisateurs légitimes**, et sa propre limite était effacée par le premier `registerSuccess()`
  de n'importe qui. Le helper `TransportLimits::effectiveClientIp()` **existait déjà** et était
  **déjà** utilisé dix lignes plus loin par `max_connections_per_ip` (`HttpClient.cpp:200`) : un
  appelant sur deux ne s'en servait pas. Les deux `clientIp()` passent désormais par
  `HttpClient::getEffectiveClientIp()`, qui l'enveloppe. Sept cas dans
  `tests/core/JsonApiThrottleIdentity_test.cpp` + trois cas au vrai parseur llhttp dans
  `tests/TransportHardening_test.cpp`, aucun golden touché. ⚠️ **Contrepartie assumée, à lire
  avec** : **F-XFF-1** ci-dessous — le correctif **expose** le throttle à `X-Forwarded-For`, ce
  qui n'était le cas dans **aucun** déploiement auparavant.

- ⚠️ **F-XFF-1 — [SÉCURITÉ, OUVERT] `X-Forwarded-For` est cru sans qu'aucun proxy de confiance
  soit vérifié** — ouvert par [T3.24](T3.24.md), **non corrigé**.

  **Le mécanisme, mesuré au vrai parseur llhttp** (`tests/TransportHardening_test.cpp`,
  `ForwardedForLine.*`, portés depuis la revue T3.24) : `effectiveClientIp()` prend la **dernière**
  entrée de la **dernière ligne** `X-Forwarded-For`, et une ligne répétée **écrase** la précédente
  (`request_headers` est une `map`, `HttpClient.h:265`). **Derrière haproxy c'est sain** : la
  production épingle `calaos-os-conf` (commit `33f794eb` via `pkgbuilds/calaos-os-conf/PKGBUILD`),
  dont `conf/haproxy-calaos.cfg` porte **`option forwardfor` SANS `if-none`** — haproxy ajoute
  **toujours** sa ligne, en queue, et la liste forgée par le client est **jetée en bloc**.

  **Le défaut** : `calaos_server` **ne vérifie jamais que son pair TCP est haproxy** — aucun
  `trusted_proxy`, aucune comparaison de `getClientIp()` à une adresse attendue. Et ce n'est pas un
  cas d'école : **`HttpServer.cpp:29-31` bind `listen_address` = `0.0.0.0` par défaut**
  (`docs/16_config_options.md`) alors qu'haproxy ne vise que `127.0.0.1:5454` — **le port 5454
  répond donc directement depuis le LAN sur un déploiement standard**. Un client qui le joint en
  direct **choisit son identité**, donc son seau de throttle **et** son compteur de connexions.
  ⚠️ **Et il DOIT rester joignable** : le parc RemoteUI et les apps mobiles en LAN s'y connectent
  en direct (voir la CORRECTION en fin d'entrée). Le défaut n'est donc **pas** que le port soit
  ouvert — c'est qu'on **croie l'en-tête sans savoir d'où vient la connexion**.

  ⚠️ **T3.24 CRÉE cette exposition pour le throttle, il ne la subit pas.** Une première rédaction
  de cette entrée affirmait le contraire (« l'attaquant pouvait déjà verrouiller les autres ») :
  **c'est faux**, corrigé en revue. Avant T3.24 les deux `clientIp()` rendaient le **pair TCP**,
  donc `X-Forwarded-For` n'avait **aucun effet** sur `LoginThrottle`, dans **aucun** déploiement.
  En exposition directe, T3.24 donne à l'attaquant **deux capacités qu'il n'avait pas** :
  (a) **s'exonérer** du backoff, donc brute-forcer un mot de passe **sans limite**, et
  (b) **throttler une victime ciblée** en forgeant son adresse.

  **L'échange est assumé, pas gratuit** : ce qu'il retire est un **DoS de lockout non authentifié**
  atteignable depuis le WAN et frappant **tous** les utilisateurs ; ce qu'il ajoute demande un
  **accès LAN** ; et l'arbre fait **déjà** confiance à cet en-tête pour `max_connections_per_ip`
  depuis son merge. La garantie invoquée reste une **garantie de déploiement** (`DECISIONS.md`),
  **pas une garantie de code**.

  ### ⛔ CORRECTION (2026-08-25) — la mitigation retenue n'est PAS `listen_address`

  ~~**Le vrai correctif, et il est gratuit — suite à ouvrir hors de ce dépôt (calaos-os)** : poser
  **`listen_address = 127.0.0.1`** dans la configuration de calaos-os. L'option **existe déjà** et
  est documentée ; seul haproxy pourrait alors joindre le port, ce qui rend la confiance en
  `X-Forwarded-For` **saine** au lieu d'hypothétique.~~

  ⛔ **`listen_address` est ÉCARTÉ** — dans le code **comme** dans la configuration de calaos-os.
  Décision utilisateur du 2026-08-25 (`DECISIONS.md`, « `X-Forwarded-For` : la confiance se
  conditionne au **pair TCP** »). **Deux raisons, vérifiées au source, et chacune suffit** :

  1. ⭐ **La clé gouverne DEUX serveurs, pas un.** `HttpServer.cpp:29-31` **et
     `UDPServer.cpp:58-61`** lisent la **même** option avec le **même** repli `"0.0.0.0"`. Le
     serveur UDP est **la découverte** : il répond `CALAOS_IP <ip>` à `CALAOS_DISCOVER`
     (`UDPServer.cpp:67-84`, adresse rendue par `TCPSocket::GetLocalIPFor(remoteIp)`) et reçoit
     **aussi** les trames `WAGO INT` / `WAGO KNX` des automates (`:88-118`) — **un chemin d'IO
     vivant**. Le confiner au loopback casse la découverte **et** les entrées Wago.
  2. ⭐ **Les clients qui joignent le 5454 en direct sont des clients LÉGITIMES**, vérifié dans les
     dépôts voisins : le firmware **RemoteUI** (`calaos_remote_ui/main/calaos_protocol.h:28`
     `WS_PORT = 5454` ; `provisioning_requester.cpp:161` `http://<ip>:5454/api/v3/provision/request`
     — découverte UDP **puis** connexion directe, en clair, sans proxy), l'**application mobile en
     LAN** (`calaos_mobile/src/CalaosConnection.cpp:307-308` : hôte nu ⇒ `ws://<h>:5454/api`), et
     l'**auto-détection de `calaos_installer`** (`dialogautodetect.cpp:74`, broadcast UDP). Seul le
     **sidecar MCP** est indifférent (`calaos_mcp/config.py:41`, `ws://127.0.0.1:5454/api`).

  ⇒ **le port 5454 ouvert n'est pas un défaut : c'est le produit.**

  **La mitigation retenue — [`T3.39`](T3.39.md)** : lire `X-Forwarded-For` **uniquement si le pair
  TCP est le loopback** (`127.0.0.1` / `::1`), c'est-à-dire haproxy ; sinon prendre l'adresse du
  pair et **ignorer l'en-tête**. C'est la « liste de proxys de confiance » dont l'analyse ci-dessus
  constatait l'absence dans tout l'arbre — **réduite à une seule entrée**. Elle ferme
  **entièrement** les capacités (a) et (b) créées par T3.24 en accès direct, **sans rien casser** :
  RemoteUI et les apps LAN restent identifiés par leur **vraie** adresse, ce qui est exactement ce
  qu'un throttle veut.
  ⚠️ **Reste ouvert, accepté** : un attaquant **sur la machine elle-même** est loopback, donc peut
  encore forger. Et un déploiement où **haproxy vit sur une autre machine** serait cassé par la
  garde — d'où l'option de configuration proposée en §3 de T3.39, **par défaut le loopback**.
  ⚠️ **Ne reproposez pas `listen_address` dans six mois** : il est documenté, il a l'air gratuit,
  et il ne l'est pas. La question a été instruite deux fois ; la deuxième a trouvé `UDPServer.cpp`.

  **Non vérifié, noté tel quel** : le comportement d'haproxy 2.8 en **HTTP/2** côté frontend
  (déduit de la doc et de la conversion h2→h1, non testé) ; le **request smuggling** à travers
  haproxy, seul vecteur résiduel permettant d'injecter une ligne **après** celle du proxy ; et la
  suite n'a pas été rejouée sous ASan.

- ✅ **[SÉCURITÉ, même classe que F2] `RemoteUIManager::getRemoteUIByToken` en `string ==`** —
  **traité par T2.15** (`01089187`, « … constant-time RemoteUI token lookup »). Revérifié au
  source : `RemoteUIManager.cpp:96` compare désormais via
  `JsonApi::secureCompare(io->get_param("auth_token"), token)`, avec le commentaire de
  contrainte. Plus aucun `==` sur le token dans ce chemin.

## exprtk ASan (tracké T2.8 — CORRIGÉ, analyse initiale invalidée)

- **stack-use-after-scope** : ce n'était **ni interne à exprtk, ni bénin** — c'était un vrai UB
  release dans **notre** code. `value_str` était déclaré dans un bloc interne de
  `evaluateExpressionBool`, lié **par référence** dans la `symbol_table` d'exprtk (qui stocke un
  pointeur brut — `exprtk.hpp:10164`), puis déréférencé **après la sortie du bloc** à
  `expr.value()`. Corrigé par hoisting de la variable au scope fonction (T2.8, mergé).
  Aucune quarantaine n'a été nécessaire ; ASan reste entièrement actif.

## Qualité / design

- **[FOOTGUN] `Params::operator[]` retourne par VALEUR** — `params["k"] = v` compile et **no-op
  silencieux** (assigne à un temporaire). T1.11 a corrigé l'instance vivante (battery
  `last_battery_notif_time`, throttle qui ne marchait jamais) via `Params::Add`. D'autres sites
  `params[...] = ...` peuvent cacher le même bug. → sweep `\]\s*=` sur instances Params.

- **[DESIGN] `IOBase::get_params()` retourne un `Params&` mutable** — contourne la garde
  d'immutabilité de `set_param("id")` : un appelant pourrait réécrire "id" sans passer par
  `renameId()` (pas de rehash de la map id→IO). Aucun appelant de ce type aujourd'hui
  (vérifié grep). → envisager const ref ou mutateur dédié.

## Série E4.2 — carry-overs (ownership ListeRoom)

- **[E4.2a revue → à traiter en E4.2b] `ListeRoom::createIO`, clause `!id.empty()`** : le
  commentaire affirme reproduire le comportement « exactement comme avant », ce qui est inexact.
  L'ancien code détruisait un second IO sans id comme doublon ; le nouveau le conserve attaché à
  la pièce mais absent de `io_table` — précisément l'état « à moitié ajouté » que le commentaire
  environnant interdit. Inatteignable en production (aucun appelant ne produit ce cas), mais il
  faut soit corriger le commentaire, soit supprimer la clause.
- **[E4.2a revue → contrainte pour E4.2b] `delete_io(io, del=false)` est un TRANSFERT de
  propriété**, pas une simple suppression du conteneur : quand les conteneurs passeront aux
  smart pointers, ce chemin doit devenir `release()` et non `reset()`, sinon l'IO rendu à
  l'appelant est détruit sous ses pieds.
- **[E4.2b revue → non corrigé] `JsonApi.cpp:1820`, transfert d'un IO frère entre pièces** :
  `old_room` n'est jamais testé contre `nullptr`. Avec `room != nullptr` mais un `getRoomByIO`
  qui échoue, ce chemin déréférence un pointeur nul. Pré-existant, hors périmètre E4.2b.
- **[E4.2b revue → non corrigé] Fuite de la génération de doc (pré-existante)** : sous
  `IOBase::ScopedDocGen`, `IOBase` saute `addIOHash`, mais `IPCam.cpp:43` et
  `AudioPlayer.cpp:54` appellent `addIOHash(this)` inconditionnellement. Les objets jetables
  de doc atterrissent donc dans `io_table["doc"]` / `cameraCache` et `~IOBase` ne les
  désenregistre jamais.

## Wave 7 — follow-ups mineurs (hors périmètre, non corrigés)

- **[T2.18 revue] `buildAutoscenarioModify`** : `deleteRules()` + `addStep()` s'exécutent
  AVANT le guard `checkScenarioRules()` — sur un scénario aux IOs internes null (config
  squattée qui survit désormais au boot grâce à T2.18), déref null avant le guard.
  Strictement une amélioration vs le crash au boot pré-fix, mais remonter le guard avant
  `deleteRules()` serait plus propre. Aussi : `createInput` marque encore un IO étranger
  squatté avec `setAutoScenario(true)`.
- **[T2.19 revue] `WebCtrl` singleton par URL** : deux IOs Web partageant une URL avec des
  params `insecure` différents → first-instance-wins (direction d'échec = reste insecure,
  conforme à la politique). Pré-existant au design singleton.
- **[T2.19 doc] `insecure="false"` est sensible à la casse** (`"False"` reste insecure —
  direction sûre) ; cohérent avec les bools existants de calaos, mais l'ioDoc pourrait le
  préciser.
- **[TÂCHE EXTERNE — calaos_installer]** : ajouter l'option écrivant `insecure="false"` pour
  les NOUVEAUX devices (décision 2026-08-15) ; suggestion : matérialiser l'implicite
  `insecure="true"` à la sauvegarde pour permettre un futur flip du défaut code.

## Wave 6 — follow-ups (hors périmètre, non corrigés)

- **[MÊME CLASSE que T3.7] `McpServerManager.cpp:166` et `JsonApiHandlerHttp.cpp:45`** :
  `ProcessHandle::kill()` gardé seulement par `referenced()` — même défaut pid-0 que T3.7
  (chemins shutdown/destructeur). Découvert par le grep de T3.7. → mini-ticket, même garde
  `pid > 0`.
- **[DANGEREUX — corrigé par T3.7] `ExternProc.cpp:100-104,120-121` — kill de groupe sur pid 0** : après un
  `uv_spawn` raté, `~ExternProcServer` fait `process_exe->kill(SIGTERM)` gardé seulement par
  `referenced()` ; pid resté à 0 → `kill(0, SIGTERM)` = SIGTERM à **tout le groupe de
  processus** (a tué le harness automake pendant les tests T3.2a). Même classe que le vieux
  bug ping. → ticket prioritaire : garde `pid > 0`.
- **[SÉCURITÉ] `UrlDownloader.cpp:432,463,512,586,709` loggent l'URL complète** (`m_url`) —
  les credentials caméra (`pwd=` Foscam, `passwd=`/`_sid=` Syno) fuient dans les logs malgré
  le masquage T3.3 côté IPCam ; `JsonApiHandlerHttp.cpp:950` idem (URLs vidéo). → ticket :
  déplacer/réutiliser `IPCam::maskUrlCredentials` au niveau UrlDownloader.
- **[LIFETIME] `AVRRose.cpp` pollTimer/postRequest + retry `Timer::singleShot(10.0,[this])`
  et `AVRRoseNotifServer.cpp` captures uvw `[this]`** — non gardés (non touchés par T3.1,
  hors scope déclaré). → étendre le pattern aliveTag.
- **[LATENT] Asymétrie d'inversion volume Denon/Marantz** : `setVolume` fait `v = 99 - v`
  mais le parse `MV` ne l'inverse pas — sémantique visiblement incohérente, pré-existante.
  → à investiguer avant fix (peut-être voulu selon l'échelle device).
- **[RÉSOLU — T3.10] `ThinIo.h` promu dans `IO/`** : le template générique driver-agnostique a
  été déplacé de `IO/KNX/` vers `IO/` (git mv), et les mixins Mqtt/Web/Gpio ont été rebasés
  dessus. Wago a délibérément **non** été rebasé : son entrelacement alias/link ne rentre pas
  dans le contrat ThinIo sans en dénaturer la sémantique.
- **[DÉCISION UTILISATEUR EN ATTENTE] Gadspot conservé** (T3.3, choix conservateur) : le
  supprimer casserait le chargement des io.xml existants qui référencent le type. Garder ou
  supprimer (avec échec propre type-inconnu, comme MySensors) ?
- **[MINEUR] Foscam user/password non URL-encodés** dans les URLs construites (caractères
  spéciaux cassent l'auth) — double-encodage risqué pour les configs pré-encodées, décision
  à prendre.

## Wave 5 — follow-ups (hors périmètre, non corrigés)

- **[SÉCURITÉ, spec T2.5 différé] UrlDownloader garde `VERIFYPEER/VERIFYHOST=0` globalement** —
  l'item d'acceptation de T2.5.md (TLS vérifié par défaut, insecure opt-in) a été volontairement
  différé pour préserver l'iso-comportement pendant la réécriture libcurl. → ticket dédié
  (activer la vérification TLS par défaut + option insecure explicite ; impact : caméras
  locales en HTTPS auto-signé).
- **[CORRECTNESS, confirmé par revue] `HttpClient::_parser_begin` ne vide pas `request_headers`
  entre deux requêtes keep-alive** (`HttpClient.cpp:118-133`) — les en-têtes de la requête N
  fuient dans la requête N+1 (ex : un `origin` périmé continue de déclencher CORS). → follow-up.
- **[DOC] Paramètre GPIO réellement nommé `debounce`** (pas `debounce_time`), lu par
  `GpioInputSwitch.cpp:47`, absent de ioDoc. Le helper `GpioCtrl::parseDebounceTime` est prêt
  pour ce call site (les chaînes garbage arrivent aujourd'hui en 0.0 → fallback silencieux).
  → mini-ticket : appeler le helper depuis GpioInputSwitch + entrée ioDoc.
- **[HYGIÈNE T2.4]** `backups/corrupt/` non borné (boots corrompus répétés) ; délai de notif
  30 s = heuristique, pas un vrai événement « loop démarré ». Acceptés en l'état.
- **[DÉPLOIEMENT T2.5]** une libcurl compilée sans AsynchDNS/c-ares bloquerait la loop à chaque
  résolution (l'image de référence a AsynchDNS) — à mentionner dans la doc de déploiement.
- **[NETTOYAGE]** `configure.ac` : check `AC_CHECK_PROG` du binaire curl désormais obsolète
  (plus de subprocess). ~~`docs/13_utility_lib.md` référence encore SHA1.{cpp,h} supprimés
  (T2.3)~~ — **RÉSOLU** : vérifié au source le 2026-08-24 (E4.5e), le document ne mentionne plus
  SHA1 que pour dire que `src/lib/SHA1.{cpp,h}` n'existent plus ; le seul condensé SHA-1 restant
  est l'`EVP_Digest(..., EVP_sha1(), ...)` du handshake WebSocket
  (`src/bin/calaos_server/WebSocket.cpp:400-408`), imposé par la RFC 6455.

## Wave 4 — transverse, à traiter en priorité

- ~~**[SÉCURITÉ/UAF, tous handlers WS] `JsonApi.cpp:450-537` `buildJsonState`**~~ : **RÉSOLU** par
  T2.15 (commit `01089187`, board ✅) — le code actuel porte la garde `alive`/`apiAlive` et la
  re-résolution par `playerById()` que ce finding réclamait, avec un test dédié
  `core/JsonApiAudioState_test`. Vérifié sur le code de master le 2026-08-16.
  ⚠️ **La famille de bugs frères, elle, n'est PAS résolue** : ~22 méthodes
  `audio*`/`audioDb*`/`decodeGetPlaylist`/`getNextPlaylistItem` et ~42 sites d'appel capturent
  `this`/`AudioPlayer*`/`IPCam*` à travers des I/O réseau asynchrones **sans garde**, alors que
  le pattern existe dans les mêmes classes et n'est appliqué qu'à 3 sites (`get_cover`,
  `downloadCameraPicture`). `JsonApiHandlerWS` ne déclare même pas son membre alive-token.
  → **T3.17**.
- **[LIFETIME] `HttpClient.cpp:578` `sendToClient`** : enregistre un `once<uvw::WriteEvent>` par
  appel capturant `this` brut — dangling si le client est détruit avec des writes en vol.
- **[FOOTGUN] `Utils::from_string("")` retourne true** — ✅ **TICKETÉ : [T3.25](T3.25.md)**
  (2026-08-25), **fiche la plus prioritaire du lot**.
  ⭐ **ADDENDUM (revue de `fix/t3.25`, 2026-08-25) — LE TITRE DE CETTE PUCE EST TROP ÉTROIT.**
  Mesuré au `g++ -std=c++17` : le retour de `from_string` bascule `true` → `false` sur **TROIS**
  familles d'entrée, pas une. **(1) le blanc** (`""` et toute chaîne blanche) — la seule où `dest`
  n'était **pas écrite**, et donc la seule qui produisait un symptôme ; **(2) le signe seul**
  (`"-"`, `"+"`) — `num_get` consomme le signe, ne trouve pas de chiffre, échoue et écrit 0 ;
  **(3) ⭐ le DÉBORDEMENT** (`"2147483648"`, `"4294967296"`, `"99999999999999999999"` en `int` ;
  `"1e400"` en `double`) — `num_get` consomme **tout**, **sature** à `INT_MAX`/`INT_MIN`/`±DBL_MAX`
  et pose `failbit`, et `iss.eof()` répondait « succès » pour la valeur saturée. **La valeur, elle,
  ne bouge pas** : la saturation était déjà figée par `core/JsonApiSession_test` et
  `core/JsonApiInputGuards_test`, **avant comme après** ⇒ **la découverte du ticket sur cette
  famille n'est pas `INT_MAX`, c'est le `false`**, et il n'était épinglé nulle part avant la revue.
  ⚠️ **La conclusion de l'audit survit intacte** : seuls les **8** sites de `src/` qui lisent le
  retour peuvent voir la différence, et les deux affectés (`WagoConfigParse.h`, `GpioCtrl.h`) y
  **gagnent**. → détail dans [T3.25](T3.25.md) §4 et §9, suivi dans [T3.25a](T3.25a.md).

  ⛔ **DEUX CORRECTIONS DE FAIT À CETTE PUCE, mesurées** : (1) « avec dest zéro-initialisée » est
  **FAUX** — sur une entrée vide **ou blanche**, la sentinelle échoue **avant** `num_get` et
  **`dest` n'est pas écrite du tout** (mesuré : dest pré-semé à 21845 ⇒ ressort à 21845) ; le
  « 0 en cas d'échec » ne vaut que pour une entrée **illisible NON blanche** (`"true"` → 0,
  `"12abc"` → 12). (2) le site n'est plus `src/lib/Utils.h:308-315` mais
  **`src/lib/StringUtils.h:104-111`** depuis T2.2 (`Utils.h` ne contient plus le mot
  `from_string`). ⭐ Et **`Utils::is_of_type<T>("")` rend `true` aussi** (`:96-103`, son `T tmp;`
  est lui-même non initialisé) ⇒ **une garde `is_of_type` ne protège pas**.
  ⭐ **Balayage : 319 sites d'appel dans `src/`, 310 ignorent le retour, 165 passent une locale
  déclarée sans initialiseur, 71 un membre sans initialiseur en-classe.** Un second balayage
  indépendant donne 320 / 157 / dont 112 sans garde `.empty()` — les deux s'accordent à ~5 %.
  ⭐ **Et le chemin est ATTEIGNABLE À DISTANCE** : `set_state` n'a **aucun `scopeDenied`**
  (`JsonApiHandlerWS.cpp:170-231`), `JsonApi.cpp:774/777` passe la chaîne cliente brute à
  `set_value(string)`, et `{"value":"impulse up "}` atteint `IO/OutputShutter.cpp:110-116` où
  `int v;` non initialisé part en durée d'impulsion sur un volet physique. Chaque appelant doit se
  défendre par un range-check (cf. `parseGridDimension` T1.10).
- **[INFRA TESTS] Les suites Python (tests/python/, 42+ tests T1.8+T1.16) ne sont PAS câblées
  dans `make check`** — délibéré en wave 4 (le format Makefile.am est gtest-only, le câblage
  pytest demande configure.ac). → petit ticket d'infra.

## Wave 4 — par sous-système (hors périmètre, non corrigés)

**MCP/Roon (T1.16)** :
- `client.py:45` — le `_task` de `connect()` n'est jamais annulé ; l'annuler au shutdown du
  lifespan server.py = changement de comportement (validation requise).
- `config.py:31` — `@lru_cache` sur `get_config()` rend tests/reloads collants.
- `ExternProcRoon_main.py:171` — `subscribe` peut ajouter deux fois la même zone ; `:181` —
  `"next"` dupliqué dans l'alternation d'actions (bénin).

**Sidecar auth / calaos-python (T1.8)** :
- `McpProxyHandler.cpp` forwarde les octets client tels quels : pourrait injecter/normaliser
  `X-Forwarded-For` lui-même (défense en profondeur).
- `message.py` (Python) n'a **aucune borne** sur `payload_length` entrant (le C++ cape à 4 MiB) —
  une longueur corrompue peut buffériser jusqu'à 4 GiB.
- `extern_proc.py` `run()` : `select` sur fd fermé lève et ne fait que logger — pas de reconnexion.
- `logger.py:9` — `colorama.init(strip=False)` en side effect d'import.

**RemoteUI OTA/WS (T1.10)** :
- OTA : pas de vrai backpressure (l'image entière transite une fois par la write-queue libuv) —
  le vrai fix demande HttpClient.
- Clamp haut 30 jours sur l'intervalle de rescan OTA : seul changement visible utilisateur non
  strictement mandaté par le spec — à faire valider.

**IO controllers (T1.19)** :
- `GpioCtrl.cpp:~185` — debounce codé en dur 0.05 s, ignore le `debounce_time` passé par
  `GpioInputSwitch.cpp:50` depuis la config. Le câbler = changement de comportement → validation.
- MySensors : messages **droppés avec warning** quand la gateway est déconnectée (avant : crash).
  Le spec permettait drop OU queue — passer à une queue est une option à valider.
- `GpioCtrl.h:60` — `getFd()` déclaré jamais défini (erreur de link si utilisé).
- `MySensorsController.cpp:287` — ids node/sensor vides depuis des lignes malformées créent des
  entrées junk dans `hashSensors`.
- `MySensors.cpp:23` — `DataType2String` tombe en fin de fonction sans return pour types
  inconnus (UB).

**Extern-proc mains (T1.17)** :
- `KNXExternProc_main.cpp` partage probablement le pattern argv hors-bornes (non possédé par
  T1.17 — à vérifier sous T3.2* ou follow-up).
- `MqttCtrl.cpp` (côté serveur) tronque toujours les payloads à NUL embarqué
  (`json_string_value` vers Params C-string).
- `McpServerManager.cpp:281` — table de backoff quasi-dupliquée de celle de WagoMap.

**Reolink (T1.14)** :
- Le désenregistrement C++ ne dit PAS au process Python d'arrêter de surveiller la caméra
  (aucune action `unregister` dans le protocole) — events droppés côté C++ en attendant ;
  follow-up protocolaire raisonnable, pas une faille.
- `ExternProcReolink_main.py:~773` — `background_tasks` mélange `concurrent.futures.Future` et
  tâches asyncio ; `cancel()` peu fiable sur coroutine en cours.
- `ReolinkCtrl` singleton + son `ExternProcServer *process` jamais détruits/arrêtés au shutdown.
- `ReolinkCtrl.cpp:84` — log debug du payload d'event complet (verbeux, sans credentials).

## RemoteUI (T1.6)

- `RemoteUI::getProvisioningResponse` hardcode `ws://localhost:5454/api/v3/remote_ui/ws` comme
  websocket_url → devices provisionnés pointés sur localhost ; faux en déploiement réel.
- `RemoteUI::getRemoteUIConfigMessage` fait `std::stoi(get_param("brightness"))` /
  `std::stoi(get_param("timeout"))` non gardé → throw sur vide/non-numérique (contraste
  `getBrightness` qui utilise `Utils::from_string`).
- Le chemin d'auth WS vérifie le timestamp deux fois (redondant ; retirer altérerait l'ordre imposé).
- Singleton `RemoteUIManager` jamais détruit (timers arrêtés dans un dtor qui ne tourne jamais) —
  pattern pré-existant.

## ExternProc (T1.9)

- Quirk latence frame de longueur nulle : une frame `payload_length==0` dont l'en-tête épuise le
  buffer ne signale `finished` qu'au **prochain** `processFrameData`. Inoffensif avec de vrais senders.
- Opcode inconnu : avale silencieusement 5 octets et resynchronise à l'aveugle — aucune erreur
  remontée, stream désynchronisé. Un design plus strict fermerait la connexion (comme longueur
  excessive).
- `ExternProcClient` ctor fait `argc -= 2; argv += 2;` pour `--socket`/`--namespace` sans vérifier
  la position → corrompt argc/argv si les options sont ailleurs.
- `connectSocket()` calcule `len = strlen + sizeof(sun_family)` pour connect() — idiome non
  portable ; `sizeof(sockaddr_un)` ou forme `offsetof` plus robuste.
- `ExternProcServer` binde un socket à préfixe prévisible dans `/tmp` monde-inscriptible
  (`/tmp/calaos_proc_<uuid>_…`) — l'uuid limite le squatting mais un runtime dir privé serait plus propre.
- `sendMessage` (client) ignore les écritures `send()` courtes (seules les erreurs sont vérifiées).

## E4.3ab — bugs révélés par les nouveaux tests

Remontés par `tests/TimeRangeCalendar_test.cpp` et `tests/core/ConfigRoundTrip_test.cpp`,
**non corrigés** (le ticket était test-only, zéro fichier de production touché).

- `src/lib/TimeRange.h` n'a **aucun include guard** → l'inclure directement *et* via
  `InPlageHoraire.h` est une erreur dure `redefinition of class TimeRange`. Le test doit passer par
  exactement un chemin d'inclusion (contourné par un commentaire dans le test, pas corrigé).
- **UB atteignable** : `TimeRange::getStartTimeSec()`/`getEndTimeSec()`
  (`TimeRange.cpp:100-104,142-146`) déclarent `int h, m, s;` et **ignorent la valeur de retour** de
  `from_string()`. Une chaîne proto tronquée (`split()` complète la liste avec des `""`) les laisse
  non initialisés → secondes-du-jour aberrantes (`-1858848627` observé). Atteignable depuis le
  chemin proto/JSON. `TimeRangeTest.EmptyProtoDoesNotReadOutOfBounds` épingle seulement que la
  construction reste dans les bornes, jamais la valeur : c'est de l'UB, pas un contrat.
- **Nettoyage `ListeRule::in_event` couplé à une liste blanche de `gui_type`** (⚠️ correction d'une
  suspicion initiale de UAF : `ListeRule::Remove(IOBase *)` **est bien appelé**, depuis
  `ListeRoom::deleteIO()` `ListeRoom.cpp:359`, et `~Room()` y passe — il n'y a donc **pas** de UAF
  vivant aujourd'hui). Le problème réel est la fragilité du couplage : le désenregistrement est
  conditionné à `get_param("gui_type")` ∈ {`time`, `temp`, `analog_in`, `time_range`, `timer`}, et
  non à qui s'est réellement enregistré. Les 3 seuls appelants de `ListeRule::Instance().Add(this)`
  (`InPlageHoraire` `time_range`, `InputTime` `time`, `InputAnalog` `analog_in`, + `InputTemp`
  `temp`) sont couverts par chance ; **tout futur IO qui s'enregistre avec un autre `gui_type`
  laissera un pointeur pendant déréférencé par `RunEventLoop()` (`ListeRule.cpp:148`)**. À traiter
  par le ticket de la série E4.2 qui possède `ListeRule` (symétrie Add/Remove plutôt que liste
  blanche de chaînes).
- Cosmétique : `InPlageHoraire.cpp:220` — `LoadRange()` concatène l'offset de fin dans `sstart` au
  lieu de `sstop` (log de debug uniquement).
- Cosmétique : `CalaosConfig.cpp:365-368` — un `rules.xml` vide mais valide logue « `<calaos:rules>`
  node not found » parce que le test porte sur le **premier enfant**, pas sur le nœud lui-même ; or
  `LoadConfigRule()` crée exactement ce fichier puis s'en plaint.
- **Ambiguïté produit épinglée par un test** : une plage inversée (fin < début, ex. 23:00→01:00 —
  la façon naturelle d'écrire « à cheval sur minuit ») est aujourd'hui une plage **vide** qui ne
  matche à **aucun** moment (`hasChanged()` teste `cur >= start && cur <= end`, sans wrap).
  `InPlageHoraireEvalTest.InvertedRangeNeverMatches` épingle le comportement actuel pour qu'un futur
  correctif de wrap-around le casse **délibérément**. **En attente d'une décision utilisateur.**

## E4.4b — divergences XPath TinyXPath→pugixml

Le rapport d'implémentation en listait 3 classes de divergence. La revue en a trouvé une
**quatrième**, et c'est la plus importante des quatre pour l'utilisateur :

- **L'arithmétique de TinyXPath tronquait en entier et débordait en int32.** pugixml, lui, est
  conforme XPath 1.0 (tout nombre est un `double` IEEE 754). Observé :
  - `//temperature/@value + 0` sur `value="21.5"` → TinyXPath **`21`**, pugixml **`21.5`** ;
  - `1000000 * 1000000` → TinyXPath **`-727379968`** (débordement int32 silencieux), pugixml
    **`1000000000000`** ;
  - `-1 div 0` → TinyXPath **`""`** (chaîne vide), pugixml **`-Infinity`**.
- **C'est la seule classe de divergence où une config qui produisait déjà un nombre exploitable
  en produit maintenant un autre.** Les trois autres classes ne concernent que des expressions
  qui échouaient (ou renvoyaient du vide) des deux côtés. Une config qui faisait de
  l'arithmétique sur une valeur décimale voyait donc jusqu'ici sa partie fractionnaire jetée :
  le nouveau résultat est **correct**, mais il est différent, et un `WebCtrl` calibré sur
  l'ancienne troncature (seuils, échelles) doit être revérifié. Déjà consigné côté utilisateur
  dans `RELEASE_NOTES.md`.

Correction d'exactitude apportée par la revue, sans impact sur le code :

- Le commentaire de `WebCtrl.cpp` autour de la garde `if (!context)` (`WebCtrl.cpp:297`) **surévalue
  légèrement** sa portée. pugixml rejette les documents vides, réduits à un commentaire ou à une PI
  **dès le parse**, avec `status_no_document_element` — on n'atteint donc jamais la garde par ces
  entrées-là, elles sont déjà sorties en amont. La garde reste une **défense en profondeur**
  légitime sur `document_element()`, mais ce n'est pas elle qui attrape l'ancien SIGSEGV
  TinyXPath. Inoffensif ; reformulation du commentaire possible plus tard, aucune urgence.

## T3.13 — suites

Deux points **non bloquants** relevés à la revue de T3.13, laissés en l'état (aucun n'est causé
par le ticket) :

- **`Utils::time2string_digit()` (`src/lib/StringUtils.cpp:161`) massacre silencieusement les
  durées négatives.** La garde `if (hours > 0)` ne sert qu'à omettre le champ heures ; sur une
  valeur négative elle jette **le signe avec** : `-3600` sort en **`00:00`** (une heure avant
  minuit affichée comme minuit pile), et `-1800` sort en **`-30:00`** (le signe survit sur le
  champ minutes, mais la lecture « -30 heures » est fausse). Or une borne *calculée* peut être
  négative : un lever de soleil moins un offset important tombe avant minuit. `TimeRange::toString()`
  appelle cette fonction à **4 endroits**, donc une borne négative s'y affiche encore mal. T3.13
  contourne le problème **localement** (`timeToLogString()` dans `TimeRange.cpp`, qui imprime le
  compte de secondes brut quand la valeur est négative) plutôt que de toucher la fonction
  partagée, dont la sortie est épinglée par `CommonLib_test`. Un correctif propre devrait
  extraire le signe avant le découpage h/min/s et réajuster le test.

- **Chemin d'échec polaire de `sun_rise_set` : le cache n'est jamais peuplé.**
  `computeSunSetRise()` retourne **avant** d'écrire dans le cache quand `res != 0` (latitude où
  le soleil ne se lève ou ne se couche pas du jour). Conséquence : chaque appel refait le travail
  complet — `get_config_options()`, donc mutex + `flock` partagé + re-parse de la config. Le
  défaut est **pré-existant**, mais T3.13 l'**amplifie** : maintenant que l'évaluation ne
  court-circuite plus, *toutes* les plages solaires d'un `InPlageHoraire` paient ce coût à chaque
  tick (10 Hz) et non plus seulement la première qui matchait. **Inatteignable aux latitudes
  françaises** ; à corriger en peuplant le cache (ou un marqueur d'échec) sur ce chemin aussi.

## E4.2c — suites

Vérifié par la revue, **non corrigé** :

- **Formulation à corriger dans le ticket/board** : « une règle survit au save/reload » est trop
  fort — l'id survit à la **SAUVEGARDE** ; au **RECHARGEMENT** la condition/action est toujours
  rejetée (contrat de chargement figé par `CoreSmoke_test.RuleWithUnknownIoIsDropped`), donc la
  règle revient avec 0 condition et l'id est perdu à la sauvegarde **suivante**. Le vrai gain est
  quand l'IO réapparaît **avant** le rechargement.

- `ConditionStd::getVarIds(vector<IOBase*>&)` n'a **plus aucun appelant** — code mort à supprimer.

- **Pré-existant, comportement préservé mais désormais visible** : le chemin de compat legacy
  audio/caméra de `ConditionStd::LoadFromXml` fait `in = io;` sans mettre à jour `id` vers le
  nouvel id, alors qu'`ActionStd::LoadFromXml` fait correctement `id = out->get_param("id")`.
  Résultat : `params`/`ops` restent clés sur l'ancien `iid`/`oid` tandis que l'évaluation utilise
  le nouveau — ces conditions s'évaluent contre des params vides. Candidat correctif d'une ligne.

## E4.3cd

Deux nits relevés à la revue, **non bloquants**, laissés en l'état :

- **`./configure --enable-asan=<valeur invalide>` désactive silencieusement au lieu d'échouer.**
  Le bloc teste `test "x${enable_asan}" = "xyes"` : `--enable-asan=garbage` (ou `=1`, ou `=true`)
  tombe donc dans le `else` implicite et produit un build **sans** sanitizer, sans le moindre
  avertissement. Seuls `--enable-asan` et `--enable-asan=yes` marchent. Le piège est réel — on
  croit mesurer sous ASan et on ne mesure rien — mais il reste visible dans le résumé de
  `configure` (`AddressSanitizer (--enable-asan).....: no`). Correctif propre : un `AS_CASE` sur
  `yes|no` avec `AC_MSG_ERROR` sur tout le reste.

- **Le comportement lcov de bout en bout du job CI `coverage` n'est vérifié par aucune exécution.**
  L'image de dev ne contient pas `lcov`, donc la chaîne `--zerocounters` → `--capture --initial`
  → `--capture` → `--add-tracefile` → `--remove` → `genhtml` n'a jamais tourné ; seules la syntaxe
  YAML et la cohérence des options ont été relues. En particulier les noms de catégories passés à
  `--ignore-errors` (`gcov,source,graph`) sont ceux de **lcov 1.16** (debian:12) et seraient
  rejetés par lcov 2.x, et le filtre `--remove` n'a pas été confronté à de vrais chemins. **Non
  bloquant par construction** : le job est `continue-on-error: true`, `make check` est neutralisé
  par `|| echo`, et l'upload est `if: always()` avec `if-no-files-found: warn` — chaque mode
  d'échec produit donc un rapport vide plutôt qu'une CI cassée. La première exécution réelle sur
  GitHub sera la vraie validation.

## E4.4cd — suites

- **Régression de diagnosticabilité (mineure)** : les erreurs de parsing loggent désormais un
  **offset en octets** au lieu d'un **numéro de ligne**. Pour un opérateur qui débogue une config
  cassée, la ligne est bien plus utile. Follow-up simple : calculer la ligne à partir de l'offset
  (compter les `\n` jusqu'à `result.offset`) et logger les deux.

- `XmlUtils::setAttribute` ne touche que la première occurrence si un fichier source contient un
  attribut **réellement dupliqué** (TinyXML dédoublonnait au parsing). Inoffensif aujourd'hui :
  chaque sauvegarde reconstruit depuis `Params` (`std::map`) et aucun nœud chargé n'est
  re-sérialisé tel quel.

- Divergences `hits` hors des six cas testés, atteignables seulement en éditant le XML à la
  main : `"0x10"` → TinyXML 0 / pugixml 16 ; `"999999999999"` → TinyXML −727379969 (UB de
  `sscanf`) / pugixml 2147483647. pugixml est strictement meilleur.

## ⚠️ Piège méthodologique — `_DEPENDENCIES` et faux verts en red-before-green

Plusieurs binaires de test surchargent `<name>_DEPENDENCIES` dans `tests/Makefile.am` (convention
héritée de `JsonApiHardening_test`) **pour éviter qu'automake ne fasse des objets construits
ailleurs des prérequis**. Effet de bord : le binaire de test **ne se relinke pas** quand l'objet de
production qu'il teste change. Conséquence directe sur notre méthode : une vérification
« red-before-green » qui se contente de restaurer l'ancien source et de relancer `make` peut
**exécuter l'ancien binaire et rendre un faux vert**. Constaté 2× (revue E4.2c, revue T3.15 — cette
dernière a d'abord conclu à tort que le test passait sur le code pré-fix). **Règle** : pour toute
preuve red-before-green, supprimer explicitement le `.o` de production ET le binaire de test avant
de reconstruire, puis vérifier que la reconstruction a bien eu lieu. Candidat follow-up : revoir la
nécessité de ces surcharges `_DEPENDENCIES`.

- `remote-ui.md` documente le **contenu** de `device_info` sous forme d'enfants
  `<calaos:param name/value>` alors que le lecteur **et** l'écrivain utilisent des **attributs** —
  divergence doc/code préexistante. En revanche l'**emplacement** documenté (l.534, à l'intérieur
  de `<calaos:remote_ui>`) est correct et c'est bien ce que T3.15 implémente.

## E4.4dbis — suites

Deux divergences **cosmétiques** vérifiées par la revue du port de `ConfigStore.cpp` vers pugixml.
Non bloquantes — la sortie produite est strictement mieux formée qu'avant — mais bonnes à connaître
si quelqu'un **diffe une config** avant/après migration :

- un `local_config.xml` **édité à la main sans déclaration XML** gagne un `<?xml version="1.0"?>`
  à la première sauvegarde : pugixml en émet toujours une, TinyXML n'en émettait pas.

- un **BOM UTF-8** était préservé par TinyXML (`useMicrosoftBOM`) ; il est désormais **supprimé**.

## E4.2d — suites

Deux constats **non bloquants** relevés par la revue du passage de `Rule`/`ListeRule` aux
`unique_ptr`. Aucun des deux n'est une régression introduite par E4.2d :

- `AddCondition`/`AddAction` n'ont **pas** la garde null que `Add(Rule*)` a gagnée. Ce n'est pas
  une régression : `LoadFromXml` garde déjà en amont, aucun appelant ne peut y passer un pointeur
  nul aujourd'hui. À harmoniser si un nouvel appelant apparaît.

- le test `RemovingTheSameRuleTwiceIsNotADoubleFree` **compare une valeur de pointeur
  indéterminée** (le pointeur a été détruit par le premier `Remove`) : c'est de l'UB formel au
  sens du standard, même si tout compilateur réel se contente d'une comparaison de bits. Le point
  est commenté dans le test lui-même.

## T3.16 — dette de formatage restante

Une fois `src/lib/pugixml` exclu du pathspec de `format-check` (T3.16), une PR **simulée
contenant tous les commits du refactoring** (163 commits, `origin/master...HEAD`) fait encore
remonter **~5 087 lignes réparties sur 122 fichiers de notre propre code** — mesuré en
`debian:12` avec clang-format 14.0.6, la version exacte du job.

Principaux contributeurs :

| Lignes | Fichier |
|---|---|
| 361 | `tests/UrlDownloader_test.cpp` |
| 236 | `tests/TimeRangeCalendar_test.cpp` |
| 217 | `src/lib/StringUtils.cpp` |
| 209 | `src/lib/ConfigStore.cpp` |
| 208 | `tests/core/Timer_test.cpp` |
| 145 | `src/bin/calaos_server/IO/KNX/KNXIo.cpp` |
| 141 | `tests/core/JsonApiAudioState_test.cpp` |
| 139 | `src/bin/calaos_server/HttpClient.cpp` |
| 138 | `src/bin/calaos_server/JsonApi.cpp` |
| 135 | `src/lib/ConfigOptions.cpp` |

(longue traîne ensuite)

**Portée réelle du problème** : ce chiffre est un **pire cas théorique**. `format-check` est
`pull_request`-only, donc il **ne bloque pas les push sur master**, et une PR de taille normale
ne voit que **ses propres lignes touchées**. En revanche, une PR qui porterait l'intégralité du
refactoring **échouerait**.

**Correctif prévu** : une passe `clang-format` dédiée sur ces fichiers. À lancer **après le merge
d'E4.2e** (qui détient `ListeRule.cpp`), afin que la passe puisse le couvrir aussi sans conflit.

## E4.2e — suites

- **[LATENT, désormais inatteignable par ce chemin] `Params::operator[]` renvoie `""` pour une
  clé absente** : le chemin de compatibilité audio/caméra de `ConditionStd::LoadFromXml`
  (`io->get_param("iid") == id`) faisait donc **correspondre le premier IO audio/caméra de la
  config** à une entrée sans id. E4.2e traite l'id vide avant résolution, ce qui rend ce chemin
  inatteignable — mais le comportement de `Params::operator[]` reste un piège (voir aussi le
  finding « `operator[]` renvoie par VALEUR »).

- `RemoveCondition`/`RemoveAction` ne recalculent pas `missingIoIds` : une règle resterait
  désactivée après suppression de la condition fautive. Aucun appelant de production aujourd'hui.

- `Rule::get_condition(i)`/`Rule::get_action(i)` indexent sans garde de bornes
  (`Rule.h:128-129`). Tous les appelants de production sont des boucles bornées et il n'existe
  aucune API JSON exposant les règles ; seul du code de test avec un index littéral peut y
  tomber. → petite garde à ajouter.

## E4.2f — cadrage (back-pointers AutoScenario)

- **UB latent à l'extinction, hors périmètre E4.2f** : `~Room` appelle `ListeRule::Instance()`.
  La sûreté de l'arrêt ne tient qu'à `main.cpp:137-142`, qui construit `ListeRule` **avant**
  `ListeRoom` (commentaire « Ensure calling order of destructors ») — donc `~ListeRoom` court en
  premier et `~AutoScenario` ne touche plus aucune `Rule`. **Rien ne teste cet invariant** :
  réordonner ces deux lignes réintroduit un accès à un singleton détruit, silencieusement.
  À épingler par un test, ou à rendre explicite autrement qu'un ordre de déclaration.
- **Back-pointers mesurés inoffensifs — NE PAS convertir** : `AutoScenario.h:54-59`
  (`ioScenario`, `ioIsActive`, `ioScheduleEnabled`, `ioStep`, `ioTimer`, `ioTimeRange`),
  `AutoScenario.h:61` `roomContainer`, `AutoScenario.h:42` `ScenarioAction::io`,
  `IO/Scenario.h:38` `auto_scenario`. Les seuls appelants de `deleteIO()` sur ces IOs sont
  `AutoScenario` lui-même et chacun annule le membre juste après ; il n'existe aucune API JSON
  de suppression d'IO générique. Les convertir = ~40 sites réécrits pour zéro danger réel.
  Consigné pour qu'un futur passage ne « complète » pas la conversion par symétrie.

## E4.0 — inventaire de l'API JSON (mesures)

- **`docs/08_http_api.md` est faux, pas seulement incomplet** : il couvre 6 opérations sur 57,
  **décrit une enveloppe d'event qui n'est pas celle du code**, documente un `push_notification`
  qui n'existe pas, et annonce `get_mcp_info` en WebSocket alors qu'il est HTTP-only.
  → **ne peut pas servir d'oracle** pour les tests de caractérisation. À reprendre depuis le code
  (prévu en fin de série E4.0).
- **Divergences HTTP/WS à geler telles quelles** (ce sont des bugs, mais les figer d'abord) :
  `audio_db` attend `get_albums` en HTTP et `get_album` en WS — incompatibilité silencieuse ;
  `autoscenario` avec un `type` inconnu **ne répond rien du tout** ; `buildQuery()` teste
  `Exists("id")` mais lit `jParam["input_id"]`.
- **Tout part en chaîne** : 32 `json_string` et **zéro** `json_real`/`json_integer` dans
  `JsonApi.cpp`. Seule exception, `eventlog` (déjà en nlohmann) émet 4 entiers JSON. Bonne
  nouvelle pour la migration : le piège du formatage numérique (`.0` final) est presque sans
  objet. `buildJsonIO` **omet** une clé absente au lieu d'émettre `null`.
- **Le piège `_DEPENDENCIES` mord actuellement dans l'arbre de travail local** (27 occurrences) :
  `JsonApi.o` daté du 28/05 pour un source du 11/08, et le `tests/Makefile` généré ne connaît que
  3 des 20 tests `core/`. ⚠️ **Cela n'invalide aucun build de merge** — tous passent par
  `make distclean` dans le conteneur, qui efface les `.o`. C'est un artefact de l'arbre local.
- **4 types d'events sont morts** : 24 constantes d'enum, 23 types réels, 19 réellement poussés.

## T3.14 — suites

Quatre réserves **non bloquantes** relevées par la revue (mesures reproduites indépendamment).
Aucune ne remet en cause le correctif : les deux UB visés sont bien supprimés.

- **Le garde-fou anti-régression de l'instance 2 ne mord que sous `--enable-asan`**
  (`tests/StaticLogShutdown_test.cpp:150`). Mesuré : sur un build **plain** contre la lib **non
  corrigée**, `StaticDestructorStillReachesTheLogger` **passe** — seul `NoParasiteOutputAtShutdown`
  échoue. L'UAF sur `calaosLogger()` (le `logger_hash` détruit avant le `WagoMapManager` statique)
  est un accès mémoire silencieux qui ne change pas la sortie observable ; il ne devient un échec
  de test que sous ASan, **qui n'est pas la configuration CI par défaut**. Autrement dit, une
  régression de l'instance 2 seule repasserait verte en CI. Le test reste utile (il fixe le motif),
  mais ne le créditer que du red-before-green de l'instance 1.

- **`defaultCoutLogger()` peut allouer *pendant* la chaîne atexit** (`src/lib/LogSetup.cpp:71-75`).
  Le singleton est construit **paresseusement**, donc son `new Logger()` s'exécute au premier
  appel — y compris si ce premier appel vient d'un destructeur statique. Cela contredit
  l'intention affichée en `LogSetup.cpp:101-103` (« do not allocate memory »). **Inoffensif** en
  pratique (l'objet n'est jamais détruit, il n'y a donc pas d'ordre à violer), mais l'invariant
  écrit et le code divergent. Correctif d'une ligne si on veut les réaligner : appeler
  `defaultCoutLogger();` dans `freeLoggers()` **avant** de poser le drapeau, ce qui force la
  construction pendant que `main()` tourne encore et supprime toute allocation à l'extinction.

- **Un premier log tardif peut encore écrire en brut sur `cout`** (`src/lib/Logger.cpp:101`). Si le
  cache de niveaux est **froid**, le tout premier log — même émis depuis un destructeur statique —
  déclenche encore `get_config_option()`, donc une lecture de `local_config.xml`, et peut produire
  « Parse error… / local_config.xml » directement sur `cout`. C'est désormais **memory-safe** (plus
  de lecture pendante : c'est exactement ce que T3.14 corrige), mais le critère d'acceptation
  « plus aucun message parasite » ne tient que parce que `main()` logge en premier et réchauffe le
  cache. Un binaire qui ne loggerait qu'à l'extinction reverrait le message — propre, mais parasite.

- **[PRÉEXISTANT — non introduit par T3.14] Fuite vraie sur double `initLogger()`**
  (`src/lib/LogSetup.cpp:106-107`). `initLogger()` écrase `loggerHash()[defaultDomain()]` **sans
  détruire le `Logger` précédent du même domaine** : appelé deux fois, le premier `Logger` est
  définitivement perdu. C'était déjà le cas avant ce ticket ; ce qui change, c'est que la fuite est
  désormais **invisible à LSan** (le hash lui-même n'étant plus détruit, tout son contenu est
  atteignable à la sortie, donc classé « still reachable » et non « definitely lost »). À traiter
  comme une dette propre si `initLogger()` devient ré-appelable.

## E4.2f — suites

Deux réserves **non bloquantes** relevées par la revue (mesures reproduites indépendamment).
Aucune ne remet en cause le correctif : l'UAF atteignable depuis l'API JSON est bien supprimé.

- **`getRuleSteps()` purge *et* alloue un vecteur neuf, et il est appelé dans la condition de
  boucle** : `Scenario::toJson()` écrit `for (uint i = 0; i < auto_scenario->getRuleSteps().size(); i++)`
  (`IO/Scenario.cpp:99`), donc chaque itération refait un `purgeDeadSteps()` **et** une allocation
  de `vector<Rule *>` — O(n²) purges + O(n²) allocations par sérialisation d'un scénario. Le motif
  d'appel dans la condition de boucle est **préexistant** ; E4.2f l'aggrave légèrement en ajoutant
  la purge au corps de l'accesseur (avant, `getRuleSteps()` ne faisait que copier). Purement
  **cosmétique** aux tailles réelles (quelques dizaines d'étapes) : la correction est de
  *snapshoter* l'appel une fois hors de la boucle, ce que le commentaire d'`AutoScenario.cpp:40-48`
  demande déjà à tout futur appelant capable de détruire une `Rule` en cours d'itération.

- **Le `purgeDeadSteps()` d'`addStep()` est du code mort aujourd'hui** (`AutoScenario.cpp:636`).
  Les deux appelants de production (`JsonApi.cpp:1680` et `:1773`) n'appellent `addStep()` qu'après
  un `deleteRules()` ou sur un scénario neuf : la liste n'est jamais trouée à cet endroit et la
  purge n'a jamais rien à retirer. Elle est **gardée comme défense en profondeur** — et ce n'est
  pas l'équivalent de `checkScenarioRules()`, qui *renumérote* les étapes survivantes alors
  qu'`addStep()` ne numérote que la nouvelle ; sur une liste trouée, purger ne suffirait pas, il
  faudrait renuméroter. Le commentaire en place (`AutoScenario.cpp:620-635`) dit exactement cela,
  pour qu'un futur appelant qui casse la séquence ne construise pas silencieusement la collision
  de numéros d'étape.

## E4.0a — suites

- **Le `dynamic_cast` de `buildJsonCameras()`/`buildJsonAudio()` est du code défensif
  INATTEIGNABLE — E4.0b ne doit pas dépenser d'effort à couvrir sa branche fausse.** Mesuré :
  `ListeRoom::addIOHash()` (`ListeRoom.cpp:80-85`) ne pousse dans `cameraCache`/`audioCache` que
  si `get_param("gui_type")` vaut exactement `"camera"` ou `"audio_player"` ; les **seuls**
  endroits qui posent ces deux valeurs sont les constructeurs d'`IPCam` et d'`AudioPlayer`, qui
  appellent eux-mêmes `addIOHash()` dans la foulée. Toute autre classe d'IO **écrase** `gui_type`
  avec sa propre valeur (p. ex. `IO/IntValue.cpp:83-85` force `var_bool`/`var_int`/`var_string`),
  donc **aucun attribut XML ne peut faire entrer un IO étranger dans l'un des deux caches**. Le
  `dynamic_cast` n'a pas de branche fausse joignable depuis une configuration : la demande
  initiale du ticket (« mettre au moins un IO qui ne passe pas le `dynamic_cast` ») est
  **infaisable** et a été retirée d'`E4.0.md`. Ce n'est pas un défaut à corriger — c'est une
  garde à laisser en place, documentée pour qu'aucun sous-ticket n'aille chercher une couverture
  qui n'existe pas.

- **Le backlog de la file d'events fuit d'un cas de test à l'autre, pas seulement du chargement
  vers la session.** `EventManager` empile dans un idler uvw qui n'est jamais dépilé tout seul
  dans les tests ; on croyait que `loadReferenceHouse()` devait drainer les `EventIOAdded`
  qu'il lève, sinon tout cas épinglant une **absence** de message trébuche sur les événements en
  attente. ⚠️ **Cette cause était fausse et a été corrigée par E4.0d** (voir la section
  « E4.0d — events » plus bas) : le chargement ne lève **rien du tout**, et le backlog vient de
  `~Room()` au *teardown du cas précédent*. Le drain reste nécessaire, pour cette autre raison.
  La mesure va plus loin : ce qu'un cas laisse dans la file est délivré aux sessions du
  cas **suivant** — un test de silence **passe seul et échoue dans la suite complète**. Le drain
  de `TearDown()` (`JsonApiCharacterization.cpp:643-652`, `pumpEventLoop()` après
  `LoginThrottle::clear()`) est donc **aussi porteur** que celui de `loadReferenceHouse()` :
  les deux sont structurels, pas des précautions cosmétiques. Tout sous-ticket E4.0b→E4.0f qui
  bâtit sa propre fixture doit reproduire les **deux** drains, et E4.0d — le seul à faire tourner
  la boucle pour de bon — est celui qui en dépend le plus.

## T3.17a — suites

- **R4 — fuite de `jplayer`/`jplaylist` si l'objet de connexion du player meurt sans jamais
  rappeler (préexistant, hors périmètre).** Les gardes de T3.17a `decref` la paire à chaque
  sortie anticipée *de callback* ; elles ne peuvent rien pour le cas où le callback n'est
  **jamais invoqué du tout** — si l'objet de connexion du player est détruit alors qu'il porte
  encore des callbacks en attente, les `json_t*` capturés par valeur dans la fermeture partent
  avec elle sans `decref` → fuite. Le défaut est **antérieur à T3.17a et inchangé par lui** : la
  chaîne fuyait déjà de la même façon avant la garde. **Non observable dans les tests** — le fake
  tire toujours le callback ou en cède la propriété, donc aucun cas ne laisse une fermeture mourir
  en attente. À traiter **au niveau de l'épique** (propriétaire des `json_t*` ou politique de
  destruction des connexions), pas dans un sous-ticket de garde.

- **Note pour T3.17b/c — le second UAF est réel, une garde par token seul ne suffit pas.** Le
  `AudioPlayer*` brut capturé à travers l'aller-retour asynchrone a été **confirmé réel par la
  revue**, avec deux preuves indépendantes : (1) les adresses de tas sont **distinctes** — chunk
  `AudioPlayer` en `0x612…`, handler en `0x60e…`, donc ce sont bien deux objets de durées de vie
  séparées, et libérer l'un ne dit rien de l'autre ; (2) la `WsTestSession` était **encore vivante**
  au moment du crash, donc `apiAlive` n'était **pas** expiré. Conséquence directe pour T3.17b/c :
  une garde qui se contenterait de tester le token franchirait `expired()` sans broncher puis
  **déréférencerait le player déjà libéré à la ligne suivante**. Les deux morts sont indépendantes
  et demandent **deux** protections — le token pour la mort du client, la **re-résolution de l'IO
  par son id** pour la mort du player.

## E4.0b — divergences découvertes en caractérisant (non corrigées, gelées telles quelles)

Découvertes en écrivant les 52 cas de `JsonApiHome_test.cpp`. **Aucune n'est corrigée** — la série
E4.0 gèle le comportement réel, bugs compris. Candidates à des tickets.

1. **[API, sérieux] `get_io` / `get_state` ne lisent pas `items` au même endroit selon le
   transport.** WS lit `jsonRoot["data"]["items"]` (`JsonApiHandlerWS.cpp:255,314`), HTTP lit
   `items` **à la racine** (`JsonApiHandlerHttp.cpp:285,349`). Le même document envoyé aux deux
   transports n'adresse donc pas les mêmes IOs, et le mauvais transport répond `{}` ou l'enveloppe
   seule — **jamais une erreur**. Documenté dans les deux sens par des tests.
2. **[API] `set_state` en WS ne répond rien sans `msg_id`** (`JsonApiHandlerWS.cpp:334`) alors que
   **l'état est bien changé**. En HTTP la réponse est inconditionnelle. Un client qui omet `msg_id`
   croit sa commande perdue alors qu'elle a été exécutée.
3. **[MORT] `get_states` et `query` ne renvoient de contenu pour aucune IO d'une maison normale.**
   `get_all_values_bool/double/string()` et `query_param()` ne sont surchargés que par
   `IOAVReceiver` (`IOBase.h:103-111` renvoient des maps vides, seul `Audio/AVReceiver.cpp`
   surcharge). Deux des neuf commandes « cœur » du plan E4.0 sont en pratique sans contenu.
4. **[API] `get_param` sur un param inconnu n'est pas une erreur** : réponse `{"<param>":""}`.
   `"wrong io/param"` ne signifie jamais que « io inconnu ». Et **sans membre `param`**, la réponse
   est `{"":""}` — objet à **clé vide**, valide mais à surveiller à la migration.
5. **[API] Asymétrie param vide/absent** : `set_param` **refuse** une valeur vide
   (`JsonApi.cpp:690`) — un param ne peut pas être blanchi, seulement supprimé par `del_param` ;
   mais `del_param` sur un param **absent** renvoie `{"success":"true"}`.

**Remarque de cadrage sur E4.0.md** : le plan compte `get_states` et `query` parmi les neuf
opérations « cœur, ce que tout client appelle ». Le point 3 montre que leur valeur de filet est
celle d'un contrat de **forme vide**, pas d'un payload. Le vrai poids d'E4.0b est sur `get_home`,
`get_io`, `get_state`, les trois commandes de params et `set_state`.

**Acquis utiles pour E4.1** (le piège numérique n'est pas tout à fait absent) : `get_state` épingle
`Utils::to_string(double)` sur `"42.5"` et `"1.23457e+06"` — ostream, 6 chiffres significatifs,
**notation scientifique sur le fil, en chaîne**. Et `del_param` prouve que la clé **disparaît** de
`get_io` au lieu de passer à `null`.

**⚠️ La maison de référence est délibérément asymétrique — ne pas « harmoniser ».** La revue
indépendante d'E4.0b a démontré **par contre-mutation** que 6 clés de `buildJsonIO()` n'étaient
jamais observées *en présence* : la maison ne posait aucun des params optionnels, si bien que
supprimer ces clés de la production laissait la suite **verte** (52/52 sur du code muté). Un golden
ne prouve l'absence d'une clé que si un autre IO du même golden la porte. Depuis la correction,
`HOUSE_ACCENTED` porte les **7 params optionnels** (`hits`, `chauffage_id`, `unit`,
`auto_scenario`, `step`, `io_style`, `value_warning`) et **les autres IOs restent volontairement
pauvres** : c'est le contraste *à l'intérieur d'un même golden* qui épingle le contrat d'absence.
Sous la même mutation, la suite donne maintenant **3 rouges dont un qui nomme la clé perdue** — et
le binaire d'E4.0a passe au rouge lui aussi, l'enrichissement renforçant le filet des deux
tickets. Enrichir la maison est permis ; **uniformiser les IOs pour « faire propre » détruirait le
filet**.

## T3.17d — suites

- **[UAF, hors périmètre — mérite un ticket] `IPCam::downloadSnapshot()`, branche `isRunning()`
  (`IPCam.cpp:124-128`)** : quand un transfert est déjà en cours, le callback de l'appelant est
  parqué dans `Timer::singleShot(0, [=]() { dataCb(lastSnapshot); })`. Ce `[=]` capture le `this`
  de l'**IPCam** et lit le membre `lastSnapshot`, et **`~IPCam()` n'annule pas le timer**. C'est un
  use-after-free au niveau IPCam — et c'est **le seul chemin par lequel un callback de snapshot
  peut survivre à sa caméra**. T3.17d a couvert le côté handler ; le côté IPCam reste ouvert.
- **[DIVERGENCE, gelée telle quelle] `processCamera()` n'a pas de branche `else`**
  (`JsonApiHandlerHttp.cpp:909-999`) : une caméra **connue** avec un `type` non reconnu ne répond
  **rien du tout** — même forme de silence que `autoscenario` à type inconnu. Épinglée par
  `UnknownTypeAnswersNothingAtAll`.
- **[PORTÉE DU FILET — ce que les tests de T3.17d ne prouvent PAS]** `FakeSnapshotCamera`
  (`tests/core/JsonApiCameraSnapshot_test.cpp:114`) **surcharge `downloadSnapshot`**. Les deux cas
  `CameraDeletedMidTransferStillAnswers` et `ClientAndCameraGoneIsIgnored` épinglent donc le
  contrat du **handler** face à un callback **déjà détaché** de sa caméra — **pas la plomberie
  réelle d'`IPCam`**. Le fait « la caméra ne rappelle jamais » (raison 2 de T3.17.md, § *le second
  UAF n'est PAS universel*) repose donc sur la **lecture du code seule, pas sur une mesure** — et
  le point ci-dessus montre justement qu'il est faux via la branche `isRunning()`.
- **[PORTÉE DU FILET] `EmptyDownloadAnswersTheFallbackPicture`
  (`tests/core/JsonApiCameraSnapshot_test.cpp:227-238`) n'assère ni `Content-Length` ni le corps** :
  `camfail.jpg` est absent de l'arbre de test, donc la branche de repli ne peut pas être comparée
  octet à octet. L'invariant T3.17d « aucune donnée mutilée » n'est prouvé **octet à octet que sur
  la branche nominale** ; sur la branche de repli, le test ne vérifie que la forme.
- **[NULLPTR, préexistant, atteignable depuis l'API — mérite un ticket, concerne directement
  T3.17c] `player->get_database()->...` sans contrôle de nullité.** `audioGetDbStats`
  (`JsonApi.cpp:966`) **et les 15 `audioDbGet*`** (`JsonApi.cpp:1129…1576`) appellent
  `player->get_database()->get*(...)` **sans jamais tester le retour**. Or `AudioPlayer::database`
  vaut **`nullptr` par défaut** (`AudioPlayer.cpp:28`) et **aucun des deux handlers ne filtre sur
  `canDatabase()`** : 0 occurrence dans `JsonApiHandlerHttp.cpp` comme dans `JsonApiHandlerWS.cpp`
  ; la seule occurrence de `canDatabase()` de tout `JsonApi.cpp` est `:406`, où elle sert
  uniquement à **publier la capacité** dans `get_home`, jamais à garder un appel. Résultat :
  **déréférencement de `nullptr` atteignable depuis l'API** sur un lecteur audio sans base de
  données. **Préexistant, non introduit par T3.17b** — mais c'est ce que son implémenteur a heurté
  en écrivant son fake, et **T3.17c va marcher dessus** puisque sa plage est exactement celle des
  `audioDbGet*`. À traiter comme un ticket propre, **pas** en douce dans une garde de durée de vie.

## T3.17b — divergences gelées

Découvertes en caractérisant les cinq méthodes `audio*` mono-coup
(`tests/core/JsonApiPlayerState_test.cpp`). **Aucune n'est corrigée** — T3.17b est une garde de
durée de vie, il gèle le comportement observable tel quel (les goldens sont **inchangés entre le
commit de caractérisation et le commit de garde**, ce qui le prouve). La politique du harnais
(`tests/core/JsonApiCharacterization.h:159-169`) exige qu'une divergence gelée soit consignée ici
et pas seulement dans l'en-tête du fichier de test.

1. **[API] `get_playlist_size` et `get_time` avalent `audio_action` ; `get_stats` l'émet.** Les
   deux premières ajoutent bien `audio_action` aux params **du player**, puis construisent un
   `Params` **neuf** pour la réponse — si bien que la clé **n'atteint jamais le client**.
   `audioGetDbStats`, lui, renvoie les params du player et l'émet donc. La même famille de
   commandes est ainsi **incohérente sur le fil**, sans qu'aucune erreur ne le signale.
   Épinglé par contraste entre goldens, **et vérifié sur les deux transports** :
   `t317b_ws_audio_get_playlist_size.json` / `t317b_http_audio_get_playlist_size.json` et
   `t317b_ws_audio_get_time.json` / `t317b_http_audio_get_time.json` (clé **absente**) contre
   `t317b_ws_audio_db_get_stats.json` / `t317b_http_audio_db_get_stats.json` (clé **présente**).
   C'est le contraste *entre goldens du même ticket* qui fait le filet — ne pas « harmoniser »
   l'un sur l'autre sans ticket dédié.
2. **[API, cosmétique mais gelé] Faute de frappe de production : `unkown player_id`** (au lieu de
   `unknown`). C'est le message d'erreur rendu au client pour un `player_id` **inconnu**, et il est
   **épinglé tel quel dans 3 goldens** — `t317b_ws_audio_unknown_player.json`,
   `t317b_http_audio_unknown_player.json` et `t317b_ws_audio_db_get_stats_unknown_player.json`
   (les deux familles `audio` et `audio_db` rendent le même message fautif). Le corriger est un
   **changement de contrat visible client** : il faut un ticket et une entrée de notes de version,
   pas une retouche opportuniste. Toute correction future devra régénérer ces 3 goldens.
   ⚠️ **Ne pas confondre avec le `player_id` vide**, qui suit un chemin distinct et répond
   `"empty player id"` — correctement orthographié, épinglé par
   `t317b_ws_audio_empty_player_id.json`. Les deux messages sont différents ; un correctif de la
   faute de frappe ne doit pas les fusionner.

## E4.0c — divergences gelées

Découvertes en caractérisant `get_timerange`, `set_timerange` et les **sept sous-commandes
`autoscenario`** (`tests/core/JsonApiScenario_test.cpp`, 52 cas, 9 goldens). **Aucune n'est
corrigée** : E4.0c est de la caractérisation pure, **zéro ligne de `src/`**. Les sept divergences
ci-dessous ont toutes été **confirmées au source par la revue indépendante**. La politique du
harnais (`tests/core/JsonApiCharacterization.h:159-169`) exige qu'une divergence gelée soit
consignée ici, et pas seulement dans l'en-tête du fichier de test.

1. **[API, contrat cassé] Le payload de `autoscenario get` n'est pas ré-injectable dans
   `modify`.** `buildAutoscenarioModify()` (`JsonApi.cpp:1834`) lit `disabled` (défaut
   **`"true"`**), `name` (défaut **`_("New unnamed scenario")`**), `visible` (défaut `"false"`),
   `room_name` et `room_type`. Or `Scenario::toJson()` (`IO/Scenario.cpp:81-150`) n'émet **aucun**
   de ces cinq champs : il émet `id`, `cycle`, **`enabled`** (la négation de `disabled`, sous un
   autre nom), `schedule`, `category`, `steps_count`, `steps`. Un client qui **renvoie tel quel ce
   qu'il vient de recevoir** renomme donc le scénario en « New unnamed scenario », le rend
   invisible et le désactive — **avec `success:true`**. Ce n'est pas un aller-retour, c'est une
   réinitialisation silencieuse. Gelé tel quel ; le corriger est un changement de contrat visible
   client, donc un ticket dédié avec entrée de notes de version.
2. **[API, perte de données silencieuse] L'index du tableau JSON sert de numéro d'étape.**
   `index_act = idx` dans `buildAutoscenarioCreate()` **et** dans `buildAutoscenarioModify()` : le
   numéro d'étape passé à `addStepAction()` est la position dans le tableau `steps` reçu, alors
   que `addStep()` n'est appelé **que** pour les steps `standard`. Un step `end` placé ailleurs
   qu'en **dernier** décale donc tout ce qui suit : les actions des steps standard suivants sont
   attachées à des indices qui n'existent pas et **disparaissent sans erreur**. Le client reçoit
   `success:true`.
3. **[SEC/API] `autoscenario` n'est pas soumis au `serviceScope`.** `JsonApiHandlerWS.cpp:177-219`
   pose `scopeDenied()` sur `set_param`, `del_param`, `audio_db`, **`set_timerange`**, `eventlog`,
   `register_push` et `settings` — mais **pas** sur `autoscenario`, qui **crée, modifie et
   supprime** des scénarios ainsi que leurs règles associées (`deleteRules()`). Une session de
   scope service, à qui l'on refuse d'écrire une plage horaire, peut donc **détruire des
   scénarios**. L'asymétrie est mesurée, pas déduite.
4. **[API] Silence total sur un `type` d'autoscénario inconnu ou absent, sur les deux
   transports.** La chaîne de `if/else if` n'a **pas d'`else`** (WS `:474-491`, HTTP `:883-896`).
   Côté HTTP c'est pire que côté WS : **aucune réponse et aucune fermeture** — la socket est
   laissée ouverte, le client attend indéfiniment. Le silence est ici un comportement observable
   et il est épinglé comme tel.
5. **[API] Asymétrie WS/HTTP confirmée sur un troisième périmètre** (après `audio_db` et `audio`
   inventoriés par E4.0) : les arguments se lisent **sous `data` en WS et à la racine en HTTP**.
   Pour `autoscenario`, l'argument ainsi déplacé est le **`type` lui-même**, c'est-à-dire le
   sélecteur de sous-commande. La migration jansson → `nlohmann::json` doit préserver les **deux**
   emplacements.
6. **[API] `set_timerange` — trois comportements destructeurs ou permissifs, gelés.**
   (a) **`ranges` absent ⇒ toutes les plages sont effacées** : `o->clear()` est appelé **avant**
   la lecture (`JsonApi.cpp:1629`), et `json_array_foreach` sur un `nullptr` itère zéro fois. Une
   requête qui ne voulait changer que les `months` vide donc l'agenda.
   (b) **`months` plus court que 12 est accepté sans erreur** (zéro-extension implicite), ce qui
   éteint silencieusement les mois manquants.
   (c) **Un `day` hors 1..7 est silencieusement perdu** : sept `if` indépendants, aucun `else`,
   aucune erreur. La plage est simplement ignorée et le client reçoit un succès.
7. **[Events] `type_str` n'est pas le nom de l'enum.** `EventTimeRangeChanged` se sérialise en
   **`timerange_changed`** (`EventManager.cpp:155`). C'est la chaîne du fil qui fait contrat, pas
   l'identifiant C++ ; toute table de correspondance écrite depuis les noms d'enum sera fausse.

**Constat (d) de T3.18, mesuré ici pour la première fois.** `Scenario::toJson()` filtre les
actions par `if (!sa.io) continue;` — dans la boucle des steps standard **et** dans celle du step
`end`. Une étape dont l'IO a disparu (supprimée à chaud) est donc rendue **sans son action et sans
la moindre indication** : elle est **indistinguable d'une étape laissée vide exprès**. Épinglé par
le contraste entre `e40c_ws_autoscenario_get.json` et
`e40c_ws_autoscenario_get_hot_deleted.json`. C'est exactement le point (d) que T3.18 annonce
vouloir traiter en ajoutant `broken` / `disabled_missing_io` / `missing_ios` au payload ; **T3.18
devra régénérer les goldens de scénario** (`CALAOS_GOLDEN_UPDATE=1`), ce que le bloc `# E4.0c` de
`tests/Makefile.am` signale déjà.

⚠️ **Réserve de fond de la revue, à ne pas perdre — même mode de défaillance qu'E4.0b.** Le
relecteur indépendant a démontré **par contre-mutation** que `cycle` et `enabled` n'étaient
**jamais observés en désaccord** dans la première version : `false/false` dans cinq goldens,
`true/true` dans le sixième. **Échanger les deux noms de clés laissait 52/52 vert** — le filet ne
prouvait rien sur cette paire. L'implémenteur a fermé la réserve avec **deux témoins
indépendants**, puis a **balayé tout `Scenario::toJson()`** avec le même critère : `id`↔`schedule`
→ **13 rouges**, `category`↔`steps_count` → **15 rouges**, `step_pause`↔`step_type` → **13
rouges**, action `id`↔`action` → **13 rouges**. **Aucune autre paire aveugle.** Toute évolution
future de ces goldens doit conserver le **désaccord** entre clés symétriques : c'est lui, et non
leur présence, qui épingle le contrat.

## T3.17e — suites

Réserves du relecteur indépendant (verdict **MERGE**, réserves **documentaires uniquement**). Elles
sont conservées ici parce que **ce sont de vraies informations**, pas des remarques de forme : elles
corrigent à la baisse ce que l'audit croyait déjà couvert.

- **[PORTÉE DU FILET — le trou de couverture était SOUS-déclaré] `~HttpClient(){ delete jsonApi; }`
  n'était exercé par AUCUN test, sur AUCUN des deux transports.** L'audit annonçait un trou sur le
  seul transport WS ; il était **total**. `HttpTestRequest` construit un vrai `HttpClient` mais
  **n'installe jamais le handler dans `HttpClient::jsonApi`**, et un `grep -rn jsonApi tests/`
  restreint aux **sources** ne renvoyait, avant T3.17e, **aucune occurrence** (les seules
  correspondances sont dans des binaires compilés, le symbole entrant par les objets de production
  liés). Autrement dit : **l'arête de propriété qui porte toute la démonstration de couverture
  transitive de T3.17** — un propriétaire, une destruction — n'était **prouvée par aucune mesure**,
  seulement par lecture. Tout raisonnement « couvert transitivement via `apiAlive` » écrit avant
  T3.17e reposait donc sur cette lecture seule.
- **[UAF LATENT, inatteignable aujourd'hui — le seul risque propre au niveau de dérivation
  supplémentaire] Fenêtre d'ordre de destruction : `apiAlive` ne la protège pas.** Le sous-objet
  `JsonApi` meurt **strictement après** le sous-objet `WebSocket` (`~WebSocket()` puis, par la base,
  `~HttpClient()` qui fait le `delete jsonApi` de `HttpClient.cpp:162`). Entre les deux, `jsonApi`
  est **vivant au-dessus d'un transport à moitié détruit**, et le jeton `apiAlive` — encore valide
  puisque `~JsonApi()` n'a pas commencé — **ne dit rien de cette fenêtre**. Elle est **inatteignable
  en l'état** : `~JsonApi()` est vide (`JsonApi.cpp:252-254`), `~JsonApiHandlerWS()` ne fait que
  déconnecter (`JsonApiHandlerWS.cpp:40-43`), et `removeWebSocketHandler` n'émet rien sur le mourant
  (`RemoteUIManager.cpp:413-431` → `RemoteUI::setOnline`, `RemoteUI.cpp:356-361`, **sans
  `emitChange()`**). Mais **tout futur `sendData.emit()` depuis un destructeur** y ferait un
  use-after-free via le slot `[=]{ sendTextMessage(data); }` (`WebSocket.cpp:341-344`), qui capture
  implicitement le `WebSocket` **déjà détruit**. À relire avant d'ajouter la moindre émission sur un
  chemin de destruction — c'est une hypothèse de sûreté, pas une propriété garantie.
- **[ANGLES MORTS non déclarés par l'audit]** Trois, à connaître avant de s'appuyer sur le nouveau
  binaire : (1) **`RemoteUIWebSocketHandler` n'est exercé par aucun cas** — le troisième niveau de
  dérivation est raisonné, jamais instancié ; (2) la **branche d'échec d'authentification**
  (`WebSocket.cpp:331`) est **raisonnée mais non épinglée** — aucun test ne la traverse ; (3)
  **`closeConnection` n'est pas câblé** par le `WsTransport` de test alors que la production le câble
  (`WebSocket.cpp:345`) — le filet ne couvre donc que la moitié `sendData` du câblage.
- **[LIMITE DU TEST — il épingle l'invariant, pas le mécanisme]
  `EventRaisedBeforeTheTransportDiesIsNeverDelivered` ne distingue pas les deux mécanismes de
  sûreté.** Retirer le seul `evcon.disconnect()` de `~JsonApiHandlerWS()` **laisserait le cas vert**,
  `sigc::trackable` prenant le relais. Le test prouve donc « l'événement n'est pas livré », **pas**
  « c'est la déconnexion explicite qui l'empêche ». C'est acceptable — l'invariant est ce qui compte
  — mais **ne pas le citer comme preuve que la déconnexion explicite est nécessaire**.

## E4.0d — events : divergences et types morts

Découvertes en caractérisant les **events temps réel** (`tests/core/JsonApiEvents_test.cpp`,
57 cas, 15 goldens) — la surface qui n'avait **aucun test** avant ce ticket. **Rien n'est
corrigé** : E4.0d est de la caractérisation pure, **zéro ligne de `src/`**. La politique du
harnais (`tests/core/JsonApiCharacterization.h:159-169`) exige qu'une divergence gelée soit
consignée ici, et pas seulement dans l'en-tête du fichier de test.

1. **[HARNAIS, cause corrigée] Le backlog d'events ne vient PAS du chargement de la maison.**
   Le commentaire de `loadReferenceHouse()` affirmait depuis E4.0a que le chargement lève un
   `EventIOAdded` par IO, « 5 here ». **Les deux moitiés étaient fausses**, et cinq sous-tickets
   avaient lu cette phrase. Mesuré : `EventIOAdded` a **un seul** site d'émission dans tout
   `src/`, `ListeRoom::createIO()` (`ListeRoom.cpp:466`), qui est le chemin **runtime** de l'API
   JSON ; le chargement de configuration passe par `Room::LoadFromXml()`
   (`Room.cpp:152-175`), qui construit et rattache les IOs **en silence**. Un client connecté
   pendant le boot du serveur ne voit **rien** de la maison qui se construit. De plus la maison de
   référence porte **8** IOs, pas 5. Le vrai backlog vient de l'autre bout du cycle de vie :
   `~Room()` → `RemoveIO()` → `Room.cpp:77` lève un `EventIODeleted` **par IO**, et
   `CoreFixture::TearDown()` détruit les pièces **après** que le fixture a pompé la boucle — ces
   events survivent donc dans le cas **suivant**, où ils sont délivrés à sa première session.
   Épinglé par `LoadingAHouseFromConfigRaisesNoEventAtAll`. Correctif structurel (déplacer le
   `pumpEventLoop()` **après** `CoreFixture::TearDown()`) **ticketé E4.0g**, délibérément non fait
   ici : trois sous-tickets sont en vol sur ce harnais et en changer la sémantique sous eux serait
   pire que le bug. Corrections **de commentaires uniquement** dans
   `JsonApiCharacterization.{h,cpp}` ; le `pumpEventLoop()` n'a **pas** bougé.

2. **[CODE MORT] Cinq des 24 types d'events ne sont jamais émis.** Aucun `EventManager::create()`
   nulle part pour `EventRoomAdded` (**5**), `EventRoomDeleted` (**6**),
   `EventRoomPropertyDelete` (**8**) : ils n'existent que dans l'enum et dans `typeToString()`.
   `EventPushNotification` (**22**) n'existe que comme **étiquette** `HistEvent::event_type`
   posée en base à `ActionPush.cpp:105` — jamais `create()`, donc **jamais poussé sur le fil**
   malgré son `type_str` `push_notif`. Le cinquième est plus vicieux :
   **`EventAudioPlaylistCleared` (**18**) est mort par branche inatteignable.** Son unique site
   (`Squeezebox.cpp:311`) est dans un `else if (p["2"] == "clear")` à `Squeezebox.cpp:306`, mais
   `"clear"` est **déjà consommé** par la branche `Squeezebox.cpp:290`
   (`loadtracks || clear || play || load`), qui émet `EventAudioPlaylistReload`. **Un « playlist
   clear » rapporte donc `playlist_reload`, jamais `playlist_cleared`.** Gelé tel quel.

3. **[API, piège client] 11 des 23 `type_str` ne se déduisent pas du nom de la constante.**
   Un client qui génère ses noms depuis l'enum se trompe sur presque la moitié :
   `EventTimeRangeChanged` → **`timerange_changed`** (pas `time_range_changed`) ; les **cinq**
   `EventAudioPlaylist*` **perdent le préfixe `audio`** et trois d'entre eux gagnent `tracks_`
   (`playlist_tracks_added`, `playlist_tracks_deleted`, `playlist_tracks_moved`,
   `playlist_reload`, `playlist_cleared`) ; `EventTouchScreenCamera` →
   **`touchscreen_camera_request`** (suffixe ajouté) ; `EventPushNotification` → **`push_notif`**
   (tronqué) ; les deux `*PropertyDelete` → **`io_prop_deleted`** / **`room_prop_deleted`**
   (abrégé *et* conjugué) ; et le défaut porte une **faute de frappe** : `EventUnkown` → chaîne
   **`"unkown"`** (`EventManager.cpp`, branche `default`). La faute est **gelée** : elle est
   observable par les clients depuis toujours.

4. **[PROTOCOLE] La numérotation de l'enum fait partie du protocole de fil.** L'enveloppe d'event
   porte `type` avec la **valeur ordinale brute** de l'enum (`"3"` pour `io_changed`), à côté de
   `type_str`. Or `CalaosEvent::EventType` (`EventManager.h`) n'est numéroté **que** sur son
   premier membre (`EventUnkown = 0`) : tous les autres sont implicites. **Insérer une valeur au
   milieu décale silencieusement tout ce qui suit** pour tout client qui lit `type`. Les **24**
   valeurs (0 à 23) sont donc épinglées une par une par le golden
   `e40d_ws_event_catalog.json` : toute réorganisation de l'enum casse le test, ce qui est
   exactement l'intention.

5. **[SÉCURITÉ — gelé, non corrigé, mérite son ticket] Une session `serviceScope` reçoit TOUS les
   events de la maison.** `JsonApiHandlerWS::handleEvents()` (`JsonApiHandlerWS.cpp:53-60`) ne
   teste **que** `loggedin` et **jamais** `serviceScope`, alors que ce même drapeau est consulté
   sur **7** commandes du chemin requête/réponse : `set_param`, `del_param`, `audio_db`,
   `set_timerange`, `eventlog`, `register_push`, `settings` (`JsonApiHandlerWS.cpp:177-219`). Une
   session sidecar MCP à qui l'on **refuse** de lire les paramètres, la base audio ou le journal
   d'événements **reçoit malgré tout le flux temps réel complet** — ids d'IO et valeurs d'état
   compris, donc l'essentiel de ce que le refus était censé protéger. Le cloisonnement n'est
   appliqué que sur la moitié requête/réponse de l'API, pas sur la moitié push. **Gelé** :
   E4.0d est de la caractérisation, et corriger ceci change un comportement visible client.

6. **[LIMITE DE PORTÉE ASSUMÉE] Les payloads audio et `io_status_changed` ne sont PAS
   opposables.** Rien n'appelle `Squeezebox`, `RoonPlayer` ni `MqttCtrl` dans la suite — ~~ces
   objets ne sont même pas liés au binaire~~ ⛔ **FAUX pour `RoonPlayer`, voir la CORRECTION en
   fin de puce**. Les **8** payloads audio et `io_status_changed` sont
   donc épinglés depuis des events **fabriqués à la main**, ce qui ne prouve rien de la forme que
   la production émet réellement. Quatre payloads audio du golden ont d'ailleurs été **corrigés**
   dans ce ticket parce qu'ils gelaient des formes que la production n'émet pas : les corriger ne
   les rend **pas** opposables pour autant ; ce que ça achète, c'est que le golden **cesse
   d'affirmer une forme fausse**. Ce qui **est** opposable : l'enveloppe, la numérotation,
   l'encodage (UTF-8 accentué) et la stringification pour les **19** types atteignables, et les
   **formes de payload** pour les **7** types réellement déclenchés par du code de production
   traversé par la suite.

   ### ⛔ CORRECTION (2026-08-25, T3.27) — `RoonPlayer.o` EST lié, et il l'était dans ce commit

   ⛔ « ces objets ne sont même pas liés au binaire » est **faux pour `RoonPlayer`**, et il l'était
   **dans le commit E4.0d lui-même**. Mesuré en `python3` (`_SOURCES`/`_LDADD`/`_DEPENDENCIES` de
   `tests/Makefile.am` aplatis, variables Make développées, continuations `\` recollées) :

   | objet | `d68e59f1` (E4.0d) | `fb9d064c` (master) | verdict de la déclaration |
   |---|---|---|---|
   | `Audio/RoonPlayer.$(OBJEXT)` | **8** cibles, dont **`core_JsonApiEvents_test` lui-même** | **17** cibles | ⛔ **FAUSSE** |
   | `Audio/Squeezebox.$(OBJEXT)` | **0** | **0** | ✅ vraie |
   | `IO/Mqtt/MqttCtrl.$(OBJEXT)` | **0** | **1** (`JsonPathSyntax_test`) | ✅ vraie **à la date**, **périmée depuis** |

   ⭐ **Ce qui reste vrai, et c'est l'essentiel de la puce** : *rien n'APPELLE* ces trois classes
   dans la suite. Les 8 payloads audio et `io_status_changed` restent **non opposables**, et la
   conclusion de la puce **tient**. ⛔ **Mais la RAISON invoquée était fausse** : ce n'est pas que
   l'objet manque au binaire, c'est qu'**aucun chemin de la suite ne l'exécute** alors qu'il est là,
   à portée d'un `#include` et d'un cas. La distinction est opérationnelle : « non lié » se lit
   comme *irréductible* et clôt la discussion ; « lié mais jamais exécuté » se lit comme *un cas à
   écrire*, et c'est ce qu'il faut lire ici.

   ⚠️ **Erreur non isolée** — voir **F-LINK-1**, la dette méthodologique transverse de ces
   déclarations. Corrigé aussi dans `ORCHESTRATION.md` (journal E4.0d).

7. **[ÉCART DOC/CODE] `docs/08_http_api.md` décrit une enveloppe d'event qui n'est jamais
   émise.** Le bloc `docs/08_http_api.md:210-221` montre un objet **plat**
   `{"type": "io_changed", "id": ..., "value": ...}`. Le fil porte en réalité
   `{"msg": "event", "data": {"type": "<ordinal>", "type_str": ..., "event_raw": ...,
   "data": {...}}}` — **`data` imbriqué dans `data`**, `type` numérique à côté de `type_str`, et
   ni `id` ni `value` à la racine. La liste `:224-231` contient **14 noms réels + 1 fantôme**
   (`push_notification`, alors que le code dit `push_notif`) et **omet 9 des 23** types
   (`io_prop_deleted`, `room_prop_deleted`, les 5 `playlist_*`, `touchscreen_camera_request`,
   `push_notif`). `docs/10_events_notifications.md` ne documente, lui, **aucune** enveloppe de
   fil. ⚠️ **Ne pas corriger ces deux documents ici : E4.0f est en train de le faire** — signalé
   pour éviter le double travail et un conflit inutile.

## T3.17c — suites

Réserves du relecteur indépendant (verdict **MERGE AVEC RÉSERVES**, réserves **fermées** avant ce
merge) et bizarrerie gelée rencontrée en caractérisant les 15 `audioDbGet*`.

- **[FIXTURE PAUVRE — corrigé dans ce ticket ; 78/78 vert ne prouvait presque rien sur la
  pagination] Le macro `DB_METHOD_CASES` n'assérait pas `db->calls[0].nb`.** Il vérifiait le nom de
  la méthode de base appelée, `from` et l'id, mais **pas la taille de page**. La revue a mesuré le
  trou en injectant un **échange de clés** dans le parsing de requête — faire lire `"from"` à
  `audioDbGetAlbums` là où il doit lire `"count"` — et la suite est restée **78/78 verte**.
  Conséquence : **13 des 14 méthodes de liste pouvaient paginer avec la mauvaise taille de page**
  sans qu'un seul test ne bronche. Corrigé par un argument **`expNb`**, assérté **sur les deux
  transports** (`tests/core/JsonApiMusicDb_test.cpp:468` pour WS, `:485` pour HTTP) — le cas HTTP ne
  vérifiait jusque-là que le nom de la méthode, or **le parsing de requête est du code par méthode
  et par transport : un transport ne prouve rien sur l'autre**.
  ⚠️ **Contrainte de maintenance, à respecter dans toute évolution de la table** : `from` et `count`
  doivent rester **deux nombres différents** (aujourd'hui `2` et `7`, `tests/core/JsonApiMusicDb_test.cpp:522-535`).
  Les rendre égaux — par exemple en « harmonisant » la table — ferait passer les deux assertions
  même avec les deux clés interverties, et **la couverture disparaîtrait en silence**, sans qu'aucun
  test ne devienne rouge.
- **[DIVERGENCE GELÉE] `processDbResult()` recopie le `Params` marqueur de `count` dans `items`.**
  `JsonApi.cpp:1079-1101` parcourt `data.vparams`, retient `scount` quand un `Params` porte la clé
  `count`, puis fait `json_array_append_new(aret, p.toJson())` **sur tous les `Params`, marqueur
  compris** — il n'y a **aucune exclusion**. Le marqueur ressort donc **à la fois** comme
  `total_count` et comme un élément de `items`. Épinglé tel quel par les goldens `t317c_*` ; **non
  corrigé** : c'est la forme de réponse que les clients reçoivent aujourd'hui, et l'invariant T3.17
  interdit de la changer dans un ticket de garde.
  ⚠️ **Nuance mesurée par E4.0f, à ne pas écraser** : `processDbResult()` **ne réordonne jamais** —
  il préserve l'ordre de `data.vparams`. **`items[0]` n'est donc le marqueur que sur les chemins où
  la source l'émet en premier**, ce qui n'est pas général. `SqueezeboxDB::parseListAnswer()` traite
  `count` comme un **séparateur d'enregistrement** au même titre que `id`
  (`Audio/SqueezeboxDB.cpp:66-73`), sa position dépend donc du flux renvoyé par le serveur ; et
  `getRandoms()` ajoute le marqueur **en dernier** (`Audio/SqueezeboxDB.cpp:774-775`). **Ne pas
  réécrire cette entrée sous la forme « `items[0]` vaut toujours `{"count":"N"}` » : c'est faux.**

## E4.0e — session et chemins d'erreur

Caractérisation de tout ce qui n'est pas un payload de données : login, `login_service`,
`settings/change_cred`, `register_push`, `get_mcp_info`, `eventlog`, `config/get`, et **tous** les
chemins d'erreur des deux transports. **102 cas, 29 goldens, zéro ligne de `src/`.** Relu par un
relecteur indépendant (verdict **MERGE AVEC RÉSERVES**, réserve **fermée et prouvée par mutation**).

- **[CRASH — mérite son ticket] SIGFPE distant sur `eventlog`, déclenchable par tout client
  authentifié, sur les deux transports.** `JsonApi.cpp:2010` initialise `perPage = 100`, puis
  `:2013` appelle `Utils::from_string()` **dont le code de retour est ignoré**
  (`StringUtils.h:105-111`). Sémantique C++11 vérifiée empiriquement, et elle n'est pas celle qu'on
  suppose : chaîne **vide** → le sentry de l'`istream` échoue **avant** `num_get`, la valeur 100
  survit ; **non numérique** (`"abc"`, `"1,5"`, `"true"`) ou **`"0"`** → `num_get` s'exécute, échoue
  et **écrit 0** dans la destination ; **très grand** → **sature à `INT_MAX`**, donc inoffensif.
  `HistLogger::getEvents()` ne clampe pas, et `HistLogger.cpp:268` calcule
  `rowcount / ac->per_page` **dans le thread worker sqlite** → **division entière par zéro, SIGFPE,
  processus mort**. Le `try` de `:257` n'attrape rien : **un signal n'est pas une exception**.
  Requête suffisante : `?action=eventlog&per_page=0`. **Non exercé délibérément** — le signal
  tuerait le binaire de test ; le mécanisme sous-jacent est épinglé par
  `FromStringWritesZeroOnFailureWhichIsWhyEventLogCanDivideByZero`.
- **[PLAN FAUX — corrigé] La ligne UTF-8 de `E4.0.md` était fausse dans ses DEUX colonnes.** Elle
  annonçait « `json_dumps` renvoie `NULL` → HTTP **500** + fermeture » et, côté WS, « chaîne vide ».
  Les deux branches sont **du code mort** : jansson refuse les octets **à la construction**
  (`json_string()` → `NULL`, `json_object_set_new()` → `-1` sur valeur nulle **et** sur clé
  invalide) et **aucun de ces codes de retour n'est testé** (`JsonApi.cpp`, `Params::toJson()` en
  `src/lib/Params.cpp:134-147`) ; la paire est **silencieusement supprimée**, le conteneur reste
  bien formé et le dump réussit. Le comportement réel est donc **200 OK tronqué** sur HTTP et, sur
  WS, **une enveloppe parfaitement formée à laquelle il manque un membre** — plus insidieux que la
  chaîne vide annoncée, car **rien ne signale l'absence**. À la bascule, **l'inverse et pire** :
  nlohmann accepte ces octets dans l'arbre et **lève `type_error.316` depuis `dump()`**, les deux
  `sendJson` dumpent à nu et **aucun des deux fichiers de handler ne contient un seul `try` ou
  `catch`** → **`std::terminate` sur une connexion vivante**. Le canal d'injection est trivial :
  `HfURISyntax::getQuery()` percent-**décode** (`hef_uri_syntax.cpp:363-368`) **avant** le découpage
  de `HttpClient.cpp:340-347`, donc `?param=%ff%80x` met des octets arbitraires en **clé** via
  `buildJsonGetParam()`. Détail complet et arbitrage attendu de E4.1 : voir la ligne corrigée du
  tableau des pièges de bascule de [`E4.0.md`](E4.0.md).
- **[FIXTURE PAUVRE — trouvée par la revue, corrigée dans ce ticket] `id` et `created_at` de
  `HistEvent::toJson()` n'étaient assérés que par `is_string()`.** Les deux sont des chaînes non
  déterministes, donc **interchangeables** : la revue a **échangé les deux valeurs** et la suite est
  restée **102/102 verte** — alors que `HistEvent::toJson()` est **réécrit en bloc par E4.1**.
  Corrigé par **rétention des uuids semés** puis assertion **dans les deux sens** : `id` porte l'un
  des uuids, `created_at` n'en porte aucun, et seul `created_at` a la forme d'un timestamp sqlite.
  L'échange produit désormais **6 assertions rouges nommées**.
- **[PIÈGE DE HARNAIS] Le singleton `HistLogger` capture son chemin de base dans son constructeur et
  ouvre le fichier dans un thread worker.** `Utils::getCacheFile("events.db")` est figé à la
  construction, et `sqlite::database db(dbname)` (`HistLogger.cpp:190`) est **hors** du `try` de
  `:192` : un `cantopen` est donc un **throw non rattrapé dans un thread** → `std::terminate`.
  Résolu par `ensureHistLogger()` : répertoire à **durée de vie processus** créé **avant** `SetUp()`,
  avec un aller-retour synchrone qui **prouve** que le worker a ouvert la base **avant** que le
  chemin de cache ne change.
- **[CORROBORATION DE T3.17f] L'UAF de T3.17f reproduit en crash vivant.** En montant ses mutations,
  le relecteur a fait **segfauter le binaire** en retirant la garde de portée d'`eventlog` : session
  détruite avec le callback `HistLogger` **en vol**. Ce n'est plus une lecture de code, c'est un
  crash observé.

## E4.0f — audio, doc d'API et décomptes corrigés

Dernier sous-ticket de **caractérisation** de la série E4.0 (E4.0g, correctif structurel du
harnais, reste ouvert). 24 cas, 20 goldens, **zéro ligne de `src/`**. Ce ticket réécrit en outre
`docs/08_http_api.md` (298 → 915 l.) et `docs/10_events_notifications.md` (153 → 465 l.) contre
le code : chaque exemple de payload y est marqué **capturé** (tracé jusqu'à un golden) ou
**dérivé** (tracé jusqu'à un builder), et rien d'autre n'est autorisé.

### Trois corrections de fait apportées à `E4.0.md`

Ce ne sont pas des changements de comportement : ce sont des affirmations du document de cadrage
qui étaient **fausses**, mesurées ici et corrigées à la source. Elles sont listées pour que
personne ne les « recorrige » dans l'autre sens.

- **18 types d'events émis, pas 19.** `EventAudioPlaylistCleared` est **inatteignable** :
  `Audio/Squeezebox.cpp:290` teste `p["2"] == "loadtracks" || p["2"] == "clear" || p["2"] == "play"
  || p["2"] == "load"` et émet `EventAudioPlaylistReload` ; la branche `else if (p["2"] == "clear")`
  de `:306`/`:311`, qui seule émettrait `playlist_cleared`, est **masquée par la chaîne `else if`
  antérieure** — code mort. Nuance à conserver : `EventPushNotification` n'est pas non plus poussé
  en temps réel, mais pour une **autre raison** — il ne passe **jamais** par `EventManager::create()`.
  `Rules/ActionPush.cpp:105` écrit un `HistEvent` **directement en base**
  (`e.event_type = EventPushNotification`, `e.event_raw = data.dump()` `:111-115`). Son `type_str`
  est donc bien observable par un client, mais **uniquement via `eventlog`**.
  Recoupe : 23 types réels − 3 morts − `playlist_cleared` − `push_notification` = **18**.

- **4 opérations renvoient des octets bruts, pas 6.** `get_cover` **de premier niveau** et
  `get_camera_pic` ne renvoient **pas** d'octets : les deux finissent dans
  `JsonApiHandlerHttp::exeFinished()` (`JsonApiHandlerHttp.cpp:574-591`), qui répond un **objet
  JSON** `{"success":"true","contenttype":"image/jpeg","encoding":"base64","data":…}`. Octets bruts
  `image/jpeg` uniquement pour : `audio/get_cover` (sous-action), `camera/get_picture`,
  `camera/get_video`, `event_picture`.

- **`items[0]` n'est PAS toujours `{"count":"N"}`.** `processDbResult()` (`JsonApi.cpp:984-1006`)
  ne réordonne **jamais** ce que lui remet la couche audio, et cette couche place le marqueur où
  ça l'arrange : `SqueezeboxDB::getAlbums_cb()` (`Audio/SqueezeboxDB.cpp:66-73`) traite `count:`
  comme un **séparateur d'enregistrement**, exactement comme `id:` — il peut donc tomber
  n'importe où dans la liste ; et `getRandoms()` (`:750-776`) l'ajoute **en dernier**
  (`p.Add("count", …); result.push_back(p);` après les quatre entrées). L'affirmation générale
  « le premier élément porte le compte » ne doit pas être réintroduite. Le contrat réellement
  gelé est : **le dernier marqueur rencontré gagne**, la ligne porteuse **conserve ses autres
  clés**, et l'absence totale de marqueur signifie **absence de la clé `total_count`**.

### Divergences et bugs gelés (non corrigés)

- **[BUG] `time_elapsed` perd de la précision sur le fil.** `Utils::to_string()`
  (`src/lib/StringUtils.h:112-118`) est un `std::ostringstream` **nu** : aucun `setprecision`,
  aucun `fixed`. Sur un `double` cela donne les **6 chiffres significatifs** par défaut, puis la
  **notation scientifique**. Conséquences mesurées, épinglées par deux goldens
  (`e40f_ws_audio_time_six_significant_digits.json`,
  `e40f_ws_audio_time_large_value_goes_scientific.json`) :
  `1234.56789` part sur le fil en `"1234.57"` — **3 décimales perdues** ; et `123456789.0` part en
  `"1.23457e+08"`, qu'un `parseInt` naïf côté client lit **`1`**. C'est un comportement livré
  aujourd'hui ; il est **gelé, pas réparé** (invariant de la série).

- **[BUG] `/api/v2` et `/api/v3*` passent le filtre de chemin sans handler.**
  `HttpClient.cpp:458-461` laisse passer `/api`, `/api.php`, `/api/v2` et tout ce qui commence par
  `/api/v3` ; mais plus bas, `:653-659`, seul `proto_ver == API_HTTP` instancie un
  `JsonApiHandlerHttp` — l'`else` se contente de
  `cWarningDom("network") << "API version not implemented"; return;`. Le serveur **n'envoie donc
  rien du tout** : pas de 404, pas de 501, pas de fermeture. La connexion est **laissée en
  suspens** jusqu'au timeout du client.

### Pièges pour les clients (documentés dans les deux documents réécrits)

- **[PIÈGE CLIENT] `event_raw` porte trois formes incompatibles sous le même nom de clé.** Un
  client qui écrit un seul parseur pour cette clé se casse :
  1. **chaîne plate url-encodée** — events temps réel, `EventManager.cpp:190`
     (`"event_raw", toString().c_str()` dans le `json_pack`) ; ce n'est **pas** du JSON ;
  2. **objet JSON imbriqué** — `eventlog`, `HistLogger.cpp:93-100` fait
     `j["event_raw"] = Json::parse(event_raw)` (et retombe sur `Json::object()` si le parse échoue) ;
  3. **`{message, pic_uid}`** — `Rules/ActionPush.cpp:111-115` ; sous-cas du second : c'est le
     contenu **stocké** par `ActionPush`, ressorti tel quel par le conteneur d'`eventlog`.

- **[PIÈGE CLIENT] `steps_count` ≠ longueur du tableau `steps`.** `IO/Scenario.cpp:95` émet
  `getRuleSteps().size()`, c'est-à-dire **les seules étapes réelles**, tandis que l'étape
  synthétique `step_type:"end"` est ajoutée **hors de la boucle** (`:125-142`).
  **Invariant : `len(steps) == steps_count + 1`, toujours.** Un client qui dimensionne son tableau
  sur `steps_count` **tronque silencieusement les actions de sortie** du scénario.
  ⚠️ **Contraste à garder en tête** : dans `get_playlist`, `count` **est** bien la longueur du
  tableau ; et le `total_count` d'`audio_db` est un compte **fourni par la base**, sans rapport
  garanti avec la longueur de `items`. **Trois champs de comptage, trois sémantiques.**

### Fixture pauvre trouvée par la revue

Les 7 cas `processDbResult()` amorçaient **tous** un marqueur de count, chacun à un endroit
différent — de sorte que remplacer `if (!scount.empty())` par `if (true)` dans le builder laissait
la suite **62/62 verte**. Le contrat « aucun `count` nulle part → **aucune** clé `total_count` »
n'était donc épinglé par rien, alors qu'il est **publié aux clients** dans `08_http_api.md`.
Comblé par `NoCountAnywhereMeansNoTotalCountKeyAtAll`, plus les deux cas de rejet croisé
`get_albums`/`get_album` (chaque transport rejette l'orthographe de l'autre, dans les deux sens).
Leçon générale : **une fixture qui amorce toujours la précondition ne teste jamais son absence.**

### L'ancien `10_events_notifications.md` était factuellement faux

- **Configuration mail inventée.** Les vraies clés sont `notif/mail_sender`,
  `notif/mail_recipients` et `smtp_debug`, consommées par le binaire **hors-processus**
  `calaos_mail` — pas par `calaos_server`. Le document décrivait d'autres clés.
- **L'exemple XML d'`ActionPush` était inventé** : il ne correspondait à aucune forme que le
  parseur de règles accepte. Remplacé par une forme tracée au code.

## T3.17f — suites

Dernier sous-ticket de la série T3.17 (`eventlog`, UAF non gardé sur les deux transports). Ce qui
suit est **réutilisable au-delà du ticket** : les trois premiers points sont des acquis de méthode.

### La couverture transitive est MESURÉE, pas raisonnée — première fois de la série

Toute la série T3.17 s'appuyait sur un raisonnement de **propriété** : les deux handlers dérivent de
`JsonApi`, base unique non virtuelle, un seul propriétaire (`HttpClient::jsonApi`), une seule
destruction (`HttpClient.cpp:162`) — donc une garde posée dans `JsonApi.cpp` couvre les deux
transports. T3.17e avait déjà signalé que cette arête de propriété **n'était prouvée par aucune
mesure**, seulement par lecture (cf. § *T3.17e — suites*, premier point). **T3.17f la mesure.**

La trace ASan du chemin **HTTP** dit, en toutes lettres, que :

- le bloc libéré **est l'objet `JsonApiHandlerHttp` lui-même** ;
- il est libéré par **son propre destructeur**, `~JsonApiHandlerHttp()` (`JsonApiHandlerHttp.cpp:53`) ;
- la lecture fautive tombe à **+48 octets dans ce même bloc** — c'est-à-dire **dans le sous-objet de
  base `JsonApi`**, là où vit `apiAlive`.

**Conséquence à retenir, et c'est la formulation générale de l'acquis : une garde écrite uniquement
dans `JsonApi.cpp` neutralise un use-after-free dont le site de libération est dans un fichier
jamais ouvert par le correctif.** Le bloc libéré et le membre qui sert de jeton sont **le même
bloc** — ce n'est plus une inférence sur l'ownership, c'est une coïncidence d'adresses observée.
Tout futur ticket de cette famille peut s'appuyer là-dessus **sans réédifier la démonstration**,
et T3.17f n'a effectivement édité **aucun handler**.

**Corollaire — ce bug valide rétrospectivement la retraite de l'ancien critère de diagnostic.**
Aucun des deux callbacks de `buildJsonEventLog` n'odr-use `this` : **aucun avertissement
`-Wdeprecated` n'a jamais pointé ici**, et aucun inventaire de `JsonApi.cpp` fondé sur ces
avertissements ne pouvait le trouver. Le détail complet — l'unique avertissement de la famille est
au **mauvais fichier** et ne couvre qu'**un transport sur deux** — est dans
`T3.17.md`, § *L'argument décisif, découvert par T3.17f*.

### La sentinelle d'ordonnancement — validée sur DEUX étages par la revue

Les cas de durée de vie (« le handler meurt pendant que sqlite travaille ») ont besoin d'une
certitude que le harnais ne donne pas gratuitement : **le callback en vol a bien été dépêché**.
Sans elle, un cas « rien n'est émis » passerait **même si le callback n'était jamais tiré** — la
suite serait verte pour la mauvaise raison, et un correctif retiré ne la ferait pas rougir.

La sentinelle est une **seconde requête**, postée après celle sous test, dont l'arrivée prouve que
la première a déjà été traitée. Sa validité repose sur **deux mécanismes distincts**, tous deux
vérifiés :

1. **Côté worker** — `HistLogger` consomme `eventQueue` avec un `ThreadedQueue` à **consommateur
   unique**, en **FIFO strict** : la requête sous test est exécutée par le thread sqlite **avant**
   celle de la sentinelle.
2. **Côté boucle** — chaque action alloue **son propre** `uvw::AsyncHandle`
   (`HistLogger.cpp:133` et `:150`, `uvw::Loop::getDefault()->resource<uvw::AsyncHandle>()`), et
   `resource()` fait un **`QUEUE_INSERT_TAIL`** à la création. `uv__async_io` dépêche donc dans
   **l'ordre d'insertion** : le handle en vol passe **avant** celui de la sentinelle **même si les
   deux `send()` tombent dans le même tour de boucle**.

C'est le second étage qui est le point non évident, et il est indispensable : le FIFO du worker
seul ne dit rien de l'ordre de **livraison** côté boucle si les deux réveils se groupent.

### Septième récidive du « fixture pauvre », comblée

Même famille que celles d'E4.0c/E4.0f, et **septième occurrence** : la fixture amorçait toujours la
même précondition, donc n'en testait jamais la variation. Ici, **aucune requête ne demandait jamais
une page valide ≠ 0** — le seul cas non nul, `page="9"`, part **en erreur « page is out of range »
avant même la construction du document**. Mesuré par mutation : figer l'écho `page`, figer l'écho
`per_page` **et** annuler l'offset `int start = ac->page * ac->per_page;` (`HistLogger.cpp:269`)
laissaient la suite **19/19 verte**, les trois mutations à la fois.

Comblé par un **seul** cas, `TheSecondPageEchoesItsOwnCoordinatesAndCarriesTheSecondSlice`, qui
vérifie la tranche **par provenance** (quels événements précisément, pas seulement combien) —
ce qui tue les trois mutations d'un coup. **Leçon, la même qu'en E4.0f** : une fixture qui n'exerce
jamais qu'une seule valeur d'un paramètre ne teste pas ce paramètre, elle teste une constante.

### ⚠️ Le piège des TROIS jetons — ce n'est pas un usage hors fichier

Formulation corrigée en revue **après vérification**, à ne pas réécrire dans l'autre sens :
`apiAlive` n'apparaît, dans **tout `src/`**, que dans **deux fichiers** — `JsonApi.h` (la
déclaration) et `JsonApi.cpp` (**24 occurrences = 24 méthodes gardées** sur les 26 méthodes
`std::function` du fichier ; les deux restantes, `buildJsonStates()` et `buildQuery()`, sont
**synchrones** et n'en ont pas besoin). **Zéro usage de production hors de ces deux fichiers** (le
symbole n'apparaît ailleurs que dans des commentaires de tests et des binaires compilés). Il n'y a
donc **aucun** problème de portée à surveiller.

Le vrai piège pour le prochain lecteur est la **confusion de trois jetons homonymes en rôle**,
appartenant à trois niveaux d'objet :

| jeton | déclaré | utilisé par |
|---|---|---|
| `JsonApi::apiAlive` | `JsonApi.h` | les 24 callbacks async de `JsonApi.cpp`, **et rien d'autre** |
| `JsonApiHandlerHttp::handlerAlive` | `JsonApiHandlerHttp.h:64` | les 5 callbacks gardés de `JsonApiHandlerHttp.cpp` (`:462`, `:727`, `:936`, `:1057`, `:1075` — `get_cover` deux fois, les instantanés caméra, le ré-armement `singleShot`) |
| celui de `RemoteUIWebSocketHandler` | son propre en-tête | ses propres callbacks |

**Règle** : un callback doit prendre le jeton de **l'objet dont il touchera les membres**, pas
celui qui est « à portée ». Voir aussi le commentaire de `JsonApi.h` au-dessus d'`apiAlive`, qui
porte la même mise en garde au point d'usage.

### Non exercé délibérément — SIGFPE sur `per_page` (division par zéro, à distance)

`buildJsonEventLog` lit `per_page` du client (`JsonApi.cpp:2107`) et le passe tel quel à
`HistLogger`, qui fait `rowcount / ac->per_page` (`HistLogger.cpp:268`) **sans contrôle de
nullité**. Un client authentifié qui envoie `per_page: "0"` provoque donc un **SIGFPE**, sur les
deux transports.

**Aucun cas de T3.17f ne l'exerce, et c'est volontaire : il tuerait le binaire de test.** Tous les
cas envoient un `per_page` numérique non nul. Le défaut est **hors périmètre d'un ticket de garde
de durée de vie** — c'est un défaut de **validation d'entrée**, de la même famille que **T3.19**
(plantage à distance atteignable depuis l'API, aujourd'hui cadré sur le seul `audio_db`). **À
rattacher à T3.19 ou à ticketer à côté ; ne pas le laisser se perdre ici.**

## E4.0g — clôture du harnais

Dernier maillon de la série E4.0. **Zéro ligne de `src/`** : ce ticket ne corrige aucun défaut de
production, il change la **sémantique du cycle de vie** du harnais de caractérisation sous les
binaires qui le partagent. Le contrôle qui l'atteste n'est pas `make check` mais `--gtest_shuffle`.

### La vraie cause, définitivement

`EventIOAdded` n'a **qu'un seul site de création** : `ListeRoom::createIO()`
(`src/bin/calaos_server/ListeRoom.cpp:466`), le chemin **runtime** de l'API JSON.
`Room::LoadFromXml()` est **muet** — il construit et attache les IOs sans lever quoi que ce soit.
**Charger une maison ne produit donc aucun event.**

Le backlog venait de l'**autre bout** du cycle de vie : `CoreFixture::TearDown()` appelle
`clearCoreState()`, qui détruit les pièces ; `~Room()` appelle `RemoveIO()` par IO et chacun lève
un `EventIODeleted` (`src/bin/calaos_server/Room.cpp:77`). La maison de référence porte **8** IOs.
Ces events étaient produits **par le teardown parent lui-même**, donc **après** l'unique drain du
fixture, qui pompait **avant** d'appeler `CoreFixture::TearDown()`. Ils survivaient dans l'idler de
l'`EventManager` et étaient livrés à la **première session du cas suivant** — y compris à une
session créée après coup, `newEvent` étant émis au moment du **flush**, pas de la **mise en file**.

Le correctif tient en l'**ordre de deux instructions** : `CoreFixture::TearDown()` d'abord,
`pumpEventLoop()` ensuite.

### Finding de série — deux rustines sur cinq n'ont jamais rien absorbé

Cinq rustines s'étaient accumulées contre cette fuite. Mesurées **une par une**, en restaurant
l'ancien ordre et en faisant varier les graines, seules **trois** portaient quelque chose :

| rustine | emplacement | avec le défaut présent |
|---|---|---|
| `JsonApiEvents_test` | fin de `SetUp()` | rouge 3/3 graines |
| `JsonApiSession_test` | milieu de `SetUp()` | rouge 2/3 graines |
| `JsonApiWsTransport_test` | fin de `SetUp()` | rouge 14/15 graines |
| `JsonApiAudioPayload_test` | fin de `SetUp()` | **0 rouge sur 18 graines** |
| `JsonApiHome_test` | tête d'un corps de cas | **0 rouge sur 18 graines** |

Les deux dernières avaient été ajoutées **par mimétisme**, à partir du diagnostic faux « le
chargement lève un `EventIOAdded` par IO » que **E4.0d a réfuté**. C'est la **trace visible d'une
fausse explication ayant circulé six sous-tickets durant** : personne ne les avait mesurées, elles
ont été recopiées du voisin en même temps que sa justification erronée.

### Une **sixième** rustine, apparue pendant la revue (T3.17f)

`JsonApiEventLog_test` **n'existait pas** à la base de rebase d'E4.0g : **T3.17f l'a créé pendant
la revue**, contre l'**ancienne** sémantique, et lui a donné un `pumpEventLoop()` en fin de
`SetUp()` commenté comme **« workaround mandatory »** en **citant E4.0g** comme défaut connu. Après
merge, ce commentaire était **faux** et le pompage **mort**. Mesuré : **vert 8/8 graines sans lui**
— inerte, comme `AudioPayload` et `Home`. Retiré dans un commit séparé.

**Le nombre de consommateurs du harnais est donc passé de 11 à 12 pendant la revue**, et le
douzième est précisément celui que l'implémenteur ne pouvait pas avoir mesuré. C'est le mode de
défaillance à retenir : *un ticket qui change une sémantique partagée peut voir un nouveau
consommateur apparaître sous lui pendant sa propre revue.* Vérifier la liste des consommateurs
**au moment du merge**, pas au moment de la mesure.

### Un vert à vide, découvert en retirant le drain mort — le point le plus instructif

`LoadingAHouseFromConfigRaisesNoEventAtAll` crée sa `WsTestSession` **avant** le chargement et
s'appuyait sur le pompage interne de `loadReferenceHouse()` pour être vidée. Une fois ce pompage
retiré — il n'absorbait plus qu'un backlog désormais inexistant — il ne restait **aucun pompage**,
et `EXPECT_EQ(0u, ws.count())` passait **parce que rien n'était livré, pas parce que rien n'était
levé**. Le cas serait resté **vert même si le chargement s'était mis à lever des events** : son
oracle ne mesurait plus rien.

Corrigé par un pompage **explicite dans le cas**, documenté comme porteur. Tous les appelants de
`loadReferenceHouse()` ont été balayés : **c'est le seul cas avec un puits d'events vivant avant le
chargement**.

> **Règle générale** : une assertion d'**absence** n'a de valeur que si le canal a été **flushé**.
> *Non livré n'est pas non levé.* Un `EXPECT_EQ(0, ...)` sans pompage en amont est un oracle mort.

### Le contrat désormais posé dans l'en-tête

Écrit sur `JsonApiCharacterizationTest::TearDown()` (`tests/core/JsonApiCharacterization.h`) :

> **Un sous-ticket hérite d'une file vide.** N'ajoutez pas de pompage défensif à votre `SetUp()`.
> Si vous croyez en avoir besoin, c'est qu'une **seconde source** d'events survit à `TearDown()` :
> **nommez-la dans `FINDINGS.md`**, ne la pompez pas.

Le harnais ne s'exempte pas de sa propre règle : c'est pourquoi le drain devenu mort de
`loadReferenceHouse()` a été **retiré** et non conservé « par prudence ».

### Preuve inverse, re-mesurée sur la révision livrée

Ancien ordre restauré, 8 graines par binaire :

| binaire | cas rouges | code de sortie non nul |
|---|---|---|
| `JsonApiEvents_test` | **8/8** | 8/8 |
| `JsonApiHome_test` | **8/8** | 8/8 |
| `JsonApiSession_test` | 6/8 | **8/8** |
| `JsonApiWsTransport_test` | 2/8 | **8/8** |
| `JsonApiAudioPayload_test` | 0/8 | 0/8 |
| `JsonApiScenario_test` | 0/8 | 0/8 |

Deux enseignements. D'abord `JsonApiHome_test` passe de **0/18** à **8/8** une fois le drain du
loader retiré : sa rustine était inerte **parce qu'une autre la couvrait** — retirer deux
protections redondantes révèle le défaut que chacune masquait seule. Ensuite l'écart entre « cas
rouges » et « code de sortie non nul » n'est pas du bruit : sur certaines graines le binaire
**meurt** au lieu d'échouer proprement (assertion `uv__finish_close` de libuv, `std::bad_alloc`).
**Compter les cas rouges ne suffit pas — il faut surveiller le code de sortie**, sinon un binaire
qui se termine avant de rapporter passe pour vert.

### Périmètre réel du harnais

`JsonApiCharacterization.cpp` est compilé par **12** binaires (11 au moment de la mesure, +
`JsonApiEventLog_test` arrivé avec T3.17f). **`JsonApiHardening_test` et `JsonApiAudioState_test`
ne le compilent pas** — ils ne sont pas concernés. Le chiffre de **13** qui a circulé était faux.

## T3.19 — suites

Trois acquis de la double correction (SIGSEGV `audio_db` + SIGFPE `per_page`), tous les trois
mesurés, et tous les trois portant sur des raisonnements qui *semblaient* évidents et étaient faux.

### `canDatabase()` n'est PAS la précondition — c'est le pointeur

Le réflexe est de filtrer sur la capacité annoncée par le lecteur. Mesure :

- `canDatabase()` est une **constante par classe** (`AudioPlayer.h:101` → `false`,
  `Squeezebox.h:204` → `true`, `RoonPlayer.h:177` → `false`) ;
- elle a **un seul lecteur non-commentaire dans tout l'arbre** : `JsonApi.cpp:406`, où elle est
  **simplement publiée** dans le payload `get_home` — elle ne garde rien, elle décrit ;
- ce qui est **déréférencé** est le **pointeur** `AudioPlayer::database`, laissé `NULL` par le
  constructeur de base (`AudioPlayer.cpp:28`), rendu sans garde par `get_database()`
  (`AudioPlayer.h:105`), et dont **`Squeezebox.cpp:81` est la seule assignation de tout l'arbre**.

Les deux ne coïncident que par accident, et les **deux coins divergents sont épinglés** par la
suite :

| drapeau `canDatabase()` | pointeur `database` | comportement gelé |
|---|---|---|
| **vrai** | **nul** | **refusé** (`no music database`) |
| **faux** | **valide** | **servi**, avec les arguments transmis |

Un filtre écrit sur `canDatabase()` aurait donc laissé passer le **premier** — la classe annonce
une base, le déréférencement du `nullptr` a lieu quand même — **et** volé sa réponse au **second**.
*Généralisable : une capacité déclarée n'est pas une précondition d'exécution. La précondition est
l'objet effectivement déréférencé.*

### Pourquoi la garde `per_page` teste la VALEUR et non le code de retour de `from_string()`

L'autre réflexe : « `Utils::from_string()` renvoie un booléen, testons-le ». Mesure :
`Utils::from_string("")` échoue **elle aussi**, au sentry du flux, **avant** `num_get` — donc
**rien n'est écrit** et la valeur par défaut 100 survit. Or c'est exactement le comportement
**voulu** pour un `per_page` absent ou vide.

Tester le code de retour aurait donc refusé un cas qui doit être servi. Tester la valeur
(`perPage <= 0`) est **strictement plus petit** (une comparaison, pas un changement de signature)
**et strictement plus fidèle** : tout `per_page` qui obtenait une réponse la garde à l'identique —
vide/absent → 100, parse partiel `"1,5"` → 1, valeur énorme → `INT_MAX` saturé.

Preuve que la prémisse n'a pas bougé :
`FromStringWritesZeroOnFailureWhichIsWhyEventLogCanDivideByZero`
(E4.0e) reste **vert sans qu'une seule assertion soit touchée** — `from_string()` écrit
toujours 0 en échec, et c'est toujours exactement pourquoi cette garde existe.

### `LIMIT` négatif en SQLite = « pas de limite » — la fenêtre était plus large que « petite table »

Vérifié sur **SQLite 3.51.2**. Un `per_page` négatif atteignait le moteur, et un `LIMIT` négatif
y signifie **aucune limite** : la requête renvoyait **toutes** les lignes sous un document
annonçant `per_page:-5`.

Le contrôle de page de `HistLogger.cpp:268-273` ne rattrapait pas grand-chose. Avec
`per_page = -5`, `total_page = rowcount / -5 + ((rowcount % -5) > 0 ? 1 : 0)` en arithmétique
entière C++ (troncature vers zéro, reste du signe du dividende) :

| `rowcount` | `total_page` calculé | `page=0 > total_page` ? | conséquence |
|---|---|---|---|
| 0 | 0 | non | **passe** → toutes les lignes (aucune) |
| 1 | 0 + 1 = 1 | non | **passe** → toutes les lignes |
| 2 | 0 + 1 = 1 | non | **passe** → toutes les lignes |
| 3 | 0 + 1 = 1 | non | **passe** → toutes les lignes |
| 4 | 0 + 1 = 1 | non | **passe** → toutes les lignes |
| **5** | **-1 + 0 = -1** | **oui** | refusé — **mais en nommant `page`, pas `per_page`** |
| 6 | -1 + 1 = 0 | non | **passe** → toutes les lignes |

Re-vérifié au-delà de la plage 0–6 de la revue : le contrôle **passe** aussi pour
`rowcount` **7, 8 et 9** (`total_page = 0` dans les trois cas) et ne se remet à refuser qu'à partir
de **10**. La fenêtre exacte pour `per_page = -5` est donc `rowcount ≤ 9`, **sauf 5**.

Autrement dit le refus existant était une **anomalie isolée au milieu de la fenêtre** (`rowcount`
= 5), et **désignait le mauvais paramètre**. La formulation « sur une table vide » qui avait
circulé était doublement fausse : la table vide est précisément le cas **inoffensif** (aucune ligne
à sur-livrer), et la fenêtre couvre bien plus que zéro ligne. La formulation juste est **« une
table assez petite pour que le contrôle de page passe »** — c'est celle du message de commit, de
`JsonApiInputGuards_test.cpp` et, depuis ce merge, du commentaire de `JsonApi.cpp`.

### Aucune garde sur `page` — et l'argument de sûreté, écrit dans le source

Le ticket demandait de clamper `per_page` « et vérifier `page` ». Aucune garde `page` n'est ajoutée,
et c'est **sûr** : `HistLogger.cpp:270-277` refuse `page < 0` et `page > total_page` **avant** que
`start = page * per_page` (`:269`) ne serve à construire une requête ; les seules valeurs de `page`
qui atteignent la clause `LIMIT` sont donc déjà dans la plage, et `start` ne peut pas déborder.
Le comportement est épinglé de l'extérieur par `ANonNumericPageIsStillReadAsPageZero` (un
`from_string()` en échec écrit 0, qui est aussi le défaut : une `page` illisible est donc
<!-- ⚠️ T3.25 (2026-08-25) : vrai pour une entrée illisible NON blanche ("true" → 0). FAUX pour
     "" et pour toute chaîne blanche, où from_string rend `true` SANS RIEN ÉCRIRE. Ici la garde
     tient parce qu'elle teste la VALEUR, pas le retour — mais la justification écrite ci-dessous
     ne vaut que pour un des deux régimes. Voir T3.25.md §1. -->
**indistinguable** de la page 0) et `APageOutOfRangeIsStillHistLoggersOwnRefusal` (le refus reste
celui de `HistLogger`, asynchrone, avec sa propre formulation).

L'argument est désormais **écrit en commentaire à l'endroit de la garde absente**
(`JsonApi.cpp`, `buildJsonEventLog()`), parce qu'une lacune documentaire sur une garde **absente**
est exactement ce qui pousse un lecteur ultérieur à l'ajouter « au cas où » — ou, pire, à retirer
celle qui existe en aval en la croyant redondante.

---

## T3.18 — suites

### ⭐ La porte 1 était effaçable par une simple **lecture** — parce qu'un accesseur purge

C'est l'acquis le plus instructif du ticket, et il n'a été trouvé qu'au **troisième** commit. La
réserve R1 de la revue indépendante signalait un `isDangling()` « non couvert ». Ce n'était **pas
un trou de couverture, c'était un bug**, et il vidait la décision utilisateur de sa substance.

La chaîne :

1. `Scenario::toJson()` émet la clé **`category` avant `broken`**.
2. `getCategory()` appelle `purgeDeadSteps()`.
3. `purgeDeadSteps()` **efface l'entrée pendante** (la compaction introduite par E4.2f).

Donc, pour un scénario dont la règle d'étape venait d'être détruite, le scan brut d'`isDangling()`
répondait **faux** : au moment où la sérialisation atteignait `broken`, la preuve avait déjà été
détruite **une clé plus tôt**, par la clé précédente du même document. **Sérialiser le scénario
effaçait la preuve que le scénario était cassé.** Et ce n'était pas propre à `toJson()` : *tous*
les accesseurs de lecture purgent — `getCategory()`, `stepRule()`, `getRuleSteps()`. La porte 1
était **neutralisable par n'importe quelle lecture**, y compris celle d'un client qui ne fait
qu'afficher la liste des scénarios.

Le correctif tient en deux moitiés, **toutes deux nécessaires** : `purgeDeadSteps()` **mémorise**
ce qu'elle retire (`stepRuleDestroyed`, `AutoScenario.cpp:62`), et `isBroken()` lit la mémoire
**avant** de lancer le scan (`:185`, le scan est en `:189`). Mémoriser sans lire d'abord, ou lire
d'abord sans mémoriser, ne corrige rien.

L'état reste **entièrement dérivé et jamais persisté** — `checkScenarioRules()` le remet à zéro en
re-collectant les règles (`:564`) — et **aucun client ne peut l'écrire** : c'est un membre privé,
sans accesseur en écriture, absent de la sérialisation comme de la configuration. Il ne faut pas le
confondre avec `disabled_missing_io`, qui est l'inverse exact : persistant, collant, et volontaire.

> **Leçon générale, indépendante de Calaos : un état dérivé calculé par un accesseur qui mute est
> un état falsifiable par lecture.** Dès qu'un getter a un effet de bord de compaction, tout
> prédicat qui recalcule son résultat en scannant la structure compactée est en course avec ses
> propres lecteurs — et l'ordre des clés d'un document JSON suffit à décider du résultat.

### La **troisième paire** de la trappe d'E4.0c

La trappe d'E4.0c (deux champs qui s'accordent dans tous les cas observés, donc dont le désaccord
n'est jamais testé) a resservi une troisième fois. Ici la paire est **`broken` / `missing_ios`**.

Le cas divergent est `broken` **vrai** avec `missing_ios` **vide** : une règle d'étape *détruite*
ne laisse **aucun id à nommer**, contrairement à un IO simplement introuvable. Dans tous les autres
cas les deux champs bougeaient ensemble, et c'est précisément pourquoi la mutation du relecteur
neutralisant `isDangling()` laissait **5 binaires verts**. Deux paires sur trois avaient été
couvertes par les tickets précédents ; celle-ci ne l'était pas.

### ⚠️ Le canari qui devenait une vraie commande

E4.0c utilisait la chaîne littérale **`"reenable"`** comme sonde de « type autoscenario inconnu »,
pour vérifier que le serveur reste **silencieux**. Livrer la commande `autoscenario reenable`
**transformait le canari en commande valide** : le test de silence aurait continué de **passer**,
mais **pour une raison entièrement différente** — et aurait cessé de surveiller quoi que ce soit,
sans jamais rougir pour le signaler.

La sonde est changée en **`e40c_not_a_command`**, et une assertion **positive** est ajoutée pour
couvrir le nouveau comportement de `reenable`.

> **Leçon générale : une sonde de test doit être une valeur qui ne peut pas devenir valide.**
> Un canari choisi dans l'espace des noms réels finit par être implémenté, et il meurt en silence
> le jour où il est implémenté — c'est-à-dire exactement le jour où on aurait eu besoin de lui.

### R3 — ✅ **TRANCHÉ (2026-08-24)**, ouvert en [T3.20](T3.20.md) — `modify` blanchit un scénario amputé

Après un aller-retour `autoscenario modify`, `deleteRules()` **détruit la référence morte**. Donc
`isBroken()` devient **faux**, et `tryReenable()` **réussit** sur un scénario qui a silencieusement
perdu une action d'étape. Le drapeau collant est alors levé **légitimement**, par le mécanisme
prévu, sur un scénario **amputé** — c'est-à-dire le résultat même que le ticket voulait rendre
impossible.

**Préexistant** : rien ne le signalait avant T3.18, et `missing_ios` prévient désormais **avant**
le round-trip. Mais le refus de `tryReenable()` ne peut **rien voir après** : il n'y a plus de
référence pendante à détecter.

> **Question ouverte pour l'utilisateur** : `autoscenario modify` doit-il **refuser** de
> reconstruire un scénario dont une étape référence un IO absent ? C'est le seul endroit où
> l'information existe encore.

~~Laissé intact sur instruction, à ouvrir en ticket de suivi.~~

**➡️ Réponse de l'utilisateur (2026-08-24) : OUI, mais sur le PAYLOAD, pas sur l'état d'avant.**
`autoscenario modify` refuse de reconstruire un scénario dont le **payload reçu** cite un IO
absent, et l'erreur **nomme les ids manquants** (même forme que le refus de `reenable`).
Conséquence voulue : **réparer** (payload nettoyé) reste possible — c'est le chemin de réparation
nominal — tandis que **blanchir** (payload avec l'étape morte) devient impossible. Un refus portant
sur l'état d'avant enfermerait l'utilisateur : plus d'édition d'un scénario cassé, donc plus jamais
de réparation.

**L'analyse ci-dessus reste exacte et reste la référence du ticket.** Elle est complétée par deux
mesures faites au moment de la décision : le point d'insertion obligatoire est
**`JsonApi.cpp:2033`, AVANT `deleteRules()` (`:2034`)** — `deleteRules()` + `addStep`/`addStepAction`
(`:2057-2079`) tournent **avant** le seul garde-fou du corps, `checkScenarioRules()` (`:2130`) ;
et le saut silencieux est le `if (out)` sans `else` de **`:2075`**, jumelé à `:1981` dans
`buildAutoscenarioCreate`.

→ **Ticket : [T3.20](T3.20.md)** · **Décision : `DECISIONS.md`, entrée
« 2026-08-24 — `autoscenario modify` refuse un **payload** qui référence un IO absent »**.
**Non implémenté.**

### R5 — ✅ **TRANCHÉ (2026-08-24)**, ouvert en [T3.20](T3.20.md) — l'asymétrie du drapeau posé à la main

Un client qui pose `disabled_missing_io` à la main sur un scénario **sain** n'affecte **pas** le
booléen en mémoire : le scénario **continue de tourner** jusqu'au prochain redémarrage, où il se
retrouve **désactivé**. L'écriture ne prend donc effet qu'au reboot, alors que la lecture est
immédiate.

Asymétrie **non documentée** ailleurs que dans cette note. ~~Le ticket classe ce sens comme un
**déni de service par client authentifié**, catégorie déjà assumée par la série. Hors périmètre,
laissé intact sur instruction.~~

**➡️ Décision de l'utilisateur (2026-08-24) : fermer la porte.** `disabled_missing_io` devient un
paramètre **en lecture seule côté API** — persisté et relu comme aujourd'hui, mais toute écriture
venant d'un client est **ignorée**. Seuls écrivains légitimes : **le moteur** et
**`autoscenario reenable`**.

**L'analyse ci-dessus reste exacte** ; le recensement fait pour trancher l'a complétée sur trois
points, tous consignés dans le ticket :
- il y a **exactement deux** chemins d'écriture client, `buildJsonSetParam` (`JsonApi.cpp:694`) et
  `buildJsonDelParam` (`:724`). `autoscenario modify`/`create` n'en sont **pas** : ils ne
  construisent leurs `Params` que sur 6 clés nommées en dur ;
- ⭐ **`buildJsonDelParam` court-circuite `IOBase::del_param()`** — il appelle
  `o->get_params().Delete(...)`. La garde d'immuabilité de `"id"` (`IOBase.cpp:112-123`) n'est donc
  **jamais atteinte depuis l'API**, exactement le trou que `IOBase.h:120-122` annonçait comme
  accepté hors périmètre de T1.11 ; le premier appelant à l'avoir emprunté est l'API elle-même, et
  `IOIdIntegrity_test.cpp:95` ne le couvrait que par appel **direct** ;
- **arbitrage ignorer-vs-erreur** : *ignorer + logguer + répondre `success`*, aligné sur le
  précédent `set_param("id")` (`IOBase.cpp:82-109`), qui est **`void`** et ne peut pas refuser vers
  l'appelant (`JsonApi.h:159-162` le dit déjà). Pas de nouveau chemin d'erreur.

⚠️ Le commentaire de `ScenarioDisabledMissingIo_test.cpp:711-715` (« *Any authenticated client can
del_param the flag* ») deviendra **faux** à la livraison de T3.20 et doit être réécrit.

→ **Ticket : [T3.20](T3.20.md)** · **Décision : `DECISIONS.md`, entrée
« 2026-08-24 — `disabled_missing_io` : en **lecture seule** côté API »**. **Non implémenté.**

### Onze tests de contrat modifiés — et la liste du ticket était fausse **dans les deux sens**

Le ticket annonçait **six** tests de contrat à modifier. Il y en a **onze**, et sa liste contenait
à la fois des tests qui n'avaient pas besoin de bouger et des tests qu'elle omettait. Le décompte
d'un ticket est une **estimation**, pas un périmètre : ici, s'y tenir aurait laissé des contrats
non réalignés.

Le cas le plus notable est **`SavingRulesAfterAnIoDeletionIsClean`, dont l'assertion s'inverse** :
l'id mort devait auparavant **disparaître** de `rules.xml`, il doit désormais y **survivre**. C'est
la conséquence directe de la décision utilisateur — un scénario désactivé reste **intact** dans la
configuration, donc la référence morte doit être **conservée**, pas nettoyée. Un test dont
l'assertion s'inverse est un signal fort : ce n'est plus un ajustement, c'est le contrat qui change
de sens.

## E4.6 — cadrage (refonte AutoScenario)

### ⭐ `calaos_installer` ampute les règles, hors API — défaut d'un autre dépôt, à ne pas perdre

Trouvé par la revue indépendante de T3.20, hors périmètre de `calaos_base`.

`calaos_installer` **détruit silencieusement les actions à référence morte** :
1. au chargement de `rules.xml`, chaque sortie d'action est résolue et **abandonnée sans erreur si
   l'id ne résout pas** — `if (output)` sans `else`,
   `calaos_installer/src/projectmanager.cpp:1126-1133` ;
2. à la sauvegarde, `io.xml` et `rules.xml` sont **régénérés en entier** depuis le modèle mémoire
   (`saveIOsToFile()` → `IOXmlWriter`, `projectmanager.cpp:922-933`) ;
3. les deux fichiers sont **téléversés entiers** par `api.php`
   (`src/dialogsaveonline.cpp:100-122` ; **seulement ces deux**, `local_config.xml` est commenté).

⇒ **ouvrir un projet dans l'installeur et le renvoyer suffit à amputer une règle**, définitivement,
**sans qu'aucune garde côté serveur ne puisse le voir** : le serveur reçoit un `rules.xml` cohérent
d'où la référence a simplement disparu. C'est aujourd'hui le **vecteur d'amputation le plus probable
en production** — plus probable que l'API, qui n'a aucun appelant first-party.

**Sites exacts : QUATRE, pas un** — `projectmanager.cpp:1023` et `:1082` (entrées de condition),
`:1054` et `:1125` (sorties d'action). Tous des `if (x)` sans `else`.

**Traité côté serveur par E4.6, en deux moitiés indépendantes** (D2/D10) :
- **`rules.xml`** : la définition n'y est plus, et les règles générées sont **régénérées depuis la
  définition** à chaque chargement ⇒ toute amputation par l'installeur est **écrasée au démarrage
  suivant** (auto-réparation) ;
- **`io.xml`** : la définition y vit (décision utilisateur — invariant « deux fichiers »), portée
  par les **params de l'IO**, que l'installeur **préserve intégralement sans rien en savoir**.

⭐ **La mesure qui décide, et qui vaut au-delà des scénarios** — `calaos_installer` et les données
qu'il ne modélise pas :

| | lecture | écriture | verdict |
|---|---|---|---|
| **params (attributs) d'un IO** | `Params` construit depuis **tous** les attributs, **sans liste blanche** — `projectmanager.cpp:611-618` | **tous** les params réémis — `projectmanager.cpp:197-204` | ✅ **préservés** |
| **nœuds XML enfants d'un IO** | consommés et **jetés** — `projectmanager.cpp:653-658` | **aucun** réémis, hors le cas spécial `RemoteUI` (`:206-216`) | ❌ **perdus au premier save-online** |

Preuve empirique : `cycle="false"` et **78** attributs `log_history=` survivent dans
`configs/raoulh/io.xml`, params dont l'installeur n'a aucun modèle.
Conséquence générale : **toute donnée serveur devant survivre à un aller-retour installeur doit
être portée par un param d'IO, jamais par un nœud enfant** — y compris face aux installeurs
**anciens**, qu'aucune mise à jour ne rattrapera.

⚠️ Il existe **un précédent de sous-arbre XML préservé verbatim** : les pages `RemoteUI`, lues en
brut (`readRemoteUIPagesElement()`, `:812`), stockées telles quelles (`setRemoteUIPagesXml()`,
`:799`) et réémises verbatim (`writeRemoteUIPagesContent()`, `:222-247`). Le mécanisme existe donc
déjà — mais il est **inopérant sur les installeurs déjà déployés**, ce qui est exactement pourquoi
la conception d'E4.6 ne le retient pas.

**Non traité côté installeur** : autre dépôt, et le défaut concerne **toutes** les règles.
→ ticket **I4.1** (`BOARD.md`), **recommandé, non bloquant** : les 4 sites ci-dessus, plus la
protection des params `autoscenario_*` dans `DialogListProperties.cpp:85-90` (éditeur manuel de
propriétés, ne protège aujourd'hui que `type` et `name`).

### Sites recalés — la revue de T3.20 cite un worktree, pas master

Les gardes `if (!sa.io) continue;` de `Scenario::toJson()` sont citées **`:206`** et **`:229`** par
la revue de T3.20. Sur **master `770e322f`**, `IO/Scenario.cpp` fait **195 lignes** et les sites
sont **`:158`** et **`:181`** (vérifié). L'écart de ~48 lignes vient des surcharges
`set_param`/`del_param` que T3.20 ajoute **dans son worktree** `.wave26/t3.20`. La garde elle-même
est **préexistante** : commit `faa952ea` (E4.2f, 2026-08-16), ni T3.18 ni T3.20.

### `IOBase::isAutoScenario()` est mort, et cela requalifie un défaut

`IOBase::auto_sc_mark` (`IOBase.h:65,150-151`) est écrit par `AutoScenario::createInput()`
(`AutoScenario.cpp:399`) et par `Scenario::Scenario()` (`IO/Scenario.cpp:51`), et
**`IOBase::isAutoScenario()` n'est appelé nulle part dans `src/`** (recherche exhaustive).
Conséquence : le défaut « `createInput()` marque un IO étranger squatté » est **réel mais sans effet
observable**. Ce qui reste gênant est la **cause** — des ids d'IO **dérivés** d'une chaîne de
configuration créent un espace de noms squattable, d'où la garde T2.18
(`AutoScenario.cpp:584-590`) et `ScenarioNullGuard_test.cpp:91`.
`Rule::isAutoScenario()` (`Rule.h:162`), lui, a **exactement un** consommateur :
le balayage orphelin de `ListeRoom.cpp:324`.

### Code mort et déréférencements non gardés relevés en cartographiant (E4.6, non corrigés)

- **`JsonApi.cpp:2037`** — `buildAutoscenarioModify()` calcule
  `params.Add("auto_scenario", Calaos::get_new_scenario_id())` et **ne s'en sert jamais** :
  `params["auto_scenario"]` n'est relu nulle part dans la fonction. Copié de `create` (`:1908`)
  sans relecture. Coût réel : un balayage O(n²) du cache à chaque `modify`.
- **`JsonApi.cpp:2170`** — `sc->getAutoScenario()->getIOTimeRange()->get_param("id")` après
  `addSchedule()` (`:2161`), qui laisse `ioTimeRange` **nul** si `createInput()` échoue
  (`AutoScenario.cpp:1075` — factory miss / room manquante, le cas même que T2.18 garde ailleurs).
- **`JsonApi.cpp:2105`** — `old_room->RemoveIOFromRoom(scenario)` avec `old_room` issu de
  `getRoomByIO()` (`:2100`), non gardé, alors que `room` l'est (`:2103`).

Les trois disparaissent avec E4.6d. S'ils devaient survivre à un abandon d'E4.6, ils valent un
ticket à eux seuls.

---

## E4.6a — mesures faites en posant le filet de caractérisation

> Trois écarts par rapport à ce que la cartographie d'`E4.6.md` affirmait, tous **mesurés** par un
> test qui rougit, aucun corrigé (E4.6a est de la caractérisation pure, zéro ligne de `src/`).

### ⭐ `checkScenarioRules()` ne **recrée pas** les règles d'étape — il les perd

`E4.6.md` RC1 écrit qu'« une règle qui ne matche pas est ignorée puis **recréée en double** »
(`:639-716` puis `:737-808`). **C'est vrai des règles d'en-tête et faux des étapes.** Le bloc de
recréation (`AutoScenario.cpp:737-808`) ne couvre que `button_start`, `button_stop`, `step_end`,
`time_start` et `time_stop` ; **aucune branche ne recrée une règle `step`** — la seule création
d'étape est `addStep()` (`:834`), appelée uniquement par l'API.

Conséquence, épinglée par
`AutoScenarioMigration_test.cpp::AStepRuleWhoseConditionValueDoesNotMatchIsDroppedAndThenDestroyed` :
**un seul caractère** changé dans une valeur de condition d'une règle d'étape dans `rules.xml`
(mesuré : `val="true"` → `val="false"` sur la condition `_is_active`) suffit à ce que
`checkCondition()` (`:426`) refuse la règle, que `checkScenarioRules()` ne l'adopte jamais, que le
balayage orphelin la détruise et que `SaveConfigRule()` persiste la perte. **L'étape disparaît
définitivement**, et les étapes suivantes sont renumérotées par-dessus le trou. Les règles d'en-tête,
elles, sont bien recréées en double puis l'originale est détruite — deux comportements distincts
sous la même cause racine.

### ⭐ La renumérotation d'étape ne touche **que trois** des quatre numérotations

`E4.6.md` §2.4 énumère quatre numérotations et dit que « **tous** bougent quand un voisin
disparaît ». Mesuré : `checkScenarioRules()` (`:811-829`) réécrit la **condition**
(`setRuleCondition(rule, ioStep, "==", i)`, `:817`) et **ne touche pas le param
`auto_scenario_step`**. Après la disparition de l'étape du milieu, la troisième règle **persiste
avec `auto_scenario_step="2"` alors qu'elle ne se déclenche plus que sur `_step == 1`** — et
`buildAutoscenarioModify()` ne relit jamais le param, donc rien ne recolle jamais les deux.
Épinglé par `LosingTheMiddleStepRenumbersEveryStepAfterIt`. Le tri de `:811` s'appuie pourtant sur
ce même param (`_sortCompStepRule`), ce qui rend l'ordre des étapes dépendant d'une valeur que
personne ne remet à jour.

### Compte de `TESTS` — refait, et l'écart d'`E4.6.md` §8.2 expliqué

`E4.6.md` §8.2 signalait « 68 binaires » contre « 65 lignes `check_PROGRAMS +=` » sans trancher.
Décompte exact sur `master = aa4821f7` :

| | |
|---|---|
| entrées `TESTS` | **68** — et non 68 *lignes* : la ligne `tests/Makefile.am:18` en porte **deux** (`check-config-options.sh check-config-docs.sh`), d'où 67 lignes pour 68 entrées |
| dont scripts shell | **3** (`check-config-options.sh`, `check-config-docs.sh`, `run-python-tests.sh`) |
| dont binaires | **65** |
| entrées `check_PROGRAMS` | **66** = les 65 binaires de `TESTS` **+ `StaticLogShutdown_helper`**, qui est construit mais n'est pas une entrée `TESTS` |

Le « 68 » du ticket est donc un compte d'**entrées `TESTS`**, correct, et le « 65 » un compte de
**lignes `check_PROGRAMS +=`**, correct aussi : les deux ne mesuraient pas la même chose. Après
E4.6a : **69 entrées `TESTS`**, 66 binaires, 67 entrées `check_PROGRAMS`.

### Artefact cosmétique du harnais, préexistant, hors périmètre

Tout binaire `core/*` termine son exécution sur
`[ERR] (CalaosConfig.cpp:552) Could not open <tmp>/cache/iostates.cache.tmp for write !` :
`~Config()` vidange le cache d'états **après** que `CoreFixture::TearDown()` a supprimé le
répertoire temporaire. Vérifié identique sur `core/JsonApiScenario_test` et
`core/ScenarioDisabledMissingIo_test`. Sans effet sur le résultat des tests, jamais consigné
jusqu'ici. **Ne pas le confondre avec un échec.**

### E4.6a, suites de revue — un point de la revue infirmé par la mesure

La revue d'E4.6a a demandé de corriger un commentaire du cas de référence en affirmant que
`autoscenario modify` **efface** le drapeau collant `disabled_missing_io`, ce qui ferait prendre à
`tryReenable()` sa branche no-op (`AutoScenario.cpp:288-295`). **Mesuré : c'est l'inverse.**
`modify` **laisse le drapeau posé** — c'est précisément ce que fige
`ScenarioDisabledMissingIo_test::ModifyDoesNotClearTheDisabledFlag` (`:789`) — donc `tryReenable()`
atteint bien `setDisabledMissingIo(false)` et **lève réellement la porte 2**, uniquement parce que
l'aller-retour de l'étape 3 a blanchi `isBroken()`.

Les deux lectures finissent sur `success:true`, ce qui est exactement pourquoi il fallait mesurer :
le cas assertait le résultat sans nommer le mécanisme. Il porte désormais un **échange autour du
seul appel** (`EXPECT_TRUE(isDisabledMissingIo())` avant, `EXPECT_FALSE(...)` après), donc les deux
lectures ne peuvent plus être confondues. **Le critère d'acceptation ne bouge pas** : après E4.6d,
`reenable` doit **refuser** ici (§11.3, étape 4).

### E4.6a — deux oracles faibles, consignés et non corrigés

- L'assertion d'événement du cas (b) n'était **pas** un oracle mort (elle pompait avant de compter)
  mais elle était strictement plus faible que la preuve `_is_active`/`_step` déjà présente. Elle a
  été renforcée en **paire absence/présence sur le même canal et le même compteur** : zéro
  `io_changed` sur la branche marquée-et-cassée (le chemin sort en `IO/Scenario.cpp:91`, avant
  `EmitSignalIO()`), **deux** sur la branche démarquée (l'appui, puis la remise à `false` par
  `_button_start`). C'est cette moitié d'absence qui rend l'**unique** pompage du fichier porteur.
- `EXPECT_NE("Soirée", name)` dans le cas de référence reste un **oracle d'inégalité faible** : il
  passerait pour n'importe quel autre nom. Conservé tel quel — la valeur exacte
  (`_("New unnamed scenario")`) est **localisée** (`JsonApi.cpp:2038`), donc l'asserter en dur
  rendrait le test dépendant de la locale du binaire. À reprendre par E4.6d, qui supprime le défaut.

### E4.6a — treize pompages qui n'absorbaient rien (récidive §9.7, trouvée en revue)

`core/AutoScenarioMigration_test.cpp` a d'abord été livré avec **14 `pumpEventLoop()`** et un
en-tête affirmant que **deux** étaient mesurés. **Treize n'absorbaient rien**, vérifié en les
retirant : 19/19 verts en ordre par défaut **et** sur 5 graines mélangées (1, 7, 42, 1234, 99999).
- **douze** étaient placés après `ListeRoom::checkAutoScenario()`, recopiés les uns des autres ;
- **un** était dans le chargement de la maison, justifié par « le chargement lève un `EventIOAdded`
  par IO » — une explication que `JsonApiCharacterization.h` **corrige déjà par écrit** : le
  chargement de config ne lève **rien**, `EventIOAdded` n'a qu'un site d'appel et c'est
  `ListeRoom::createIO()`, le chemin d'exécution.
Il en reste **un**, et il est porteur parce qu'il précède une assertion d'**absence**.
C'est le même mécanisme que les 6 rustines de la série : **une explication fausse voyage plus vite
qu'une mesure**. Le seul contrôle qui l'attrape est de retirer le pompage et de relancer.

---

## E4.1a — ce que la coupure du pont a révélé (hors périmètre, à reprendre)

### ⭐ L'échappement `\uXXXX` de jansson est en **MAJUSCULES**, celui de nlohmann n'existe pas

Mesuré en écrivant la caractérisation (le cas a échoué au premier run et c'est lui qui l'a
appris) : `json_dumps(..., JSON_ENSURE_ASCII)` sérialise `é` en **`é`**, hex **majuscule**.
`nlohmann::json::dump()` fait deux choses différentes à la fois : il **n'échappe pas** le
non-ASCII du tout (les octets UTF-8 partent bruts) et, pour ce qu'il échappe réellement (les
contrôles), il utilise l'hex **minuscule**.

**Mesure des trois formes** (`é` = U+00E9, plus `U+001F` et `U+0001`) :

| Forme | `é` | `U+001F` | `U+0001` |
|---|---|---|---|
| jansson `JSON_ENSURE_ASCII` - **ce qui part aujourd'hui** | `\u00E9` | `\u001F` | `\u0001` |
| `nlohmann::dump()` nu | `é` **brut** | `\u001f` | `\u0001` |
| `nlohmann::dump(-1, ' ', true)` | `\u00e9` | `\u001f` | `\u0001` |

/!\ **Meme le port le plus proche n'est pas byte-identique** : `ensure_ascii = true` donne
`\u00e9`, hex **minuscule**. Et contrairement a ce qu'on suppose facilement, **les caracteres de
controle ne sont pas tous identiques** des deux cotes : `U+001F` diverge par la casse de son hex,
`U+0001` non - parce que ses chiffres ne contiennent aucune **lettre**. Un test de controle bati
sur `U+0001` seul ne verrait rien et ne prouverait rien.

Consequence pour la suite d'E4.1 : **chaque processus externe** qui lit la sortie de
`jansson_to_string()` - Wago, KNX, Lua (`ScriptExtern_main`, `ScriptExec`, `ScriptBindings`) - verra
un flux **different octet par octet**, quelle que soit la forme choisie. **Semantiquement
identique**, et **aucun** de ces canaux n'a de golden (E4.0d : hors API, pas de filet). Pire : les
goldens des emetteurs d'API **ne rougiront pas non plus**, puisqu'ils comparent des documents
**parses** (contrat d'oracle d'E4.0) et que les trois formes parsent vers le **meme** document.

Le seul garde-fou est donc
`ParamsJson.Tripwire_TheThreeWireEscapingsAreThreeDifferentBytestreams`, qui epingle les trois
formes **separement**, sur la chaine **brute**, **sans aucune normalisation de casse** - une
assertion insensible a la casse laisserait passer precisement le port
`dump(..., ensure_ascii = true)`, celui qui a le plus de chances d'etre choisi. Verifie par
mutation : ce port **rougit** (`form 1 changed: {"k_accent":"\u00e9",...}`) et le port `dump()` nu
rougit aussi (`{"k_accent":"é",...}`). Ce cas **doit** etre modifie consciemment par le
sous-ticket qui migre les emetteurs.

### Carte des emetteurs - la ou l'asymetrie se declenchera

E4.1a **ne declenche rien** : aucun site de dump n'est modifie. Les emetteurs sont
`JsonApiHandlerWS.cpp:72`, `JsonApiHandlerHttp.cpp:223` et `EventManager.cpp:80` pour l'API (filet
de goldens, mais **aveugle a l'echappement**, voir ci-dessus), et `WagoMap.cpp` (10 envois),
`KNXCtrl.cpp:305`, `KNXExternProc_main.cpp:220`, `ScriptExec.cpp:168,184`, `ScriptBindings.cpp`,
`ScriptExtern_main.cpp:140` pour les wires drivers - **aucun filet du tout**.

### `Config::saveStateCache()` a déjà tranché U+FFFD — c'est le précédent à suivre

`CalaosConfig.cpp:556-558` dumpe le cache d'états avec
`dump(4, ' ', false, Json::error_handler_t::replace)`, avec le commentaire qui va avec (« une
valeur non UTF-8 venue du matériel ne doit pas faire lever `dump()` et perdre tout le cache »).
La décision utilisateur du 2026-08-17 (`error_handler_t::replace` partout, **pas** de `try/catch`)
n'est donc pas une nouveauté à inventer : **elle a déjà un site d'application dans l'arbre**, et
c'est la forme exacte à recopier sur les deux `sendJson`.

### `Params::fromNJson()` **lève** `type_error.302` sur une valeur non-chaîne

Non documenté jusqu'ici, et c'est pourtant la raison d'être du `try`/`catch` qui enveloppe **toute**
la désérialisation de `Config::readStateCache()` (`CalaosConfig.cpp:504-529`). `fromNJson` fait
`p.params[it.key()] = it.value()` : la conversion implicite `Json -> std::string` **lève** au lieu
de coercer. Un cache qui parse en JSON mais porte un état numérique fait donc lever, pas silencer.
Épinglé (`ParamsJson.FromNJson_ThrowsTypeError302OnANonStringValue`), id compris.

### `IODoc.h` prenait jansson par la fenêtre de `Params.h`

`IODoc.h:56` déclare `json_t *genDocJson();` sans jamais inclure jansson : il l'obtenait
**uniquement** parce que `Params.h` l'incluait. C'est le seul en-tête de l'arbre dans ce cas —
`KNXCtrl.h`, `JsonApiHandlerHttp.h` et `JsonApiHandlerWS.h` nomment aussi `json_t` mais passent par
`ExternProc.h` / `JsonApi.h`, qui incluent `Jansson_Addition.h`. Corrigé sur place (l'en-tête nomme
maintenant sa dépendance), mais c'est le symptôme d'un arbre d'includes qui compile **par accident**.

### Dette laissée par E4.1a, à retirer par le dernier sous-ticket de la série

- **`jansson_from_params()`** (`src/lib/Jansson_Addition.h`) : adaptateur transitoire, **99 appels**
  dans 13 fichiers. `grep -rn jansson_from_params src tests` est la **liste exacte** de ce qui
  reste à migrer côté `Params`. Quand elle est vide, la fonction et l'en-tête disparaissent.
- **`Params::toNJson()` / `Params::fromNJson()`** gardent leur préfixe `N`, qui n'existait que pour
  les distinguer de la face jansson. Le renommage en `toJson()`/`fromJson()` a été **volontairement
  écarté** ici : supprimer le membre plutôt que le renommer garantit que **tout site oublié est une
  erreur de compilation dure**, jamais un changement d'overload silencieux. À faire à la fin.
- **Trois `toJson()` membres rendent encore `json_t*`** et ne sont **pas** des `Params` :
  `CalaosEvent::toJson()` (`EventManager.cpp:185`), `KNXValue::toJson()` (`KNXCtrl.cpp:76` et
  `KNXExternProc_cli.cpp:493`), `Scenario::toJson()` (`IO/Scenario.cpp:108`). Intacts par
  construction — le périmètre d'E4.1a est `Params`, pas « tout ce qui s'appelle toJson ».
- **Commentaires périmés** : plusieurs tests de la série E4.0 citent `Params::toJson()` en prose
  (`JsonApiEvents_test.cpp:316,625,648`, `JsonApiSession_test.cpp:52,2068`,
  `JsonApiAudioPayload_test.cpp:324`, `JsonApiMusicDb_test.cpp:786`). **Non touchés** : E4.1a ne
  réécrit aucune prose de test existante, pour que son diff reste lisible comme une bascule
  mécanique. À balayer en fin de série.


### /!\ Trois fuites de `json_t` **preexistantes**, dont deux dans la zone sans filet

Trouvees par la revue d'E4.1a. **Ce ne sont pas des regressions** : elles sont anterieures au
ticket, qui a reecrit ces lignes mecaniquement sans les corriger - et **sans les voir**. C'est la
trouvaille dans la trouvaille : une bascule mecanique traverse un bug sans jamais le lire.
Volontairement **non corrigees ici** (hors perimetre d'E4.1a). **Elles meritent un ticket.**

1. **`WagoMap::write_multiple_bits()`** (`WagoMap.cpp:328-337`) - construit `jret`, y ajoute le
   tableau `values`... puis envoie une **seconde serialisation fraiche** :
   `process->sendMessage(jansson_to_string(jansson_from_params(p)))`. Donc **le tableau `values`
   n'est jamais emis** - c'est un bug **fonctionnel**, pas seulement une fuite - et **`jret` fuit**.
2. **`WagoMap::write_multiple_words()`** (`WagoMap.cpp:402-411`) - strictement identique, au type
   des elements pres.
3. **`IODoc::genDocJson()`** (`IODoc.cpp:163`) - `json_object_set` (et **non** `_new`) pour
   `list_value` : la reference fraiche n'est jamais reprise, elle **fuit**.

/!\ Les deux premieres sont dans **la zone sans filet** : aucun test n'execute les drivers Wago
(E4.0d). Un `values` jamais emis a donc pu vivre la indefiniment sans qu'aucune suite ne bronche -
et c'est exactement le genre de site que le sous-ticket des emetteurs va toucher.

---

## E4.5c — écarts trouvés en réécrivant `02_io_drivers` / `05_audio` / `06_ipcam`

Tous vérifiés au source de master (`1b9f400f`). **Aucun n'est corrigé ici** : le ticket est de la
documentation pure, ces cinq entrées demandent une modification de `src/` ou d'un document hors
périmètre.

- **[DOC, périmètre E4.5f] `docs/08_http_api.md:844-855` est périmé par T3.19.** La section
  « `audio_db` sur un player sans base de données fait planter le serveur » décrit le défaut au
  présent et conseille au client de « vérifier lui-même le champ `database` renvoyé par
  `get_home` avant d'émettre un `audio_db` ». Le déréférencement est gardé depuis T3.19
  (`JsonApi.cpp:976-985`, `audioDbUnavailable()`), la commande est **refusée proprement** avec
  `{"error":"no music database"}` (goldens `t319_ws_audio_db_no_database.json` et
  `t319_http_audio_db_no_database.json`). ⚠️ Le remplacement doit dire que **le test est le
  pointeur, pas `canDatabase()`** — la capacité n'est que publiée.
- **[API/ioDoc] `hifirose` est un `model` d'ampli accepté mais non publié.**
  `AVRManager::Create()` accepte six modèles (`AVRManager.cpp:55-71`), dont `hifirose` →
  `AVRRose`. La description du paramètre publiée à l'installeur n'en liste que **cinq** :
  `_("AVReceiver model. Supported: pioneer, denon, onkyo, marantz, yamaha")`
  (`Audio/AVReceiver.cpp:257`). Conséquence : `--gendoc` et calaos_installer n'offrent jamais
  HiFi Rose, alors que le driver, son serveur de notifications (port 9284) et 14 Ko de code
  existent. → mini-ticket : ajouter `hifirose` à la chaîne.
- **[COMPORTEMENT] `StandardMjpeg` : la capacité PTZ dépend de la *présence* du paramètre, pas de
  sa valeur.** `if (param.Exists("ptz")) { caps.Add("ptz","true"); caps.Add("position","8"); }`
  (`IPCam/StandardMjpeg.cpp:41-45`), idem pour `zoom` (`:46-49`). Une caméra configurée
  `ptz="false"` est donc annoncée **PTZ avec 8 positions mémoire** dans `get_home`. Le golden
  `ws_get_home.json` ne l'attrape pas : sa caméra « plain » **omet** l'attribut
  (`tests/core/JsonApiCharacterization.cpp:735-737`). Documenté comme piège dans `06_ipcam.md`,
  mais c'est le code qui est incohérent avec le type `TYPE_BOOL` déclaré à l'ioDoc.
- **[COMPORTEMENT] `Internal` : casse du `type` incohérente entre la fabrique et l'IO.**
  `IOFactory::CreateIO()` et `RegisterClass()` passent le type en minuscules
  (`IO/IOFactory.cpp:42`, `IO/IOFactory.h:81`), mais `Internal::get_type()` compare le paramètre
  `type` **à l'octet près** à `"InternalBool"` / `"InternalInt"` / `"InternalString"`
  (`IO/IntValue.h:53-59`). Un `type="internalbool"` **crée l'IO** puis rend `TUNKNOWN`. Le même
  écart existe partout où un driver relit `get_param("type")` au lieu de son type de classe.
- **[PERTE DE DONNÉES, conséquence du contrat « type inconnu ignoré »] Un IO de type inconnu est
  effacé d'`io.xml` par le seul fait de démarrer.** `Room::LoadFromXml()` saute l'IO nul
  (`Room.cpp:176-181`) et `Config::SaveConfigIO()` — **unique** écrivain d'`io.xml` — reconstruit
  un `pugi::xml_document` **neuf** depuis les seules pièces de `ListeRoom`
  (`CalaosConfig.cpp:313-330`, `Room.cpp:187-200`). Aucun mécanisme de préservation n'existe
  (`rawXml` / `preserveUnknown` → 0 occurrence dans `src/`). ⚠️ **La réécriture n'attend aucune
  action de l'utilisateur** : `main.cpp:196` planifie `ListeRoom::checkAutoScenario()` **0,1 s
  après le démarrage**, qui se termine par `SaveConfigIO()` (`ListeRoom.cpp:339-341`) ; neuf
  autres sites la déclenchent aussi (huit dans l'API JSON, un dans le provisioning RemoteUI). Le contrat annoncé pour MySensors (T2.12)
  et Gadspot (T3.6) est donc **exact au démarrage** (`CoreSmoke_test.cpp:86-99`) mais **rassurait
  à tort sur le fichier** : `RELEASE_NOTES.md` a été corrigé en conséquence, et
  `02_io_drivers.md` / `06_ipcam.md` le disent. **Si la préservation est le comportement voulu,
  c'est un ticket** ; sinon la note de version doit rester aussi explicite qu'elle l'est
  maintenant.
- **[COSMÉTIQUE] `ReolinkInputSwitch` nomme son hôte `hostname`** (`IO/Reolink/ReolinkInputSwitch.cpp:40`)
  là où tous les autres drivers réseau utilisent `host` (Wago, KNX, MQTT, Hue, LAN, Squeezebox,
  AVReceiver). Renommer casserait les configs existantes ; à traiter par un alias si jamais.

### E4.5c — suites de revue : deux bugs de code trouvés en corrigeant la doc

- **[⭐ PLANTAGE DÉTERMINISTE DU SERVEUR, trouvé en caractérisant T3.29, reproduit en revue —
  ✅ **LIVRÉ** sur `fix/t3.35` (`3685dc7b` caractérisation, `3db14a92` correctif), fiche
  [T3.35](T3.35.md)] Un `path` de configuration valant `[` fait tomber
  `calaos_server`.** Branche index des **deux** parseurs (`IO/Mqtt/MqttCtrl.cpp`,
  `IO/Web/WebCtrl.cpp`) : `val` vaut `"["`, `val.erase(0, 1)` le vide, `val.pop_back()` **sous-flue
  le `size_t`** — et le `Utils::from_string(val, idx)` qui suit est **HORS du `try`**
  (`MqttCtrl.cpp:132`, `WebCtrl.cpp:202`) ⇒ **`std::bad_alloc` s'échappe de `getValueJson()`**.
  Aucun `catch` sur toute la chaîne (`getValue` → IO Mqtt → `main.cpp`) ⇒ **`std::terminate()`**.
  ⚠️ **Ce n'est donc pas « de l'UB théorique »** : la première rédaction de ce finding sous-vendait
  gravement le défaut. Le mot juste est **plantage**, et il est **mesuré sur les deux parseurs**.
  **Atteignable par une faute de frappe dans un paramètre de configuration** ; ⛔ **aucun vecteur
  distant** (le `path` n'est pas écrit par un client, seulement par `calaos_installer`).
  **Volontairement non figé par un test** : la lecture de la chaîne vidée est de l'UB, et on ne
  caractérise pas de l'UB — cette décision-là était juste, c'est la formulation qui ne l'était pas.
  Correctif : une garde `if (val.size() < 2)` vers le chemin d'erreur, dans le **même `catch`** que
  le message d'aide de T3.35.
  ⭐ **Livré 2026-08-25, et la sortie de l'impasse « on ne fige pas de l'UB » vaut d'être notée** :
  les cas ne figent PAS le comportement actuel, ils figent le **chemin d'erreur qui devrait
  exister** (valeur vide, aucune exception, jeton nommé) — le contrat que tous les autres échecs de
  ce parseur honorent déjà. Rouge mesuré sur le commit de caractérisation : **6 cas, `exit status:
  1`**, `Actual: it throws std::bad_alloc`, sur les deux parseurs.
  ⚠️ **Deux corrections à ce finding, mesurées par mutation** : (a) `Utils::from_string` **PEUT**
  lever ici — elle copie `val` dans un `istringstream`, c'est de là que vient le `bad_alloc` ; donc
  la rentrer dans le `try` suffit à faire **survivre** le processus (mutation M1 : garde déplacée,
  `ALonePathBracketDoesNotKillTheProcess` reste vert). (b) Le jeton **`[]`** — voisin non signalé —
  lisait un `int` **NON INITIALISÉ** sur master (`from_string("")` laisse sa destination intacte,
  le sentry échoue avant `num_get`) ; corrigé par `int idx = 0`, ce qui converge avec T3.25.
  ⚠️ **T3.25 ne corrigeait PAS ce plantage** : il naît du `pop_back()`, **avant** `from_string`.
  ⭐ **T3.35b (revue du correctif, 2026-08-25) — LE CORRECTIF AVAIT DÉPLACÉ LE PROBLÈME, et le
  troc était défavorable.** `MqttCtrl::getValue()` posait **`err = false` inconditionnellement**
  avant `getValueJson()`, qui rend une chaîne vide sur **chacun** de ses échecs. Une fois le
  plantage supprimé, `battery_path = "["` **franchissait** le `if (!err)` de l'appelant avec
  `v == ""` et alimentait `double rawValue;` **non initialisé** (idem `wireless_signal`, `uptime`).
  **Mesuré** : la même forme compilée avec `-ftrivial-auto-var-init=pattern` publie un niveau de
  batterie de **≈ −5,3·10³⁰⁷ %**. Un plantage est visible et diagnosticable ; une valeur inventée
  remonte dans l'interface et **les règles agissent dessus**.
  ⭐ **La racine était le drapeau, pas les lectures.** `err` disait « un payload est arrivé », ses
  **neuf** sites d'appel l'entendent tous comme « une valeur en est sortie » et traitent
  `err == true` comme « ignorer cette mise à jour » ⇒ le rendre honnête ne peut transformer qu'une
  valeur inventée en mise à jour sautée. Le drapeau est désormais produit **par le parseur**
  (surcharge à 4 arguments de `getValueJson()`) ; les lectures sont gardées **en second verrou**,
  parce que cette branche doit être juste **sans T3.25**, qui n'est pas mergée et peut être revertée.
  ⭐ **`MqttCtrl::getValueColor()` — site que T3.25 ne couvre PAS**, avec ou sans elle : `double x,
  y; int b;` non initialisés et `err` effacé par **n'importe laquelle** des trois lectures réussies
  ⇒ `path_x` cassé + `path_y` bon = `fromXYBrightness()` reçoit un `x` **jamais écrit** ;
  `from_string` n'est même pas appelée sur le chemin fautif. Corrigé : initialisation, **ET** au
  lieu du OU, et exigence d'un **nombre**.
  ⚠️ **Recompte des locales non initialisées** de `MqttCtrl.cpp` + `WebCtrl.cpp` : **8 variables sur
  5 sites** (3 `rawValue`, `x`/`y`/`b`, `coeff_a`/`coeff_b`), et non 4 — le compte de 4 portait sur
  les **sites** en comptant `getValueColor` pour un et omettait `coeff_a`/`coeff_b`.
  ⚠️ **La garde de T3.35 vérifiait une LONGUEUR quand son message annonçait une FORME.** Mesuré :
  `weather/[5/description` rendait `clear sky` (élément 0) et `weather/[12/description`
  `light rain` (élément 1), **sans rien journaliser** — `pop_back()` mangeait le chiffre. Durcie en
  `val.size() < 2 || val.back() != ']'` ⇒ pas de trou à ficher.
  ⚠️ **Fixture pauvre, motif de la série.** `EXPECT_TRUE(logContains(log, "weather[0]"))` était
  satisfait par la **première** ligne du scénario (`subpath not found weather[0]`), donc le texte de
  l'indication n'était asserté **nulle part** : supprimer `suggestion.insert(...)` faisait proposer
  *le chemin fautif lui-même* pour **0 rouge**. Aiguille corrigée en `"did you mean weather/[0]"` ⇒
  la mutation donne **2 rouges**. **Balayage des autres assertions** : **un seul scénario de toute
  la suite émet plus d'une ligne de journal**, donc **2** assertions au total pouvaient être
  satisfaites par une ligne antérieure — les deux corrigées. Famille voisine distincte : **4**
  assertions satisfaites par une autre partie de **la même** ligne (l'écho du `path` au lieu du
  jeton), 2 corrigées, 2 laissées avec leur raison.
  ⚠️ **`int idx = 0` n'était retenu par AUCUN test, et ne peut pas l'être.** La mutation
  `int idx = 0` → `int idx;` reste à **0 rouge** après correctif — mais c'est désormais **correct** :
  l'index est assigné sur **toutes** les branches, donc l'initialiseur est mort et le mutant est
  **équivalent par construction**. Un test ne peut pas détecter une lecture non initialisée de façon
  portable ; la sortie est de rendre le code **déterministe** et d'épingler ce qu'il **décide** —
  ce que font deux mutations qui rougissent (valeur : 7 rouges, avertissement : 4 rouges).
  ⚠️ **Agrégat SHA-256 des goldens `2a3526f0…` : RETIRÉ de la fiche.** Irreproductible — 13 recettes
  essayées au total (5 par le relecteur, 8 de plus ici), aucune ne le redonne. Le fait vérifiable
  qui le remplace : **145 goldens, arbre `d4ebc61f`**. ⚠️ **Diffstat de `3db14a92` corrigé** :
  **+148/−6** et non +134/−6.
  ⚠️ Voisin mesuré au même endroit et **figé, lui** : un index **non numérique** (`[zz]`) lit
  silencieusement l'**élément 0** — `Utils::from_string` laisse sa destination à 0 et son retour
  n'est pas regardé (famille T3.25).

- **[BUG, ⭐ visible par tous les utilisateurs] La syntaxe d'index de tableau publiée par l'ioDoc
  ne fonctionne pas** — ✅ **TICKETÉ : [T3.29](T3.29.md)** (2026-08-25) — ✅ **LIVRÉ** sur `fix/t3.29` (`a8be430b` caractérisation, `6154ef93` correctif).
  ⚠️ **Deux chiffres du finding ne se reproduisent pas** — voir T3.29.md §5.3/§5.4 : les occurrences
  hors `build_doc/` sont **92**, pas 110 (`po/en@quot.po` et `po/en@boldquot.po`, 32 à eux deux,
  sont **générés et non suivis**) ; et **aucune traduction n'est invalidée** — 7 des 8 entrées
  n'étaient traduites nulle part, la 8ᵉ (fr, `path` Web) a été corrigée avec son `msgid`, `fr.po`
  reste à 404 traduits / 6 fuzzy / 24 non traduits. **Le « 8 chaînes dans 2 fichiers » est exact.**
  ⭐ **Ce que ni le finding ni la fiche n'avaient vu, et qui tranche l'option « accepter aussi
  l'ancienne syntaxe »** : `weather[0]` n'est pas un jeton inerte, c'est une **recherche de clé
  d'objet valide** — sur une charge `{"weather[0]": {…}}` la forme collée **résout** (test
  `AGluedIndexStillMatchesAKeySpelledThatWay`). Rendre le parseur tolérant aux deux formes
  **casserait** ce cas en silence, et les noms de clés des charges MQTT ne sont pas sous notre
  contrôle. La doc seule est donc corrigée.
  ⚠️ **Références recalées sur master `db6770a7`** : les 7 sites MQTT sont
  `MqttCtrl.cpp:383,386,390,394,398,402,405` (et non `:415` / `:418…:437`), le site Web est
  `WebDocBase.cpp:53-57` (littéral à `:56`, chaîne concaténée sur 5 lignes) ; les parseurs sont
  `MqttCtrl.cpp:114,125` et `WebCtrl.cpp:184,195`. **Le « ~8 » est exact : 8 chaînes, 2 fichiers.**
  ⭐ **Ce que la fiche d'origine n'annonçait pas : le gros du travail est dans les catalogues** —
  `weather[0]` apparaît **110 fois hors `build_doc/`**, dont 8 dans `src/`, 8 dans `calaos.pot`,
  9 dans `fr.po`, 8 dans chacun de `de/es/hi/nb/pl/ru`, et 16 dans chacun de `en@quot`/`en@boldquot`
  (msgid + msgstr). Changer les msgid invalide les traductions de 9 catalogues. Les descriptions des paramètres `path` de MQTT (`MqttCtrl.cpp:415`, et les
  six `*_path` de statut `:418…:437`) et des IOs Web (`WebDocBase.cpp:53`) donnent toutes l'exemple
  **`weather[0]/description`**. Or les deux parseurs — qui sont le même code dupliqué —
  découpent le chemin **sur `/` seul** et ne traitent un jeton comme index que s'il **commence
  par `[`** (`MqttCtrl.cpp:146,157` ; `WebCtrl.cpp:185,196`). Le jeton `weather[0]` part donc en
  `parent.at("weather[0]")`, échoue, la valeur rendue est **vide** et un
  `Error in path …, subpath not found` est journalisé (`MqttCtrl.cpp:177-186`). **La forme qui
  marche est `weather/[0]/description`** — l'index doit être son propre segment.
  ⚠️ Ce n'est pas une coquille documentaire : ces chaînes alimentent `--gendoc` **et
  calaos_installer**, donc **tous les utilisateurs voient la mauvaise syntaxe** au moment où ils
  configurent un capteur MQTT dont le payload contient un tableau — cas très courant
  (Zigbee2MQTT, OpenWeather). Corrigé dans `02_io_drivers.md` (§MQTT et §Web) ; **le code, lui,
  mérite son ticket** : corriger les ~8 chaînes `_()` (et re-vérifier les `.po`).
- **[BUG] `RoonPlayer`** — ✅ **TICKETÉ : [T3.28](T3.28.md)** (2026-08-25), avec le finding
  `RoonPlayer.cpp:39-50` d'E4.5d ci-dessous : **un seul ticket pour les deux**.
  ⚠️ **Trois corrections apportées par le ticket** : le `paramAdd` est à **`RoonPlayer.cpp:180`**
  (et non `:174`) et le `from_string` à **`:185`** (et non `:179`) — décalage de +6 dû au bloc de
  commentaire inséré par E4.1g à `:159-164` ; et **`from_string("")` ne laisse PAS `port` à 0, elle
  le laisse INDÉTERMINÉ** (`RoonPlayer.h:214` : `int port;` sans initialiseur, hors liste d'init).
  ⚠️ **« Roon inutilisable » est trop fort** : avec `host` vide — le mode par défaut annoncé —
  `args` reste vide, aucun `--port` n'est passé et l'autodétection fonctionne. **Seule la
  configuration à hôte statique est cassée.** Aucun test sur matériel réel, ni ici ni là.
  **`port` est déclaré obligatoire sans défaut, et `--port 0` part au
  sidecar.** `paramAdd()` a pour signature
  `(name, description, ParamType, bool mandatory, string defaultval = "", bool readonly = false)`
  (`IO/IODoc.h:46`). `RoonPlayer.cpp:174` écrit
  `paramAdd("port", …, IODoc::TYPE_INT, 9330)` : le `9330` occupe la place de **`mandatory`** (donc
  `true`) et `defaultval` reste **vide**. À l'exécution `Utils::from_string("")` laisse `port` à
  **0** (`RoonPlayer.cpp:179`), et si `host` est renseigné c'est **`--port 0`** qui est passé au
  sidecar (`RoonPlayer.cpp:46-48`) ; le défaut 9330 n'existe que côté Python **quand le drapeau
  est absent**, ce qui n'arrive alors jamais. → mini-ticket : `paramAddInt("port", …, 0, 65535,
  false, 9330)`.
  ⚠️ **Rapprochement avec E4.5d, qu'aucun des deux tickets ne pouvait faire seul** : E4.5d a
  trouvé en parallèle que `RoonPlayer.cpp:43` **perd `--host` et `--port` au respawn** (le
  `startProcess(exe, "roon")` du handler `processExited` ne repasse pas `args`). Pris ensemble —
  port 0 au premier lancement, host et port perdus à chaque relance — **il est probable que
  l'intégration Roon soit inutilisable en configuration à hôte statique aujourd'hui**. À
  confirmer sur matériel avant de trancher, mais les deux défauts sont sur le même chemin et
  méritent un seul ticket.

  ✅ **LIVRÉ par [T3.28](T3.28.md) (`fix/t3.28`, 2026-08-25).** Quatre corrections apportées à
  cette entrée, **mesurées** :
  1. **`paramAdd` n'a ni `min`, ni `max`, ni validation** — `type` ne sert qu'à `typeToString()`
     (`IODoc.cpp:52-63`). Un `TYPE_INT` déclaré par `paramAdd` est donc un entier **sans bornes
     annoncées** ; seul `paramAddInt` émet `min`/`max` **et** émet `default` inconditionnellement.
     Et `defaultval` restant `""`, le `if (!defaultval.empty())` de `:59` fait que la clé
     `"default"` **n'existe pas du tout** — pas `"default": ""`, **pas de clé**. Vérifié à
     l'exécution sur un vrai `RoonPlayer`.
  2. **Le défaut Python `9330` n'existe que drapeau ABSENT — confirmé au source.**
     `ExternProcRoon_main.py:43` : `add_argument('--port', default=9330)` **sans `type=int`** ⇒
     drapeau absent = **entier** `9330`, drapeau présent = **chaîne** telle quelle, **aucune
     validation**. `:42` : `--host` n'a **aucun** `default` ⇒ `None` ⇒ `get_roon_host()` bascule
     sur `RoonDiscovery` (`:75-83`), qui **ignore le port**. Une valeur **négative** (mémoire
     indéterminée) passe `argparse` sans broncher — `_negative_number_matcher` — et arrive
     jusqu'à `RoonApi`.
  3. **`from_string` rend `true` sur une SURCHARGE** en écrivant `INT_MAX`
     (`"99999999999999999999"` → `true`, `2147483647`).
     ⚠️ **Rectifié après revue** : la première rédaction disait « ni cette fiche ni T3.25 ne le
     disaient », ce qui est **faux pour la saturation**. `FINDINGS.md:1546` écrit déjà « **très
     grand** → **sature à `INT_MAX`**, donc inoffensif », `T3.19.md:86` porte la même ligne, et
     `tests/core/JsonApiSession_test.cpp:1120-1122` la **fige** (`EXPECT_EQ(2147483647, perPage)`,
     commentaire « Overflow does NOT zero it, it saturates ») — `JsonApiInputGuards_test.cpp:915`
     fige la même valeur sur le fil. **Ce qui était inédit, c'est le RETOUR `true`** : ces sources
     décrivent ce qui est **écrit** et concluent « inoffensif » **pour `per_page`** (destination
     amorcée à `100`), aucune ne dit que `from_string` **répond succès** sur une entrée qu'aucun
     `int` ne représente — donc qu'un appelant qui *teste* le retour est aussi exposé qu'un
     appelant qui l'ignore.
  4. **Après T3.25**, le défaut non corrigé donnerait `--port 0` — **déterministe au lieu
     d'aléatoire, toujours faux** : le symptôme change, la cause non.
     `RoonArgs::portFromParams()` amorce sa destination avec `9330` et répond donc **la même chose
     avant et après T3.25** ; les deux tickets restent indépendants.
     ⚠️ **Rectifié après revue** : la fiche affirmait que T3.25 ne change que le régime « chaîne
     blanche ». **Faux** — T3.25 rend `!fail() && eof()` au lieu de `eof()`, donc le régime de
     **débordement** change aussi de retour : `"99999999999999999999"` rendait `true`/`INT_MAX`,
     il rend `false`/`INT_MAX`. Deux régimes sur trois bougent. Sans effet ici (la plage filtre
     `INT_MAX` dans les deux mondes), mais la fiche le disait mal.
  ⛔ **Aucun test sur matériel réel** — ni ici, ni en E4.5c, ni en E4.5d. C'est dit dans la fiche.

  ⚠️ **[BUG, PRÉ-EXISTANT, NON INTRODUIT ET NON CORRIGÉ PAR T3.28] Un `host` contenant un ESPACE
  met le sidecar Roon dans une boucle de relance à 100 ms** — ✅ **fiché : [T3.28a](T3.28a.md)**
  (2026-08-25). `RoonArgs::buildArgs()` concatène `host` sans le protéger :
  `" --host " + host + " --port " + to_string(port)`. `ExternProcServer::startProcess()` découpe
  cette chaîne sur `" "` (`Utils::split`) et la remet dans un `CStrArray` — un `host` valant
  `"192.168.7.42 --foo"`, ou simplement `"mon core"`, produit donc des **argv supplémentaires**
  qu'`argparse` refuse (`ExternProcRoon_main.py`, `parse_args()` sort en `SystemExit(2)`). Le
  process meurt aussitôt, `processExited` relance, **et il n'y a aucun backoff** (le même trou que
  « Sept contrôleurs sur huit respawnent leur sous-processus sans aucun délai », plus bas dans
  cette même section E4.5d, recense) : le serveur reboucle sur un
  `fork`/`exec` par ~100 ms tant que la configuration reste en place.
  ⚠️ **Ni introduit ni aggravé par T3.28** : avant le ticket, `args` était assemblé exactement de
  la même façon à `RoonPlayer.cpp:46-48`. T3.28 a **déplacé** cette concaténation dans
  `RoonArgs::buildArgs()`, ce qui la rend pour la première fois **testable** — mais n'y touche pas.
  Le champ vient de `calaos_installer` et n'est pas validé ; ce n'est pas une escalade de
  privilèges (l'attaquant doit déjà pouvoir écrire `io.xml`), c'est un **déni de service par
  faute de frappe**. Correctif hors périmètre T3.28 : voir `T3.28a.md`.

---

## E4.5d — écarts trouvés en réécrivant `12/14/15` contre le code (hors périmètre, non corrigés)

Tous mesurés au source le 2026-08-24, aucun corrigé (le ticket est de la doc pure, invariant
« aucune ligne de `src/` »).

- **[BUILD, casse à l'exécution] La sonde MCP de `configure.ac:218-219` ne teste pas
  `websockets`.** `$PYTHON -c "import mcp, uvicorn, fastapi"` active `HAVE_PYTHON_MCP`, mais
  `calaos_mcp/client.py:16` fait `import websockets`, qui est bien une dépendance déclarée de
  `src/bin/calaos_mcp/pyproject.toml:20`. Une machine sans `websockets` **construit et installe**
  le sidecar, qui échoue ensuite à l'import — le manager le respawne alors indéfiniment
  (backoff plafonné à 60 s). Idem pour `pydantic`/`starlette`, tirés en transitif par fastapi/mcp
  mais non sondés. → ajouter `websockets` (au moins) à la sonde et à la ligne
  `pip3 install` du message d'aide (`configure.ac:225`).
- **[DOC-DANS-LE-CODE FAUSSE] `ConfigOptions.cpp:886-889` décrit mal `mcp_rate_limit`.** La
  chaîne dit « Maximum number of **authentication attempts** the MCP sidecar accepts from one IP
  address ». Or `auth.py:150-156` incrémente la fenêtre glissante **avant** la vérification du
  Bearer, pour **toute** requête, `tools/call` réussi compris : l'option plafonne le **trafic**,
  pas les tentatives d'authentification. `mcp_ban_failures`, lui, est bien décrit (il compte les
  échecs). Cette chaîne est rendue à l'utilisateur par l'ioDoc → à corriger dans un ticket qui a
  le droit de toucher `src/`.
- **[SURFACE MORTE, mesurée] Trois éléments du sidecar MCP sont câblés et jamais atteints.**
  (a) `client.py:156-158` `CalaosClient.autoscenario()` — **aucun des 9 tools ne l'appelle**
  (`grep -rn autoscenario src/bin/calaos_mcp tests/python` : seules les 3 lignes de la définition
  et la mention dans le frozenset mort ci-dessous). (b) `client.py:23-27` `_ALLOWED_ACTIONS`,
  un `frozenset` d'actions « permises sous la portée service », **référencé nulle part** : il ne
  filtre rien, et il porte le même nom que celui de `tools/audio.py:5-8`, qui lui est bien utilisé
  — piège de lecture. (c) `models.py` : les modèles pydantic `IO`/`Room`/`Scenario`/`AudioPlayer`
  sont installés (`Makefile.am:23`) et **importés par aucun module**. → soit retirer, soit brancher ;
  en l'état la doc devait explicitement dire que ce n'est pas utilisable, ce que fait désormais
  `docs/15_mcp_server.md`.
- **[FIABILITÉ] `RoonPlayer.cpp:39-50` — le respawn perd `--host` et `--port`** — ✅ **TICKETÉ
  avec le finding `paramAdd` d'E4.5c : [T3.28](T3.28.md)** (2026-08-25). Références **vérifiées
  exactes** (`:43` sans args, `:50` avec, `:46-48` la construction). ⚠️ Le respawn **sans délai**
  des sept contrôleurs (puce suivante) est **délibérément laissé hors de T3.28** : sept fichiers,
  sept propriétaires, ticket transverse à part. Le premier
  `startProcess(exe, "roon", args)` (`:50`) passe `--host <ip> --port <n>` construits depuis la
  configuration, mais le handler `processExited` (`:43`) rappelle `startProcess(exe, "roon")`
  **sans args**. Après le premier redémarrage, le sous-processus retombe donc sur la découverte
  automatique `RoonDiscovery` (`ExternProcRoon_main.py:75-83`) au lieu du core configuré — et se
  connecte potentiellement au mauvais core, ou à aucun. → mini-ticket : capturer `args` dans le
  lambda.
  ✅ **LIVRÉ par [T3.28](T3.28.md).** ⚠️ **Ce n'était pas un oubli de capture** : `args` était
  construit **après** le `connect`, donc `[=]` ne *pouvait pas* le capturer. Corrigé
  **structurellement** et non par une capture : `RoonCtrl::procArgs` est construit avant le
  `connect` et `RoonCtrl::launch()` est **le seul site de lancement** du fichier — les deux chemins
  ne peuvent plus diverger parce qu'il n'y en a plus qu'un. Une tripwire source
  (`tests/core/RoonArgs_test.cpp`, via `CALAOS_TOP_SRCDIR`) compte ce site unique, `RoonCtrl` étant
  inatteignable depuis `make check`.
- **[FIABILITÉ] Sept contrôleurs sur huit respawnent leur sous-processus sans aucun délai.**
  `MqttCtrl.cpp:43-48`, `KNXCtrl.cpp:36-41` et `:48-53`, `OLACtrl.cpp:31-36`, `OwCtrl.cpp:33-38`,
  `ReolinkCtrl.cpp:37-43`, `RoonPlayer.cpp:39-44` rappellent `startProcess()` directement depuis
  `processExited`. Un binaire qui échoue au démarrage (dépendance Python absente, broker
  injoignable au point de faire sortir le process) donne une **boucle de spawn serrée**, seulement
  freinée par le `Timer::singleShot(0.1, …)` de `ExternProc.cpp:191`. Seul `WagoMap` a un backoff
  (`WagoMap.h:165-172`, 1/2/3/5 s, plafond volontairement bas), et `McpServerManager` en a un autre
  (`:284`, 1→60 s). → généraliser le backoff de `WagoMap` : c'est déjà noté comme table
  quasi-dupliquée (`McpServerManager.cpp:284` vs `WagoMap.h:172`).
- **[SÉCURITÉ, throttle — élargi par la revue E4.5d, ticket `T3.24` lancé] `clientIp()`
  n'utilise pas `X-Forwarded-For`, sur les DEUX transports de login.** `JsonApiHandlerWS.cpp:45-51`
  **et** `JsonApiHandlerHttp.cpp:55-61` portent le **même corps** : ils renvoient
  `HttpClient::getClientIp()`, c'est-à-dire le **pair TCP** (`HttpClient.cpp:710-732`, qui lit
  `peer<uvw::IPv4>()`), alors que `TransportLimits::effectiveClientIp()` (`HttpClient.h:144-155`)
  existe précisément pour donner l'identité vue par haproxy et est bien employé par le plafond
  `max_connections_per_ip` (`HttpClient.cpp:200-203`). Les deux valeurs alimentent
  `LoginThrottle` : `JsonApiHandlerWS.cpp:121,131` côté WS, `JsonApiHandlerHttp.cpp:104,122,137`
  côté HTTP. Conséquence : derrière haproxy le pair est **toujours** le proxy, donc **tous les
  utilisateurs partagent un seul seau `LoginThrottle`** — un attaquant peut bloquer le login de
  tout le monde, et son propre backoff est celui du proxy. N'affecte **pas** `login_service` :
  le sidecar MCP se connecte en loopback, où le pair **est** la bonne identité. La revue avait
  d'abord été consignée WS-seulement ; c'est elle qui a trouvé le jumeau HTTP.
  → `T3.24` : faire passer les deux `clientIp()` par `effectiveClientIp()`.
- **[DOC — corrigé dans ce ticket, consigné pour mémoire] `docs/12_extern_proc.md` et
  `docs/14_python_extern_proc.md` étaient faux sur le framing.** Le premier décrivait un octet de
  début `START = 0x02` **qui n'existe pas** et une longueur sur **2 octets** (max 65535) ; le
  second en tirait une note affirmant une **incompatibilité 4 octets Python / 2 octets C++** et
  invitait à « vérifier la compatibilité ». Le format réel est le même des deux côtés : 1 octet
  d'opcode `0x21` + 4 octets de longueur big-endian, plafond 4 MiB côté C++
  (`ExternProc.h:37-44,57`, `message.py:36-45,73-84`). Le `12` portait en outre un **exemple MQTT
  entièrement inventé** (`{"action":"subscribe"}`, `{"type":"connected"}` — aucun de ces messages
  n'existe), une ligne de table décrivant un **sous-processus Reolink C++** inexistant, et un
  chemin de socket `$CALAOS_HOME/run/` que le code ne consulte jamais. Même classe que les
  défauts d'E4.0f : la doc n'était pas périmée, elle était fausse.

---

## E4.5e — écarts trouvés en réécrivant `07_remoteui` / `09_lua_scripting` / `13_utility_lib`

> Tous **hors périmètre** d'E4.5e (documentation pure : aucune ligne de `src/`, aucun test).
> Vérifiés au source le 2026-08-24.

### Documentation — trous restants, dans d'autres périmètres

- **[DOC, → E4.5a] `--enable-asan` n'est documenté dans aucun `docs/*.md`.** L'option existe
  (`configure.ac:241-266`, `ASAN_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1"`,
  désactivée par défaut, affichée dans le résumé de `configure` à `:331`). Sa place naturelle est
  la section « dépendances / build » de `00_overview.md`, qui n'est pas dans le lot E4.5e.
  Elle ne peut **pas** aller dans `16_config_options.md` : ce document est généré depuis le
  registre des clés de `local_config.xml`, et `--enable-asan` n'est pas une clé de configuration.
- **[DOC, → E4.5f] Le plafond d'en-têtes HTTP de 32 Kio → `431` n'est documenté nulle part.**
  `src/bin/calaos_server/HttpClient.h:72-76` (`static constexpr std::size_t MaxHeadersSize =
  32 * 1024;`, « Not configurable on purpose »), réponse construite en
  `HttpClient.cpp:47-49` / `:191`. `grep -rl 431 docs/` ne renvoie que `03_rules_engine`,
  `04_scenarios` (sans rapport) et les documents de refactoring — **pas** `08_http_api.md`, qui est
  le lecteur naturel. Fixe et non configurable, il n'a pas d'entrée dans le registre non plus.

### RemoteUI — la spec en arbre diverge du code

`src/bin/calaos_server/RemoteUI/remote-ui.md` est un document de **spécification** ; trois points
ne décrivent pas ce que le code fait :

1. **`device_info` en XML** : la spec décrit des enfants `<calaos:param name=… value=…/>`
   (`remote-ui.md:534-540`) ; le code écrit et relit des **attributs**
   (`IO/RemoteUI/RemoteUI.cpp:261-262` et `:157-158`).
2. **Emplacement des appareils** : la spec les met dans une section `<calaos:remote_uis>` sous
   `<calaos:home>` (`remote-ui.md:524-526`) ; le chargeur lit `<calaos:remote_ui>` comme enfant
   d'une **pièce** (`Room.cpp:171`).
3. **Clé du code de provisioning** : `provisioning_code` dans la spec (`remote-ui.md:43-46`),
   `code` dans le code (`RemoteUIProvisioningHandler.cpp:117` et `:246`).

→ soit corriger la spec, soit la marquer explicitement comme « cible, non implémentée ».

### RemoteUI — incohérences internes au code

- **[MORT] Chaîne de type d'IO cherchée à trois valeurs différentes.** `REGISTER_IO(RemoteUI)`
  (`IO/RemoteUI/RemoteUI.cpp:84`) fixe le type à `"RemoteUI"`, mais `RemoteUIManager` teste
  `"RemoteUI" || "remote_ui_output"` (`RemoteUIManager.cpp:90-91` et `:117-118`) tandis que
  `RemoteUIProvisioningHandler` teste `"RemoteUI" || "remote_ui"`
  (`RemoteUIProvisioningHandler.cpp:151`). Les branches alternatives sont **mortes** et
  divergentes. → nettoyage.
- **[DOC/IODOC] Onze paramètres lus sans être déclarés dans `ioDoc`** : `name`, `brightness`,
  `timeout`, `theme`, `room` (`IO/RemoteUI/RemoteUI.cpp:493-497`, `:511`, `:525`) et les huit
  `screensaver_*` (`RemoteUIWebSocketHandler.cpp:272-279`). Ils n'apparaissent donc pas dans
  `io_doc.json`, alors qu'ils sont poussés à l'appareil dans `remote_ui_config_update`.
- **[CORRECTNESS, confirme le finding T1.6 « stoi non gardé »]** la conséquence observable est
  pire que « throw » : l'exception de `std::stoi(get_param("brightness"))` /
  `std::stoi(get_param("timeout"))` (`IO/RemoteUI/RemoteUI.cpp:496-497`) est avalée par le
  `catch (const std::exception &e)` de `RemoteUIWebSocketHandler::processApi`, qui la journalise
  comme **« JSON parse error »** (`RemoteUIWebSocketHandler.cpp:151-154`). L'appareil ne reçoit
  aucune réponse `remote_ui_config` et le log accuse le mauvais coupable.
- **[COMPORTEMENT, plus grave qu'il n'y paraît] La décision de mise à jour OTA** — ✅ **TICKETÉ :
  [T3.26](T3.26.md)** (2026-08-25), **bloqué sur décision utilisateur**.
  ⚠️ **Deux nuances ajoutées par le ticket, mesurées** : (1) le comportement est **DÉLIBÉRÉ**, le
  commentaire `:247-248` dit *« This allows switching between dev and release branches »* — ce
  n'est donc pas un oubli mais un arbitrage à rouvrir ; (2) **ce n'est pas un vecteur à distance** :
  `OtaHttpHandler` n'expose **aucun endpoint d'envoi** (seulement `GET …/download` et
  `POST …/ota/rescan`, **localhost uniquement**, `OtaHttpHandler.cpp:68`), donc déposer un firmware
  demande un accès en écriture au système de fichiers. Le risque réel est l'**erreur
  d'exploitation** (restauration, retour de release), pas l'attaque. **Non vérifié** : si
  l'appareil installe seul la notification — c'est un autre dépôt, et ça décide de la gravité.
  C'est une *égalité*
  de chaînes**, pas une comparaison sémantique de versions : le seul test est
  `if (firmware->getVersion() == currentVersion)` → « à jour, on ne propose rien »
  (`OtaFirmwareManager.cpp:249-253`). Il n'y a **aucun** ordre : toute version *différente* de
  celle de l'appareil est annoncée comme une mise à jour disponible, **y compris une version plus
  ancienne**. Déposer un firmware plus vieux dans le répertoire d'un `hardware_id` suffit donc à
  faire proposer un **downgrade** à tout le parc concerné, silencieusement. S'ajoute le fait que
  la comparaison est textuelle, donc `1.10.0` et `1.9.0` ne sont de toute façon pas ordonnables
  ici. → ticket : comparer sémantiquement et ne proposer que du strictement supérieur (ou rendre
  le downgrade explicite et opt-in).

### Lua — quirks d'API mesurés en documentant `09_lua_scripting`

- **[API, conséquence identifiée] `setIOParam` et `waitForIO`** — ✅ **TICKETÉ : [T3.27](T3.27.md)**
  (2026-08-25). ⚠️ **Le correctif vient APRÈS `E4.1j`**, dont `ScriptBindings.cpp` est le périmètre
  exclusif et qui est **en cours**. Toutes les références de cette puce ont été **revérifiées
  exactes** (`:293`, `:333`, corps `:254-292` et `:300-331`, `Lunar.h:130-137`). Nuance ajoutée
  par le ticket : les chemins d'erreur réels passent par `lua_error()`, donc **lèvent** — le défaut
  est un **mensonge d'API** (il fait écrire des gardes qui ne gardent rien), pas une perte de
  détection. Ils déclarent `return 1` sans rien
  empiler** sur leur chemin de succès (`ScriptBindings.cpp:293` et `:333` ; corps vérifiés
  `:254-292`, `:300-331`). Ce n'est pas « une valeur indéterminée » : `Lunar::thunk` retire
  `self` puis laisse **les arguments de l'appel** sur la pile avant d'invoquer la méthode
  (`Lunar.h:132-136`), et `return 1` demande à Lua de prendre le **sommet de pile** comme unique
  résultat. Le script récupère donc, de façon parfaitement reproductible, **le dernier argument
  qu'il vient de passer** — `calaos:setIOParam(id, key, val)` renvoie `val`, et
  `calaos:waitForIO(id)` renvoie `id`. Un script qui teste ce retour croit lire un statut de
  succès et lit en réalité son propre argument, ce qui est **toujours vrai** pour une chaîne non
  vide. → soit `return 0`, soit empiler un vrai statut.
  ✅ **LIVRÉ par [`T3.27`](T3.27.md) (2026-08-25) : `return 0`, PAS un booléen empilé.**
  ⚠️ **Ce choix DIVERGE de la recommandation écrite dans la fiche du ticket** (§2, « Recommandation :
  le booléen »), et le motif est **mesuré**, pas stylistique. Deux constats :
  (a) **la convention de la table est sans ambiguïté** — balayage `python3` des ~~**9**~~ ⛔ **13** (voir plus bas) **`lua_CFunction`
  de tout l'arbre**, toutes dans ce fichier : les 3 **accesseurs** (`getIOValue`, `getIOParam`,
  `getEnv`) rendent `1` et empilent leur valeur ; les 4 **mutateurs/actions** (`setIOValue`,
  `requestUrl`, `sendPushNotif`, `Lua_print`) rendent `0`. `setIOParam` est le **frère direct** de
  `setIOValue`. **Aucun binding de l'arbre ne rend un booléen de succès** — le proposer, c'est
  inventer une convention, pas s'aligner.
  (b) ⭐ **il n'y a AUCUNE information de succès à empiler** : `LuaIOBase::set_param()` est `void`
  et poste un message **sans réponse** sur la socket `ExternProc` (`:502-509`) ; `waitForIO()` ne
  revient normalement que si `waitForIOChanged.emit()` a répondu vrai **et** que `abort` est faux —
  le cas `abort` **lève**. ⇒ `lua_pushboolean(L, true)` serait une **CONSTANTE `true`**, donc
  `if calaos:waitForIO(io) then` resterait vrai **pour toujours** : c'est exactement le défaut, avec
  un nom plus rassurant. La rétro-compatibilité que le booléen achetait était la
  **rétro-compatibilité avec le bug**.
  ⚠️ **Contrepartie assumée, et elle est réelle** : la bascule est **silencieuse**. Un script qui
  écrivait `if calaos:waitForIO(io) then A else B end` passait **toujours** par `A` et passera
  **toujours** par `B`, sans erreur ni ligne de journal. Entrée `RELEASE_NOTES` écrite pour ça, avec
  la liste de ce qu'un utilisateur doit chercher dans ses scripts. **Si un relecteur ou l'utilisateur
  préfère le booléen malgré (b), le retour arrière est d'UNE ligne par fonction** — les deux `return
  0` de `ScriptBindings.cpp` (`:311`, `:360`) et les six cas ⭐ du filet.
- **[API] `requestUrl()` jette le corps de la réponse.** Aucun des deux chemins ne connecte
  `m_signalCompleteData` ni ne renvoie quoi que ce soit (`ScriptBindings.cpp:344-354`,
  `:361-367`, `return 0` à `:376`) : un script peut déclencher une requête HTTP mais **ne peut pas
  en lire le résultat**. Limitation réelle de l'API scriptable, pas un bug de sécurité.
- **[FOOTGUN] `ScriptManager::abortScript()` ne remet jamais `abort` à `false`**
  (`ScriptManager.h:90`) : une instance abortée abortera tous les scripts suivants. Sans effet en
  production, le processus `calaos_script` sortant après un seul script
  (`ScriptExtern_main.cpp:144`), mais c'est une bombe si l'exécution en-processus revient un jour.
- **[API] Un global `Calaos` (majuscule) est exposé en plus de `calaos`.** `Lunar::Register()`
  dépose la table de méthodes dans les globales sous `T::className` (`Lunar.h:25-26`,
  `ScriptBindings.cpp:122`), entrée `new` comprise. Le constructeur lève systématiquement
  (`ScriptBindings.cpp:128-133`), donc c'est inoffensif — mais c'est de la surface non voulue, et
  le message porte une faute de frappe : `"juste use the existing one"`.

### `src/lib` — code mort compilé

- **[MORT] `Calendar.{cpp,h}` n'a aucun consommateur.** `Calendar.h` n'est inclus que par son
  propre `.cpp` et listé dans `src/lib/Makefile.am:53-54` — il est compilé dans
  `libcalaos_common` et rien ne l'appelle. Il n'offre par ailleurs **aucune** gestion de jours
  fériés, et `TimeRange` ne l'utilise pas (le seul `#include "sunset.h"` de l'arbre est
  `TimeRange.cpp:24`, et il ne tire pas `Calendar.h`) —
  les deux affirmations contraires étaient dans `13_utility_lib.md` et ont été retirées.
  → candidat à la suppression.
- **[MORT] `CalaosModule.h` n'est inclus par aucun fichier de l'arbre** (seule occurrence :
  `src/lib/Makefile.am:52`). C'est l'API de modules-widgets de l'interface tactile, fondée sur EFL
  (`Evas`/`Ecore`/`Edje`, `CalaosModule.h:24-31`), dépendances que le serveur ne lie plus.
  → candidat à la suppression.

---

## I4.1 — deux préexistants de `calaos_installer` (dépôt **externe**, non corrigés)

Trouvés en instruisant I4.1 (purge silencieuse des règles à id non résolu). **Hors périmètre du
ticket**, laissés intacts, consignés ici pour ne pas les perdre. Dépôt :
`/home/raoul/repos/calaos/calaos_installer`.

- **[BUG] `Action::duplicate()` (`Calaos/Action.cpp:43`) oublie `action_touchscreen_cam`.** Tous
  les autres champs sont recopiés, celui-là non : **copier-coller d'une action « touchscreen »
  perd la caméra** qu'elle affichait. Défaut réel, antérieur à I4.1, corrigible en une ligne.
- **[UB latent] `FormConditionStd::qitem` est hors liste d'initialisation.** Non atteint
  aujourd'hui, mais c'est **exactement la même famille** que `Condition::output` — pointeur membre
  non initialisé dont les lecteurs testent `nullptr` — qui, lui, était atteignable et produisait
  un segfault à la sauvegarde (corrigé par I4.1, `6cbd6f6`).

Voir [`I4.1.md`](I4.1.md) §5bis.

---

## E4.5f — vérification de `08_http_api.md` / `10_events_notifications.md`

Les deux documents réécrits par E4.0f étaient **justes sur le fond** ; ce qui les avait périmés
tient à T3.18/T3.19 et à la **dérive des numéros de ligne**. Ce qui a été trouvé et n'est **pas**
corrigé ici, faute de périmètre, est consigné ci-dessous.

### ⚠️ Le throttle de login n'est **pas** par client derrière le reverse proxy (`T3.24`)

Mesuré : les deux transports passent à `LoginThrottle` le résultat de `clientIp()`, qui renvoie
`HttpClient::getClientIp()` — l'adresse du **pair TCP**
(`JsonApiHandlerHttp.cpp:55-60`, `JsonApiHandlerWS.cpp:45-50`, `HttpClient.cpp:710-724`). Le
plafond de connexions par client, lui, résout bien `X-Forwarded-For`
(`HttpClient.cpp:195-215`) : **les deux mécanismes n'ont pas la même notion de « client »**.
Derrière haproxy — le déploiement réel — toutes les sessions partagent l'adresse du proxy et donc
**le même compteur d'échecs** : le délai exponentiel devient global, un tiers en échec bloque les
autres, et le mécanisme ne protège pas ce qu'il annonce protéger.
`docs/08_http_api.md` porte désormais une **réserve** à cet endroit, renvoyant ici. Le défaut
lui-même reste ouvert en `T3.24`. Non corrigé, hors périmètre d'un ticket de documentation.

### Deux refus HTTP n'étaient documentés nulle part — corrigés dans le périmètre

`431` (en-têtes > 32 Kio, `TransportLimits::MaxHeadersSize`, non configurable) et `429`
(`max_connections_per_ip`, 50 par défaut) sont des réponses qu'un client d'API peut recevoir
**avant** que la moindre action JSON ne soit lue, et sans corps JSON. Elles sont désormais dans
`08_http_api.md`, § « Deux refus qui arrivent avant le JsonApi ». La **sémantique des options**
reste du ressort de `16_config_options.md` (E4.5e).

### `--enable-asan` reste non documenté

Signalé par les agents voisins ; c'est une option de **build**, hors du périmètre de `08`/`10`.
Elle appartient à `16_config_options.md` / `README` (E4.5e). Non traitée ici.

### ⭐ Le payload de scénario EST amputé — la doc affirmait l'inverse (trouvé en revue)

Corrigé dans le périmètre, mais consigné parce que c'est le constat que la refonte `E4.6` doit
traiter. `Scenario::toJson()` saute silencieusement toute action dont l'IO ne résout pas
(`if (!sa.io) continue;`, `IO/Scenario.cpp:158` et `:181`) : l'étape amputée est répondue **sans
marqueur**, indiscernable d'une étape qui a toujours eu moins d'actions — et une étape qui perd sa
seule action est répondue avec un tableau `actions` **vide**. Mesuré par les goldens
(`e40c_ws_autoscenario_get.json` : 2 actions à l'étape 2 ; `..._get_broken.json` : 1) et épinglé
sous le nom `ABrokenStepSilentlyLosesItsActionFromThePayload`
(`tests/core/JsonApiScenario_test.cpp:1771`).

**Conséquence client** : `steps` **ne permet pas** de désigner l'étape en cause ; `missing_ios` est
la seule clé du payload qui nomme ce qui manque. C'est le constat (d) de T3.18.

La première rédaction d'E4.5f affirmait « rien n'est retiré du payload » — une **inversion**, non
une imprécision, sur le point le plus important de la section, et **sans référence**, donc hors de
portée de tout contrôle automatique. Voir l'entrée suivante.

### La dérive des références `Fichier.cpp:ligne` — et la mesure honnête de ce qu'un script y peut

> ✅ **TICKETÉ : [T3.32](T3.32.md)** (2026-08-25). La conclusion ci-dessous est **reprise telle
> quelle** et ne doit pas être rouverte. ⚠️ **Une seule correction, d'unité** : « 36 groupes …
> sur 329 numéros » mélange deux unités. Remesuré en `python3` sur master : `08_http_api.md`
> porte **121 groupes / 237 numéros**, `10_events_notifications.md` **61 / 97**, soit **182
> groupes / 334 numéros** pour les deux ⇒ le ratio est **36 sur ~182 groupes (≈ 20 %)**.
> Surface totale des 16 documents : **1109 groupes / 2074 numéros**.

**36 groupes de références** de ces deux documents (sur 329 numéros de ligne cités) ne pointaient
plus sur ce qu'ils annonçaient — jusqu'à 190 lignes d'écart dans `JsonApi.cpp`, que T3.18/T3.19
ont allongé. Aucune ne pointait sur un fichier disparu, donc **rien ne rougissait** : elles
désignaient simplement une accolade ou une ligne vide. Toutes sont recalculées et revérifiées
dans ce commit.

⚠️ **Un script qui se contente d'afficher la ligne visée ne teste rien.** Il déplace le travail
sur un relecteur humain — c'est-à-dire exactement la situation actuelle. Pour qu'il *teste*, la
doc doit citer non seulement `Fichier.cpp:ligne` mais une **ancre** attendue à cette ligne : un
nom de symbole, une chaîne littérale. Le contrôle devient alors « la ligne N contient-elle encore
`X` ? », qui est falsifiable.

**Mesure honnête de sa portée, faite sur cette branche même** : *aucun* des deux défauts que la
revue y a trouvés n'aurait été attrapé par un tel script.

- **La référence plausible qui ment** — `WebSocket.cpp:314-315` cité pour le plafond de connexions
  par IP, alors que ces lignes sont la limitation d'authentification RemoteUI
  (`AuthFailureReason::RateLimited`). La ligne **existe** et **contient bien `429`** : une ancre
  l'aurait **validée**. Seule une relecture voit que le mécanisme n'est pas celui qu'on annonce.
- **L'affirmation fausse qui ne cite rien** — « rien n'est retiré du payload », alors que
  `Scenario::toJson()` saute l'action dont l'IO ne résout pas. Il n'y a **rien à ancrer** : un
  script ne peut pas contrôler une phrase sans référence. Et c'était le point le plus important
  de la section.

Un troisième cas est venu de la revue elle-même et mérite d'être noté : elle situait ce `continue`
à `IO/Scenario.cpp:160`, il est à **:158**. Celui-là, une ancre l'attrape — c'est exactement le
sous-ensemble que le script couvre, et il est **étroit**.

> **Conclusion retenue, plus modeste que l'intuition de départ.** Le script ne vaut que pour la
> **dérive mécanique**, et seulement s'il est **ancré**. Il est câblé en cible
> **`make check-docs` NON bloquante**, délibérément **hors de `make check`** : un faux rouge sur
> de la doc à chaque refactoring de `JsonApi.cpp` finirait par être désarmé, et *un contrôle qu'on
> désarme vaut moins que pas de contrôle*. Ce qui attrape le reste — la référence qui ment,
> l'affirmation qui ne cite rien — reste la **revue**, et il n'y a pas de substitut. Obligatoire à
> chaque revue de doc dans la suite d'E4.5.


## E4.1b — les invariants d'émission (écarts mesurés, hors périmètre non corrigés)

- ⛔ **[F-E41B-1] LA FICHE `E4.1b.md` SE TROMPAIT : le `std::terminate` était atteignable AVANT
  ce ticket, pas seulement à partir d'E4.1o.** Elle écrivait « aujourd'hui aucun payload
  client-influencé n'atteint ces deux surcharges ». **Mesuré faux.** `eventlog` les atteint toutes
  les deux (`buildJsonEventLog()` est la seule méthode `std::function<void(Json &)>` de
  `JsonApi.h`), et `HistEvent::toJson()` (`HistLogger.cpp:82-103`) recopie **`io_id`, `io_state`,
  `pic_uid` bruts depuis sqlite** dans l'arbre nlohmann. Ces trois chaînes ne traversent **aucun**
  parseur JSON : `EventManager::appendEvent()` (`EventManager.cpp:93`) y met
  `ev.getParam()["state"]`, que `set_state` peut alimenter en **octets percent-décodés** par le
  repli GET de `JsonApiHandlerHttp.cpp:88`. Démonstration exécutée : `SIGABRT` (134),
  `[json.exception.type_error.316] invalid UTF-8 byte at index 0: 0xFF`.
  ⭐ **CONDITION DE PORTÉE, ajoutée par la revue (R1) et que je n'avais PAS mesurée** : la chaîne
  complète exige un **IO journalisé de type CHAÎNE** (`OutputString`/`InputString`). Sur un IO
  booléen ou analogique, `set_value(octets arbitraires)` **renvoie `false` et n'émet rien**, donc
  rien n'est persisté et la chaîne s'arrête à l'étape 1. Mesuré par la revue sur les deux configs
  réelles : `log_history="true"` est **ubiquitaire** (**78** IOs chez `raoulh`, **48** chez
  `solanora`, tous à `"true"`) mais **exclusivement** lumières, volets, scénarios et variateurs
  ⇒ **ces deux installations ne sont pas exploitables telles quelles**. Il faut donc énoncer les
  **deux** moitiés : **tout compte authentifié ordinaire suffit** (aucun contrôle de scope sur le
  dispatch HTTP, aucun `scopeDenied` sur `set_state` même en `serviceScope`, les 7 refus existants
  sont **WS-only**) **et** l'installation doit **posséder un IO chaîne journalisé**.
  ➡️ **Conséquence pour la suite de l'épique** : `E4.1o` n'est pas le premier point où le crash
  devient atteignable, il n'est que le premier où la fiche l'avait vu — et **E4.1o, lui, n'aura
  PAS besoin de cette condition** : il remet une clé fournie par le client directement dans
  l'arbre, sans passer par aucun IO. **Corrigé par ce ticket.**

- ⚠️ **[F-E41B-2] `OtaHttpHandler` renvoie l'identifiant matériel pris DANS L'URI dans un corps
  JSON.** `handleFirmwareDownload()` construit
  `"No firmware found for hardware ID: " + hardwareId` où `hardwareId` est un `uri.substr()`
  brut, validé seulement contre `/`, `..` et le vide — **les octets non-UTF-8 passent**. Le dump
  est désormais durci, donc plus de `terminate`, mais **le reflet d'une portion d'URI non
  assainie dans une réponse reste un motif à surveiller** (le second est
  `RemoteUIProvisioningHandler`, qui reflète des params `io.xml` que pugixml ne valide pas).
  Chemin **derrière l'authentification HMAC** et derrière `OTA_DISABLED`, donc non urgent.
  **Hors périmètre, non corrigé au-delà du durcissement du dump.**

- 📏 **[F-E41B-3] Deux écarts d'inventaire dans la fiche.** `Audio/AVRRose.cpp` porte **9** sites
  `d.dump()`, pas 8. Et `RemoteUI/RemoteUIWebSocketHandler.cpp`, listé comme un site à part, **n'a
  aucun `dump()` nlohmann** : son `sendJson` est `using JsonApiHandlerWS::sendJson;`
  (`RemoteUIWebSocketHandler.h:90`), donc le site WS le couvre ; son `json_dumps()` de `:246` est
  du jansson et appartient à E4.1n. Un nouveau site est apparu pendant le ticket,
  `IO/IOFactory.cpp:117`, posé **déjà conforme** par E4.1k.

- 📏 **[F-E41B-4] Recomptage de la dette `jansson_from_params`, hors du hook `rtk`.** Sur `master`
  `138c16ee` (E4.1k mergé), fichiers **suivis** de `src/` + `tests/` : **100 occurrences sur 100
  lignes**, dont **70 dans `JsonApi.cpp`**. Sur l'arbre suivi **entier** (docs comprises) : **128
  occurrences / 126 lignes**. Les chiffres qui circulaient (104, 102) mélangeaient les deux
  périmètres et/ou une base antérieure. Le classement par fichier est inchangé.

- ⚠️ **[F-E41B-5] Le groupe « wires tiers » de ce ticket n'a AUCUN oracle, et ne peut pas en
  avoir.** `RoonPlayer` (7), `AVRRose` (9), `OWExternProc_main`, `calaos_config`,
  `ConfigOptions` reçoivent le gestionnaire d'erreur **seul** : par construction il ne change
  **rien** tant que les données sont valides, donc **aucune observation** ne distingue l'avant de
  l'après. Ce n'est pas un trou de ce ticket, c'est la définition d'un filet passif — mais il faut
  le savoir : **une contre-mutation sur ces 20 sites ne rougirait rien**, et ce n'est pas un
  symptôme du piège `_DEPENDENCIES`. Les harnais capables de les exercer appartiennent à E4.1d→j.

- 📌 **[F-E41B-6] `log_history` n'est PAS la borne du canal d'injection — le TYPE de l'IO l'est.**
  `EventManager::appendEvent()` (`EventManager.cpp:78`) ne journalise que si l'IO porte
  `log_history == "true"`, et je pensais que c'était là la restriction. **Mesuré par la revue
  (R1) : non.** `log_history="true"` est **ubiquitaire** — **78** IOs chez `raoulh`, **48** chez
  `solanora`, **tous** à `"true"`. La vraie borne est que l'octet invalide doit **persister**, donc
  que l'IO soit de **type chaîne** (`OutputString`/`InputString`) : sur les lumières, volets,
  scénarios et variateurs qui composent **la totalité** des IOs journalisés de ces deux configs,
  `set_value(octets arbitraires)` **renvoie `false` et n'émet rien**. ⇒ ces deux installations
  **ne sont pas exploitables telles quelles** ; une installation **avec un IO chaîne journalisé**
  l'est. Je n'avais pas mesuré cette condition — je m'étais borné à dire que l'ampleur restait à
  recompter, ce qui était vrai mais insuffisant.

## E4.1k — le générateur `io_doc.json` (`IODoc.{h,cpp}`, `IOFactory.cpp`)

### ⭐ La prémisse « les descriptions d'IO sont en français et contiennent des accents » est FAUSSE

`E4.1k.md` annonçait qu'`ensure_ascii` allait **changer visiblement** le fichier versionnable
`io_doc.json`. Mesuré sur `master` `beccf106`, deux fois, indépendamment :

- `grep -rnP '[^\x00-\x7F]' src/bin/calaos_server/IO/` ne trouve du non-ASCII que **dans des
  commentaires** (des tirets cadratins) — **aucune** chaîne de documentation d'IO n'en porte ;
- l'`io_doc.json` **réellement généré** (`calaos_server --gendoc`, 525 588 octets) contient
  **0 octet ≥ 0x80** et **0 séquence `\uXXXX`**.

⇒ `ensure_ascii = true` est un **no-op strict** sur cet artefact aujourd'hui. Il est appliqué quand
même (invariant d'épique, et il tient le fichier en ASCII si une description accentuée apparaît un
jour), mais **il ne fait bouger aucun octet**. La seule différence avant/après est **l'ordre des
clés**. À noter aussi : `json_dumps()` était appelé **sans** `JSON_ENSURE_ASCII` ici — ce fichier
était donc en **forme 2** du tripwire (UTF-8 brut), pas en forme 1.

### ⚠️ `genDocJson()` avait un appelant, et la fiche n'en annonçait aucun

`tests/core/WebIO_test.cpp:64` (`docParamNames()`) consomme le `json_t*` de `genDocJson()` pour en
extraire les **noms** des paramètres documentés des sept types Web. C'est le **seul** appelant du
dépôt hors `IOFactory.cpp`, et le périmètre de fichiers d'E4.1k ne le mentionne pas : la bascule de
signature **ne compile pas** sans lui. Adaptation mécanique faite dans un commit séparé, **zéro
assertion touchée**. ⇒ **Pour les fiches suivantes de la série** : le tableau « Fichier / Appels »
compte les *sites d'appel jansson*, pas les *appelants de la signature qui change*. Les deux
ensembles sont différents, et le second est celui qui casse le build.

### `IODoc.h` était le fournisseur transitif de `<jansson.h>` pour 139 objets — le retrait est sûr

`IOBase.h` inclut `IODoc.h`, qui incluait `<jansson.h>` (posé par E4.1a) : **139** objets de
`calaos_server` recevaient jansson par cette seule chaîne. Vérifié **avant** de la couper, sur les
fichiers `.deps/*.Po` du build : les **24** fichiers de `src/` qui utilisent des symboles jansson
**sans** les inclure eux-mêmes reçoivent **tous** l'en-tête **aussi** par `IO/ExternProc.h`,
`JsonApi.h` ou `Jansson_Addition.h`. `IODoc.h` n'était le fournisseur **unique** d'aucun d'eux.

#### ⛔ CORRECTION — l'avertissement que ce ticket avait posé pour E4.1c était FAUX

> La première version de cette entrée disait : « `IO/ExternProc.h` est le fournisseur transitif de
> jansson pour ~20 fichiers, son `#include <jansson.h>` n'est pas mort au sens du build, le retirer
> casse la compilation de tout ce monde ». **Infirmé par la revue, mesure à l'appui.** L'erreur
> aurait fait renoncer E4.1c à un nettoyage sûr : on la garde visible plutôt que de la réécrire.

Le fait exact : `ExternProc.h:26` `#include <jansson.h>` est **totalement redondant**, parce que la
ligne **27** juste en dessous, `#include "Jansson_Addition.h"`, mène à
`src/lib/Jansson_Addition.h:24` — qui inclut `<jansson.h>`. Vérifié par la revue : ligne 26
supprimée, `make -C src -j12 -k` → **build OK, zéro `error:`**. ⇒ **E4.1c tel qu'écrit ne casse
rien.**

Trois autres chiffres de la version fausse, corrigés : le compte réel est **9 fichiers / 10
objets**, pas « ~20 » ; **5 des fichiers nommés ne dépendent pas d'`ExternProc.h`** pour jansson
(`MqttCtrl.cpp`, `ReolinkCtrl.cpp`, `IO/Scenario.cpp`, `EventManager.cpp`, `ScriptExec.cpp` — ils
l'obtiennent par une autre chaîne) ; et la vraie condition de casse est le retrait de
**`Jansson_Addition.h` (ligne 27), donc [E4.1x](E4.1x.md)**, jamais celui de la ligne 26.

⚠️ **Réserve du relecteur sur sa propre mesure, à reporter telle quelle** : elle a été faite sur un
`./configure` **nu**. `OWCtrl.cpp` et `OWExternProc_main.cpp`, que l'analyse du graphe d'includes
signale comme dépendants, **n'ont donc pas été compilés**. La conclusion vaut pour le **build par
défaut**, pas pour `--with-owfs`.

### La fuite `IODoc.cpp:163` a disparu d'elle-même

`json_object_set` au lieu de `_new` sur `list_value` (consignée par la revue d'E4.1a) : le
comptage de références n'existe plus avec `nlohmann`, la fuite disparaît sans correction et **sans
changement de comportement observable**. Rien d'autre n'a été fait.

### ⚠️⚠️ Le piège `_DEPENDENCIES`, variante **faux ROUGE UNIFORME** — nouvelle, et vicieuse

La première campagne de contre-mutation a rendu **7 mutations sur 7 « RED »**… **toutes avec
exactement le même cas en échec**, celui de la **première** mutation. Diagnostic : les objets
serveur (`$(CALAOS_SERVER_BUILDDIR)/…/IODoc.$(OBJEXT)`) sont **délibérément retirés** des
prérequis par `core_<X>_test_DEPENDENCIES = …libcalaos_common.la`. Donc
`make -C src && make -C tests core/<X>_test` **recompile bien le `.o` muté** mais **ne relie pas**
le binaire de test : on exécute le binaire de la mutation **précédente**.

⇒ Un ROUGE obtenu ainsi **ne prouve rien** : il peut être le rouge d'une autre mutation. Le
symptôme qui trahit, et le seul, c'est que **plusieurs mutations rendent le même cas en échec**.
La correction : **effacer le binaire de test ET les `.o` mutés** avant chaque reconstruction
(`rm -f tests/core/<X>_test …/IODoc.o …/IOFactory.o`). C'est la variante « faux ROUGE » du piège
déjà connu — la version connue produisait un rouge *illégitime*, celle-ci produit un rouge
*légitime mais qui atteste la mauvaise mutation*, ce qui est pire : elle est **verte à la
lecture**.

### ⭐ La suite était AVEUGLE à `ensure_ascii` — et un cas synthétique le rend testable

Trouvé par la revue, comblé en suites. **Mutation-sonde** `dump(4, ' ', true, …)` → `false` :
**VERTE**, toute la suite passe. Les 15 cas d'origine comparent des **documents parsés**, et
`é` et l'octet UTF-8 brut se parsent en la **même** chaîne — exactement l'asymétrie que
l'épique documente. ⇒ **l'invariant `ensure_ascii = true` était appliqué sans le moindre oracle.**

L'obstacle est réel : **aucun IO de l'arbre ne porte de non-ASCII dans sa documentation**, donc une
assertion d'octets sur l'artefact réel est un **oracle mort**. La parade tient en une observation :
**`IOFactory::RegisterClass()` est publique** et la fabrique publie le nom de type **d'origine**
comme clé de premier niveau. Un type **synthétique** dont le **nom** est non-ASCII, délégant à
`CreateIO("inputtimer")` pour son document, met du non-ASCII dans le fichier **et nulle part
ailleurs** — sans nouvelle classe d'IO, sans nouveau fichier, sans toucher `src/`.

`TheJsonFileStaysPureAsciiWhenATypeNameIsNot` : le type est bien documenté (l'assertion n'est pas
vide), **aucun octet ≥ 0x80 dans tout le fichier**, et la forme d'échappement est celle de
`nlohmann` — `é` **minuscule**, pas le `é` **majuscule** de jansson. Mesuré **rouge** dès
`ensure_ascii = false`.

⚠️ **Le registre d'`IOFactory` est un singleton de processus et l'enregistrement est définitif.**
Il est purement **additif**, la **première** inscription gagne (donc le cas est idempotent sous
`--gtest_shuffle`), et tous les autres cas du fichier n'assertent que sur des clés qu'ils nomment
eux-mêmes.

### `calaos_installer` est indifférent au changement d'ordre du premier niveau

Établi au source par la revue : `src/IODoc.cpp:9-44` parse en `QJsonObject` — **déjà trié par
Qt** — réindexe en minuscules et ne fait que des **lookups par clé** ; les deux consommateurs
(`FormActionStd.cpp:116`, `WidgetIOProperties.cpp:56`) lisent les **tableaux**, dont l'ordre est
**inchangé sur les 70 types**. Seul effet visible : un gros **diff de permutation** à la prochaine
régénération de `data/doc/{en,fr}/io_doc.json`.

---

## E4.1e — wire KNX : deux défauts préexistants de `value_char`, non corrigés

Trouvés en caractérisant `IO/KNX` avant la bascule jansson → `nlohmann::json`. **Aucun des deux
n'est corrigé** : les corriger change ce qu'une valeur KNX veut dire sur le wire, ce qu'une
migration de bibliothèque n'a pas le droit de faire. Les deux sont épinglés par
`tests/KNXCtrlWire_test.cpp` et `tests/KNXExternProcWire_test.cpp`.

**La cause commune.** `KNXValue::toJson()` sérialise `value_char` avec
`Utils::to_string(unsigned char)`, qui est un `std::ostringstream` : il écrit le **caractère**, pas
le nombre. `value_char = 65` donne `"A"`, mais `value_char = 0` donne une chaîne d'**un octet NUL**
et `value_char = 200` donne l'octet **0xC8 seul**, qui n'est pas de l'UTF-8 valide.

### 1. `value_char = 0` — corruption silencieuse sans conséquence

C'est le **chemin commun** : toute `KNXValue` par défaut et toute valeur `KNXString` porte
`value_char = 0`. `json_string()` tronque à son NUL et émet `""` ; `nlohmann` conserve l'octet et
émet `"\u0000"`. **Le pair décode 0 dans les deux cas** (`Utils::from_string` sur un `unsigned char`
est l'extracteur de **caractère** : sur `""` la sentinelle du flux échoue et la valeur reste à son
défaut 0, sur le NUL il lit 0). ⇒ octets différents, sémantique identique.

### 2. `value_char > 0x7F` — la valeur est perdue, des deux côtés

Réel pour les caractères **EIS 13 / EIS 16** au-dessus de 0x7F (un accentué latin-1, par exemple).

- **jansson** : `json_string()` répond `NULL`, `json_object_set_new()` répond `-1`, **aucun des deux
  codes de retour n'est testé** — ni dans `IO/KNX`, ni dans `jansson_from_params()`. La clé
  `value_char` **disparaît silencieusement** du message et le pair décode 0.
- **nlohmann** : l'octet reste dans l'arbre et un `dump()` nu **lève `type_error.316`** — c'est-à-dire
  `std::terminate` sur une connexion vivante. Avec `error_handler_t::replace`, imposé par l'épique
  et appliqué ici, il devient U+FFFD et le pair décode **0xEF** (premier octet de U+FFFD).

**Ni l'un ni l'autre ne préserve 200.** Le défaut existe depuis toujours ; la bascule ne fait que
remplacer une perte par une autre. Un ticket dédié devrait sérialiser `value_char` en **nombre**
(`Utils::to_string((int)value_char)`) — mais c'est un changement **structurel** du wire, à faire
avec caractérisation d'abord, comme le bug d'écriture multiple Wago (Q2 d'E4.1).

## E4.1e — deux corrections de cartographie

1. ⚠️ **Il n'existe aucune option `--with-knx`.** `E4.1e.md` et le brief l'annonçaient. En réalité
   `configure.ac:134-136` fait `AC_CHECK_HEADERS([eibclient.h])` → `AM_CONDITIONAL([HAVE_LIBKNX])` :
   le support est **détecté par présence d'en-tête**. La conséquence pratique est la même — sur une
   machine sans `eibclient.h`, `calaos_knx` n'est pas construit et **deux des cinq fichiers du
   ticket ne sont pas compilés du tout** — mais le contrôle à faire n'est pas un drapeau de
   configure : c'est la ligne `Eib/KNX support (eibd or knxd).......: yes` du résumé de configure,
   ou `CXXLD calaos_knx` dans le log de make.
2. ⚠️ **`KNXValue::toJson()` est défini 2 fois, pas 3.** `E4.1.md` et `E4.1e.md` disent « déclaré 2
   fois et défini 3 fois ». Les définitions sont `KNXCtrl.cpp:76` et `KNXExternProc_cli.cpp:493` ;
   le troisième site cité, `KNXExternProc_main.cpp:267`, est un **appel**. La contrainte réelle
   (tout bouge dans le même commit) est inchangée, seul le décompte l'est.

## E4.1e — variante « faux VERT » du piège `_DEPENDENCIES`, mesurée

La première campagne de contre-mutation a rendu **0 rouge sur une mutation par échange évidente**.
Cause : `KNXCtrlWire_test_DEPENDENCIES` (comme tous les tests qui réutilisent des `.o` du serveur)
ne liste **que** `libcalaos_common.la`, donc `make` n'a **aucune raison de relier** le binaire de
test quand un `.o` du serveur change. Recompiler le `.o` ne suffit pas : le binaire reste l'ancien.

C'est la **jumelle** de la variante « faux ROUGE uniforme » rencontrée par E4.1k. Les deux ont la
même racine et le même remède :

1. effacer **le `.o` du serveur ET le binaire de test** ;
2. **lire les lignes `CXX` et `CXXLD`** dans la sortie de make — si le `CXXLD` du binaire de test
   n'apparaît pas, la mutation n'a pas été exercée, quel que soit le résultat affiché ;
3. exiger que **des mutations différentes donnent des jeux de rouges différents** ;
4. faire un **contrôle sans mutation** (attendu : 0 rouge) avant de faire confiance au harnais.

## E4.1e — le wire KNX transporte des octets bruts du bus : `dump()` nu = `std::terminate`

⭐ **La surface est bien plus large que « des chaînes EIS 15/16 », et c'est ce qui décide de la
priorité : le déclencheur est du matériel domestique ordinaire, pas un équipement exotique.**

Deux chemins, tous deux dans `KNXValue::setValue()` (`IO/KNX/KNXExternProc_cli.cpp`) :

1. ⛔ **`case 6 / 13 / 14` (valeurs 8 bits) — le chemin le plus banal qui soit.**
   `value_int = data.at(1) & 0xFF` puis `value_char = value_float = value_int`, donc **tout octet
   ≥ 0x80** produit un `value_char` que `Utils::to_string(unsigned char)` rend comme **un octet
   isolé, invalide en UTF-8**. **EIS 6 est le scaling 0-255** : un **gradateur à 78 % vaut 200**.
   Il ne faut donc **aucun appareil particulier** — une installation domestique ordinaire suffit à
   atteindre le `dump()`.
2. `case 15 / 16` (chaînes) : `value_string` reçoit les **octets bruts de la trame**, sans
   validation ni transcodage. Un appareil qui envoie du texte **latin-1** — le cas normal sur KNX —
   y met de l'UTF-8 invalide.

Les deux valeurs sont sérialisées par `calaos_knx` dans sa boucle de monitoring et re-sérialisées
par `calaos_server` à l'écriture.

**La chaîne est nue de bout en bout** : `EIBGetGroup_Src()` → `setValue()` → `toJson()` → `dump()`,
et il n'y a **aucun `try`/`catch` dans tout `IO/KNX/`** (vérifié fichier par fichier) —
`monitorWait()` est appelée nue par `readTimeout()`, et `EXTERN_PROC_CLIENT_MAIN` n'en pose pas non
plus.

Mesuré sur les octets `C9 74 E9` :

| Forme | Résultat |
|---|---|
| jansson `JSON_ENSURE_ASCII` (avant E4.1e) | `json_string()` répond `NULL`, **la clé `value_string` disparaît entièrement du message**, en silence — le serveur reçoit un event sans chaîne |
| `nlohmann` `dump()` **nu** | **lève `type_error.316`** depuis `KNXProcess::monitorWait()`, **sans gestionnaire au-dessus** ⇒ `std::terminate` de `calaos_knx` sur une installation vivante |
| `nlohmann` `dump(-1, ' ', true, error_handler_t::replace)` (E4.1e) | U+FFFD par octet fautif, ASCII pur, document parsable, perte de données mais **pas de plantage** |

⇒ **l'invariant 3 de l'épique (gestionnaire d'erreur) n'est pas une précaution théorique sur ce
wire** : sans lui, la bascule aurait transformé une amputation silencieuse en **crash du processus
KNX déclenchable par un simple appareil du bus**. Épinglé par
`RawNonUtf8BusBytesAreReplacedInsteadOfCrashing_DECLARED_DELTA` dans les deux binaires ; prouvé par
mutation (retrait du gestionnaire ⇒ 2 rouges par binaire ; `ensure_ascii = false` ⇒ 4 rouges par
binaire).

⚠️ **La perte de données, elle, n'est pas réparée** : U+FFFD n'est pas plus le caractère d'origine
que l'absence de clé. La vraie correction est de **transcoder** (KNX EIS 15 est de l'ASCII 7 bits,
EIS 16 du latin-1) ou de sérialiser ces champs en base64. Ticket dédié, avec caractérisation
d'abord — hors périmètre d'une migration de bibliothèque.

## E4.1e — ⭐ un test-miroir reste VERT quand le produit casse : mesuré, et le remède est à 10 lignes

**La leçon la plus transférable de ce sous-ticket, et elle vaut pour tous les wires de la série.**

Quand l'émetteur est inatteignable depuis un test — ce qui est le cas de **tous** les drivers
`ExternProc` (`sendMessage()` non virtuelle, contrôleurs en singleton à constructeur privé qui
lancent un sous-processus, boucles bloquées sur une socket matérielle) — la tentation est d'écrire
un test qui **reproduit** l'assemblage du message avec les mêmes primitives, et de geler ses octets.
C'est ce qu'E4.1e a fait d'abord, **fidèlement, ligne à ligne**.

⛔ **Ça ne protège rien.** Un miroir fige ce que **le test** fait, pas ce que **le produit** fait.
Mesuré par la revue : en remettant les **4** `dump()` de production de `IO/KNX` en `.dump()` nu —
c'est-à-dire en réintroduisant exactement le `type_error.316` / `std::terminate` documenté
ci-dessus — la suite est restée **34/34 VERTE**. Le ticket documentait une régression critique et ne
s'en protégeait pas.

✅ **Le remède est bon marché** : extraire l'assemblage d'enveloppe en **fonctions libres** appelées
par **la production ET le test** (`knxWriteMessage`, `knxReadMessage`, `knxEventMessage`,
`knxDisconnectedMessage` — ~10 lignes déplacées, aucun changement de comportement). La même mutation
donne ensuite **2 rouges par binaire**.

**Deux fausses pistes, écartées après mesure** : la capture du log (`LogStream` écrit sur
`std::cout`) ne couvrait que **2 sites sur 4** — `monitorWait()` ne loggue pas son `res` — et rendre
`sendMessage()` virtuelle ne suffisait pas non plus, le constructeur du contrôleur étant privé
derrière un singleton qui lance deux sous-processus.

➡️ **À appliquer aux sous-tickets de wire restants** (`E4.1f` OLA, `E4.1g` MQTT, `E4.1h` Wago,
`E4.1i` Reolink, `E4.1j` Lua) : si le test construit lui-même le message qu'il gèle, **il ne teste
pas l'émetteur** — extraire d'abord.

## E4.1e — le piège `_DEPENDENCIES` reste ARMÉ pour le prochain

Les deux tests neufs d'E4.1e reconduisent le motif du dépôt :
`<test>_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la` **seul**, alors que le binaire
lie des `.o` du serveur (`IO/KNX/KNXCtrl.o`, `IO/ExternProc.o`, `IO/KNX/KNXExternProc_cli.o`).
`make` n'a donc **aucune raison de relier** ces binaires quand un `.o` du serveur change. Ce n'est
pas un défaut introduit ici — c'est le motif de **tous** les tests qui réutilisent des objets du
serveur — mais il faut le dire : **le piège n'est pas désamorcé, il attend le suivant.**

Ses **deux** faces, toutes deux rencontrées dans la série :
- **faux VERT** (E4.1e) : la mutation n'est jamais exercée, tout reste vert, on conclut que le test
  ne mord pas — ou pire, on conclut qu'il mord alors qu'on n'a rien mesuré ;
- **faux ROUGE** (E4.1k) : le binaire est périmé et échoue sur du code qui n'existe plus.

**Protocole à appliquer sans exception** : (1) effacer **le `.o` ET le binaire de test** ;
(2) **lire les lignes `CXX` et `CXXLD`** dans la sortie de make — pas de `CXXLD`, pas de mesure ;
(3) exiger que **des mutations différentes donnent des jeux de rouges différents** ; (4) faire un
**contrôle sans mutation** (attendu : 0 rouge) avant de faire confiance au harnais.
---

## E4.1g — ce que la bascule du wire MQTT a mesuré (hors périmètre, non corrigé sauf mention)

- **🔴 `MqttCtrl::publishTopic()` faisait un `json_decref` de trop — disparu avec la bascule.**
  `jansson_to_string()` (`Jansson_Addition.h:150-165`) **vole la référence** : il appelle
  `json_decref(jroot)` dans **les deux** branches. `MqttCtrl.cpp:115-116` faisait
  `process->sendMessage(jansson_to_string(jroot)); json_decref(jroot);` — soit un décrément sur un
  objet **déjà libéré**, à **chaque publication MQTT**. Le site jumeau de la même fonction (`:41`,
  la configuration du broker) était correct, et il part avec `jansson_to_string`.

  ⚠️ **CORRECTION — ma première rédaction affirmait « les 38 autres appels relus, aucun ne
  double-décrémente ». C'ÉTAIT FAUX, sur les deux moitiés de la phrase**, et la revue l'a
  attrapé. Recompté hors du hook `rtk` (blancs de commentaires posés en préservant les numéros de
  ligne) : **27 sites d'appel** dans `src/` après ce ticket — pas 38 — plus **1 définition** dans
  `Jansson_Addition.h`, et **16** d'entre eux passent une variable nue. Et **il en reste un qui
  double-décrémente** :

  > 🔴 **`src/bin/calaos_server/IO/Reolink/ReolinkCtrl.cpp:151-153`** —
  > `string message = jansson_to_string(jroot); process->sendMessage(message); json_decref(jroot);`
  > **exactement le même défaut**, à chaque envoi. **Non corrigé ici : hors périmètre, et
  > l'agent d'E4.1i travaille dessus.**

  **La leçon vaut plus que le bug** : une affirmation de relecture fausse dans un `FINDINGS.md`
  est **pire qu'une absence** — elle fait renoncer le suivant à chercher. Une phrase de la forme
  « j'ai tout relu, il n'y a rien » ne devrait être écrite **que** si elle est adossée à un
  comptage reproductible, montré.

- **⭐ L'octet nul ne traversait PAS l'aller-retour, alors que tout le code amont existait pour ça.**
  `payloadToJsonString()` prenait grand soin d'utiliser `json_stringn(data, len)` pour ne pas
  tronquer un payload binaire au premier octet nul, et `json_dumps()` l'écrivait correctement
  échappé. Mais **`json_loads()` refuse cet échappement sans `JSON_ALLOW_NUL`** — mesuré :
  *« … is not allowed without JSON_ALLOW_NUL »* — et le drapeau n'était passé nulle part. Le
  consommateur (`MqttCtrl.cpp:52-61`) jetait donc **tout le message**, topic compris, avec un
  simple « Error parsing json ». Corrigé mécaniquement par la bascule (`nlohmann` accepte
  l'échappement) ; **changement utilisateur déclaré**, entrée `RELEASE_NOTES`.

- **`IO/Mqtt/MqttExternProc_main.h` était listé dans `calaos_mqtt_SOURCES`
  (`src/bin/calaos_server/Makefile.am:431`) et n'existe pas.** Automake le tolérait et le build
  passait ; c'était une entrée morte qui aurait faussé `make dist`. **Remplacée** par
  `IO/Mqtt/MqttWire.h`, qui existe — donc corrigée au passage, dans le périmètre.
  ⚠️ `IO/KNX/KNXExternProc_main.h`, lui, **existe bien** : le défaut n'était pas systématique.

- **La suite entière est aveugle à `ensure_ascii`, sauf là où on lui donne un oracle d'octets.**
  Confirmé sur ce périmètre : avant l'ajout des cas `MqttWireForm.*`, muter `ensure_ascii` de
  `true` à `false` ne rougissait **rien**. Un oracle sémantique (document parsé) ne peut pas voir
  un changement d'échappement — c'est le contrat d'E4.0a. **Tout sous-ticket d'E4.1 qui pose un
  `dump()` sans au moins une assertion sur les octets applique l'invariant sans aucun témoin.**

- **⚠️ Piège `_DEPENDENCIES`, variante FAUX VERT — nouvelle, et plus dangereuse que la variante
  faux ROUGE.** La première campagne de contre-mutations a rendu **0 rouge partout, contrôle
  compris**. Cause : **`check_PROGRAMS` n'est pas construit par un `make` nu**. Le harnais effaçait
  le binaire de test, `make -j12` ne le reconstruisait pas, `./tests/MqttWire_test` n'existait
  plus, la sortie était vide — et « aucun cas rouge » ressemblait à un succès. La consigne connue
  (« des rouges identiques d'une mutation à l'autre sont la signature du piège ») **ne l'attrape
  pas** : ici il n'y avait pas de rouge du tout. Ce qui l'attrape est **l'absence de la ligne
  `CXXLD <test>`** dans la preuve de compilation, et un `[ -x <binaire> ]` explicite. À porter
  dans le brief des sous-tickets suivants.

- **Quatre trous du filet trouvés par la revue, tous par des mutations que je n'avais pas faites.**
  Trois sont **fermés** par les suites : (1) `error_handler_t::replace` **n'avait aucun témoin** —
  le muter en `ignore` laissait 32/32 vert, alors que c'est le **troisième invariant d'émission**
  d'E4.1 et que le chemin est **portant** (`MqttCtrl::publishTopic()` envoie un payload **jamais
  assaini** au `dump()`, et sans le gestionnaire un `type_error.316` **tuerait `calaos_server`**) ;
  (2) la garde `Exists("user") && Exists("password")` n'était épinglée qu'**à moitié**, le cas
  **password seul** manquait ; (3) **aucun payload de 4 octets** n'était exercé, donc les deux
  bornes serrées du validateur (`0xF0` exige une continuation ≥ `0x90`, `0xF4` une ≤ `0x8F`)
  pouvaient être élargies sans un seul rouge. Le quatrième reste **ouvert et consigné** :
  ⚠️ **les 5 sites d'appel hors `MqttWire.h` ne sont couverts par rien** — échanger les arguments
  de `MqttCtrl::publishTopic()` ou de la lambda `messageRcv` laisse la suite **entièrement
  verte**. Le code *partagé* est tenu, son *câblage* ne l'est pas, et rien dans le dépôt ne peut
  le tenir tant qu'aucun test ne lie les objets serveur.

- **⚠️ Deux nouvelles façons de rendre une campagne de mutations mensongère, rencontrées ici.**
  (1) **Éditer le harnais pendant qu'il tourne.** `bash` lit son script **au fil de l'exécution** :
  réécrire le fichier décale l'interpréteur, qui s'est mis à exécuter une ligne de C++ comme une
  commande shell. (2) **Une campagne tuée en cours laisse le fichier muté dans le worktree** — la
  campagne suivante, qui semait son « original » depuis le worktree, a donc pris la version mutée
  pour référence et sorti **3 rouges au contrôle**. Les deux règles qui en découlent :
  **ne jamais modifier un harnais en cours d'exécution**, et **semer la copie de référence depuis
  une source en lecture seule** (montage du harnais), jamais depuis l'arbre de travail.
  ⭐ Corollaire utile : **le contrôle sans mutation attrape les deux** — il est sorti rouge dans
  les deux cas. C'est le seul garde-fou qui ait fonctionné, et il a aussi attrapé une **assertion
  fausse de ma main** (`ASSERT_EQ(15u, …)` sur un topic de **13** octets).

- **⭐ Un `dump()` levant peut se lire « 0 rouge » — troisième variante du faux vert.**
  Muter `error_handler_t::replace` en `strict` fait **lever** `dump()`. Si gtest n'attrapait pas
  l'exception, le binaire **avorterait**, aucune ligne `FAILED` ne serait émise, et un harnais qui
  compte les `FAILED` lirait **0 rouge** — un **faux vert** produit par un test qui **meurt**.
  Mesuré ici : gtest attrape bien, la mutation rend **3 rouges**. Mais tout harnais de mutation de
  la série doit vérifier **le code de sortie du binaire de test**, pas seulement compter les
  `FAILED`, sinon un cas qui tue le processus passe pour un cas qui passe.

- **⚠️ Le hook `rtk` peut mentir, et il a menti sur ce ticket.** `grep`/`awk` passés par le hook ont
  rendu **0 correspondance** sur des motifs qui en avaient (`grep -n "u0000" fichier` sur un fichier
  qui contenait la chaîne, `awk '/Mqtt/'` sur un log qui en était plein), et une redirection de
  `docker logs` vers un fichier a produit **9 lignes** au lieu du log complet. Aucun de ces échecs
  ne remonte d'erreur : ils **ressemblent à un résultat négatif légitime**. Toutes les affirmations
  chiffrées de `E4.1g.md` ont donc été **re-mesurées via `python3`/`subprocess`/`hashlib`**, en
  lisant les blobs git et le log brut du conteneur. **À faire systématiquement dans la série** :
  un `grep -c` qui rend 0 n'est une preuve d'absence que s'il a été exécuté hors du hook.

- **Le `path` MQTT n'est pas du JSONPath, et sa syntaxe reste un contrat utilisateur.**
  `MqttCtrl.cpp` : découpage sur `/`, index seulement si le jeton **commence** par `[` (forme
  réelle `weather/[0]/description`). Non modernisé, délibérément. À noter tout de même : un chemin
  à segment vide (`a//b`) produit un jeton vide, dont `val[0]` lit le terminateur — défini par le
  standard, sans conséquence, l'accès `at("")` échouant ensuite proprement. Non corrigé.

## E4.1c — les trois résidus jansson (2026-08-25)

### ⛔ La « CORRECTION » posée par la revue d'E4.1k est CONFIRMÉE — et ⭐ la liste d'E4.1x est **6**

Le retrait d'`IO/ExternProc.h:26` `#include <jansson.h>` **seul** fait perdre `<jansson.h>` à
**0** unité ; le retrait de `src/lib/Jansson_Addition.h:24` **seul**, à **0** aussi.

⚠️ **AUTOCORRECTION — j'ai d'abord écrit « 10 » ici pour la liste d'E4.1x. C'était faux, et pour
deux raisons cumulées.** Les trois chiffres, et pourquoi seul le dernier est la liste à servir :

| Chiffre | D'où il vient | Verdict |
|---|---|---|
| **10** | statique brut, mutation **infidèle** : `<jansson.h>` retiré des **deux** fichiers fournisseurs | ⛔ **artefact de la simulation.** `Jansson_Addition.h` **utilise `json_t` dans son propre corps** : lui retirer son include le casse lui-même — mesuré, **4158 `error:` et 128 objets en échec**. La mutation sciait *toutes* les branches, pas celle d'`ExternProc.h`. Ce n'est pas ce que fait E4.1x, qui **supprime le fichier** une fois ses 7 fonctions sans appelant |
| **8** | les 10 moins la **prose** | `IO/KNX/KNXCtrl.cpp:310` et `IO/KNX/KNXExternProc_main.cpp:33` ne portent `json_loads` **que dans un commentaire** (`//E4.1e: json_loads() answered NULL…`), idem les 2 `jansson_*` de `KNXCtrl`. **0 appel en code.** Sur 17 porteurs bruts de `src/`, **15 hors prose** |
| ⭐ **6** | mutation **fidèle** : `ExternProc.h` cesse de **déléguer**, `Jansson_Addition.h` **intact** | **la liste réelle**, obtenue **deux fois indépendamment** — graphe `python3` et `make -C src -j12 -k` → **6 objets en échec, exactement les mêmes 6 fichiers** |

**Les 6 que `E4.1x` devra servir** : `IO/OLA/OLACtrl.cpp`, `IO/OLA/OLAExternProc_main.cpp`,
`IO/Wago/WagoMap.cpp`, `IO/Wago/WagoExternProc_main.cpp`, `LuaScript/ScriptBindings.cpp`,
`LuaScript/ScriptExtern_main.cpp`. **Aucune KNX.**

**L'écart 8 → 6, expliqué — c'est le point à retenir** : `EventManager.cpp` et `IO/Scenario.cpp`
**n'ont jamais dépendu d'`ExternProc.h`** pour jansson. Ils tiennent `Jansson_Addition.h` d'un
chemin à eux : `EventManager.cpp → EventManager.h → Jansson_Addition.h`, et
`IO/Scenario.cpp → IO/Scenario.h → IOBase.h → EventManager.h → Jansson_Addition.h`. La mutation
infidèle les faisait tomber parce qu'elle coupait **la branche commune**. Ils restent à migrer
(E4.1x / E4.6), mais **pas au titre d'`ExternProc.h`**.

⚠️ **Leçon de méthode, générale à la série** : une simulation de suppression d'en-tête doit être
**fidèle à ce que le ticket cible fera**. Retirer un `#include` d'un en-tête **qui s'en sert
lui-même** ne mesure pas la dépendance de ses consommateurs — ça mesure sa propre autodestruction,
et le chiffre obtenu est **surestimé sans que rien ne le signale**.

**Preuve indépendante du graphe, par contre-mutation d'ÉCHANGE** (`M2` de la campagne d'E4.1c) :
échanger `#include "Jansson_Addition.h"` contre `#include <jansson.h>` dans `ExternProc.h` laisse
`ExternProcHeaderAloneStillProvidesTheJanssonApi` **VERT**. Les deux lignes sont
**interchangeables** ; la ligne 26 n'apportait rien. Retirer **les deux** (`M3`) rougit ce cas.

### La réserve `--with-owfs` est levée — et elle était sans objet au source

Deux mesures, indépendantes :

1. **Elle ne tenait sur AUCUNE machine, pas seulement sur celle-ci.** `configure.ac` n'a pas de
   `--with-owfs` (ni `--with-mqtt`, `--with-knx`, `--with-ola`) : les quatre drivers sont
   **auto-détectés**. Mais surtout, **OWFS n'est protégé par aucun garde** :
   `src/bin/calaos_server/Makefile.am:184` met `IO/OneWire/OWCtrl.cpp` dans
   `calaos_server_SOURCES` et `:390` met `calaos_1wire` dans `bin_PROGRAMS`, **hors de tout
   `if HAVE_OWCAPI`**. `OWCtrl.cpp` et `OWExternProc_main.cpp` sont donc compilés
   **inconditionnellement**, quelle que soit la présence d'`owcapi.h`. ⇒ **la mesure de la revue
   d'E4.1k compilait déjà OWFS**, et sa réserve était sans objet dès le départ.
2. **Et surtout : `OWCtrl.cpp` et `OWExternProc_main.cpp` portent 0 symbole jansson.** Ils sont déjà
   en `nlohmann` (`OWExternProc_main.cpp` porte même un `dump()` — c'est l'un des 5 wires tiers en
   UTF-8 brut de l'exception nommée, périmètre d'un autre ticket). La dépendance que l'analyse du
   graphe leur prêtait est **une dépendance d'en-tête, pas d'usage** : elle ne pouvait rien casser.

### Le chemin transitif `ReolinkCtrl.cpp` renvoyé par le ticket voisin est **inerte**

⚠️ **Précision** : sous cette forme exacte, le chemin **n'existe plus après E4.1c** —
`ReolinkCtrl.h:31` inclut toujours `ExternProc.h`, mais jansson lui arrive maintenant par
`ExternProc.h → Jansson_Addition.h`, ou par `ReolinkCtrl.h:32 IOBase.h → EventManager.h →
Jansson_Addition.h`. Le chemin d'inclusion **subsiste** donc, mais `ReolinkCtrl.cpp` porte **0 symbole jansson** depuis le merge d'**E4.1i**. Il ne
consomme donc rien de ce que le chemin lui livre, et il ne figure dans aucune des trois colonnes du
tableau ci-dessus. Rien à faire ni pour E4.1c ni pour E4.1x de ce côté.

### Le tableau « Fichier / Appels » d'`E4.1.md` est périmé par rapport à master — **8 fichiers, pas 6**

⚠️ **AUTOCORRECTION : j'avais écrit 6.** Recompté **hors prose** sur `24f635e3`, **8** entrées du
tableau sont à **0 appel jansson en code** : `MqttCtrl.cpp` (compté 13), `MqttExternProc_main.cpp`
(15), `ReolinkCtrl.cpp` (9), `IODoc.cpp` (16), `IOFactory.cpp` (4), `KNXExternProc_cli.cpp` (2),
**`KNXCtrl.cpp` (6)** et **`KNXExternProc_main.cpp` (6)** — E4.1e / E4.1g / E4.1i / E4.1k sont
mergés. Les deux KNX manquaient à ma première liste parce que je comptais **prose comprise** : leur
unique jeton `json_loads` est dans un commentaire. `HttpClient.cpp` (2) fait le neuvième, par E4.1c.
**Les fiches non encore livrées de la vague 1 doivent recompter leur propre périmètre, en excluant
les commentaires, avant de s'y fier.**

### Deux numéros de ligne faux, dans deux fiches

`jansson >= 2.5` est à **`configure.ac:51`** (`requirements_calaos_common=…`). La ligne **52** est le
`PKG_CHECK_MODULES`. `E4.1c.md:20` **et** `E4.1x.md` (travail, point 4) citent tous deux `:52`.
**E4.1x doit éditer la 51.**

### ⚠️ Exposition conditionnelle, à léguer à E4.1x : trois extern-procs ne sont pas toujours compilés

`calaos_ola` (`Makefile.am:400 if HAVE_LIBOLA`), `calaos_knx` (`:413 if HAVE_LIBKNX`) et
`calaos_mqtt` (`:428 if HAVE_LIBMOSQUITTO`) **sont conditionnels** — contrairement à OWFS. Une
machine sans `libola` **ne compile jamais `IO/OLA/OLAExternProc_main.cpp`**, qui est **l'un des 6
vrais casseurs** ci-dessus. ⇒ **E4.1x ne peut pas conclure sur un seul environnement** : soit il
construit avec `libola`, `eibclient` et `mosquitto` installés, soit il déclare explicitement que sa
mesure ne couvre pas `OLAExternProc_main.cpp`. *(Non mesuré ici : aucun build sur une machine
dépourvue de ces paquets. `OLACtrl.cpp`, `WagoMap.cpp`, `WagoExternProc_main.cpp`,
`ScriptBindings.cpp` et `ScriptExtern_main.cpp` sont, eux, inconditionnels.)*

### `HttpClient.cpp` ne contenait déjà pas le mot `jansson`

Le critère d'acceptation 1 de la fiche (« `grep -c jansson` → 0 sur chacun des trois ») y était
**vrai avant comme après** : ce qui y a disparu, ce sont les 2 jetons `json_array_size` /
`json_array_get` du **corps** de la macro morte. Sur `ExternProc.h`, le critère n'est vrai qu'en
**sensible à la casse** : `Jansson_Addition.h` (J majuscule) reste, et c'est voulu — c'est la ligne
d'E4.1x.

**La macro était bien morte, vérifié sur l'image** : `/usr/include/jansson.h:243` définit
`json_array_foreach`, jansson **2.14**, au-dessus du plancher `>= 2.5` de `configure.ac:51`. Les
**16** vrais sites d'appel de l'arbre (`JsonApi.cpp` ×6, les deux handlers ×2, Wago ×4, OLA ×1,
`ScriptExtern_main.cpp` ×1) sont **tous dans d'autres unités de traduction** et la tiennent de
`<jansson.h>` ; la macro vivait dans un `.cpp`, elle ne pouvait fuir nulle part.

⭐ **Et le détail qui prouve qu'elle était morte *depuis toujours*, pas « depuis 2.5 »** (trouvé par
la revue, vérifié) : `HttpClient.cpp` voyait déjà `<jansson.h>` **bien avant sa ligne 111** — dès sa
ligne **21** (`RemoteUIProvisioningHandler.h → RemoteUIManager.h → EventManager.h →
Jansson_Addition.h`) et de nouveau en **23** (`HttpClient.h:27 → JsonApiHandlerHttp.h:24 →
JsonApi.h:25 <jansson.h>`). Son `#ifndef` était donc évalué **après** la définition de jansson :
il **n'a jamais pu se déclencher**, quelle que soit la version installée.

### ⚠️ Dette laissée, et son propriétaire est E4.1x

`tests/JanssonResidues_test.cpp` (6 cas) appelle l'**API C jansson exprès** — c'est le seul moyen de
prouver au runtime qu'`ExternProc.h` fournit encore l'en-tête — et l'un de ses tripwires épingle la
ligne `#include "Jansson_Addition.h"` **qu'E4.1x supprime**. Ce fichier **remontera** dans le
`grep -rn 'json_t\|jansson' src tests` du critère d'acceptation 1 d'E4.1x. **La bonne réponse là-bas
est de le supprimer entièrement**, pas de le porter : il n'a plus d'objet une fois jansson parti.
Marqué en tête du fichier **et** dans le commentaire de son bloc `tests/Makefile.am`.

### Ce que le filet d'E4.1c ne couvre pas — dit franchement

Quatre de ses six cas lisent le **texte des sources livrées** (via `-DCALAOS_TOP_SRCDIR`, sur le
modèle de `CALAOS_GOLDEN_DIR`) : ce sont des **garde-fous de suppression**, pas des oracles de
comportement. Ils rougissent si on remet un include ou la macro — mesuré, 3 mutations, 3 rouges
distincts — mais **ne prouvent rien sur l'exécution**. Le seul oracle d'exécution du ticket ne peut
rougir que si les **deux** fournisseurs d'`ExternProc.h` disparaissent. **La vraie sécurité de ce
ticket est le compilateur** : c'est le build `distclean` avec les quatre drivers actifs qui la donne,
pas la suite.

Et il n'y a **aucun oracle d'octets ici, par mesure et non par supposition** : `ExternProc.h`,
`WebCtrl.cpp` et `HttpClient.cpp` portent **0 `dump()`**, **0 émission `nlohmann`**, **0 appel
jansson**. Aucun non-ASCII ne peut atteindre un `dump()` de ce périmètre parce qu'il n'y en a aucun.
C'est bien le seul sous-ticket de la série dans ce cas, comme `E4.1.md` l'annonçait.

### ⭐ Une garde de sonde à un seul symbole ne rougit pas : elle CASSE LE BUILD

Trouvé par la revue d'E4.1c, corrigé, et **mesuré dans les deux sens**. Le cas
`JanssonProvidesArrayForeachNativelyAtTheConfiguredFloor` portait une sonde
`#if defined(json_array_foreach)` et un `ASSERT_TRUE` sur son résultat — mais **le corps** du cas,
qui *appelle* `json_array_foreach()`, était gardé par un **autre** symbole
(`#if defined(JANSSON_VERSION_HEX)`). Conséquence : un jansson qui livre l'en-tête **sans** la macro
ne rendait pas le cas rouge, il rendait le **build** rouge, et le message de l'`ASSERT_TRUE` ne
pouvait **jamais** s'afficher.

J'avais écrit qu'un tel jansson n'était pas constructible sans changer l'image. **Faux** : un
en-tête écran `jansson.h` qui fait `#include_next <jansson.h>` puis `#undef json_array_foreach`,
prépendu par `CPPFLAGS=-I…`, le construit en trois lignes et sans toucher au paquet. Reproduit :

| Version | Build | Résultat |
|---|---|---|
| avant (`677ef37d`) | **RC=2**, 1 `error:` à `JanssonResidues_test.cpp:210`, **0 `CXXLD`** | aucun binaire |
| après (`&& defined(json_array_foreach)`) | **RC=0**, `CXXLD` ×1, 0 `error:` | **1 rouge**, le cas visé, message affiché |

⚠️ **Règle générale pour la série** : quand une sonde de préprocesseur `#if defined(X)` alimente une
assertion, **le corps qui utilise X doit être gardé par X lui-même**, jamais par un symbole voisin
« qui va avec ». Sinon la dégradation qu'on prétend détecter se manifeste en **erreur de
compilation** — non silencieuse, donc non bloquante, mais l'oracle n'a **jamais** l'occasion de
parler. Le même piège existe partout où un `#if` de disponibilité et un `#if` d'usage divergent.


---

## Ouverture des findings de la nuit en tickets (2026-08-25)

Dix fiches ouvertes, **T3.25 → T3.34**. Chaque finding d'origine renvoie désormais à la sienne,
et l'analyse est conservée en place. Ce qui suit est ce que la **vérification au source** a
ajouté ou infirmé, et qui n'existait dans aucun finding.

### ⛔ Trois findings de la nuit se sont révélés INEXACTS au source

1. **« `from_string("")` retourne true avec dest zéro-initialisée »** (:615-617) — **non** :
   `dest` **n'est pas écrite du tout**, et le régime « écrit 0 » ne concerne que les entrées
   **illisibles NON blanches**. Le site a aussi changé (`StringUtils.h:104-111`, plus `Utils.h`).
   ⇒ [T3.25](T3.25.md).
2. **« `RoonPlayer` : `from_string("")` laisse `port` à 0 »** (:2458) — **non** : `port` est
   **indéterminé** (`RoonPlayer.h:214`, `int port;` sans initialiseur). Et **« Roon inutilisable »**
   est trop fort : le mode **autodétection** (host vide) fonctionne, seul l'hôte statique est
   cassé. ⇒ [T3.28](T3.28.md). ✅ **Corrigé et livré** (`fix/t3.28`, 2026-08-25) ; le
   partage exact autodétection / hôte statique est **confirmé au source du sidecar Python**, et
   `RELEASE_NOTES.md` le dit à l'utilisateur en nommant qui est concerné.
3. ⚠️ **CE POINT ÉTAIT LUI-MÊME FAUX, et il est corrigé ici (2026-08-25).** J'avais écrit que
   `LmsHost{}`, `LightState` et `RedChannel` « n'existent nulle part dans l'arbre ». **Le
   balayage était juste, la conclusion fausse : il portait sur `master`.** Ces types existent sur
   **`refactor/e4.1d`** (`689e26b0` — `Audio/SqueezeboxWire.h:188-191`, `IO/Hue/HueWire.h:107-114`)
   et **`refactor/e4.1f`** (`4e238f2c` — `IO/OLA/OLAWire.h:91-124`), **deux branches livrées et
   revues, non mergées à la date du balayage**. Sur master, le seul précédent reste
   `enum class RuleDetachPolicy` (T3.18). ⇒ **cinq récidives** au total : **3 ouvertes sur
   master**, **2 fermées sur branches en attente**. ⇒ [T3.31](T3.31.md), réécrite.

### ⭐ `IO/OutputShutter.cpp:119` et `OutputShutterSmart.cpp:171` — `impulse down` ne marche pas

`val.compare(0, 13, "impulse down ")` puis **`val.erase(0, 11)`**. Le préfixe fait **13**
caractères, on en retire **11** : il reste `"n "` collé devant la valeur.
`from_string("n 500", v)` échoue (entrée illisible non blanche) ⇒ **`v = 0`**, et
`ImpulseDown(0)` est appelé **quelle que soit la durée demandée**.
La branche `impulse up ` juste au-dessus (`:110-116`) fait `compare(0, 11, …)` + `erase(0, 11)` —
**correct**. La faute vient de ce que la longueur est écrite **deux fois** sans rien qui les lie,
alors que `Utils::strStartsWith()` est utilisé **douze lignes plus bas**, à `:124`.
Trace visible depuis toujours dans l'API : `cmd_state = "impulse down 0"` (`:169`).
⇒ **[T3.34](T3.34.md)**, deux caractères, deux fichiers. **À livrer AVANT T3.25 L1**, qui touche
les mêmes lignes.

### ⛔ `Utils::split` PADE — l'« accès hors bornes » de `KNXExternProc_main.cpp` n'existe pas

Un balayage de la nuit signalait des accès `tokens[1]`/`tokens[2]` **hors bornes** à
`IO/KNX/KNXExternProc_main.cpp:145-147` et `:158-160` sur une adresse à moins de trois
composantes. **Faux, vérifié au source** : `src/lib/StringUtils.cpp:210` —
`while (tokens.size() < (uint)max) tokens.push_back("");` — avec `max = 3`, le vecteur porte
**toujours** trois éléments. **Aucun accès hors bornes, aucun segfault.**
⭐ **Le vrai défaut est plus discret** : le remplissage est `""`, donc `from_string` rend `true`
sans écrire et `b`/`c` restent **indéterminés** ⇒ **une adresse KNX malformée produit une adresse
de groupe arbitraire sur le bus**, sans erreur. Les `& 0x0F` / `& 0xFF` **masquent**, ils ne
valident pas. ⇒ [T3.33](T3.33.md).

### `IO/Wago/WagoExternProc_main.cpp` — le jumeau exact du défaut OLA, non corrigé

12 sites (`:75-81`, `:108-112`, `:131-137`, `:160-166`, `:193-198`, `:217-223`, `:229-230`,
`:266`) déclarent `UWord address; int count; UWord value;` **sans initialiseur** et les
alimentent depuis `jsonData[…]`. C'est **ligne pour ligne** la forme qui a envoyé des canaux DMX
à 21845 depuis `OLAExternProc_main.cpp:75-81`.
📏 **E4.1h a initialisé le côté ÉMETTEUR (`WagoMap.cpp:203-204`) et pas le côté RÉCEPTEUR.**
Vecteur **interne** (tube `ExternProc`, cadrage longueur-préfixée) ⇒ pas atteignable à distance,
mais une adresse modbus arbitraire, ce sont des relais. ⇒ [T3.33](T3.33.md).

### 📏 `IO/OLA/OLAOutputLightRGB.cpp:35` — `channel_red` documenté sur `0..9999`

`paramAddInt("channel_red", …, 0, 9999, true)` alors que `:36` et `:37` déclarent
`channel_green` et `channel_blue` sur `0..512`. Le `9999` est recopié de la ligne `universe`
(`:34`). **Mineur**, un caractère, même famille que T3.29 (une chaîne d'ioDoc qui ment).
Consigné dans [T3.29](T3.29.md) §2.5, à faire ou à laisser explicitement.

### ⭐ `set_state` n'a AUCUN `scopeDenied` — mesuré

`JsonApiHandlerWS.cpp:170-231` : les sept messages protégés sont `set_param`, `del_param`,
`audio_db`, `set_timerange`, `eventlog`, `register_push`, `settings`. **`set_state` (`:197-198`)
n'en a pas.** Côté HTTP (`JsonApiHandlerHttp.cpp:162`) il n'y a aucune couche de portée.
⇒ **tout compte authentifié, portée service comprise, atteint `set_state`** — c'est le prérequis
du chemin atteignable à distance de [T3.25](T3.25.md) §2.

---

## E4.1f — wire OLA (`OLACtrl` ↔ `calaos_ola`)

### F-OLA-1 ⭐ `Utils::from_string("")` **annonce un succès et n'écrit rien** — `calaos_ola` pilotait un canal DMX **non initialisé**

Trouvé par le commit de caractérisation, **corrigé dans un commit séparé**, et c'est le seul défaut
de comportement de ce ticket.

`OLAExternProc_main.cpp` déclarait `unsigned int channel; unsigned int val;` **sans initialiseur**,
puis appelait `Utils::from_string()` sur ce que l'aplatissement `jansson_decode_object` avait mis
dans le `Params`. Deux comportements **différents**, et un seul est un trou :

| Entrée | *Sentry* | Extraction | Destination | Retour |
|---|---|---|---|---|
| `""` (ce que devient un `null`, un objet, un tableau) | **échoue** (eof immédiat) | **jamais exécutée** | **inchangée** | **`true`** |
| `"true"` (non vide, illisible) | passe | échoue | **mise à `0`** (règle C++11) | `false` |

La règle C++11 « stocker 0 quand l'extraction échoue » **ne s'applique pas quand le sentry échoue**,
parce que `operator>>` ne s'exécute pas du tout. Et comme la lecture anticipée du sentry a positionné
`eofbit`, `from_string()` **retourne `true`**.

Or la clé **existe** (`jansson_decode_object` ajoute la paire même pour un `null`, avec la chaîne
vide), donc la garde `p.Exists("channel") && p.Exists("value")` **passe**. L'`unsigned int` non
initialisé partait dans `ola::DmxBuffer::SetChannel()` : **un canal au hasard, à un niveau au
hasard**. Six exécutions de `tests/OLAWire_test.cpp` y ont lu **21845, 21942, 22007, 22069, 22072 et
64**.

**Correction** : `OLAWire::decodeMessage()` initialise son `ChannelValue` à `{0, 0}`.
**`Utils::from_string` n'est PAS corrigé** — il est utilisé dans tout l'arbre, son comportement est
**épinglé** (`AnEmptyStringMakesFromStringWriteNothingAndStillClaimSuccess`) et pas modifié.
**Pas d'entrée `RELEASE_NOTES`** : aucun message que `OLACtrl` produit ne peut atteindre ce chemin.

⚠️ **À rechercher ailleurs** : le motif « déclarer une variable non initialisée puis la remplir par
`Utils::from_string()` sans regarder le retour » n'est pas propre à OLA. Partout où la source de la
chaîne peut être **vide**, la variable reste indéterminée. Non balayé par ce ticket.

### F-OLA-1bis ⭐⭐ Le balayage a été fait par la revue — **112 sites sans garde, et la famille est atteignable À DISTANCE**

Mécanisme **confirmé indépendamment** au source (`src/lib/StringUtils.h:104-110`, `return iss.eof()`)
et **prouvé à l'exécution**. Balayage `python3` de `src/` : **320 sites d'appel de
`Utils::from_string()`, 157 sur une destination non initialisée, dont 112 SANS garde.** Trois blocs :

- **(a) `IO/Wago/WagoExternProc_main.cpp`, 12 sites — le JUMEAU EXACT du défaut OLA, NON CORRIGÉ.**
  Même forme, même wire interne, même aplatissement en `Params`.
- **(b) `IO/KNX/KNXExternProc_main.cpp:144-147` et `:157-160`**, plus des accès `tokens[1..2]`
  **hors bornes**.
- **(c) ⚠️ ATTEIGNABLE À DISTANCE — c'est le bloc grave.** `JsonApi.cpp:774/777` passe la chaîne
  **client brute** à `set_value(string)`. Un `{"value":"impulse up "}` atteint
  `IO/OutputShutter.cpp:114` et son `int v` non initialisé dans `ImpulseUp(v)`. Idem
  `OutputShutterSmart` ×5, `OutputLightDimmer` ×5, `OutputLightRGB` ×10, `OutputLight`, `IntValue`,
  `JsonApi.cpp:1964` et `:2057`.

⇒ **le cas OLA était le seul INATTEIGNABLE de la famille**, ce qui valide l'absence d'entrée
`RELEASE_NOTES` **pour E4.1f uniquement**. **Un ticket dédié est en cours d'ouverture par un autre
agent ; E4.1f ne traite aucun de ces sites.**

### F-OLA-7 ⛔ `impulse down` ne peut pas fonctionner — bug adjacent, hors périmètre, ticketé ailleurs

Trouvé par la revue d'E4.1f. `IO/OutputShutter.cpp:120` fait `val.erase(0, 11)` **après** avoir
reconnu le préfixe `"impulse down "`, qui fait **13 caractères**. Il reste donc `"n <ms>"` en entrée
du `from_string`, qui **échoue systématiquement**. La commande `impulse down` est **inopérante**,
et — voir F-OLA-1bis bloc (c) — elle laisse en plus la durée **non initialisée**.
**Non corrigé ici** : hors du périmètre exclusif d'E4.1f, ticketé ailleurs.

### F-OLA-2 ⭐ Sur ce wire, un aller-retour est un oracle **qui ne peut pas échouer**

Le décodeur aplatit **toute** valeur en chaîne (règles de `jansson_decode_object`) puis la relit
avec `Utils::from_string()`. Mesuré : `[{"channel":"11","value":"200"}]` pilote le canal 11 à 200
**exactement comme** `[{"channel":11,"value":200}]`.

⇒ une régression de l'émetteur de `json_integer()` vers `json_string()` — le réflexe « tout part en
chaîne » du reste de la série E4.1 — **laisserait vert n'importe quel test d'aller-retour**, tout en
étant la panne silencieuse que ce ticket doit empêcher. **La preuve de type doit être sur le TEXTE
ÉMIS**, jamais sur un décodage. Épinglé par
`StringTypedEntriesAreAcceptedByTheDecoderWhichIsWhyTheEmitterMustBePinned`, qui existe pour que ce
point aveugle reste écrit.

### F-OLA-3 ⭐⭐ Le trou des arguments positionnels : **fermé par le typage**, mesuré dans les deux sens

Trois sous-tickets de la série ont mesuré qu'une permutation de deux arguments positionnels **au
site d'appel** restait verte : une fonction libre ne couvre pas son propre appelant. Sur OLA le trou
est fermé **par le compilateur**, pour **six lignes** de types :

| Sonde dans `OLACtrl.cpp` | Résultat |
|---|---|
| échanger les deux arguments de `buildSetValueMessage` | ⛔ `could not convert 'OLAWire::DimmerPercent(value)' … to 'OLAWire::DmxChannel'` |
| échanger les arguments rouge et bleu de `buildSetColorMessage` | ⛔ `could not convert 'OLAWire::BlueChannel(channel_blue)' … to 'OLAWire::RedChannel'` |
| `RedChannel(channel_blue)` / `BlueChannel(channel_red)` (échange **dans** l'enveloppe) | ✅ compile, suite **verte** — **résiduel** |

`DmxChannel` / `DmxLevel` / `DimmerPercent` sont distincts ; `RedChannel` / `GreenChannel` /
`BlueChannel` dérivent de `DmxChannel` (conversion **vers** la base, jamais entre frères). La
`ColorValue` est passée **entière**, donc l'appariement composante ↔ canal se fait **dans la
fonction testée**.

**Le résiduel est réel** : le typage ferme la permutation **d'arguments**, pas la substitution
**d'expression à l'intérieur** d'une enveloppe nommée. Mais `RedChannel(channel_blue)` se lit,
là où `f(a, b)` contre `f(b, a)` ne se lit pas. **Recommandation pour le reste de la série** : c'est
bon marché et c'est la seule fermeture réelle connue de ce trou.

### F-OLA-4 📏 Les décomptes d'`E4.1.md`/`E4.1f.md` ne se reproduisent pas

Recompté en `python3` sur `master` `3357e4f7` : `OLACtrl.cpp` porte **28** appels (annoncé 18) et
`OLAExternProc_main.cpp` **6** dont une **macro** (annoncé 5). Le **18** est le total **moins les 8
`json_integer` et les 2 `jansson_to_string`** — c'est-à-dire un décompte qui ignore précisément les
appels porteurs du contrat de type de ce wire. Le classement des fichiers ne change pas.

### F-OLA-5 ⚠️ La liste des « six unités » d'E4.1x est périmée — elle était déjà à **quatre**

Vérifié au compilateur sur `master` `3357e4f7` (`#include "Jansson_Addition.h"` retiré d'
`IO/ExternProc.h`, puis `make -C src/bin/calaos_server -k -j12`) : **`WagoMap.cpp` et
`WagoExternProc_main.cpp` recompilent déjà sans la délégation** — E4.1h les a sortis et est sur
`master`. **Après E4.1f, il reste DEUX unités** : `LuaScript/ScriptBindings.cpp` et
`LuaScript/ScriptExtern_main.cpp`. À recompter au moment d'E4.1x plutôt qu'à le lire.

### F-OLA-6 Une incohérence hors périmètre, signalée et non corrigée — **confirmée par la revue**

`OLAOutputLightRGB.cpp` déclare `channel_red` sur `0..9999` alors que `channel_green` et
`channel_blue` sont sur `0..512` (comme le canal d'`OLAOutputLightDimmer`). Un univers DMX512 a 512
canaux. C'est de la documentation d'IO (`ioDoc->paramAddInt`, ce que voit l'installeur), pas du
wire, et le fichier n'est pas dans le périmètre d'E4.1f.


## ⚠️ Dette méthodologique — les erreurs de RAISONNEMENT que la série a commises et mesurées

⚠️ **Section à APPENDRE, jamais à réécrire.** Chaque ligne est une conclusion qui s'est révélée
fausse *alors qu'elle était vraie en surface*. Un conflit sur cette section se résout en **gardant
les deux côtés**.

| # | Motif | Ce qui a été conclu | Ce qui était vrai | Ce qui l'a mis en défaut |
|---|---|---|---|---|
| **M-1** | *une limite vraie pour une raison fausse* | `F-LUA-3` — le typage « ferme le trou des sites d'appel » | le trou était fermé, mais pas par ce qui était invoqué | recomptage des sites |
| **M-2** | *0 rouge ≠ mutant équivalent* | **T3.35b §6.6** : `int idx = 0` est « mort », R1 (`int idx;`) donne 0 rouge ⇒ **équivalent par construction** | l'initialiseur était **VIVANT** : sur `[ ]` / `[\t]`, `Utils::from_string()` **n'écrit pas** la destination et la branche `idx = 0` n'est pas prise ⇒ `parent.at(idx)` lisait le **déclarateur** | ⭐ **la sonde POISON** : `int idx = 7` au lieu de `int idx;` ⇒ **4 rouges** (`array index 7 is out of range`, les deux parseurs). ⇒ **RÈGLE : une mutation d'initialiseur ne prouve l'équivalence QUE si sa variante poison est jouée aussi.** Une suppression d'initialiseur ne peut pas rougir un test portable ; une valeur fautive, si. |
| **M-3** | *une limite vraie pour une raison fausse* (même motif que M-1) | **T3.35b §6.8** : les lambdas de `subscribeStatusTopics()` sont intestables car « `IOBase` et `EventManager` ne sont pas dans la clôture de liaison » | **ils y sont** — `CORE_TEST_LDADD` commence par `CORE_SERVER_OBJECTS`, qui liste `IOBase.o` et `EventManager.o` ; `nm -C --defined-only` donne `IOBase::setStatusInfo` et `EventManager::create` en **`T`** et les **6 lambdas** définies dans `MqttCtrl.o`. La vraie raison est un **DISPATCH** absent : `subscribeCb` est **privé** et le seul code qui le parcourt est la lambda `messageReceived` du **constructeur**, pilotée par la boucle `uvw` de `calaos_mqtt` ; `storeMessage()` ne fait que **stocker** | lecture de `tests/Makefile.am` + `nm`. ⚠️ **Le coût de la fausse raison** : elle désigne « ajouter des `.o` au `LDADD` » comme sortie, ce qui **ne changerait rien**. La vraie désigne une **couture de dispatch** (ou l'extraction `resolveJsonPath()` de T3.37). |
| **M-4** | *une assertion satisfaite par autre chose que ce qu'elle prétend vérifier* (« fixture pauvre ») | **T3.35b §6.5** : `logContains(log, "weather[0]")` prouvait l'indication | elle était satisfaite par la **ligne précédente** (`subpath not found`) ; casser l'indication laissait **0 rouge** | mutation R2 rejouée après resserrage de l'aiguille ⇒ **2 rouges** |
| **M-5** | *un § qui affirme une clôture qu'il n'a pas mesurée* | **T3.35b §6.4** : « ⇒ il ne reste pas de trou à ficher » | la phrase portait sur la garde de **forme** et a été lue comme portant sur la garde d'**index** ; **cinq** jetons (`[]`, `[ ]`, `[\t]`, `[+]`, `[-]`) recevaient un « succès » de `from_string()` sans porter de nombre | sonde compilée contre le corps de `Utils::from_string` ⇒ tableau complet en **T3.35.md §6.4bis** |
| **M-6** | *une déclaration d'ABSENCE écrite après coup pour expliquer un vert* | **[F-LINK-1]** ci-dessous : « cet objet n'est lié par aucun binaire de test » — écrit cinq fois dans la série | **trois** de ces cinq déclarations sont fausses ou à moitié fausses ; « lié » et « exercé » sont deux propriétés différentes, et « ne peut pas être lié » n'a été vrai dans **aucun** cas examiné | aplatissement `python3` de `tests/Makefile.am` avec les variables Make développées ⇒ tableau des cinq déclarations rejouées, plus bas dans cette section |
| **M-7** | *un remède correct généralisé à une famille à laquelle il n'appartient pas* | **[F-HARN-1]** ci-dessous : `DECISIONS.md` faisait du relink `_DEPENDENCIES` la « cause racine des **cinq** variantes » de faux vert | il en existe une **sixième**, d'une autre famille : la mutation **non appliquée**. Elle survit à un `rm -f` parfait, à un `CXXLD` exigé et à un code de sortie 0 | campagne de contre-mutation T3.27, puis falsification en revue (`LAX_M2`) ⇒ le remède propre à cette famille est le **`cmp` d'application** + la comparaison des **ENSEMBLES** de rouges au témoin |

⭐ **Le fil commun de M-1, M-3 et M-5** : *la conclusion est juste, la justification ne l'est pas*.
C'est le cas le plus coûteux, parce que **rien ne rougit** — la seule défense est de **remesurer la
justification**, pas de revérifier la conclusion.

⚠️ **Fusion de deux apports (2026-08-25), au merge de T3.27.** Cette section a été écrite deux fois
le même jour : par **T3.35** (motifs M-1 à M-5, tableau ci-dessus) et par **T3.27** (M-6/M-7,
détaillés ci-dessous). Les deux contenus sont conservés **intégralement** et vivent désormais sous
un seul titre — le tableau ci-dessus est l'**index des motifs**, les sous-sections qui suivent
portent les **mesures**. La consigne du préambule ne change pas : **on appende, on ne réécrit pas**.

### T3.27 — la dette des déclarations « objet non lié » (suites de revue, 2026-08-25)

- ⭐⭐ **[F-LINK-1] DETTE MÉTHODOLOGIQUE TRANSVERSE — « cet objet n'est lié par aucun binaire de
  test » est un argument que ce dépôt écrit souvent, et qu'il vérifie rarement. Sur CINQ
  déclarations de ce type balayées, TROIS sont fausses ou à moitié fausses.**

  ⚠️ **Ce n'est pas une erreur de calcul, c'est une erreur de MÉTHODE**, et elle a une signature :
  la déclaration est écrite **après** avoir constaté un vert, pour l'expliquer. Elle referme la
  discussion (« hors d'atteinte, rien à faire ») là où la mesure aurait ouvert un cas à écrire.
  Les cinq occurrences ci-dessous ont toutes été rédigées de cette façon.

  ### Méthode de mesure — reproductible, à réutiliser telle quelle

  `tests/Makefile.am` est **aplati en `python3`** : continuations `\` recollées (`re.sub(r'\\\n',
  ' ')`), affectations `=` / `+=` accumulées dans l'ordre du fichier, puis **variables Make
  développées récursivement** (`$(CORE_TEST_LDADD)`, `$(CORE_SERVER_OBJECTS)`,
  `$(CALAOS_SERVER_BUILDDIR)`…) en gardant littéraux `$(OBJEXT)` / `$(top_builddir)`. Une **cible**
  est tout `FOO` portant un `FOO_SOURCES`, `FOO_LDADD` ou `FOO_DEPENDENCIES`, moins les faux
  positifs `AM` et `CORE_TEST`. On cherche alors l'objet dans la concaténation des trois variables.
  ⚠️ **Le balayage textuel naïf ne marche pas** : `CORE_TEST_LDADD` **contient**
  `CORE_SERVER_OBJECTS`, donc chercher `$(CALAOS_SERVER_BUILDDIR)` en toutes lettres sous-compte
  massivement — c'est le piège déjà consigné pour [`T3.36`](T3.36.md), et c'est **la même cause**
  que les trois déclarations fausses ci-dessous.

  ### Les cinq déclarations, rejouées

  | # | Déclaration, et où elle est écrite | Verdict | Mesure |
  |---|---|---|---|
  | 1 | `ScriptBindings.o` « n'est lié dans **AUCUN** binaire de test de l'arbre » — `E4.1j.md`, `FINDINGS.md`/F-LUA-3, `ORCHESTRATION.md` (journal E4.1j) | ⛔ **FAUSSE** | **1** cible (`LuaSandbox_test`) sur `d68e59f1`, `599fcea9`, `8bcffdc8` **et** `fb9d064c` — **jamais zéro**. Le vert venait du **relink** (`_DEPENDENCIES`), pas du périmètre |
  | 2 | « rien n'appelle Squeezebox/**RoonPlayer**/MqttCtrl, ces objets **ne sont même pas liés** » — `FINDINGS.md`/E4.0d, `ORCHESTRATION.md` (journal E4.0d) | ⛔ **FAUSSE pour `RoonPlayer`** | **8** cibles sur `d68e59f1`, **le commit E4.0d lui-même**, dont `core/JsonApiEvents_test` ; **17** sur `fb9d064c`. `MqttCtrl` : **0** à la date, **1** aujourd'hui (`JsonPathSyntax_test`) ⇒ **vraie mais périmée** |
  | 3 | `core/RoonArgs_test` est la « **seule suite de l'arbre** à lier `Audio/RoonPlayer.$(OBJEXT)` » — `T3.28.md`, `BOARD.md`, **et le commentaire de `tests/Makefile.am`** | ⛔ **FAUSSE** | **17** cibles sur `fb9d064c`, dont **16 `core/JsonApi*` qui lui préexistent toutes** |
  | 4 | `WagoCtrl.o` « n'est lié par aucun binaire et **ne peut pas l'être** » — `T3.30.md` | ⚠️ **MOITIÉ FAUSSE** | non-lié = **vrai** (**0** cible, les quatre arbres). « ne peut pas » = **faux** : `WagoCtrl.cpp` ∈ `calaos_server_SOURCES` **et** `calaos_wago_SOURCES`, `IO/Wago/WagoCtrl.o` **existe**, nom plat, comme `Audio/RoonPlayer.o`. La vraie limite est d'**exécution** (`!is_connected()`) |
  | 5 | `Squeezebox.o` « n'est lié par AUCUN binaire de test » (**F-SQBOX-2**) | ✅ **VRAIE** | **0** cible sur les quatre arbres |
  | 6 | `HueOutputLightRGB.o` lié par **exactement une** cible (E4.1d) | ✅ **VRAIE** | **1** — `core/LanHue_test`, sur les quatre arbres |

  ⇒ **3 fausses ou à moitié fausses sur 5 déclarations « non liable »** (les entrées 5 et 6 sont
  les deux qui tiennent ; l'entrée 6 est un compte exact, pas une déclaration d'absence).

  ### ⭐ Ce que la série apprend, et qui vaut plus que les six lignes

  1. **« Lié » et « exercé » sont deux propriétés différentes, et ce dépôt les confond.** Les 16
     suites `core/JsonApi*` **lient** `RoonPlayer.o` pour satisfaire l'éditeur de liens du cœur et
     ne l'**appellent** jamais. Écrire « pas lié » quand on veut dire « jamais appelé » **change la
     conclusion** : « pas lié » se lit *irréductible* et clôt le sujet ; « lié mais jamais
     exécuté » se lit *un cas à écrire*, et c'est presque toujours la vérité.
  2. **« Ne peut pas être lié » n'a été vrai dans AUCUN des cas examinés.** Tout `.o` d'un
     `_SOURCES` de `calaos_server` est présent dans l'arbre de build sous son nom plat et se lie
     par `$(CALAOS_SERVER_BUILDDIR)/…` — **50 cibles le font déjà**. L'obstacle réel, quand il
     existe, est **à l'exécution** (`!is_connected()`, un socket, un `fork`), et le nommer désigne
     la couture à écrire.
  3. **Ces déclarations naissent d'un vert qu'on explique après coup.** Le remède n'est pas la
     vigilance, c'est **la mesure avant la phrase** — et, quand un vert doit être expliqué,
     l'hypothèse à écarter **en premier** est le **faux vert de relink** ([`T3.36`](T3.36.md)),
     pas la limite de périmètre.

  ### Comptes `_DEPENDENCIES`, recomptés par ce ticket — ⚠️ trois chiffres divergents circulaient

  Trois valeurs coexistaient dans la doc (« 47 des 84 », « 49/85 », « 50/96 `check_PROGRAMS` /
  85 `TESTS` ») et **aucune n'est reproductible telle quelle**, parce qu'elles mélangent trois
  dénominateurs différents. Mesuré par la méthode ci-dessus, **le piège lui-même est réel** — seuls
  les cardinaux étaient en cause :

  | arbre | entrées `TESTS` | dont scripts shell | binaires de test | `check_PROGRAMS` | portant `_DEPENDENCIES = libcalaos_common.la` |
  |---|---|---|---|---|---|
  | `fb9d064c` (master) | **86** | 3 | **83** | **84** (83 + `StaticLogShutdown_helper`) | **49** |
  | `fix/t3.27` rebasée | **87** | 3 | **84** | **85** | **50** |

  ⚠️ **La valeur EST unique**, ce sont les dénominateurs qui bougent : **49 / 83 binaires** =
  **49 / 84 `check_PROGRAMS`** = 49 sur 86 entrées `TESTS`. Et **elle augmente à chaque suite
  ajoutée** : tout ticket qui cite ce ratio doit **dire sur quel arbre il l'a mesuré**.
  Aucune cible ne porte une **autre** valeur de `_DEPENDENCIES` : les 49 portent toutes exactement
  `$(top_builddir)/src/lib/libcalaos_common.la`.

  ⇒ **Ticket proposé** : le contrôle est **automatisable en dix lignes** — le script d'aplatissement
  ci-dessus, exécuté en cible non bloquante à côté de `check-config-docs.sh`, saurait répondre
  « quelles cibles lient `X.o` ? » et **rendrait toute déclaration de ce genre vérifiable avant
  d'être écrite**. Voisin naturel de [`T3.32`](T3.32.md) (contrôle ancré de la doc) et de
  [`T3.36`](T3.36.md) (le relink lui-même).

- ⭐ **[F-HARN-1] LA SIXIÈME VARIANTE DE FAUX VERT N'A PAS LA MÊME CAUSE RACINE QUE LES CINQ
  AUTRES — elle vient du HARNAIS, pas de `_DEPENDENCIES`.**

  `DECISIONS.md` (« `T3.36` : priorité haute, mais après `E4.1x` ») écrit que le défaut de relink
  est « la **cause racine des cinq variantes** de faux vert/rouge ». **Il en existe une sixième, et
  elle est d'une autre famille** : rencontrée pour de vrai pendant la campagne de contre-mutation
  de T3.27, elle survit à un `rm -f` parfait et à un `CXXLD` exigé.

  **Le mécanisme** : le motif de remplacement du harnais de mutation était faux d'un fragment
  (`* A pushed…` au lieu de `* raises. A pushed…`), l'`assert` du `python3` a échoué, et **comme le
  harnais n'avait pas `set -e`, le cas a tourné NON MUTÉ** — donc vert. Le binaire était
  correctement recompilé et relié, la ligne `CXXLD` était bien là, le code de sortie valait 0 :
  **tous les garde-fous de la série répondaient juste**, parce que ce qui manquait n'était pas le
  lien mais **la mutation elle-même**.

  ⇒ **Cause racine distincte, remède distinct** : `_DEPENDENCIES` se ferme avec `rm -f` + `CXXLD`
  exigée ; **celle-ci ne se ferme QUE par un `cmp` d'application** (comparer le fichier muté à
  l'original et **refuser de scorer** s'ils sont identiques) **et par la comparaison des ENSEMBLES
  de cas rouges au témoin**, jamais de leurs cardinaux. Dans l'épisode réel, **seule la comparaison
  des ensembles a révélé le problème** : `M0_control` et `M2` étaient **identiques**, ce qu'un
  compte de rouges (0 et 0) n'aurait pas dit.

  ⭐ **Falsifiée et validée en revue.** En mode laxiste — `cmp` d'application retiré, comparaison
  aux ensembles remplacée par un compte —, la reproduction `LAX_M2` donne
  **`CXX LuaScript/ScriptBindings.o` = 1, `CXXLD LuaCalaosApi_test` = 1, `.o` bien relié, 0 ligne
  `FAILED`, code de sortie 0** : **un faux vert parfait**, indiscernable d'un mutant survivant, sur
  un mutant qui n'a **jamais existé**.

  ⚠️ **Conséquence pour tous les briefs** : la consigne « `rm -f` + exiger `CXXLD` + juger au code
  de sortie » est **nécessaire et insuffisante**. Il faut y ajouter **`cmp` d'application** et
  **comparaison des ensembles**. `DECISIONS.md` est corrigé en ce sens : **cinq variantes de la
  famille `_DEPENDENCIES`, plus une sixième d'une autre famille.**

### T3.28b — ⭐ le premier remboursement de `F-LINK-1`, et le **troisième** verdict qui manquait (2026-08-25)

> Append à la section `F-LINK-1` ci-dessus, écrit **sans l'avoir vue** : `T3.28b` a été instruit en
> parallèle de la revue de `T3.27` et est arrivé aux **mêmes chiffres par une autre route**
> (aplatissement `python3` des corps de `*_LDADD`) — **8** cibles au commit E4.0d, **16** à la base
> de `T3.28`, **17** sur `fb9d064c`. Deux mesures indépendantes, un seul résultat. Ce qui suit est
> ce que `T3.28b` ajoute et qui n'était pas dans la mesure de `T3.27` : **la moitié « exécution »**.

- ⭐⭐ **[F-LINK-1, suite] Le point 2 de la section ci-dessus — « l'obstacle réel, quand il existe,
  est à l'exécution » — a été REJOUÉ sur `RoonCtrl`, et l'obstacle n'existait pas non plus.**

  `T3.28` justifiait ses deux tripwires de source par : *« `RoonCtrl::Instance()` est un singleton
  `static` dont le constructeur construit un `ExternProcServer` — qui bind un socket unix — et lance
  `calaos_roon`. Rien dans `make check` ne peut construire un `RoonCtrl`. »* **Chaque clause est
  vraie, la conclusion est fausse**, et elle n'avait jamais été essayée :

  1. `Instance()` est **publique et `static`** (`RoonPlayer.h:139`) ; seul le **constructeur** est
     privé, et une fabrique publique n'est pas un mur.
  2. Le **bind** (`IO/ExternProc.cpp:31-84`) crée un socket dans `/tmp` : aucun test de l'arbre
     n'a jamais été bloqué par ça.
  3. Le **`spawn`** est un **point d'observation**, pas un mur.
     `Prefix::binDirectoryGet()` (`src/lib/Prefix.cpp:31-37`) répond `getenv("CALAOS_BIN_PREFIX")`
     **sans le mettre en cache** ⇒ un test choisit l'exécutable lancé. Un faux `calaos_roon` qui
     journalise son `argv` et sort suffit : l'`ExitEvent` arme le `Timer::singleShot(0.1, …)`
     (`ExternProc.cpp:191`/`:198`), le contrôleur **relance**, et pomper la boucle donne **une ligne
     par lancement**.
  4. ⭐ **Le précédent dormait déjà dans l'arbre** : `tests/core/KnxIo_test.cpp` construit un **vrai
     `KNXCtrl`** — même forme de singleton, même `ExternProcServer`, même `spawn` — à **chaque**
     `make check`, et le documente dans son en-tête. La seule chose que personne n'avait faite :
     laisser le `spawn` **réussir**.

  **Mesuré** : 2 lancements journalisés en **120 ms**, tous deux portant
  `--namespace roon --host 192.168.7.42 --port 9331`. ⇒ `tests/core/RoonArgs_test.cpp` a un **14ᵉ
  cas d'exécution**, la tripwire du respawn passe **second filet**, et la mutation
  `startProcess(exe, "roon", std::string())` — qui **survivait** à `T3.28` — **rougit**.

- ⭐ **Le verdict manquant, et le tableau à trois entrées qu'il faut appliquer aux suivantes.**
  La section `F-LINK-1` ci-dessus distingue **non lié** de **lié mais non exercé**. La série a
  besoin d'une **troisième** case, parce que les tripwires de `T3.28` sont nées de sa confusion
  avec la deuxième :

  | Verdict | Comment le prouver | Ce qu'il autorise |
  |---|---|---|
  | **non lié** | corps des `*_LDADD` aplatis **et** `nm` sur le binaire | ajouter le `.o`, puis reposer la question |
  | **lié mais non ATTEIGNABLE** | nommer **le mécanisme exact** : méthode privée sans dispatcheur public (le cas `MqttCtrl` de **M-3**), garde `!is_connected()` (le cas `WagoCtrl`)… | une tripwire, **et l'écrire comme telle** |
  | ⭐ **atteignable, personne n'a regardé** | **l'appeler** | de **vrais** tests |

  ⚠️ **`RoonCtrl` était le troisième cas et sa fiche annonçait le deuxième.** Un singleton n'est
  pas une preuve d'inatteignabilité : `Instance()` est publique, et un `spawn` s'observe.

- ⭐ **Le critère qui dit quelle tripwire SURVIT à cette découverte, et il est vérifiable par
  mutation.** Des deux tripwires de `T3.28`, une seule tombe :
  - `…TheRespawnLaunchesThroughTheSameCallSite` était justifiée **par le lien/l'exécution** ⇒ elle
    tombe de son rôle de premier filet dès que le code est exercé ;
  - `…ThePortMemberIsInitialisedToTheDefaultPort` est justifiée par une **équivalence
    observationnelle** (`RoonPlayer.cpp:218` écrit `port` **inconditionnellement** avant tout
    usage) ⇒ elle **survit**, et le brief de `T3.28b` avait raison de le pressentir.
    **Vérifié, pas lu** : la mutation `int port = RoonArgs::DefaultPort;` ⇄ `int port = 0;` laisse
    **tous** les cas comportementaux verts — y compris les quatre qui construisent un vrai
    `RoonPlayer` — et ne rougit **qu'elle**.

  ⇒ **RÈGLE : une tripwire justifiée par une équivalence observationnelle est légitime ; une
  tripwire justifiée par « on ne peut pas lier / on ne peut pas atteindre » doit être rejouée avant
  d'être crue.** C'est le pendant exact de la règle **M-2** (« 0 rouge ≠ mutant équivalent »).

⛔ **Ce que `T3.28b` n'a PAS mesuré** : les entrées 1, 4 et 5 du tableau de `T3.27` ci-dessus, et
les autres déclarations d'inatteignabilité encore debout — `FINDINGS.md` `ReolinkCtrl::doRegisterCamera`
(« méthode privée d'un singleton qui lance un processus »), la permutation du `brace-init` de
`ReolinkEventRegistry`, le wire KNX (« constructeur privé derrière un singleton qui lance deux
sous-processus ») et `WagoMap` (« bind un socket UDP »). **Aucune n'est rejouée.** Elles sont
listées ici pour que la prochaine campagne commence par la mesure et non par la citation.

### T3.28b — **revue** : la règle `F-LINK-1` était juste et **incomplète**, et une 7ᵉ affirmation tombe (2026-08-25)

⚠️ **Append à la section `F-LINK-1` ci-dessus. En conflit, GARDER LES DEUX CÔTÉS** — cette section
est éditée par plusieurs agents, on appende, on ne réécrit jamais en silence.

- ⭐⭐ **[F-LINK-1] LA RÈGLE RÉÉCRITE — elle avait DEUX trous, et les deux sont du même genre : elle
  décrivait un verdict sans dire COMMENT il se prouve.**

  La rédaction précédente (juste au-dessus) disait :

  > ⇒ **RÈGLE : une tripwire justifiée par une équivalence observationnelle est légitime ; une
  > tripwire justifiée par « on ne peut pas lier / on ne peut pas atteindre » doit être rejouée
  > avant d'être crue.**

  ⛔ **Elle reste vraie, elle n'est pas suffisante.** La revue de `T3.28b` a relevé que *« son
  premier membre ne tient que parce que l'auteur a fait ce qu'elle ne dit pas — vérifier
  l'équivalence PAR MUTATION, pas par lecture »*. Et il lui manquait le symétrique. Rédaction
  complète, à citer telle quelle :

  > ⭐ **RÈGLE `F-LINK-1` (v2), trois membres :**
  >
  > 1. **Une tripwire justifiée par une ÉQUIVALENCE OBSERVATIONNELLE est légitime — à condition que
  >    l'équivalence soit prouvée PAR MUTATION, jamais par lecture.** Une équivalence lue est une
  >    hypothèse ; une équivalence mutée est une mesure. La forme du geste : muter la propriété
  >    prétendument invisible, et exiger que **l'ensemble** des cas rouges soit **exactement** la
  >    tripwire — si un cas comportemental rougit aussi, l'équivalence est fausse ; s'il n'en
  >    rougit aucun, y compris la tripwire, c'est la tripwire qui ne garde rien.
  > 2. **Une tripwire justifiée par « on ne peut pas lier / on ne peut pas atteindre » doit être
  >    rejouée avant d'être crue.** Sept de ces déclarations sont tombées à la mesure dans ce dépôt.
  > 3. ⭐ **Le verdict « atteignable, personne n'a regardé » doit être rejoué LUI AUSSI** — c'est le
  >    seul des trois qui autorise à **déplacer** une tripwire de son rôle de premier filet, donc
  >    c'est celui qui coûte le plus cher s'il est faux. Le rejouer veut dire : le cas d'exécution
  >    qui remplace la tripwire doit rougir sous **la mutation que la tripwire attrapait**, mesurée,
  >    sur le code livré. Un cas d'exécution vert ne prouve rien de ce qu'il remplace.

  ⭐ **Le membre 3 n'est pas théorique : c'est exactement ce qui a mordu `T3.28b` à sa première
  livraison.** Elle avait prouvé « atteignable » et écrit de vrais tests — mais son cas d'exécution
  atteignait le singleton **en appelant `RoonCtrl::Instance()` lui-même**, ce qui laisse
  `RoonPlayer.cpp:227` — le site qui **reporte la configuration de l'utilisateur** — hors du chemin.
  La mutation `Instance(host, port)` ⇄ `Instance(host, RoonArgs::DefaultPort)` **survivait à 14/14,
  sortie 0** : le défaut de terrain de `T3.28` **déplacé d'un site**, invisible.
  **Le remède tient en une phrase et vaut pour tout singleton** : *un binaire de test EST un
  processus*. `tests/core/RoonSpawnViaPlayer_test` donne au singleton un processus neuf et couvre
  l'autre forme ; la mutation rougit, et **elle seule**.

- ⭐ **[F-LINK-1, 7ᵉ affirmation tombée] Le wire KNX — « constructeur privé derrière un singleton
  qui lance DEUX sous-processus » — est atteignable, et il est DÉJÀ exercé à chaque `make check`.**

  Elle figurait dans la liste « pas rejouée » du paragraphe ci-dessus. Elle est tombée pendant la
  revue de `T3.28b`, **par le mécanisme que `T3.28b` venait de publier** : `CALAOS_BIN_PREFIX` n'est
  pas mis en cache (`src/lib/Prefix.cpp:31-37`), et `KNXCtrl` construit son `exe` à partir de lui
  (`IO/KNX/KNXCtrl.cpp:98`). **Mesuré deux fois indépendamment** (le relecteur, puis la livraison
  corrigée), en posant un `calaos_knx` enregistreur dans un `mktemp -d` et en lançant
  `tests/core/KnxIo_test` **sans le modifier** :

  ```
  --socket <sock> --namespace knx --server ip:127.0.0.1
  --socket <sock> --namespace knx --internal-monitor-bus --server ip:127.0.0.1
  ```

  **Les DEUX sidecars démarrent et journalisent leur `argv`**, `KnxIo_test` sort en 0. ⇒ la ligne de
  commande KNX, y compris celle du moniteur de bus, est **observable depuis `make check` dès
  aujourd'hui**, sans une ligne de `src/`. ⚠️ **Personne n'a écrit ce test** : c'est une mesure, pas
  un filet. Ticket évident pour qui passera par là.

- ⛔ **Ce qui reste NON REJOUÉ après cette correction — la liste à jour.** Le paragraphe
  « Ce que `T3.28b` n'a PAS mesuré » ci-dessus en listait **quatre** ; le wire KNX en sort. Il
  reste **trois** :

  | # | Déclaration | Où | État |
  |---|---|---|---|
  | 1 | `ReolinkCtrl::doRegisterCamera` — « méthode privée d'un singleton qui lance un processus » | `FINDINGS.md` | ⚠️ **non rejouée** — mais le singleton lance un **processus**, donc le mécanisme `CALAOS_BIN_PREFIX` s'applique *a priori* |
  | 2 | la permutation du `brace-init` de `ReolinkEventRegistry` | `FINDINGS.md` | ⚠️ **non rejouée** |
  | 3 | `WagoMap` — « bind un socket UDP » | `FINDINGS.md` | ⚠️ **non rejouée** — ⭐ un bind de socket n'a jamais été un mur dans **aucun** des sept cas mesurés |

  Les entrées **1, 4 et 5** du tableau de `T3.27` restent également non rejouées.
  ⚠️ **Elles sont listées, pas instruites.** Aucune ne doit être citée comme acquise.

---

### F-RGB-1 ⛔ Troisième site de la classe « longueur écrite deux fois » — `IO/OutputLightRGB.cpp:107-109`

Trouvé par le balayage exhaustif exigé par **T3.34** (critère d'acceptation n°2), sur tout `src/` :
**50 sites** `compare(0, N, "…")`, dont **3** où `N` ou le `erase(0, M)` qui suit ne vaut pas la
longueur du littéral. Deux sont ceux de T3.34 ; le troisième est ailleurs :

```cpp
107|    else if (val.compare(0, 8, "set off ") == 0)
109|        val.erase(0, 4);        // ⛔ "set off " fait 8
```

`set off #AABBCC` devient `off #AABBCC`, `ColorValue` le refuse (`isValid()` faux) et **toute la
branche est un no-op silencieux** : ni couleur mémorisée, ni `cmd_state`, ni erreur.

⛔ **CORRECTION (revue T3.34, 2026-08-25) — la version précédente de cette fiche écrivait que la
branche était « morte de bout en bout ». C'est FAUX, et c'est mesuré comme tel.** La revue a lié une
vraie `OutputLightRGB` et envoyé `set off #445566` : la commande renvoie **`success`**, `setColorReal`
reçoit **+0 appel**, l'état de l'IO est **inchangé**. La branche est donc **atteinte**, elle ne fait
simplement **rien**.

**Elle est appelable depuis trois entrées de l'arbre**, toutes générales — chacune passe une chaîne
cliente arbitraire à `IOBase::set_value(std::string)`, donc à `OutputLightRGB::set_value` :

| Site | Chemin |
|---|---|
| `JsonApi.cpp:774` | `set_state` de l'API JSON — `io->set_value(jParam["value"])` |
| `Rules/ActionStd.cpp:152` et `:207` | les actions de règle et de scénario (`TBOOL` littéral, puis `TSTRING`) |
| `LuaScript/ScriptBindings.cpp:191` | `setIOValue()` en Lua — `io.set_value(Utils::to_string(lua_tostring(L, 2)))` |

⇒ **Ce qui est vrai** : la branche **n'a jamais fonctionné**, donc **rien ne régresse** à la laisser
en l'état et le report hors périmètre reste justifié.
⇒ **Ce qui est faux** : « morte ». C'est une **commande cassée et appelable aujourd'hui** — depuis
l'API, depuis les règles, depuis Lua — qui répond `success` et **n'a silencieusement aucun effet**.
Le seul fondement de l'ancienne formulation était que `set off ` n'apparaît dans aucun
`ioDoc->actionAdd()` et que `cmd_state = "set off …"` (`:118`) n'est produit que par la branche
elle-même : **non documentée ≠ non atteignable**. `set_value` n'a jamais eu de liste blanche.

⚠️ **Cette nuance décide du sort de la fiche** : une fiche qui dit « mort » ne sera jamais reprise ;
une fiche qui dit « appelable et silencieusement sans effet » le sera. **Mérite son propre ticket**,
avec la décision « réparer ou supprimer » posée explicitement — et, si c'est « réparer », en sachant
que les effets réactivés (mémorisation de couleur, `cmd_state_bool = false`, `DELETE_NULL(timer_auto)`)
n'ont jamais été observés par personne.
⇒ **Versé à `F-LINK-1`** : c'est la **sixième** affirmation d'inatteignabilité démentie par la mesure.

### F-SIGC-1 ⭐ `Timer::singleShot` + `sigc::mem_fun` sur un objet non-`trackable` — le motif, pas seulement les volets

Instruit en livrant **T3.34**. ⚠️ **L'hypothèse initiale — « `ImpulseDown(0)` ⇒ échéance immédiate
⇒ UAF à chaque appel » — est INFIRMÉE**, et sa mécanique est fausse. Mesuré dans l'image du
conteneur, **libuv 1.44.2**, sonde autonome liant le vrai `src/lib/Timer.cpp` :

```
singleShot(-0.001)  -> uv timeout = 18446744073709551615   fired=0  handle ARMÉ, jamais collecté
singleShot(0.0)     -> tire au tour de boucle SUIVANT
singleShot(0.035)   -> tire à 34 ms
```

Un délai négatif passe `static_cast<uint64_t>(-1.0)` à `uv_timer_start`, qui **écrête l'échéance
débordante à `(uint64_t)-1`** : le one-shot **ne tire jamais**. Un délai nul tire au tour suivant,
alors que l'objet est **encore vivant**. ⇒ **ni 0 ni négatif ne produisent la fenêtre qu'un UAF
exige.**

**Mais l'UAF existe, et son déclencheur est une commande BIEN FORMÉE.** `class IOBase`
(`IOBase.h:35`) **ne dérive de rien** : ce n'est pas un `sigc::trackable`, donc
`sigc::mem_fun(*this, &X::Stop)` tient un pointeur **nu** que rien ne déconnecte, et le handle uvw
du one-shot est **anonyme** — aucun destructeur ne peut l'annuler. Il suffit d'un **délai positif
court** (`impulse up 500`) suivi d'un `ListeRoom::deleteIO()` (API JSON), d'un `Room::RemoveIO()` ou
d'un `~Room()`. **Reproduit trois fois en exécution, sans sanitizer** :

1. IO détruite puis boucle pompée ⇒ **SIGSEGV, exit 139, et AUCUNE ligne `FAILED`** ;
2. la simple succession *création / `impulse` / destruction* de deux cas voisins : le one-shot
   orphelin est retombé sur l'adresse **réutilisée** par le volet suivant et **l'a arrêté** — un
   volet vivant piloté par le callback d'un volet mort ;
3. trace `gdb` : `sigc::bound_mem_functor0<void, Calaos::OutputShutterSmart>::operator()` appelé
   depuis `uvw::TimerHandle::startCallback` ← `uv_run`.

**T3.34 n'a gardé que ses 4 sites** (les deux `Up()`/`Down()` de `OutputShutter.cpp` et de
`OutputShutterSmart.cpp`), par jeton de vie `std::weak_ptr` sur un membre — le schéma d'`aliveTag`
de `Timer`/`Idler` eux-mêmes. Il devait les prendre : **corriger la longueur agrandit la fenêtre**
de `impulse_time` seul à la durée demandée par le client, jusqu'à la course complète du volet.
⭐ **Le motif reste à instruire ailleurs dans l'arbre** — c'est plus large qu'un ticket sur les
volets, et ça mérite son propre balayage (`Timer::singleShot` + `sigc::mem_fun`, ou lambda capturant
`this`, sur un objet dont la durée de vie n'est pas garantie jusqu'à l'échéance).

### F-IMP-1 Fuite de handle libuv atteignable à distance, une par commande

Corollaire mesuré du même régime écrêté. Deux entrées d'API mènent à un délai négatif dans les IO
volet :

- `impulse_time` **absent** (volet à relais ordinaire) ⇒ `impulse_time == -1`, donc
  `impulse down 0` ou `impulse down -1` rend la somme négative ;
- `Utils::from_string` **sature à `INT_MAX`** ⇒ `{"value":"impulse down 99999999999999999999"}`
  fait déborder `INT_MAX + impulse_time` (**UB signé**), qui repasse négatif.

Le one-shot **ne tire jamais et reste armé** : le volet fait sa course complète, **et un handle
libuv retenant l'IO fuit à chaque appel**. Il suffit de répéter la commande. Corrigé dans T3.34 pour
ses deux fichiers (somme et borne en `double`, délai écrêté à zéro) ; ⚠️ **le même calcul d'échéance
en `int` non gardé est à chercher ailleurs** — partout où une durée venue du client est additionnée
puis divisée avant d'atteindre un timer.

### F-TEST-2 ⚠️ Le piège de non-relink s'étend au FAUX ROUGE, pas seulement au faux vert

Rencontré en livrant T3.34, et il a coûté deux campagnes de mesure avant d'être vu. Le piège connu
(`_DEPENDENCIES = libcalaos_common.la` ⇒ les `.o` serveur ne sont pas des prérequis) est présenté
comme un producteur de **faux verts** : le binaire ne relie pas le code modifié. Mesuré ici, il
produit aussi des **faux rouges et des crashs**, et ceux-là sont bien plus déroutants.

Séquence : une campagne de contre-mutations restaure les sources à la fin **sans reconstruire**.
`OutputShutterSmart.o` reste donc celui de la **dernière mutation**. Un `cd tests && make <suite>`
relie alors joyeusement (`CXXLD` bien présent, code de sortie du `make` à 0) un objet **muté** à des
sources saines : la suite **segfaute**, et le journal accuse un cas qui n'a rien fait de mal.

⇒ **Le `rm -f` des `.o` touchés ne suffit pas s'il n'est pas suivi d'un `make` qui les reconstruit
vraiment** — un `make` lancé dans `tests/` **ne reconstruit pas `src/`**. La séquence sûre est
`rm -f <objets> <binaire>` puis un `make` **à la racine**, puis le `make` de la suite, et le
`CXXLD` n'est une preuve que du **lien**, jamais de la fraîcheur des objets liés.



---

## T3.34 — revue (2026-08-25) : versement à **F-LINK-1**, et le motif `singleShot`

### ⭐ [F-LINK-1, apport n°6] Une sixième déclaration d'inatteignabilité, démentie par la mesure

⚠️ **Cette entrée s'ajoute à `F-LINK-1`** (dette méthodologique transverse : « cet objet n'est lié
par aucun binaire de test » / « cette branche est morte », écrit sans mesure), section
*« T3.27 — suites de revue : la dette des déclarations "objet non lié" »* ci-dessus.
⚠️ **Section à APPENDRE, jamais à réécrire** — en conflit, **garder les deux côtés**.

Les cinq premières affirmations d'inatteignabilité démenties portaient sur le **lien**
(`ScriptBindings`, `RoonPlayer`, `WagoCtrl`, `MqttCtrl` — celle-là vraie mais **périmée**) ou sur des
**lambdas MQTT**. **La sixième est de la même famille, sur un autre axe** : elle ne dit pas
« pas lié », elle dit **« branche morte »** — et elle est fausse exactement de la même façon, pour
la même raison.

| | |
|---|---|
| **Déclaration** | « `OutputLightRGB.cpp:107` — la branche `set off ` est **morte de bout en bout** » (`T3.34.md` §1 et `FINDINGS.md`/F-RGB-1, première rédaction) |
| **Argument avancé** | `set off ` n'est dans aucun `ioDoc->actionAdd()`, et le seul producteur de `cmd_state = "set off …"` est la branche elle-même |
| **Verdict** | ⛔ **FAUSSE** |
| **Mesure** | `OutputLightRGB` réelle, `set off #445566` ⇒ **`success`**, `setColorReal` **+0 appel**, état **inchangé**. Atteinte depuis **`JsonApi.cpp:774`**, **`ActionStd.cpp:152` / `:207`**, **`ScriptBindings.cpp:191`** |
| **Ce qui restait vrai** | la branche **n'a jamais fonctionné** ⇒ rien ne régresse, le report hors périmètre tient |

⭐ **Ce que ce sixième cas ajoute à la leçon de `F-LINK-1`.** Les cinq premiers confondaient **lié** et
**exercé**. Celui-ci confond **documenté** et **atteignable** — et c'est le même glissement : on
constate une absence *dans un endroit qu'on sait lire* (le `Makefile.am`, l'`ioDoc`) et on en conclut
une absence *dans l'arbre*. Or `IOBase::set_value(std::string)` **n'a pas de liste blanche** : trois
chemins généraux (API JSON, règles, Lua) passent une chaîne cliente arbitraire, et **aucun** ne
consulte l'`ioDoc`. La forme générale de l'erreur, valable pour les six :

> **une propriété de la DOCUMENTATION ou du BUILD est prise pour une propriété du FLOT DE CONTRÔLE.**

⇒ **Même remède que `F-LINK-1`** : la mesure **avant** la phrase. Ici elle tient en une ligne — lier
l'IO, envoyer la commande, regarder la valeur de retour **et** la sonde d'effet. ⚠️ Et le coût de
l'erreur n'est pas nul : **« mort » referme la fiche pour de bon**, alors que **« appelable et
silencieusement sans effet » la fait reprendre**. La formulation décide de la suite, pas la sévérité.

### ⚠️ Précision de mécanique versée à **F-SIGC-1** : `sigc::trackable` ne protège PAS une lambda

Recompté en revue, et c'est le point qui fait diverger les balayages. `Timer::singleShot`
(`src/lib/Timer.cpp:103-123`) recopie la `sigc::slot` dans le handler uvw et l'appelle telle quelle :

- **`sigc::mem_fun(*this, …)` sur une classe `sigc::trackable`** ⇒ le slot est **notifié** de la mort
  de l'objet et devient un no-op. **Protégé.** (C'est le cas des **6** sites de `Squeezebox`.)
- **une lambda qui capture `this`** ⇒ sigc++ ne voit **qu'un foncteur opaque**. `trackable` ne
  déconnecte rien, la lambda est appelée, le `this` est pendouillant. **NON protégé**, que la classe
  dérive de `trackable` ou non. (C'est le cas des **8** sites de `RoonPlayer`, des **2** d'
  `ExternProcServer`, de celui d'`EventManager` et de celui d'`UrlDownloader:734`.)

⇒ **Filtrer un balayage sur « la classe dérive-t-elle de `sigc::trackable` ? » sous-compte** : la
question qui décide est **« la cible est-elle un `mem_fun`, ou une lambda ? »**. Détail consigné ici
parce qu'il change la liste de [`T3.40`](T3.40.md), pas seulement son cardinal.

### ⭐ `F-LINK-1`, septième et huitième cas — et le **huitième est celui qui coûtait le plus cher**

Versés par la **deuxième revue de `fix/t3.25`** (réserve 1). Les six premiers cas confondaient
**lié** / **exercé** et **documenté** / **atteignable**. Les deux suivants confondent
**« il faudrait un banc » / « il faut regarder où le produit publie déjà »**.

| # | La phrase qui était fausse | La mesure qui l'a tuée |
|---|---|---|
| **7** | *« les IO Wago ne sont pas testables sans matériel ni sous-processus »* | ⭐ **`WagoMap::get_maps()` est PUBLIC** (`WagoMap.h`), et `WagoMap` **indexe ses singletons sur le couple `(host, port)`**. Le port qu'une IO a **réellement choisi** est donc lisible **depuis le produit**, sans mock, sans accesseur neuf, sans extraction en en-tête. `core/WagoPortDefault_test` épingle les **9** lignes `port` des six classes Wago avec ce seul joint |
| **8** | *« `relay_num` n'a pas d'observable »* | **Presque vrai, et c'est le piège** : son unique consommateur (`set_value_real` → `RemoteUIManager::sendCommand`) **sort en avertissement** tant qu'aucun appareil ne tient un websocket. Mais l'objet était **lié** dans `core/RemoteUIDeviceInfo_test` **depuis T3.15** et **jamais instancié**. Coût réel de la fermeture : **un accesseur `const` de trois mots**, pas un banc |

⇒ **La règle se resserre** : avant d'écrire « non testable », chercher **ce que le produit publie
déjà** — une table de singletons, un registre, un compteur, un `get_*()` existant. Sur ces deux
cas la réponse était **dans le `.h` de production**, à côté de la ligne à épingler.

### ⚠️ Le motif de la pile n'est **pas** une règle — correction versée à la dette des non-initialisés

`T3.25` §8.7 annonçait « à `-O2`, une locale non initialisée lit `0` cinq fois sur cinq ». **Faux.**
Même programme, deux compilateurs, cinq exécutions chacun :

| | `-O0` | `-O1` | `-O2` | `-O3` |
|---|---|---|---|---|
| **g++ 12.2** | `32766` ×5 | `0` ×5 | `0` puis `32648` ×4 | — |
| **g++ 16.2** | — | — | `0` ×5 | `0` ×5 |

⇒ **C'est un phénomène du compilateur ET de la forme de la pile, pas une propriété.** Conséquence
qui vaut pour **toute** la série : **un oracle qui attend `0` sur un chemin de non-initialisation
est VIDE PAR CONSTRUCTION** — il a été mesuré **vert chez un relecteur** et **rouge chez l'auteur**
sur exactement le même code défectueux. Le remède est celui déjà employé par
`StringUtilsFromString_test`, `OLAWire_test` et `ScriptWire_test` : **semer une sentinelle non
nulle** (`21845`, `0xA5A5A5A5`, une chaîne impossible) **et peindre la pile** (`0x55`) quand la
destination est une locale de production hors d'atteinte. ⚠️ Même peinte, ce n'est pas un rouge
**garanti** : un compilateur qui garde la variable en **registre** ne touche pas la pile. **Le rouge
déterministe d'un défaut de non-initialisation vit sur la PRIMITIVE, jamais sur l'IO.**

### `MqttCtrl.cpp:226` — un non-initialisé que la famille « débordement » vient de rejoindre

`int b;` sans initialiseur, gardé par `is_of_type<int>` à `:247` (`getValueColor`, luminosité MQTT).
**Avant `T3.25`** un débordement donnait `b = INT_MAX` — faux mais **défini**. **Depuis**, la garde
refuse, `b` **n'est jamais écrite**, et si `path_x`/`path_y` ont été lus alors `err == false` et
`b / 255.0` lit une valeur **indéterminée**. Ce n'est **pas neuf** (tout jeton non numérique y
menait déjà) et ce n'est **pas** une régression de `T3.25` : c'est la **famille des entrées qui
l'atteignent** qui s'élargit. → [`T3.35`](T3.35.md).

### ⚠️ Le piège du `endif` mord AUSSI à la résolution de conflit, pas seulement à l'écriture

Mesuré sur le rebase de `fix/t3.25` sur `b7a4c63d`. `tests/Makefile.am` reçoit des **appends purs
des deux côtés**, donc « garder les deux côtés » est la bonne résolution — **et elle est fausse
telle quelle**. Les deux versants du conflit s'arrêtent **avant** le `endif` final, qui est du
**contexte partagé** situé **après** le marqueur `>>>>>>>`. Concaténer les deux versants laisse donc
**un seul `endif` pour deux `if` ouverts** : mesuré `if` **80** / `endif` **79**, profondeur finale
**1**, et automake casse. ⇒ **Le résolveur doit refermer explicitement le versant amont** avant de
coller le versant local. **Recompter `if`/`endif` sur CHAQUE commit de la série**, pas seulement sur
la tête : le trou avait été introduit au **premier** des 11 et se propageait à tous les suivants —
et il rendait **le commit de caractérisation non compilable**, donc la preuve par contre-mutation
impossible.

### ⭐ Neuvième variante de faux vert/rouge — **la restauration pristine par `copy2` ne recompile rien**

Rencontrée pendant la campagne de la deuxième revue de `fix/t3.25`, et elle vaut pour **toute** la
série, pas seulement pour ce ticket. Le protocole publié dit « **restaurer depuis une copie
pristine, jamais `git checkout`** » — et l'implémentation évidente, `shutil.copy2()`, **préserve la
date de modification**. Le fichier restauré ressort donc **plus ancien** que les objets compilés
depuis le mutant : `make` les juge à jour et **ne les recompile pas**.

**Mesuré** : le retour au témoin a rendu **1 rouge** (`KNXCtrlWire_test`) sur un arbre dont les
**11 fichiers étaient prouvés identiques au pristine par `cmp`**. `git status` propre, `cmp` propre,
et pourtant le binaire portait encore la mutation. Ici le symptôme est un faux **rouge** ; la même
mécanique produit un faux **vert** dès que la mutation est restaurée *avant* la passe qui doit la
voir.

⇒ **Après toute restauration** : `os.utime()` sur chaque fichier restauré **et** purge des objets
de `src/`, puis reconstruction complète. ⚠️ La purge bute sur des artefacts appartenant à `root`
(`src/lib/llhttp/src/.libs/`, écrits par le conteneur de compilation) — **pas de `sudo`** : la date
rafraîchie sur les sources suffit, puisque tout ce qui les inclut redevient périmé.

⭐ **C'est la n° 11 de la liste canonique** (plus bas, avant `F-PYTEST-1`). ⚠️ Cette section disait
« **la neuvième** » : le compte d'alors n'avait énuméré ni `F-BUILD-1` ni `F-TYPE-3`. Les **onze**
ont la même forme : **l'arbre a l'air juste à l'endroit qu'on regarde**.

### ⚠️ Un oracle écrit par l'audit et démenti par la campagne — `ColorUtils.cpp:245`

Consigné parce que c'est **la campagne, et non la relecture, qui l'a trouvé**. L'audit du volet
débordement avait classé `ColorValue::setString("99999999999")` en « comportement CHANGÉ » : avant
`setRgb(32767,255,255)` et `isValid() == true`, maintenant `ColorInvalid`. **Faux.** Il avait lu
`setAlpha()`, qui **borne**, et supposé que `setRgb()` en faisait autant. `setRgb()` **sort
immédiatement** sur une composante hors `0..255` **sans écrire `type`**, laissé à `ColorInvalid` en
tête de `setString()` — et tout débordement entier sature à `INT_MAX`/`INT_MIN`, donc `dec >> 16`
vaut `32767` ou `-32768`, **jamais** dans `0..255`.

⇒ **Aucun décimal en débordement n'a jamais pu construire une couleur valide**, ni avant ni après.
Le cas de test écrit pour l'épingler était **vide** : mesuré **vert** sous `CM-OVF`. Il est
**conservé en témoin** (il fige la borne `16777215` → `#FFFFFF`) et **étiqueté comme tel dans son
propre commentaire**. **Leçon** : lire la fonction **appelée**, pas sa voisine de même famille —
`setRgb` borne, `setAlpha` borne, et **`setRgb` refuse**.

### ⚠️ `F-PYTEST-1` recompté : ce ne sont pas **11** cas qui ne s'exécutent pas, ce sont **19**

Mesuré pendant la campagne de la deuxième revue de `fix/t3.25`, sur l'image de compilation
courante, `make check` **vert**, `PASS: run-python-tests.sh`, `exit status: 0` :

```
Ran 23 tests in 0.025s
OK
```

**23** cas exécutés. Or `tests/python/` en déclare **42** :

| Fichier | Cas | Style | Exécuté ? |
|---|---|---|---|
| `test_t116_mcp_config_io.py` | 10 | `unittest` | ✅ |
| `test_t116_roon.py` | 8 | `unittest` | ✅ |
| `test_t116_mcp_client.py` | 5 | `unittest` | ✅ |
| ⛔ `test_auth.py` | **11** | pytest | ❌ |
| ⛔ `test_extern_proc.py` | **4** | pytest | ❌ |
| ⛔ `test_logger.py` | **4** | pytest | ❌ |

`run-python-tests.sh` bascule en **repli `unittest` de la bibliothèque standard** quand `pytest`
est absent — et il l'est dans l'image. Le repli ne charge que les suites écrites en `unittest` :
les **19** cas en style pytest sont **silencieusement sautés**, sans une ligne de journal, et le
script **rend 0**. Le chiffre de 11 ne comptait que `test_auth.py`.

⇒ **Le contrôle reste le même, et il faut l'appliquer aussi ici** : compter **les cas réellement
exécutés** à chaque passe, jamais le seul code de sortie. Et le correctif de fond est d'installer
`pytest` dans l'image, ou de faire de son absence un **SKIP explicite (77)** par fichier plutôt
qu'un silence. Hors périmètre de `T3.25` ; **aucun** des 19 ne touche `from_string`.
---

## T3.37 — l'extraction du parseur de chemin JSON (2026-08-25)

### ⭐ [F-WEB-1] `WebCtrl::getValue()` ne rapporte rien, et **trois** de ses **quatre** appelants traitent un échec comme une lecture

La §6.8 de [`T3.35`](T3.35.md) laissait la divergence ouverte avec cette justification :
*« `WebCtrl::getValue()` rend une chaîne et aucun de ses trois appelants n'en demande ; lui en
ajouter un serait de l'API morte »*. **C'est un constat, pas une justification** — et le compte
était de trois, il est de **quatre**.

| Site | Ce qu'il fait d'un échec silencieux (`""`) | Verdict |
|---|---|---|
| `IO/Web/WebOutputString.cpp:41` | `value = getValue(path); emitChange();` | ⚠️ **publie la valeur vide**, écrasant la précédente |
| `IO/Web/WebInputString.cpp:42` | `if (v != value) { value = v; emitChange(); }` | ⚠️ **idem**, une fois (déjà noté en T3.35 §6.8) |
| `IO/Web/WebOutputAnalog.cpp:41` | `if (Utils::is_of_type<double>(v)) Utils::from_string(v, value);` | ✅ **inerte** — `is_of_type<double>("")` rend **`true`** (flux vide ⇒ `eof()`), mais `from_string("", value)` **n'écrit rien** ⇒ valeur précédente conservée, pas d'émission |
| `IO/Web/WebDocBase.h:113` → `WebCtrl::getValueDouble()` | `double val = 0; … return val;` puis `convertValue()` + `emitChange()` | ⚠️⚠️ **publie `0`** — une température de 0 °C ou un analogique nul que **rien** ne distingue d'une vraie lecture, et sur lequel les règles de l'utilisateur agissent |

⭐ **La forme n'est PAS celle du §6.1 de T3.35, et c'est mesuré** : là-bas, `err = false` posé
inconditionnellement alimentait un `double rawValue;` **non initialisé** (≈ −5,3·10³⁰⁷ % de
batterie). Ici il n'existe **aucune locale lisible non initialisée** — `getValueDouble()` déclare
`double val = 0;`, `WebOutputAnalog` écrit dans un membre — ce que le tableau du §6.3 de T3.35
disait déjà de ce fichier. Même **famille** (un échec publié comme une lecture), **gravité
moindre** : valeur plausible et déterministe.

⇒ **T3.37 tranche : le drapeau est produit par le parseur unifié pour TOUT LE MONDE** (il n'existe
plus de variante sans drapeau), **et il s'arrête à `WebCtrl::getValueJson(path, filename, bool &err)`**,
épinglé par test. Il n'est **pas** câblé jusqu'aux quatre sites, et la raison n'est pas
« personne n'en demande » :

- ⛔ **un drapeau honnête sur une branche sur trois serait le défaut du §6.1, pas sa correction.**
  `getValue()` sert **JSON, XML et TEXT**. `getValueXml()` a **cinq** retours d'échec explicites
  (`WebCtrl.cpp` 254, 266, 284, 292, 298),
  `getValueText()` en a **deux** explicites **plus** des échecs silencieux non instruits
  (`Utils::from_string(tokens[0], line_nb)` dont le retour n'est pas regardé — **famille T3.25** —
  et un `item_nb` hors plage qui rend `""`). Un `err` qui vaut « échec » pour JSON et « rien » pour
  les deux autres est un drapeau qui ment, exactement comme celui que T3.35b a dû défaire.
- ⛔ **aucun des quatre sites n'est atteignable par un test** de `JsonPathSyntax_test`
  (`WebCtrl::Instance()` est un singleton indexé sur l'URL, les IO passent par `IOFactory`).

**Suite à donner, nommée** : rendre `WebCtrl::getValue()` honnête sur ses **trois** branches et
sauter la mise à jour chez les **quatre** appelants, avec la caractérisation qui va avec. C'est un
**changement de comportement** (une valeur vide ou nulle cesse d'être publiée) et il lui faudra sa
note de version — T3.37 n'en produit aucun et n'en écrit donc pas.

### ⭐ [F-BUILD-1] `make check` a imprimé **deux** `Testsuite summary`, le premier était un FAUX VERT — **et les DEUX comptaient un test de moins que l'arbre n'en déclare**

⚠️ **Finding sous-vendu, et RÉÉCRIT le 2026-08-25 après revue.** Il a été **observé une fois**, il
n'a **jamais été reproduit**, et l'hypothèse que la première rédaction avançait a depuis été
**mesurée et INFIRMÉE**. Il est consigné parce que le *symptôme* suffit à égarer une lecture, et
parce que la **règle de lecture** qu'il impose, elle, vaut indépendamment de la cause.

**Ce qui a été mesuré**, sur le commit de caractérisation de T3.37 (`ec1abcb0`), au **tout premier
build d'un worktree neuf** (aucun `Makefile`, aucun objet), recette
`./autogen.sh && ./configure && make -j12 && make check -j6` dans le conteneur :

```
... Testsuite summary ...  # TOTAL: 87  # PASS: 87  # FAIL: 0     <- PREMIER bloc
... Testsuite summary ...  # TOTAL: 87  # PASS: 86  # FAIL: 1     <- SECOND bloc
```

alors que `tests/JsonPathSyntax_test.log` porte bien **2 `FAILED`** et que le binaire, exécuté
directement, sort en **1**.

#### ⭐ L'épisode comportait DEUX défauts, pas un — et la première rédaction n'en a vu qu'un

**Le commit de caractérisation déclarait 88 entrées `TESTS`**, pas 87 — mesuré en `python3` sur le
blob de `ec1abcb0` *tel qu'il était au moment de l'épisode*, avant le rebase de la branche :
3 hors condition + 84 sous `HAVE_GTEST` + 1 sous `HAVE_GTEST && HAVE_LIBKNX` = **88**, zéro
doublon ; et le relecteur en mesure **88** partout sur **ce commit et cette image**. Or **les deux
blocs annoncent `# TOTAL: 87`**.

⇒ ⭐ **Une suite n'a pas tourné du tout**, et le résumé ne l'a pas dit — pas de `SKIP`, pas de
`ERROR` : elle est simplement **absente du total**. C'est la famille **`F-PYTEST-1`** (branche
`fix/fpytest1` en vol, parente de **[F-DEP-2]** ci-dessus : une suite qui ne s'exécute pas est
rapportée **`PASS`**, jamais `SKIP` — mesuré ailleurs à **3 fichiers sur 6 et 19 cas sur 42**).

**L'épisode est donc la superposition de deux variantes distinctes** : un **doublon d'affichage**
dont le premier bloc ment (`FAIL: 0` sur un arbre rouge) **et** un **test non exécuté** que ni
l'un ni l'autre des deux blocs ne signale. La première rédaction de ce finding notait `TOTAL: 87`
**deux fois sans le relever** et concluait au seul doublon : c'était une lecture incomplète.

#### ⛔ L'hypothèse « redémarrage de `make` après régénération » est INFIRMÉE, mesurée

La première rédaction avançait : *GNU make ré-exécute le but après que `config.status` a régénéré
les `Makefile`, et la sortie des deux tentatives se retrouve dans le même flux.* **Testée
directement, elle ne tient pas.** `touch tests/Makefile.in`, `touch configure.ac` (qui déclenche
`autoreconf` **puis** `config.status`) et `touch tests/Makefile.am` **régénèrent bien pendant
`make check`** — et ne produisent **jamais** deux résumés. make 4.3 refait les makefiles **avant**
d'attaquer le but, pas après ; automake 1.16.5.

⭐ **Fait structurant qui en découle** : **seul `tests/Makefile.am` définit `TESTS`**. Deux résumés
exigeraient **deux invocations de `check-TESTS`** — donc deux `Makefile` portant une règle
`check-TESTS`, ce que cet arbre n'a pas. La cause reste **NON ÉTABLIE**, et l'explication la plus
naturelle vient d'être éliminée.

⚠️ **Non reproduite en 9 exécutions** : les **2 premiers builds** d'un worktree neuf, aux deux
parallélismes (`-j12 && make check -j6` **et** `-j32 && make check -j16`), plus **6 relances**
dont **3** forçant explicitement la régénération (`tests/Makefile.in`, `configure.ac`,
`tests/Makefile.am`). **Plus 9 exécutions de la passe de correction du 2026-08-25**, dont une où
`tests/Makefile.am` a **réellement** changé et où `automake` + `config.status` ont régénéré
`tests/Makefile` **pendant** la recette : **un** bloc, `88/88/0` ; **plus une dixième** après le
rebase de la branche sur `701a98e4` (`T3.28b` a ajouté une suite) : **un** bloc, `89/89/0`,
régénération comprise. ⭐ **À SURVEILLER, ni à nier ni à
surestimer** : une observation unique, jamais rejouée, dont le mécanisme est inconnu et dont
l'hypothèse la plus plausible est morte.

#### ⭐ La règle de lecture de `make check` — c'est elle qui survit à la cause

⛔ **Le remède écrit dans la première rédaction — « le code de sortie, ou le DERNIER bloc » — est
INSUFFISANT, et sa seconde moitié ne vaut rien.**

- La **première** moitié est juste : le code de sortie était correct dans les deux cas.
- ⛔ La **seconde** est circulaire. Sans `--output-sync`, l'ordre du flux sous `-j` est
  **précisément** ce qu'on invoque pour expliquer le doublon ; « prendre le dernier trouvé »
  **présuppose donc ce qu'il faudrait prouver**. Il n'y a aucune garantie que le dernier bloc
  imprimé soit le dernier bloc produit.
- ⛔ Et **ni l'une ni l'autre ne détecte `F-PYTEST-1`** : une suite qui ne tourne pas ne fait ni
  échouer `make check`, ni apparaître un second bloc. Le code de sortie est **0** et il a raison
  sur ce qu'il mesure — il mesure simplement moins de tests qu'il n'y en a.

⭐ **RÈGLE CORRECTE, à appliquer à tout verdict `make check` de ce dépôt :**

> Un `make check` n'est vert que si **(1)** son **code de sortie** est `0` **ET** **(2)** le
> `# TOTAL` du résumé **égale le nombre attendu d'entrées `TESTS`** — c'est-à-dire les entrées
> `TESTS` de `tests/Makefile.am` **activées par la configuration** de l'image utilisée.
> Les deux conditions sont nécessaires ; **aucune des deux n'est suffisante**.
> Le code de sortie seul rate `F-PYTEST-1` ; le `# TOTAL` seul rate un échec.

**Comment obtenir le nombre attendu** (`python3`, jamais `grep` — le hook `rtk` réécrit `grep` et
`awk`) : compter les jetons de toutes les lignes `TESTS =` / `TESTS +=` de `tests/Makefile.am`,
en tenant la pile des `if`/`else`/`endif` pour savoir lesquelles la configuration active.
⚠️ **Deux façons parfaitement légitimes de déplacer le nombre attendu**, et il faut les connaître
avant de crier au trou de couverture : **(a)** `HAVE_LIBKNX` — sur une image sans libknx le nombre
attendu passe de 88 à **87**, la seule façon honnête d'obtenir 87 à l'époque, et le relecteur a
vérifié que ce n'était **pas** le cas sur l'image de l'épisode ; **(b)** ⭐ **le simple passage du
temps** — `T3.28b`, fusionné le 2026-08-25, fait passer l'arbre de 88 à **89**. ⇒ **Le nombre
attendu se RECOMPTE à chaque verdict ; il ne se retient pas, et surtout il ne se recopie pas d'une
fiche à l'autre.**

**Et le mieux reste le mieux** : sur une cible précise, **exécuter le binaire de test directement**
et relever son code de sortie. C'est le protocole que la campagne de mutations de T3.37 utilise à
chaque tour, et il est resté juste partout — y compris pendant cet épisode.

⚠️ **Ce que cela ajoute aux variantes déjà consignées** : ce n'est ni un défaut de relink
(`_DEPENDENCIES`), ni une mutation non appliquée (`F-HARN-1`), ni une mort du binaire. C'est un
**piège de LECTURE** doublé d'un **trou de couverture** : `grep -m1 'Testsuite summary' -A6` sur
une reconfiguration peut rendre un vert parfait sur un arbre rouge, **et** un résumé parfaitement
formé peut taire une suite entière.

### ⚠️ Précision versée à **M-3** : l'extraction de T3.37 **n'ouvre PAS** les lambdas de `subscribeStatusTopics()`

La §6.8 de T3.35 donnait deux sorties possibles à l'intestabilité des trois
`if (!readStatusNumber(v, rawValue)) return;` : une **couture de dispatch**, ou *« l'extraction de
`resolveJsonPath()` de T3.37 »*. **La seconde est fausse, et T3.37 le constate en la livrant** :
extraire le parseur donne un point d'entrée testable **au parseur**, pas aux lambdas. Celles-ci
restent enregistrées dans `subscribeCb`, qui est **privé**, et que seul le lambda
`process->messageReceived` du **constructeur** parcourt. Il reste donc **une seule** sortie : la
couture de dispatch.

⇒ Motif **M-1/M-3** encore une fois — *une sortie plausible qui ne mène nulle part* —, consigné ici
pour que la fiche T3.35 ne soit pas lue comme si T3.37 l'avait refermée.

### ⭐ [F-RELINK-T337] `JsonPathSyntax_test` ne se relie PAS quand `MqttCtrl.cpp`/`WebCtrl.cpp` changent — **faux ROUGE reproduit sur source propre** (périmètre [`T3.36`](T3.36.md))

⚠️ **Préexistant** : introduit par [T3.29](T3.29.md) avec la cible, **pas** par T3.37 — mais T3.37
le consigne parce que c'est son harnais qui le porte, et parce qu'un relecteur **l'a subi**.

`JsonPathSyntax_test_DEPENDENCIES` est écrasé à **`$(top_builddir)/src/lib/libcalaos_common.la`
seul**, alors que le `_LDADD` de la cible relie **`IO/Mqtt/MqttCtrl.o`**, **`IO/Web/WebCtrl.o`** et
**`IO/Web/WebDocBase.o`**. Ces trois objets ne sont donc **pas des prérequis du binaire** : `make`
les **recompile** bien, et le binaire **n'est pas relié**.

**Mesuré** sur `fix/t3.37`, même image, `make -j12` à la racine puis `make check -j6`, verdict pris
sur le **code de sortie du binaire exécuté directement** :

| # | Geste | `CXXLD` | `MqttCtrl.o` recompilé | binaire | rouges | `make check` |
|---|---|---|---|---|---|---|
| 1 | mutation M6 de `MqttCtrl.cpp` **+ `rm -f`** binaire et `.o` | **1** | 1 | **1** | **3**, tous MQTT | 88/87/1 |
| 2 | ⭐ **restauration PRISTINE de `MqttCtrl.cpp`, SANS `rm -f`** | **0** | **1** | **1** | **les 3 MÊMES** | **88/87/1** |
| 3 | même source pristine, **avec `rm -f`** binaire + `.o` de test | **1** | 0 | **0** | **0** — 63/63 | 88/88/0 |

*(campagne menée **avant** le rebase sur `701a98e4`, d'où le total de 88 ; l'arbre livré en déclare
**89** et sort `89/89/0`. Le trou de relink, lui, ne dépend pas du nombre de suites.)*

⇒ ⭐ **La ligne 2 est un FAUX ROUGE sur un arbre propre** : `make` recompile bien `MqttCtrl.o`, mais
le binaire relié à l'objet **muté** survit et continue d'échouer. C'est la variante **symétrique**
du faux vert habituel de `_DEPENDENCIES`, et elle est **plus traître** : un faux vert fait rater un
défaut, un faux rouge fait **inventer** un défaut qui n'existe pas — puis « corriger » du code sain.

### ⛔ Une précision que la revue avait à l'envers, et elle est mesurée

La revue avançait que **faire du parseur un en-tête met le geste normal — éditer `JsonPath.h` —
« pile dans le trou »**. ⛔ **C'est faux, et l'inverse est vrai** : `tests/JsonPathSyntax_test.cpp`
fait `#include "JsonPath.h"`, donc l'en-tête est dans le **`.deps` de la cible elle-même**.

**Mesuré** : mutation du message `"no path segment to resolve"` dans `JsonPath.h`, **sans aucun
`rm -f`** ⇒ **`CXXLD` = 1**, objet de test recompilé, binaire à **exit 1**, **2 rouges**
(`MqttJsonPathTest.APathThatSplitsIntoNoTokenIsLoggedAndNamesThePath` et son jumeau
`WebJsonPathTest.*`) — **un par copie de l'appelant**. Le relink a bien eu lieu.

⚠️ **Mais la conclusion pratique de la revue reste JUSTE, pour une autre raison** : cette protection
est **incidente, pas conçue**. Elle ne tient qu'aussi longtemps que la suite **inclut** `JsonPath.h`.
Or le §4 de la fiche T3.37 pousse justement à ne tester qu'à travers les deux enveloppes ; le jour
où quelqu'un retire cet `#include`, **l'en-tête rejoint les deux `.cpp` dans le trou, sans que rien
ne le signale**. ⇒ Ne jamais s'y fier : appliquer la règle, pas l'exception.

**Règle pour cette cible** (écrite aussi **en commentaire dans `tests/Makefile.am`**, au-dessus du
bloc `if HAVE_GTEST` de la cible — c'est là que le prochain regardera) : `rm -f` du binaire **et**
de `tests/JsonPathSyntax_test-JsonPathSyntax_test.o` avant **chaque mutation** *et* avant **chaque
restauration**, puis exiger la ligne **`CXXLD    JsonPathSyntax_test`** (espace **double**, chemin
relatif à `tests/`) et prendre le verdict au **code de sortie du binaire lancé directement**.

⚠️ **Ampleur, recomptée en `python3` sur l'arbre rebasé sur `701a98e4`** : **87 `check_PROGRAMS`**,
**52** portent la surcharge `_DEPENDENCIES`, **51** relient réellement des objets serveur ⇒ **le
trou est armé sur 51 cibles**, une seule surcharge est inoffensive. ⚠️ **Piège de décompte
confirmé** (déjà signalé par `T3.36`) : un balayage naïf du seul `_LDADD` textuel donne **31**,
parce que `CORE_TEST_LDADD` **contient** `CORE_SERVER_OBJECTS` — il faut développer les variables.
⚠️ Ces trois nombres **montent à chaque suite ajoutée** (`T3.36` en mesurait 47/48/80) : les
recompter, jamais les recopier.

⛔ **La ligne `_DEPENDENCIES` n'a PAS été corrigée par T3.37, et ne doit pas l'être à la légère** :
c'est le périmètre de `T3.36`, cela concerne 50 cibles d'un coup, et une modification hâtive du
harnais est **exactement** ce qui a produit les **onze** variantes de faux vert/faux rouge de
la série (liste canonique numérotée plus bas).
Ce finding est là pour que `T3.36` hérite d'une **mesure**, pas d'une intuition.

---

## T3.45 — la fabrication d'archive source (2026-08-25)

### F-DIST-1 — ⭐ **l'archive source est inconstructible** : les bibliothèques vendorées ne sont distribuées par personne

Révélé par le **premier `make distcheck` du dépôt** (T3.45). Une fois la faute de chemin de
`src/lib/calaos-python/Makefile.am` réparée, `make dist` produit bien une archive, elle se déplie,
`configure` réussit — puis **la construction échoue** :

```
src/lib/ExpressionEvaluator.cpp:2: fatal error: exprtk.hpp: No such file or directory
src/lib/uvw/src/uvw.hpp:1:    fatal error: uvw/async.hpp: No such file or directory
```

`src/lib/Makefile.am` distribue l'en-tête parapluie `uvw/src/uvw.hpp` mais **aucun** des en-têtes
`uvw/src/uvw/*.hpp` qu'il inclut ; `exprtk.hpp` n'est listé **nulle part**.

Mesure statique (suivis par git ∖ contenu du tarball), avec le commit de chaque chiffre :

| Commit de `fix/t3.45` | suivis absents | dont code sous `src/` |
|---|---:|---:|
| `e416ff25` (le correctif) | **414** | **126** |
| `8a117171` (tête, la fiche `T3.45.md` en plus) | **415** | **126** |

L'écart de 1 sur le total est **exactement `docs/refactoring/T3.45.md`** (`docs/` n'est distribué
par personne : 162 des 415). ⭐ L'écart **125 / 126** n'est **pas** `src/lib/sole/demo.cc` mais
**`src/lib/sole/sole.cxx`** : le jeu d'extensions `.c .cc .cpp .h .hpp` donne 125, y ajouter `.cxx`
donne 126. Répartition du code : 57 `uvw` (sur 82 fichiers suivis, **1 seul** distribué — le
parapluie), 31 `sqlite_modern_cpp` (**0** distribué), 27 `exprtk` (**0** distribué), 4
`libquickmail`, 4 `sole`, plus **`version.h`**, **`OWFSUtils.h`** et **`sigc_fix_functor.h`**, qui
sont à nous et non à un tiers. ⭐ `pugixml` (4/4) et `calaos-python` (8/8) sont **complets** : l'état
correct est atteignable arbre par arbre. ⚠️ Les **licences** des tiers ne partent pas non plus
(`llhttp/LICENSE`, `llhttp/LICENSE-MIT`, `libquickmail/COPYING`, `exprtk/license.txt`).

⚠️ **Portée** : aucun binaire n'a pu être construit depuis un tarball de ce dépôt **depuis
2025-02-16** — d'abord parce que `make dist` échouait, ensuite parce que l'archive est incomplète.
Donc **aucun risque de binaire divergent** en circulation ; c'est le **chemin de release** qui est
mort, pas le produit.

**Non corrigé par T3.45** (périmètre distinct et bien plus large : la faute T3.45 était une faute
de chemin d'une ligne). ⭐ **Numéro attribué : [`T3.48`](T3.48.md)**, avec un point de départ imposé
— *`make distcheck` doit franchir le build*.

### F-DIST-2 — la CI ne lance **ni `dist` ni `distcheck`**

`.github/workflows/ci.yml` : build, `make check`, couverture lcov, `clang-format` sur lignes
changées, job de dépendances Python du sidecar MCP. **Zéro** occurrence de `dist` dans tout
`.github/`. C'est la raison pour laquelle F-DIST-1 et la faute T3.45 ont vécu **~18 mois** sans être
vues : *personne ne lançait la cible*.

**Remède proposé** : un job CI `distcheck` séparé — mais **après** [`T3.48`](T3.48.md), sinon il
est rouge dès le premier jour. En attendant, `tests/check-extra-dist.sh` (T3.45, **33,7 ms**
médiane sur n=20, dont 12 ms de démarrage d'interpréteur) tient le sous-ensemble « un chemin de
distribution qui ne résout aucun fichier », **pas** le reste — il vaut dans le sens
« déclaré ⇒ existe » et **jamais** dans le sens inverse, qui est celui de F-DIST-1.

### F-DIST-3 — `make dist` réécrit les `po/*.po` suivis

Lancer `make dist` régénère `po/calaos.pot` et fait un `msgmerge` sur les `.po`. ⚠️ **Compte
corrigé** : sur les **8** `po/*.po` **suivis**, **7 sont réécrits** (`de`, `es`, `fr`, `hi`, `nb`,
`pl`, `ru`) et ⭐ **`en.po` reste strictement inchangé** ; `po/calaos.pot` est réécrit lui aussi.
Volume mesuré : **20 589** lignes ± sur les 7 `.po` et **2 870** sur le `.pot`. Deux `.po` **non
suivis** (`en@quot.po`, `en@boldquot.po`, générés depuis `en.po`) sont réécrits sans salir
`git status`. Le `.pot` commité date du **2025-06-29** : les
références de lignes sources y sont périmées. Sans conséquence fonctionnelle, mais **tout
lanceur de `make dist` doit penser à `git checkout -- po/`** — piège à commit accidentel.

---

## T3.31 — le typage contre la permutation d'arguments (2026-08-25)

- ⚠️ **[F-WAGO-4] et [F-REO-5] — ✅ CORRIGÉS EN PARTIE par T3.31** (branche `fix/t3.31`, quatre
  commits : caractérisation, correction, documentation, attribution de T3.46 — ⚠️ **sans SHA
  volontairement**, la branche a été rebasée deux fois et un SHA recopié est faux dès le rebase
  suivant ; référence stable : `master` = `1c6ab7a9`). Deux en-têtes neufs, `IO/Wago/WagoTypes.h`
  (`Address`, `Count`, `WordValue`, `BitValue`) et `IO/Reolink/ReolinkTypes.h` (`Hostname`,
  `Username`, `Password`, `EventType`) : **un champ, constructeur `explicit`, aucune base commune,
  aucun opérateur de conversion**. `WagoWire`, `WagoMap`, `WagoCtrl`, `ReolinkWire`, `ReolinkCtrl`
  et `ReolinkEventRegistry::CameraRegistration` les prennent. **Sept contre-mutations par échange,
  sept refus de compilation deux à deux distincts** (fichier, ligne et paire de types différents à
  chaque fois) ; **témoin sans mutation : aucun refus**. Le commit de caractérisation est
  **rouge exécuté** : 5 cas rouges sur 62 exécutés, `RC=1` sur les trois suites, `CXXLD` vu pour
  chacune. **Restent NON fermés et déclarés : le saut dans `libmbus` (F-WAGO-9 ci-dessous, ⇒ [T3.46](T3.46.md)) et
  l'emballage de la mauvaise variable** (`WagoTypes::Address(val)` type-checke ; limite
  intrinsèque, une ligne par commande).

- ⭐ **[F-TYPE-1] — MESURE QUI INFIRME UNE RÈGLE PUBLIÉE : un constructeur positionnel ne ferme
  RIEN.** La règle du §1bis de [T3.31](T3.31.md) — *« un type nommé ne ferme rien s'il reste un
  agrégat initialisable positionnellement »* — et son acceptation n°3 — *« fermer
  `CameraRegistration` **par un constructeur** »* — sont **fausses pour une structure à plusieurs
  champs**, mesuré au `g++ -std=c++20 -Wall -Wextra -Wconversion` :

  | forme | `T{a, b, c, d}` permuté | `T(a, b, c, d)` permuté |
  |---|---|---|
  | agrégat nu (master) | **compile**, 0 avertissement | **compile** (init. d'agrégat entre parenthèses, C++20) |
  | + constructeur positionnel (**ce que la fiche demandait**) | **compile**, 0 avertissement | **compile** |
  | + un type fort **PAR CHAMP** | **refusé** | **refusé** |

  ⇒ **ce n'est pas l'agrégat-ness qui ouvre le trou, c'est l'ORDRE.** Un constructeur retire
  l'agrégat-ness et reste positionnel. `OLAWire.h` (`refactor/e4.1f`) ferme parce que ses
  enveloppes ont **UN champ** — donc aucun ordre à se tromper —, **pas** parce qu'elles ne sont pas
  des agrégats. La règle réécrite : **un type ferme quand il ne reste plus deux paramètres du même
  type côte à côte, à aucun niveau — signature comprise.** Épinglé par
  `tests/ReolinkRegistry_test.cpp`, cas `TheAggregateProbesActuallyDiscriminate`.

- ⭐ **[F-TYPE-2] — les désignateurs C++20 sont COSMÉTIQUES ici, mesuré.**
  `Reg{.hostname = h, .username = **p**, .password = **u**, …}` — bonne clé, mauvaise valeur —
  **compile**, 0 avertissement. Seul le désordre **des clés** est refusé (C++20 impose l'ordre de
  déclaration), et ce n'est pas ainsi que le bug se produit. ⇒ **ils rendent la permutation
  visible, pas impossible** : la fiche le disait, c'est maintenant mesuré.
  Mesuré aussi et sans effet sur une permutation de **paramètres** : `[[nodiscard]]` (il garde une
  valeur de **retour**), et un ordre imposé par le type de retour (les valeurs restent
  interchangeables à l'intérieur de chaque étape).

- ⚠️ **[F-WAGO-9] — NON CORRIGÉ, PRÉEXISTANT, hors périmètre de T3.31, ✅ TICKETÉ :
  [T3.46](T3.46.md)** (numéro **attribué par le coordinateur**, vérifié libre sur `master` et dans
  les huit worktrees vivants — `T3.40` à `T3.45` sont pris). **`libmbus` prend l'adresse et la
  donnée comme deux `mbus_uword` côte à côte, sur des ÉCRITURES.** `mbus_cmd_force_single_coil(mbus, slave, coil_addr, data)` et
  `mbus_cmd_preset_single_register(mbus, slave, register_addr, preset_data)` (`libmbus/mbus.h:114`
  et `:116`) : une permutation **compile en silence** et **force un relais / écrit un registre à
  une adresse arbitraire de l'automate**. C'est la troisième ligne rouge de
  T3.43 §5.5bis (⚠️ fiche **non mergée**, worktree `.wave57/fwago8`), et T3.31 ne la ferme pas — il ferme les **trois sauts Calaos**
  au-dessus (`WOAnalog` → `WagoMap` → `WagoWire`, puis `WagoExternProc_main` → `WagoCtrl`), pas
  celui-là. **Mesuré comme résiduel** : contre-mutation M5 de T3.31, `mbus_cmd_preset_single_register(mbus, 1, (mbus_uword)val, address)` **compile, rc=0**.
  ⭐ **Et ce n'est PAS une impossibilité technique, contrairement à ce que la première rédaction du
  correctif affirmait** : mesuré au `gcc -std=c11`, une `struct` à **un champ** ferme une
  permutation en **C** exactement comme en C++ (`error: incompatible type for argument 1`). Ce qui
  arrête T3.31, c'est **le coût et la propriété** : sept signatures d'une bibliothèque **tierce
  importée** (`$Id: mbus_conf.h,v 1.1.1.1 2003/…`), déjà divergée d'amont, répartie sur quatre
  fichiers `.c`, et que **rien dans l'arbre n'exécute** — la correction serait vérifiée **par la
  compilation seule**. T3.43 §3 a refusé le même changement pour la même raison, et un
  précédent de la même nuit a refusé de patcher `uvw` vendu au profit d'un ticket. **Une voie
  intermédiaire a été envisagée et écartée, mesurée** : une façade C++ typée au-dessus de
  `libmbus` déplacerait le déballage de **sept endroits vers sept endroits** — `WagoCtrl.cpp` est
  le seul appelant C++ et il n'a **qu'un** site par commande. **Gain net nul.**

- ⚠️ **[F-TYPE-3] — un en-tête TEMPLATE ne se vérifie pas au `-fsyntax-only`, et une campagne de
  mutation peut en mourir.** Mesuré en écrivant T3.31 : la mutation M4 permute les arguments dans
  `WagoIOBase.h`, et `g++ -fsyntax-only WagoIOBase.h` a rendu **`rc=0`, faux vert** — le corps est
  dans un membre de `WIDigitalBase<T>`, **jamais instancié** tant qu'aucune unité ne l'instancie.
  Compilée via `WIDigitalBP.cpp`, la même mutation rend **`rc=1`**. ⇒ **une passe de mutation qui
  vise un en-tête template DOIT compiler une unité qui l'instancie**, jamais l'en-tête seul. À
  ranger à côté des cinq variantes de `_DEPENDENCIES` : c'est **une façon de plus** d'obtenir un
  vert parfait sans avoir rien vérifié. ⚠️ **Cette entrée s'écrivait « une sixième » : le rang était
  déjà pris par `F-HARN-1`.** ⇒ **c'est la n° 10 de la liste canonique** (plus bas), reclassée au
  merge de `T3.31` comme cette liste le prévoyait.

- ⭐⭐ **[F-TYPE-4] — un correctif de typage ne balaye qu'UNE DIRECTION de la chaîne, et laisse
  l'autre nue. Mesuré sur T3.31 même, par sa revue.** T3.31 a typé la chaîne Reolink
  **sortante** (enregistrement d'une caméra : `registerCamera` → `CameraRegistration` →
  `buildRegisterMessage`) et a livré en se croyant complet. La chaîne **entrante** — `dispatch`,
  `cameraKey`, `hasCamera`, `callbackCount`, `EventCallback`,
  `ReolinkInputSwitch::eventReceivedCallback` — est restée en `std::string` nus, et les mots
  « `cameraKey` » et « `dispatch` » avaient **zéro occurrence** dans la fiche de livraison.
  Mutations du relecteur : `cameraKey(event_type, hostname)` et
  `dispatch(event_type, hostname, event_data)` ⇒ **`make` complet, 0 `error:`, 0 avertissement**.
  ⚠️ **La conséquence de la seconde était pire que celle que le ticket fermait** : une `dispatch()`
  permutée cherche une clé sous laquelle rien n'est enregistré, renvoie 0, et **tous les
  événements de toutes les caméras sont perdus en silence**.
  ⇒ **Règle** : sur un ticket de typage, **recenser les DEUX sens du trajet avant de livrer** —
  commande **et** réponse, émission **et** réception, aller **et** retour. La même vérification a
  immédiatement produit une seconde occurrence : les quatre `sigc::slot` de réponse de
  `WagoMap.h:38-41` et leurs **onze** implémentations, toutes nues (⇒ T3.46 partie B).
  ⚠️ **Et ces sites sont exactement ceux qu'aucun test ne peut atteindre** : `nm` sur les binaires
  de test ne trouve **aucun symbole `ReolinkCtrl`**, donc les deux mutations étaient **vertes par
  construction**. Une fiche de typage doit donc **énumérer les signatures**, pas affirmer une
  couverture — *une fiche qui affirme une couverture qu'elle n'a pas est pire qu'une fiche qui
  déclare un trou*.

- ⚠️ **[F-TYPE-5] — une sonde de type peut mentir DANS LE SENS DANGEREUX, et deux façons ont été
  mesurées.** Trouvées par le relecteur de T3.31 sur les sondes de `ReolinkRegistry_test.cpp`,
  toutes deux rendant **« fermé » sur un type grand ouvert** :
  **(a) référence lvalue** — `std::declval<std::string>()` produit un **rvalue**, donc un
  constructeur prenant `std::string &` fait répondre **`false` aux deux sondes** alors que
  `std::string h, u, p, e; LieRef bad(h, p, u, e);` **compile** et met le mot de passe dans
  `username`. La sonde ne dit rien des arguments lvalue tant qu'on ne l'interroge pas avec
  `string &`.
  **(b) rétrécissement** — `isBraceInitializable<T, int>` vaut **0** pour un constructeur
  **non-`explicit`** `T(unsigned short)`, non pas grâce au garde mais parce que les accolades
  refusent `int → unsigned short`. Lu comme « fermé », il masque **W1 grand ouvert** : `T obj(i)`
  compile. ⚠️ Symétriquement, `is_constructible` ne discrimine pas non plus — un constructeur
  `explicit` reste **directement** appelable. ⇒ **la seule question qui vaut pour une enveloppe
  numérique est la copy-initialisation** (`is_convertible_v` / `is_invocable_v`), parce que c'est
  celle qu'un site d'appel pose.
  ⇒ **Règle** : **écrire les angles morts de la sonde dans le fichier**. *Une sonde dont les
  angles morts sont écrits vaut mieux qu'une sonde qu'on croit complète.* Les deux témoins sont
  exécutables dans `TheAggregateProbesActuallyDiscriminate`.

- ⚠️ **[F-FLAKY-1] — `core/ShutterImpulse_test` a une course d'HORLOGE MURALE, et elle rougit sous
  contention CPU.** Observé en revalidant T3.31 : `make check -j8` pendant que trois autres agents
  buildaient ⇒ **`# FAIL: 1`**, `ShutterImpulse_test.cpp:384`,
  `PlainImpulseDownWithoutImpulseTimeStillHonoursTheDuration`, `Actual: true / Expected: false`.
  Le cas pompe la boucle **juste en deçà** de l'échéance d'impulsion
  (`pumpLoopFor(stillMovingProbeMs(kPlainDownMs, 0))`) puis exige `EXPECT_FALSE(sh.isStopped())` :
  si l'ordonnanceur vole assez de temps, la boucle **dépasse** l'échéance et le volet s'arrête
  **avant** l'assertion.
  ⭐ **Mesuré comme flottant, pas supposé** : (a) le même arbre a rendu **93/93** deux fois avant
  et une fois après ; (b) le cas seul passe **10/10** en isolation ; (c) `nm` sur
  `tests/core/ShutterImpulse_test` trouve **0 symbole** de ce qui était en cours de modification,
  sur **6938** — le lien de causalité est exclu, pas seulement jugé improbable.
  ⇒ **Piège de diagnostic** : c'est un **faux ROUGE** dépendant de la charge, et il tombe sur un
  fichier que le ticket courant ne touche pas — la réaction réflexe (« ma modification a cassé
  quelque chose ») est fausse. **Vérifier par `nm` et par la répétition en isolation avant de
  conclure**, et **ne jamais relancer un `make check` en aveugle** pour faire disparaître un
  rouge sans l'avoir expliqué.
  ⚠️ **Non corrigé ici** (fichier hors périmètre de T3.31) : à traiter par une échéance relative
  au temps simulé plutôt qu'à l'horloge, ou par une marge. Voir aussi la règle de parallélisme :
  `make check -j8` quand 3-4 agents buildent, `-j16` seulement en solo.
## F-PYTEST-1 — la huitième variante de faux vert : des tests qui ne s'exécutent pas (2026-08-25)
## ⭐ LA LISTE CANONIQUE DES VARIANTES DE FAUX VERT / FAUX ROUGE — **onze**, numérotées

*(Établie le 2026-08-25 en fermant les réserves de la 2ᵉ revue de [T3.44](T3.44.md). ⚠️ **C'est LA
référence** : toute mention d'un compte ou d'un rang ailleurs dans `docs/refactoring/` doit
s'y accorder. Avant elle, trois numérotations incompatibles coexistaient — « cinq » (la seule
famille `_DEPENDENCIES`), « sixième » (revendiqué par **deux** findings différents — `F-HARN-1` et
`F-TYPE-3`, arbitré ci-dessous), et un compte
global « septième / huitième / neuvième » qui **n'avait jamais énuméré `F-BUILD-1` ni
`F-TYPE-3`**.)*

⚠️ **Le fil commun des onze** : *l'arbre a l'air juste à l'endroit qu'on regarde.*

| n° | variante | id | où c'est mesuré |
|---|---|---|---|
| **1** | **Faux ROUGE uniforme** — le binaire non relié rejoue la mutation précédente, toutes les passes rougissent sur le même cas | — (E4.1k) | *Le piège `_DEPENDENCIES`, variante faux ROUGE UNIFORME*, plus haut |
| **2** | **Faux VERT de relink** — le `.o` muté est recompilé, le binaire de test n'est **pas** relié : la mutation n'est jamais exercée | — (E4.1e) ; voir aussi `F-RELINK-T337` | *E4.1e — variante « faux VERT » du piège `_DEPENDENCIES`* |
| **3** | **Faux ROUGE après rebase** — le `.o` d'avant le rebase survit ; le binaire teste une garde contre un objet qui ne l'a pas | — (E4.0b) | ⚠️ **hors `FINDINGS.md`** : `ORCHESTRATION.md` ; voisin mesuré ici : `F-TEST-2` |
| **4** | **Faux VERT total** — `check_PROGRAMS` n'est pas construit par un `make` nu : le binaire n'existe plus, **0 rouge, témoin compris** | — (E4.1g) | *E4.1e — le piège `_DEPENDENCIES` reste ARMÉ pour le prochain* |
| **5** | **Faux VERT par mort du binaire** — le test avorte (`throw`/segfault), **aucune** ligne `FAILED` n'est émise, un harnais qui compte les rouges lit **0** | — (E4.1g) | idem, § « un `dump()` levant peut se lire *0 rouge* » |
| **6** | **Mutation jamais appliquée** — motif de remplacement faux **et** harnais sans `set -e` ⇒ le cas tourne **NON MUTÉ**, vert parfait | **F-HARN-1** | *LA SIXIÈME VARIANTE… vient du HARNAIS* ; falsifiée en revue (`LAX_M2`) |
| **7** | **Lecture de non-initialisé** — un oracle qui attend `0` sur un chemin non initialisé est **vide par construction** ⚠️ **phénomène, pas règle** | — | [T3.34](T3.34.md) §  « La 7ᵉ variante » ; *Le motif de la pile n'est pas une règle*, plus haut |
| **8** | **Tests qui ne s'exécutent pas** — repli `unittest discover -p 'test_t116_*.py'`, 3 suites sur 6 ramassées, sortie **0** ⇒ automake écrit **`PASS`** | **F-PYTEST-1** | section suivante ; [T3.44](T3.44.md) |
| **9** | **Deux `Testsuite summary`** — le premier bloc est un faux vert, et **les deux** comptent un test de moins que l'arbre n'en déclare | **F-BUILD-1** | *`make check` a imprimé **deux** `Testsuite summary`*, plus haut |
| **10** | **En-tête template au `-fsyntax-only`** — le corps vit dans un membre d'un template **jamais instancié**, `rc=0` : mutation « compilée verte » sans rien vérifier | **F-TYPE-3** | *`[F-TYPE-3]` — un en-tête TEMPLATE ne se vérifie pas au `-fsyntax-only`*, plus haut (mergée avec `T3.31`) |
| **11** | **Restauration pristine par `copy2`** — la date préservée rend la source **plus ancienne** que le `.o` muté : `make` ne recompile rien | — | *Neuvième variante de faux vert/rouge*, plus haut |

⚠️ **Deux voisins qui ne sont PAS des variantes de plus** — les recompter serait une douzième et une
treizième imaginaires : **`F-TEST-2`** (le non-relink étendu au faux **rouge**) et
**`F-RELINK-T337`** (faux rouge reproduit sur source propre) sont deux **instances** de la
famille `_DEPENDENCIES`, n° 1 à 5.

⚠️ **Une dette de numérotation reste, et une a été payée** :
1. ✅ **PAYÉE — la n° 10 (`F-TYPE-3`)**. Cette liste a été écrite alors que `F-TYPE-3` vivait
   encore sur `.wave59/t3.31`, où elle se présentait comme « une **sixième** » — **collision de rang
   avec `F-HARN-1`**. `T3.31` ayant été mergée depuis, l'entrée est **dans cet arbre** et le mot
   « sixième » en a été retiré : c'est la **n° 10**. ⚠️ **C'est le seul mécanisme qui a marché** :
   la dette était écrite ici, avec l'instruction, et le ticket suivant l'a exécutée.
2. ⚠️ **RESTE — la n° 3 n'a jamais eu de section dans `FINDINGS.md`** ; son seul écrit est dans
   `ORCHESTRATION.md`. Elle est comptée ici parce qu'elle a été **rencontrée et mesurée**, pas
   parce qu'elle est fichée. ⇒ **à ficher par le prochain ticket qui la rencontre**, pas à
   reconstituer de mémoire.

⚠️ **« Cause racine des CINQ variantes » reste juste** partout où c'est écrit de `_DEPENDENCIES` :
`T3.36` ferme les n° 1 à 5, **et elles seules**. Les six autres ont chacune une cause racine et un
remède propres. C'est la faute **M-7** du catalogue des fautes de méthode : *un remède correct
généralisé à une famille à laquelle il n'appartient pas.*

## F-PYTEST-1 — la **8ᵉ** des **onze** variantes de faux vert (liste canonique ci-dessus) : des tests qui ne s'exécutent pas (2026-08-25)

*(Trouvée par la revue de [T3.39](T3.39.md) en mesurant F-MCP-XFF-1 ; **fermée par
[T3.44](T3.44.md)**, branche `fix/fpytest1`. Cette section est le versement de ce qui a été
**mesuré** en la fermant, pas la redite du finding.)*

### ⭐ Le mécanisme exact — et pourquoi le suspect évident est presque juste

`tests/run-python-tests.sh:50-60` (état `b7a4c63d`) lançait `pytest` s'il était importable et
retombait sinon sur `python3 -m unittest discover -p 'test_t116_*.py'`. Le motif ramasse **3** des
**6** suites de `tests/python/` ; les trois autres disparaissent.

⚠️ **Il n'y a PAS d'`exit 0` explicite sur `pytest` absent.** Le `0` propagé par l'`exec` est celui
d'`unittest discover`, qui a **vraiment réussi** — sur un sous-ensemble que personne n'avait
déclaré. Automake lit `0 = PASS` et écrit `:test-result: PASS` dans le `.trs`. ⭐ **Ce n'est donc
même pas un `SKIP`** : un `SKIP` (77) est compté dans la colonne **visible** `# SKIP:`. C'est
pourquoi **aucun** garde-fou des **dix autres** variantes ne le voit — ni `rm -f` + `CXXLD`, ni `cmp`
d'application, ni la comparaison des ensembles : **tout ce qui a tourné est vert ; ce qui manque,
c'est ce qui n'a pas tourné.**

### Le périmètre, **reproduit** (`python3`/`ast`, `conftest.py` exclu)

| | fichiers | cas |
|---|---|---|
| déclarés dans `tests/python/` | **6** | **42** |
| ramassés par `test_t116_*.py` | 3 | 23 |
| ⚠️ **non exécutés, suite verte** | **3** | **19** |

`unittest discover -p 'test_t116_*.py'` rend `Ran 23 tests … OK` : les 23 sont confirmées **par
exécution**. **50 % des fichiers, 45 % des cas.** Les chiffres du finding sont exacts.

### ⭐ Ce que le finding ne voyait pas : installer `pytest` **ne suffirait pas**

`test_auth.py` fait `pytest.importorskip("fastapi")` et `("httpx")` ; `test_extern_proc.py` et
`test_logger.py` font `importorskip("colorama")`. **Aucun** de ces quatre modules n'est dans l'image
de build (mesuré). Avec `pytest` seul, les 19 cas seraient **`skipped` par pytest**, pytest
sortirait **0**, et le `PASS` silencieux reviendrait **une couche plus bas**.
⇒ **Le remède doit compter les CAS, pas les dépendances.**

### ⭐ Le balayage de TOUTES les entrées `TESTS` — la question que personne n'avait posée

**94 entrées** sur `master` `df2851d0` (recomptées `python3` à la livraison, continuations
recollées, `if`/`else`/`endif` empilés, zéro doublon) : **4 scripts shell** + **89 binaires sous
`if HAVE_GTEST`** + ⭐ **1 binaire imbriqué `if HAVE_GTEST` → `if HAVE_LIBKNX`**
(`KNXExternProcWire_test`). La branche en ajoute une, l'oracle : **95**.
⚠️ **Ces totaux montent à chaque merge de la série** (`89`/`90` sur `701a98e4`, `93`/`94` sur
`55beb79b`, `94`/`95` sur `df2851d0`) : **les recompter, jamais les recopier.** Ce qui ne bouge pas :
**une seule imbrication** dans tout l'arbre.

| entrée `TESTS` | peut rendre `PASS` sans exécuter ? | mesure |
|---|---|---|
| `run-python-tests.sh` | ⛔ **OUI, totalement** — 3/6 fichiers, 19/42 cas, sortie 0, `PASS` | mesuré ; **corrigé par T3.44** |
| `check-config-options.sh` | ✅ **NON** — chaque scan optionnel porte un `else fail "… scan rule X is dead"`, plus une garde d'anti-vacuité `nb_used == 0` (« *this test would pass whatever the code does* ») | lu + exécuté |
| `check-config-docs.sh` | ✅ **NON** — binaire absent / document absent / générateur muet ⇒ `exit 1` | lu + exécuté |
| **les 90 binaires, en bloc** | ⛔ **OUI, autrement** — tout le bloc est `if HAVE_GTEST` ; sans l'en-tête gtest les 90 **quittent `TESTS`**, le total tombe de 95 à **5**, le résumé affiche toujours `# FAIL: 0`. Seul `GTEST_INFO` au `configure` le dit | statique |
| ⭐ `KNXExternProcWire_test` | ⛔ **OUI, d'une troisième façon** — imbriqué `if HAVE_GTEST` → `if HAVE_LIBKNX` ; sans `libknx` **l'entrée n'existe plus**, pas même en `SKIP` : rien ne l'imprime ET rien ne la compte | **mesuré** : `# TOTAL: 95` (image de dev) vs **94** (image CI) |
| `UrlDownloader_test` | ⚠️ **OUI, partiellement** — ⭐ **7** cas derrière `REQUIRE_CURL()` (le 8ᵉ match du balayage est la ligne `#define`, `UrlDownloader_test.cpp:64`) ; **sans `curl` dans le `PATH` : `ran=10, skipped=7, passed=3`, sortie 0, `PASS`** | **mesuré** (`PATH=/nocurl`) |
| `core/CalaosConfigRobustness_test` | ⚠️ **OUI, partiellement** — `GTEST_SKIP` si `geteuid() == 0`, donc **dans tout conteneur root**, y compris chaque build de cette série | **mesuré : 1/12 sauté, `PASS`** |
| `Utils_config_test`, `ConfigModel_test` | ⚠️ 1 cas chacun si la descente de privilèges échoue | mesuré : **0** sauté |
| ⭐ `TimeRangeCalendar_test` | ⚠️ **11** cas : 1 (`tzdata` absent) **+ 10** — le 2ᵉ `GTEST_SKIP` est dans le **helper de fixture** `evalInStableWindow`, appelé par 10 `TEST_F` | mesuré : **0** sauté ; ⚠️ la fiche écrivait « 2 cas », en comptant les `GTEST_SKIP` et non les cas **atteignables** |
| `core/JsonApiCharacterization_test` | ⚠️ 3 cas en mode mise à jour des goldens | mesuré : **0** sauté |
| les **80** autres binaires | aucun saut conditionnel à l'environnement, **et zéro `DISABLED_` dans tout le dépôt** | balayage `python3` |

**Compte par binaire, RECOMPTÉ à la 2ᵉ revue** (balayage `python3` des ⭐ **89** `.cpp` de `tests/` —
**86 `*_test.cpp` + 3 auxiliaires** : `StaticLogShutdown_helper.cpp`, `core/CalaosCoreFixture.cpp`,
`core/JsonApiCharacterization.cpp` ; motifs `GTEST_SKIP(` et `TEST*(…, DISABLED_…`) :
**6 binaires sur 86** portent au moins un saut conditionnel, ⭐ **24 cas atteignables** au total
(`TimeRangeCalendar_test` **11**, `UrlDownloader_test` 7, `core/JsonApiCharacterization_test` 3,
`ConfigModel_test` 1, `Utils_config_test` 1, `core/CalaosConfigRobustness_test` 1) ; **80 binaires
propres**, **0 `DISABLED_`**.
⚠️ **Trois chiffres de la première rédaction étaient faux** : « 87 `.cpp` » (les **3 auxiliaires**
étaient oubliés), « `TimeRangeCalendar_test` : 2 cas » (c'est **11** : le 2ᵉ `GTEST_SKIP` est dans
le **helper de fixture** `evalInStableWindow`, appelé par **10** `TEST_F` — ⚠️ *compter les
`GTEST_SKIP` n'est pas compter les cas qu'ils sautent*), et donc « 15 cas » (c'est **24**).
**Agrégat mesuré sur le `make check` de livraison (image de dev, conteneur root, arbre rebasé sur
`df2851d0`) : ⭐ 90 binaires, 1595 cas exécutés / 1594 passés, 1 seul silencieusement sauté**
(`core/CalaosConfigRobustness_test`, `geteuid() == 0`) ⇒ ⭐ **plafond de silence 24 / 1595, et non
15 / 1527**.
⚠️ Le relevé de la 2ᵉ revue, fait sur `1c6ab7a9`, donnait **89 `.cpp` / 86 binaires / 1533-1532** :
**mêmes corrections, autre arbre** — `master` a mergé `T3.25`, `T3.45` et `T3.48` entre-temps. ⚠️ **Nuance** : un `GTEST_SKIP` **imprime** `[  SKIPPED ]` et gtest le compte — il est *à
moitié* visible. Le défaut de `run-python-tests.sh` était d'un cran pire : la suite entière
manquait, comptée nulle part.

### ⭐ `pytest` dans l'image ? en CI ? — **l'hypothèse du rapporteur est fausse dans les deux sens**

| | `python3` | `pytest` | `fastapi`/`httpx`/`colorama` | `curl` | gtest | verdict |
|---|---|---|---|---|---|---|
| image de build locale | 3.11.2 | **non** | **non** | oui | oui | avant **`PASS` sur 23/42**, après **`SKIP`** |
| CI (`debian:12` + liste `apt` de `ci.yml`) | ⛔ **absent** | absent | absent | oui | oui | **77 = `SKIP`**, déjà honnête |

La CI n'a pas `pytest` : elle n'a **aucun `python3`** (rejoué : `apt-get install` de la liste exacte
de `.github/workflows/ci.yml` dans `debian:12`, puis `command -v python3` ⇒ introuvable).
`AM_PATH_PYTHON` pose `PYTHON=:`, le script sort 77. ⇒ **notre image n'est pas « incomplète par
rapport à la CI » ; la CI est plus vide encore, et son silence est du bon type.**

⭐ **Corollaire, plus lourd que le défaut initial et NON corrigé : les 42 cas Python ne tournent sur
AUCUNE machine de CI.** `test_auth.py` — les 11 cas du throttle MCP, le filet de la fiche `F-MCP-XFF-1`
ouverte par la revue de [T3.39](T3.39.md) (numérotée `T3.42` dans `.wave54/t3.39`, non mergée —
⚠️ vérifier son numéro au merge, la série en a renuméroté une cette nuit) — n'a jamais été exécuté
par un `push`. Le remède tient en une ligne d'`apt` dans
`ci.yml`, **propriété de `T0.1`** : à ticketer à part.

### Le remède retenu, et les deux écartés

**`SKIP` automake réel (`exit 77`) + comptabilité publiée**, pas l'échec franc :
- **échec franc** ⇒ casse le build de quiconque n'a pas `pytest`, **nous compris**, et serait
  contourné dans l'heure — on aurait troqué un faux vert contre un `TESTS` amputé, le même silence
  sous un autre nom ;
- **dépendance obligatoire dans `configure.ac`** ⇒ déplace l'échec vers la configuration, pour une
  dépendance **de test seulement** ; le dépôt ne le fait pour **aucune** dépendance optionnelle
  (gtest compris : sans lui `make check` reste vert avec 3 tests).

`tests/python-suite-runner.py` compte les cas **déclarés** (`ast`, plus aucun motif de nom de
fichier — **le glob était le mécanisme du défaut**), compte les **exécutés**, publie
`run-python-tests: suites=N/M cases=N/M`, et sort **1 (rouge) / 77 (un cas non exécuté) / 0 (tout
exécuté)**. `configure.ac` gagne `PYTEST_INFO`, **de la forme de `GTEST_INFO`**. ⇒ **on ne peut plus
lire `88/88` sans que la colonne `# SKIP:` dise que le Python n'a pas été mesuré.**

### ⭐ La forme d'oracle que cette famille exige

`tests/check-python-tests-reporting.sh` n'exerce pas le produit : il exerce **l'honnêteté de rapport
d'une autre entrée de `TESTS`**. ⚠️ **Le piège à éviter était un oracle qui passerait aussi bien la
suite tournant que ne tournant pas.** Il est désamorcé en **fabriquant** le monde « la suite ne
tourne pas » plutôt qu'en l'attendant : un module fantôme `pytest.py` qui lève `ImportError` pousse
une machine avec `pytest` et une machine sans dans **le même état**. L'oracle a été joué **dans les
deux mondes** — sans deps : `suites=3/6 cases=23/42` → **77** ; avec `pytest fastapi httpx colorama`
: `6/6`, `42/42` → **0** — **vert dans les deux**, parce qu'il asserte le **rapport**, pas la
dépendance ; et rouge dès que le rapport ment (`C1`, `C2` rouges sur la caractérisation).

⚠️ **Conséquence pour tous les briefs, précisée** : à côté de « le binaire a-t-il été relié ? » et
« la mutation a-t-elle été appliquée ? », il faut poser **« combien de cas ont réellement tourné, et
est-ce le nombre attendu ? »** — et le nombre attendu doit être **lu dans les sources**, jamais dans
ce que le lanceur a bien voulu ramasser.

### ℹ️ `F-BUILD-1` — non reproduite

Premier `make check` d'un worktree neuf (`.wave58/fpytest1`, `./autogen.sh && ./configure &&
make -j12 && make check -j6`, un seul passage) : **un seul** bloc `Testsuite summary` dans tout le
journal (compté `python3`), `# TOTAL: 89 / # PASS: 88 / # SKIP: 1 / # FAIL: 0`, sortie **0**. Le
double bloc n'est **pas** apparu ici. ⇒ point de mesure négatif, la variante reste **ouverte et non
reproduite** ; le remède provisoire (juger au code de sortie, ou au DERNIER bloc) reste de mise.

### ⭐ Ce que la REVUE a mesuré et que le premier correctif ratait (2026-08-25, `RETOUR À L'AUTEUR`)

Trois choses, toutes rejouées et fermées ; le détail est dans [`T3.44`](T3.44.md) §7–§8.

1. ⛔ **Un méta-oracle qui ne peut pas mesurer ne doit PAS rougir.** La première version comptait les
   cas déclarés avec le `python3` **ambiant** et sortait **1** quand il n'y en avait pas — c'est le
   cas **de la CI**, dont la liste `apt` n'installe aucun `python3`. Résultat mesuré dans
   `debian:12` + liste exacte de `ci.yml` : `# TOTAL: 88 · FAIL: 1`, `make check` **RC 2**,
   `build-and-test` rouge **à chaque `push`**. ⭐ **C'est « l'échec franc » que le ticket écarte
   explicitement, réintroduit par la porte du harnais.** Règle générale : *un harnais qui ne peut
   pas mesurer n'a rien trouvé* ⇒ **77**, jamais 1 ; et les cas qui n'ont besoin de rien (ici les
   deux témoins, `/bin/sh` seul) se jouent **d'abord**.
2. ⭐ **Compter n'est pas mesurer : il faut comparer des NOMS.** Le relecteur a posé dans la fixture
   un fichier = 1 cas `@pytest.mark.parametrize`(×3) + 1 cas `@pytest.mark.skip`. `pytest` imprimait
   `45 passed, 1 skipped` et le lanceur publiait **`cases=44/44`, « every declared case executed »,
   sortie 0** : le `got >= want` **par fichier** laissait **3 instances d'un cas payer pour un cas
   jamais exécuté**. ⚠️ **Le risque était écrit dans la fiche comme « futur » : il était présent.**
   ⇒ intersecter les **ensembles de noms** (`<testcase name=…>` du junit, suffixe `[…]` retiré,
   contre les noms pointés lus à l'`ast`), jamais des cardinaux.
3. ⭐ **La « seconde couche » d'un défaut doit être épinglée par son PROPRE cas.** L'invariant vendu
   en tête du lanceur — *un cas sauté de l'INTÉRIEUR compte comme NON exécuté* — n'était épinglé par
   **aucun** cas : deux mutations (`if skipped: … continue` supprimé ; `skipped` reclassé en
   `executed`) laissaient l'oracle **vert**. Le remède est un cas qui **fabrique l'arbre source
   entier** — deux cas déclarés dont un qui ne peut pas tourner — et l'exige **sur les deux
   back-ends**. Campagne rejouée : **6/6 mutants tués dans le monde `pytest`, zéro survivant**,
   témoin à ensemble rouge **VIDE**.

⚠️ **Deux fragilités d'oracle mesurées au passage, à retenir pour tout méta-oracle** :
- **le faux rouge latent** — le cas « `pytest` caché » épinglait **en dur** que la fixture garde des
  suites *pytest-only* ; **porter `tests/python/` en `unittest`, un progrès souhaitable, cassait
  `make check`**. Un oracle d'honnêteté ne doit asserter que ce qui est vrai dans **tous** les
  mondes (ici : la ligne existe, elle est cohérente, et **retirer une dépendance ne peut pas faire
  tourner PLUS de cas**) ;
- **l'angle mort du motif** — un fichier **`*_test.py`** sous `tests/python/` est ramassé par
  `pytest` (moitié de son `python_files` par défaut) mais n'était **pas déclaré** par le compteur
  `ast` : suite entière ni exécutée ni comptée, **aucune note**. **Exactement la famille du défaut
  corrigé.** Les deux motifs sont désormais déclarés, et un cas rapporté hors déclaration est
  imprimé `UNDECLARED: …`.

⚠️ **Le gain CI annoncé n'existait pas.** `PYTHON=:` court-circuite **avant** le runner : la ligne
`suites=N/M cases=N/M` **n'est jamais imprimée en CI**, le `SKIP: run-python-tests.sh` **y était
déjà** avant le correctif, et l'étape qui dumpe les `.log` est `if: failure()`. **Seul gain vérifié
en CI : la ligne `PYTEST_INFO` au `configure`.** ⇒ dit franchement dans la fiche plutôt que
revendiqué ; la visibilité CI est laissée à un ticket dédié.

### ℹ️ `make dist` cassé — **renvoi vers [`T3.45`](T3.45.md)**, pas une entrée autonome

⚠️ **Le défaut appartient à [`T3.45`](T3.45.md)** (livrée) : **pas d'entrée `FINDINGS` propre ici** —
deux entrées sur le même défaut se percutent au merge, comme `F-LINK-1` écrite deux fois cette nuit.
⭐ **Et `T3.45` va bien plus loin que la faute de chemin** : `distcheck` y révèle que **l'archive est
inconstructible — 414 fichiers suivis absents du tarball, dont 125 sources/en-têtes** (arbres
vendorés entiers). **Lire `T3.45`, pas ce paragraphe.**

**Corroboration indépendante, versée seulement parce qu'elle vient d'un autre chemin** : en cherchant
un substitut à `make distcheck` pour vérifier `run-python-tests.sh` en **`srcdir` lecture seule**,
`src/lib/calaos-python/Makefile.am` est apparu déclarant `EXTRA_DIST = python/calaos_extern_proc/*`
alors qu'il n'existe **aucun** répertoire `python/` sous `src/lib/calaos-python/` (les fichiers sont
en `calaos_extern_proc/*` ; le `calaospython_PYTHON` juste au-dessus, lui, est correct) — régression
de **`06d799fe`**. ⇒ **substitut joué faute de `distcheck`** : `srcdir` monté **en lecture seule**,
deux mondes ⇒ **77** et **0**, **zéro résidu** dans l'arbre source.

### ⚠️ Deux compromis ASSUMÉS de ce harnais, écrits pour être relus (2026-08-25)

1. ⭐ **Un cas de test fabriqué peut être le seul qui tue un mutant réel.** `C5d`/`C5e` renomment une
   méthode à l'import — forme que `tests/python/` ne pratique pas et ne devrait pas pratiquer.
   Ils restent parce que la mesure l'exige : sans eux, la mutation qui **remet la comparaison par
   compte** (`got >= want` par fichier, la régression même de la revue) **survivait à toute la
   campagne**. Une fois le suffixe `[…]` de paramétrisation retiré en amont, trois instances d'un
   cas se replient sur un nom et les comptes cessent de mentir seuls ; le seul monde où ils mentent
   encore est celui-là. ⇒ **règle** : un cas fabriqué qui tue un mutant réel vaut mieux qu'un cas
   d'allure naturelle qui ne tue rien — **mais la raison s'écrit DANS le fichier**, sinon le
   relecteur suivant le prend pour un caprice et le « simplifie ».
2. ⚠️ **Un fichier de test non déclaré ne fait PAS échouer le build — résidu volontaire.** Quand un
   back-end rapporte un cas inconnu du déclarateur, `python-suite-runner.py` imprime
   `UNDECLARED: … (note, does not fail the suite)` et **ne change pas son code de sortie**.
   ⇒ **quelqu'un peut ajouter un fichier de test sous un nom qu'aucun des deux motifs
   `python_files` ne ramasse, et ne jamais le voir tourner, le build restant vert.** C'est **la
   famille même du défaut fermé ici**, laissée ouverte d'un cran : en faire un échec dur casserait
   `make check` chez tout le monde au premier caprice de collecte d'un `pytest` futur — l'« échec
   franc » que `DECISIONS.md` écarte. **La note est le milieu honnête** : le fait est imprimé, au
   même endroit que la comptabilité. Le changement est d'**une ligne** — et demande **un arbitrage,
   pas un patch**.
   ⭐ ⚠️ **NUANCE MESURÉE À LA 2ᵉ REVUE — « la note est le milieu honnête » est vrai à 90 %, pas à
   100 %.** La note n'est imprimée **que si un back-end RAPPORTE** un fichier ou un nom que la
   déclaration ne connaît pas. **Une suite que PERSONNE ne collecte n'est rapportée par personne** :
   rien n'est imprimé du tout, et le silence est **total**. La note couvre « collecté mais non
   déclaré » ; elle ne couvre pas « collecté par personne ».
3. ⚠️ ⭐ **Second résidu, en sens INVERSE : une classe *mixin* produit un FAUX SKIP.** Le parcours
   `ast` déclare les méthodes en `test*` de **toute** classe, y compris une classe de base que
   **ni** back-end ne collecte (`pytest` collecte les classes nommées `Test*`, `unittest` les
   sous-classes de `TestCase` ; un mixin n'est ni l'un ni l'autre). Ses méthodes sont alors
   déclarées et jamais exécutées ⇒ `cases=0/N` et **77**. ⚠️ **C'est un faux `SKIP`, pas un faux
   `PASS`** : il est bruyant, il nomme les cas manquants sur la ligne `NOT RUN`, et il se trompe du
   **bon côté**. Restreindre le parcours aux classes « d'allure collectable » échangerait cette
   erreur bruyante et sûre contre une erreur silencieuse et fausse. ⇒ **laissé tel quel,
   délibérément, et écrit ici pour ne pas être redécouvert comme un défaut.**

### ⛔ ⭐ 2ᵉ REVUE — **le correctif de F-PYTEST-1 contenait le défaut même qu'il corrige** (2026-08-25)

⚠️ **À lire avant tout autre paragraphe de cette section.** La 2ᵉ revue a rendu *MERGE SOUS RÉSERVE*
en confirmant les trois levées précédentes **par ses propres mesures** — et en trouvant, **dans le
correctif lui-même**, une **nouvelle instance de la variante n° 8** (ce n'est **pas** une douzième
variante : même mécanisme, même remède, autre victime) : *des tests qui ne s'exécutent pas, sur un
vert parfait.*

**Le mécanisme, reproduit sur l'arbre RÉEL** (image de dev + `python3-pytest python3-fastapi
python3-httpx python3-colorama`, arbre complet) : marquer **un** cas de `tests/python/test_logger.py`
`@pytest.mark.skip`, et poser à côté un paquet `tests/python/regress/` (`__init__.py`) contenant un
`test_logger.py` **de même basename** déclarant un cas **du même nom pointé**. Résultat mesuré :

```
42 passed, 1 skipped
run-python-tests: suites=6/6 cases=42/42
run-python-tests: PASS, every declared case executed        LAUNCHER_RC=0
```

**Ni `NOT RUN`, ni `UNDECLARED`, ni `SKIP`.** Un cas déclaré n'a pas tourné et le lanceur a écrit
`PASS`. ⭐ **C'est exactement le défaut que le ticket ferme, commis à l'intérieur du correctif.**

**La cause, mesurée maillon par maillon** :
1. la **déclaration** lisait un `os.listdir` **PLAT** ⇒ `regress/test_logger.py` n'existait pour
   personne ;
2. l'**exécution** classait les cas par **`os.path.basename`** ⇒ les deux fichiers tombaient sur
   **une seule clé** et le cas qui tourne payait pour le cas sauté ;
3. ⭐ **et la famille junit par défaut n'écrit même pas `file=`** : mesuré sur **pytest 7.2.1 ET
   9.1.1**, `xunit2` (défaut depuis pytest 6) omet `file=` et `line=`. Le chemin qui tournait
   vraiment n'était donc pas `basename(file)` mais le repli qui prenait la **première** composante
   « module » du `classname` et lui collait `.py`. ⚠️ **Le premier correctif de cette réserve a
   corrigé la mauvaise moitié** : le mutant qui remettait la clé par basename dans le lecteur de
   `file=` **a survécu à toute la campagne**, faute d'être atteignable.

⭐ **Pourquoi rien ne l'avait vu, et c'est la leçon transportable** : les **cinq** arbres fabriqués
de l'oracle étaient **TOUS PLATS**. *Un oracle dont toutes les fixtures ont un seul répertoire ne
peut pas rougir sur une collision de chemin.* ⇒ **règle : quand une comptabilité est indexée par un
chemin, au moins une fixture doit être NON PLATE.**

⚠️ **Et la fixture non plate doit être ASYMÉTRIQUE** : la première version (2 cas + 1 cas) donnait,
sous la clé par basename, **exactement les deux mêmes nombres** que la comptabilité honnête, et le
mutant survivait encore. Rendue asymétrique (2 + 2, dont un cas que **seul** le sous-répertoire
déclare), honnête = **3/4** et clé par basename = **2/4**. ⇒ **une fixture de collision doit être
construite pour que les DEUX comptabilités divergent numériquement, pas seulement conceptuellement.**

### ⭐ Trois autres choses que la 2ᵉ revue a mesurées et qui valent hors de ce ticket

1. ⛔ **La moitié d'une ligne publiée n'était assertée nulle part.** L'oracle vérifiait les
   dénominateurs et le numérateur `cases=`, **jamais** le numérateur `suites=`. Mutation : rendre
   `ran_files += 1` inconditionnel ⇒ le lanceur publie **`suites=6/6 cases=23/42`** — ligne
   **auto-contradictoire**, toutes les suites complètes et la moitié des cas manquants — et
   **l'oracle reste VERT**. ⇒ **règle : tout champ d'une ligne machine publiée doit être asserté,
   ou il ne veut rien dire ; et une ligne à plusieurs champs doit avoir un invariant de COHÉRENCE
   entre eux.**
2. ⛔ **`xfail` : les deux back-ends se contredisaient, et le silence était du mauvais côté.** Un
   `@unittest.expectedFailure` ordinaire ⇒ back-end **pytest** : `cases=42/43`, **RC 77** (le junit
   le classe `<skipped type="pytest.xfail">`) ; back-end **unittest** : `43/43`, **RC 0**
   (`addExpectedFailure` le compte exécuté). ⭐ **Le back-end `unittest` a raison** : un `xfail` a
   **tourné**, son corps a levé, c'est son issue attendue. ⇒ **un seul `xfail` légitime sous
   `tests/python/` aurait figé `make check` en `SKIP` PERPÉTUEL sur toute machine ayant `pytest`.**
   Seule exception, gardée : `xfail(run=False)`, que `pytest` préfixe `[NOTRUN]` — celui-là n'a
   vraiment pas tourné.
3. ⚠️ **Un « angle mort fermé » qu'aucun cas n'épingle n'est pas fermé.** La 1ʳᵉ rédaction
   revendiquait la fermeture de l'angle mort `*_test.py` ; la mutation qui **retire ce motif** du
   déclarateur **survivait dans les deux mondes**. ⇒ **règle : une revendication de clôture se
   présente avec le mutant qu'elle tue, ou elle se retire.**

### ⭐ Le fait qui remet le reste en perspective — `run_with_pytest` est du CODE MORT aujourd'hui

**Mesuré** : **aucune** des deux images du projet n'a `pytest` (image de dev : `ModuleNotFoundError` ;
CI `debian:12` + liste `apt` de `ci.yml` : **aucun `python3` du tout**). ⇒ tout le back-end `pytest`
du lanceur — c'est-à-dire l'endroit où vivent les trois réserves ci-dessus — **ne s'exécute nulle
part dans ce dépôt**. Il devient **le seul chemin** le jour où [`T3.47`](T3.47.md) installe les
paquets.
⇒ **C'est pourquoi ces réserves sont bloquantes pour `T3.47` et non pour le merge de `T3.44`** : elles
sont la **dette d'entrée** de qui prendra `T3.47`, écrite en tête de sa fiche.
⚠️ **Corollaire de méthode, général** : *un correctif dont le chemin principal n'est exécuté par
aucune machine du dépôt n'est pas « vérifié parce que la suite est verte ».* Il n'est vérifié que par
ce qui **fabrique** ce chemin — ici l'oracle et sa campagne de mutation.

---

## E4.1l — `CalaosEvent::toJson()` : ce que la bascule a mesuré (2026-08-25)

### ⭐ Le troisième wire : `EventManager.cpp:79` n'avait pas d'ORACLE — et la première rédaction de ce finding disait « pas exercé », ce qui est FAUX

`CalaosEvent::toJson()` a **trois** consommateurs qui sérialisent, pas deux : le push WS, la
réponse HTTP `poll_listen`, et **la ligne d'historique** — `EventManager::appendEvent()` dumpe
l'event dans `HistEvent::event_raw` et le donne à `HistLogger`, **synchroniquement, au moment de
la mise en file**, pas sur l'idler.

⛔ **L'affirmation renversée, et comment.** La première livraison écrivait ici : *« Mesuré : aucun
cas de l'arbre ne l'exécutait »*. Elle n'était pas mesurée, elle était **raisonnée** — trois
maillons tous **exacts** individuellement : (a) `tests/core/CalaosCoreFixture.h:106` dit bien
*« HistLogger/DataLogger are only reachable for IOs flagged with `log_history="true"` ; the minimal
config sets neither »* ; (b) la maison de référence ne pose effectivement le paramètre nulle part ;
(c) les suites `eventlog` de la série (`JsonApiEventLog_test`, `JsonApiSession_test`,
`JsonApiInputGuards_test`, `JsonApiEmissionBytes_test`) ensemencent bien
`HistLogger::appendEvent()` **à la main**. **La conclusion ne suit pas.**

⭐ **La mesure qui l'a tuée — un `fprintf` sur la ligne du dump, un `make check` complet, comptage
des marqueurs par fichier `.log`** (relevée par la revue, **reproduite indépendamment** ici, mêmes
chiffres) :

| Suite | Passages | Préexistante ? |
|---|---:|---|
| `core/ImpulseGarbageIo_test` | **10** | ✅ inchangée par le ticket |
| `core/WagoPortDefault_test` | **2** | ✅ inchangée par le ticket |
| `core/SetStateGarbage_test` | **1** | ✅ inchangée par le ticket |
| `core/EventWireBytes_test` (filet neuf) | 4 | — |
| **total** | **17**, dont **13 avant le ticket** | |

**La cause n'est pas dans les fixtures, elle est dans le PRODUIT** : ⭐ **sept classes d'IO posent
`log_history="true"` elles-mêmes, par défaut, dans leur constructeur** —
`OutputLight.cpp:53`, `OutputShutter.cpp:62`, `OutputShutterSmart.cpp:73`,
`OutputLightDimmer.cpp:58`, `OutputLightRGB.cpp:53`, `OutputAnalog.cpp:50`, `Scenario.cpp:55`,
toutes sous la forme `if (!get_params().Exists("log_history")) set_param("log_history", "true");`.
Les trois suites ci-dessus construisent `OutputShutter`, `OutputLightDimmer`, `OutputAnalog`,
`OutputLightRGB` et `OutputLight`. Et la chaîne de garde `EventManager.cpp:61-77` est **byte à
byte identique** entre `master` (`df2851d0`) et cette branche — **491 octets des deux côtés**,
comparés en `python3` ⇒ **elle était tout aussi ouverte AVANT la bascule**.

⭐ **Ce qui reste vrai, et qui suffit : AUCUN ORACLE NE REGARDAIT CES OCTETS.** Les 13 passages
faisaient tourner le dump **pour son effet de bord**, en assertant sur tout autre chose ; aucune
suite ne relisait `event_raw` tel qu'`EventManager` l'avait écrit. C'est une affirmation sur
l'**oracle**, pas sur l'atteignabilité — plus modeste, plus faible, et **défendable**. Elle
justifie exactement la même chose : les trois cas `HistoryRow` de
`tests/core/EventWireBytes_test.cpp`, qui font
`io->set_param("log_history", "true")` sur un IO de la maison de référence, puis
`EventManager::create(EventIOChanged, {{"id", …}, {"state", …}})`, puis relisent la colonne
`event_raw` par `HistLogger::getEvents()`. **Mesurés rouges avant la bascule, verts après.**

⚠️ **`EventManager.o` est dans `CORE_SERVER_OBJECTS`, donc lié dans tous les binaires `core/`** —
mais ce n'était pas non plus l'argument : *lié* n'est ni *exercé* ni *observé*, et le vrai piège
ici était le troisième terme. Leçon versée à **`F-LINK-1`**, ci-dessous.

### ⭐ `F-LINK-1`, **neuvième** affirmation d'atteignabilité renversée — et **la première dans l'autre sens**

⚠️ **Append à la section `F-LINK-1`. En conflit, GARDER LES DEUX CÔTÉS.**

Les huit premiers cas de `F-LINK-1` allaient tous dans le même sens : *« ce n'est pas atteignable /
pas testable / pas liable »*, et la mesure répondait **si, ça l'est**. ⭐ **Le neuvième va dans le
sens INVERSE, et c'est le premier** : *« ce n'est exercé par rien »*, et la mesure répond **si, 13
fois par `make check`, par trois suites qui existaient déjà**.

| # | La phrase qui était fausse | La mesure qui l'a tuée |
|---|---|---|
| **9** | *« `EventManager.cpp:79` n'était exercé par AUCUN test de l'arbre »* (E4.1l, 1ʳᵉ livraison) | ⭐ **un `fprintf` sur la ligne du dump + un `make check` + comptage des marqueurs par `.log`** : **17 passages**, dont **13 dans 3 suites préexistantes**. Cause : **7 classes d'IO posent `log_history="true"` par défaut dans leur propre constructeur**, et la chaîne de garde est **byte-identique** master↔branche |

> ⭐ **RÈGLE `F-LINK-1` (v3), quatrième membre :**
>
> 4. **« Non exercé » se mesure en INSTRUMENTANT LE SITE — jamais en raisonnant sur la
>    configuration.** Le geste est d'une ligne : poser un `fprintf` (ou un compteur statique) sur
>    la ligne en question, lancer la campagne, compter les passages **par fichier `.log`** — ce qui
>    donne en prime **quelles** suites y passent, donc **pourquoi**. Tout le reste est une
>    hypothèse déguisée en mesure. Et la sortie de secours, quand la mesure renverse la phrase,
>    est presque toujours une affirmation **plus faible et suffisante** : ici, *« aucun oracle ne
>    regardait ces octets »*, qui justifie le même filet neuf sans être fausse.

⛔ **Pourquoi ce cas-là est instructif, et pas seulement une erreur de plus.** Le raisonnement était
**correct à chaque maillon** — le commentaire de fixture cité est exact, la maison de référence ne
pose vraiment pas le paramètre, les suites `eventlog` ensemencent vraiment à la main. Il ne manquait
qu'une chose : **le produit lui-même posait le drapeau**, dans sept constructeurs que personne
n'avait balayés. ⇒ **Une chaîne de maillons vrais ne fait pas une mesure.** Le raccourci de
diagnostic à retenir, symétrique de celui du cas 7-8 (*« chercher ce que le produit publie déjà »*)
est : ⭐ **avant d'écrire « rien n'atteint ce site », balayer ce que le PRODUIT met par défaut** —
`grep` du nom du paramètre de garde sur tout `src/`, pas seulement sur `tests/`. Ici,
`grep -rn log_history src` rendait **8 lignes** et la réponse était dans **7** d'entre elles.

⚠️ **Corollaire sur le coût.** Cette erreur ne se voit **pas** dans le vert : la suite neuve est
verte, les 145 goldens sont intacts, le `make check` est bon. Elle ne coûte rien **au code** et
tout **au récit** — et le récit, ici, est ce dont sept tickets suivants héritent. C'est la raison
pour laquelle elle vaut un retour à l'auteur alors que le `src/` est sain.

### ⛔ QUATRE affirmations de la fiche `E4.1l.md` que la mesure contredit

1. **« `JsonApiHandlerHttp.cpp:417` — `json_array_append_new(jev, i->toJson())` dans `eventlog` »**
   → **faux**, ce site est dans **`processPolling()`** (l'action `poll_listen`), pas dans
   `eventlog`. `eventlog` est `processEventLog()`, déjà en nlohmann depuis E4.0/E4.1b via
   `HistEvent::toJson()`, et **il n'est pas dans ce périmètre**. Conséquence directe : le piège
   annoncé en tête de fiche — *« `eventlog` est le SEUL endroit de l'API qui émet de vrais entiers
   JSON »* — visait un site **hors périmètre**. Recompté sur le périmètre réel : **zéro** entier
   JSON, **zéro** réel, tout part en chaîne, `"type"` compris (l'entier de l'enum passé par
   `Utils::to_string()`). Épinglé sur les octets par
   `TheEventTypeTravelsAsAJsonStringOnAllThreeWires`, **vert avant et après**.
2. **« `ScriptExec.cpp` ne peut pas être migré ici : il dépend aussi de
   `JsonApi::buildFlatIOList()` (`:162`) »** → **faux pour le site concerné**. La dépendance à
   `buildFlatIOList()` est dans le message **frère** (`jroot`, le contexte initial, `:157-170`) ;
   le bloc qui appelle `ev.toJson()` (`jev`, `:186-191`) ne construit que
   `{msg:"event", data:<event>}` et **n'a aucune dépendance jansson propre**. Il aurait donc pu
   être migré entièrement ici, **sans adaptateur**.
   ⇒ L'adaptateur `jansson_from_json()` a quand même été écrit, **parce que la fiche l'exige
   nommément et non négociablement**, et parce qu'il achète quelque chose de réel : le
   round-trip par `json_loads()` fait **re-échapper jansson dans sa forme MAJUSCULE** en sortie,
   donc **le seul octet qui bouge sur le wire `calaos_script` est l'ordre des clés de l'objet
   event**. **Pour `E4.1m` : le supprimer coûte 4 lignes** (l'enveloppe `jev` devient
   `Json{{ "msg", "event" }, { "data", ev.toJson() }}` + un `dump()`), et il n'y a **aucune** autre
   contrainte.
3. **« `tests/core/JsonApiEvents_test.cpp`, `JsonApiEventLog_test.cpp` suivent la signature »**
   → **faux** : **aucun** test de l'arbre n'appelle `CalaosEvent::toJson()` (vérifié par balayage
   des fichiers suivis). Les deux suites passent par les transports. **Zéro fichier de test n'a
   dû suivre la signature**, et le commit de bascule ne touche aucun test.

4. **« `json_pack("{s:s, …}")` abandonne silencieusement une paire dont la chaîne C est `NULL` »**
   → **faux**, mesuré sur sonde compilée contre le vrai jansson : `json_pack` rend **`NULL` pour
   l'objet ENTIER** dès qu'un `%s` reçoit `NULL` — `CalaosEvent::toJson()` n'aurait donc rien rendu
   du tout, et il n'y avait aucune « paire perdue » à ce niveau. **L'abandon de paire venait d'un
   cran plus bas**, de `jansson_from_params()` : `json_string()` rend `NULL` sur de l'UTF-8
   invalide, `json_object_set_new()` rend `-1`, et **seule cette paire-là** disparaissait pendant
   que l'objet restait valide. ⇒ **Deux modes de défaillance, pas un.** Le commentaire de
   `EventManager.cpp` qui recopiait l'affirmation de la fiche a été **corrigé** (prose seule).

### ⭐ Le wire RemoteUI reçoit AUSSI ce changement — mesuré, non annoncé par la fiche

`JsonApiHandlerWS` branche `handleEvents` sur `EventManager::newEvent` **dans son constructeur**
(`JsonApiHandlerWS.cpp:37`), `RemoteUIWebSocketHandler` en **hérite** sans le redéfinir, et
`RemoteUIWebSocketHandler.cpp:72` appelle `setAuthenticated(true)` — la garde `if (!loggedin)` de
`handleEvents` est donc franchie pour un écran déporté. ⇒ **les push d'événements vers un appareil
RemoteUI changent d'ordre de clés et d'échappement exactement comme ceux de l'API publique.**
Le tableau des wires d'`E4.1.md` classe RemoteUI en « déjà en nlohmann aujourd'hui » : c'est vrai
de ses **réponses**, pas de ses **events**, qui partaient par la surcharge jansson de `sendJson()`.
L'autre bout est le firmware d'un dépôt voisin, qui décode avec un vrai parseur.

⚠️ **Le renvoi « à reprendre par `E4.1n` » de la première rédaction était TROMPEUR : il n'y a plus
rien à faire côté CODE.** Ce ticket couvre ce wire **par construction** (l'héritage fait que la
bascule s'y applique sans une ligne de plus), et le delta est **déjà déclaré** dans
`RELEASE_NOTES.md`, qui nomme explicitement les écrans déportés. L'exigence est **satisfaite ici**.
⇒ **`E4.1n` hérite d'une tâche exclusivement DOCUMENTAIRE** : corriger la ligne RemoteUI du tableau
des wires d'`E4.1.md`, qui la classe « déjà en nlohmann aujourd'hui » — vrai de ses **réponses**,
faux de ses **events**. ⛔ **Ne pas ouvrir de ticket de code là-dessus, il serait vide.**

### Ce qui bouge sur les octets — ⛔ **la première rédaction en déclarait TROIS, il y en a CINQ**

⚠️ **La revue a trouvé les deux manquants ; ils sont ici REPRODUITS**, sur une sonde compilée
contre le vrai `jansson` et le vrai `json.hpp`, exécutant les deux chaînes réelles :
`jansson_from_params()` + `json_dumps(JSON_COMPACT|JSON_ENSURE_ASCII)` d'un côté,
`Params::toNJson()` + `dump(-1, ' ', true, error_handler_t::replace)` de l'autre. **120 sondes,
75 DIFF, 45 SAME.**

| # | Delta | Avant | Après | 1ʳᵉ livraison |
|---|---|---|---|---|
| 1 | ordre des clés | ordre d'insertion | trié | ✅ déclaré |
| 2 | casse de l'hexadécimal | `\u00E9` | `\u00e9` | ✅ déclaré |
| 3 | **valeur** en UTF-8 invalide | paire **supprimée** | conservée en U+FFFD | ✅ déclaré |
| 3b | ⭐ **CLÉ** en UTF-8 invalide | paire **supprimée** (`{}`) | clé servie en `k��z` | ⛔ **non dit** |
| 4 | ⭐ **`U+007F` (DEL)**, valeur **ou** clé | **octet BRUT** : `{"c":"a<0x7f>b"}` = **11 o** | `{"c":"a\u007fb"}` = **16 o** | ⛔ **manquait** |
| 5 | ⭐ **`0x00` embarqué**, valeur **ou** clé | **TRONCATURE** à la chaîne C : `a\0b` → `"a"` ; une clé `k\0z` → `"k"`, donc **paire renommée en silence** | `"a\u0000b"` / `"k\u0000z"` | ⛔ **manquait** |

⭐ **Le delta 4 est le seul qui change la LONGUEUR du message** (11 → 16 octets sur la sonde), donc
le `Content-Length` de la réponse HTTP. C'était l'énoncé « seule la casse de l'hexadécimal » qui le
masquait : il est **trop étroit**, et c'est exactement le mot « seule » qui était faux.
⭐ **Le delta 5 est le plus vicieux** : avant, une clé contenant un `0x00` était servie **sous un
autre nom**, silencieusement, avec sa valeur — un client la lisait sans rien remarquer.

**Atteignabilité des deltas 4 et 5** : `%7f` et `%00` sur `set_state`. `Utils::url_decode`
(`StringUtils.cpp:57-73`) fait `ret += (char) htoi(...)` — l'octet entre **tel quel** dans la
`std::string`, y compris le zéro, et ne traverse **aucun parseur JSON**. ⚠️ *Vérifié au niveau du
décodeur ; **pas** rejoué de bout en bout contre un `calaos_server` réel.*

**Ce qui a été balayé au-delà** (résultats détaillés dans `RELEASE_NOTES.md`) : `0x00` ;
`0x01`–`0x1F` ⇒ **9 diffs, toutes de casse** (`0B 0E 0F 1A 1B 1C 1D 1E 1F`) — chiffre du relecteur,
reproduit à l'identique ; `0x7F` ; `0x80`–`0x9F` en **octets bruts** ⇒ **32/32 diffs** (UTF-8
invalide) ; `U+0080`–`U+009F` **bien formés** ⇒ **12 diffs**, casse ; **substituts** écrits en UTF-8
(`U+D800`/`U+DC00`/`U+DFFF`) en valeur et en clé ⇒ diffs, UTF-8 invalide ; **non-caractères**
`U+FFFE`/`U+FFFF`/`U+FDD0`/`U+1FFFE` ⇒ diffs de casse, **aucun n'est filtré ni avant ni après** ;
⭐ `U+2028`/`U+2029` ⇒ **IDENTIQUES**, échappés des deux côtés ; surlongue `C0 AF`, tronquée
`E2 80`, séquence 5 octets ⇒ diffs (UTF-8 invalide) ; `"` et `\` ⇒ identiques.

⛔ **Ce qui n'a PAS été balayé, et dont les sept tickets suivants héritent** : les chaînes
**longues** (aucun effet de bord de tampon cherché), les **nombres** et **booléens** (hors périmètre
ici, tout part en chaîne), la **profondeur** d'imbrication, les **doublons de clés**, l'ordre de tri
sur des clés **non ASCII** (il devient l'ordre des unités de code UTF-8, pas un ordre linguistique
— et il n'y a **aucun** oracle dessus), et tout ce qui n'est pas `Params` → JSON : les autres
constructeurs de l'API basculent dans `E4.1m` … `E4.1s` et **n'ont pas été sondés**. Rien contre un
client tiers, rien sous ASan.

**Par wire** :

| Wire | Ordre des clés | Deltas 2-5 |
|---|---|---|
| WS `{"msg":"event",…}` (API + RemoteUI) | enveloppe `msg,data` → `data,msg` ; event `event_raw,type,type_str,data` → `data,event_raw,type,type_str` | tous |
| HTTP `poll_listen` | racine `success,events` → `events,success` ; même bascule sur l'event | tous |
| Ligne d'historique (`event_raw` en base) | même bascule sur l'event | tous |
| `calaos_script` (`ScriptExec`) | **seul** l'objet event se trie ; l'enveloppe reste en ordre d'insertion | ⚠️ **aucun** — le round-trip `json_loads()` de l'adaptateur ré-échappe en jansson. **Non mesuré sur sonde**, déduit du fait que la sortie repasse par `json_dumps()` |

⚠️ **Aucun de ces deltas n'est visible d'un golden** : les 145 comparent des documents parsés.
`tests/core/EventWireBytes_test.cpp` est le seul oracle qui les voit — 9 de ses 10 cas étaient
rouges sur l'arbre jansson, et le 10ᵉ (le témoin de typage) vert des deux côtés.

### Le changement de comportement sur l'UTF-8 invalide, et pourquoi pas de correctif séparé

`jansson_from_params()` **supprimait la paire en silence** (`json_string()` rend `NULL`,
`json_object_set_new()` rend `-1`, aucun des deux codes n'était testé) ; `Params::toNJson()` la
conserve et `error_handler_t::replace` la rend en U+FFFD. ⚠️ **Et cela vaut aussi quand c'est la
CLÉ qui est mal encodée** : `json_object_set_new()` valide l'UTF-8 de la clé et rend `-1` de la
même façon ; désormais la clé est servie en `k��z`. ⛔ **À ne pas confondre avec `json_pack()`** :
sondé, `json_pack("{s:s,…}")` avec une chaîne C `NULL` ne perd pas une paire, il rend **`NULL`
pour l'objet ENTIER** — c'est un tout autre mode de défaillance, et le commentaire de
`EventManager.cpp` qui l'attribuait à `json_pack` a été corrigé. Sur un event `io_changed`, cela veut dire
que `data.state` **disparaissait** de l'event et de la ligne d'historique et qu'il est maintenant
présent, en garbage lisible. **Même arbitrage que `F-LUA-2` (E4.1j) et `F-REO-6`** : les deux issues
sont du garbage, l'entrée était déjà cassée dans les deux, et la nature de l'observable ne change
pas. La surface d'injection est celle qu'`E4.1b` a mesurée et écrite : `set_state` par le repli
sur les paramètres GET (`JsonApiHandlerHttp.cpp:88`), octets pourcent-décodés qui ne traversent
**aucun parseur JSON**.

### Un des trois invariants est INERTE sur l'adaptateur — mesuré, pas supposé

`jansson_from_json()` dumpe avec la forme complète de l'épique, mais son résultat est
**immédiatement re-parsé** par `json_loads()`, et un parseur est aveugle à l'échappement.
`ensure_ascii` y est donc **structurellement inobservable** : la contre-mutation `true → false`
laisse `ParamsJson_test` **vert**, et c'est écrit en toutes lettres dans le fichier de test pour
que personne ne lise ce vert comme une couverture. `error_handler_t::replace` et **l'ordre trié**,
eux, sont observables et ont chacun leur cas. C'est exactement le défaut qu'`E4.1k` a trouvé chez
elle (une suite aveugle à `ensure_ascii`), déclaré cette fois **avant** la revue.

### Ce qui n'est PAS mesuré, à ne pas surestimer

- **Le site d'appel de l'adaptateur n'est pas exercé.** `ScriptExec.cpp:190` vit dans un lambda
  branché sur `newEvent` après le spawn d'un vrai `calaos_script` par uvw ; aucun test en
  processus ne peut y arriver. `ScriptExec.o` est **lié** partout, ce qui ne prouve rien.
  La substitution y fait **un token de large** et n'a que le compilateur pour filet. **Déclaré,
  pas glosé** — et c'est la formulation que `F-LINK-1` demande.
- **Rien n'a tourné sous ASan.** Aucun `calaos_server` réel, aucun `calaos_script` réel, aucun
  client tiers.
- **`processPolling()` n'a pas été rejoué contre un client de production** ; l'argument de
  compatibilité est *raisonné* (tout consommateur décode avec un vrai parseur), pas *exercé*.

---

## T3.46 — la moitié ÉCRITURE du chemin RETOUR Wago, et le sort de `libmbus` (2026-08-25)

Mesures faites en livrant `fix/t3.46` (base `df2851d0`). Fiche : `docs/refactoring/T3.46.md` §7.
**Toutes recomptées sur cet arbre** ; aucun cardinal repris d'une fiche antérieure.

- ⭐ **[F-LINK-1, nouvelle occurrence — et une affirmation publiée qui était FAUSSE]**
  `T3.31` §7.11.5, la fiche `T3.46` §6.2 et le commentaire de `IO/Wago/WagoTypes.h` écrivaient
  tous les trois qu'**aucune** des onze implémentations de callback Wago n'est *atteinte par un
  binaire de test*. **Mesuré : faux au premier degré.** `nm -C` sur les **92** ELF de `tests/`
  trouve `WagoWriteCallback` et `WagoReadCallback` **définis dans un binaire**,
  `core/WagoPortDefault_test`, qui lie `WOAnalog.o`, `WODigital.o` et `WagoMap.o`.
  ⭐ **La formulation correcte est mesurable et a été mesurée** : `::abort()` inséré en **première
  instruction** de `WOAnalog::WagoWriteCallback`, `WODigital::WagoWriteCallback` et
  `WOAnalog::WagoReadCallback`, reconstruction, exécution ⇒ **11 cas, 11 PASS, `exit 0`**.
  ⇒ **liés dans 1 ELF sur 92, invoqués dans 0.** *« Lié » n'est pas « exercé », et « non lié » non
  plus n'est pas la même chose que « non atteint » : les trois se mesurent séparément.*

- ⭐ **[F-WAGO-9 — TRANCHÉ : `libmbus` n'est PAS patchée, et voici la mesure qui décide]**
  ⛔ **RECTIFIÉ le 2026-08-25 (2ᵉ revue de T3.46) — la 1ʳᵉ rédaction de ce finding était FAUSSE.**
  Elle disait : « la paire permutable **n'est pas dans les six commandes publiques** ». **Faux** :
  `mbus.h:109-127` montre que **les six commandes que Calaos appelle portent TOUTES une paire
  `(mbus_uword, mbus_uword)` adjacente** — `coils_addr`/`coils_num`, `start_addr`/`points_num`,
  `coil_addr`/`data`, `register_addr`/`preset_data`, et les deux formes `*_multiple_*` ; les deux
  commandes **non appelées** sont justement les seules sans la paire (`diagnostics` n'a qu'**un**
  `mbus_uword`, `report_slave_id` aucun). ⚠️ **Et la mesure `M5` du même ticket la contredisait** :
  elle permute `mbus_cmd_preset_single_register(mbus, 1, (mbus_uword)val, address)` — **une
  permutation d'une signature publique** — à `rc=0`.
  ⭐ **Corollaire de méthode** : *une phrase qui affirme « la paire n'y est pas » à côté d'une
  mesure qui permute cette paire n'a pas été relue contre sa propre mesure.* Et ici la formulation
  fausse rendait le refus **plus fort qu'il n'est**.
  ⭐ **CE QUI EST EXACT, ET CE QUI PORTE RÉELLEMENT L'ARBITRAGE**, un cran plus bas : les six
  délèguent à **un seul constructeur de requête interne**,
  `mbus_cmd_addr_wdata(mbus, slave_addr, funct_code, mbus_uword addr, mbus_uword data)`
  (`libmbus/mbus_cmd.c:78-80`), et sur ses **CINQ** sites d'appel (`:254`, `:292`, `:332`, `:368`,
  `:405`) ses deux paramètres mot portent **TROIS paires de rôles différentes** : adresse +
  **compte** (`read_coil_status`, `read_holding_registers`), adresse + **donnée**
  (`force_single_coil`, `preset_single_register`), **sous-fonction** + donnée (`diagnostics`).
  ⇒ **« un type par rôle » ne s'applique pas à cette fonction sans la SCINDER** — un changement
  *fonctionnel* dans du C tiers de 2003 que rien n'exécute.
  ⇒ ⭐ **Le refus tient sur le COÛT, mesuré. Il ne tenait PAS sur « la paire est absente ».** La
  décision est inchangée ; c'est sa justification publiée qui était fausse. Corrigée aux **trois**
  endroits où elle avait été écrite : ici, `T3.46.md` §7.5(b), et le commentaire d'en-tête de
  `tests/WagoWriteReply_test.cpp`. Typer seulement les six signatures
  publiques laisserait la paire vivante une trame plus bas et déplacerait les six déballages de
  `WagoCtrl.cpp` **dans** `libmbus`. C'est le « 7 endroits → 7 endroits » de `T3.31` §7.5,
  **mesuré un niveau plus profond, avec une raison concrète**.
  Recomptes : `mbus.h` déclare **8** `mbus_cmd_*`, Calaos en appelle **6**, un site par commande,
  tous dans `WagoCtrl.cpp`. `nm -C` sur les 92 ELF de `tests/` :
  `mbus_cmd_preset_single_register`, `mbus_cmd_force_single_coil`, `mbus_cmd_addr_wdata` **définis
  dans 0, référencés dans 0**.
  ⚠️ **Ce n'est toujours PAS une impossibilité technique** — re-mesuré `gcc 16.2.1 -std=c11 -Wall
  -Wextra` : `struct` à un champ, forme correcte `rc=0`, paire typée permutée `rc=1`, permutation
  nue `rc=1`. C'est un **coût**, et le coût vient d'être remesuré plus haut qu'estimé.
  ⇒ **Résiduel fermé AUTREMENT, pas repoussé** : un **tripwire de source** dans
  `tests/WagoWriteReply_test.cpp` lit `WagoCtrl.cpp` via `CALAOS_TOP_SRCDIR` et exige que
  l'adresse reste le **3ᵉ** argument des deux appels d'écriture. **Contre-mutations M6/M7 :
  permuter l'un ou l'autre appel COMPILE toujours (`rc=0`)** — la preuve que le saut C est nu —
  **et le tripwire devient rouge**.
  ⚠️ **Un tripwire n'est pas un type** : il n'empêche pas un **nouveau** site d'appel écrit à
  l'envers, il ne tombe que si ces deux lignes-là bougent. Écrit tel quel dans le test.

- **[Déclarations mortes — `IO/OutputAnalog.h`]** `OutputAnalog::WagoReadCallback` et
  `::WagoWriteCallback` étaient **déclarées et définies nulle part** : balayage `python3` de tout
  l'arbre, **1** occurrence du premier nom (dans un commentaire) et **0** du second. `WOAnalog`
  déclare et définit les siennes. **Supprimées** par T3.46 — ce qui règle le « point dur
  architectural » du §6.3.3 (une base **générique** hors arbre Wago qui aurait dû apprendre
  `WagoTypes::`) **sans arbitrage**, parce qu'il n'y avait rien à typer.

- ⭐ **[Piège de méthode — la permutation SÉMANTIQUEMENT IDENTIQUE]** Sur les quatre callbacks
  d'écriture typés, la contre-mutation par échange ne dit pas la même chose partout, et confondre
  les deux cas mène soit à un faux rouge exigé, soit à une trouvaille retirée à tort :
  `WOAnalog::WagoWriteCallback` fait `value = _value` ⇒ permuter **change le programme** (la
  sortie rapporterait son adresse modbus). `WODigital::WagoWriteCallback` et
  `WOVoletBase::WagoWriteCallback` ne lisent **que `status`**, leurs deux autres paramètres sont
  **inutilisés** ⇒ permuter est un **no-op sémantique**, et **exiger un test rouge y serait exiger
  qu'un test distingue deux programmes identiques**. Le typage y ferme un **contrat** (pour la
  prochaine implémentation, le prochain site d'émission), pas une réponse fausse vivante.
  ⇒ **Distinguer les deux avant d'écrire l'oracle**, et dire lequel est lequel.

- **[Le mannequin de `WagoMap.cpp` — vrai à moitié]** `T3.31` a mesuré que la **valeur** des deux
  émissions de réponse d'écriture est un littéral (`false` pour un bit, `0` pour un mot), donc
  qu'une permutation y substitue un mannequin. ⭐ **Ce qui manquait : l'ADRESSE de ces mêmes lignes
  n'est pas un mannequin**, elle est décodée de la réponse. Une permutation envoie donc l'**adresse
  vivante** à la place de la valeur, et `WOAnalog` l'assigne puis `emitChange()`.
  ⇒ La moitié écriture vaut **moins** que la moitié lecture, pas **rien**.

- ⚠️ **[Reste dû — la moitié LECTURE du chemin retour]** `MultiBits_cb` / `MultiWords_cb`
  (`IO/Wago/WagoMap.h`) et **sept** implémentations —
  `WagoMap::WagoModbusReadHeartbeatCallback`, `WIAnalog::WagoReadCallback`,
  `WITemp::WagoReadCallback`, `WOAnalog::WagoReadCallback`, `WODigital::WagoReadCallback`,
  `WIDigitalBase::WagoReadCallback` (`WagoIOBase.h`), plus `OutputAnalog::WagoReadCallback`
  **supprimée** — et **quatre** sites d'invocation dans `WagoMap::processNewMessage`. Leur
  `(UWord address, int count)` est la paire qu'E4.1h a mesurée **verte** sur un échange. C'est la
  moitié **la plus dangereuse** ; T3.46 a reçu l'autre pour périmètre et le dit.
  ⭐ **Numéro attribué : [`T3.50`](T3.50.md), fiche créée, et c'est LE PROCHAIN de cette chaîne —
  pas « plus tard ».** **13 sites nommés** : 2 `typedef` (`WagoMap.h:38`, `:39`), 7 implémentations
  (`WagoMap.cpp:172`, `WIAnalog.cpp:72`, `WITemp.cpp:68`, `WOAnalog.cpp:69`, `WODigital.cpp:81`,
  `WagoIOBase.h:104` — **6 vivantes** — plus `OutputAnalog::WagoReadCallback`, la 13ᵉ, **mesurée
  morte et supprimée par T3.46**), et 4 sites d'invocation (`WagoMap.cpp:214`, `:225`, `:238`,
  `:249`). **12 vivants sur 13.** ⚠️ **L'inversion d'ordre (écriture avant lecture) n'est
  acceptable qu'à cette condition** : elle est acceptée parce qu'elle est dite franchement, et elle
  ne doit pas se répéter.

- ⛔ **[Non corrigé, comme exigé]** `WOAnalog::WagoWriteCallback` écrase sa propre valeur
  rapportée avec le littéral `0` après chaque écriture réussie, puis `emitChange()`
  (`T3.46.md` §6.4). Changement de **comportement** sur du code de `master` : arbitrage séparé.
  **F-WAGO-7** (`UWord address;` / `int count;` non initialisés dans le dispatcher de
  `calaos_wago`) est **inchangé par ce ticket** : même paire de valeurs, mais un défaut
  d'**initialisation** et non de **signature**, et dans un fichier que T3.46 ne touche pas.
  **Toujours ouvert, toujours dû.**

- ⭐ **[F-FLAKY-1 — ÉLARGI : le finding nomme un cas, la famille en compte AU MOINS DEUX]**
  Vu pendant la validation finale de T3.46 (`make distclean` + reconfigure complet, `make check -j8`,
  trois autres agents buildaient) ⇒ **`# FAIL: 1`**, mais **pas le cas que `F-FLAKY-1` nomme** :
  `core/ShutterImpulse_test.cpp:297`,
  `ShutterImpulseTest.PlainImpulseDownKeepsMovingUntilTheRequestedDuration`,
  `Actual: true / Expected: false` — **le voisin** de
  `PlainImpulseDownWithoutImpulseTimeStillHonoursTheDuration` (`:384`), **de forme identique** :
  la boucle est pompée **juste en deçà** de l'échéance d'impulsion, puis
  `EXPECT_FALSE(sh.isStopped())`.
  ⇒ **Corollaire de méthode** : chercher « le cas de F-FLAKY-1 » par son NOM conduit à conclure
  « ce n'est pas le flake connu » et donc « c'est ma modification ». **C'est le FICHIER et la
  FORME de l'assertion qui identifient la famille, pas le nom du cas.**
  **Exclusion de causalité refaite pour T3.46, pas citée** : (a) `ShutterImpulse_test.cpp` hors du
  diff (11 fichiers) ; (b) `nm -C` sur le binaire : **6938 symboles, 0** portant l'un des neuf noms
  touchés par le ticket ; (c) **12 exécutions du cas seul en isolation : 12 PASS, 0 FAIL** ;
  (d) un `make check` antérieur sur le même code : **19/19** sur cette suite.
  ⚠️ **Aucun `make check` relancé pour faire disparaître le rouge** ; le 94/95 est publié tel quel
  à côté du 95/95, dans `T3.46.md` §7.8.

  ⭐ **TRANCHÉ le 2026-08-25 par la 2ᵉ revue de T3.46 — la mesure POSITIVE que personne n'avait
  faite.** `F-FLAKY-1` a été **reproduit sur `master` NU** (`df2851d0`, worktree neuf, même
  recette, **aucune ligne de `fix/t3.46`**) ⇒ `exit 2`, `# TOTAL: 94 / PASS 93 / FAIL 1`, **seul
  échec `core/ShutterImpulse_test.cpp:297`** — le cas, la ligne et le message exacts du run 2 de
  T3.46. Quantifié : **24 exécutions à vide ⇒ 0 rouge**, **48 exécutions sous 96 brûleurs ⇒ 5
  rouges** (`:297` ×2, `:384` ×3).
  ⇒ ⭐ **LA FAMILLE EST D'AU MOINS SIX, pas deux.** La forme identique — pomper la boucle **juste
  en deçà** de l'échéance puis exiger `EXPECT_FALSE(sh.isStopped())` — existe à
  `core/ShutterImpulse_test.cpp` **lignes 297, 343, 384, 450, 498 et 664** (relues une par une :
  les cinq premières via `pumpLoopFor(stillMovingProbeMs(...))`, la 6ᵉ via un `pumpLoopFor(200)`
  fixe).
  ⇒ ⭐ **CE N'EST PAS DU BRUIT, C'EST UN DÉFAUT DE TEST** : une durée mesurée sur l'**horloge
  réelle**, sans horloge injectable. `stillMovingProbeMs = (impulse + requested) / 2` avec
  `kPlainDownMs = 147` et `kPlainImpulseTimeMs = 35` ⇒ sonde à **91 ms** pour une échéance à
  **147 ms** : ⭐ **56 ms de marge**, que l'ordonnanceur mange sous charge. Le classer « flottant »
  et passer à autre chose est la mauvaise lecture ; **la correction est une échéance relative au
  temps simulé, ou une marge** — ⇒ **`T3.49`**.
  ⚠️ **Acquis : ne plus relancer un `make check` pour le faire disparaître, et ne plus refaire
  l'exclusion de causalité ticket par ticket.** Si ce rouge apparaît, il se **note** et on
  continue.

### ⭐ T3.46 — ce que la 2ᵉ revue a ajouté par la MESURE (append, 2026-08-25)

- ⭐ **[F-TYPE-5 — INARMABLE sur un `sigc::slot`, et c'est plus fort que « nous avons pris soin »]**
  L'angle mort connu de la sonde `is_invocable_v` est le **paramètre référence non-const** : avec
  un `T &`, la sonde répond FAUX **même pour l'ordre correct**, donc toutes les assertions
  `EXPECT_FALSE` d'un fichier passeraient gratuitement. `T3.46` écrivait s'en protéger **par
  discipline** (« les quatre implémentations prennent leur charge par valeur »).
  ⭐ **Mesuré (mutation `MXD`) : c'est une propriété de la BIBLIOTHÈQUE.** Réécrire un paramètre
  de `SingleBit_cb`/`SingleWord_cb` en `T &` ⇒ **`rc=2`** : `sigc++` instancie l'invocateur du slot
  avec des arguments **prvalue** aux sites d'émission et **refuse lui-même la référence**.
  ⭐ **Rejoué indépendamment** (témoin `M0` à ensemble VIDE, ancre unique, mutation relue sur le
  disque, `os.utime` pour qu'un arbre déjà construit ne rende pas un faux vert, restauration
  comptée et vérifiée octet à octet, passe de contrôle revenue à `rc=0` ensemble VIDE) — la ligne
  qui dit tout : `WagoMap.cpp:244: error: cannot bind non-const lvalue reference of type
  'sigc::slot3<…, WagoTypes::WordValue&>::arg3_type_' to an rvalue`.
  ⇒ **Sur ces deux `typedef`, l'angle mort ne peut PAS être réarmé en silence : la tentative est un
  échec de build, pas une suite verte.**
  ⚠️ **Portée à ne pas élargir** : propriété de `sigc::slot`, **pas** de la sonde. Sur une fonction
  ou un foncteur ordinaires, `F-TYPE-5` reste entier.
  Les trois autres contournements essayés donnent aussi `rc=2` : `MXA` permuter `status` ↔
  `BitValue`, `MXB` la **même** enveloppe deux fois, `MXC` un `static_cast` explicite.
  ⇒ **Aucun contournement trouvé** : ni cast, ni agrégat, ni `operator=`, ni base commune, ni
  retour au scalaire.

- ⭐ **[F-ORACLE — un tripwire de source a un TAUX DE FAUX ROUGE, et il se mesure]**
  Nouveau motif, générique. Le tripwire de `tests/WagoWriteReply_test.cpp` (qui épingle l'ordre des
  arguments des deux appels `libmbus` d'écriture) était annoncé avec « une reformulation innocente
  peut le faire rougir ». **Mesuré : 4 réécritures innocentes sur 5 le faisaient rougir** — espace
  avant `(`, renommage du local `address` → `addr`, extraction du cast dans une variable, et ⭐
  **ajout d'un COMMENTAIRE DE DOC nommant la fonction** (`src.find(fn + "(")` accrochait le
  commentaire et rendait **2** arguments au lieu de 4). Seul un reflow sur trois lignes restait
  vert.
  **Durci** (blanchiment des commentaires et littéraux par automate à 5 états à positions
  préservées ; nom refusé s'il est fragment d'un identifiant plus long ; espace toléré avant `(` ;
  préfixe `addr` accepté) ⇒ **1 faux rouge sur 5**, les deux vrais rouges `M6`/`M7` **conservés**.
  Avant/après mesurés dans **une même sonde `g++ -std=c++17`** alimentée par le même jeu, en
  extrayant l'analyseur des **deux** versions du fichier. ⭐ **Puis `M6`/`M7` revérifiés de bout en
  bout sur le binaire livré** — le tripwire lisant le source **à l'exécution**, la mutation se fait
  **sans reconstruire**, ce qui exclut le faux vert de « l'arbre déjà construit » : témoin `rc=0`
  6/6, `M6` et `M7` `rc=1` avec **un seul** cas rouge, contrôle après restauration `rc=0` 6/6.
  ⭐ **Règle à retenir** : *un garde-fou qui rougit parce que quelqu'un a écrit un commentaire est
  un garde-fou que le premier venu désactive.* Ignorer commentaires et littéraux est le **minimum**
  pour tout oracle qui lit du source — plusieurs suites du dépôt le font déjà. Et **publier le
  taux mesuré**, pas « ça peut arriver ».

- **[Trou d'oracle — l'assertion sans contrôle positif]** Dans `WagoWriteReply_test.cpp`,
  `EXPECT_FALSE(std::is_base_of_v<Address, WordValue>)` était **la seule** assertion du fichier
  sans témoin : un trait répondant FAUX à tout l'aurait fait passer gratuitement. **Comblé** par un
  couple `AProbeBase`/`AProbeDerived` et un `EXPECT_TRUE` à côté.
  ⚠️ **Second trou, DÉCLARÉ et non comblé** : **les quatre implémentations ne sont sondées par
  aucun cas**. Ce qui les ferme est la **compilation** plus la campagne `M1`/`M2`/`M3`. Vraie
  preuve, mais strictement plus faible qu'un oracle exécuté — rien ne verrait une implémentation
  ré-élargie aux scalaires si les sites d'émission étaient mis à jour dans le même commit.


---

## T3.48 — l'archive source redevient constructible (2026-08-25)

### F-DIST-1 — ⭐ **FERMÉ par [`T3.48`](T3.48.md)** (`fix/t3.48`, `206d66fa` + `7fbecfdc`)

⛔ **La cause supposée était fausse et il faut le dire : ce n'était PAS `SUBDIRS`.**
`src/lib/Makefile.am` distribue déjà, **sans aucun `SUBDIRS`**, des fichiers de sous-répertoires
(`llhttp/src/api.c`, `uri_parser/hef_uri_syntax.cpp`, `sole/sole.hpp`, `cpptui/cpptui.hpp`).
Automake résout un chemin de sous-répertoire comme un chemin plat. **Personne n'avait simplement
écrit ces fichiers dans une variable.**

**Forme retenue** : `VENDORED_DIST_TREES` + un `dist-hook` dans `src/lib/Makefile.am` — **le
répertoire, et non le fichier, est l'unité de distribution** ; **9 arbres couvrent 219 fichiers**
et un `git subtree pull` qui en ajoute quarante ne demande aucune retouche. ⚠️ Le patron
`pugixml` (4/4) / `calaos-python` (8/8) est une **énumération explicite** : lu, gardé pour les
petits arbres, **écarté** pour `uvw` (82), `sqlite_modern_cpp` (36) et `exprtk` (31), où il
produirait exactement la liste périmable que le ticket interdit. ⚠️ `EXTRA_DIST = <répertoire>`
(qu'automake sait faire) a été écarté aussi : il copie **verbatim**, donc `.deps/`, `.libs/` et
`*.lo` d'une construction dans l'arbre ; le hook élague, et **le tarball est identique en arbre
vierge et en arbre construit — 1079 entrées des deux côtés, différence `∅` dans les deux sens**.

**Mesuré, `180c4b87` → `7fbecfdc`** : suivis absents **417 → 204** ; **code sous `src/` 126 → 0** ;
fichiers sous `src/` ou `tests/` **211 → 0** ; entrées d'archive **842 → 1079** ; entrées de
`po/POTFILES.in` absentes **72 → 0** ; goldens **145/145** inchangés.
⭐ **Licences : 2/11 → 11/11.** Le ticket en annonçait 4 ; il y en avait **9** manquantes —
`exprtk/license.txt`, `libquickmail/COPYING`, `libquickmail/License.txt`, `llhttp/LICENSE`,
`llhttp/LICENSE-MIT`, `sole/LICENSE`, `sqlite_modern_cpp/License.txt`, `uvw/LICENSE`,
`uvw/docs/LICENSE`. Elles voyagent **avec le code qu'elles couvrent**, pas dans un dossier tiers.

**`make distcheck` : RC 0 de bout en bout**, et ⭐ **`make check` a tourné DEPUIS L'ARCHIVE pour
la première fois — `# TOTAL 95`, 95 PASS** : la réserve ouverte par T3.45 §6 est fermée.

### F-DIST-4 — ⭐ le mur qui était derrière : `check-config-docs.sh` meurt dans tout tarball

Trouvé en franchissant le build. `tests/check-config-docs.sh` diffe
`docs/16_config_options.md` contre `calaos_config options --markdown` — et **`docs/` n'est
distribué par personne**. Dans un tarball, le test **échoue en dur** (`FAIL: … is missing`),
juste après une construction pourtant réussie. ⚠️ **Ce n'est pas une exception au choix « `docs/`
n'est pas distribué »** : ce fichier n'est pas de la documentation, c'est une **sortie de
générateur commitée qu'un test consomme**. Corrigé en le nommant seul dans l'`EXTRA_DIST`
racine ; les 162 autres `docs/` restent dehors. **Aucun troisième mur** : `install`/`uninstall`,
`distcleancheck` et le `dist` imbriqué passent sans retouche.

### F-DIST-2 — **débloqué** : le job CI `distcheck` peut être branché

Il était « à brancher après T3.48, sinon rouge dès le premier jour ». **`distcheck` est vert** :
la condition est levée. Reste ouvert (aucune ligne de `.github/` écrite par ce ticket).
En attendant, la paire de contrôles statiques tient les deux sens, pour **~83 ms** cumulées :
`check-extra-dist.sh` « déclaré ⇒ existe » (**37,3 ms**, 859 chemins) et
`check-dist-coverage.sh` « existe ⇒ déclaré » (**46,3 ms**, 893 fichiers balayés) — le second
**importe** l'analyseur du premier plutôt que d'en écrire un deuxième.

### F-DIST-3 — **reconfirmé une fois de plus**

Le `make dist` de ce ticket a réécrit `po/calaos.pot` et **7** `.po` (`de`, `es`, `fr`, `hi`,
`nb`, `pl`, `ru`), **`en.po` intact**. `git checkout -- po/` appliqué ; `git status -uall` vide.

### F-DIST-5 — `po/en.po` n'est pas distribué parce que `en` n'est pas dans `po/LINGUAS`

Constaté au passage, **non traité** : `po/LINGUAS` liste `en@boldquot`, `en@quot`, `de`, `es`,
`fr`, `hi`, `nb`, `pl`, `ru` — **pas `en`**. Les deux `en@*.po` étant *générés depuis* `en.po`
par les règles gettext, leur source ne part pas dans l'archive. Sans conséquence mesurée
(`make check` depuis l'archive est vert), mais un `autoreconf` de tarball qui voudrait les
régénérer ne le pourrait pas. Hors périmètre T3.48.

### F-DIST-1 (suite) — ⭐ les **trois trous de la revue**, et un quatrième trouvé en les colmatant

⚠️ **Les chiffres de l'entrée ci-dessus valent pour la base `180c4b87`.** La branche a été
**rebasée sur `df2851d0`** et tout a été remesuré : entrées d'archive **1079 → 1081**, suivis
absents **204 → 205** (le delta est entièrement `docs/`, 163 → 164), `docs/` non distribués
**162 → 164**, chemins `check-extra-dist` **859 → 862** (+3 de `T3.31`), fichiers balayés par
l'oracle **893 → 895**. Les invariants n'ont pas bougé : **0 non couvert**, **0 code absent sous
`src/`**, **11/11 licences**, **goldens `d4ebc61f` 145 identiques**.
⛔ **Un chiffre était incohérent et il est tranché : 211, pas 212.** Le commit de caractérisation
(**sujet et corps**) et la première rédaction de `T3.48.md` §6.12 disaient 212 ; `FINDINGS.md` et
le tableau avant/après disaient 211. Remesuré en arbre **vierge** (`git archive`) **aux deux
commits de caractérisation**, avant et après rebase : **211 des deux côtés**. ⚠️ Le sujet du
commit **ne peut pas** être corrigé sans réécrire l'historique et ne l'a pas été.

Le correctif ci-dessus a été livré vert, puis la revue a mesuré trois façons de le contourner.
Les trois sont fermées ; le détail est dans [`T3.48`](T3.48.md) §6.13 à §6.16.

1. ⚠️ **Le hook et l'oracle divergeaient sur les liens symboliques.** `find -type f` **élimine**
   les liens ; `check-dist-coverage.py` marche sur `os.walk()` et **les compte couverts** ⇒
   sous-expédition **silencieuse**. **0 lien suivi dans ces neuf arbres aujourd'hui**, donc
   **latent, pas actif**. Fermé par `\( -type f -o -type l \)` + `cp -pR`. ⭐ **Prouvé, et la
   preuve apprend quelque chose** : un lien suivi posé dans `src/lib/uvw` est vu **0 fois** par
   `find -type f`, **1 fois** par la forme neuve ; il **part** — et il part comme **fichier
   régulier**, parce qu'automake archive avec `tar --format=ustar -chf` et que **`-h` déréférence**.
   Le contenu archivé est `cmp`-identique à sa cible, laquelle devient une **entrée de lien dur**
   dans l'archive. Aucune donnée perdue ; le contenu voyage sous les deux noms.
2. ⛔ **L'oracle croyait la VARIABLE, pas le MÉCANISME — et c'est la forme exacte du défaut que
   le ticket corrige, retournée contre son propre filet.** Hook neutralisé (`dist-hook: @true`),
   `VENDORED_DIST_TREES` intacte ⇒ **219 fichiers disparaissent et l'oracle reste VERT**. Seul
   `distcheck` le voyait, et la CI ne le lance pas. Fermé : avant de créditer **un seul** fichier
   à la variable, l'oracle vérifie que le `Makefile.am` qui la déclare est **généré par
   `AC_CONFIG_FILES`**, qu'il définit **exactement un** `dist-hook`, et que **cette recette nomme
   `$(VENDORED_DIST_TREES)` et copie**. Les **quatre** façons de casser le mécanisme sont
   rouges : recette neutralisée, règle supprimée, **seconde règle ajoutée** (make garde la
   dernière — neutralisation *par addition*), et `src/lib/Makefile` retiré d'`AC_CONFIG_FILES`
   (271 non couverts). ⚠️ **Ce qui reste hors de portée est écrit dans la docstring** : un hook
   qui nomme la variable et copie **mal** satisfait les trois contrôles. **Le jour où quelqu'un
   éditera ce hook et se trompera subtilement, `make check` ne dira rien.** `distcheck` le dira,
   et il n'est toujours pas en CI (F-DIST-2).
3. ⚠️ **Une release depuis un arbre sale fuitait.** Un fichier **non suivi** posé dans un arbre
   listé partait dans le tarball. L'élagage attrapait `*~` et les déchets de construction, **pas**
   `*.orig`, `*.rej`, `*.swp`, `*.user` — et **l'oracle ne regarde pas les non-suivis du tout**
   (mesuré : cinq intrus, `rc 0`, « 221 couverts, 0 non couvert »). ⭐ **Tranché : élargir la
   liste de suffixes a été ÉCARTÉ** — une liste est toujours en retard d'un suffixe. Le prédicat
   exact d'une release est « suivi par git », donc **le hook demande à git et REFUSE de
   construire** une archive qui emporterait un fichier privé (les cinq intrus sont **nommés**,
   `exit 1`). ⚠️ Il se **met en retrait** là où il n'y a pas de dépôt enraciné sur `$(top_srcdir)`
   — l'arbre que `distcheck` déplie — ce qui est correct : cet arbre contient exactement ce qui a
   déjà été expédié.
4. ⭐ **Trouvé en colmatant le n°3, par la sonde et non à la lecture** : la première version de la
   garde **triait sous `LC_ALL=C` et comparait sous la locale ambiante**. `comm` valide l'ordre de
   ses entrées avec **sa propre** collation, et une collation UTF-8 est en désaccord avec l'ordre C
   exactement sur les différences de casse dont ces arbres sont pleins (`LICENSE` à côté de
   `license.txt`). Résultat : `comm: file 1 is not in sorted order`, `exit 1` — **une release
   refusée pour la mauvaise raison**, et seulement quand un vrai intrus faisait diverger les deux
   listes (donc invisible sur un arbre propre). `LC_ALL=C` est désormais exporté pour **toute** la
   recette. ⚠️ **Leçon** : `sort` et `comm` doivent partager la locale, sinon la sonde ment
   précisément le jour où elle sert.

### F-DIST-4 (suite) — ⚠️ **le trou résiduel : retirer la LIGNE sans supprimer le FICHIER**

`docs/16_config_options.md` est bien une **donnée de test**, pas de la documentation, et le
distribuer est la bonne réponse : `tests/check-config-docs.sh` la consomme. Bonus mesuré : le
chemin entre dans les **859** de `check-extra-dist.sh`, donc **supprimer le fichier rougit**
(« déclaré ⇒ existe »). ⛔ **Mais le sens inverse n'est tenu par personne** : retirer la **ligne**
`EXTRA_DIST` en **laissant** le fichier n'est vu par **aucun** test statique —
`check-extra-dist.sh` n'a plus rien à résoudre, et `check-dist-coverage.sh` ne balaie que `src/`
et `tests/`, donc pas `docs/`. Le tarball redevient silencieusement cassé et **seul `distcheck`
le voit**. ⚠️ Élargir le champ de l'oracle à `docs/` n'est **pas** la réponse : les 164 autres
`docs/` ne sont pas distribués **par choix** et rougiraient tous. La réponse est le job CI
`distcheck` de **F-DIST-2**.

### F-DIST-2 (suite) — le coût remesuré, et la robustesse de l'import

**Coût remesuré** sous la charge réelle de la machine (`load average` **17,50**, médiane sur
**n=20**) : plancher `python3 -c pass` **10,4 ms**, `check-extra-dist.py` **33,6 ms**,
`check-dist-coverage.py` **44,7 ms**. ⚠️ La revue avait mesuré **67,7 ms** sous **quatre
constructions concurrentes** : **les deux sont vrais, et c'est le point** — le chiffre dépend de
la charge et n'a de sens qu'accompagné d'elle. Dans tous les cas, **sous 70 ms sur une suite de 95
tests**. **Import robuste, vérifié** : renommer `tests/check-extra-dist.py` fait sortir
`check-dist-coverage.py` en **RC 2** avec un message explicite — **pas de vert silencieux**.

### ⚠️ Licences — « 11/11 » veut dire **11/11 de ce qui existe**

Les 11 fichiers de licence tiers présents dans le dépôt partent désormais tous dans l'archive
(2/11 avant). ⛔ **`src/lib/uri_parser` ne contient AUCUN fichier de licence**, ni avant ni après :
ce n'est pas un fichier perdu à la distribution, c'est un fichier **absent de l'import amont**.
Le point est **juridique**, il doit être exact : l'archive est complète **par rapport au dépôt**,
et le dépôt est incomplet **par rapport à ce qu'il embarque**. À obtenir auprès de l'amont.
