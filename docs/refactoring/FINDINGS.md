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

- ✅ **[F-WAGO-8] — CORRIGÉ : [T3.43](T3.43.md)** (2026-08-25). ⚠️ **SÉCURITÉ.** *(Énoncé d'origine conservé ci-dessous, addendum de livraison en fin d'entrée.)* **PRÉEXISTANT, hors périmètre de T3.30, trouvé en refermant F-WAGO-7 :
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
  ⭐ **Ce ticket existe et son code est MERGÉ : [T3.43](T3.43.md)** — branche `fix/fwago8`,
  rebasée sur `master` puis `merge --ff-only`. L'entrée est FERMÉE côté arbre.

  ⭐ **ADDENDUM DE LIVRAISON — T3.43 (2026-08-25).** Caractérisation `2cc41b44` (**tests seuls, zéro
  ligne de `src/`**, vérifié sur le commit), correction `15765564`. **L'énoncé ci-dessus est exact
  sur le fond et se corrige sur deux points de périmètre, mesurés en `python3` :**

  1. ⭐ **`read_coil_status` n'est PAS le seul, et ce ne sont pas les commandes attendues.** Balayage
     des **8** commandes déclarées par `mbus.h` : **TROIS** lisent une quantité **dans la réponse**
     pour la recopier dans un tampon **dimensionné par l'appelant** — **FC 01** (`read_coil_status`),
     **FC 03** (`read_holding_registers`) et **FC 17** (`report_slave_id`, sans aucun appelant dans
     l'arbre). ⚠️ **`read_input_status` (FC 02) et `read_input_registers` (FC 04) N'EXISTENT PAS**
     dans cette bibliothèque : `MBUS_FC_READINPUTSTATUS`/`MBUS_FC_READINPUTREGISTERS` sont
     **définis** (`mbus_cmd.c:52`/`:53`) mais **aucune fonction ne les utilise**. Les cinq autres
     commandes (05, 06, 08, 15, 16) lisent des **mots de position et de taille fixes** qu'elles
     **comparent** à la requête : elles ne sont pas touchées. Tableau complet : [T3.43](T3.43.md) §2.
  2. ⭐ **La portée est plus large que « le battement de cœur ».** **Tout** ce qui lit un automate
     Wago dans Calaos lit **UN bit ou UN mot** — `WagoIOBase.h:138`/`:174`, `WODigital.cpp:62`,
     `WIAnalog.cpp:111`, `WITemp.cpp:99`, `WOAnalog.cpp:53` — donc **1 octet** ou **2 octets**
     alloués, débordés de **254** et de **252**. Ce n'est pas un cas de bord, c'est le cas nominal.
  3. ⚠️ **« Il faut un pair malveillant ou en panne » reste vrai mais était sous-dit** : Modbus/TCP
     **n'est pas authentifié**, donc « le pair » n'est pas nécessairement l'automate — c'est
     n'importe qui capable d'atteindre le socket.

  **Correction : REJETER, pas écrêter.** `mbus_cmd_bytecount_ok()` exige **deux** faits — le compte
  est celui que la requête implique **et** il tient dans le corps réellement reçu (les deux ne
  s'impliquent pas : 2040 bobines demandent légitimement 255 octets et seuls 251 tiennent dans un
  corps). ⛔ **Écrêter aurait été « désamorcer ici, réarmer là »** : `WagoCtrl::read_bits()` boucle
  sur **`nb`**, pas sur ce qui a été reçu, et publierait des bits jamais reçus comme états d'entrée
  d'automate. Le `-1` rendu est celui que la bibliothèque rend déjà sur une mauvaise adresse
  d'esclave ; **tous** ses appelants le traitent déjà, et les IO gardent leur **dernière valeur
  connue** au lieu d'en inventer une. ⚠️ **Aucune signature publique n'a changé.**

  ⭐ **Le débordement est PROUVÉ, pas supposé** : la suite `tests/MbusResponse_test.cpp` (**14 cas**)
  lie les objets **réels** `mbus_cmd.o` et `mbus_rqst.o`, remplace **seulement**
  `mbus_sock_read()`/`mbus_sock_write()`, fabrique une trame au `byte_count` mensonger, et remet à
  `libmbus` des tampons qui sont des **tranches d'arènes sentinelles**. Mesuré sur le code livré :
  `firstByteWrittenPast(1) = 1` (le battement de cœur déborde son unique octet),
  `firstWordWrittenPast(3) = 3`, `byteAfterBuf(0) = 0x3C` au lieu de `0xC3` (**l'octet juste après
  `mbus_struct::buf` est écrit**), et **4 octets lus hors de `buf`** livrés à l'appelant à l'index
  **251**. Un débordement de tas ne plante pas de façon fiable : aucun oracle ici ne repose sur un
  plantage, et aucun n'a **`0`** pour valeur de passage (`-1` = « resté dans les bornes »).

  **Ce qui reste ouvert, et qu'il ne faut PAS croire fermé :**
  - ⛔ **FC 17** garde un **contrat documenté** (`mbus.h`) sur la taille de `slave_data` plutôt
    qu'un bornage : sa requête n'annonce **aucune** quantité, il n'y a rien à quoi confronter le
    compte. Sans appelant aujourd'hui ; le jour où il en a un, **passer la taille en paramètre**.
  - ⛔ **F-WAGO-7 n'est pas fermé par ce ticket** : `int count;` et `UWord address;` non initialisés
    dans **six** branches de `WagoExternProc_main.cpp` restent entiers. Seule la conséquence
    « recopie bornée par la réponse » disparaît. **Aucune ligne de `WagoCtrl.cpp` ni de
    `WagoExternProc_main.cpp` n'a été touchée.**
  - ⭐ **Ce qui a d'abord été fiché comme un « trou de filet » (M6) n'en est PAS un, et la
    correction vaut d'être lue.** Permuter les deux arguments de `mbus_cmd_bytecount_ok()` compile
    et laisse la suite verte — **parce que la permutation est sémantiquement NULLE** aux sites
    FC 01/FC 03 : le prédicat exige `bc == exp`, et sous cette égalité les deux ordres sont la
    **même** proposition. Aucun oracle ne PEUT être rouge, et en exiger un serait exiger qu'un test
    distingue deux programmes identiques. ⭐ **Mesuré par contraste : la même permutation au site
    FC 17** — où `expected` vaut `-1` — **est attrapée** (mutation M7, 1 rouge). Le filet mord
    partout où la permutation a un effet.
  - ⚠️ **En revanche la FORME reste un piège de site d'appel, et c'est le sujet de T3.31.**
    Inventaire `python3` des signatures de `libmbus` + `WagoBits.h` + `WagoCtrl.h` : **15 fonctions**
    portent au moins une paire de paramètres **de type identique**, donc permutables **sans
    avertissement**. **Trois sont des paires adresse ⇄ valeur sur des ÉCRITURES** :
    `mbus_cmd_force_single_coil(coil_addr, data)`, `mbus_cmd_preset_single_register(register_addr,
    preset_data)` et ⭐ `WagoCtrl::write_single_word(UWord address, UWord val)` — **la paire même de
    F-WAGO-7**. Deux de ces trois sont **dans `libmbus`**, donc **un typage fort côté Calaos seul ne
    les fermera pas**. S'y ajoute `WagoCtrl::read_bits(UWord address, int nb, …)`, permutable en
    silence par conversion (`UWord` → `int`) : **c'est le M6 d'origine de T3.30, toujours ouvert**.
    Tableau complet et classement par gravité : [T3.43](T3.43.md) §5.5bis.
  - ⛔ **Rien n'a tourné sous ASan ni contre un automate réel.**

- ⚠️ **[F-WAGO-12] — NOUVEAU, NON CORRIGÉ, adjacent à F-WAGO-8 et trouvé en le refermant (⚠️ ouvert sous le numéro **F-WAGO-9** sur `fix/fwago8`, renuméroté au merge : `T3.31` avait entre-temps attribué F-WAGO-9 à la paire adresse/donnée, cf. plus bas) :
  `mbus_cmd_addr_mdata()` déborde `mbus_struct::buf` de 8 octets sur le chemin d'ÉCRITURE, et sa
  longueur de trame TRONQUE.** `mbus_cmd.c` construit la requête dans `mbus->buf + MBUS_HDR_LEN` :
  **7** octets d'en-tête de corps puis `byte_count` octets de données, soit jusqu'à
  `6 + 7 + 255` = **268** dans un `buf` de **260** ⇒ **8 octets hors de la structure**. Et
  `mbus_rqst(mbus, 7 + byte_count)` prend un `mbus_ubyte` : `7 + 255 = 262` **tronque à 6**, donc
  la trame émise annonce une longueur absurde. ⚠️ **Ce n'est PAS F-WAGO-8** : ici la quantité vient
  de **la requête**, donc de l'appelant, pas du pair — la correction n'est pas la même. **Portée** :
  seules FC 15 et FC 16 passent par là, atteintes uniquement par
  `WagoCtrl::write_multiple_bits()`/`write_multiple_words()`, dont T3.30 §6.1 a mesuré que la
  chaîne est **morte de bout en bout**. Il faut `nb >= 2033` bobines ou `>= 128` registres pour
  l'atteindre. **Établi au source, rien n'a été exécuté sur ce chemin.** ⭐ **TICKET DÉDIÉ
  RECOMMANDÉ**, à traiter avec le premier appelant de `write_multiple_*`.

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

- 🟠 **[F-DEP-8] La publication ne dépend d'aucun test, et le fichier prétend le contraire — plus
  deux paquets pip que Dependabot ne peut pas atteindre.** Mesuré à [T3.22](T3.22.md) en parsant
  les trois workflows.

  **(1) Publication non gardée.** `.github/workflows/docker-publish-dev.yml` déclare
  `on: push: branches: [master]` ; son unique job n'a **ni `needs:`, ni `if:`**, et l'arbre ne porte
  **aucun** `workflow_run`. Les tests sont dans un **autre** fichier de workflow, donc hors de
  portée d'un `needs:`. ⇒ tout merge incrémente la version, crée un tag, publie
  `ghcr.io/calaos/calaos_base:dev` et dispatche un `build_deb`, **que `build-and-test` soit vert,
  rouge ou en cours**. ⚠️ Et `docker-publish-dev.yml:16` porte le commentaire
  `# run only when code is compiling and tests are passing` : il énonce une garantie que le fichier
  n'implémente pas. ⇒ **[T3.125](T3.125.md)** — corriger le commentaire est sans risque, poser un
  `workflow_run` ou une protection de branche est un **changement de livraison** (et le flottement
  `F-FLAKY-2` en ferait manquer).

  ✅ **(1) FERMÉE par [T3.125](T3.125.md)** : `docker-publish-dev.yml` part désormais sur
  `on: workflow_run: workflows: [ "Build and Test" ]`, le job exige `conclusion == 'success'`, et un
  contrôle mécanique (`.github/check-workflow-gating.py`, auto-test 10/5) refuse un nom cité
  inexistant. ⛔ **Rien de tout cela n'a jamais tourné chez GitHub** — voir la section « Nu » de la
  fiche.

  **(2) Deux paquets hors de tout manifeste.** `Dockerfile:58-64` (et son jumeau du stage `runner`)
  installe `roonapi` et `reolink-aio` **sans borne de version**, délibérément hors du
  `pyproject.toml`. Ce sont pourtant deux imports d'exécution réels
  (`Audio/ExternProcRoon_main.py`, `IO/Reolink/ExternProcReolink_main.py`). Aucun manifeste ne les
  nomme ⇒ **aucune entrée `dependabot.yml` ne peut les surveiller**, et la résolution du jour reste
  libre de les casser — le mode d'échec exact de `F-DEP-1`, réduit de six paquets à deux mais **non
  fermé**. Sur les 8 paquets pip de l'image livrée : **6 épinglés et surveillés, 2 flottants et
  invisibles**. À traiter avec `F-DEP-3` (même garde), ou en les déclarant dans un manifeste que le
  `Dockerfile` développe déjà.

  ⭐ **Vérifié à la revue de merge**, en parsant les trois workflows plutôt qu'à l'œil :
  `workflow_run` **0 occurrence dans tout `.github/`**, `needs:` **0 dans les trois workflows**,
  `if:` **0 dans `docker-publish-dev.yml`** (les 4 de l'arbre sont dans `ci.yml`). Le compte pip
  aussi : `[project].dependencies` **6** + `[project.optional-dependencies].test` **3** = **9
  épinglés sur 9** au manifeste ; le `Dockerfile` n'étend **pas** l'extra `test` pour l'image, donc
  l'image porte **6** paquets déclarés + `roonapi` + `reolink-aio` = **8**, dont **2 hors de tout
  manifeste**. ✅ **La moitié « commentaire menteur » est FERMÉE** par la revue de merge de
  [`T3.22`](T3.22.md) (issue A de [`T3.125`](T3.125.md)) — texte seul, YAML parsé inchangé. Restent
  ouvertes la publication non gardée et les deux paquets invisibles.

- 🟠 **[F-DEP-9] Le contrôle `dependabot-config` n'a pas d'auto-test, et il est aveugle au défaut le
  plus coûteux du fichier qu'il garde.** Mesuré à la revue de merge de [`T3.22`](T3.22.md) §10.3–10.4,
  script **ré-extrait du YAML du workflow**.

  **(1) Cassable en silence.** Deux contre-mutations du contrôle lui-même — jamais de la règle qu'il
  énonce : `"."` ajouté au n-uplet `pip` de sa table `MANIFESTS` ⇒ un répertoire déclaré **sans
  manifeste** passe à **rc 0**, et la sortie sur l'arbre livré est **identique au caractère près** ;
  `sys.exit(1 if errors else 0)` → `sys.exit(0)` ⇒ **les quatre** défauts passent. ⭐ C'est la forme
  mesurée à la revue de `T3.63` : une sonde se casse dans son **masquage lexical** et sa
  **reconnaissance**, là où un auto-test ne regarde pas. Les deux sondes de la famille qui en portent
  un (`check-echo-ceilings.sh`, `check-order-sentinels.sh`) l'ont précisément pour cette raison.
  ⚠️ **Aggravation propre à celui-ci** : il ne tourne pas dans `make check`, donc rien en local ne
  peut le voir tomber.

  **(2) Aveugle au fichier non parsable.** Contrôle **non muté**, il accepte (rc 0) : un
  `applies-to: "security-update"` (typo), une clef mal orthographiée sur une entrée, un `directory`
  portant un `*` derrière lequel il n'y a rien, un manifeste présent qui ne déclare rien, un
  manifeste qui est un répertoire. ⛔ Les deux premiers sont le mode d'échec le plus coûteux —
  Dependabot **ignore le fichier entier** quand il ne le parse pas, ce qui emporte **aussi la
  surveillance npm existante**. Le validateur de schéma SchemaStore les attrape (**1 erreur**
  chacun), mais il n'a été passé **qu'à la main, une fois**. ⇒ [`T3.126`](T3.126.md).



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

  ---

  ✅ **FERMÉ par [T3.39](T3.39.md)**, mergée sur `master` (`--ff-only`, historique linéaire). `TransportLimits::effectiveClientIp()` ne lit
  `X-Forwarded-For` que si le **pair TCP est le loopback**, via `isTrustedProxyPeer()` :
  `127.0.0.0/8` **entier**, `::1`, et la forme `::ffff:127.x` (test de **préfixe**, jamais de
  containement). **Un seul site**, donc les **deux** appelants — throttle de login des deux
  transports **et** cap `max_connections_per_ip` (`HttpClient.cpp:193`) — héritent de la garde
  sans duplication. Les capacités **(a)** et **(b)** sont fermées.

  **Ce que l'instruction a corrigé dans l'entrée ci-dessus** :
  - ⛔ **le remède qu'elle désignait était faux**. `listen_address = 127.0.0.1` gouverne **aussi**
    `UDPServer.cpp:58-61` (découverte + trames Wago) et couperait tout le parc RemoteUI :
    **écarté par décision utilisateur** (`DECISIONS.md`, 2026-08-25). La note de confiance de
    `getEffectiveClientIp()` et l'entrée `RELEASE_NOTES.md`, qui le conseillaient toutes deux,
    sont **réécrites** — une note qui désigne le mauvais remède est pire qu'absente.
  - ✅ **l'entrée disait vrai sur le reste** : `rfind(',')` prend bien la **dernière** entrée
    (celle du proxy, pas celle que le client contrôle), et `option forwardfor` est bien **sans**
    `if-none` sur les **trois** générateurs de config haproxy du produit
    (~~`calaos-os-conf/conf/haproxy-calaos.cfg:48`~~, `pkgdebs/haproxy/haproxy_pre:63`,
    `calaos_ddns/haproxy/haconfig.go:39-41`), tous sur **`127.0.0.1:5454`**.
    - ⛔ **CORRECTION (T3.39 R-provenance, 2026-08-25)** : la **conclusion tient**, la **première
      citation était mauvaise** et la réserve « provenance invérifiable » était **trop pessimiste**.
      `haproxy-calaos.cfg` n'est **pas** sur cette machine (dépôt distant seulement épinglé par un
      `PKGBUILD`) et il est **périmé** (ère Arch ; l'image livrée est Debian/dpkg). La topologie se
      vérifie **entièrement hors ligne** sur l'artefact réellement livré,
      `calaos-build/out/calaos-os.rootfs.tar` : `usr/sbin/haproxy_pre:38` `option forwardfor`
      (**`if-none` : 0 occurrence**), `usr/sbin/haproxy_pre:63`
      `server calaos-server 127.0.0.1:5454 check`, `usr/share/calaos-ddns/haproxy.template:34`
      idem, et **`--network=host` sur les deux units podman**
      (`haproxy.service:27`, `calaos-server.service:30`). Rien ne dépend d'un fichier distant.

  **Balayage complémentaire (`python3`, arbre entier)** : côté **C++**, `X-Forwarded-For` est la
  **seule** tête de provenance lue — **`X-Real-IP` : 0 occurrence**, `Forwarded` (RFC 7239)
  **jamais parsé**.
  - ⛔ **CORRECTION (T3.39 R1, 2026-08-25) — la phrase « Aucun autre site à durcir » était FAUSSE,
    et c'est la moitié du trou.** Le balayage n'avait porté que sur le C++. **`calaos_mcp`
    (`src/bin/calaos_mcp/python/calaos_mcp/auth.py:65-78`, MÊME DÉPÔT) lit `X-Forwarded-For`
    avec exactement la même règle « dernière ligne, dernière entrée » et SANS AUCUN test du pair**,
    et en clef son rate-limiter **et sa liste de bannissement**. Or `/mcp` est servi **sur le
    5454** et le proxy C++ transmet les octets **sans réécrire l'en-tête**. ⇒ les deux capacités
    que T3.39 ferme côté serveur restent **entièrement ouvertes** côté MCP. Consigné en
    **F-MCP-XFF-1** ci-dessous (fiche **[T3.42](T3.42.md)**) ; **la protection de F-XFF-1 est
    INCOMPLÈTE tant que F-MCP-XFF-1 n'est pas fermé**.

  **Ce qui reste ouvert, et qui est ACCEPTÉ** :
  - un attaquant **sur la machine elle-même** est loopback ⇒ il peut encore forger l'en-tête et
    choisir son seau. Écrit dans la note de confiance, pas corrigé ;
  - ⚠️ **un reverse-proxy DÉPORTÉ sur une autre machine voit son en-tête ignoré** ⇒ ses clients
    retombent dans le seau unique du proxy, soit le défaut T3.24 **pour cette topologie-là**.
    Aucun déploiement Calaos ne la produit (haproxy et `calaos_server` viennent du **même
    méta-paquet**, en **deux unités podman `--network=host`**), elle n'est atteignable qu'en
    éditant `/mnt/calaos/haproxy/haproxy.cfg` à la main ou le backend de `calaos_ddns`. **Une
    option `trusted_proxies` (défaut `"127.0.0.1,::1"`, donc iso-comportement) est PROPOSÉE mais
    volontairement NON introduite** — l'arbitrage appartient à l'utilisateur, cf. T3.39 §7.1 ;
  - le **request smuggling** à travers haproxy reste hors périmètre, inchangé.
  - ⛔ **ET CE QUI N'EST PAS ACCEPTÉ, ajouté par la revue de T3.39** : **F-MCP-XFF-1** ci-dessous.
    Ce n'est pas un arbitrage, c'est la **même faille non fermée sur l'autre moitié du port 5454**.

- ✅ **F-MCP-XFF-1 — [SÉCURITÉ, FERMÉ par [T3.42](T3.42.md)] le sidecar MCP croyait
  `X-Forwarded-For` sans aucun test du pair : le rate-limit et le bannissement `/mcp` étaient contournables depuis le LAN** (trouvé par
  la revue de T3.39, **mesuré de bout en bout**, **corrigé**).

  **Le chemin, mesuré et non repris sur parole** :
  1. `/mcp` est servi **sur le port 5454** — `WebSocket.cpp:74-127` renifle la première requête et,
     sur un chemin `/mcp`, bascule la connexion en tunnel (`McpProxyHandler`) ;
  2. le proxy transmet les octets **verbatim** : `McpProxyHandler::writeToSidecar()` (`:281-287`)
     fait une copie mémoire et un `write`, **aucune réécriture d'en-tête** ;
  3. le sidecar est joint par une **socket Unix** — `calaos_mcp/__main__.py:116-141` bind un
     `AF_UNIX` puis le passe à uvicorn par `fd=`, **jamais de host/port** ;
  4. ⇒ **`request.client` vaut `None`** côté Starlette. **Mesuré** sur les versions épinglées
     (uvicorn 0.34.2 / starlette 1.3.1) **et** en dernières versions, en `httpx(uds=)` **et** en
     octets HTTP/1.1 bruts sur une socket `AF_UNIX` : identique. Le repli
     `request.client.host if request.client else "unknown"` (`auth.py:77`) prend **toujours** la
     branche `"unknown"` ;
  5. ⇒ `_source_ip()` n'a **aucune** information de pair sur laquelle bâtir la garde de T3.39.
     Avec le **vrai** `BearerAuthMiddleware`, `rate_limit=5`, 20 requêtes sur la vraie socket :
     · clef fixe (sans en-tête → `"unknown"`) → **200 : 4 · 429 : 16**
     · `X-Forwarded-For` forgé et **tourné à chaque requête** → **200 : 20 · 429 : 0**
     **Contournement total du rate-limiter**, et symétriquement le bannissement d'une victime
     choisie en portant son adresse.

  **Pourquoi ce n'est PAS une garde de trois lignes, et donc pourquoi T3.39 ne l'a pas fermé** —
  c'est l'argument de périmètre, il est mesuré, pas supposé :
  - le sidecar **ne peut pas** se garder lui-même : `request.client` est `None`, l'information de
    pair n'existe **que** côté C++ ;
  - le C++ **ne peut pas** l'injecter à peu de frais : **seule la PREMIÈRE requête d'une connexion
    est inspectée**. `WebSocket.cpp:64-69` renvoie vers `mcpProxy->onClientData()` **avant** le
    bloc de reniflage, l'état `Proxied` n'a **aucune transition de retour**, et tout octet suivant
    part au sidecar sans lecture. ⇒ **assainir l'en-tête une fois à l'ouverture serait contourné
    par une seconde requête sur la même connexion keep-alive.** Un correctif juste demande un
    **découpage requête par requête dans le tunnel** — c'est-à-dire transformer un tunnel d'octets
    en proxy HTTP, avec la surface de smuggling que cela rouvre. **Refonte, pas garde.**

  **Forme du correctif proposée** (à instruire dans le ticket dédié) : que `McpProxyHandler`
  **retire toute ligne `X-Forwarded-For` cliente et pose la sienne** (`getClientIp()`) quand le
  pair n'est pas le loopback, **sur chaque requête du tunnel** — le sidecar garde alors sa règle
  actuelle sans une ligne de Python à changer. Alternative moins invasive à peser : refuser
  `/mcp` aux pairs non-loopback (mais `/mcp` depuis le LAN peut être légitime).

  ✅ **FERMÉ (T3.42).** ⛔ **La forme de correctif proposée ci-dessus était la bonne, mais pas
  suffisante seule** : le relais peut poser un `X-Forwarded-For`, un client aussi, et le sidecar
  n'a aucun moyen de les distinguer. Livré : `McpRequestFilter` (`McpRequestFilter.h`, header
  inline) **découpe le tunnel requête par requête** dans le sens client → sidecar, retire de
  **chaque** en-tête la ligne `X-Calaos-Client` du client et pose la sienne — identité
  `TransportLimits::effectiveClientIp()` de **cette** requête, précédée d'un **credential dérivé
  de `mcp_service_token`** (SHA-256, étiquette de séparation de domaine) qu'aucun client MCP ne
  voit. Le sidecar **ne lit plus `X-Forwarded-For` du tout**. Ce qui ne se découpe pas avec
  certitude (repli d'en-tête, `Content-Length` ambigu, *trailers*, `Upgrade`) **abat la
  connexion**. Mesuré de bout en bout, octets du vrai filtre C++ rejoués dans le vrai
  `BearerAuthMiddleware`, `rate_limit=5`, 20 requêtes à en-tête **tourné** :
  · pair LAN `192.0.2.55` → **200 : 5 · 429 : 15**, **un seul** seau (`192.0.2.55`)
  · pair loopback (haproxy) → **200 : 20 · 429 : 0**, **20** seaux ⇒ la granularité derrière le
  proxy est intacte.
  **Ce qui reste, et qui est accepté** : une requête qui joindrait la socket Unix sans passer par
  le relais n'a pas d'identité et tombe dans un seau partagé (socket `0660`, donc du boîtier
  lui-même) ; et, comme côté serveur, du code tournant **sur la machine** est loopback et choisit
  encore son seau.

  ⭐ **Vérifié au merge avec un vrai client MCP** : le SDK officiel (`mcp` 1.28.1, transport
  streamable-http) mène une session complète — `initialize`, `notifications/initialized`, flux SSE
  `GET`, `tools/list`, `tools/call`, `DELETE` de session — à travers le filtre compilé depuis
  l'arbre, sans un seul `400`, et les six requêtes portent l'identité écrite par le relais. Le SDK
  n'émet aucune des formes refusées (pas d'`Upgrade`, pas de *trailer*, `Content-Length` partout) ;
  un corps `chunked` traverse quand même, un `Upgrade` reçoit bien `400`, et deux requêtes
  **pipelinées** portant chacune un `X-Calaos-Client` client — credential correct compris — sont
  toutes deux réécrites. **La réserve « aucun client réel exercé » est levée.**

- ✅ **F-IP6-1 — [CORRECTION, FERMÉ par [T3.41](T3.41.md), PRÉEXISTANT] `HttpClient::getClientIp()` ne détectait pas la
  famille d'adresse : sur un pair IPv6 il rendait `"0.0.0.0"`, jamais l'adresse** (trouvé par la
  revue de T3.39, **mesuré**, le défaut précède T3.39).

  ✅ **FERMÉ.** `details::address<I>()` teste désormais `ss_family` (divergence assumée d'uvw
  amont), et les trois lecteurs de pair de l'arbre passent par `Calaos::tcpPeerAddress()`, qui
  **démappe** `::ffff:x.y.z.w`. ⭐ **La branche `::1` de `isTrustedProxyPeer()` est vivante et
  tenue** ; la branche `::ffff:127.x` reste de la défense en profondeur, et la note de
  `HttpClient.h` le dit exactement au lieu de laisser croire aux trois.

  ⭐⭐ **Le recensement que la fermeture a produit, et qui sert à qui touchera cette valeur** :
  `getClientIp()` est l'identité de **six** décisions de sécurité — plafond de connexions par
  source, backoff de login des deux transports JSON API, étranglement du sidecar MCP, limite de
  débit **et liste noire** du provisionnement RemoteUI, limite de débit de l'authentification HMAC
  (WS **et** OTA HTTP), et la porte de privilège `OtaHttpHandler::isLocalhost()` du `rescan` de
  micrologiciel — plus deux lignes de journal. **Cinq d'entre elles sont des clés de seau.**

  ⭐ **Le seau partagé est MESURÉ, pas déduit** (contre-mutation M5 de T3.41 : la lecture de master
  remise sur un serveur qui écoute vraiment en IPv6, deux pairs IPv6 distincts se refusent l'un
  l'autre au plafond réglé à 1). ⭐ **Mais toutes ces gardes échouent en FERMETURE** avec
  `"0.0.0.0"` : `isTrustedProxyPeer("0.0.0.0")` est faux donc l'en-tête est **écarté**, un seau
  partagé **resserre** la limite au lieu de l'ouvrir, et `isLocalhost("0.0.0.0")` **refuse** le
  rescan. ⇒ **F-IP6-1 était un défaut de DISPONIBILITÉ, pas de sécurité.** Le défaut de sécurité de
  ce chemin est `F-IP6-2`, ci-dessous, et c'est lui qui cachait celui-ci.

  ⚠️ **Le choix sur les adresses mappées n'a PAS été arbitré par `isTrustedProxyPeer()`** — mesuré :
  elle accepte les deux orthographes, elle est neutre. Il l'a été par `OtaHttpHandler::isLocalhost()`,
  qui **ignore** la forme mappée, et par le fait qu'une clé de seau doit désigner un client et non
  une configuration d'écoute. *À recopier : « quelle fonction décide » se lit à la source de toutes
  les fonctions concernées, pas de celle que le brief nomme.*

  **La cause** : `uvw`'s `details::address<I>()` (`src/lib/uvw/src/uvw/util.hpp:384-398`) demande le
  pair dans un `sockaddr_storage` puis le **`reinterpret_cast` en `sockaddr_in` sans regarder
  `ss_family`**. `getClientIp()` (`HttpClient.cpp:700-722`) essaie `peer<uvw::IPv4>()` d'abord et
  le renvoie dès qu'il est **non vide** — or il ne l'est jamais.

  **Mesuré** sur une connexion `::1` réellement acceptée (programme C, `getpeername` + `inet_ntop`,
  exactement la séquence d'uvw) :
  `famille = AF_INET6` · `peer<IPv6>().ip = "::1"` · `sin6_flowinfo = 0x00000000` ·
  **`peer<IPv4>().ip = "0.0.0.0"`** (non vide ⇒ il gagne) ⇒ **`getClientIp()` rend `"0.0.0.0"`**.

  **Conséquences** :
  - les branches **`::1`** et **`::ffff:127.x`** de `TransportLimits::isTrustedProxyPeer()` sont
    **du code mort en production** — elles restent en défense en profondeur, la note de
    `HttpClient.h` le dit désormais explicitement au lieu de promettre une protection qui n'opère
    pas ;
  - sur la configuration livrée, **aucun coût** : `listen_address` vaut `"0.0.0.0"`
    (`ConfigOptions.cpp:499`), l'écoute est IPv4, tout pair est `AF_INET`, la branche `127.` marche ;
  - ⚠️ mais si un opérateur pose **`listen_address = "::"`** (valeur légale d'une clé documentée),
    **tout** pair devient `AF_INET6`, `getClientIp()` rend `"0.0.0.0"` pour tout le monde, la garde
    le refuse, et **toute l'installation retombe dans un seau unique** — le défaut T3.24, pour
    cette configuration-là. La garde **échoue en fermeture** (aucune identité n'est forgée), donc
    c'est une régression de **disponibilité**, pas un trou de sécurité — **mais c'en est une contre
    master pour cette valeur**, et elle est écrite plutôt que tue.

  **Correctif livré** : le test de `ss_family` a été posé dans `details::address<I>()` plutôt que
  dans `getClientIp()` — `uvw::Handle::fileno()` existe et est public, mais il rend un descripteur
  **non initialisé** quand `uv_fileno()` échoue, et lire `getpeername()` sur un entier quelconque
  aurait échangé un défaut d'identité contre un pire.

  ⭐ **Exercé, pas déclaré couvert** : `core/PeerAddressFamily_test` monte un vrai `HttpServer` sur
  `::`, s'y connecte depuis `::1` et depuis deux adresses de 127.0.0.0/8, et **mesure le plafond de
  connexions par source** — pas une chaîne. ⚠️ **Sa fixture se vérifie elle-même** (`getsockname` du
  bout client de chaque socket) : sans ce garde-fou, la contre-mutation qui inverse la famille
  d'écoute laisse les cas IPv6 **verts** faute de client à mesurer. *Le foin qui exclut ce qu'on
  cherche, attrapé par un cas dédié.*

- ⛔ **F-IP6-2 — [SÉCURITÉ, OUVERT ET FERMÉ par [T3.41](T3.41.md), PRÉEXISTANT] toute valeur IPv6 de
  `listen_address` écoutait en réalité sur `0.0.0.0` : `listen_address = "::1"` ouvrait le serveur
  sur TOUTES les interfaces.**

  **La cause** : `uvw::TcpHandle::bind()` (et `UDPHandle::bind()`) est **templaté sur la famille et
  vaut `IPv4` par défaut** ; `HttpServer` et `UDPServer` l'appelaient sans paramètre de famille.
  `uv_ip4_addr()` **`memset` sa sortie et y pose `AF_INET` + le port AVANT** de rendre l'erreur de
  lecture du littéral, et **uvw jette ce code de retour**. Mesuré sur la libuv de l'image :

  ```
  uv_ip4_addr("::", 5454, &a)  ->  rc=-22 (EINVAL)  family=2 (AF_INET)  addr=0.0.0.0  port=5454
  ```

  **Conséquences** :
  - ⛔ **défaut OUVRANT** : l'opérateur qui restreint son écoute obtient l'inverse de ce qu'il
    demande, **et sur les deux serveurs** — l'API HTTP/WebSocket et la découverte UDP ;
  - la documentation de la clé promettait le contraire (« une adresse qui n'existe pas sur la
    machine empêche le serveur de démarrer ») ;
  - ⭐ **il rendait `F-IP6-1` entièrement latent** : aucune valeur de `listen_address` ne produisait
    un pair non-`AF_INET`, donc la réserve `"::"` de `RELEASE_NOTES.md` **décrivait un symptôme que
    personne ne pouvait observer**.

  ✅ **FERMÉ** : la famille est choisie sur le littéral (`Calaos::isIpv6Literal()`), aux deux sites.

  ⭐ **La chaîne a été re-mesurée maillon par maillon à la revue de merge**, sur la libuv de l'image
  (1.44.2), en lisant la socket d'écoute par un `getsockname()` **brut sur le descripteur** — donc
  sans passer par l'uvw qu'on juge :

  ```
  uv_ip4_addr("::1")  -> rc=-22  family=AF_INET  addr=0.0.0.0
  bind(défaut) listen_address="::1"           -> socket AF_INET 0.0.0.0 : connexion IPv4 ACCEPTÉE
  bind(famille) listen_address="::1"          -> socket AF_INET6 ::1    : connexion IPv4 REFUSÉE
  ```

  ⇒ le défaut ouvrant **et** sa fermeture sont l'un et l'autre observés, pas déduits.
  ⚠️ **Nuance de direction, qui corrige la formulation reçue** : `"::"` n'était pas *élargi* mais
  **rétréci** (`0.0.0.0` n'accepte pas l'IPv6, un client IPv6 ne pouvait pas entrer). L'élargissement
  ne concerne qu'une adresse IPv6 **précise** — `::1` ou l'adresse d'une seule interface.

  ✅⭐ **LES DEUX AUTRES FORMES SONT FERMÉES par [T3.106](T3.106.md)** — elles étaient MESURÉES ici,
  et le sont de nouveau, cette fois par un cas de l'arbre (`core/ListenAddressFallback_test`, qui
  lit la socket d'écoute au `getsockname()` sur le descripteur, un `fork` par valeur) :
  - une `listen_address` qui n'est **ni** IPv4 **ni** IPv6 (faute de frappe, nom d'hôte) : elle
    élargit toujours l'écoute — **arbitrage utilisateur du 2026-09-06 : retomber en le disant**,
    parce qu'une faute de frappe ne doit pas rendre un boîtier domotique injoignable — mais elle
    n'est plus muette : l'avertissement **nomme la valeur refusée** ;
  - ⭐ **et une adresse bien formée mais ABSENTE de la machine.** Le mécanisme est confirmé :
    `uvw` publie l'`ErrorEvent` **avant** que `HttpServer` n'ait posé son auditeur, l'erreur est
    perdue, et `listen()` **auto-lie le handle**. Mesuré sur master **avec le port demandé par la
    suite**, donc constaté et non déduit : `192.0.2.1` → port demandé **41909**, port obtenu
    **43483** ; `2001:db8::1` → **56629** demandé, **44217** obtenu, famille `AF_INET6`, adresse
    `::`. ✅ L'auditeur est désormais posé **avant** le `bind`, et le repli garde le **port
    configuré**. ⭐ **Re-mesuré à la revue de merge**, en remettant les cinq fichiers de production
    dans leur forme de `master` sur l'arbre livré : `192.0.2.1` → **59841** demandé / **39467**
    obtenu ; `2001:db8::1` → **59535** / **44519**, famille `AF_INET6`, adresse `::`. Chiffres
    différents, mécanisme identique — c'est donc bien le mécanisme qui est mesuré.

  ⛔⭐ **UNE QUATRIÈME FORME, TROUVÉE ET FERMÉE À LA REVUE DE MERGE de [T3.106](T3.106.md) : le
  repli lui-même échoue.** Quand l'adresse configurée est absente **et** que le port est déjà tenu
  par quelqu'un d'autre, le `bind` élargi échoue à son tour. Deux raisons se cumulaient :
  - ⛔ **libuv RETIENT `EADDRINUSE` au `bind` et ne le rend qu'au `listen()`/`recv()`.** Un helper
    qui écoute l'`ErrorEvent` autour du seul `bind` rend donc **vrai** sur le mode d'échec le plus
    banal d'un serveur — le port est déjà pris ;
  - ⛔ et l'auditeur du `listen()` était, lui, toujours posé **après** le `listen()`.

  **Mesuré** (`SO_ACCEPTCONN` sur `/proc/self/fd`) : **zéro** socket en écoute, **zéro** ligne
  d'erreur, et le constructeur imprimait quand même `Listening on port <N>`. ✅ **Fermé** : auditeur
  avant le `listen()`, échec **nommé**, et la ligne d'annonce n'est plus imprimée quand rien
  n'écoute. Tenu par `AFallbackThatCannotBindEitherSaysSoAndClaimsNothing`, dont la contre-mutation
  (auditeur remis après le `listen()`) rougit.
  ⚠️ `UDPServer` n'a pas le défaut : son `once<ErrorEvent>` est posé **avant** le `bind`.

  ✅⭐ **ET LA MOITIÉ UDP N'EST PLUS UN RAISONNEMENT** — c'était la réserve `R2` de la revue de
  `T3.41` (les deux familles de `bind` de `UDPServer` échangées ⇒ 0 rouge). La revue de merge de
  `T3.106` a lié `UDPServer.$(OBJEXT)` au harnais (aucune référence non résolue) : le même enfant
  construit aussi le serveur de découverte, sa socket est lue au `getsockname()`, et la
  contre-mutation qui lui fait ignorer l'adresse configurée **rougit**.

- ✅ **F-IP6-3 — [DISPONIBILITÉ, FERMÉ par [T3.107](T3.107.md)] sous une `listen_address`
  IPv6, `UDPServer` se lie à la bonne famille mais ne sait pas lire ses correspondants : la
  découverte ne répond plus et les entrées Wago/KNX poussées en UDP sont ignorées.**

  La correction de `F-IP6-2` ne portait que sur le `bind`. `UDPHandle::recv()` — comme `send()` —
  est **templaté sur la famille et vaut `IPv4` par défaut**, et son rappel emploie l'**autre**
  surcharge de `details::address<I>()`, celle qui prend un `sockaddr *` : la divergence CALAOS ne
  la touche pas. Un datagramme IPv6 est donc relu comme un `sockaddr_in`.

  **Mesuré** (image de dev, `listen_address = "::1"`, un vrai datagramme envoyé depuis `[::1]`) :

  ```
  expéditeur réel        [::1]:45552
  ce que processRequest() reçoit :  remoteIp="0.0.0.0"  remotePort=45552
  ```

  **Conséquences, toutes en FERMETURE** : `TCPSocket::GetLocalIPFor("0.0.0.0")` ne trouve pas
  d'interface ⇒ la réponse `CALAOS_IP` n'est jamais envoyée (`calaos_installer`, l'application
  mobile et les écrans ne trouvent plus le boîtier) ; et `WIDigitalBase::ReceiveFromWago()` compare
  `ip == host` ⇒ **aucune** entrée Wago/KNX poussée ne correspond, elles sont silencieusement
  perdues. Rien n'est attribué au mauvais équipement.

  ⚠️ **Ce n'est pas une régression sur la configuration livrée** (`0.0.0.0`, écoute IPv4 : chemin
  inchangé), mais **c'en est une contre master pour une `listen_address` IPv6** — où le service
  fonctionnait, précisément parce que le `bind` IPv6 n'avait jamais lieu. Écrit dans
  [`RELEASE_NOTES.md`](RELEASE_NOTES.md) plutôt que tu. ⇒ [T3.107](T3.107.md).

  ✅ **FERMÉ par [T3.107](T3.107.md)**, et **deux affirmations ci-dessus étaient fausses** :

  1. ⛔ **`GetLocalIPFor("0.0.0.0")` ne rend PAS vide.** Mesuré : `inet_pton(AF_INET, "0.0.0.0")`
     vaut **1**, le noyau ramène le `connect()` sur `0.0.0.0` à la boucle locale, et la fonction
     rend **`"127.0.0.1"`**. La réponse était donc **tentée**, avec un `sockaddr_in` qu'un
     descripteur `AF_INET6` refuse (`ENETUNREACH`, mesuré).
  2. ⛔⭐ **Et la conséquence réelle est plus lourde que celle qui était écrite.** `send()` publie
     son échec **sur le handle**, où le `once<ErrorEvent>` de `UDPServer` appelle `h.stop()` : le
     **premier `CALAOS_DISCOVER` arrêtait la réception pour la vie du processus**, entrées Wago et
     KNX comprises, y compris celles de correspondants qui n'avaient rien demandé. Une seule ligne
     au journal, qui ne nomme ni Wago, ni la découverte. ⇒ l'amplificateur est fiché à part,
     **`F-UDP-1`** ci-dessous, parce qu'il survit au correctif.

  ⭐ **Le correctif n'est pas de GARDER la seconde surcharge, et c'est mesuré.** Sur les **7** appels
  de `address<I>(const IpTraits<I>::Type *)` dans l'arbre, **2 seulement sont exposés** — les deux
  de `recvCallback<I>` ; les 4 de `uv_interface_addresses()` testent `sin_family` eux-mêmes, le
  septième est à l'intérieur de la surcharge gardée. Et la garder rendrait une `Addr` **vide** :
  honnête, toujours inutilisable. Le paramètre `I` de `recv()` est **une supposition qu'un
  propriétaire n'a aucun moyen de garder en phase avec son `bind`** ⇒ `details::sender()` lit la
  famille que le noyau a annoncée, ce qui ferme la seconde porte **par construction** à ses deux
  seuls sites exposés. *À recopier : une surcharge qui reçoit un pointeur déjà typé ne peut pas se
  défendre ; c'est son APPELANT qui doit cesser de deviner.*

  ⭐ **Un troisième étage, trouvé en rendant la lecture juste** : `TCPSocket::GetLocalIPFor()` ne
  connaît que l'IPv4 — repli `SIOCGIFADDR`, qui n'a pas de réponse IPv6 — et rendait `""` pour un
  correspondant `::1`. Fermé ici aussi. Le garde du correctif est tenu **des deux côtés** : la
  contre-mutation qui échange le test de littéral IPv6 contre celui d'IPv4 rougit la suite neuve
  **et** `core/ParseErrorSecret_test`, qui exerce `AVRRose` sur un hôte IPv4.

  ⛔⭐ **Et le mode d'échec était pire que silencieux** : `processRequest()` imprime
  `received input N state=X` **avant** d'émettre le signal, tandis que le non-appariement de
  `ReceiveFromWago()` (`ip == host`) n'écrit rien. Le journal affirmait donc que l'entrée était
  arrivée, au moment même où elle n'allait nulle part.

- ✅ **F-UDP-1 — [DISPONIBILITÉ, FERMÉ par [T3.135](T3.135.md), mesuré par [T3.107](T3.107.md)]
  un envoi UDP qui échoue arrête la RÉCEPTION, pour la vie du processus.**

  `UDPHandle::send()` publie son `ErrorEvent` **sur le handle**, et le `once<ErrorEvent>` de
  `UDPServer::createUdpSocket()` y appelle `h.stop()` — sans se réarmer, puisque c'est un `once`.
  Un envoi raté est l'affaire d'**un** correspondant ; il coûte ici **toute** la réception :
  découverte, `WAGO INT`, `WAGO KNX`, jusqu'au redémarrage. Une ligne d'erreur, qui ne nomme rien
  de ce qui est perdu.

  ⚠️ **`T3.107` a retiré la CAUSE, pas la règle** : plus aucun envoi n'est refusé par sa propre
  famille, mais un correspondant devenu injoignable entre sa requête et la réponse (route perdue,
  interface descendue) produit le même `ErrorEvent`. ⇒ [T3.135](T3.135.md).

  ✅ **Fermé le 2026-09-07.** La perte est reproduite au socket, sans défaut de famille : un serveur
  lié à `127.0.0.1` répondant à `192.0.2.1`, que le noyau refuse de router (`EINVAL`). Après une
  seule réponse refusée, l'entrée KNX suivante, la réponse à `CALAOS_DISCOVER` et l'entrée Wago
  d'après valaient toutes les trois le sentinelle `-`. ⭐ **L'arbre ne distingue PAS les deux échecs
  au rappel** — même événement, même charge, publiés au même endroit, et `send()` ne rend pas sa
  requête : la distinction se fait sur les datagrammes dont uvw doit encore une complétion. Un envoi
  raté nomme son correspondant et n'arrête rien ; seule une erreur sans envoi en attente arrête
  encore, en disant ce qu'elle emporte. ⛔ **Cette branche-là n'est mesurée par rien**
  ⇒ [`T3.149`](T3.149.md), et le tampon d'envoi non possédé ⇒ [`T3.142`](T3.142.md).

- ⛔ **F-UDP-2 — [ROBUSTESSE, OUVERT, mesuré par la revue de merge de [T3.107](T3.107.md)] l'adresse
  vide que `details::sender()` peut rendre n'est distinguée par personne.**

  Le patch Calaos rend un `Addr{}` sur `addr == nullptr` et sur une famille inconnue. `UDPServer`
  est son seul consommateur dans l'arbre et ne teste pas cette vacuité : un `remoteIp` vide
  traverserait `ip == host` sans la satisfaire — donc sans un mot — puis partirait dans un envoi
  que le noyau refuserait.

  ⚠️ **Inatteignable en UDP aujourd'hui** : le noyau fournit toujours un `sockaddr` avec un
  datagramme reçu, et les deux familles couvrent tout ce que l'arbre lie. Ce n'est pas un symptôme,
  c'est une **hypothèse non écrite** — et la série a déjà montré ce que deviennent les hypothèses
  que rien ne tient. ⇒ [T3.139](T3.139.md).

- ✅ **F-ROSE-1 — [CONFINEMENT, FERMÉ par [T3.109](T3.109.md)] le port de notification HiFi Rose
  ignorait `listen_address`.**

  `AVRRoseNotifServer` liait `"0.0.0.0"` en dur sur le port 9284, construit dès qu'une
  configuration porte un ampli AVRRose. Mesuré au descripteur : `listen_address = 127.0.0.1` ⇒
  socket `AF_INET 0.0.0.0:9284`. Un opérateur qui narrowait son écoute gardait un port ouvert sur
  son LAN, **sans une ligne**.

  ✅ **FERMÉ** : l'adresse vient de la clé, par les mêmes deux aides que l'API et la découverte
  (`listenAddressOrWildcard`, `bindListenAddress`). ⚠️ **Le confinement a un prix, et il est dit
  deux fois** : l'ampli pousse *vers* ce port, donc une adresse qu'il ne peut pas joindre ramène
  l'état au sondage de repli de 30 s — la ligne d'écoute nomme l'adresse liée, et
  `docs/16_config_options.md` porte la phrase.

- ✅ **F-ROSE-2 — [SILENCE, FERMÉ par [T3.109](T3.109.md)] le serveur de notifications annonçait
  une écoute qu'il n'avait pas.**

  L'auditeur d'`ErrorEvent` était posé **après** le `bind` et après le `listen`. Mesuré, port 9284
  tenu par un tiers : **0 socket d'écoute** dans le processus, et
  `Push notification server listening on port 9284` **présente** au journal. La fiche l'affirmait ;
  c'est vrai.

  ✅ **FERMÉ** : auditeur avant le `bind`, piège d'erreur autour du `listen` (libuv retient
  `EADDRINUSE` du `bind` pour le rendre au `listen`), et la ligne de succès n'est imprimée que si
  l'écoute a eu lieu — sinon une ligne d'erreur qui nomme ce qui est perdu.

- ✅ **F-EXTPROC-14 — [SILENCE, FERMÉ par [T3.109](T3.109.md)] le tube des sidecars ne lisait
  aucune erreur du tout.**

  `bind(sockpath)` puis `listen()` sans **aucun** `ErrorEvent`. Mesuré sous épuisement de
  descripteurs : `uv_pipe_bind` rend `EMFILE`, `uv_listen` rend `EINVAL`, **0 socket d'écoute**, et
  la seule trace était une ligne de débogage annonçant le chemin sur lequel rien n'écoutait. Aucun
  sidecar — les sept familles — ne peut alors se connecter.

  ✅ **FERMÉ** : un auditeur avant le `bind`, effacé après le `listen`, et une ligne d'erreur qui
  nomme le chemin et la famille de sidecar qui ne se connectera jamais.

- ⛔ **F-ROSE-3 — [ROBUSTESSE, OUVERT, ouvert PAR le correctif de [T3.109](T3.109.md)] l'adresse
  annoncée à l'ampli et l'adresse liée ne sont comparées par personne.**

  `AVRRose::registerDevice()` publie `connectIP = TCPSocket::GetLocalIPFor(host)`, dérivé de la
  route vers l'ampli ; l'adresse liée vient désormais de `listen_address`. Deux calculs, deux
  endroits, aucune comparaison. Sous `listen_address = 127.0.0.1` l'inscription réussit, l'ampli
  tente, la connexion est refusée, l'état retombe sur le sondage de 30 s — et les deux moitiés de
  l'explication sont au journal, à deux endroits que personne ne rapproche.

  ⚠️ **C'est le correctif qui rend l'écart possible** : avant lui, l'écoute était toujours sur
  toutes les interfaces. Il l'a documenté dans la clé, il ne l'a pas rendu observable au démarrage.
  ⇒ [T3.140](T3.140.md).

- ⛔ **F-UVW-1 — [ROBUSTESSE, OUVERT, mesuré par la revue de merge de [T3.109](T3.109.md)]
  `resource<>()` rend `nullptr` quand `init()` échoue, et aucun des 45 sites ne le regarde.**

  `uvw::Loop::resource<R>()` fait `ptr = ptr->init() ? ptr : nullptr` (`loop.hpp:250-254`).
  L'arbre l'appelle **45 fois** hors `uvw/` et déréférence le retour **45 fois** sans un test.

  ⭐ **44 de ces sites sont sûrs par accident** : `uv_tcp_init`, `uv_pipe_init`, `uv_udp_init`,
  `uv_timer_init`, `uv_async_init`, `uv_idle_init`, `uv_signal_init` n'ouvrent aucun descripteur
  sur Linux et ne peuvent pas échouer. ⛔ **Le 45ᵉ, non** : `UrlDownloader.cpp:333` demande un
  `PollHandle` sur une socket que libcurl vient de rendre, donc `uv_poll_init_socket`
  (`poll.hpp:91-95`), qui échoue pour `EINVAL`, `EBADF` ou un descripteur déjà surveillé —
  et `:337` déréférence quatre lignes plus bas. Le mode d'échec est un **SIGSEGV du serveur**,
  sur le chemin de tout téléchargement HTTP.

  ⚠️ **Établi par lecture, jamais provoqué en service.** ⇒ [T3.141](T3.141.md).

- ⛔ **F-UDP-3 — [COUVERTURE, OUVERT, mesuré par la revue de merge de [T3.107](T3.107.md)] une
  assertion manquante rétrécit l'ensemble rouge de M3.**

  `TheServerStillReadsAfterAnsweringADiscovery` exige l'absence de `UDP server error` pour `::1` et
  `::`, **pas pour `127.0.0.1`**, alors que la contre-mutation M3 la produit aussi sur cette forme.
  Le cas voit donc moins que ce qu'il pourrait voir.

  ⚠️ **Le critère de clôture n'est pas « l'assertion est ajoutée »** mais « M3 rejouée fait
  **grossir** l'ensemble rouge ». Un ensemble inchangé après ajout veut dire que l'assertion neuve
  est morte — c'est exactement le résultat qu'il ne faut pas publier comme un succès.
  ⇒ [T3.139](T3.139.md).

- ⛔⭐⭐ **F-UDP-4 — [SÉCURITÉ, OUVERT, mesuré par la revue de merge du lot 147, 2026-09-07] le
  serveur ÉMET de la mémoire libérée vers un pair du réseau local non authentifié.**

  `uvw::UDPHandle::send(…, char *, …)` (`udp.hpp:396-401`) construit sa requête avec un
  **suppresseur qui ne fait rien** : le tampon n'est ni copié ni possédé.
  `UDPServer::processRequest()` lui donne le `c_str()` d'une `string` **locale**. Tant que la file
  d'envoi est vide, `uv__udp_sendmsg` part avant le retour ; dès qu'un envoi est déjà en attente, la
  requête est mise en file et lue au tour de boucle suivant, quand la `string` n'existe plus.

  **Mesuré** : trois `CALAOS_DISCOVER` envoyés dos à dos — une lecture en rafale de libuv en avale
  jusqu'à **32**, donc les trois réponses sont postées dans le même tour. La **1ʳᵉ** réponse est
  correcte ; les **2ᵉ et 3ᵉ** sont **19 octets de tas libéré**, émis **au correspondant**
  (`e28d1eb1ba5500004050…` au lieu de `CALAOS_IP <adresse>`). ⭐ **Sans ASan** — le défaut ne demande
  aucun outil pour se produire, seulement pour être nommé. ⭐ **À l'identique sur `master`** ⇒
  **préexistant** : [T3.135](T3.135.md) ne l'introduit pas et n'y change rien, il le rend seulement
  observable.

  ⛔ **La gravité n'est pas « plantage possible ».** C'est une **divulgation de mémoire du serveur
  vers un pair du LAN**, déclenchable par la **découverte UDP** — le seul verbe servi **sans
  authentification** —, répétable à volonté, et dont le contenu dépend de ce que l'allocateur vient
  de libérer. ⇒ [T3.142](T3.142.md).
  *À recopier : « non mesuré » et « inoffensif » ne sont pas la même phrase ; ici la mesure coûtait
  trois datagrammes.*

- ⛔ **F-UDP-5 — [ROBUSTESSE, OUVERT, ouvert PAR le correctif de [T3.135](T3.135.md), lot 147] un
  chemin de journal non borné et piloté par le réseau.**

  `once<uvw::ErrorEvent>` est devenu `on<>` — c'est le bon geste, un boîtier perdant deux
  correspondants n'en signalait qu'un (`M2` ⇒ 3 cas rouges) — mais l'auditeur n'a **aucun plafond** :
  **100 réponses refusées produisent 100 lignes `[ERR]`**, une par datagramme, et le nombre de
  datagrammes est décidé par qui parle sur le réseau local, sans authentification.

  ⚠️ `master` écrivait une ligne puis appelait `h.stop()` : le défaut de disponibilité que `T3.135`
  corrige était aussi, **par accident**, sa propre borne. Le correctif retire l'arrêt et ne met rien
  à la place. ⭐ C'est la **même forme** que celle que [T3.105](T3.105.md) vient de fermer sur les
  relances de sidecar. ⇒ [T3.152](T3.152.md).

- ⛔ **F-UDP-6 — [COUVERTURE, OUVERT, mesuré par la revue de merge du lot 147] le câblage
  `processRequest → sendTo` n'est épinglé par aucun cas.**

  La contre-mutation `M6` de [T3.135](T3.135.md) annonçait *« le site d'envoi de `master` »* ; elle
  retire la **comptabilisation**, pas l'**appel**. La mutation que ce libellé annonçait — le site
  d'envoi de `master` **remis littéralement** dans `processRequest()` — laisse
  `core/UdpSendFailureIsolation_test` **9/9 verte**. Les neuf cas entrent par le transport, aucun ne
  part d'une requête reçue pour exiger qu'une réponse soit **postée**. ⇒ [T3.151](T3.151.md).
  *À recopier : une contre-mutation qui remet « la forme `master` » d'un site doit remettre ce que le
  site FAIT, pas ce qu'on en a extrait pour mesurer — sinon on épingle son propre instrument.*

  ⭐ **Et une bonne nouvelle du même tour** : la « fenêtre de mauvaise attribution » que `T3.135`
  déclarait non mesurée n'a **aucun producteur connu**. Une socket UDP **non connectée** ne reçoit
  pas les ICMP — sonde noyau : la lecture rend `EAGAIN`, jamais `ECONNREFUSED` —, et la socket du
  serveur n'est jamais `connect()`ée. Ce n'est donc pas un risque de taille inconnue ; c'est aussi
  pourquoi [T3.149](T3.149.md) est difficile à fermer par un cas.

- ✅ **F-PYTEST-1 — [FAUX VERT, FERMÉ par [T3.47](T3.47.md)] `tests/python/test_auth.py` était
  silencieusement SAUTÉ par `make check`, qui restait vert** (trouvé en mesurant F-MCP-XFF-1).

  ✅ **FERMÉ.** Le correctif demandé ci-dessous — sortir **77** plutôt que **0** et *affirmer* le
  nombre de cas exécutés — est livré : `python-suite-runner.py` compte les cas déclarés et les cas
  exécutés, publie les deux et fait dépendre son code de sortie de leur égalité, et
  `check-python-tests-reporting.sh` est le méta-oracle qui garde cette propriété. Mesuré au merge
  de T3.42 : `run-python-tests: suites=6/6 cases=45/45`, `PASS`. La disparition du symptôme (image
  de développement portant `pytest`) n'aurait pas suffi à fermer l'entrée ; c'est le fait qu'une
  dépendance manquante produise désormais un `SKIP` visible, jamais un `PASS`, qui la ferme.
  `tests/run-python-tests.sh:50-60` lance `pytest` **s'il est disponible**, sinon retombe sur
  `unittest discover -p 'test_t116_*.py'`. Sur cette machine `/usr/bin/python3 -m pytest` →
  `No module named pytest`, et `tests/run-python-tests.sh.log` du dernier `make check` réel porte :
  « *skipping pytest-only suites (test_auth.py, test_extern_proc.py, test_logger.py)* », `.trs` =
  **PASS**. ⇒ **11 cas de `test_auth.py` — précisément ceux du throttle MCP — ne s'exécutent pas,
  et la suite ne le signale que dans un log que personne ne lit.** Rejoués à la main sous un venv
  avec les vraies dépendances : **11 passent** (le code est bon ; c'est la *mesure* qui manquait).
  ⭐ **Et la portée dépasse ce fichier — mesuré (`python3`)** : sur les **6** suites de
  `tests/python/` (**42** cas), seules **3** (**23** cas) sont ramassées par le motif
  `test_t116_*.py` ⇒ ⭐ **3 fichiers sur 6 (50 %) et 19 cas sur 42 (45 %) ne s'exécutent pas**
  pendant que `make check` affiche `# FAIL: 0`. C'est une **huitième variante de faux vert**,
  d'une **famille distincte** des sept autres — *une suite qui s'auto-saute est indistinguable
  d'une suite qui passe* — **enregistrée comme telle à côté de `F-HARN-1`** (voir « LA HUITIÈME
  VARIANTE DE FAUX VERT » plus bas). Correctif : faire **échouer** `run-python-tests.sh` quand
  `pytest` manque, ou sortir **77 (`SKIP`)** — jamais **0** — et **affirmer** le nombre de cas
  exécutés.
  ⚠️ Corollaire pour F-MCP-XFF-1 : **aucun** test ne mentionne `_source_ip` ni `request.client`, et
  **tous** les cas de `test_auth.py` fournissent un `X-Forwarded-For` — le repli `"unknown"` et le
  modèle de menace « sans proxy » sont **entièrement non testés**.

- ⚠️ **F-MCP-SNIFF-1 — [SÉCURITÉ, OUVERT, PRÉEXISTANT] la détection de smuggling de `/mcp` se
  contourne avec ~8 Kio de bourrage d'en-têtes** (trouvé en mesurant F-MCP-XFF-1, **hors périmètre
  T3.39**, `sniffRequest()` **non corrigé**).
  ⚠️ **Atténué depuis T3.42, entrée maintenue ouverte** : le filtre du relais rejoue les mêmes
  indicateurs (`Content-Length` en double, `Content-Length` + `Transfer-Encoding`) sur **chaque**
  en-tête et abat la connexion au-delà de 8 Kio sans fin de bloc, donc le bourrage mesuré
  n'atteint plus le sidecar. `sniffRequest()` lui-même rend toujours `Mcp` sans contrôle dans ce
  cas, et la branche morte `"/mcp?"` est toujours là.
  `McpProxyHandler::sniffRequest()` (`:144-156`) ne cherche `detectSmuggling()` que s'il a **vu**
  la fin du bloc d'en-têtes ; si `\r\n\r\n` n'est pas trouvé **et** que le tampon dépasse
  `SNIFF_LIMIT` (**8192**, `:34`), il rend `Mcp` **sans aucun contrôle**. Vérifié en compilant le
  **texte réel** des lignes 32-157 dans un harnais : un bloc portant **deux `Content-Length`** et
  bourré au-delà de 8 Kio sans terminaison passe en `Mcp`. `MCP_SNIFF_LIMIT` vaut **16384**
  (`WebSocket.h:101`) et `MaxHeadersSize` **32768**, donc c'est bien la limite de 8 Kio qui tombe
  la première. Mineur au passage : `McpProxyHandler.cpp:134-135` teste `"/mcp?"` sur un chemin dont
  la query a **déjà** été retirée (`:131-132`) — **branche morte**.

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
  ⭐⭐ **CE N'EST PLUS LATENT, C'EST MESURÉ** (2026-09-06, [T3.57](T3.57.md)) : sous
  `--enable-asan`, **`core/IncomingLogStockLevel_test` sort en 1** après avoir passé ses 19 cas —
  *heap-use-after-free*, `~ListeRoom` → `~Room` → `ListeRoom::detachIOFromRules()` →
  `ListeRule::Remove()` parcourt le `vector<IOBase *>` dont `~ListeRule` a **déjà libéré** le
  tableau. Un binaire de test n'a pas le `main()` du serveur, donc il n'a pas l'ordre de
  déclaration qui sauve. **Rejoué sur `master`** : même rapport, même adresse — **antérieur**, et
  le seul rouge du `make check` sous ASan.
- **Back-pointers — NE PAS convertir**, et ⛔⭐ **LA RAISON ÉCRITE ICI ÉTAIT FAUSSE ; corrigée le
  2026-09-06 par [T3.57](T3.57.md).** La consigne, elle, **tient toujours** : convertir coûterait
  **~104 sites** dans `AutoScenario.cpp` (recomptés : `ioScenario` 19, `ioIsActive` 18,
  `ioScheduleEnabled` 12, `ioStep` 15, `ioTimer` 20, `ioTimeRange` 17, `roomContainer` 3 — les
  « ~40 » d'E4.2f étaient sous-estimés) que **E4.6c doit de toute façon retirer**.
  ⛔ **Ce qui est faux, c'est « pour zéro danger réel ».** La phrase mesurait **qui détruit** ces
  IOs — et cette moitié est encore vraie, recensée appelant par appelant en T3.57. Mais **T3.18 a
  mis `ListeRoom::refreshBrokenScenarios()` à la fin de CHAQUE `deleteIO()`**, et
  `AutoScenario::stopBrokenRun()` y **lit** `ioIsActive` puis **écrit** `ioStep` et `ioTimer` pour
  **tous** les scénarios vivants : le danger vient désormais de **qui les LIT**, pas de qui les
  détruit, et le pointeur pendant est déréférencé **dans le `deleteIO()` lui-même**.
  ⭐ **Et une porte existe, ce n'est pas un chemin de modèle** : rien ne refuse un
  `autoscenario_uid` dupliqué dans `io.xml`, deux scénarios partagent alors une seule machinerie,
  et `autoscenario delete` sur l'un détruit celle de l'autre ([T3.117](T3.117.md)).
  ✅ **Fermé par T3.57 SANS conversion** : `Room::RemoveIO(del=true)` et `~Room` préviennent les
  scénarios avant de détruire (`AutoScenario::forgetIO()`/`forgetRoom()`). **Ne convertissez
  toujours pas** — et ne recopiez plus « zéro danger réel ».
  ℹ️ Deux des neuf noms de la liste d'origine n'étaient pas des back-pointers :
  `ScenarioAction::io` est une **valeur de retour** construite sur place par `getRealAction()`, et
  `ioScenario` / `IO/Scenario.h:38 auto_scenario` sont des liens de **propriété** — `~AutoScenario`
  déréférence `ioScenario`, donc il ne doit surtout pas être annulé.

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

## T3.20 — suites

> ⚠️ **La branche `fix/t3.20` a été ENTERRÉE** (refus de `modify` abandonné par décision,
> `E4.6.md` §Q1 ; D7 livre valider-puis-muter en E4.6d). **Ces mesures-là survivent** : c'est sur
> elles que s'appuie le raisonnement d'E4.6 Q1. Le code, lui, n'a jamais été mergé.

### ⭐ Le round-trip d'une **UI** ne peut PAS déclencher le refus de `modify` — et c'est voulu

Le constat de `T3.20.md` (étape 1 de la chaîne R3) dit que le client renvoie le payload « **avec
l'étape morte**, telle qu'elle est rendue par `Scenario::toJson()` ». **Mesuré : ce n'est pas le
cas.** `Scenario::toJson()` saute les actions dont l'IO ne résout pas (`if (!sa.io) continue;`,
`IO/Scenario.cpp`), et `JsonApiScenario_test.ABrokenStepSilentlyLosesItsActionFromThePayload` le
pinne depuis E4.0c : l'étape est rendue **présente mais vide**. Le payload qu'une application relit
ne **nomme donc jamais** l'id manquant, et le refus de T3.20 — qui porte sur les ids **cités** — ne
peut pas s'y déclencher.

Conséquence à connaître, et **elle n'est pas un trou** : une application qui réinjecte tel quel ce
qu'elle vient de lire obtient toujours `success`, le scénario est reconstruit **sans** l'action
morte, et `isBroken()` retombe à faux. C'est exactement le comportement que le principe de
conception du ticket **veut** : *le refus porte sur ce qu'on écrit*, et un payload qui ne cite
aucun IO absent est, par définition, une **réparation** (l'utilisateur laisse tomber l'action).
Le drapeau `disabled_missing_io` restant collant, le scénario ne repart pas tout seul : il passe en
« réparé, en attente de réactivation », et c'est `autoscenario reenable` — donc un geste humain
explicite, après avoir vu `missing_ios` dans le payload (T3.18) — qui conclut.

Ce que le refus attrape réellement, c'est le client qui garde **sa propre copie** du scénario :
`calaos_installer`, qui lit `rules.xml` où `ActionStd::SaveToXml()` conserve l'id disparu
**verbatim** (E4.2e). C'est là que « blanchir » était possible sans que personne ne voie rien.

> **Leçon générale : avant d'écrire une validation d'entrée, vérifier ce que la sortie contient
> réellement.** Une validation calibrée sur un payload que le serveur ne produit jamais protège
> contre un geste que personne ne fait.

### La bascule de `del_param` fait de `get_params()` la dernière porte ouverte

`JsonApi.cpp` routait `del_param` par `o->get_params().Delete(...)` : la référence **mutable** que
`IOBase::get_params()` rend publiquement. T3.20 ferme ce site précis, mais **`get_params()` reste
une porte d'écriture non gardée** pour tout l'arbre — `IOBase.h` l'annonce depuis T1.11 (« *callers
must not use it to change "id"* »). Recensé après le ticket : plus aucun appelant de `get_params()`
ne modifie `"id"` ou `"disabled_missing_io"` **hors** de `Scenario::writeDisabledMissingIoParam()`,
qui est la porte moteur voulue. Le jour où un troisième param protégé apparaîtra, la bonne réponse
n'est plus une garde par classe mais un `Params` en lecture seule (ou une liste blanche
centralisée) — cf. « Hors périmètre » de `T3.20.md`.

### Le piège 5 du ticket n'était PAS silencieux — mesuré, contre l'annonce

`T3.20.md` prévenait : oublier la porte moteur ferait de `setDisabledMissingIo()` un no-op
silencieux, « **et rien ne rougirait** — les tests de T3.18 qui vérifient le drapeau passent par le
booléen en mémoire ou par `io.xml` ». **Contre-mutation exercée** (porte moteur retirée,
`setDisabledMissingIo()` repassé par `set_param`/`del_param`, qui refusent désormais) : **8 cas
rougissent**, dont **cinq de T3.18 lui-même** — `LosingTheIoOfAStepDisablesTheWholeScenario`,
`FlagSurvivesAStartupSaveReloadCycle`, `ReenableClearsTheFlagOnceTheIoIsBackAndTheScenarioRunsAgain`,
`ReenableCommandRefusesWithTheIdsAndSucceedsOnceRepaired`, `ThePayloadTellsTheFourStatesApart`.

La raison est exactement celle que le ticket citait comme rassurante et qui ne l'est pas : ces cas
lisent le drapeau **par `io.xml`**, or c'est justement l'écriture vers `io.xml` que la porte moteur
porte. Le booléen en mémoire, lui, aurait bien menti — mais aucun de ces cinq cas ne s'en contente.

> **Leçon générale : « le test passe par X, donc il ne verra pas une régression de Y » est une
> hypothèse, pas un constat.** Elle se vérifie en mutant, pas en lisant. Ici elle était fausse dans
> le sens favorable ; elle aurait pu l'être dans l'autre.

### Non touché : la renumérotation des étapes placées après l'étape `end`

`AutoscenarioCreateDropsTheActionsOfAStepPlacedAfterTheEndStep` (bug gelé d'E4.0c :
`index_act = idx` utilise l'index du **tableau JSON** alors que `addStep()` numérote par le nombre
d'étapes standard déjà créées) est **inchangé** par T3.20. Le helper de validation ne regarde que
les ids, jamais les index — un payload dont toutes les actions résolvent passe la validation et
perd quand même ses actions mal indexées, exactement comme avant.

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
- **(b) `IO/KNX/KNXExternProc_main.cpp:144-147` et `:157-160`.** ⛔ **La mention « plus des accès
  `tokens[1..2]` hors bornes » qui figurait ici est FAUSSE** — `Utils::split` pade
  (`StringUtils.cpp:210`), voir l'entrée dédiée ci-dessus. Corrigé par **T3.33**, qui l'a
  reverifié au source et figé le padding dans un cas
  (`KNXExternProcAddr.SplitPadsTheTokenListToThree`).
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

### F-OLA-6 ✅ **FERMÉ** par [`T3.38`](T3.38.md) — une incohérence hors périmètre, signalée et non corrigée — **confirmée par la revue**

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
« **la neuvième** » : le compte d'alors n'avait énuméré ni `F-BUILD-1` ni `F-TYPE-3`. Les **quatorze**
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
harnais est **exactement** ce qui a produit les **quatorze** variantes de faux vert/faux rouge de
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
  [T3.43](T3.43.md) §5.5bis (⭐ fiche et code de `fix/fwago8` mergés), et T3.31 ne la ferme pas — il ferme les **trois sauts Calaos**
  au-dessus (`WOAnalog` → `WagoMap` → `WagoWire`, puis `WagoExternProc_main` → `WagoCtrl`), pas
  celui-là. **Mesuré comme résiduel** : contre-mutation M5 de T3.31, `mbus_cmd_preset_single_register(mbus, 1, (mbus_uword)val, address)` **compile, rc=0**.
  ⭐ **Et ce n'est PAS une impossibilité technique, contrairement à ce que la première rédaction du
  correctif affirmait** : mesuré au `gcc -std=c11`, une `struct` à **un champ** ferme une
  permutation en **C** exactement comme en C++ (`error: incompatible type for argument 1`). Ce qui
  arrête T3.31, c'est **le coût et la propriété** : sept signatures d'une bibliothèque **tierce
  importée** (`$Id: mbus_conf.h,v 1.1.1.1 2003/…`), déjà divergée d'amont, répartie sur quatre
  fichiers `.c`, et que **rien dans l'arbre n'exécute** — la correction serait vérifiée **par la
  compilation seule**. [T3.43](T3.43.md) §3 a refusé le même changement pour la même raison, et un
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
## ⭐ LA LISTE CANONIQUE DES VARIANTES DE FAUX VERT / FAUX ROUGE — **quatorze**, numérotées

*(Établie le 2026-08-25 en fermant les réserves de la 2ᵉ revue de [T3.44](T3.44.md). ⚠️ **C'est LA
référence** : toute mention d'un compte ou d'un rang ailleurs dans `docs/refactoring/` doit
s'y accorder. Avant elle, trois numérotations incompatibles coexistaient — « cinq » (la seule
famille `_DEPENDENCIES`), « sixième » (revendiqué par **deux** findings différents — `F-HARN-1` et
`F-TYPE-3`, arbitré ci-dessous), et un compte
global « septième / huitième / neuvième » qui **n'avait jamais énuméré `F-BUILD-1` ni
`F-TYPE-3`**.)*

⚠️ **Le fil commun des quatorze** : *l'arbre a l'air juste à l'endroit qu'on regarde.*

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
| **12** | ⭐ **La restauration qui ne restaure RIEN** — dans un **worktree git**, le `.git` est un *fichier* pointant hors du montage : `git checkout` **échoue en silence** dans le conteneur, la mutation précédente reste sur le disque et le témoin **n'est plus le témoin** | — (T3.40) | *Douzième variante — la restauration qui ne restaure RIEN*, plus bas |
| **13** | ⭐ **La borne de sûreté devenue la contrainte active** — un plafond « ne jamais bloquer `make check` » atteint **avant** l'échéance testée : l'attente rend la main en 80 ms au lieu de 650, verte, **sans rien exercer** | — (T3.40) | *Treizième variante — la borne de sûreté devenue la contrainte active*, plus bas |
| **14** | ⭐ **Le cache de compilation qui rend un objet périmé** — ⚠️ **CONDITIONNELLE** : elle n'existe que si le cache est mal configuré, mais **l'un des réglages fautifs était le DÉFAUT** (`compiler_check = mtime`). Le `.o` ne correspond pas à la source, **sans aucune trace** : compilation réussie, objet bien daté, test vert | — (T3.51) | *T3.51 — Quatorzième variante de faux vert*, plus bas ; [T3.51](T3.51.md) |

⚠️ **Deux voisins qui ne sont PAS des variantes de plus** — les recompter en ferait deux
**imaginaires** de plus : **`F-TEST-2`** (le non-relink étendu au faux **rouge**) et
**`F-RELINK-T337`** (faux rouge reproduit sur source propre) sont deux **instances** de la
famille `_DEPENDENCIES`, n° 1 à 5.

⭐ **La n° 14 a été ajoutée par [T3.51](T3.51.md)** (2026-08-26) — c'est la **seule CONDITIONNELLE**
de la liste : elle n'existe que si le cache de compilation est mal configuré. Elle est comptée ici
parce que le réglage fautif ② était **le défaut livré**, donc **active et non hypothétique**.

⭐ **Les n° 12 et 13 ont été ajoutées par [T3.40](T3.40.md)**, l'une **rencontrée par son relecteur
sur sa propre campagne**, l'autre **produite par T3.40 elle-même en corrigeant un flottement**.
⚠️ Le fil commun des quatorze n'a pas changé : *l'arbre a l'air juste à l'endroit qu'on regarde* —
et les deux dernières ajoutent une précision : **l'endroit qu'on regarde peut être le verdict, alors
que la mesure est dans le compte de fichiers restaurés ou dans le temps par cas.**

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

## F-PYTEST-1 — la **8ᵉ** des **quatorze** variantes de faux vert (liste canonique ci-dessus) : des tests qui ne s'exécutent pas (2026-08-25)

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
ouverte par la revue de [T3.39](T3.39.md), fiche [T3.42](T3.42.md) — n'a jamais été exécuté
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
## T3.40 — livraison (2026-08-25) : `F-SIGC-1` instruite hors des volets

⚠️ **Section à APPENDRE, jamais à réécrire** — en conflit, **garder les deux côtés**.

### ⭐ [F-SIGC-1, apport] `RoonPlayer` **EST** un `IOBase` : le classement de la fiche était faux

La fiche `T3.40` rangeait les 20 sites en **5 `IOBase`** (atteignables par `deleteIO()`) et
**15 autres**, et mettait `RoonPlayer` (**8 sites**) dans les seconds, au motif que la classe dérive
de `sigc::trackable`. **Mesuré au source** :

```
Audio/RoonPlayer.h:167   class RoonPlayer: public AudioPlayer, public sigc::trackable
Audio/AudioPlayer.h:32   class AudioPlayer: public IOBase
Audio/RoonPlayer.cpp:29  REGISTER_IO_USERTYPE(Roon, RoonPlayer)
```

Les deux affirmations sont vraies **et c'est la première qui décide** : un `RoonPlayer` est créé par
l'`IOFactory`, rangé dans une `Room`, et `ListeRoom::deleteIO()` l'atteint **exactement** comme un
volet. ⇒ il n'y a pas **5** sites `IOBase` non gardés mais ⭐ **13, sur 6 classes**
(`Scenario`, `InputSwitchLongPress`, `InputSwitchTriple`, `IPCam`, `KNXIo<Base>`, `RoonPlayer`).

⚠️ **La forme générale de l'erreur** : `sigc::trackable` a été utilisé **deux fois de suite comme
critère de classement**, et **les deux fois il a menti dans un sens différent** — d'abord en
laissant croire que `RoonPlayer` était *protégé* (démenti par la revue de T3.34 : ce sont des
lambdas), puis en laissant croire qu'il n'était *pas un `IOBase`*. La question qui classe un site
n'est **jamais** de quoi la classe hérite : c'est **« existe-t-il un chemin de destruction réel ? »**
et **« la cible est-elle un `mem_fun` ou une lambda ? »**, deux questions indépendantes.

### ⭐ [F-SIGC-1, apport] Le compilateur répond lui-même à « ce `[=]` capture-t-il `this` ? »

L'arbre compile en **C++20**, où g++ émet
*« implicit capture of `this` via `[=]` is deprecated »* **exactement** pour les lambdas `[=]` qui
capturent `this`. Un `make` complet en produit **120**, et c'est une **mesure**, pas une lecture.

Confrontée à cette liste, l'exclusion des deux faux positifs de la fiche est **confirmée par
l'outil qui décide** : `IPCam/Foscam.cpp:125` et `LuaScript/ScriptExec.cpp:129` **n'y figurent
pas**. Les sites `[=]` qui y figurent (`IPCam.cpp:126`, `Scenario.cpp:103`,
`InputSwitchLongPress.cpp:87`, `EventManager.cpp:47`, `PollListenner.cpp:60`,
`UrlDownloader.cpp:734`) capturent bien `this`.

⚠️ **Complément, pas remplacement** : une capture **explicite** (`[this]`, `[this, data]`) ne produit
aucun avertissement. Le balayage textuel reste nécessaire ; l'avertissement tranche seulement les
`[=]`, qui sont précisément les cas que la lecture confond.

### ⛔ [F-SIGC-1, apport] `class IOBase: public sigc::trackable` — essayé, ça **ne compile pas**

```
error: 'sigc::trackable' is an ambiguous base of 'Calaos::RoonPlayer'
error: 'sigc::trackable' is an ambiguous base of 'Calaos::IOAVReceiver'
error: 'sigc::trackable' is an ambiguous base of 'Calaos::Squeezebox'
```

Ces trois classes dérivent **déjà** de `sigc::trackable` à côté de leur branche `IOBase`. Et même en
les démêlant, le bénéfice serait **1 site sur 20** : c'est le nombre de `sigc::mem_fun` du balayage
(`InputSwitchTriple:97`), les 19 autres étant des lambdas que `trackable` ne déconnecte jamais.
⇒ **ce qui ferme la classe de défauts est un jeton de vie porté par `IOBase`**, qui ne fait aucune
différence entre une lambda et un `mem_fun`. `sizeof(sigc::trackable)` = **8**,
`sizeof(std::shared_ptr<bool>)` = **16**.

### ⭐ [F-SIGC-1, apport] Un use-after-free qui ne plante pas — et l'oracle qui le voit quand même

`RoonPlayer::get_playlist_size_cb` **ne touche aucun membre** : elle écrit dans la donnée capturée
et **rappelle le slot de l'appelant**. Après destruction du lecteur, le callback **ne plante pas** :
il **répond tranquillement à une requête d'API pour un IO détruit**. ⇒ *« le binaire a survécu »* ne
prouve rien, et un filet qui n'attend qu'un `SIGSEGV` serait **vert pour la mauvaise raison** sur
sept des huit sites Roon.

Deux oracles déterministes en sortent, tous deux sans dépendance à l'allocateur :

1. ⭐ **le tampon empoisonné** : la sonde est construite par **placement `new` dans un tampon que le
   test possède**, détruite par appel explicite du destructeur, puis le tampon est rempli de
   `0xA5` — **hors du domaine** (tous ces callbacks écrivent `0`/`false`). Aucun allocateur ne peut
   redistribuer ce tampon, donc une écriture à travers le `this` pendouillant est **certaine** d'y
   être vue, et « jamais écrit » ne se confond pas avec « écrit la valeur du défaut » (7ᵉ variante) ;
2. **le compteur d'appels**, quand le callback rappelle l'appelant : plus tranchant encore, et il
   montre que le défaut n'est pas seulement un risque de plantage.

⚠️ Un tel filet doit porter **son propre auto-test** (« le détecteur voit-il une écriture ? »), sans
quoi tous ses cas sont **vertement vides**.

### ⭐ [F-SIGC-1, apport] La garde recopiée redevient une garde qu'on peut poser de travers

T3.34 réécrit son jeton **à chaque site**. Recopié 16 fois, il redevient la forme où l'erreur
s'écrit — et l'erreur est nommée : **témoin capturé en `shared_ptr` fort**, garde inerte,
use-after-free mué en **fuite de handle**. T3.40 empaquette le mécanisme **une fois**
(`LifetimeTag`, `src/lib/Timer.h`, à côté du `singleShot` dont il répare l'angle mort) et
**n'expose que le `weak_ptr`** : la faute devient **inécrivable au site d'appel**, et reste
écrivable **au seul endroit où une contre-mutation peut la viser**. Mutation faite : les **4**
oracles de sites rougissent **et** le binaire meurt (`139`). *Le trou ne se referme pas par la
vigilance, il se referme par une forme où l'erreur ne s'écrit pas* (leçon T3.31).

### ⚠️ [F-SIGC-1, apport] Ce que T3.40 ne ferme PAS

**16 sites sur 20** sont gardés. Restent : `lib/UrlDownloader.cpp:734` — **délibéré**, l'idler
n'*utilise* pas l'objet, il **est** sa destruction différée (`delete this`), et une garde y
supprimerait le `delete` ⇒ fuite — et **3 singletons** (`EventManager.cpp:47`,
`CalaosConfig.cpp:229`, `McpServerManager.cpp:292`) dont l'`Instance()` est un **static local de
fonction** qu'aucun `delete` de l'arbre n'atteint. Le motif est écrit **au source**, appuyé sur
*« aucun chemin de destruction dans l'arbre »*, pas sur *« un singleton, c'est sûr »*.

⛔ Et **parmi les 16 fermés, 3 le sont sans oracle dédié** : `IPCam/IPCam.cpp:126` (atteindre le site
demande un `UrlDownloader` réellement en transfert, et `~IPCam` supprime le téléchargeur en vol —
trois facteurs confondants), `IO/ExternProc.cpp:191/198` et `PollListenner.cpp:60` (chemin de
destruction établi **au code**, pas exercé). **Retirer ces gardes-là ne rougit rien.**

### ⭐ [F-SIGC-1, apport] Le filet ferme la garde **mal posée** ; la garde **absente** demande un censeur de source

`LifetimeTag` rend la faute « témoin capturé en `shared_ptr` **fort** » **inécrivable au site
d'appel** — c'est MU-F qui le mesure. Il ne dit **rien** de la garde **absente** :
`Timer::singleShot()` et `Idler::singleIdler()` restent **publics**, et un site neuf écrit demain
avec un `this` nu compile, se relie et tourne exactement comme les vingt que le ticket a pesés.
⛔ **Rien dans l'arbre ne le détectait**, et le filet de cas gtest ne le pouvait pas : il n'observe
que le code qu'il exécute.

Trois remèdes, deux écartés **par la mesure** :

| Remède | Coût **mesuré** | Verdict |
|---|---|---|
| `[[deprecated]]` sur les deux points d'entrée | ⭐ **essayé, pas raisonné** : reconstruction complète avec l'attribut ⇒ **85 avertissements sur 27 sites distincts** — les **4** laissés bruts délibérément, les **8** portant déjà leur propre garde, les **6** `mem_fun` de `Squeezebox`, les **2** qui ne capturent pas `this`, les **3** singletons, `UrlDownloader:739`, `main.cpp:198`, **et les 2 de `LifetimeTag` lui-même**. **Aucun n'est un défaut** ; l'arbre ne compile en `-Werror` nulle part, donc le bruit serait simplement ignoré | écarté |
| surcharge exigeant le jeton pour les classes dérivant d'`IOBase` | C++ ne sait pas exprimer *« ce site d'appel est dans une classe dérivant de X »*. Le contrôle redeviendrait un **nom à respecter**, c'est-à-dire exactement la discipline qui a échoué | écarté |
| ⭐ **censeur de source** dans le filet | 1 fichier, 3 cas gtest, **~110 ms** | **retenu** |

Le censeur **gèle l'ensemble des appels bruts** de `src/` (par fichier, commentaires blanchis,
espaces écrasés — `Timer :: singleShot (` et `Timer::singleShot(` comptent pareil). Un one-shot
fire-and-forget ajouté n'importe où **rougit** ; la correction est soit `LifetimeTag`, soit **une
ligne d'allocation avec sa raison**.

⚠️ **Ce qu'il ne dit pas** : il **ignore** si un site capture `this`, et l'ignorera toujours. Il dit
*« quelqu'un a ajouté un one-shot que ce ticket n'a jamais regardé »* — la question qui n'avait
**aucune** réponse avant, et la seule qu'un recensement textuel puisse répondre honnêtement.

⚠️ **Effet de bord MESURÉ, et il change la lecture d'une campagne.** MU-A…MU-E et MU-G convertissent
une garde en appel brut **dans un fichier dont l'allocation est 0** ⇒ elles rougissent **aussi** le
censeur. ⛔ Les ensembles rouges ne sont donc **plus deux à deux disjoints** : ils **partagent** le
censeur. ⭐ Ce qu'il faut écrire, c'est que leur **différence au censeur** l'est — pas
« disjoints ».
⭐ Conséquence heureuse : **MU-E n'est plus muette**. Le censeur est déclaré **en premier** et ne
touche ni la boucle ni un IO, donc il rougit **avant** que le binaire ne meure. La signature de MU-E
passe de « **139 sans une seule ligne `FAILED`** » à « **1 ligne `FAILED` puis 139** ».

### ⭐ [F-SIGC-1, apport] Un jeton porté par la BASE meurt en DERNIER — la fenêtre se ferme par un invariant, pas par un ordre de déclaration

`IOBase::ioAlive` est un membre de la **base**. L'ordre du langage est : corps de `~Derived` →
membres dérivés (ordre inverse de déclaration) → corps de `~IOBase` → membres d'`IOBase` → bases.
⇒ **pendant tout le démontage dérivé, la garde répond « vivant »**, et un one-shot qui tirerait là
tournerait sur un objet à moitié détruit.

⛔ **Aucun ordre de déclaration ne ferme ça** : un sous-objet de base est **toujours** détruit après
le dérivé complet. Déclarer `ioAlive` **dernier membre** d'`IOBase` — c'est fait — ne gagne que la
moitié visible : le membre devient le **premier détruit** parmi ceux d'`IOBase`, donc la garde est
déjà morte pendant que `~IOBase` démonte `param`, `ioDoc` et `status_info`. Gain réel, minuscule,
**et ce n'est pas la fermeture**.

⭐ **La fermeture est un INVARIANT** : *aucun destructeur de cet arbre ne pompe la boucle*. Un
one-shot en attente ne peut pas tirer pendant un destructeur si rien, dans ce destructeur, ne rend
la main à la boucle. Mesuré : hors `src/lib/uvw` (tiers vendoré, sa propre boucle et ses propres
tests), **tout `src/` pompe la boucle en exactement TROIS endroits** — `calaos_server/main.cpp` (la
boucle principale) et les **deux** liaisons `requestUrl()` de `LuaScript/ScriptBindings.cpp`, qui
tournent dans le **sous-processus** de script. **Aucun n'est un destructeur.**

⚠️ **Un invariant non exécutable se périme.** Celui-là l'est : le cas
`IoLifetimeSourceGuardTest.NothingNewPumpsTheEventLoop` gèle l'ensemble des trois, à l'**égalité**
(pas au « ≤ ») — un quatrième n'est pas interdit, il **oblige** quelqu'un à répondre
*« est-il atteignable depuis un destructeur ? »* avant de mettre l'ensemble à jour. Contre-mutation
**MU-I** (un `run()` de plus dans `ScriptBindings.cpp`) : **rouge, et rouge SEUL** — sortie 1,
16 cas lancés, 15 OK.

⭐ **La leçon générale** : *un jeton de vie ne protège jamais le démontage de son propre objet.*
Là où l'objet garde n'est pas le dernier à mourir, la sûreté ne vient pas du jeton mais de la
**garantie que personne ne rend la main à la boucle pendant la destruction** — et cette garantie
doit être **écrite et mesurée**, sinon elle se referme sur le prochain qui ajoute un `run()`.

### ⭐ [F-SIGC-1, apport] « Couvert » et « couvert par ÉCHANTILLON » ne se valent pas

Deux familles multi-sites, deux natures de couverture **différentes**, que six mois effacent si on
écrit « couvert » dans les deux cas :

| Famille | Sites | Nature | Ce qu'un mutant posé ailleurs donne |
|---|---|---|---|
| `KNXIo<Base>` | **11 types** KNX | ⭐ **structurellement couverts** : les onze partagent **LA MÊME LIGNE** (`IO/KNX/KNXIo.h:78`, mixin `template`) | il n'y a **aucun autre endroit** où le poser |
| `RoonPlayer` | **8 sites** | ⭐ **couvert par ÉCHANTILLON** : **1 site sur 8** est tenu par un mutant (`get_playlist_size`) | **il SURVIT** — mesuré sur `get_volume` |

⚠️ **Et une couverture structurelle ne dit pas qu'elle est non vide.** Le cas KNX n'avait **aucun**
compagnon `*StillRunsWhileAlive` : sa non-vacuité reposait entièrement sur MU-E, c'est-à-dire sur
*« retirer la garde tue le binaire »*. Cela prouve que la garde **porte aujourd'hui** ; cela ne dit
rien du jour où `read_at_start` n'armerait plus rien — ce jour-là MU-E cesserait de planter, **le
mutant survivrait**, et le cas resterait vert pour la mauvaise raison. Compagnon ajouté, et la
contre-mutation qui le vise (**MU-H** : le délai 1,5 s porté à 30 s, la garde **intacte**) le rougit
**SEUL** — ni MU-E ni le cas de destruction ne la voient.

### ⛔ Douzième variante de faux vert (**n° 12 de la liste canonique**) — **la restauration qui ne restaure RIEN**

Rencontrée par le **relecteur de T3.40 sur sa propre campagne**. Son script restaurait l'arbre par
`git checkout -- <fichiers>` **exécuté dans le conteneur**. Or l'arbre est un **worktree git** : son
`.git` est un *fichier* pointant vers `…/calaos_base/.git/worktrees/<nom>`, **hors du montage**.
Dans le conteneur, `git` ne voit donc **aucun dépôt**, `git checkout` **échoue** — et son code de
retour n'était pas testé.

⇒ **rien n'était restauré**. La mutation précédente restait sur le disque, la suivante s'appliquait
**par-dessus**, et le résultat le plus dangereux est le **vert** : un `M0` joué après une mutation
restée en place est un témoin **qui n'est plus le témoin**.

⭐ **Ce qui l'a rendu visible** : le script **imprimait le nombre de fichiers restaurés**, et il y a
lu **zéro**. **Aucun** autre signal ne le disait — ni le code de sortie du `make`, ni le journal du
test, ni même le `cmp` d'application, qui comparait la mutation à… l'état déjà muté.

**La règle qui en sort** :
1. la restauration ne s'appuie **jamais** sur `git` à l'intérieur d'un conteneur — copie
   **pristine** montée à part, `shutil.copyfile` (dates **non** préservées : F-TEST-2 exige que
   `make` recompile) ;
2. elle **compte** ce qu'elle a remis, et l'on **refuse de conclure si le compte est nul** ;
3. elle **revérifie octet à octet** après coup, et la vérification d'application (`cmp`) compare à
   la **pristine**, jamais à l'état courant ;
4. ⭐ elle rapporte aussi **quels** fichiers avaient changé : après une mutation, cet ensemble doit
   valoir **exactement `{le fichier muté}`** — c'est la même mesure qui prouve que la mutation a été
   appliquée **et** qu'elle n'a pas débordé **et** qu'elle a été rendue.

⚠️ **Généralisation, et c'est là que ça mord** : *tout* outil qui « ne fait rien » silencieusement
dans le conteneur produit cette variante. `git` en est un cas particulier — mais dans un
**worktree**, il l'est **structurellement**, pas par accident.

### ⛔ Treizième variante de faux vert (**n° 13 de la liste canonique**) — **la borne de sûreté devenue la contrainte active**

Produite **en corrigeant** un flottement, pendant la livraison de T3.40. Le filet attend l'expiration
de délais réels (250 ms, 400 ms, 1,5 s) en pompant la boucle ; l'attente était bornée à l'horloge.
Pour la rendre robuste à une machine affamée — où une attente bornée **uniquement** à l'horloge peut
trouver son échéance déjà passée et rendre la main **sans avoir pompé** — un plancher d'itérations a
été ajouté… **avec un plafond « ne jamais bloquer `make check` » de 200 000 itérations**.

⛔ Une boucle `uv_run(NOWAIT)` au repos atteint 200 000 itérations en **~80 ms**. Le plafond est donc
devenu la **contrainte active** : *toutes* les attentes rendaient la main au bout de **80 ms au lieu
de 650 ou 1900**. Les cas restaient **verts** — ils n'exerçaient plus **rien**, l'échéance qu'ils
testent n'expirant jamais. **Sortie 0, 16 cas sur 16, aucune ligne rouge, aucun signal.**

⭐ **Ce qui l'a rendu visible : les TEMPS PAR CAS**, et rien d'autre — une suite passée de
**4 024 ms à 1 042 ms**. Ni le code de sortie, ni le nombre de cas, ni le journal `PASS/FAIL` ne le
disaient, et une campagne de contre-mutation ne l'aurait pas vu non plus : les mutants **survivants**
seraient devenus la norme, ce qui se lit comme « le filet ne couvre pas ce site », pas comme
« le filet n'attend plus ».

**Les deux règles** :
1. ⭐ **une borne de sûreté ne doit jamais pouvoir devenir la contrainte active.** Ici l'horloge
   suffisait déjà à faire terminer la boucle : le plafond n'apportait aucune sûreté et retirait
   toute la mesure. *Une garde qui peut mordre avant la condition qu'elle protège n'est pas une
   garde, c'est un raccourci.*
2. ⭐ **une attente doit dire si elle a attendu.** `pumpLoopFor()` se termine désormais par un
   `ADD_FAILURE()` lorsque le temps écoulé est **inférieur** au temps demandé : le défaut ci-dessus
   serait aujourd'hui **rouge**, pas invisible.

⚠️ **Conséquence sur la méthode de campagne** : relever les **temps par cas** à chaque tour, pas
seulement `PASS`/`FAIL`/code de sortie. Un journal qui ne garde que le verdict est aveugle à toute
la famille « l'oracle a cessé d'observer ».

⭐ **Et cette variante a un JUMEAU DE L'AUTRE CÔTÉ, déjà fiché ailleurs : `F-FLAKY-1` / [T3.49](T3.49.md).**
Ma n° 13 est *l'attente qui rend la main TROP TÔT* (verte, sans rien exercer). `F-FLAKY-1` est
*l'attente qui rend la main TROP TARD* (rouge, sans défaut) — et c'est bien l'hypothèse que T3.49
retient. Les deux sortent **du même helper**, écrit de la même façon :

```cpp
tests/core/ShutterImpulse_test.cpp:113   //et, avant ce ticket, IoLifetimeTimer_test.cpp
void pumpLoopFor(int ms)
{
    auto deadline = now() + ms;
    while (now() < deadline) loop->run<NOWAIT>();     //borné à l'HORLOGE, et MUET
}
```

⇒ **remède transposable, et il est concret** : (1) l'attente **dit si elle a attendu**
(`ADD_FAILURE()` quand l'écoulé est inférieur au demandé) — cela sépare *« le produit s'est arrêté
trop tôt »* de *« la pompe a débordé »*, ce que le message actuel (`Actual: true / Expected: false`)
**ne permet pas de distinguer** ; (2) là où c'est une **fenêtre** qu'on vérifie, ne pas demander
*« l'événement n'a pas eu lieu avant t »* mais **mesurer l'instant où il est OBSERVÉ** et en exiger
une **borne inférieure** — un retard ne fait alors que grandir la mesure, et **ne peut plus rougir**.
⚠️ **Non appliqué ici** : `core/ShutterImpulse_test.cpp` est le périmètre de `T3.49`, pas de T3.40.
Ce paragraphe est la **mesure** que T3.49 hérite, pas une correction.

### ⚠️ [F-SIGC-1, apport] Un rouge NON REPRODUCTIBLE, et pourquoi la fenêtre ne doit pas être pinglée à l'horloge

Au tour **MU-J** d'une campagne, `ProcessExitedStillFiresWhileTheServerIsAlive` a rougi. **MU-J
n'ajoute qu'un `Timer::singleShot` dans `main.cpp`, objet qui n'est même pas relié au binaire de
test** : la mutation **ne peut pas** en être la cause. Flottement mesuré à **1 sur ~80** exécutions,
**non reproduit** en 20 exécutions isolées ni en 12 exécutions concurrentes sous 40 processus de
charge.

**Diagnostic** : le cas demandait *« le callback n'a pas tiré avant 40 ms »* pour un délai réel de
**100 ms**. Une seule itération `run<NOWAIT>` peut déborder cette marge de 60 ms sur une machine
chargée — la boucle porte le cycle de respawn de `RoonCtrl`, qui refait un `uv_spawn` **toutes les
100 ms**. L'observation tombe alors **après** l'échéance, et le cas rougit **sans défaut**.

⭐ **La forme qui ne peut pas mentir** : ne pas demander *« le callback n'a pas TIRÉ avant t »* mais
*« le callback n'a pas été **OBSERVÉ** avant t »* — c'est-à-dire **mesurer** l'instant où le
prédicat tient pour la première fois et en exiger une **borne inférieure**. Un retard ne fait que
**grandir** la mesure : une borne inférieure ne peut pas échouer parce que la machine est lente.
La borne supérieure, elle, reste un **budget** (l'attente rend la main dès que le prédicat tient),
jamais un délai subi.


## T3.50 — la moitié LECTURE du chemin retour Wago

- ⭐ **[F-WAGO-10] — NOUVEAU : `Utils::signal_wago` porte une paire permutable `(int addr, bool val)`
  dont les DEUX membres sont lus.** `Calaos.h:68` déclare
  `typedef sigc::signal<void, std::string, int, bool, std::string> type_signal_wago;` —
  `(ip, addr, val, intype)`. `int` et `bool` se convertissent **dans les deux sens** en silence.
  **1 implémentation** (`WIDigitalBase::ReceiveFromWago`, `WagoIOBase.h:194`), **1 enregistrement**
  (`:169`), **2 sites d'émission** (`UDPServer.cpp:102` `"std"`, `:118` `"knx"`).
  ⚠️ **Ce qui le distingue de tout ce que T3.31/T3.46/T3.50 ont fermé** : au receveur, les deux
  valeurs sont **utilisées** — `if (ip == host && addr == address)` puis `udp_value = val`
  (`WagoIOBase.h:195-201`). ⇒ **une permutation y CHANGE le programme** : une entrée digitale
  comparerait son adresse à un booléen et prendrait pour état l'adresse reçue. C'est exactement ce
  que la moitié lecture du chemin modbus **n'était pas** (voir l'entrée suivante).
  **Hors périmètre de T3.50** : autre signal, autres fichiers (`Calaos.h`, `UDPServer.cpp`), chemin
  **UDP** et non modbus. Fiché, pas avalé. ⚠️ **Non mesuré** : aucun test n'atteint ce chemin, et je
  n'ai pas vérifié s'il est atteignable en production — l'affirmation « les deux valeurs sont
  utilisées » est une **lecture du corps**, pas une exécution.

- ⛔ **[Infirmation — la moitié LECTURE n'était PAS « la plus dangereuse », et c'était écrit
  partout]** `T3.50.md` §1, `T3.46.md` §6.3.1 et `WagoTypes.h` affirmaient qu'une permutation de
  `(UWord address, int count)` au retour produit « une lecture fausse à une adresse fausse » et que
  `count` « gouverne la taille du vecteur lu ». **Mesuré, les deux sont faux sur cet arbre** :
  **aucune** des six implémentations ne lit `address` ni `count` — ⭐ **12 avertissements
  `-Wunused-parameter` sur les six signatures : `addr`/`address` 6 fois sur 6 ET `count` 6 fois
  sur 6** (`WagoMap.cpp:172` · `WIAnalog.cpp:72` · `WITemp.cpp:68` · `WOAnalog.cpp:69` ·
  `WODigital.cpp:81` · `WagoIOBase.h:104`), l'instrument validé par sa propre sonde puisque le
  projet compile avec `-Wno-unused-parameter` —, et les vecteurs de réponse sont construits depuis
  le **tableau JSON `"values"`**, jamais depuis `count`. ⇒ **la permutation était un NO-OP
  sémantique**, aux implémentations **comme** aux quatre sites d'émission. Les deux valeurs sont
  bien **vivantes** (décodées de la réponse, aucune n'est un littéral, contrairement à la moitié
  écriture) — mais vivantes et **ignorées**.
  ⚠️ **Le chiffre de 11 et le caveat publiés par la 1ʳᵉ rédaction étaient FAUX, dans le sens de la
  sous-estimation** : elle donnait `count` **5 fois sur 6** et prétendait que `WagoIOBase.h:104`
  échappait à l'instrument, gcc y émettant un `-Wshadow` à la place — la conclusion sur
  `WIDigitalBase` aurait alors reposé sur une **lecture** de corps et non sur une mesure.
  **Remesuré** : `WagoIOBase.h:104` produit **bien** `unused parameter 'count'`, et les six unités
  ne contiennent **ZÉRO** ligne `-Wshadow`. ⇒ **les six conclusions reposent sur une MESURE.**
  ⭐ **PRÉCISION AJOUTÉE AU MERGE DE T3.50, et elle réhabilite en partie la 1ʳᵉ rédaction sans
  renverser la conclusion.** Le merge a rejoué la mesure dans l'image de build sur **les 14 unités
  de `IO/Wago/`** (et non six), drapeaux projet + `-Wunused-parameter`, **`rc=0` exigé d'abord** :
  **0 unité en échec**, **13 avertissements uniques sur les six sites** — les **12** annoncés
  (**6 `address`/`addr` + 6 `count`**, `WagoIOBase.h:104:82` **colonne 82 confirmée**) **plus**
  `WagoMap.cpp:172:126` `unused parameter 'values'`, qui confirme que
  `WagoModbusReadHeartbeatCallback` n'utilise **même pas** `values`. **Témoin sans le drapeau : 0.**
  ⚠️ **Mais « ZÉRO ligne `-Wshadow` » ne vaut QUE pour les six unités de l'auteur** : sur les 14,
  `WagoIOBase.h:104:82` **émet bien** `declaration of 'count' shadows a member` — dans **une seule**
  unité, **`WIDigitalTriple.cpp`**, la seule dont la base `WIDigitalBase<InputSwitchTriple>` porte un
  membre `count` ; et **c'est exactement là, et là seulement, que `count` perd son
  `unused parameter`**. Dans `WIDigitalBP.cpp` et `WIDigitalLong.cpp` les **deux** avertissements
  sortent. ⇒ ⭐ **la conclusion tient — l'avertissement existe et il est mesuré — mais la phrase
  absolue était trop large, et l'intuition de la 1ʳᵉ rédaction (« gcc y met un `-Wshadow` à la
  place ») était juste POUR UNE unité sur cinq.** ⚠️ **Même classe de généralisation que R2 :
  une portée de balayage doit être écrite avec la phrase qu'elle porte.**
  ⭐ **Garde-fou de l'instrument, appris à ses dépens** : la 1ʳᵉ passe du recomptage a rendu
  « 0 avertissement partout » parce que les six unités **ne compilaient pas** (arbre configuré pour
  l'image de build, pas pour l'hôte). *Une unité en échec rend zéro avertissement, et zéro
  avertissement se lit comme une bonne nouvelle.* **Compter les unités en échec et exiger 0 avant
  de publier un compte d'avertissements.**
  ⭐ **Règle à retenir, la même que T3.46 §7.3 avait payée sur `M2`/`M3`** : *avant d'exiger un
  rouge comportemental d'une permutation, lire les corps.* Si les paramètres ne sont pas utilisés,
  exiger un rouge revient à exiger qu'un test distingue deux programmes identiques. **Ce que le
  typage ferme alors est un CONTRAT, et il faut le dire ainsi.**

- ⚠️ **[Portée — « `sigc++` refuse la référence lvalue » est FAUX comme phrase générale]** T3.46
  §7.3 a mesuré `MXD` : une référence non-const dans `SingleWord_cb` ⇒ `rc=2`. **Le résultat ne se
  transporte pas tel quel.** `MultiBits_cb`/`MultiWords_cb` portent **déjà** une référence lvalue
  non-const, `vector<bool> &`/`vector<UWord> &`, et `sigc++` l'accepte **depuis toujours** — parce
  que `WagoMap::processNewMessage` passe un **local nommé**. Ce qui décide est **ce que le site
  d'émission passe**, pas le slot. Remesuré ici (`MXD-R`) : sur `address`/`count`, emballés en
  **prvalue**, le refus tient (`rc=2`, *cannot bind non-const lvalue reference … to an rvalue*,
  `WagoMap.cpp:214:61`) ; sur le 4ᵉ paramètre il n'a jamais existé. ⇒ **l'angle mort `F-TYPE-5` est
  inarmable sur les deux paramètres typés, et parfaitement armé sur le troisième** — il faut donc
  un témoin exécutable de la référence lvalue dans la suite, ce que le fichier de test porte.

- ⭐ **[Trou d'oracle de T3.46 — COMBLÉ ici, par deux moyens différents]** T3.46 déclarait : *« les
  quatre implémentations ne sont sondées par aucun cas »*. Sur la moitié lecture :
  (a) **4 des 6** implémentations sont sondées **directement**, par pointeur sur membre lu par
  `std::is_invocable_v` — SFINAE-friendly, donc un ré-élargissement **rougit** au lieu de casser la
  compilation (⚠️ *un test qui ne compile pas est un test **absent**, pas un test rouge*) ;
  (b) les **2 restantes sont `private`** (`WOAnalog`, `WODigital`) — aucune classe dérivée ne peut
  les nommer, aucune sonde n'est possible. Ce qui les tient est l'enregistrement `sigc::mem_fun` de
  leur propre `.cpp`, et **c'est mesuré** (`MW`) : ré-élargir la signature de `WODigital` **seule**,
  sans toucher le `typedef`, donne `rc=2` à la ligne de l'enregistrement.
  ⭐ **Et une implémentation est réellement EXERCÉE** (`WIAnalog`, construit par le constructeur de
  production sur un hôte TEST-NET-1) : « lié » n'est pas « exercé » (`F-LINK-1`), et ici c'est
  exercé.

- ⚠️ **[F-WAGO-7 — numéros de ligne périmés dans cette fiche]** L'entrée `F-WAGO-7` ci-dessus cite
  `WagoExternProc_main.cpp:217`/`:218`/`:223` pour sa **dernière** branche (`write_words`).
  **Recompté au source** : `UWord address;` **6 fois** — `:75` · `:108` · `:131` · `:160` · `:193`
  · **`:222`** ; `int count;` **4 fois** — `:76` · `:132` · `:161` · **`:223`**. La dernière branche
  est donc à `:222`/`:223` (et son `from_string` de `count` à `:228`) : **dérive de 5 lignes**, sur
  **cette branche seulement** — les trois autres et les deux branches sans `count` n'ont pas bougé.
  **Le compte est le même : 6 et 4.** Le défaut est **intact et toujours dû** ; T3.50 n'y touche pas.
  ⭐ **Ce que T3.50 y change quand même** : un balayage `python3` de tout `IO/Wago/` à la recherche
  d'une paire **`(address, count)`** adjacente et **nue** dans une signature n'en trouve plus
  qu'**UNE**, et c'est `F-WAGO-10` (chemin UDP). La chaîne Wago **modbus** est typée par rôle de
  bout en bout ; les seuls endroits où **la paire *(address, count)*** reste nue sont désormais
  **`libmbus`** (refusée sur le coût, T3.46 §7.5, non rouverte) et **le dispatcher de `F-WAGO-7`**.
  ⚠️ **Portée exacte, et la 1ʳᵉ rédaction la donnait trop large** : elle écrivait « la liste de ce
  qui reste est **close** » sans nommer la paire, alors que la mesure ne portait que sur la forme
  `(address, count)`. **La mesure n'était pas fausse, la phrase l'était.** ⭐ **Écrire « la paire
  *(address, count)* », jamais « la paire »** — c'est la généralisation qui a renversé dix
  affirmations d'atteignabilité dans les tickets précédents. **Deux autres paires nues existent
  bel et bien dans `IO/Wago/`** : voir l'entrée suivante.

- ✅ **[NUMÉROS ATTRIBUÉS AU MERGE DE T3.50 : [`T3.53`](T3.53.md) = la paire DALI ·
  [`T3.54`](T3.54.md) = `setBufferBit`.]** ⭐ **Les deux comptes ci-dessous ont été RE-VÉRIFIÉS au
  source par le merge, indépendamment, et concordent avec ceux de l'auteur** (DALI : 5 décl. /
  5 déf. / 4 `mem_fun`, le 3ᵉ paramètre de `WODaliRVB.cpp:164` bien anonyme ; `setBufferBit` :
  1 déf. `WagoBits.h:114` / 1 appel `:138` ; `tests/WagoBits_test.cpp` : **12** `packBits`,
  **0** `setBufferBit` ; **aucun** fichier de test DALI dans l'arbre). ⛔ **Aucun autre numéro
  ouvert.** *Texte d'origine conservé ci-dessous :* ⛔ **[DEUX AUTRES PAIRES ADJACENTES NUES DANS
  `IO/Wago/`, remontées au coordinateur pour attribution.]** ⚠️ **Trouvées par la revue de T3.50, pas par moi** : mon
  balayage ne cherchait que la forme `(address, count)` et **ne pouvait pas les voir**. Vérifiées
  au source ici, les deux sont réelles. ⛔ **Aucun numéro n'est ouvert de mon propre chef**
  (`T3.40`…`T3.50` sont pris) — **il en faut un, ou deux, et c'est au coordinateur de trancher.**

  1. ⭐ **[T3.54](T3.54.md) — `setBufferBit(unsigned char *buf, int bit, bool val)` — `WagoBits.h:114`.** `int` et
     `bool` se convertissent **dans les deux sens** en silence, les deux paramètres sont
     **adjacents**, et ⭐ **les DEUX sont lus** : `buf[bit / 8]`, `0x01u << (bit % 8)`, puis
     `if (val)`. **1 définition** (`:114`), **1 seul site d'appel** (`WagoBits.h:138`,
     `setBufferBit(&out[0], i, values[i])`). Une permutation y **change le programme** — même
     classe que `F-WAGO-10`, et non pas le no-op que ce ticket a mesuré.
     ⚠️ **Atténuation, à dire dans les deux sens** : `packBits`, seul appelant, **est couvert par
     un test de comportement** (`tests/WagoBits_test.cpp`, 12 occurrences de `packBits`) — une
     permutation au site d'appel serait très probablement **rougie par un test existant**. C'est
     ce qui la sépare de `F-WAGO-10`, qu'aucun test n'atteint.
  2. ⭐ **[T3.53](T3.53.md) — `(bool status, string command, string result)` — la paire DALI.** `WODali.h:38` /
     `WODali.cpp:82` · `WODaliRVB.h:38-41` / `WODaliRVB.cpp:90`, `:111`, `:132`, `:164`.
     ⛔ **Pire que permutable : les deux types sont IDENTIQUES** (`std::string`) — il n'existe
     **aucune conversion à diagnostiquer**, donc **aucun compilateur ne pourra jamais rien en
     dire**, et le typage par rôle est le **seul** remède possible. **Les deux membres sont lus** :
     `command.find("WAGO_DALI_GET")` puis `split(result, tokens)` (`WODali.cpp:88-92`).
     **5 déclarations**, **5 définitions** — dont `WODaliRVB.cpp:164`, où le 3ᵉ paramètre est
     **anonyme** et donc hors de portée — et **4 enregistrements `sigc::mem_fun`**
     (`WODali.cpp:62`, `WODaliRVB.cpp:73`, `:75`, `:77`). ⚠️ **Aucun test ne couvre ce chemin.**
     ⇒ **c'est la plus exposée des deux** : le plus de sites, aucun filet de test, et hors de
     portée de tout diagnostic.

  ⚠️ **Non mesuré, et c'est une lecture de corps, pas une exécution** : je n'ai ni muté ni exercé
  ces deux paires. L'affirmation « les deux membres sont lus » est établie **au source**.

## E4.1m — `JsonApi`, le modèle : ce que la bascule a mesuré (2026-08-25)

### ⭐⭐ La variante n° 2 (faux VERT de relink) rencontrée **à l'échelle du `make check` ENTIER** — et la démonstration est nette

`FINDINGS.md` connaissait déjà la n° 2 : *le `.o` muté est recompilé, le binaire de test n'est pas
relié.* Ce ticket l'a rencontrée **sans muter quoi que ce soit**, sur le geste le plus banal de
toute la série, et il en a la preuve la plus propre qu'on puisse produire.

**Le geste** : instrumenter les 14 sites du périmètre avec un `fprintf`, tous dans
`src/bin/calaos_server/*.cpp`, puis
`make -j12 && make check -j6`.

**Le résultat** : `# TOTAL: 97 / # PASS: 96 / # FAIL: 0`, et **ZÉRO marqueur dans les 92 `.log`**.
Vérifié à la source du malentendu : `grep -c E41M_MARK src/bin/calaos_server/JsonApi.cpp` = **9**,
`strings tests/core/JsonApiModelWireBytes_test | grep -c E41M_MARK` = **0**, et le `make check`
n'avait imprimé **qu'UNE seule ligne `CXXLD`** (celle de `calaos_server` lui-même).

⇒ **Une modification confinée à `src/bin/calaos_server/*.cpp` ne relie AUCUN binaire de test.**
`make check` rejoue les binaires du build précédent et imprime un vert qui décrit **le code
d'avant**. Ce n'est pas une subtilité de campagne de mutation : c'est le mode de travail normal de
tout ticket de cette série qui ne touche que `src/`.

⭐ **Le corollaire qui sauve, et il est mesurable** : `libcalaos_common.la` **est** dans le
`_DEPENDENCIES` et dans le `LDADD` de tous les binaires. Donc dès qu'un ticket touche un fichier de
`src/lib/` — ce que fait tout ticket d'E4.1 qui allège `Jansson_Addition.h` — **la bibliothèque est
reconstruite et tout se relie**. Compté sur les journaux de ce ticket :

| Ce qui a changé | lignes `CXXLD` du `make check` |
|---|---|
| `src/lib/Jansson_Addition.h` + `src/bin/calaos_server/*.cpp` + 4 tests | **93** |
| 3 fichiers de `tests/core/*.cpp` seulement | **2** |
| 1 fichier de `tests/core/*.cpp` seulement | **1** |
| `src/bin/calaos_server/*.cpp` **seulement** | ⛔ **1** — aucun binaire de test |
| après `rm -f` des 92 binaires | **92** |

⚠️ **Remède, et c'est celui que les fiches exigent déjà sans dire pourquoi** : `make distclean`
avant de conclure — ou, quand on ne veut pas payer un build complet,
`find tests -type f -name '*_test' -perm -u+x -delete` puis `make check`, **et compter les lignes
`CXXLD`** : elles doivent égaler le nombre de binaires. **Le compte de `CXXLD` est l'oracle**, pas
le `# PASS`.

⚠️ **Ce n'est pas une douzième variante** : c'est la n° 2 de la liste canonique, à une échelle que
les entrées précédentes n'avaient pas décrite (elles la décrivaient au grain d'**une** mutation).

### ⭐ `F-LINK-1`, **dixième** mesure d'atteignabilité — 10 sites sur 14, et les 4 zéros sont cohérents

Marqueur posé sur les 14 sites qu'E4.1m change, `make check` complet **avec relink forcé** (voir
ci-dessus, sans quoi la mesure aurait rendu 0 partout pour une raison qui n'a rien à voir) :

- **atteints** : `dumpJsonRedacted` **748** passages / **20** suites, `buildJsonIO` **320** / 6,
  `buildJsonStatusInfo` **320** / 6, `buildJsonRoomIO` **96** / 6, `buildJsonHome`,
  `buildJsonCameras` et `buildJsonAudio` **38** / 6 chacun, `buildJsonGetIO` **29** / 3,
  `JsonApiHandlerWS::processGetHome` **29** / 5, `JsonApiHandlerHttp::processGetHome` **9** / 5 ;
- ⛔ **jamais atteints** : `buildFlatIOList`, et les **trois** sites de `LuaScript/ScriptExec.cpp`
  (message `execute`, message `event`, lecteur).

**20 suites distinctes** atteignent le périmètre, dont **19 préexistantes**. Les quatre zéros sont
**cohérents entre eux** : `buildFlatIOList()` n'a qu'un appelant, le message `execute` de
`ScriptExec.cpp`, et ce lambda ne tourne qu'après le spawn d'un vrai `calaos_script` par uvw.
⇒ **quatre sites gardés par le compilateur seul, déclarés comme tels.** Ce qui les couvre
indirectement, ce sont les **formes** qu'ils emploient, toutes dans `ScriptWire.h` et couvertes par
`tests/ScriptWire_test.cpp` (E4.1j, 32 cas). **Aucune « réplique » du site d'appel n'a été
écrite** : elle n'aurait testé que la réplique.

### ⭐ Le balayage d'octets : **cinq deltas, pas six** — et quatre catégories « non balayées » d'E4.1l refermées

Sonde compilée dans le conteneur contre le vrai `jansson` et le vrai `json.hpp`, sur les **deux
chaînes qu'E4.1m échange** (`json_string(v.c_str())` + `json_object_set_new(o, k.c_str(), …)` +
`json_dumps(JSON_COMPACT|JSON_ENSURE_ASCII)` contre `o[k] = v` +
`dump(-1, ' ', true, error_handler_t::replace)`) : **566 sondes, 315 DIFF, 251 SAME**, dont les
**256 valeurs d'octet en VALEUR** et les **256 en NOM de champ**.

Les cinq deltas d'E4.1l se reproduisent **à l'identique** sur cette chaîne-ci (130 octets
structurellement divergents de chaque côté : `0x00`, `0x7F`, et les 128 de `0x80` à `0xFF` ; plus 9
divergences de casse sur les contrôles C0). **Ce qui est neuf, ce sont les quatre catégories
qu'E4.1l déclarait explicitement non balayées, et les quatre sont NÉGATIVES :**

| Catégorie laissée ouverte par E4.1l | Verdict E4.1m |
|---|---|
| chaînes **longues** | **identiques** — 100 000 caractères ASCII, 20 000 accents, nom de 10 000 caractères |
| **doublons de clés** | **identiques** — les deux gardent la dernière valeur **à la position de la première** |
| **profondeur** d'imbrication | **identiques** — 4, 64 et 1024 niveaux |
| tri sur **clés non ASCII** | ⭐ **l'ORDRE est identique** ; seule la casse de l'échappement bouge. Les deux ordonnent sur les **octets**, pas sur une collation |

⇒ **Aucun sixième delta.** Ce que ce ticket ajoute, ce sont **deux conséquences nommées du delta 3**
(l'UTF-8 invalide), et elles sont plus lourdes que leur cause :

1. ⭐ **un équipement pouvait PERDRE SON NOM** sur `get_home` / `get_io`. `buildJsonIO()` ne testait
   **ni** le retour de `json_string()` (NULL sur de l'UTF-8 invalide) **ni** celui de
   `json_object_set_new()` (−1 alors) : la paire disparaissait, et l'équipement arrivait chez le
   client **indistinguable d'un équipement sans nom**. Une application qui indexe par nom perdait
   l'appareil, sans un message. `core/JsonApiSession_test` épinglait ce comportement comme
   divergence connue ; **le cas est retourné par ce ticket**, il asserte maintenant que le nom
   arrive, en `U+FFFD`.
2. ⭐ **un script Lua contenant un octet mal encodé ne partait pas du tout.** `ScriptWire.h`
   (E4.1j) l'avait écrit comme une **prédiction** : « *ce troisième canal s'OUVRE avec E4.1m* ».
   Il s'ouvre. Avant, la paire `script` était supprimée du message `execute` et `calaos_script`
   recevait un ordre **sans script** ⇒ **rien ne s'exécutait, en silence**. Désormais le script
   arrive avec `U+FFFD` à la place de l'octet fautif et **s'exécute**. C'est un changement de
   **comportement**, déclaré en `RELEASE_NOTES.md`. ⚠️ **Non rejoué contre un vrai
   `calaos_script`** : mesuré au niveau du message construit.

### ⚠️ `dumpJsonRedacted()` : le seul `dump()` de l'épique **sans** `ensure_ascii`, et pourquoi

Ce n'est pas un oubli, c'est un arbitrage, et il est falsifiable.

Cette fonction n'écrit pas sur un **fil** : elle écrit une ligne de **journal**
(`cDebugDom("network")`). Ce journal **n'a jamais été en ASCII** — l'appel précédent était
`json_dumps(copy, JSON_INDENT(4))`, **sans** `JSON_ENSURE_ASCII`. Poser `ensure_ascii = true`
aurait donc changé les octets d'un flux que l'épique **ne migre pas** : exactement la faute que
l'invariant 3 énonce pour l'interdire, et exactement le raisonnement de l'exception nommée
d'`E4.1.md`. C'est aussi ce que font les **trois autres `dump(4, …)` de l'arbre**
(`ConfigOptions.cpp:1496`, `CalaosConfig.cpp:558`, `calaos_config.cpp:347/:616`), tous à
`ensure_ascii = false` ; le seul `dump(4, …, true)` est `IOFactory.cpp:117`, qui écrit un **fichier
destiné à une machine**.

**Mesuré, pas supposé** : à `ensure_ascii = false`, `json_dumps(JSON_INDENT(4))` et
`dump(4, ' ', false, error_handler_t::replace)` sont **identiques à l'octet** sur ASCII, sur
`U+00E9`, sur `U+007F` **et sur un emoji hors BMP** (`U+1F600`) — le delta DEL lui-même disparaît
quand `ensure_ascii` est faux, et à `true` les trois divergeraient (`\u00e9`, `\u007f`,
`\ud83d\ude00`). ⇒ ⭐ **aucun octet ne bouge DU FAIT DE CE CHOIX-LÀ.**

### ⛔⭐ …et la phrase qui suivait était PLUS GÉNÉREUSE QUE LA MESURE — restreinte, avec les trois familles qui bougent quand même

**J'avais écrit « zéro octet ne bouge sur le journal ».** C'est faux comme phrase générale, et
c'est le motif que cette série documente depuis onze affirmations : *une phrase localement vraie
qui grandit d'un cran*. Ce qui est mesuré, c'est que **`ensure_ascii = false` ne coûte aucun
octet** — pas que le journal soit inchangé. **Trois familles bougent, aucune imputable à
`ensure_ascii`**, toutes reproduites sur la même sonde (jansson réel + le `json.hpp` du dépôt,
chaîne `json_loads`/`json_dumps(JSON_INDENT(4))` contre `Json::parse`/`dump(4,' ',false,replace)`) :

| Famille | Avant (`jansson`) | Après (`nlohmann`) | Cause |
|---|---|---|---|
| **ordre des clés** | ordre d'insertion = ordre du **client** : `{"z":1,"a":2,"m":3}` → `z, a, m` | **trié** : `a, m, z` | `std::map`, invariant 1 de l'épique |
| ⭐ **rendu des nombres** | `%.17g` : `0.1` → `0.10000000000000001` · `3.14159` → `3.1415899999999999` · `1e50` → `1.0000000000000001e50` · `1e-7` → `9.9999999999999995e-8` | plus court aller-retour : `0.1` · `3.14159` · `1e+50` · `1e-07` | algorithme de sérialisation des `double` — **et la forme de l'exposant change aussi** |
| **UTF-8 invalide** (document **construit en mémoire**) | `json_string()` rend `NULL`, `json_object_set_new()` rend −1 : **la paire disparaît**, le dump est `{}` | `EF BF BD` par octet fautif | `error_handler_t::replace`, invariant 3 |

**Identiques et vérifiés comme tels** : les entiers (`42`) et les décimaux exactement
représentables (`1.5`).

⚠️ **Nuance mesurée sur la 3ᵉ famille, à NE PAS généraliser** : par les **deux appelants de
production** — qui *parsent* le message du client avant d'appeler — cette famille **n'est pas
atteignable**. Les deux bibliothèques **refusent** un document à octet invalide (`json_loads` :
*« unable to decode byte 0xff »* ; `Json::parse(…, nullptr, false)` : `discarded`) et
`dumpJsonRedacted()` rend `""` **des deux côtés**. La divergence n'existe que pour un document
**construit en mémoire** — ce que fait le cas
`RedactedDumpDoesNotThrowOnInvalidUtf8`, et c'est bien pour ça qu'il est écrit comme ça.

⇒ **Rien à déclarer en `RELEASE_NOTES` pour le choix `ensure_ascii`** (il ne coûte aucun octet) ;
les trois familles ci-dessus, elles, sont **les deltas déjà déclarés de l'épique**, et le journal
en hérite comme n'importe quel autre `dump()`.

`error_handler_t::replace` **est** appliqué : c'est l'invariant qui empêche un `type_error.316` de
tuer une connexion vivante. Les **deux** choix sont épinglés par deux cas neufs de
`core/JsonApiHardening_test` — un relecteur qui veut l'inverse retourne le `false` en `true` et
voit rougir immédiatement.

### ⛔⭐ « Fixture pauvre » ANNONCÉE et **DÉMENTIE PAR LA MESURE** — la 11ᵉ affirmation renversée de la série, et cette fois elle est de MOI

**J'avais prédit un trou, il n'y en a pas.** Le raisonnement était : le seul lecteur de la maison de
référence est un `RoonPlayer` dont `canPlaylist()` **et** `canDatabase()` répondent `false`, donc
échanger les deux capacités dans `buildJsonAudio()` ne peut pas changer un octet, donc aucune suite
ne peut le voir. Chaque maillon est **vrai**. La conclusion est **fausse**.

**Mesuré** : la mutation `M8b` — échange de `playlist` et `database` — rend **1 rouge**,
`core/JsonApiInputGuards_test::JsonApiAudioDbGuardTest.GetHomePublishesACapabilityThatSaysNothingAboutThePointer`.

**Pourquoi** : cette suite (T3.19) **n'utilise pas seulement la maison de référence, elle ajoute ses
propres lecteurs**, et l'un d'eux — `CAPABLE_EMPTY_ID` — porte `database = "true"` et
`playlist = "false"`. Son commentaire dit **exactement pourquoi**, et il a été écrit avant ce
ticket :

> *« la capacité `canPlaylist` jumelle est lue elle aussi, pour qu'échanger les deux clés dans
> `buildJsonAudio()` ne puisse pas passer inaperçu — `CAPABLE_EMPTY_ID` a `database` "true" et
> `playlist` "false", une paire qu'aucune autre entrée ne répète. »*

⇒ **La couverture était là, et l'auteur de T3.19 l'avait posée délibérément.** Mon erreur est celle
que cette série documente depuis dix affirmations : *raisonner sur UNE fixture quand plusieurs
suites en construisent d'autres.* C'est le même mode de défaillance que `F-LINK-1`, transposé de
l'**atteignabilité** vers l'**oracle**.

⭐ **Ce qui a sauvé la mise, et c'est la seule leçon actionnable** : la mutation prédite inerte a été
**JOUÉE QUAND MÊME au lieu d'être sautée**. Une prédiction d'inertie qu'on n'exécute pas n'est pas
une mesure, c'est une opinion — et elle serait entrée dans la fiche comme un fait. ⇒ **règle :
jamais sauter une contre-mutation parce qu'on la croit inerte ; la jouer et publier le chiffre.**

### ⚠️ Une leçon de campagne : **l'ensemble de binaires est un paramètre du protocole, et un pilote l'a montré**

La première passe de campagne portait sur **6** binaires. `M1` (échanger deux IOs d'une pièce dans
`buildJsonRoomIO`) n'a rendu **qu'UN seul rouge** — alors que c'est l'une des trois
contre-mutations que la fiche exige, et qu'elle devrait faire tomber des **goldens**.

Cause : **les goldens de `get_home` vivent dans `core/JsonApiCharacterization_test`**, qui n'était
pas dans l'ensemble. Le pilote a été **jeté et rejoué**, pas rapiécé en vol (on n'édite pas un
harnais pendant son exécution), avec **9** binaires.

⇒ **Un ensemble rouge petit n'est pas d'abord un signe de filet faible : c'est d'abord un signe
d'ensemble de binaires trop étroit.** La question à se poser avant de conclure quoi que ce soit est
*« quel binaire porte l'oracle que cette mutation devrait tuer, et est-il dans ma liste ? »*.

### ⚠️ La requalification d'`E4.1n` par E4.1l est **vraie mais lue trop large** — vérifié au source

`E4.1l` a écrit, à juste titre, que les **events** RemoteUI avaient déjà basculé avec elle et qu'il
n'y avait **rien à faire côté code** là-dessus. La phrase a été reprise dans `BOARD.md` sous la
forme « **Rien à faire côté CODE ici** — ce qui reste est **DOCUMENTAIRE** », et **c'est cette
formulation-là qui est dangereuse** : lue de la ligne `E4.1n`, elle dit que le ticket entier est
documentaire.

**Il ne l'est pas.** Vérifié au source depuis `refactor/e4.1m`, sur l'arbre d'après ma bascule :
`RemoteUI/RemoteUIWebSocketHandler.cpp:236-252` **est toujours là, inchangé** —

    buildJsonState(iolist, [this, iolist, alive](json_t *jret) {
        char *json_str = json_dumps(jret, JSON_COMPACT);
        json_decref(jret);
        Json data = Json::parse(json_str);
        sendJson("remote_ui_io_states", data);
    });

— une sérialisation **et** une désérialisation complètes pour la seule traversée de la frontière
entre les deux bibliothèques. C'est le « meilleur argument concret de toute l'épique » d'`E4.1n.md`,
et il attend `buildJsonState` — qui est le périmètre **de code** d'`E4.1n`, avec
`buildJsonStates`, `buildQuery` et `decodeSetState`.

⇒ **La ligne `E4.1n` de `BOARD.md` a été corrigée par E4.1m** pour restreindre la phrase aux
events. **Le découpage `m` → `s` tient** : rien à re-planifier, seulement une phrase à ne pas
laisser se généraliser toute seule. C'est le même mode de défaillance que les neuf affirmations
d'atteignabilité de cette série — *une phrase localement vraie qui grandit d'un cran à chaque
recopie.*

### ⛔⭐ Une variante de FAUX ARTEFACT que la liste canonique n'a pas : **la campagne a écrit dans un COMMIT**

Ce n'est pas un faux vert ni un faux rouge — les quatorze de la liste canonique portent tous sur le
**résultat d'un test**. Celle-ci porte sur **le livrable**, et elle est passée à travers tous les
contrôles de test parce qu'aucun d'eux ne la regarde.

**Ce qui s'est passé, exactement.** Le commit de documentation a été fait avec `git add -A`
**pendant que la campagne de contre-mutation tournait**. À cet instant précis, l'arbre portait la
mutation `M3` — l'échange du champ masqué et du champ visible de `dumpJsonRedacted()`,
`"cn_pass"` → `"cn_user"`. `git add -A` l'a ramassée, et **le commit de doc a livré, dans `src/`,
un affaiblissement de la fonction qui masque les mots de passe dans les journaux**.

⚠️ **Aucun test ne pouvait le voir** : la campagne restaure depuis la copie pristine à chaque
itération, donc **l'arbre construit était toujours correct** ; le défaut ne vivait que dans l'objet
git. Un `make check` vert, dix contre-mutations et un témoin à ensemble vide sont tous compatibles
avec ce commit-là.

⭐ **Ce qui l'a attrapé** : la comparaison **copie pristine ↔ `HEAD`**, faite pour une tout autre
raison (vérifier qu'une restauration avait bien eu lieu). `pristine == worktree` mais
`pristine != HEAD` : l'anomalie n'est pas dans l'arbre de travail, elle est dans l'historique.

**Corrigé** par `commit --amend` : le commit de documentation ne touche plus **aucun fichier de
`src/`** (`git diff-tree --no-commit-id --name-only -r <sha> -- src` → **0 ligne**), et la ligne
sensible de `HEAD` est de nouveau `"cn_pass"`.

⚠️ **Les deux règles, à appliquer littéralement** :
1. **On ne commite pas pendant qu'une campagne tourne.** La consigne « commite AVANT ta campagne »
   n'est pas une commodité d'ordonnancement : pendant la campagne, `src/` **appartient au
   harnais**.
2. **`git add -A` est interdit tant qu'un harnais peut écrire dans l'arbre.** Nommer les fichiers,
   ou ne rien committer.
   ⭐ Et le contrôle qui ferme la famille, à faire **avant de livrer** :
   `git diff-tree --no-commit-id --name-only -r <chaque commit> -- src` sur **tous** les commits qui
   ne sont pas censés toucher `src/`, plus une comparaison **pristine ↔ `HEAD`** sur les fichiers
   du périmètre. Les deux sont mécaniques et coûtent une seconde.

### ⭐ `M6` : une contre-mutation **structurellement inerte**, et c'est la MIGRATION qui l'a rendue telle

Échanger les deux premiers identifiants demandés dans `buildJsonGetIO()` — une mutation par échange
en bonne et due forme, sur la fonction qui construit la réponse de `get_io` — laisse un ensemble
rouge **VIDE**.

**Ce n'est pas un trou de filet, c'est une conséquence directe de ce ticket.** La réponse de
`get_io` est un objet **indexé par identifiant d'IO**. Sous `jansson`, l'objet gardait l'ordre
d'insertion, donc l'ordre de la **requête** : permuter deux identifiants demandés changeait le
document servi, et un oracle aurait pu le voir. Sous `nlohmann::json`, l'objet est un `std::map`
et sort **trié** : l'ordre de la requête n'est plus observable **du tout**, par personne.

⇒ **La mutation est inobservable parce que l'information qu'elle déplace a cessé d'exister sur le
wire.** Déclaré ici plutôt que caché, exactement comme `M8` d'E4.1l : un ensemble rouge vide n'est
acceptable que **nommé et expliqué**. ⚠️ **À qui écrira un oracle sur `get_io` plus tard** : ne
cherchez pas à couvrir l'ordre de la requête, il n'y a plus rien à couvrir. Ce qui reste couvrable,
et couvert, c'est que la réponse est **triée** (`K_GetIoMapIsSortedByIdNotByRequestOrder`) et que
chaque IO y porte le bon contenu.

### ⛔⭐⭐ Le masquage des mots de passe ne tenait à **rien** — un `str_to_lower()` que RIEN ne couvrait, et la mutation qui l'enlève laisse le `make check` ENTIER au vert

**Trouvé par la revue, fermé par cette livraison.** `JsonApi::dumpJsonRedacted()` compare la clé à
une liste **entièrement en minuscules** (`"cn_pass"`, `"password"`, `"token"`, `"authorization"`,
…) après lui avoir appliqué `Utils::str_to_lower()`. **Ce `str_to_lower()` est la seule chose qui
masque `"CN_PASS"`, `"Password"` ou `"Authorization"`.**

**Mutation `X2` du relecteur — retirer ce seul appel** : `make check` **complet au VERT** —
`# TOTAL 99 / PASS 98 / SKIP 1 / FAIL 0`, **1632 cas**, **93 `CXXLD`**, **0 rouge**. ⇒ ⭐ **des
mots de passe cesseraient d'être masqués dans le journal EN SILENCE, et aucun test du dépôt ne le
dirait.** Cause : **aucun cas de l'arbre n'envoyait une clé de credential en casse mixte** — les
trois cas de `JsonApiRedact` étaient tous en minuscules.

⚠️ **Le défaut de couverture est PRÉÉXISTANT** (le `str_to_lower()` était déjà là dans la version
jansson, `JsonApi.cpp:216` d'avant bascule) — **mais c'est exactement la fonction que l'incident de
ce ticket avait mutée dans un commit**, et la fiche lui ajoutait deux cas *sans* fermer celui-là.
Le laisser ouvert aurait été le motif de la nuit : *renforcer un oracle à côté du trou.*

⭐ **Fermé** : `JsonApiRedact.HidesCredentialFieldsWhateverTheKeyCase`
(`tests/core/JsonApiHardening_test.cpp`), deux clés **réalistes** en casse mixte — `"CN_Pass"` et
`"Authorization"` (l'orthographe HTTP normale). ⚠️ **Chaque assertion est une PAIRE** : le secret
est **absent** *et* la paire est **encore là, masquée** (`"CN_Pass": "***"`). Une assertion
d'absence seule aurait aussi passé sur un dump qui **supprime** le champ — un autre défaut, pas
celui-ci. Un troisième assert garde une clé **non-credential** en casse mixte (`"Action"`) lisible,
pour qu'un « masquer tout » ne passe pas non plus.

⇒ **`X2` rejouée sur l'arbre livré : elle ROUGIT** (chiffres en fin de section).

### ⛔⭐⭐ Une mine posée SIX TICKETS à l'avance, dans une acceptation — `!Json` **compile**, et lève dans un callback que rien n'attrape

L'acceptation n° 6 de `E4.1m.md` disait qu'`E4.1o` *« va casser cette ligne à la compilation
(`!Json` ne compile pas) »*, à propos de `ScriptExec.cpp:131` :
`if (!jsonApi->buildJsonSetParam(p))`. **C'est faux, et la phrase est plus dangereuse que le
défaut** : elle promet au futur auteur d'`E4.1o` un filet — le compilateur — qui n'existe pas.

**Mesuré**, sonde `g++ -std=c++17` contre le `json.hpp` du dépôt, dans l'image de build :

| Retour de `buildJsonSetParam()` après E4.1o | `!Json` |
|---|---|
| objet non vide (`{"success":"true"}`) | **compile**, puis **lève** `type_error.302` — *« type must be boolean, but is object »* |
| objet vide | **compile**, puis **lève** `type_error.302` |
| `Json` nul | **compile**, puis **lève** `type_error.302` — *« but is null »* |

**Pourquoi** : `JSON_USE_IMPLICIT_CONVERSIONS` vaut **1** — `src/lib/json.hpp:2812-2813`, un
`#ifndef` / `#define … 1` **jamais surchargé**. Balayage de l'arbre entier (`.h`, `.hpp`, `.cpp`,
`.am`, `.ac`, `.in`, `.m4`) : **3 occurrences, les trois dans `json.hpp`**, **zéro `-D`**. La
conversion implicite `operator ValueType()` est donc active, `bool` est déduit, et
`get<bool>()` lève sur tout ce qui n'est pas un booléen. **Aucune valeur de retour ne rend cette
ligne inoffensive.**

⛔ **Et l'endroit où elle lève est le pire du fichier** : la lambda de
`process->messageReceived` (`LuaScript/ScriptExec.cpp:93-146`), appelée depuis la boucle `uvw`.
**Aucun `try`/`catch` au-dessus** — vérifié fichier par fichier : `LuaScript/ScriptExec.cpp`,
`IO/ExternProc.cpp`, `IO/ExternProc.h` n'en portent **aucun**. ⇒ **`std::terminate` de
`calaos_server`**, déclenchable par **tout script Lua qui fait un `set_param`** : le chemin
**nominal**, pas un cas limite.

⭐ **C'est EXACTEMENT le précédent KNX**, § *« E4.1e — le wire KNX transporte des octets bruts du
bus : `dump()` nu = `std::terminate` »* plus haut dans ce fichier : un appel `nlohmann` qui lève
dans `KNXProcess::monitorWait()`, rien qui attrape au-dessus, `calaos_knx` terminé par une **trame
de bus ordinaire** (un gradateur EIS 6 à 78 % suffit). La différence tient en un mot : là-bas
c'était `type_error.316` depuis `dump()`, ici c'est `type_error.302` depuis une **conversion
implicite**.

⚠️ **Et `make check` ne préviendra pas non plus** : `F-LINK-1`, mesuré par ce ticket, établit que
**les trois sites de `ScriptExec.cpp` ne sont atteints par AUCUNE suite** (0 passage sur
92 binaires). Compilateur muet + suite muette = la panne apparaît **chez l'utilisateur**.

⭐ **Bonus mesuré au passage : la garde actuelle est DÉJÀ INERTE.** Les deux branches de
`buildJsonSetParam()` finissent par `return jansson_from_params(ret)`, qui rend `json_object()` —
jamais `NULL` hors OOM. L'échec est signalé **dans le document** (`{"error": "wrong io/param"}`),
pas par le pointeur : `cWarningDom("lua") << "Failed to decode set_param…"` **n'a jamais été
imprimé**. La réécriture demandée à `E4.1o` n'est donc pas seulement sûre, elle **répare** cette
garde morte. (La **fuite** de `json_t *` du même site, elle, disparaît d'elle-même avec
`nlohmann`.)

⇒ **Réécrit à trois endroits** : `E4.1m.md` (acceptation n° 6), `E4.1o.md` (§ *Pièges propres*, en
tête, là où l'auteur du ticket le lira) et ici. ⚠️ **La leçon générale, et elle vaut au-delà de ce
site** : *« ça ne compilera pas »* est une **prédiction**, pas une mesure. Sur `nlohmann::json`
avec les conversions implicites actives, **presque tout compile** — c'est la bibliothèque entière
qui est conçue pour ça. Une promesse de garde-fou au compilateur doit être **compilée** avant
d'être écrite.

## T3.49 — `F-FLAKY-1` **FERMÉ** : ce n'était pas le vol de temps dans le pompage, c'est l'horloge EN CACHE de libuv (2026-08-26)

- ⭐ **[F-FLAKY-1, FERMÉ par [`T3.49`](T3.49.md)] — `uv_timer_start()` ne lit PAS l'horloge.** Il
  calcule son échéance depuis `loop->time`, l'horloge **en cache** de la boucle, que seul
  `uv__update_time()` rafraîchit **en tête de chaque `uv_run()`**. Toute plage de temps mural
  pendant laquelle **personne ne pompe** arme donc les échéances **dans le passé**, d'exactement la
  longueur de cette plage. Dans une suite gtest, cette plage est le démontage/montage de fixture
  plus `loadConfig()` : quelques ms à vide, **plus de 73 ms sous 96 brûleurs**.
  ⇒ **Le seuil d'un tel flottement est `gap > marge`**, pas « la contention » : un **seuil**, pas une
  probabilité, ce qui explique 0 % à vide et ~10 % sous charge sur la **même** machine.
  ⛔ **Le mécanisme qui était écrit dans la fiche — « l'ordonnanceur vole du temps à `pumpLoopFor` »
  — est INFIRMÉ par la mesure** : `pumpLoopFor(ms)` ne fait tourner les minuteries qu'au **début**
  de chaque itération et n'itère que tant que `elapsed < ms`, donc **tous** les instants de tir
  possibles sont `< ms`, strictement avant l'échéance. Sonde autonome liant la `libuv 1.44.2` de
  l'image : **40 ms de vol délibéré dans CHAQUE itération ne font jamais tirer l'échéance dans la
  fenêtre**, tandis qu'un **gap oisif de 120 ms** la fait tirer à **62 ms** pour une sonde à 91.
  ⚠️ **Conséquence de méthode, et c'est la leçon transportable** : *quand une attente à l'horloge
  murale flotte, regarder d'abord ce qui s'est passé **AVANT** l'attente, pas pendant.*

- ⛔ **[F-FLAKY-1] Une borne inférieure ne suffit pas — la n° 13 a un côté qu'on n'avait pas vu.**
  La forme retenue par [`T3.40`](T3.40.md) (*mesurer l'instant où le prédicat tient, puis en exiger
  une borne inférieure*) est juste **contre le temps volé après l'origine**, et c'est bien tout ce
  qu'il y avait dans le cas de T3.40. Elle ne couvre **pas** l'échéance qui **recule** : avec une
  horloge de boucle vieille de `gap`, la réponse devient **plus petite**, et la borne inférieure
  tombe. **Mesuré** : `gap=120`, échéance 182, sonde 91 ⇒ `pumpUntil` aurait rendu **62**.
  ⇒ **Une borne inférieure n'est un rempart que si son ORIGINE et l'ÉCHÉANCE sont dans la même
  horloge.** Il faut rafraîchir l'horloge de la boucle **juste avant** d'armer, sans quoi les deux
  origines dérivent l'une de l'autre.

- ⛔ **[F-FLAKY-1] Le même défaut latent existait dans le jumeau `core/IoLifetimeTimer_test`** — les
  **cinq** bornes inférieures du fichier mesurent désormais depuis **avant** le rafraîchissement,
  donc avant l'armement. ⚠️ **Mais le lien avec le *« rouge non reproductible »* que T3.40 a mesuré
  à 1 sur ~80 sur `ProcessExitedStillFiresWhileTheServerIsAlive` est une HYPOTHÈSE, pas un
  diagnostic.** Le mécanisme **suffirait** — marges revérifiées au source : `ExternProc`
  (`ExternProc.cpp:196` et `:205`, `singleShot(0.1)`, borne 40) ⇒ **60 ms**, la plus courte ; les
  trois `Reset` (250 / 90) ⇒ **160** ; `KNX` (1500 / 90) ⇒ **1410**. ⛔ **Et il ne se reproduit
  pas** : **240 exécutions** de la forme `master` du jumeau sous les **mêmes 96 brûleurs** (200 du
  cas filtré + 40 de la suite entière) rendent **0 rouge**, là où le même protocole rend **2/48**
  sur `ShutterImpulse`.
  ⚠️ ⭐ **Leçon de rédaction, et elle vise ce fichier-ci** : la fiche écrivait « plausible, non
  rejoué » — honnête — et **`FINDINGS.md` avait durci en « très probablement »**, donc *plus fort
  que la mesure*. **Un report d'une fiche vers le journal ne doit jamais monter d'un cran dans
  l'échelle de certitude** ; c'est le sens de la marche qui trahit, pas le mot choisi.

- ⚠️ **[F-FLAKY-1] Un tableau de marges qui prédit à l'envers vaut moins que pas de tableau.** Le
  §1.1 de la fiche calculait `marge = durée demandée − sonde` en oubliant que l'échéance vaut
  `impulse_action_time + impulse_time`. Marges réelles **73 · 91 · 159 · 180 · 231 ms** (et non
  56 · 73 · 124 · 139 · 190), ce qui **inverse les deux premiers** : c'est `:384` le plus court, et
  c'est bien lui que les observations frappent le plus souvent (§2 : 3× contre 2× ; cette
  campagne : **5 rouges sur 5**).

- ⭐ **[F-FLAKY-1] `RELEASE_NOTES` : rien de dû, et ce n'est pas une omission — c'est une
  vérification.** L'horloge en cache **ne peut pas** décaler une échéance en production : dans une
  boucle d'événements mono-thread, tout code applicatif tourne **à l'intérieur** d'un
  `uv_run()`, donc `loop->time` n'est jamais vieille de plus d'une itération.
  ⚠️ **L'exception est le DÉMARRAGE, et « la seule exception est `KNXIo` » était INCOMPLET** —
  voir le point suivant.

- ⛔ **[F-FLAKY-1 → produit] Au démarrage, TOUTE attente armée pendant le chargement de la
  configuration est amputée de la durée de ce chargement.** `uvw::Loop::getDefault()` naît au
  premier usage — `CalaosConfig.cpp:206`, le `Timer(60)` du cache d'état, dans le constructeur de
  `Config` — et `uv_loop_init()` y fige `loop->time`. `main.cpp:150-151` construit **toute** la
  configuration (`LoadConfigIO`/`LoadConfigRule`, **seuls sites d'appel de chacune**, vérifié) ;
  `main.cpp:223` (`loop->run()`) est 73 lignes plus loin ; **rien entre les deux ne rafraîchit
  l'horloge**. ⇒ ⭐ **le décalage vaut exactement le temps écoulé entre la création de la boucle et
  l'armement, c'est-à-dire la durée du chargement de la configuration : ces attentes sont plus
  courtes qu'annoncé, et celles dont le délai est inférieur à ce temps tirent IMMÉDIATEMENT, au
  premier tour de boucle.**
  ⭐ **Balayage** (`(singleShot|singleIdler)\s*\(`, `new Timer\s*\(`, `make_shared<Timer>\s*\(`,
  commentaires écartés, `src/lib/Timer.{h,cpp}` exclu car c'est l'API) : **99 sites dans 38
  fichiers**, dont **9 dans un constructeur** au sens syntaxique, plus `KNX/KNXIo.h:78` (corps en
  en-tête, hors de portée du balayage) et `CalaosConfig.cpp:232` (pas un constructeur, mais appelé
  **depuis** `LoadConfigIO`). ⚠️ **Le classement pré-boucle a été fait à la main sur ces 11 sites
  seulement ; les 88 autres ne sont pas classés** — le cardinal est publié à côté du résultat.
  **Les onze** : `Wago/WagoMap.cpp:46` **0,1 s** (⛔ le plus court de l'arbre) · `:47` 10 s ·
  `Hue/HueOutputLightRGB.cpp:47` **2 s** · `KNX/KNXIo.h:78` **1,5 s** (7 constructeurs) ·
  ⭐ `Audio/RoonPlayer.cpp:243` **10 s**, dont le commentaire dit *« wait for the process to
  start »* · ⭐ `CalaosConfig.cpp:232` **30 s**, dont le commentaire promet *« defer the
  notification until the server is fully up »* — la promesse littérale tient (rien ne tire avant
  `loop->run()`) mais **le sursis de 30 s est amputé d'autant, et nul si le chargement dépasse
  30 s** · `Audio/AVRRose.cpp:67` 30 s · `IO/InputAnalog.cpp:71` 4 h (négligeable) ·
  `CalaosConfig.cpp:206` 60 s — ⭐ **celui-là n'est pas victime, c'est l'ORIGINE : il crée la
  boucle**.
  ⭐ **Et la même horloge fuit en HORODATAGE, pas seulement en armement** :
  `Utils::getMainLoopTime()` (`Utils.cpp:136`) rend `loop->now()`, donc `InputAnalog.cpp:63`
  estampille `timer` à la **création de la boucle** et le premier `readValue()` périodique arrive
  en avance d'autant. *(`LuaScript/ScriptManager.h:65` documente déjà ce piège pour son chien de
  garde : le seul endroit de l'arbre qui l'avait vu.)*
  ⭐ **Bonne nouvelle, et elle borne le risque : il n'y a PAS de rechargement de configuration en
  production.** Quand l'API réécrit la configuration, `JsonApiHandlerHttp.cpp:707` pose
  `setNeedRestart(true)` et `HttpClient.cpp:480` appelle `uvw::Loop::getDefault()->stop()` ⇒ **le
  processus REDÉMARRE**. Et la création d'IO à chaud (`JsonApi.cpp:2004`, dans `buildAutoscenarioCreate` → `ListeRoom::createIO`)
  se fait **à l'intérieur** de `uv_run()`, sur une horloge fraîche. ⇒ **le risque est cantonné au
  démarrage, une fois par vie du processus.**
  ⚠️ **Non mesuré sur un vrai démarrage** : ni la durée du chargement, ni un tir prématuré observé.
  Lecture de source. **Aucun ticket ouvert** (`T3.40`–`T3.54` sont pris) ; `WagoMap.cpp:46` et
  `RoonPlayer.cpp:243` sont les deux qui en méritent un — **numéro à demander**.

- ⛔ **[F-FLAKY-1, passe de correction] « Rafraîchir l'horloge avant d'armer » ne suffit pas : il
  faut prendre l'ORIGINE avant le rafraîchissement.** `uv__update_time()` tourne en **tête** de
  `uv_run()`, donc une origine prise **après** le retour de l'itération de rafraîchissement est
  postérieure à `loop->time` de **tout ce que coûte la queue de cette itération** — et l'échéance,
  calculée depuis `loop->time`, atterrit d'autant **avant** `origine + délai`. **C'est le même
  défaut, déplacé dans la queue de l'itération**, et la 1ʳᵉ rédaction de `T3.49` écrivait
  « `gap > marge` devient inatteignable », ce qui était **trop fort**.
  ⭐ **Ce qui est vrai après correction est une INÉGALITÉ, pas une petitesse** : origine prise
  d'abord ⇒ **`loop->time ≥ origine`** ⇒ **`échéance ≥ origine + délai`**, *par construction*,
  quel que soit le temps passé dans l'itération ou volé par l'hôte. **Il n'y a plus de `gap` à
  comparer à une marge : le membre de gauche a disparu.**
  ⭐ **Mesuré** (sonde autonome, `libuv 1.44.2` de l'image) en rendant l'itération de
  rafraîchissement coûteuse de `D` ms : ordre d'avant ⇒ `D=0` 182, `D=40` 141, **`D=95` 87 sous une
  sonde de 91, 20/20 rouges**, `D=200` **0** ; ordre livré ⇒ **182 pour tout `D`, 0/20**. Même
  résultat sur le couple de `:384` (**51** contre **146**, sonde 73).
  ⚠️ ⭐ **Et la sonde a corrigé le mécanisme au passage : `libuv` relit l'horloge DEUX fois par
  itération** — en tête, **et inconditionnellement après `epoll_pwait()`** (`uv__io_poll`, dont le
  commentaire dit qu'il n'y a *« aucune garantie que le système ne nous ait pas déordonnancés
  pendant l'appel »*). La fenêtre résiduelle n'est donc **pas** l'itération entière mais sa
  **queue après le sondage** : `uv__run_check`, les fermetures, le retour, et tout
  déordonnancement avant la prise de l'origine. **Vérifié en brûlant `D` des deux côtés du
  sondage** : avant ⇒ 182 partout, **aucun rouge** ; après ⇒ le tableau ci-dessus. C'est pourquoi
  la première livraison passait le `make check` : cette queue vaut **normalement** des
  microsecondes. ⭐ **« Normalement » n'est pas une borne** — et c'est la différence entre un test
  qui passe et un test qui ne peut pas échouer.

- ⚠️ **[Faux vert, méthode] Une garde peut être VACANTE PAR CONSTRUCTION et se vendre comme une
  post-condition.** Le `pumpLoopFor()` de `T3.49` ajoutait `if (iterations < 1 || elapsed < ms)
  ADD_FAILURE()` sous la bannière *« une attente doit dire si elle a attendu »*. **`t0` étant pris
  à l'entrée, la condition de boucle tient à l'entrée, le corps tourne au moins une fois, et la
  boucle ne sort que quand `elapsed ≥ ms`** : les deux clauses sont **inatteignables depuis tous
  les sites d'appel existants**. Inoffensif, mais **la bannière survendait**. ⭐ **Ce qui était
  atteignable, c'est l'ARGUMENT** : `pumpLoopFor(0)` est un non-événement déguisé en attente — la
  famille exacte du faux vert n° 13. Garde rendue non vacante (`ms < 1 || …`) et **redite pour ce
  qu'elle est**. ⭐ *Avant d'écrire qu'une garde protège de X, chercher l'exécution qui la
  déclenche ; si elle n'existe pas, c'est le libellé qu'il faut corriger, pas la garde qu'il faut
  garder telle quelle.*

- ⚠️ **[Méthode, chiffres] Un taux de flottement n'est pas une constante, et « exactement le même
  chiffre » se lit comme une loi.** `T3.49` écrivait *« 5 rouges sur 48, exactement le chiffre du
  §2 »*. Le §2 relevait **2/48 sur `:297` et 3/48 sur `:384`** — même total, répartition
  différente — quand la campagne de livraison rend **5/48 tous sur `:384`**. Ces comptages sont des
  tirages de Poisson sur un **seuil** (`gap > marge`) dont l'issue dépend de la charge de l'hôte à
  la milliseconde : **5/48 et 2/48 sont compatibles**, et l'égalité des totaux est une coïncidence.
  ⭐ **Ce qui porte la démonstration, c'est l'ORDRE, pas le TAUX** : la forme de `master` redevient
  rouge dans le même conteneur à la même charge, la forme livrée y est **0/48**, et les rouges
  tombent sur la marge la plus courte du tableau corrigé.

## T3.53 — la paire DALI `(command, result)` : quand les deux types sont le MÊME type (2026-08-26)

⭐ **La conclusion qui vaut d'être retenue : c'est la seule paire de la chaîne Wago dont la mutation
par échange NE PEUT PAS ÊTRE ÉCRITE.** `WagoMap.h:81-82` déclaraient
`sigc::slot/signal<void, bool, string, string>`. Permuter les deux `string` d'un `typedef` produit
**le même texte**. Il n'y a rien à muter, et pourtant les deux rôles sont bel et bien intervertis à
l'exécution. C'est la forme la plus pure du défaut que T3.31, T3.46 et T3.50 ont chassé sous une
forme atténuée (deux types **distincts** qui se convertissent).

**Recompté** (`python3`, commentaires blanchis) : **5 déclarations / 5 définitions / 4
`sigc::mem_fun`** — la fiche était exacte site pour site. **Deux ajouts mesurés qu'elle n'avait
pas** : le `typedef` de la fente est **double** (`WagoUdp_cb` **et** `WagoUdp_signal`), et il
n'existe **qu'un seul site d'émission**, `WagoMap.cpp:439`. **13 sites**, pas 10.

⭐ **F-WAGO-11 — une implémentation MORTE au milieu d'une paire vivante.**
`WODaliRVB::WagoUDPCommand_cb` (`WODaliRVB.h:41`, `WODaliRVB.cpp:164`) est la **5ᵉ** définition et
il n'y a que **4** enregistrements. Son adresse n'est prise par **aucune ligne de l'arbre**
(balayage de l'arbre entier). ⛔ **Conséquence à ne pas arrondir : le typage ne la ferme PAS.**
Mesuré, mutation M8 : après le correctif, permuter ses deux paramètres compile toujours (`rc=0`,
0 `error:`), parce que **rien ne la lie et donc rien ne peut la refuser**. Elle est typée pour le
contrat, un point c'est tout. **Sa suppression n'a pas été faite** — T3.46 avait supprimé une
déclaration *sans définition* ; celle-ci en a une, c'est un autre arbitrage. **Non ouvert, non
numéroté au board.**

⭐ **L'AFFIRMATION « les deux membres sont lus » ÉTAIT UNE LECTURE ; elle est maintenant MESURÉE, et
elle tient** — contrairement à celle de la fiche de T3.50, que sa propre campagne avait infirmée.
`-Wunused-parameter` sur les **14** unités de `IO/Wago/`, compilation réelle, **0 unité en échec**
(sans quoi « zéro avertissement » serait un mensonge), **témoin négatif** (0 avertissement sans le
drapeau) et **témoin positif** (les `addr` inutilisés des callbacks d'écriture sont bien trouvés,
7 occurrences). ⭐ **Et une SONDE AU SITE** : faire cesser la lecture de `result` dans `WODali` fait
apparaître `WODali.cpp:82:68: warning: unused parameter 'result'`. **C'est cette sonde qui
transforme un silence en mesure** — sans elle, « zéro avertissement » ressemble exactement à la
réponse attendue.
⇒ **La permutation change le programme** : `WODali` ne prend plus jamais sa branche
`WAGO_DALI_GET`, et chaque canal de `WODaliRVB` **range son adresse DALI comme niveau**. **Une
valeur fausse vivante**, pas un no-op.

⭐ **UN TEST COMPORTEMENTAL EXISTE ET IL EST PROUVÉ CAPABLE DE ROUGIR.** ⚠️ **Portée au grain de la
mesure, après correction d'une première rédaction trop large** : il n'y avait **`0` fichier nommé
`*Dali*`** sous `tests/` et **aucun test du chemin de RÉPONSE UDP** — mais **pas** « aucun test
DALI » : `master` porte déjà `ADaliOutputWithABlankPortKeepsTheModbusDefault` et
`ADaliRvbOutputWithABlankPortKeepsTheModbusDefault` (`tests/core/WagoPortDefault_test.cpp:220`,
`:228`), qui construisent des `WODali`/`WODaliRVB` de production par la fabrique — sur le **défaut
de port Modbus**, jamais sur `udpRequest_cb()`.
`tests/core/WagoUdpReply_test.cpp` construit un `WODali` et un `WODaliRVB` par
le **constructeur de production** via `IOFactory`, les nourrit par `WagoMap::udpRequest_cb()`
(public) et relit par `get_value_string()` (public). Sur `master`, permutation appliquée au seul
site d'émission puis binaire **reconstruit et exécuté** : **2 cas passent au ROUGE**. Les 3 qui
restent verts sont des **contrôles** dont ce n'est pas le rôle — dit plutôt qu'arrondi.

⚠️ **UNE 16ᵉ VARIANTE DE FAUX VERT, ET ELLE M'A EU** — à ajouter à la liste canonique :
**deux mutations DIFFÉRENTES peuvent produire des ensembles de lignes `error:` IDENTIQUES sans que
le typage y soit pour rien.** Les permutations de `…Red_cb`, `…Green_cb` et `…Blue_cb` donnaient
**3 paires identiques sur 4 ensembles non vides** : sigc++ signale l'échec depuis
`adaptor_trait.h` et nomme le **foncteur**, identique pour les trois canaux. Le remède cherché fut
d'enrichir le VERDICT du contexte d'instanciation de gcc (`… required from here`).

⛔ **ET LE REMÈDE ÉTAIT LUI-MÊME UN FAUX VERT — c'est la vraie leçon, mesurée à la reprise.**
L'enrichissement prenait le **dernier `required from here` du journal `make -k -j8` ENTIER**, un
journal **entrelacé** où l'ordre ne veut rien dire. Campagne **rejouée** : cette règle désigne
`IO/Wago/WOVoletSmart.cpp:44` — **une unité de compilation ÉTRANGÈRE** — pour **M3, M4, M5, M6 et
M7** à la fois, **et même pour les trois témoins où RIEN n'échoue** (M0, M8, M0-end). ⇒ **sous
cette règle M5, M6 et M7 redeviennent identiques : 3 paires identiques.** Le garde-fou contre le
faux vert **était** un faux vert.

⭐ **LE CORRECTIF, ET IL EST GÉNÉRAL : ASSOCIER PAR UNITÉ DE COMPILATION, PAS PAR PROXIMITÉ DANS LE
JOURNAL.** (1) le passage `-k -j8` ne sert plus qu'au **code de retour** et à **l'ensemble des
cibles en échec**, lues dans les lignes que **make** écrit lui-même — `*** [<makefile>:<ligne>:
<cible>] Error N`, une ligne, une cible nommée ; (2) **chaque cible en échec est recompilée SEULE,
dans son propre processus, avec sa propre sortie capturée** ⇒ l'association est **par
construction**, jamais par lecture. Résultat : **0 paire identique sur 7 verdicts non vides**,
**ensemble VIDE sur les 3 témoins**, chaque implémentation refusée **à sa propre ligne
d'enregistrement** (`WODali.cpp:62`, `WODaliRVB.cpp:73`, `:75`, `:77`), et **8/8** des sites
attribués appartiennent bien au `.cpp` de leur unité. ⚠️ **À retenir hors de ce ticket : un verdict
de mutation qui dépouille un journal PARALLÈLE dépouille un document où l'ordre n'a pas de sens.**

⭐ **ET UNE LEÇON D'ÉCRITURE, LA TROISIÈME DE LA MÊME NUIT** *(après E4.1l et T3.53 §7.6)* :
**écrire sa portée au plus près de ce qu'on a MESURÉ.** Trois affirmations de cette campagne
étaient *plus larges* que leur mesure — « aucun test DALI » (mesuré : **0 fichier nommé `*Dali*`**
et **aucun test du chemin de réponse UDP**), « il n'existe aucun fichier `RELEASE_NOTES` » (mesuré :
**aucune ENTRÉE due** ; le fichier existe, suivi, 1279 lignes) et « ensembles distincts » (mesuré :
distincts **une fois l'association construite par unité**). ⚠️ **Aucune des trois n'était une erreur
de mesure : les trois étaient des erreurs de PHRASE.** Une conclusion juste énoncée trop largement
est indiscernable d'une conclusion fausse pour le lecteur suivant — et c'est lui qui la recopiera.

⚠️ **Et un piège de build rencontré, à ne pas confondre avec un rouge** : après un changement
d'en-tête seul, l'objet de test de `tests/` n'a **pas** été reconstruit et le binaire relié
(`CXXLD`) rendait encore l'ancien verdict — **4 cas faussement rouges**. Les `.deps` avaient été
brouillées par les `os.utime` de la campagne. **Un `rm -f` de l'objet du test, pas seulement du
binaire, avant de juger.**

⭐ **LE MÊME DÉFAUT EXISTE HORS DE WAGO, ET IL EST 5× PLUS GROS — numéroté [T3.55](T3.55.md).**
`Audio/Squeezebox.h:37-38` déclarent `sigc::slot/signal<void, bool, string, string,
AudioPlayerData>` : **la même paire adjacente de type identique**, émise une seule fois
(`Squeezebox.cpp:408`). **Recompté** : **84 sites** (2 `typedef`, 27 déclarations, 26 définitions,
28 `sigc::mem_fun` sur 26 cibles, 1 émission) contre **17** pour la paire DALI recomptée à la même
règle — ⚠️ **et les deux conventions sont dites** : sans les enregistrements (celle de T3.53 §5,
qui annonce **13**), Squeezebox pèse **56** ⇒ ≈4,3× ; enregistrements inclus, **84** contre **17**
⇒ ≈4,9×. **Le signalement initial disait « ~78 contre 13 » : corrigé.**

⭐ **Et la sonde au site y répond L'INVERSE de ce qu'elle répond ici, ce qui interdit de recopier la
conclusion** : `g++ -Wunused-parameter` ajouté après le `-Wno-unused-parameter` du projet, unité
compilée (`rc=0`), **témoin négatif à 0**, sonde au site validée
(`Squeezebox.cpp:718: warning: unused parameter 'result'` apparaît exactement là quand on fait
cesser la lecture, et les `request` tombent de 26 à 25) ⇒ ⛔ **`result` est lu par les 26
implémentations, `request` par AUCUNE — 26 avertissements `unused parameter 'request'`, un par
définition.**

⚠️ **Ce n'est PAS l'atténuation de T3.50** — où **les deux** membres étaient morts. Ici un seul
l'est, et c'est **l'autre** qui décide : après permutation le paramètre `result` reçoit **le texte
de la commande**, et **les 26 analyseurs le parsent**. ⇒ **valeurs fausses vivantes**, gravité de
T3.46/T3.53. ⚠️ **Et aucun filet** : `tests/` porte bien **2 fichiers nommés `*Squeezebox*`**
(24 cas), mais **aucun des deux ne LIE `Audio/Squeezebox.$(OBJEXT)`** — ils réimplantent la logique
(`SqueezeboxWire_test.cpp:141` annonce porter le *verbatim body* de la fonction) — et **aucun
n'atteint le chemin de réponse**. ⭐ **`F-LINK-1` dans sa forme la plus littérale.**

## E4.1n — `JsonApi`, l'état : ce que la bascule a mesuré (2026-08-26)

### ⛔⭐ La requalification d'`E4.1n` était FAUSSE une deuxième fois — et cette fois elle avait été « revérifiée »

E4.1m avait déjà restreint la phrase d'E4.1l aux **events** (§ *« La requalification d'`E4.1n` par
E4.1l est vraie mais lue trop large »*, plus haut). Le board a ensuite affirmé que la
requalification « documentaire » avait été **revérifiée vraie**. ⛔ **Elle ne l'était pas**, et
E4.1n l'a mesurée une **troisième** fois, sur `master` `75ed9cb9` :

    RemoteUIWebSocketHandler.cpp:236   buildJsonState(iolist, [...](json_t *jret)
    RemoteUIWebSocketHandler.cpp:246   char *json_str = json_dumps(jret, JSON_COMPACT);
    RemoteUIWebSocketHandler.cpp:250   Json data = Json::parse(json_str);

⭐ **La leçon générale, et c'est la même que pour les dix affirmations d'atteignabilité de cette
série** : une phrase localement vraie (« les *events* RemoteUI ont déjà basculé ») a grandi d'un
cran à chaque recopie jusqu'à « le ticket est documentaire », **et la recopie suivante l'a
certifiée**. ⚠️ **« Revérifié » sans la commande qui l'a revérifié n'est pas une mesure.** Remède
appliqué : la fiche `E4.1n.md` porte désormais le `grep` et les trois numéros de ligne, en tête,
sous un marqueur qui dit laquelle des trois versions fait foi.

### ⭐ La mine du pont : `Json::parse` **non gardé** dans un callback de boucle — même famille que KNX et `E4.1o`

`RemoteUIWebSocketHandler.cpp:250` était le **seul `Json::parse` du fichier sans `try` au-dessus**
(`:128` et `:185` sont dans un `try`), appelé depuis la complétion de `buildJsonState()`,
c'est-à-dire depuis un callback audio **sur la boucle d'événements**. Une `parse_error` là-dedans =
**`std::terminate` sur un serveur vivant**, exactement la forme du précédent KNX (`dump()` nu dans
`monitorWait()`) et de la mine d'`E4.1o` (`!Json` dans `ScriptExec.cpp`).

⭐ **Verdict honnête, et il est NÉGATIF** : aucune entrée ne la faisait lever **aujourd'hui**.
`json_string()` refuse l'UTF-8 invalide à la construction, donc `json_dumps` ne pouvait rien
produire que `Json::parse` refuse, et son seul autre échec (`NULL`) était gardé par le `if` de
`:249`. ⇒ **mine LATENTE, désamorcée par la validation même que cette épique retire.** Elle a été
**supprimée** plutôt que gardée : c'est la seule façon d'être sûr qu'elle ne s'arme pas.

⚠️ **Ce que cela dit du reste de la série** : chaque fois qu'un `json_string()` disparaît, une
validation d'UTF-8 disparaît avec lui, et les formes `nlohmann` en aval qui étaient **protégées par
jansson sans le savoir** deviennent atteignables. Ce ticket n'en a trouvé qu'une sur son périmètre ;
**le balayage vaut d'être refait dans chaque sous-ticket suivant, sur SON périmètre.**

### ⭐ Les cinq deltas remesurés une troisième fois — et **toujours pas de sixième**

Sonde `g++ -std=c++17` contre le jansson et le `json.hpp` de l'arbre, sur les formes exactes de
cette chaîne (résultats verbatim, pas recopiés d'E4.1l ni d'E4.1m) :

| # | jansson | nlohmann |
|---|---|---|
| 1 ordre des clés | `{"zulu":"1","alpha":"2"}` | `{"alpha":"2","zulu":"1"}` |
| 2 casse hexa | `é` (majuscules) | `é` (minuscules) |
| 3 UTF-8 invalide | `json_string()` rend `NULL`, `json_object_set_new()` rend `-1`, **paire entière perdue**, résultat `{}` | `{"k":"a��z"}` — **un U+FFFD par octet invalide** |
| 4 DEL `0x7F` | octet **brut** sur le wire, **12 octets** | echappe (`\u007f`), **17 octets** |
| 5 NUL embarqué | `{"k":"a"}` — **tronqué en silence**, valeur **et** clé | valeur entière, NUL echappe (`\u0000`) |

⚠️ **Le n° 3 est un delta de STRUCTURE, pas seulement d'octets** : une clé absente devient présente.
C'est le seul des cinq qu'un oracle **sémantique** pourrait voir — et aucun golden ne le voit,
parce qu'aucun golden ne contient d'UTF-8 invalide.

⭐ **Les NOMBRES ne mordent pas sur ce périmètre, et c'est mesuré** : `JsonApi.cpp` compte
**32 `json_string()` et ZÉRO `json_real()`/`json_integer()`**. L'`int` et le `double` de
`buildJsonState()` passent par `Utils::to_string()` et sortent en **chaîne**. La famille « nombres »
d'E4.1m est **sans objet ici**, et c'est un résultat négatif qu'il faut écrire : le prochain
sous-ticket qui touchera `eventlog` la retrouvera, lui.

### ⭐⭐ Le sixième delta que ce ticket devait ÉVITER DE CRÉER — `Json jret;` vaut `null`, pas `{}`

`nlohmann::json` construit par défaut est `value_t::null`. Trois conteneurs de cette chaîne
répondaient `{}` en jansson. Écrire `Json jret;` au lieu de `Json::object()` les fait répondre
`null` :

- **ça compile sans le moindre avertissement** ;
- **aucun golden ne le voit** : ils comparent des documents **parsés**, et `null` comme `{}` sont
  des documents valides ;
- **aucun test de la suite ne le voyait** avant ce ticket.

⇒ **Même famille que le `!Json` d'`E4.1o` et que le `dump()` nu de KNX : la forme qui compile.**
Trois cas `...AnswersAnEmptyObject` (HTTP : `get_state`, `get_states`, `query`) ferment le trou et
sont des **invariants** — ils doivent rester verts des deux côtés de toute bascule future.
⚠️ **À reprendre dans chaque sous-ticket restant** : `Params::toNJson()` est `Json(std::map)` et
rend bien un **objet** même vide (mesuré), mais **tout conteneur construit à la main est exposé**.

### ⭐ RemoteUI : **trois des cinq deltas n'atteignent PAS l'appareil physique** — mesuré, pas raisonné

Le wire RemoteUI est le seul de l'épique qui parle à un **appareil matériel** dont le client vit
dans un dépôt voisin et **n'est pas mis à jour avec le serveur**. Mesure du pont **tel qu'il
était** (`json_dumps(JSON_COMPACT)`, puis `Json::parse`, puis `dump(ensure_ascii=true, replace)`) :

| delta | atteint l'écran ? | pourquoi |
|---|---|---|
| ordre des clés | **NON** | l'aller-retour triait déjà — **et** `RemoteUI::referenced_ios` est un `std::set`, donc l'`iolist` était déjà alphabétique. Deux raisons indépendantes. |
| casse hexa | **NON** | le `dump()` final était déjà celui de nlohmann |
| DEL `0x7F` | **NON** | idem — nlohmann échappe tout point de code >= `0x7F` sous `ensure_ascii` |
| UTF-8 invalide | **OUI** | la paire était droppée **avant** le pont, par `json_string()` |
| NUL embarqué | **OUI** | tronqué **avant** le pont, par `.c_str()` |

⇒ **Deux deltas, tous deux sur charge empoisonnée.** Épinglés par **deux cas NÉGATIFS**
(`RemoteUiStateBridgeTest.InitialStatesAreSortedAsciiAndLowercaseHex` et
`.InitialStatesAlreadyEscapeDel`) et non par une phrase : sur ce wire, un résultat négatif vaut une
release de firmware. `RELEASE_NOTES.md` reçoit un encadré propre aux écrans déportés.

### ⭐ Il y a DEUX gardes de vie sur le chemin RemoteUI, et ce n'est pas celle qu'on croit qui agit

`E4.1n.md` attribuait la protection au `weak_ptr` de `RemoteUIWebSocketHandler` (`:237`). **Mesuré,
c'est la seconde ceinture, pas la première.** `RemoteUIWebSocketHandler` **EST-UN**
`JsonApiHandlerWS` **EST-UN** `JsonApi` : détruire le handler détruit `JsonApi::apiAlive`
(`JsonApi.h:256`), et `JsonApi.cpp:491` teste **ce** jeton avant chaque étape de la chaîne audio et
avant `finishOne()` — **la lambda de résultat n'est même pas entrée**. Le `handlerAlive` du handler
reste **porteur** pour le `Timer::singleShot` post-auth (`:93`), qui lui n'est **pas** dans
`JsonApi`.

⛔ **Ne « nettoie » ni l'une ni l'autre en supprimant les `json_decref`** : les `decref`
disparaissent parce que la propriété passe par valeur ; les gardes protègent `this` et les
pointeurs bruts de handler que la lambda capture, **pas le document**. Couvert par
`RemoteUiStateBridgeTest.InitialStatesSurviveTheHandlerDyingMidFlight`.

### ⛔ Une acceptation de fiche IMPOSSIBLE, corrigée plutôt que contournée

`E4.1n.md` §Acceptation 2 exigeait `core/JsonApiAudioState_test` *« vert sans modification
d'assertion »*. **Impossible** : trois de ses assertions lisaient le compteur de références de
l'objet jansson, et **il n'y a plus de compteur** quand le résultat est une valeur. Une acceptation
ne peut pas demander la survie d'une assertion sur un champ d'un type qu'elle supprime.

Remplacées par ce qu'elles épinglaient **réellement** — document complet, sous-arbres imbriqués, clé
absente et **non `null`** — **plus** le typage chaîne de chaque valeur (`"42"`, `"12.5"`), qui
n'était couvert nulle part et qu'un port aurait pu casser sans qu'un golden bronche. Les deux cas
qui portaient le défaut T2.15 (`ClientGoneBeforeAnswerIsIgnored`,
`MixedCompletionAndDeletionAnswersOnce`) sont **inchangés sur le fond** et n'ont **jamais** regardé
un compteur de références.

⚠️ **Leçon pour les fiches restantes** : une acceptation formulée sur la **forme** d'un test
(« sans modifier d'assertion ») devient fausse dès que le ticket change un **type**. Formulée sur
l'**intention** (« le cas T2.15 reste couvert »), elle résiste.

### ⭐ `get_states` et `query` n'étaient couvrables par AUCUNE maison de configuration — 13e « fixture pauvre »

Mesuré en écrivant le filet : `IOBase::get_all_values_bool/double/string()` et
`IOBase::query_param()` ont **exactement UNE surcharge chacun dans tout l'arbre**
(`IOAVReceiver`, `Audio/AVReceiver.cpp:307` et `Audio/AVReceiver.h:160`). **Tout autre IO rend une
map VIDE.**

⇒ Sur n'importe quelle maison de test constructible, `get_states` et `query` répondent `{}` — et une
fixture qui ne voit que `{}` **ne distingue ni un ordre, ni un encodage, ni une perte de paire**.
Les cas auraient été verts des deux côtés de la bascule. Le remède est une classe de test
(`ProbeIO`) qui surcharge les deux méthodes. ⚠️ **À relire par tout ticket qui prétendra couvrir
`get_states` ou `query`.**

### ⚠️ La valeur empoisonnée doit être posée SANS passer par `set_value()`

`Internal::set_value(string)` appelle `Save()`, qui écrit dans le cache d'état de `Config` — **à
l'échelle du processus, et jamais purgé** (cf. l'avertissement en tête de `CalaosCoreFixture.h`).
Une valeur contenant `0xFF 0x80` ou un octet nul écrite par ce chemin **fuit dans le cas suivant du
même binaire**, et la fuite est invisible tant que l'ordre d'exécution ne change pas. `ProbeIO`
expose donc `setRawString()`, qui écrit le membre protégé directement. ⚠️ **Le contrôle qui attrape
une régression ici est `--gtest_shuffle`, jamais l'ordre par défaut.**

### ⚠️ Une couture de test assumée : `private` vers `protected` dans `RemoteUIWebSocketHandler.h`

`sendInitialIOStates()` — la fonction pour laquelle ce ticket existe — était **impilotable** :
`authenticated_remote_ui` n'est renseigné que par `authenticateConnection()`, qui exige une vraie
poignée de main HMAC. Un bloc de trois membres passe de `private` à `protected`. **Zéro
comportement**, **aucun appelant hors de la classe ne lit ces membres**, et c'est exactement le
motif déjà employé par `WsTestSession::Handler` pour `JsonApiHandlerWS`.

⚠️ Consigné parce que c'est une modification de `src/` faite **pour un test**, et qu'elle a donc été
livrée dans un commit **séparé** de la caractérisation (qui, lui, est à zéro ligne de `src/`,
vérifié par `git diff-tree` **sur le commit**).

### ⭐ Le protocole en trois commits, et pourquoi la caractérisation seule ne suffisait pas

1. `18160275` — **caractérisation, zéro `src/`** (vérifié sur le commit) : 19 cas, verts sur l'arbre
   jansson.
2. `22c6f9b2` — **couture + 5 cas RemoteUI** : ils ne pouvaient pas tenir dans (1) sans la couture.
   Verts **avant** la migration, ce qui est ce qui les rend probants.
3. `b1081598` — **la migration**, qui a dû **réécrire 11 des 24 cas**.

⭐ **C'est ce chiffre qui prouve que le chemin est EXERCÉ**, et pas le `make check` vert : *un cas
qu'il a fallu éditer est un cas qui a tourné.* Zéro golden bougé (arbre `d4ebc61f`, 145 fichiers) —
et **un golden inchangé ne prouve rien**, il est aveugle par construction à ce que ce ticket
modifie.

### Contre-mutations — six tours, ensembles rouges deux à deux distincts au grain du CAS

`cmp` d'application `rc=1` à chaque tour ; `rm -f` du binaire **et** des objets (serveur **et**
test) avant chaque build ; ligne `CXXLD` exigée et obtenue à chaque tour (`tests/Makefile.am` exclut
délibérément les objets serveur des `_DEPENDENCIES`, donc **rien ne se relie tout seul**).

| # | Échange | Rouges |
|---|---|---|
| M1 | `buildJsonState` : accesseurs `TBOOL` <-> `TSTRING` | 10 |
| M2 | émetteur HTTP : `ensure_ascii` `true` -> `false` | 7 |
| M3 | émetteur WS : `error_handler_t::replace` -> `ignore` | 2 |
| M4 | sites d'appel HTTP : `buildJsonStates` <-> `buildQuery` | 6 |
| M5 | RemoteUI : `remote_ui_io_states` -> `remote_ui_config` | 1 |
| Témoin | extraction d'une variable locale, sémantique nulle | **0** |

⭐ **M2 et M3 séparent les DEUX invariants d'émission sur les DEUX transports** : `ensure_ascii`
rougit 7 cas HTTP et **aucun** cas WS ; `replace` rougit 2 cas WS (dont **un** cas RemoteUI) et
**aucun** cas HTTP. C'est la disjonction qu'E4.1b avait construite, et elle tient sur cette chaîne
aussi. ⚠️ **`GetStateHttpKeepsTheWholeValueAcrossAnEmbeddedNul` reste VERT sous M2**, et c'est
correct : l'octet nul est un caractère de contrôle inférieur à `0x20`, échappé **indépendamment**
d'`ensure_ascii`. Un cas qui aurait rougi là aurait signalé un oracle trop large.

### Décomptes

- `make check` en **distclean complet** : `rc=0`, `# TOTAL: 102 / PASS: 101 / SKIP: 1 / FAIL: 0`.
  `+1` sur master (`core/JsonApiStateWireBytes_test`). `SKIP` = `run-python-tests.sh`, normal.
- Cas et durées : `JsonApiStateWireBytes` **24/24**, 38 à 43 ms par cas, 748 ms au total ;
  `JsonApiAudioState` **5/5**, 14 à 15 ms. **Uniformes** — aucun plafond de sûreté.
- Jetons jansson dans `src/` (`json_*(`, `jansson_*(`, `json_t`, `JSON_*`, **hors commentaires**) :
  **734 -> 677**. `RemoteUI/RemoteUIWebSocketHandler.cpp` : **5 -> 0**.
- Avertissements de compilation **inchangés** : `JsonApi.cpp` 17, `JsonApiHandlerWS.cpp` 25,
  `JsonApiHandlerHttp.cpp` 24, `RemoteUIWebSocketHandler.cpp` 0 — identiques à l'arbre jansson,
  zéro erreur.
- **`F-FLAKY-1`** : `tests/core/ShutterImpulse_test.cpp` présent, **6 sites** (297, 343, 384, 450,
  498, 664), tous à la forme déclarée `EXPECT_FALSE(sh.isStopped())`. **Non vu rouge** sur les
  quatre `make check` complets de ce ticket ; **non relancé** pour le provoquer.

### Le découpage `n` vers `s` tient — et un point à léguer à `E4.1s`

Rien à re-planifier. Le seam (`sendJson` en surcharge nlohmann des deux côtés) a suffi, **aucun
adaptateur transitoire n'a été écrit**, et `decodeSetState` s'est révélé n'avoir **aucun** appel
jansson dans son corps : son périmètre réel était ses **deux constructeurs de réponse**, chez les
appelants (`JsonApiHandlerHttp.cpp:394`, `JsonApiHandlerWS.cpp:373`).

⚠️ **Le point légué, mesuré et laissé volontairement** : `JsonApiHandlerWS::processGetState()`
conserve `sendJson("get_state", nullptr, client_id)` sur la **surcharge jansson** quand la requête
n'a pas de `data`. Ce n'est pas un oubli : cette surcharge **omet** la clé `"data"` quand le
pointeur est nul, là où la surcharge nlohmann écrirait `"data":null`. Le golden
`e40e_ws_get_state_without_data.json` épingle l'omission. ⇒ **`get_state` a donc DEUX formes
d'enveloppe** sur le websocket depuis ce ticket (chemin normal trié, chemin sans `data` à l'ancien
ordre). C'est cohérent avec « zéro golden bougé », et c'est `E4.1s` qui devra trancher si l'on
unifie — **auquel cas ce golden bouge, et il faudra le déclarer.**

## T3.56 — l'horloge gelée au démarrage : le mécanisme tient, **les deux victimes désignées ne tiennent pas** (2026-08-26)

- ⛔⭐⭐ **[Faux vert, VARIANTE NOUVELLE] L'oracle dont la FIXTURE S'ÉVAPORE : `uvw::Loop::getDefault()`
  RECRÉE la boucle par défaut, et rend donc une horloge FRAÎCHE.** La 1ʳᵉ rédaction des cas de
  `T3.56` était **verte sur l'arbre non corrigé** — 819 ms par cas, le timer attendant bien ses
  200 ms après 600 ms d'oisiveté — alors qu'une sonde `libuv` autonome sur **la même image**
  reproduisait le défaut en trois lignes (`fired after 0 ms`).
  ⭐ **Le mécanisme, mesuré** : `uvw::Loop::getDefault()` (`uvw/src/uvw/loop.hpp`) met le wrapper en
  cache dans un **`std::weak_ptr`**. Sans référence forte — et il n'y en a **aucune** tant qu'aucun
  handle n'est vivant — le `shared_ptr` rendu meurt **en fin d'expression**, `~Loop()` appelle
  `uv_loop_close()`, et `uv_loop_close()` se termine par
  `if (loop == default_loop_ptr) default_loop_ptr = NULL;`. Le `uv_default_loop()` suivant refait
  donc **`uv_loop_init()`** sur le même `uv_loop_t` **statique** — et `uv_loop_init()` appelle
  `uv__update_time()`. **Sonde sur l'image** : deux temporaires de part et d'autre d'un `sleep`
  de 600 ms, **même pointeur de boucle**, `uv_now()` avance de **600**.
  ⇒ ⭐ **Dans un binaire sans handle vivant, tout `uvw::Loop::getDefault()->x()` RÉINITIALISE
  silencieusement la boucle par défaut.** Un test qui écrit `uvw::Loop::getDefault()->run<…>()`
  au lieu de garder la boucle dans une variable ne mesure pas ce qu'il croit — et **`calaos_server`
  n'est PAS dans cette forme** (le `Timer` du cache d'état, `CalaosConfig.cpp:206`, tient un handle
  donc une référence forte pendant tout le chargement), ce qui est **exactement** ce qui rend le
  défaut réel côté produit et invisible côté test.
  ⭐ **Le remède de méthode, générique** : *épingler* la ressource partagée pour toute la durée du
  cas **et vérifier que la fixture a bien produit l'état qu'elle prétend produire*. Les quatre cas
  de `core/Timer_test` mesurent désormais la péremption de l'horloge (`loopClockStalenessMs()`) et
  **échouent en le disant** si elle est absente. Sans cette vérification, personne ne l'aurait vu.

- ⭐ **[F-FLAKY-1 → produit] La durée du chargement de configuration est MESURÉE — c'était le
  livrable n° 1 de la fiche, et le chiffre INFIRME l'hypothèse haute qu'elle posait.**
  Instrumentation temporaire de `main.cpp` (`uv_hrtime() − uv_now(loop)` en quatre points),
  `calaos_server` réel, **deux configurations réelles** (474 IO / 284 IO, deux `WagoMap` construits,
  vérifié dans la trace) :

  | régime | `before_loadio` | `after_loadio` | `after_loadrule` | ⭐ `before_run` |
  |---|---:|---:|---:|---:|
  | hôte x86 64 cœurs, 474 IO, 3 tirs | 1 | 22–28 | 25–30 | **50–64 ms** |
  | hôte x86 64 cœurs, 284 IO, 3 tirs | 1 | 11 | 12 | **39–50 ms** |
  | **un seul cœur, 4 brûleurs dessus** (≈ 5× plus lent), 474 IO, 3 tirs | — | — | — | ⛔ **134–142 ms** |

  ⇒ ⭐ **Le chargement lui-même ne coûte que 10–30 ms ; le gros du décalage est le montage des
  services APRÈS le chargement** (`main.cpp:152-191`) — la fiche `T3.49` n'attribuait le décalage
  qu'à `LoadConfigIO`/`LoadConfigRule`, c'est **moins de la moitié**.
  ⇒ ⛔ **« un chargement de plus de 100 ms, c'est-à-dire toujours » est FAUX sur cette classe de
  matériel** : le délai le plus court de l'arbre (`WagoMap.cpp:46`, 0,1 s) garde **~40 ms de ses
  100**. Il n'en garde **plus rien** dès que la machine est cinq fois plus lente — ce qui est le
  régime plausible d'une carte embarquée, **non mesuré ici**.

- ⭐ **[F-FLAKY-1 → produit] `IO/Wago/WagoMap.cpp:46` est INOFFENSIF même en tirant immédiatement —
  lu au source, pas supposé.** Trois maillons : (1) `createUdpSocket()` est appelé **avant** la
  création du timer et `bind()` est **synchrone**, donc la socket est prête ; (2) le rappel
  n'envoie rien lui-même — il empile dans `udp_commands` et arme un timer de 50 ms **depuis
  l'intérieur de la boucle**, donc sur une horloge fraîche ; (3) le rappel commence par
  `if (heartbeat_timer->getTime() < 10.0) Reset(10.0)` et le timer **répète** : rien n'est perdu,
  rien ne s'emballe, l'occasion revient toutes les 10 s. ⇒ **le site n° 1 de la fiche est infirmé.**

- ⭐ **[F-FLAKY-1 → produit] `Audio/RoonPlayer.cpp:243` n'est PAS menacé par l'horloge gelée non
  plus** : ses **10 s** sont deux ordres de grandeur au-dessus du décalage mesuré (39–64 ms, 142 ms
  au pire régime testé). ⇒ **le site n° 2 de la fiche est infirmé aussi.** ⚠️ Ce qui reste vrai est
  la remarque de forme : ces 10 s sont comptées **depuis le constructeur**, donc depuis avant la
  boucle — rafraîchir l'horloge rend le délai honnête sur sa **longueur**, jamais sur ce qu'il
  attend.

- ⛔⭐ **[NOUVEAU, à instruire — demande de numéro] `RoonCtrl` ne se réabonne JAMAIS, et l'abonnement
  peut être perdu SILENCIEUSEMENT.** `RoonPlayer.cpp:245` appelle `subscribeZone()`, qui mémorise le
  rappel dans `subscribeCb` **puis envoie un message** via `ExternProcServer::sendMessage()` —
  lequel est `if (client) { … }` (`IO/ExternProc.cpp:135`) : **si le sidecar n'est pas encore
  connecté, le message est jeté sans un mot**. `RoonCtrl` n'écoute **pas** `processConnected`
  (vérifié : le constructeur ne connecte que `messageReceived` et `processExited`), donc rien ne
  rejoue l'abonnement. ⇒ **la zone ne remonte plus jamais son état**, et **la même perte se produit
  après CHAQUE respawn de `calaos_roon`** — `processExited` relance le processus mais ne réémet
  aucun abonnement. Indépendant de l'horloge gelée ; trouvé en instruisant `T3.56`.

- ⭐ **[F-FLAKY-1 → produit] Quatre sites d'armement pré-boucle qu'AUCUN recensement n'avait
  listés.** Le classement de `T3.49` portait sur 11 sites « en constructeur » ; il en manquait
  quatre qui ne sont pas des constructeurs mais tournent **avant** `main.cpp:223` :
  `main.cpp:194` (la boucle d'événements des règles, **0,1 s**), `main.cpp:195` (le chien de garde,
  5 s), `main.cpp:198` (`Timer::singleShot(0.1, checkAutoScenario)`, dont le commentaire dit
  *« once the main loop is started »*), et **`IO/Web/WebCtrl.cpp:115`** — un `Timer::Reset()`,
  chemin d'armement que le motif de balayage de `T3.49` (`singleShot|singleIdler|new Timer|
  make_shared<Timer>`) **ne pouvait pas voir**, atteint depuis `webRegisterPolling()` appelé à la
  fin du constructeur de chaque IO web (`IO/Web/WebDocBase.h:99`).
  ⚠️ **`uv_timer_again()` lit la même horloge en cache que `uv_timer_start()`** : un recensement
  des armements qui ne compte pas les **ré**-armements est incomplet par construction.

- ⭐ **[F-FLAKY-1 → produit] Le remède produit ne peut PAS être celui des tests, et la différence
  est structurelle.** `freshenLoopClock()` de `T3.49` fait tourner **une itération NOWAIT** : elle
  **distribue des rappels**. Appelée depuis un constructeur d'IO au milieu du chargement de
  configuration, elle ferait tourner la boucle sur un arbre à moitié construit. `uv_update_time()`
  **lit l'horloge et écrit `loop->time`, sans rien distribuer** — c'est ce que `libuv` documente
  pour ce cas exact (*« can be called manually if you have callbacks that block the event loop for
  longer periods of time »*), et c'est ce qui permet de mettre le remède **dans `Timer`**
  (`create()`, `singleShot()`, `Reset()`) plutôt qu'aux deux sites de la fiche : les douze sites
  pré-boucle se referment d'un coup, **y compris les quatre que personne n'avait listés**, et le
  prochain site écrit naît correct.

- ⭐ **[T3.56] `RELEASE_NOTES` : rien de dû, et c'est une vérification, pas un oubli.** Le décalage
  supprimé vaut **39–64 ms** sur le matériel mesuré. Aucune attente ne passe de *« tire
  immédiatement »* à *« attend »* dans ce régime — le plus court délai pré-boucle (0,1 s) gardait
  déjà 40 ms. Les effets sont : première trame `WAGO_HEARTBEAT` à 100 ms au lieu de ~40, première
  évaluation des règles à 100 ms au lieu de ~40, alertes de configuration à 30 s au lieu de 29,94.
  **Rien qu'un utilisateur puisse observer**, et **aucun rapport d'utilisateur** n'est rattaché au
  défaut. Sur une carte cinq fois plus lente le heartbeat Wago passerait bien d'« immédiat » à
  « 100 ms » — mais il est **inoffensif dans les deux cas** (bullet ci-dessus), donc toujours rien
  à annoncer.

## T3.51 — le cache de compilation : ce qui NE ment pas, et les CINQ réglages qui le feraient mentir

*(Retour de revue de la branche `tooling/ccache`, 2026-08-26. ⭐ **Les deux côtés sont gardés** : ce
qui est établi sain, et ce qui ne l'est pas.)*

### ✅ Le côté SAIN — établi, à ne pas remesurer

Le cache **tel que livré** ne ment pas. **12 scénarios d'attaque tous sains** : chemins identiques ·
macro passée en ligne de commande · `config.h` généré · collision (taille, mtime) sur **en-tête**
*et* sur **source** · `__DATE__`/`__TIME__` · mtime décalée d'un an · **trois compilations dans la
même seconde** · `cp -p` · **cache saturé, forcé à évincer**. Plus : **370 objets sur 370 identiques**
octet pour octet entre un arbre bâti depuis le cache et un arbre compilé de zéro, **aller-retour
rouge** (mutation → restauration → re-mutation ⇒ le test redevient rouge), **90/90 en concurrence**
sous charge 127, et **11 empreintes d'objets identiques** entre le mode avec cache et le mode sans.

⚠️ La restauration `cp -p` **n'est ni aggravée ni masquée** par le cache : `make` n'appelle pas le
compilateur, le cache n'est pas consulté (`CXXLD` = 0). **La discipline `rm -f` reste entière.**

### ⛔ Quatorzième variante de faux vert (**n° 14 de la liste canonique**) — **le cache qui rend un objet périmé**

⚠️ **Elle est CONDITIONNELLE** — c'est ce qui la distingue des treize précédentes : elle n'existe que
si le cache est **mal configuré**. ⛔ **Mais l'un des réglages fautifs était LE DÉFAUT QUE LA BRANCHE
LIVRAIT**, ce qui la rendait active, pas hypothétique.

| # | réglage | mécanisme | l'ancienne sonde |
|---|---|---|---|
| ① | `hash_dir = false` | l'objet compilé au chemin A est servi au chemin B ; il **diffère** de ce qu'une compilation propre produit (`DW_AT_comp_dir`) | `rc=0` |
| ② | ⭐ `compiler_check = mtime` — **LE DÉFAUT LIVRÉ** | un compilateur remplacé à taille et date égales n'invalide rien ⇒ **objet périmé servi** | **`rc=0` par construction** |
| ③ | `compiler_check = string:CONST` | ≡ `none`, que la sonde refusait **nommément** — mais celui-ci n'était **pas listé** | `rc=0` |
| ④ | `sloppiness = file_stat_matches[,_ctime]` | un **en-tête** modifié à taille **et** dates égales fait servir l'objet de la version précédente ; **reproduit 4/4** | **`rc=0`** — voir ci-dessous |
| ⑤ | ⭐ **`ignore_options = -D*`** (trouvé à la 3ᵉ passe) | une **option de compilation** sort de la clef : `-DVAL=2` reçoit l'objet de `-DVAL=1`. **Objets identiques à l'octet, `direct_cache_hit` au journal, `mov $0x1` au désassemblage.** ⚠️ **Aucun aller-retour sur les FICHIERS ne peut le voir** | **`rc=0`**, y compris avec la « liste blanche » de 4 clefs |

⭐ **Ce que cette variante a de propre à elle** : les treize précédentes se voient dans un journal
(un test qui ne tourne pas, un lien qui ne se refait pas, une restauration qui ne restaure rien).
⛔ **Celle-ci ne laisse AUCUNE trace** : la compilation réussit, l'objet est bien daté, le test est
vert, et le `.o` ne correspond simplement pas à la source. **Elle ne se voit qu'en comparant l'objet
à celui d'une compilation propre** — c'est-à-dire en le cherchant.

### ⛔ Et la garde elle-même était *fail-open* — HUIT fois (cinq, puis trois de plus en revue)

⭐ **La cause de ④ est mesurée, et elle est instructive : l'unité de traduction de la sonde n'avait
aucun `#include`.** `file_stat_matches` porte sur les **fichiers inclus** ; sans inclusion, le
mensonge **ne peut pas se manifester**. La sonde imprimait donc « aller-retour honnête », `rc=0`, sur
un cache démontré menteur — **seule une liste noire de chaînes protégeait**.

Les autres modes : un build câblé par `CXX="ccache g++"` (cache **actif**) rendait `SKIP rc=77` ; un
`CXX` exporté **avec un argument** faisait **planter** la sonde (`ccache: invalid option -- 'g'`) et
elle **convertissait sa propre panne en `77`** ; et ⭐ **`obj(A) == obj(B)` — c'est-à-dire exactement
le mensonge — était classé « non concluant », `77`**. ⚠️ **`SKIP` et `PASS` étaient indiscernables
dans le journal.**

**Quatre règles générales en sortent**, écrites dans `DECISIONS.md` (2026-08-26) :
1. *Une garde qui ne sait pas conclure doit échouer, pas se taire — et son silence doit être
   impossible à confondre avec un succès.*
2. *Interdire ce qu'on connaît ne protège que de ce qu'on connaît ; exiger ce qu'on a audité protège
   du reste.* ⚠️ **Première tentative INSUFFISANTE, et c'est le plus instructif du lot** : la liste
   noire avait été remplacée par une **liste blanche de quatre réglages** — ⛔ mais `ccache -p` en
   publie **44**, et `ignore_options=-D*`, non couvert, fait servir l'objet de `-DVAL=1` pour une
   compilation `-DVAL=2` (**objets identiques à l'octet, `direct_cache_hit` au journal, `mov $0x1`
   au désassemblage**), sonde à **`rc=0`**. ⇒ ⭐ *une liste de clefs à surveiller est toujours en
   retard d'une clef* : la sonde **demande à l'outil** sa configuration et exige que **chaque clef
   publiée** soit couverte — valeur exigée ou valeur explicitement libre —, **toute clef inconnue
   rendant `1` en la nommant**. (Même remède que « toujours en retard d'un suffixe » sur `T3.48`.)
3. ⭐ *Un détecteur qui ne peut pas mordre doit ÉCHOUER, pas réussir.* La sonde imprimait « le piège
   est armé et il mord » **sans jamais le vérifier** : avec `CCACHE_DISABLE=1`, `recache` ou
   `read_only`, le cache ne servait rien et elle rendait **`0` en 3/3**, garde de configuration
   désarmée. Elle exige désormais un **succès de cache CONSTATÉ** ⇒ **`1` en 3/3**.
4. ⭐ *Auditer un outil par un autre exemplaire du même nom, ce n'est pas l'auditer.* La sonde lisait
   `ccache -p` **trouvé dans le `PATH`** pendant que les compilations passaient par `CXX` : un
   enrobage nommé `ccache` qui exporte `CCACHE_IGNOREOPTIONS=-D*` la faisait rendre **`0`**. Elle
   interroge désormais **le binaire de la ligne `CXX`**.

⚠️ **La moitié empirique reste un détecteur par ÉCHANTILLON, et sa portée est plus étroite que ce
qui avait été écrit.** Avec le `#include` ajouté et la mtime figée dans le passé, elle voit
`sloppiness = file_stat_matches,file_stat_matches_ctime` **seule**, garde désarmée, en **4/4**
(`ccache` 4.12.3 **et** 4.7.5). ⛔ **Mais la phrase « elle voit `file_stat_matches` SEULE » est
FAUSSE** : le réglage **nu** rend `rc=0` en **4/4** des deux côtés — `ccache` compare encore le
`ctime`, qu'`os.utime` ne peut pas remettre en place. Elle ne voit **ni** les options ignorées,
**ni** `direct_mode = false`. ⚠️ **Et son piège ne mord que grâce à la mtime FIGÉE DANS LE PASSÉ** :
laissée à « maintenant », donc différente d'une écriture à l'autre, la sonde imprimait **PASS en
6/6 sur un cache démontré menteur**. ⇒ **la date figée n'est pas un détail, c'est TOUT le piège** —
et la sonde le **constate** désormais elle-même (taille *et* mtime égales, sinon `rc=1`).
⇒ ⭐ **L'AUDIT INTÉGRAL DE LA CONFIGURATION est la garde PRINCIPALE** ; elle prouve qu'un mensonge
s'est produit, jamais qu'aucun ne peut se produire. Les deux ne se remplacent pas, et elles ne pèsent
pas le même poids.

*(Note mesurée, et **tranchée** : **`base_dir` n'est pas malhonnête** — ⛔ **mais ce n'est pas une
option ouverte pour autant** : la sonde l'exige **vide** et rend `1` sinon. Le partage entre chemins
différents **n'est pas disponible** aujourd'hui ; il faudra d'abord **auditer** `base_dir`. ⛔ Ce qui
reste interdit sans condition, c'est le réflexe naturel `hash_dir=false` — le mensonge ①.)*

## E4.1o — le contrat d'aplatissement de `jansson_decode_object` en est à sa **cinquième copie manuelle**, et personne ne les tient ensemble (2026-09-01)

**Hors périmètre, non corrigé.** `jansson_decode_object()` (`src/lib/Jansson_Addition.h`) définit
quatre règles : chaîne telle quelle, booléen en **mot** `"true"`/`"false"`, nombre par
`Utils::to_string(double)`, **tout le reste** la chaîne vide **mais la clé ajoutée**. Chaque wire
qui migre doit le recopier **à la main**, parce que `Params::fromNJson()` lève `type_error.302` sur
tout ce qui n'est pas une chaîne JSON. À ce jour :

| Copie | Fichier |
|---|---|
| 1 | `LuaScript/ScriptWire.h` (`decodeObject`) |
| 2 | `IO/Reolink/ReolinkWire.h` |
| 3 | `IO/Wago/WagoWire.h` |
| 4 | `IO/OLA/OLAWire.h` + `IO/KNX/KNXExternProc_main.h` / `KNXCtrl.cpp` |
| 5 | **`JsonApi.cpp` (`decodeJsonObject`, E4.1o)** |

Chacune porte sa propre tripwire de test contre `fromNJson()`, ce qui est bien — mais **rien ne
vérifie que les cinq disent la même chose**. Une divergence sur une seule des quatre règles serait
silencieuse : chaque tripwire ne regarde que sa copie. ⇒ **À replier en UN seul helper au moment
où `Jansson_Addition.h` disparaît (E4.1x)**, avec un test unique qui compare les deux
implémentations sur le même jeu d'entrées. Pas avant : replier maintenant ferait toucher six
fichiers appartenant à six tickets différents.

## E4.1o — `set_param` et `del_param` répondent **les mêmes octets** : un oracle qui ne lit que la réponse ne distingue pas les deux actions (2026-09-01)

**Trouvé par contre-mutation, corrigé dans le fichier de ce ticket, mais la portée dépasse le
ticket.** Sur succès les deux répondent `{"success":"true"}` ; sur échec les deux répondent
`{"error":"wrong io/param"}`. **Échanger leurs deux sites d'appel dans `JsonApiHandlerHttp.cpp`
laissait la suite verte à un cas près, et par accident** (celui qui, par la forme de sa fixture,
tombait sur la branche « `value` manquant »).

⚠️ **Ce n'est pas propre à ce ticket** : la même figure existe partout où deux actions voisines
partagent un accusé de réception générique — `set_state`, `register_push`, les sept
`autoscenario`. Un ticket qui échange deux de ces sites d'appel ne sera pas attrapé par un oracle
de réponse. **Règle à répercuter dans les fiches restantes de la série** : pour toute action à
**effet de bord**, la contre-mutation par échange doit être ancrée sur un cas qui vérifie
l'**effet**, pas seulement l'accusé de réception. C'est la **13ᵉ** récidive recensée du piège
« fixture pauvre », et la **première trouvée par l'implémenteur**.

## E4.1o — la moitié jansson de `JsonApiEmissionBytes_test` a dû être retargetée une **deuxième** fois, et il y en aura d'autres (2026-09-01)

**Attendu, prévu par écrit par E4.1m, consigné pour que le suivant ne le prenne pas pour une
régression.** Le cas
`TheNlohmannHttpWireIsAsciiOnlyAndEscapesWithLowercaseHex` compare **deux émetteurs HTTP vivants**
sur la même maison. Sa moitié jansson doit donc viser une action qui atteint **encore** la
surcharge `sendJson(json_t *)`. Elle visait `get_home` (jusqu'à E4.1m), puis `get_param` (jusqu'à
E4.1o), et vise maintenant **`config type=get`** — la seule réponse HTTP synchrone restante qui
soit construite à la main en `json_t*` **et** porte un texte accentué (l'`io.xml` de la maison).

⚠️ **Après `config`, il ne reste presque rien** : `get_mcp_info` (ASCII pur), les sept
`autoscenario` et la famille `audio`/`audio_db` (asynchrones, et périmètres d'E4.1p/E4.1q).
⇒ **E4.1r/E4.1s doivent s'attendre à ce que ce cas ne soit plus retargetable du tout**, et c'est
normal : quand la surcharge jansson disparaît, cette moitié du cas disparaît avec elle. **Ne pas la
« réparer » en la faisant viser un émetteur nlohmann** — elle mesurerait alors la même chose deux
fois et le cas deviendrait vide.

## E4.1o — `T3.21` toujours ouvert : `del_param` court-circuite `IOBase::del_param()` (2026-09-01)

**Non corrigé, délibérément.** `JsonApi::buildJsonDelParam()` appelle
`o->get_params().Delete(...)` au lieu de `IOBase::del_param()`. La fiche E4.1o l'interdit
explicitement de toucher (durcissement d'une ligne, ticket **T3.21**, séparé). La migration en
`Json` **n'a rien changé** à ce chemin : la ligne est identique, seul le type de la réponse a
bougé. **T3.21 reste entièrement à faire et n'est pas plus difficile qu'avant.**

## E4.1p — `get_stats` n'est **pas** une action `audio` : la fiche l'avait mal attribuée (2026-09-01)

**Correction de fiche, mesurée, non corrigée dans le code.** `E4.1p.md` range `audioGetDbStats`
parmi les cinq `audioGet*` du périmètre. Elle n'est pas dispatchée par `processAudio()` mais par
**`processAudioDb()`** (`JsonApiHandlerHttp.cpp:829`, `JsonApiHandlerWS.cpp:455`), au milieu des
quatorze `audioDbGet*`. Une requête `action=audio` + `audio_action=get_stats` répond
`{"error":"unkown audio_action"}` — épinglé par
`JsonApiAudioWireBytesTest.AudioDbGetStatsAnswersTheDatabaseParamsPlusItsAction`, qui doit passer
par `action=audio_db`.

⇒ **`audioGetDbStats` et `audioDbUnavailable` restent en jansson en E4.1p et partent avec E4.1q**,
avec leur dispatcheur. Les migrer isolément obligeait à migrer `processAudioDb()` et ses quinze
branches, c'est-à-dire E4.1q en entier — l'inverse exact de la raison pour laquelle les deux
tickets ont été séparés.

## E4.1p — `processDbResult` **ne peut pas** basculer sans ses quatorze appelants (2026-09-01)

**Contradiction interne à `E4.1p.md`, tranchée en faveur de l'acceptation vérifiable, non corrigée
dans le code.** La fiche dit à la fois « `processDbResult` bascule **ici** » et « les 14
`audioDbGet*` sont **inchangées** (diff vide sur leurs lignes) ». Les deux sont incompatibles : les
quatorze font `result_lambda(processDbResult(data))` avec un `result_lambda` de type
`std::function<void(json_t *)>`, donc un retour `Json` ne compile plus chez elles.

**Trois ponts examinés, les trois rejetés :**

1. **Conversion implicite `Json` → `json_t*`** : n'existe pas. Le constructeur gabarit de
   `basic_json` est éliminé par SFINAE faute de `from_json` pour un pointeur. ⭐ **Bonne
   nouvelle, contrairement au `!Json` d'E4.1m/E4.1o** : l'échec est **de compilation**, bruyant,
   pas un `type_error.302` à l'exécution. Ne pas « réparer » ça en écrivant un `from_json` pour
   `json_t*` : ça rendrait la conversion **implicite partout dans l'arbre**.
2. **Pont `dump()` + `json_loads()`** : **change le comportement observable des quatorze fonctions
   hors périmètre**. `error_handler_t::replace` remplacerait l'UTF-8 invalide par U+FFFD **avant**
   que jansson ne le voie, donc la suppression silencieuse de la paire disparaîtrait chez E4.1q
   sans qu'E4.1q ait écrit une ligne.
3. **Parcours d'arbre écrit à la main** : c'est l'**adaptateur transitoire** que toutes les fiches
   de la série interdisent, avec sa propre sémantique à prouver.

⇒ **La ligne de dépendance d'`E4.1q.md` (« dur — `processDbResult` y bascule ») est à corriger.**
La dépendance E4.1q → E4.1p **reste réelle** (sérialisation stricte sur les quatre mêmes fichiers,
et la surcharge `getAudioPlayer(const Json &)` posée par E4.1p), mais elle ne porte plus sur
`processDbResult`, qui bascule **en E4.1q, atomiquement avec ses quatorze appelants**.

## E4.1p — la branche `get_cover` de `processAudio` n'est couverte par **aucun** cas de l'arbre (2026-09-01)

**Mesuré, non corrigé, hors périmètre.** `JsonApiHandlerHttp::processAudio()` a une branche
`audio_action=get_cover` qui rend une **image JPEG** et non du JSON, et qui bâtit à la main deux
charges d'erreur — `{"success":"false","error_str":"unable to get url"}` et sa jumelle
`unable to load data from url`. **Zéro occurrence de `unable to get url` dans `tests/`** (mesuré
sur les sources, pas sur les binaires) : ni golden, ni cas, ni sonde.

C'est pour cette raison que E4.1p ne l'a pas migrée : la faire passer en nlohmann trierait ses deux
clés (`error_str` avant `success`) **sans le moindre filet**. Elle part avec le dispatch, en
E4.1s — qui devra **écrire le cas d'abord**. À ne pas confondre avec `processGetCover()`
(`action=get_cover` au premier niveau), qui a son golden `e40e_http_get_cover_unknown_id.json`.

## E4.1p — deux vocabulaires de refus pour la même condition dans la famille audio (2026-09-01)

**Observation, non corrigée, aucune fiche ne la couvre.** Un identifiant qui n'est pas un lecteur
audio est refusé de **deux façons différentes** selon l'action :

- `get_playlist` répond `{"success":"false"}` (chemin `playlistNoPlayerAnswer()`, T3.17a) ;
- toute action `audio` / `audio_db` répond `{"error":"unkown player_id"}`, ou
  `{"error":"empty player id"}` si le champ est absent, non-chaîne ou vide.

Les deux sont épinglés par des goldens (`t317a_*_get_playlist_unknown_player`,
`t317b_*_audio_unknown_player`) et par les cas de `JsonApiAudioWireBytes_test` : **c'est du contrat
de wire, ce n'est pas à « harmoniser » dans un ticket de migration.** Consigné pour que la question
soit posée une fois, ailleurs, plutôt que redécouverte à chaque ticket de la chaîne.

⚠️ Même famille : la faute d'orthographe **`unkown`** (deux occurrences, `unkown player_id` et
`unkown audio_action`) est également du contrat de wire. Ne pas la corriger sans note de version.

## E4.1p — écriture morte survivante : `audio_action` ajouté à un `Params` qui n'est jamais lu (2026-09-01)

**Non corrigé, hors périmètre, sans effet observable.** `audioGetPlaylistSize()` et `audioGetTime()`
font `adata.params.Add("audio_action", "get_playlist_size" / "get_time")` puis construisent une
**autre** `Params` pour la réponse. L'ajout n'atteint donc jamais le client — c'est ce que les
goldens `t317b_*_audio_get_playlist_size` / `_get_time` enregistrent (aucun `audio_action` dedans),
et `JsonApiAudioWireBytes_test` le réasserte au niveau des octets.

La bascule E4.1p a **recopié ces deux lignes verbatim**, avec un commentaire : les retirer serait un
nettoyage noyé dans un diff de migration, et la règle de la série l'interdit. À faire par le ticket
qui reprendra cette famille, avec le cas qui va avec.

## ⚠️ Piège d'outillage — `git checkout -- src/` **ne restaure rien** dans un conteneur monté sur un worktree (E4.1p, 2026-09-01)

**Vécu, chiffres jetés et refaits.** Le driver de contre-mutations restaurait les sources entre deux
tours avec `git checkout -- src/bin/calaos_server/`. Dans le conteneur, le worktree pointe sur
`/home/raoul/repos/calaos/calaos_base/.git/worktrees/<nom>` — **un chemin qui n'est pas monté** —
donc git répond `fatal: not a git repository` **et le script continue**. Les six mutations se sont
**empilées** : M2 tournait sur M1, M5 sur M1+M2+M3+M4, et les ensembles rouges étaient énormes et
faux (`TOTAL_RED` 49 au lieu de 13).

**Le symptôme qui trahit** : des ensembles rouges qui ne font que **croître** d'une mutation à la
suivante, et un témoin qui rougit. **La parade** : copier les `.cpp` du périmètre dans un répertoire
pristine **hors de git** au début du script, restaurer par `cp`, et **faire échouer le script** sur
la première erreur de restauration. Même famille que le piège `_DEPENDENCIES` : l'outil ment en
silence et le résultat a l'air plausible.

## E4.1q — `processAudioDb()` a **seize** branches, pas quinze, et deux fiches disent quinze (2026-09-01)

**Écart documentaire, sans conséquence de code.** La fiche E4.1q et la ligne de `BOARD.md` parlent
du « dispatcheur `processAudioDb()` et ses **15 branches** ». Compté au source sur les deux
transports : **16** — les **quinze** `audioDbGet*` **plus** `get_stats`, qui n'est pas un
`audioDbGet*` mais est bien dispatché ici. Le périmètre livré est le bon (les seize ont basculé) ;
c'est le chiffre qui était faux. Consigné pour que le prochain recompte ne conclue pas à un oubli.

## E4.1q — la table de périmètre d'`E4.1s` réclame encore `processAudioDb` (2026-09-01)

`E4.1s.md` liste, pour `JsonApiHandlerWS.cpp`, « `processGetState`/`processGetIO`/
`processSetTimerange`/`processAudio`/**`processAudioDb`**/`processAutoscenario` (les signatures
`json_t *jdata`) ». **`processAudioDb` n'a plus de signature `json_t *` sur aucun des deux
transports** : E4.1q l'a migrée avec ses seize branches, comme sa propre fiche l'exige. La ligne
d'E4.1s est **périmée**, pas contradictoire — à retirer au moment d'ouvrir E4.1s, sans quoi son
implémenteur cherchera un `json_t *jdata` qui n'existe plus.

## E4.1q — `Utils::to_string(double)` **n'est pas sur le chemin de la base musicale** (2026-09-01)

La fiche prévient : « `Utils::to_string(double)` encore : les durées de pistes y passent. Ne le
corrige pas. » **Mesuré : elles n'y passent pas.** Sur ce périmètre, `processDbResult()` ne lit que
`data.vparams` et `audioDbGetTrackInfos()` que `data.params` — deux `Params`, donc deux
`map<string,string>` : la durée d'une piste arrive de la base **déjà en chaîne** et n'est jamais
reformatée. Le seul `double` de la famille audio est le temps de lecture d'`audioGetTime()`, qui
appartient à E4.1p. La consigne reste bonne (rien n'a été « corrigé ») ; c'est sa justification qui
ne s'applique pas ici.

## E4.1q — une prédiction de delta fausse, gardée avec sa mesure (2026-09-01)

`WsAnUnknownAudioDbActionIsEnveloped` avait été écrit comme cas `...Today`, dans l'idée que
l'enveloppe WS de la branche `else` du dispatcheur allait trier à la bascule. **Elle n'a pas
bougé** : cette branche appartient au transport, pas au dispatcheur, et son argument accoladé se
liait **déjà** à la surcharge nlohmann de `sendJson()` avant ce ticket. Le cas a été gardé, renommé
en invariant, avec la mesure et l'erreur écrites dans son commentaire plutôt que discrètement
effacées. **Une prédiction qu'un cas va rougir n'est pas une preuve ; seul le tour de mesure l'est**
— ce qui vaut aussi dans l'autre sens, pour un cas qu'on croyait invariant.

## E4.1r — ce qui a ete VU et deliberement PAS corrige (migration mecanique, 2026-09-01)

Q1 a ete tranchee : E4.1r migre les neuf autoscenarios **mecaniquement**, et E4.6d les reecrit
integralement ensuite. Tout ce qui suit a donc ete **laisse tel quel, expres**. C'est le dossier
d'entree d'E4.6d.

- **Les deux refus de `buildAutoscenarioCreate()` repondent LES MEMES OCTETS.** Le site « le
  factory a rendu null / le cast a echoue » et le site « `checkScenarioRules()` a echoue »
  repondent tous deux `{"error":"scenario creation failed"}`. Seule la ligne de journal les
  distingue, et un client ne la voit pas. Les deux sont atteignables et sont epingles separement
  par `core/JsonApiScenarioWireBytes_test` (`CreateWithoutAnyRoomIsRefused`,
  `CreateWhoseRulesCannotBeBuiltIsRefused`) — **par leur effet de bord, pas par leur payload**.
  Sur les quatre sites `perr` de la famille, il n'y a donc que **trois** orthographes.
- **`buildAutoscenarioModify()` mute AVANT de valider.** Elle appelle `deleteRules()` en deuxieme
  ligne, reconstruit toutes les etapes, applique le nom, la visibilite, la piece, le cycle et le
  `disabled` — **puis** teste `checkScenarioRules()` et peut repondre
  `{"error":"scenario modification failed"}`. Quand elle refuse, la configuration a **deja** ete
  modifiee en memoire et le scenario est laisse sans regles. C'est le « validation avant
  mutation » qu'E4.6d doit poser.
- **`buildAutoscenarioModify()` ne sauve pas la configuration sur son chemin d'erreur** —
  `SaveConfigIO()`/`SaveConfigRule()` sont apres le `return`. L'etat en memoire et l'etat sur
  disque divergent donc jusqu'au prochain enregistrement.
- **Le payload de `get` ne peut pas etre renvoye a `modify`** (deja epingle par E4.0c) : `get`
  emet `enabled`, `modify` lit `disabled`, et `get` n'emet ni `name`, ni `visible`, ni la piece.
  Non corrige.
- **`buildAutoscenarioCreate()`/`Modify()` utilisent l'indice du tableau JSON comme numero
  d'etape** : une etape `end` qui n'est pas la derniere fait perdre en silence les actions de
  toutes les etapes standard qui la suivent. Non corrige (epingle par E4.0c).
- **Une action dont l'IO ne resout pas est silencieusement ignoree** a l'ecriture, et l'etape
  correspondante est rendue vide a la lecture. Non corrige.
- **`buildAutoscenarioList()` ignore son argument** (`VAR_UNUSED(jdata)`) mais le prend quand
  meme, pour rester alignee sur les huit autres. Non corrige.
- **Un `deleteIO()` sur un IO interne d'un scenario VIVANT segfaute.** Trouve en cherchant un
  chemin vers le refus de `modify` : detruire `scenario_0_is_active` pendant que le scenario
  existe laisse `AutoScenario::ioIsActive` pendante, et le `modify` suivant la dereference.
  **Non atteignable par l'API** (aucune commande ne supprime un IO interne de scenario, et le
  chemin de rechargement reconstruit tout), donc **hors perimetre et non instruit** — mais c'est
  la meme famille que T3.40 et ca merite un ticket de duree de vie a soi. Le cas de test a ete
  reecrit pour passer par le fichier de configuration a la place.
  ✅⭐ **INSTRUIT ET CORRIGE le 2026-09-06 par [T3.57](T3.57.md), et deux points ci-dessus sont a
  rectifier** : (1) le segfault n'attend pas « le `modify` suivant », il tombe **dans le
  `deleteIO()` lui-meme**, par `refreshBrokenScenarios()` → `stopBrokenRun()` ; (2)
  ⛔ **« non atteignable par l'API » est faux** — rien ne refuse un `autoscenario_uid` duplique dans
  `io.xml`, deux scenarios partagent alors une seule machinerie, et `autoscenario delete` sur l'un
  detruit celle de l'autre ([T3.117](T3.117.md)).
- **`ScenarioNullGuard_test.cpp` garde son include jansson** alors qu'il n'utilise plus un seul
  symbole jansson. Include mort, laisse en place : le nettoyage des includes est E4.1c/E4.1x, pas
  ce ticket.
- **`JsonApi.h` garde son include jansson** alors que son corps ne contient plus **aucun** jeton
  jansson hors commentaires (16 -> 0). Il tombe avec E4.1s/E4.1x.

## E4.1r — l'adaptateur transitoire a **deux** appelants, et deux est le plancher (2026-09-01)

La fiche E4.1r annonce « un adaptateur transitoire pour **ce seul site** » et pose comme
acceptation `grep -rn <nom> src` -> **exactement 1**. **La fiche a oublie
`buildAutoscenarioList()`**, qui appelle elle aussi `Scenario::toJson()` — et l'en-tete de
`tests/core/JsonApiScenario_test.cpp` le documente noir sur blanc depuis E4.0c (« `it->toJson()`
sur chaque scenario, donc la liste bouge exactement comme un `get` »).

`Scenario::toJson()` a donc **deux** sites d'appel dans `JsonApi.cpp`, tous deux dans le perimetre
d'E4.1r, tous deux obliges de traverser les deux bibliotheques. Les trois ecritures possibles
etaient : deux conversions **en ligne** et zero adaptateur nomme (pire : rien a greper pour E4.6d),
**deux** adaptateurs (pire encore), ou **un** adaptateur a deux appelants. C'est le troisieme qui a
ete livre : `janssonScenarioPayloadBridge()`, **1 definition, 2 appelants**, tous deux commentes
comme tels. Ce n'est **pas** la recidive d'E4.1p (16 appelants la ou un suffisait) : ici deux est
le plancher structurel, mesure. A corriger dans la fiche plutot que dans le code.

## E4.1r — le NUL embarque est INATTEIGNABLE tant que le dispatch est en jansson (2026-09-01)

Mesure, et ca a tue l'hypothese evidente. On attendait le delta n5 de la serie (le NUL qui ne
tronque plus) au moins **cote entree** : `jansson_string_get()` construisait une `std::string`
depuis un `const char*` et tronquait, `jsonStringGet()` garde la chaine entiere. **La question
n'est jamais posee** : la requete est encore parsee par **jansson** sur les deux transports (le
dispatch est le perimetre d'E4.1s) et jansson **refuse** un NUL echappe dans une chaine sans
`JSON_ALLOW_NUL`. Le message est jete avant qu'une seule ligne d'autoscenario tourne — zero
reponse, zero scenario cree.

**E4.1s en herite** : nlohmann **accepte** un NUL echappe. Le jour ou le parse de requete bascule,
ces requetes commencent a etre servies, sur **toute** l'API et pas seulement ici. A mesurer la-bas,
pas a decouvrir. Epingle par `AnEmbeddedNulInAnActionIsRefusedByTheRequestParser`.

## E4.1r — deux des cinq deltas de la serie n'ont PAS lieu, a cause du fichier exclu (2026-09-01)

Tous les tickets de la serie depuis E4.1l rapportent **cinq** deltas. Sur le payload d'un
autoscenario il n'y en a que **trois** (ordre des cles, casse de l'hexadecimal, echappement de
`DEL`), et la raison est la meme pour les deux manquants : `IO/Scenario.cpp` est **exclu** (Q5) et
`Scenario::toJson()` fabrique encore ses chaines avec `json_string(const char *)`.

- **L'UTF-8 invalide est toujours DROPPE AVEC SA CLE**, un etage sous tout ce que ce ticket
  touche. Il ne devient pas `U+FFFD` ici.
- **Un NUL embarque tronque toujours** la valeur, au meme endroit.

=> **E4.6d herite des deux**, et c'est lui qui les rendra visibles — pas E4.1s. Un relecteur qui
cherche « les cinq deltas » sur ce ticket doit en trouver **trois**, et c'est le bon resultat.

## E4.1r — ce que les contre-mutations M3 et M5 disent du filet preexistant (2026-09-01)

Echanger `room_name` et `room_type` dans `buildAutoscenarioCreate()` ne rougit **qu'un seul cas de
tout l'arbre**, et c'est un cas ecrit par ce ticket (`RoomNameAndRoomTypeAreNotInterchangeable`).
Les 52 cas et 9 goldens d'E4.0c n'observaient pas la piece dans laquelle le scenario atterrit : ils
creent tous le scenario dans la **seule** piece de leur maison, donc l'echange y est invisible —
une « fixture pauvre » **preexistante**, pas introduite ici. Idem pour M5 (les deux orthographes de
refus echangees) : **trois cas rouges, tous les trois neufs**. Consigne parce que c'est exactement
l'angle mort qu'E4.6d devra couvrir quand il reecrira ces fonctions.

## E4.1s — la mine du NUL a explose, et voici exactement ce qu'elle a fait (2026-09-01)

E4.1r avait ecrit que le NUL embarque etait **inatteignable de bout en bout** tant que le parse de
requete restait en jansson, et que **E4.1s en heriterait**. C'est arrive. Mesure, sur les deux
transports :

- `Json::parse()` **accepte** un `\u0000` echappe, en **valeur** et en **nom de champ**, la ou
  `json_loads()` avait **deux refus distincts** (`"\u0000 is not allowed without JSON_ALLOW_NUL"`
  et `"NUL byte in object key not supported"`).
- A travers les emetteurs qu'E4.1s possede, le zero **traverse entier** : trois octets stockes,
  trois octets rendus, `\u0000` sur le fil. Epingle par
  `N_AnEscapedNulInAParamValueIsStoredWholeAndComesBackEscaped` et son jumeau WebSocket.
- A travers `Scenario::toJson()` (`IO/Scenario.cpp`, **exclu**, Q5) il est **TRONQUE EN SILENCE** :
  une action `a\0b` revient `"a"`. **C'est atteignable depuis ce ticket et ca ne l'etait pas
  avant.** Epingle par `AnEmbeddedNulInAnActionIsTruncatedByScenarioToJson`, avec E4.6d nomme dans
  le commentaire.

**DECISION, prise et assumee : ACCEPTER, EPINGLER, DECLARER.** Aucune garde n'a ete ajoutee sur le
parse d'entree. Une garde aurait ete un **refus neuf** que la fiche ne demandait pas, et elle aurait
masque la vraie cause, qui vit dans le fichier exclu. => **E4.6d doit corriger `Scenario::toJson()`,
et le test ci-dessus rougira le jour ou il le fera** — c'est voulu, il porte la consigne de le
reecrire.

## E4.1s — le parse d'entree accepte DEUX autres choses qu'il refusait, et la fiche se trompait sur l'une (2026-09-01)

Mesure sur une sonde compilee contre le vrai jansson et le `json.hpp` du depot (3.11.3) :

- **Un entier au-dela d'`int64`** : jansson repondait `"too big integer"` et jetait **tout le
  document**. nlohmann en fait un `double` (`1.2345678901234568e+29`), que le contrat
  d'aplatissement transforme ensuite en chaine par `Utils::to_string(double)`. Une requete qui
  recevait un `400` net est desormais **servie**.
- **La profondeur d'imbrication.** ⚠️ **La fiche d'E4.1s dit « jansson n'a pas de limite par
  defaut ». C'EST FAUX** : jansson plafonne a **2048** (`JSON_PARSER_MAX_DEPTH`) et repond
  `"maximum parsing depth reached"` ; nlohmann n'a **aucune** limite. 2048 passe des deux cotes,
  2049 ne passait pas et passe maintenant.
  ⭐ **Et ce n'est PAS un deni de service neuf, mesure plutot que suppose** : 100 000 niveaux sont
  analyses **puis liberes** sans debordement de pile (json.hpp 3.11.3 detruit iterativement), et le
  corps reste borne par la taille de la requete HTTP. Ce qui change est ce qui est **accepte**, pas
  la survie du processus.

**Les verrous, eux, n'ont pas bouge** et c'est epingle case par case : UTF-8 invalide, demi-substitut
isole, debordement de reel, contenu apres la fin du document. Un NUL **brut** termine toujours le
corps des deux cotes — `json.hpp` place `'\0'` a cote d'`eof()` dans son lexer, exactement comme
`json_loads(data.c_str())` s'arretait a la chaine C.

## E4.1s — un cas VERT pour la mauvaise raison, trouve par le tour rouge (2026-09-01)

`P_ANestingDepthAbove2048IsRefusedByTheParserToday`, ecrit dans le commit de caracterisation, est
reste **VERT** apres la bascule alors qu'il pinnait exactement ce que la bascule changeait. La cause
n'est pas le parseur : sa premiere moitie envoyait une requete refusee, donc **enregistrait un echec
de connexion**, et le **LoginThrottle** — pas le parseur — repondait `400` a la seconde moitie.

⚠️ **C'est le piege de la « fixture pauvre » sous sa forme la plus mechante** : le cas passe pour une
raison qui n'a rien a voir avec ce qu'il pretend mesurer, et rien dans la sortie ne le signale. Il
n'a ete vu que parce que le tour rouge attendait **14** mouvants et n'en a trouve que **13**.
=> Corrige : **toute** requete HTTP de `core/JsonApiDispatchWireBytes_test` passe par un helper qui
vide le throttle d'abord. **A retenir pour toute suite qui enchaine un refus et une acceptation sur
le meme transport HTTP** — le throttle a sa propre suite (`core/JsonApiThrottleIdentity_test`), il
n'a pas a etre l'oracle de quelqu'un d'autre.

## E4.1s — le convention de comptage jansson a deux faux positifs, et ils sont dans WebSocket.cpp (2026-09-01)

Le motif `\b(json_\w*|jansson\w*)\b` de la convention d'`ORCHESTRATION.md` compte **2 jetons dans
`src/bin/calaos_server/WebSocket.cpp`**, un fichier qui n'a **jamais** utilise jansson. Ce sont la
declaration et l'usage d'une variable locale nommee `json_body`, dans une reponse d'erreur deja
emise par `dump(-1, ' ', true, replace)`. Rien a corriger dans le code ; a savoir quand le compte
final d'E4.1x devra tomber a zero : **il tombera a 2, pas a 0**, sauf a renommer la variable ou a
raffiner le motif. Consigne pour que la cloture ne parte pas en chasse d'un jansson qui n'existe
pas.

## T3.36 — le premier `make check` repare est VERT, et il faut dire pourquoi ce n'est pas une preuve (2026-09-01)

La fiche et `ORCHESTRATION.md` annoncaient tous deux que la reparation du relink ferait
**probablement rougir** des suites qui ne passaient que grace a l'absence de relink, et que ces
rouges seraient des **trouvailles**. **Il n'y en a eu aucune** : `TOTAL 110 / PASS 108 / SKIP 2 /
FAIL 0 / ERROR 0` au `make distclean` final, et **0 FAIL** au premier `make check` d'apres
correctif.

⚠️ **Ce vert ne dit pas que le harnais etait sain ; il dit que la mesure est partie d'un arbre
entierement reconstruit.** Dans un arbre reconstruit de zero, aucun binaire de test n'est perime :
il n'y a rien a reveler. Le defaut ne produit un faux vert que dans un cycle **incrementiel** — on
mute, on rebase, on contre-mute — c'est-a-dire exactement le regime de travail de la serie E4.1, et
exactement celui que les briefs compensaient a la main. **Le seul rouge que le correctif ait
produit est celui qu'il devait produire** : `JsonPathSyntax_test` sur le defaut T3.29 reintroduit,
avec sa ligne `CXXLD` enfin presente.

**Consequence operationnelle** : ne pas lire ce vert comme « il n'y avait rien ». Les faux verts
passes de la serie ne sont pas rejouables a posteriori — les arbres ou ils sont survenus n'existent
plus. **Aucun ticket propose** : il n'y a pas de rouge a instruire.

## T3.36 — la convention `_DEPENDENCIES` gardait un VRAI danger, et il fallait le remplacer, pas seulement le retirer (2026-09-01)

Le commentaire *« Built elsewhere, do not let automake turn them into prerequisites »* n'etait pas
gratuit. `tests/Makefile.am` n'a aucune regle pour les `.o` de `calaos_server` — mais **`make` en a
une**, sa regle implicite `.cpp.o`, et automake en ajoute une seconde du meme nom. En build
in-tree, `src/bin/calaos_server/Calaos.cpp` **existe** a cote de son `.o` : declarer l'objet en
prerequis sans autre precaution autorise `tests/` a le **recompiler avec SES propres drapeaux**
(`AM_CPPFLAGS` de `tests/`, pas ceux de `src/`) — un objet silencieusement faux, la ou l'ancienne
forme ne donnait « que » un faux vert.

Le correctif ferme les deux : les objets sont prerequis **et** une regle explicite

```make
$(CALAOS_SERVER_BUILDDIR)/%.$(OBJEXT):
	@:
```

**masque** la regle implicite. Un objet reellement absent ressort alors en erreur d'edition de
liens **nommant le fichier**, au lieu d'etre recompile de travers. En pratique le cas ne se
presente pas (`SUBDIRS = src data tests` a la racine, boucle **serie** : `src/` est bati avant
`tests/`), mais la garde ne coute rien et rend l'invariant lisible.

**A retenir** : quiconque relira ce fichier verra des `.o` d'un autre repertoire en prerequis et se
demandera pourquoi c'est sur. La reponse est cette regle-la, et elle doit survivre a toute
regeneration future du fichier.

## T3.36 — `automake` refuse une variable partagee qui finit par `_DEPENDENCIES` (2026-09-01)

La variable partagee qui porte les prerequis de la famille `CORE_TEST_LDADD` s'appelait d'abord
`CORE_TEST_DEPENDENCIES`. `automake` la prend alors pour une variable **par cible** et avertit :
`variable 'CORE_TEST_DEPENDENCIES' is defined but no program or library has 'CORE_TEST' as
canonical name (possible typo)`. Renommee **`CORE_TEST_DEPS`**, l'avertissement disparait.

⚠️ Curiosite mesuree au passage : **`CORE_TEST_LDADD` n'avertit pas**, alors qu'il finit lui aussi
par un suffixe par-cible. La regle pratique est donc empirique et non deductible : **`_DEPENDENCIES`
est verifie, `_LDADD` ne l'est pas**. A savoir pour tout futur regroupement dans ce fichier.

## T3.36 — deux overrides `_DEPENDENCIES` sont inoffensifs, et il faut les nommer pour ne pas les « corriger » (2026-09-01)

Sur les **67** overrides `_DEPENDENCIES` de `tests/Makefile.am`, **2 ne relient aucun `.o` de
`calaos_server`** : **`JanssonResidues_test`** (deja nomme par la fiche) et
**`StringUtilsFromString_test`** (nouveau, arrive depuis). Leur `_LDADD` ne contient que
`libcalaos_common.la` et des bibliotheques : l'override n'y retire **rien**, et le laisser tel quel
est correct. Les compter parmi les suites aveugles donnerait **67 sur 102** au lieu de **65 sur
102**.

⚠️ **Et le piege de comptage inverse se reproduit a l'echelle d'aujourd'hui** : chercher
`$(CALAOS_SERVER_BUILDDIR)` **en toutes lettres** dans les `_LDADD` donne **45** — les 20 suites qui
n'ecrivent que `$(CORE_TEST_LDADD)` relient pourtant les 35 objets de `CORE_SERVER_OBJECTS` sans
jamais nommer la variable. *(La fiche annoncait le meme ecart a son echelle : 27 pour 47.)*
**Ce fichier ne se compte qu'en resolvant les variables** — et c'est desormais
`tests/check-test-deps.py` qui le fait, a chaque `make check`.

## E4.6b — un test qui vérifie « l'IO a disparu » par une SOUS-CHAÎNE du fichier casse dès que le fichier a le droit de citer l'id (2026-09-01)

Trois fixtures partagées vérifiaient qu'un IO amputé n'était plus dans `io.xml` ainsi :

```cpp
removeIoFromXml(ioXml, IO_TARGET);
EXPECT_EQ(std::string::npos, ioXml.find(IO_TARGET));   //<- la sous-chaîne, pas l'élément
```

`tests/core/AutoScenarioMigration_test.cpp` (`loadScenarioWithTwoAmputatedActions()`),
`tests/core/JsonApiScenario_test.cpp` et `tests/core/JsonApiScenarioWireBytes_test.cpp`.

E4.6b met la **définition** du scénario dans `io.xml` (D2) et cette définition **garde l'id d'une
action dont l'IO a disparu** (D4). La chaîne réapparaît donc **légitimement**, et les trois
préconditions tombent — **8 cas rouges d'un coup**, dont le garde-fou
`AnUnmarkedScenarioIoStillRunsItsRulesWhenTheButtonIsPressed`, alors qu'**aucune propriété mesurée
par ces cas n'avait bougé**.

⚠️ **Deux d'entre elles étaient des `ASSERT_`**, donc le corps de la fixture s'arrêtait là : les cas
en aval s'exécutaient sur l'**état du cas précédent** et échouaient sur des symptômes sans rapport
(un scénario « sain » là où on attendait `broken`). **Le message d'erreur ne désignait jamais la
cause.**

**La leçon, généralisable** : une assertion d'absence portée sur le **document entier** mesure
l'objet à travers une chaîne dont on suppose qu'aucune autre partie du document ne peut la
produire. Cette supposition n'est pas énoncée, donc personne ne la revoit quand elle devient
fausse. **Mesurer la structure** — ici `id="…"`, l'attribut qui déclare l'élément — et, quand la
chaîne survit ailleurs pour une bonne raison, **l'assertir positivement** : le test devient alors
un témoin de la nouvelle propriété au lieu d'un obstacle à elle.

## E4.6b — un jeton de test pris dans l'espace d'un allocateur global rend le cas dépendant de l'ordre (2026-09-01)

Le cas de l'`as_*` orphelin plaçait `as_s9_actions` sur l'IO scénario, « un pas qui n'existe pas ».
Les ids d'étape sont pourtant frappés `s<n>` par un **compteur de processus** : au moment où ce cas
tourne, `s9` peut très bien être le nom d'une étape **réelle** d'un autre cas du même binaire.
Le cas passait seul et échouait dans la suite complète — et il aurait pu faire l'inverse.

C'est la **récidive §9.4 d'E4.6.md** (« les sondes de test qui deviennent valides », E4.0c avait pris
`reenable` comme canari de commande inconnue et T3.18 a livré la commande), avec une variante
plus perfide : ici ce n'est pas un ticket futur qui rend la sonde valide, c'est **l'exécution
elle-même**, dans le même binaire, selon l'ordre. Corrigé en `as_s_orphan_actions` — un id d'étape
**valide** que l'allocateur ne peut **jamais** produire, puisqu'il ne frappe que `s<chiffres>`.
**Une sonde doit être hors de l'espace de nommage d'un allocateur, pas seulement hors des noms
observés aujourd'hui.**

## E4.6c — un prédicat de garde comparé à une valeur qui est vide des DEUX côtés au premier passage (2026-09-03)

Le générateur de règles ne doit toucher que ce qu'il a écrit, et se met en retrait tant qu'une règle
du scénario ne porte pas son uid. Écrit de la façon qui vient naturellement —
`rule->get_param(KEY_UID) != monUid` — le prédicat **ne tient pas au moment exact où il compte** :
sur une configuration antérieure à la définition, la définition n'existe pas encore, donc `monUid`
est **vide lui aussi**, les deux côtés comparent égal, et la garde ne se déclenche pas.

Mesuré sur `configs/raoulh` : **125 règles à l'entrée, 139 à la sortie** — rien de détruit, mais
14 règles construites à côté des 18 existantes, donc chaque scénario joue ses actions deux fois.
Avec le prédicat correct (« ne porte **aucun** uid »), 125 → 125, octet pour octet.

**La leçon, générale** : une garde qui compare l'état d'autrui à *son propre* état n'est prouvée que
si le cas de test rend les deux états **différemment vides**. Le premier cas de mise en retrait ne
retirait l'uid que des **règles** et laissait `autoscenario_uid` sur l'IO scénario : la garde
fautive y passait, parce qu'un côté était non vide. C'est le « fixture pauvre » de §9.3 d'E4.6.md
sous une forme qu'aucune des sept récidives précédentes n'avait prise — la pauvreté n'était pas dans
les **valeurs** de la fixture mais dans le fait qu'**un seul des deux termes de la comparaison**
était mis à l'épreuve. Le cas qui l'attrape retire l'uid des deux côtés, et il est le seul à rougir.

## E4.6c — `autoscenario create` d'un scénario cyclique écrit un chaînage faux, corrigé au démarrage suivant (2026-09-03)

Sur `master`, `buildAutoscenarioCreate()` appelle `checkScenarioRules()` **avant** d'ajouter les
étapes, et c'est cette passe-là qui applique le drapeau `cycle` au chaînage de la dernière étape.
Les étapes sont créées ensuite par `addStep()`, qui chaîne toujours vers `-1`. Résultat : un
scénario créé avec `cycle:"true"` est **sauvegardé avec sa dernière étape chaînée à `-1`**, donc il
ne boucle pas — jusqu'au redémarrage suivant, où la boucle de renumérotation de
`checkScenarioRules()` la réécrit à `0`. Un `rules.xml` fraîchement écrit n'est donc **pas** un
point fixe de la passe de démarrage, ce qu'aucun test ne disait.

E4.6c le fait disparaître par construction (le chaînage est calculé à chaque génération, à partir de
la définition), mais le défaut est réel sur `master` et vaut d'être consigné : **un scénario
cyclique créé par l'API ne boucle qu'après un redémarrage.**

## E4.6d — les `step_id` du payload rendaient les goldens dépendants de l'ordre d'exécution (2026-09-03)

**La trouvaille la plus coûteuse du ticket, et elle n'a rien à voir avec l'API.** Le schéma neuf
émet un `step_id` par étape (décision D3 : l'API adresse une étape par son identité, plus par sa
position). Les allocateurs de `AutoScenarioDef` sont **monotones à l'échelle du processus** et ne
recyclent jamais — c'est la propriété que D3 demande, et elle est juste **pour un serveur**.

Le harnais de test, lui, charge des dizaines de configurations sans rapport dans **un seul**
processus. Le `step_id` qu'un scénario reçoit dépendait donc du nombre de cas exécutés avant lui, et
**tout golden qui le porte devenait flaky sous `--gtest_shuffle`** — de même que les assertions
d'octets de `core/JsonApiScenarioWireBytes_test`. Le premier golden régénéré portait `s0`/`s1` parce
qu'il avait été produit sous un `--gtest_filter` ; le même cas dans la suite complète en aurait
produit d'autres.

**Ce qui aurait dû tirer la sonnette plus tôt** : le fichier de caractérisation d'E4.0c documente
déjà pourquoi `io_0` et `scenario_0` sont déterministes — « les deux générateurs ne balaient que le
`ListeRoom` **courant**, que la fixture vide entre les cas ». Un générateur qui ne dépend **pas** de
l'état courant est précisément celui qui échappe à cette garantie, et la série n'en avait encore
aucun dans un payload.

**Parade** : `AutoScenarioDef::resetIdAllocators()`, appelé par `CoreFixture::clearCoreState()`.
Sémantiquement, c'est l'état d'un processus qui vient de démarrer — et le chargement re-fold chaque
id lu dans les compteurs (`observeUid()` / `observeStepId()`), donc remettre les compteurs à zéro ne
peut jamais faire redonner un id existant.

**La règle générale, à porter aux tickets suivants** : *tout identifiant émis dans un payload doit
être une fonction de l'état que la fixture remet à zéro, ou la fixture doit apprendre à le remettre
à zéro.* Sinon le golden ne mesure pas le payload, il mesure l'ordre des cas — et il le fait en
silence, en restant vert tant que personne ne mélange.

## E4.6d — `Scenario::toJson()` tronquait un NUL et droppait l'UTF-8 invalide AVEC SA CLÉ : deux deltas, une seule ligne (2026-09-03)

Consigné parce que la **forme** du correctif est instructive. Les deux défauts que §6.1 d'E4.6.md
attribue à E4.6d — l'octet zéro coupé en silence, et la chaîne invalide supprimée avec sa clé parce
que ni le retour de `json_string()` ni celui de `json_object_set_new()` n'étaient testés — sont
**deux symptômes d'un seul choix de type** : `json_string(const char *)`.

En sortant `IO/Scenario.cpp` de jansson, la valeur entre dans le document en `std::string`. Les deux
transports dumpent déjà avec `ensure_ascii` et `error_handler_t::replace` (les trois invariants
d'émission posés par E4.1b), donc **aucune garde n'a été ajoutée** : l'octet zéro ressort échappé et
la chaîne invalide ressort remplacée, parce que c'est ce que fait le sérialiseur qu'on utilise déjà
partout ailleurs. Zéro ligne de code défensif pour deux pertes de données silencieuses.

⚠️ **Cela ne dispense pas de T3.58 volet (b)** — décider si le parseur doit *refuser* un NUL en
entrée reste ouvert, et les deux sont indépendants : un `toJson()` propre porte la valeur entière
mais ne dit rien de ce que l'API doit accepter.

## E4.1x — cinq copies d'un même contrat de lecture JSON, et personne ne les a repliées (2026-09-03)

`jsonStringGet()` (le défaut sur un membre absent, sur un membre qui n'est pas une chaîne, et sur
une racine qui n'est pas un objet) et `decodeJsonObject()` (l'aplatissement : chaîne telle quelle,
booléen en `"true"`/`"false"`, nombre par `Utils::to_string(double)`, tout autre type en chaîne vide
**avec la clé quand même ajoutée**) existent en **cinq exemplaires identiques** : `JsonApi.cpp`,
`JsonApiHandlerHttp.cpp`, `JsonApiHandlerWS.cpp`, `LuaScript/ScriptWire.h` et les wires drivers.

Quatre commentaires promettaient que `E4.1x` les replierait « une fois `Jansson_Addition.h` parti ».
**E4.1x ne le fait pas** : deux des copies vivent dans les deux fichiers de handler que `E4.6e`
réécrit ensuite, et replier un contrat dans un ticket dont le régime de preuve est « aucun octet ne
bouge » demanderait son propre filet. Les commentaires disent désormais la duplication au lieu de
nommer un ticket qui ne l'a pas tenue.

⚠️ **Le risque est que les cinq copies divergent en silence** : rien ne les compare entre elles, et
le contrat qu'elles reproduisent n'a plus de référence exécutable depuis que la bibliothèque
d'origine est partie. **Ticket recommandé : `E4.7`** (proposé, non ouvert — `E4.7` est libre), à lancer **après `E4.6e`**,
qui réécrit deux des cinq copies. Il les replierait en une seule, avec les cas de caractérisation
qui existent déjà dans `ScriptWire_test`, `JsonApiParamsWireBytes_test` et
`JsonApiScenarioWireBytes_test` comme filet.

## E4.1x — le `grep` d'acceptation d'une suppression ne doit pas mesurer la prose (2026-09-03)

Le critère d'acceptation 1 d'E4.1x demandait `grep -rn 'json_t\|jansson' src tests` **à zéro ligne**.
Il est **inatteignable**, et pas parce que le travail est incomplet : à la clôture il restait **122
lignes** dans `src/` (et 296 dans `tests/`), **toutes en commentaire** — de la prose qui explique *pourquoi* un contrat
écrit à la main reproduit exactement ce que faisait la bibliothèque d'avant — plus **un littéral de
chaîne**, `Utils::getTmpFilename("jpg", "_json_temp")`, que le motif attrape sur `_json_t`.

**La leçon, générale** : sur un ticket de suppression, le critère doit porter sur **le code**, pas
sur le texte du fichier. La convention de comptage de la série (commentaires, littéraux et
`#include` blanchis) est le bon instrument — elle rend **2** ici, et les deux sont des faux
positifs déjà documentés. Un critère écrit sur un `grep` brut oblige soit à mentir sur le résultat,
soit à effacer des commentaires utiles pour satisfaire un motif.

## E4.6e — trois trouvailles, aucune corrigée ici (2026-09-03)

### 1. `_ALLOWED_ACTIONS` du sidecar MCP autorise ce que le serveur refuse désormais

`src/bin/calaos_mcp/python/calaos_mcp/client.py:25` liste `"autoscenario"` parmi les actions
permises à une session de service, et `CalaosClient.autoscenario()` (`:156-158`) est câblé. Depuis
E4.6e le serveur **refuse** ce message aux sessions de service : l'allowlist cliente et le gate
serveur ne disent plus la même chose.

**Sans conséquence aujourd'hui, mesuré** : `CalaosClient.autoscenario()` n'a **aucun appelant** —
aucun outil MCP ne l'invoque (recherche exhaustive dans `src/` et `tests/`), ce qui confirme la
« surface morte » d'E4.6 §1.2. Un outil MCP écrit contre elle recevrait `{"error":"scope denied"}`.

⚠️ **Ce n'est pas seulement une ligne à supprimer, c'est une question de conception** : faut-il que
le sidecar puisse lire les scénarios (`list`, `get`) ? Si oui, le gate serveur devrait discriminer
la sous-commande, ce qu'E4.6e a délibérément refusé de faire (§8.7). **Hors périmètre** — autre
dépôt logique, et la réponse appartient à l'utilisateur.

### 2. Sortir `resetIdAllocatorsForTests()` de l'API publique n'a pas de voie propre

Le renommage porte l'interdit, mais la fonction reste **publique**. Les deux sorties envisagées et
écartées :

- **`friend` du fixture** : oblige `src/bin/calaos_server/Scenario/AutoScenarioDef.h` à déclarer un
  type de `tests/`. La dépendance partirait dans le mauvais sens.
- **classe imbriquée `Testing`** : ajoute un type public à l'en-tête pour héberger une seule
  fonction statique, et n'empêche personne de l'appeler depuis `src/`.

⛔ **Ce qu'il ne faut surtout pas faire, c'est la retirer** : sans elle les compteurs d'ids sont
process-wide et les goldens redeviennent dépendants de l'ordre d'exécution des suites. La parade
est acquise ; seule sa visibilité reste imparfaite.

### 3. Le silence de `camera` est le même défaut, au même endroit, et il survit

`JsonApiHandlerHttp::processCamera()` n'a toujours pas d'`else` : un identifiant de caméra **valide**
avec un `type` inconnu ne reçoit **ni réponse ni fermeture**, exactement comme `autoscenario` avant
E4.6e. Deux cas de `JsonApiSession_test` l'épinglent (`CameraWithAKnownIdAndAnUnknownTypeIsSilent`,
`CameraWithoutATypeIsSilent`). D8 nommait `autoscenario` seul, donc rien n'a été touché.

⚠️ **La leçon est de forme** : la famille est « un sous-dispatch sans `else` », pas
« `autoscenario` ». Personne n'a mesuré combien de sous-dispatchs de ces deux fichiers sont dans ce
cas ; le corriger domaine par domaine au fil des épiques laisse le dernier survivre longtemps.

---

## E4.6e — au merge : numéros de ticket proposés, et une quatrième trouvaille (2026-09-03)

Les trois trouvailles ci-dessus ont été **vérifiées au merge**, pas relues. Deux d'entre elles
n'étaient portées par **aucun ticket** ; elles le sont désormais, nominativement — **proposés, aucun
n'est ouvert**.

### `T3.59` (proposé) — la famille « un sous-dispatch sans `else` », recensée puis fermée

Reprend la trouvaille 3 ci-dessus. `JsonApiHandlerHttp::processCamera()` n'a toujours pas d'`else` :
un id de caméra **valide** avec un `type` inconnu ne reçoit **ni réponse ni fermeture** — le défaut
exact qu'E4.6e vient de corriger sur `autoscenario`, socket comprise. Deux cas de
`JsonApiSession_test` l'épinglent (`CameraWithAKnownIdAndAnUnknownTypeIsSilent`,
`CameraWithoutATypeIsSilent`), donc la bascule est déjà écrite d'avance.

⭐ **Ce que le ticket doit faire, et qui n'a jamais été fait** : **recenser** tous les sous-dispatchs
des deux handlers, pas seulement corriger `camera`. Personne ne sait combien il en reste. Corriger
domaine par domaine au fil des épiques est précisément ce qui laisse le dernier survivre des années.

### `T3.60` (proposé) — le transport HTTP n'a aucune notion de portée de service

`serviceScope` est un membre de `JsonApiHandlerWS` **et de lui seul** (`JsonApiHandlerWS.h:52`), écrit
en un unique point (`:752`, `processLoginService()`). `JsonApiHandlerHttp` ne le connaît pas : les
huit commandes que le gate refuse en WebSocket passent **intégralement** en HTTP.

⚠️ **L'écart n'est pas neuf, mais E4.6e le rend visible** : le même message `autoscenario` est
maintenant refusé sur un transport et accepté sur l'autre, et c'est le premier de la liste dont
l'asymétrie a été mesurée et écrite. **Aucun ticket ne portait ce sujet.** Deux réponses possibles,
et le ticket doit trancher plutôt que patcher : soit HTTP n'ouvre jamais de session de service et il
faut l'**écrire et le prouver par un test**, soit il le peut et le gate doit remonter sous le
dispatch commun.

### 4. `getEndStepAction(int)` n'a aucun lecteur — la justification écrite ne couvre que sa jumelle

`AutoScenario.h:282-283` conserve la paire `getEndStepActionCount()` / `getEndStepAction(int)` avec
pour raison qu'elles sont « le seul observable » de l'étape finale, laquelle entre dans l'empreinte
de pureté d'`AutoScenarioRules_test`. **Mesuré : l'argument ne vaut que pour la moitié de la paire.**

- `getEndStepActionCount()` → **un** lecteur, `tests/core/AutoScenarioRules_test.cpp:722`.
- `getEndStepAction(int)` → **zéro** lecteur, ni dans `src/` ni dans `tests/`.

Ce n'est pas une régression et rien n'en dépend, mais c'est la dette d'E4.6d **déplacée, pas soldée** :
un accesseur public qu'aucune ligne du dépôt n'appelle, gardé par une phrase qui décrit l'usage de
l'autre. Deux issues, l'une ou l'autre : l'empreinte de pureté lit **aussi** les actions de l'étape
finale, ou l'accesseur part. **À trancher dans `E4.6g`.**

## E4.6f — la périphérie du marqueur

### 1. ⭐ `rankOf()` rend `-1`, et `-1` est inférieur à tous les rangs — les cas `K_` étaient vacuants

`core/JsonApiModelWireBytes_test` compare des **rangs de clés** pour prouver que l'objet IO sort
**trié** et non dans l'ordre d'insertion. `rankOf()` (fichier, section des helpers) rend `-1` quand
la clé n'est pas là. `EXPECT_LT(rankOf(keys, "auto_scenario"), rankOf(keys, "id"))` reste donc
**vrai** quand `auto_scenario` **a disparu du payload** : renommer une clé en production laisse le
cas vert sur une prémisse fausse. Mesuré : la mutation M1 d'E4.6f (les deux marqueurs échangés)
laissait `K_IoObjectKeysAreSortedNotInsertionOrdered` **vert**.

Corrigé dans le ticket par un `ASSERT_GE(rankOf(keys, k), 0)` sur les sept clés que le cas compare.
⚠️ **Le même motif est à vérifier partout où `rankOf()` est comparé** — c'est un helper partagé de
ce fichier, et rien n'oblige les autres cas `K_` à nommer des clés qui existent.

### 2. Le paragraphe « A SCENARIO is among them » n'avait aucun témoin d'absence

`CalaosConfig.cpp` lève `anyScenario` dans la branche « étape de scénario » et ajoute, en fin de
rapport, un paragraphe qui prévient qu'un scénario **reste mort jusqu'à réactivation manuelle** —
ce qui est faux d'une règle ordinaire, laquelle revient toute seule. Aucun cas du dépôt ne lisait un
rapport dans lequel **rien** n'appartenait à un scénario : déplacer `anyScenario = true` dans
l'autre branche laissait **tout le dépôt vert**. Comblé par
`ScenarioDisabledMissingIoTest.TheScenarioParagraphIsAbsentWhenNoDisabledRuleBelongsToAScenario`,
seul témoin de la mutation M3.

### 3. L'alerte nomme le scénario par son **uid**, pas par son nom d'utilisateur — question ouverte

`- step of scenario 'as_0' (rule 't318_sc_step')` : `as_0` est l'identité que le modèle donne au
scénario depuis E4.6b, et elle est retrouvable dans `io.xml` (`autoscenario_uid="as_0"`, sur
l'élément qui porte aussi `name="…"`). C'est **strictement l'échange** de ce que faisait l'ancienne
alerte, qui imprimait `scenario_0` — aussi opaque, sur la même ligne du même fichier. **Mais ni l'un
ni l'autre n'est ce que l'utilisateur voit dans son interface**, qui est le `name` de l'IO scénario.

La version conservatrice a été livrée (échange de clé, aucun nouveau couplage). **Le lookup par
`ListeRoom::getAutoScenarios()` pour imprimer le `name` appartient naturellement à
[`E4.6h`](E4.6.md), qui rouvre déjà ce même canal d'alerte** (§6 : « alerter par le mécanisme
existant, `CalaosConfig.cpp:405-440` ») et doit de toute façon **nommer** un scénario perdu.

### 4. ⛔ `IO/Scenario.cpp` n'est toujours pas re-clé — §5.3 n'est pas atteinte, et aucun ticket ne la porte

Le marqueur qui **décide qu'un `AutoScenario` est construit** reste `auto_scenario`
(`IO/Scenario.cpp`, commentaire posé par E4.6c). Conséquences, toujours vraies après E4.6f :

- les **4 anciens scénarios de `configs/raoulh` sont toujours des auto-scénarios** et apparaissent
  dans `autoscenario list` — le mandat de §1.1 (« ils cessent d'être reconnus ») **n'est pas tenu** ;
- `Calaos::get_new_scenario_id()` survit avec son unique appelant, `buildAutoscenarioCreate()` ;
- les ids des 5 IOs internes restent **dérivés** du `scenario_id` (`scenario_0_step` & consorts), donc
  `ScenarioNullGuard_test::CheckScenarioRulesAbortsWhenInternalIoIsHijacked` reste pertinent.

Le re-cléage coûte **71 cas rouges sur 7 binaires** (M6 d'E4.6b, §8.4) et **fait basculer les deux
acquis d'E4.6a** que toute la série a traités comme des garde-fous. Ce n'est pas un oubli d'E4.6f :
c'est un ticket à part entière, qui doit d'abord **trancher** ce que devient le garde-fou. **Aucune
ligne de §6 ne le porte aujourd'hui.**

### 5. Le marqueur historique disparaît du payload des **IOs internes** — non compensé

`auto_scenario` était recopié sur les 5 IOs de machinerie de chaque scénario (`createInput()`), donc
`get_home` les rattachait visiblement à leur scénario. `autoscenario_uid` n'est posé **que sur l'IO
scénario**. Après E4.6f, un client qui voudrait grouper `scenario_0_step` avec son scénario ne le
peut plus par le payload générique. **Mesuré sans conséquence connue** : ces IOs sont
`visible="false"`, §1.2 a établi qu'aucun consommateur ne lit ce champ, et l'API `autoscenario` rend
la définition entière. Consigné parce que c'est la seule perte d'information du ticket.

### 6. ⭐ Trouvé au merge — une **troisième copie** de la liste des 16 params, jamais déclarée, publie encore `auto_scenario`

`buildJsonIO()` n'est pas le seul endroit qui construit le payload générique d'un équipement.
`RemoteUI/RemoteUIWebSocketHandler.cpp:317` en porte une **copie littérale**, la même liste de seize
noms dans le même ordre, recopiée à la main :

```
vector<string> params = { "id", "name", …, "state", "auto_scenario", "step", … };
```

Elle alimente `remote_ui_config_update`, le payload envoyé aux interfaces distantes. E4.6f ne l'a
pas touchée — elle est hors du périmètre §6, qui ne nomme que `JsonApi.cpp` et `CalaosConfig.cpp` —
et l'auteur ne la mentionne nulle part. **Conséquence : les deux transports divergent désormais.**
L'API 5454 publie `autoscenario_uid`, RemoteUI publie `auto_scenario`, sur le même équipement.

Ce n'est pas une régression d'E4.6f (RemoteUI publiait déjà l'ancien marqueur avant), mais c'est une
**duplication qui a échappé à toute la série** : une liste en dur recopiée est exactement ce qui
rend un re-cléage incomplet sans que rien ne rougisse. ⇒ **ticket proposé `T3.62`** : recenser les
copies de la liste de `buildJsonIO()`, les ramener à une seule source, et décider ce que RemoteUI
doit publier.

### 7. ⛔⭐ Le re-cléage du marqueur d'IO n'a **toujours aucun numéro** — ⇒ ticket proposé `T3.61`

Le §4 ci-dessus décrit le trou ; il ne lui donne pas de nom, et un finding sans numéro se perd.
Vérifié au source au merge : `IO/Scenario.cpp:60` teste bien `get_param("auto_scenario") != ""`
pour décider qu'un `AutoScenario` est construit, avec le commentaire d'E4.6c juste au-dessus, et
`Calaos::get_new_scenario_id()` (`Calaos.cpp:48`) survit avec son **unique** appelant
`JsonApi.cpp:2407`, qui frappe `auto_scenario` sur un scénario neuf. **§5.3 n'est pas atteinte.**

⇒ **ticket proposé `T3.61`, à ouvrir pour clore l'épique E4.6** : re-cléer le marqueur d'IO sur
`autoscenario_uid`, ce qui suppose d'abord de **trancher le sort des deux garde-fous d'E4.6a**
(`AnUnmarkedScenarioIoStillRunsItsRulesWhenTheButtonIsPressed`,
`AutoscenarioGetAndListIgnoreAnUnmarkedScenarioIo`), puis de dériver les ids des 5 IOs internes de
l'uid — donc de retirer `get_new_scenario_id()`. Coût mesuré par M6 d'E4.6b : **71 cas rouges sur
7 binaires**. ⚠️ **C'est ce ticket, et lui seul, qui rend vraie l'affirmation de §1.1** (« les
anciens scénarios cessent d'être reconnus ») : tant qu'il n'est pas fait, les 4 scénarios de
`configs/raoulh` restent des auto-scénarios et apparaissent dans `autoscenario list`.

### 8. Le motif `rankOf()`/-1, cherché ailleurs au merge — **un seul autre site**, sans risque immédiat

Audit demandé au merge après le §1. Résultats sur tout `tests/` :

- `rankOf()` n'existe que dans `core/JsonApiModelWireBytes_test.cpp` (3 cas l'utilisent).
  Le seul site **encore non gardé** est `K_GetHomeEnvelopeAndItsThreeMembersAreSorted:296-297`
  (`data` < `msg` < `msg_id`), qui compare des rangs sans jamais asserter la présence des clés.
  Risque **faible** — ces trois clés sont structurelles, pas des params optionnels — mais c'est
  exactement la même fabrique à cas vacuants. ⇒ **ticket proposé `T3.63`** : y poser le même
  `ASSERT_GE`, et interdire le motif « comparateur qui rend -1 pour absent » dans les suites de
  caractérisation.
- Les autres sentinelles `-1` du dépôt (`pumpUntil()`/`pumpUntilSince()` de `IoLifetimeTimer_test`,
  `ShutterImpulse_test`) sont **déjà gardées** : chaque appelant fait `ASSERT_GE(x, 0)` avant de
  comparer, et `ShutterImpulse_test` le documente en toutes lettres.
- `roomIndexOf()` (`ListeRoomOwnership_test.cpp:57`) rend -1 lui aussi, mais son résultat est passé
  à `lr.Remove()`, jamais comparé : un -1 y produit un échec, pas un faux vert.

## ⛔⭐ `T3.64` (proposé) — E4.6h : les sauvegardes de `config put` sont horodatées à la SECONDE, deux envois rapprochés n'en laissent qu'une (2026-09-03)

`Config::BackupFiles()` (`CalaosConfig.cpp`) nomme son dossier `"%d-%m-%Y_%H-%M-%S"`. Deux
`config put` **dans la même seconde** écrivent donc dans le **même** dossier, et le second y
recopie la configuration telle qu'elle est **après** le premier : la copie du premier est
**écrasée**, et l'état d'avant le premier téléversement n'existe plus nulle part.

**Mesuré, pas déduit** : la première version de
`AutoScenarioUploadGuardTest::TwoUploadsLeaveTwoBackupsAndTheNewestIsTheStateJustBeforeTheLastOne`
enchaînait les deux envois et voyait **une** sauvegarde là où le cas en attendait deux. Le cas
attend maintenant 1,1 s entre les deux, et le commentaire dit pourquoi.

⇒ **ticket proposé `T3.64`** (non ouvert) : donner à `BackupFiles()` un nom de dossier qui ne
peut pas collisionner (suffixe, ou refus de réutiliser un dossier existant). **C'est un chemin de
PERTE DE SAUVEGARDE**, pas une gêne cosmétique : l'état d'avant le premier téléversement n'existe
plus nulle part. Confirmé par l'agent de merge d'E4.6h.

**Portée** : ce n'est pas propre aux scénarios — c'est toute la configuration. En exploitation
normale deux téléversements sont séparés de bien plus d'une seconde (un humain manipule
`calaos_installer`), et le défaut ne se voit que sur des envois scriptés en rafale. **Non corrigé
par E4.6h** : `CalaosConfig.cpp` est hors du périmètre §6, et la correction (un suffixe, ou un
refus de réutiliser un dossier existant) touche tous les appelants de `BackupFiles()`.

## ⚠⭐ E4.6h — À ARBITRER PAR L'UTILISATEUR : deux registres d'alerte coexistent sur le même canal (2026-09-03)

`CalaosConfig.cpp:422` (l'alerte E4.2e re-clée par E4.6f) imprime
`- step of scenario '<uid>' (rule '<nom de la règle>')`. L'uid est ce que le **modèle** range, pas
ce que l'**utilisateur** reconnaît. L'alerte neuve d'E4.6h, sur le même canal, nomme au contraire
par le `name` avec l'uid en repli.

Les deux lignes du même canal ne nomment donc pas la même chose de la même façon. E4.6h **n'a pas
uniformisé** : `CalaosConfig.cpp` n'est pas dans son périmètre (§6), et le témoin d'échange d'E4.6f
`TheStartupAlertCalls…OnlyARuleTheProjectionWrote` fige le texte actuel. Le brief demandait le
lookup du `name` « puisque tu rouvres le même canal », ce qui a été lu comme portant sur l'alerte
**du ticket** ; l'utilisateur dormait et n'était pas joignable, donc la version conservatrice a été
livrée.

**Ce qu'il faudrait pour le faire** : un lookup uid → IO scénario → `name` (un balayage de
`ListeRoom`, l'uid n'étant indexé nulle part aujourd'hui), et retourner un cas d'E4.6f.
**Coût** : petit. **Décision** : à l'utilisateur.

## E4.6g — la doc n'était pas obsolète, elle était FAUSSE, et le mécanisme est toujours le même (2026-09-04)

`docs/04_scenarios.md` et `docs/03_rules_engine.md` ont été écrits par E4.5b contre le code de
`master = aa4821f7`, correctement et avec des `Fichier.cpp:ligne` vérifiés. Six sous-tickets plus
tard, **13 affirmations enseignaient le contraire du code livré** et **une trentaine de citations
`Fichier.cpp:ligne` désignaient une autre ligne** — la plupart du temps une ligne qui a l'air
plausible, ce qui est pire qu'une ligne hors fichier.

Deux enseignements, tous deux généraux :

1. ⭐ **Une doc adossée à des numéros de ligne se périme sans qu'aucun test ne rougisse.** Les
   décalages de cette passe viennent à 100 % de fichiers qu'E4.6 a rouverts (`JsonApi.cpp` +1416
   lignes, `AutoScenario.cpp` réécrit, `Rule.h` -5 lignes) — mais aussi de fichiers qu'E4.6 n'a
   **pas** touchés et qu'un autre ticket a décalés de cinq lignes (`InPlageHoraire.cpp`, T3.25 ;
   `ActionPush.cpp`, E4.1b). **`make check` était vert du premier au dernier jour.**
   C'est exactement ce que **T3.32** (`make check-docs`, contrôle ancré des références) doit
   fermer, et il est **toujours ouvert** : rien n'a vérifié cette passe à part la relecture.
2. ⚠️ **Un `grep` ne suffit pas à trouver les affirmations fausses.** Les plus dangereuses ne
   citaient aucun symbole disparu : « un scénario **oublie automatiquement** les étapes dont la
   règle a disparu » (`RELEASE_NOTES.md`, section E4.2f) est une phrase entièrement en français,
   qui décrit `purgeDeadSteps()` sans le nommer, et qui affirme aujourd'hui l'**inverse** du
   comportement livré. Il a fallu relire les trois documents en entier contre le code.

**Les 13 affirmations fausses corrigées**, pour mémoire : l'invariant `len(steps) == steps_count+1`
et les clés `steps_count`/`step_pause`/`step_type`/`action.id`/`action.action` (disparues) ·
la compaction `purgeDeadSteps()` et le latch `stepRuleDestroyed` (disparus) · `AutoScenario::END_STEP`
(disparu) · `Rule::auto_sc_mark` / `isAutoScenario()` (disparus) · le balayage orphelin de
`checkAutoScenario()` décrit comme actif (supprimé) · « `autoscenario` n'est pas soumis au
`serviceScope` » (il l'est) · « silence total sur un `type` inconnu » (c'est une erreur) ·
« `modify` blanchit un scénario amputé » (il ne le peut plus) · « le payload de `get` n'est pas
ré-injectable dans `modify` » (l'aller-retour est une identité) · « l'index du tableau JSON sert de
numéro d'étape » (les étapes ont un id) · les commandes `get_scenarios` / `get_scenario`
(n'existent pas, et n'ont jamais existé) · « un scénario oublie automatiquement les étapes dont la
règle a disparu » (il les conserve et se signale cassé) · « les deux différences qui récupèrent des
données perdues n'ont pas encore lieu » (elles ont lieu).

## E4.6g — dépassement de périmètre assumé : `AutoScenario::getEndStepAction(int)` supprimé (2026-09-04)

Solde renvoyé par le merge d'E4.6d puis par celui d'E4.6e. Vérifié à nouveau ici avant de couper :
**aucun lecteur dans `src/`, aucun dans `tests/`**. Son jumeau `getEndStepActionCount()` en a un,
`core/AutoScenarioRules_test:722`, dans l'empreinte de pureté de lecture
`ReadingAScenarioTwiceAnswersTheSameAndChangesNothing`.

E4.6e avait conservé la paire, avec pour raison écrite que « supprimer le seul indexeur laisse un
compteur qu'on ne peut pas parcourir ». **C'est vrai et ce n'est pas suffisant** : un compteur sans
indexeur reste un observable utile — c'est précisément l'usage qu'en fait le seul appelant — alors
qu'un indexeur sans appelant est du code que personne ne compile contre une attente. La symétrie
d'API n'est pas une raison de garder du code mort ; si un appelant en a besoin un jour, il le
réécrit en trois lignes.

Le commentaire au-dessus de `getEndStepActionCount()` (`AutoScenario.h:275-280`) a été réduit en
conséquence : il justifie maintenant **une** fonction, et il dit toujours pourquoi elle survit sans
appelant de production (`getCategory()` ne parcourt que les étapes standard).

## ⚠️ E4.6g — TRANCHÉ : les noms de cas basculés ne sont PAS renommés (2026-09-04)

Renvoyé à ce ticket par §8.6 (« un lecteur qui `grep` un nom en tire l'inverse de la vérité »).
Trois exemples au moins : `AnEmbeddedNulInAnActionIsTruncatedByScenarioToJson`,
`AGetModifyGetRoundTripIsAnIdentityOnlyWhileNothingIsMissing`,
`ReadingBackAndEchoingThePayloadRestartsAnAmputatedScenarioWithTwoSuccessTrue`.

**Décision : on garde les noms.** Trois raisons, dans cet ordre :
1. `E4.6.md` **cite ces noms comme critères d'acceptation** (§6.1 nomme le cas du NUL, §8.3, §8.5 et
   §8.6 en nomment une vingtaine). Renommer invaliderait une page de citations que **rien** ne
   vérifie — `make check-docs` (T3.32) n'existe pas ;
2. c'est la convention de la série : un cas de caractérisation **garde son nom** en basculant, ce
   qui est ce qui permet de le suivre d'un ticket à l'autre ;
3. chacun porte une bannière `✅ FLIPPED` juste au-dessus de son corps.

**Le risque reste réel** et il est traité là où il coûte le moins : `docs/04_scenarios.md` cite
désormais ces trois cas **avec un avertissement explicite** disant que le nom décrit le défaut
d'avant et qu'il ne faut pas en déduire un comportement. Si l'utilisateur préfère le renommage, il
appartient à un ticket qui mettra à jour `E4.6.md` dans le même commit.

## E4.6g — deux commentaires de `src/` désignent du code disparu (2026-09-04)

Non corrigés : hors du périmètre déclaré (documentation), et sans effet sur le comportement.
Signalés pour le ticket qui rouvrira ces fichiers.

- `Scenario/AutoScenarioDef.cpp:388` et `AutoScenarioDef.h:184-185` renvoient au « sweep of
  `ListeRoom.cpp:324` » pour expliquer pourquoi `auto_scenario` n'est jamais touché par le nettoyage
  des params orphelins. **Ce balayage n'existe plus depuis E4.6c** ; la raison, elle, tient toujours
  (ne pas toucher aux params d'autrui), mais elle s'appuie sur un site disparu.
- `AutoScenario.h:179` et `ListeRoom.cpp:344` gardent le nom `checkScenarioRules()`, qui n'est plus
  qu'un alias de `rebuildRules()` (`AutoScenario.h:180`). Volontaire, mais un lecteur qui cherche
  « check » ne trouve plus de vérification.

## ⚠️ E4.6g, au merge — le contrôle mécanique des références NE VOIT PAS l'affirmation fausse (2026-09-04)

Sondage du merge : **48 citations `Fichier.cpp:ligne`** rouvertes et confrontées à la phrase
qu'elles appuient, sur les 328 extractibles des deux documents. Les 328 résolvent vers un fichier
tracké **unique** et **aucune ne dépasse la fin de son fichier** — le contrôle mécanique de l'auteur
tient. **Une seule affirmation fausse trouvée**, et elle est instructive :

`03_rules_engine.md` disait qu'`InputTimer` « appelle `hasChanged()` lui-même à l'expiration
(dérivé, `IO/InputTimer.cpp:140-190`) ». **L'intervalle cité est le bon fichier, la bonne fonction,
et il contient bien `hasChanged`** — mais `InputTimer::hasChanged()` a un **corps vide**
(`:186-189`) et `TimerDone()` appelle `EmitSignalIO()` (`:174`), c'est-à-dire le chemin du signal
d'IO (`IOBase.cpp:157-162` → `ListeRule::ExecuteRuleSignal`). Corrigé au commit de doc du merge.

⭐ **Ce que ça dit de [T3.32](T3.32.md).** Un contrôle qui rouvre l'intervalle et vérifie qu'il
existe — ce que l'auteur a fait, correctement — **valide** cette citation. Une ancre de fragment la
validerait aussi (`hasChanged` est bien là). Seule la relecture humaine de la **phrase contre le
code** l'attrape. C'est la limite déjà écrite sur la ligne `T3.32` (« la référence plausible qui
ment »), mesurée une fois de plus, sur un ticket dont l'exactitude était l'unique livrable.

## T3.58 — les gardes du parseur de requête JSON (2026-09-04)

- ⭐⭐ **[F-JSON-2] `JsonApi::dumpJsonRedacted()` fait tomber `calaos_server` sur un document
  profond, AVANT le contrôle des identifiants — et ce n'est pas le parseur.** C'est la trouvaille
  de T3.58, et elle **corrige une affirmation d'E4.1s** (« ce n'est PAS un déni de service neuf :
  100 000 niveaux parsent et se détruisent sans débordement de pile »). E4.1s avait mesuré
  `Json::parse()` **seule**, et cette mesure-là est juste. Mais `processApi()` appelle ensuite
  `dumpJsonRedacted(jsonRootDoc)` (`JsonApi.cpp:491`) sur **chaque** requête, et l'argument est
  évalué quel que soit le niveau de journal. Cette fonction fait **trois** parcours récursifs :

  1. `Json copy = jroot;` — la copie profonde de `basic_json` est récursive ;
  2. la `std::function<void(Json &)> redact` qui se rappelle elle-même ;
  3. `copy.dump(4, …)` — le sérialiseur de `nlohmann` est récursif **et** indenté.

  L'indentation rend la sortie **quadratique en la profondeur**. Mesuré à `-O2`, pile 8 Mio :
  **16,8 Mo** de ligne de journal à 2048 niveaux, **1,07 Go** à 16 384, **7,4 Go** à 43 000, et
  **SEGFAULT à 44 000** (pile épuisée dans la copie profonde ; la trace montre 640 000 trames pour
  100 000 niveaux). Sur une box réelle c'est l'OOM qui tue avant, vers 16 000 niveaux.

  ⭐ **T3.58 rend ce chemin inatteignable depuis l'API** (plafond de profondeur à 2048 sur les deux
  transports) **mais ne corrige pas la fonction**. Elle reste quadratique, et 2048 niveaux — qui
  restent **acceptés** — coûtent toujours 16,8 Mo transitoires par requête non authentifiée.
  ⇒ **Ticket proposé `T3.65`** : ne construire la ligne rédigée que si le domaine `network` est
  effectivement journalisé, et/ou la dumper **non indentée**. Les deux sont indépendantes du
  plafond, et l'une des deux suffit à ramener les 16,8 Mo à quelques kilo-octets.

- ✅ **[F-JSON-1] FERMÉ par [`T3.62`](T3.62.md) (2026-09-04).** Le parse local de
  `RemoteUIWebSocketHandler::processApi()` est gardé par `requestNestingWithinLimit(data)` : au-delà
  de 2048 niveaux le document n'est plus construit du tout, le parent émet l'unique refus, la
  session reste ouverte et répond à la trame suivante. **Seuil exercé des deux côtés** dans
  `core/JsonApiStateWireBytes_test` (`AFrameAtTheCapIsServed` / `AFrameAboveTheCapIsRefusedByThe
  LocalParse`), sur la branche **locale** du handler (`remote_ui_get_config`) et non sur celle du
  parent : une garde posée seulement chez le parent laisse le premier cas rouge. Ce qui suit est le
  relevé d'origine, gardé pour la mesure.

  ⛔ **[F-JSON-1, relevé d'origine] `RemoteUIWebSocketHandler::processApi()` contourne le plafond.**
  `RemoteUI/RemoteUIWebSocketHandler.cpp:128` faisait son **propre** `Json::parse(data)` — sous
  `try`/`catch`, donc pas de `terminate` — **avant** de déléguer à `JsonApiHandlerWS::processApi()`
  (`:157`). Le plafond de T3.58 vit dans le parent : sur une socket RemoteUI, un document profond
  est donc **parsé une fois à plein tarif** avant que quoi que ce soit ne le refuse. Le correctif
  tient en une ligne — garder ce parse local derrière le même prédicat
  `JsonApi::requestNestingWithinLimit(data)`. **Non fait** : hors du périmètre déclaré de T3.58, et
  ce fichier est déjà le sujet de `T3.62`.

  ⭐⭐ **CE CONTOURNEMENT N'EST PAS UN CHEMIN PRÉ-AUTHENTIFICATION — lu de bout en bout au merge, et
  c'est ce qui rend le renvoi à `T3.62` acceptable.** Le handler n'est installé comme `jsonApi` de la
  socket que si `authenticateConnection()` a rendu `true` (`WebSocket.cpp:283-286`) ; sur un échec il
  est **détruit** et le handshake répond 401/403/429 puis ferme (`WebSocket.cpp:290-333`). Cette
  authentification est un **HMAC sur un secret partagé** provisionné : en-têtes obligatoires,
  `Bearer` non vide, fenêtre d'horodatage, limitation de débit, nonce de 64 caractères non rejouable,
  token connu, puis `RemoteUI::validateHMAC()` (`HMACAuthenticator.cpp:58-99`,
  `RemoteUIManager::validateAuthenticationWithReason()`). ⇒ **`processApi()` de RemoteUI n'est
  atteignable qu'après authentification forte**, et le plantage `F-JSON-2` reste **inatteignable par
  un client non authentifié**. Ce qu'un appareil RemoteUI **légitime** peut encore faire, c'est
  imposer **un** parse profond (allocation, pas la ligne de journal quadratique) — nuisance, pas
  déni de service pré-auth.

  ⚠️ **Deux autres `Json::parse()` sont, eux, VRAIMENT pré-authentification et n'ont pas de
  plafond** : `RemoteUI/RemoteUIProvisioningHandler.cpp:103` et le chemin MCP. **Ni l'un ni l'autre
  n'appelle `dumpJsonRedacted()`** — vérifié : cette fonction n'a que **deux** appelants, les deux
  `processApi()` gardés par ce ticket — donc aucun ne porte le défaut quadratique. **Non mesurés.**

- ✅ **[F-XML-1] FERMÉ par [`T3.66`](T3.66.md)** (2026-09-04). La garde est dans
  `IOBase::set_param()`, à la frontière du **modèle** : un **nom** ou une **valeur** portant un
  `\0` est refusé **avant** d'atteindre `io.xml`, `set_param()` rend `bool`, et l'API répond
  `{"error":"param refused"}` au lieu d'annoncer un succès. **Prouvé en rechargeant la
  configuration** — l'IO n'est plus renommé, l'attribut voisin est intact. ⛔ **Le parseur n'a pas
  été touché** : le NUL échappé traverse toujours la porte et l'API entière là où E4.6d l'a rendu
  traversant. ⚠️ **Deux chemins d'écriture restent hors garde** — voir `F-XML-2` ci-dessous, c'est
  la limite haute voulue. Le constat d'origine est conservé tel quel :

- ⭐⭐ **[F-XML-1, constat d'origine] L'écrivain XML coupe toute valeur — et tout NOM de paramètre — au premier octet
  nul, en silence, et la mémoire diverge du disque jusqu'au redémarrage.**
  `XmlUtils::setAttribute()` (`src/lib/XmlUtils.h:89`) finit sur
  `pugi::xml_attribute::set_value(value.c_str())`, et résout le nom par
  `node.attribute(name.c_str())` / `append_attribute(name.c_str())`. Trois conséquences mesurées
  par `core/JsonApiRequestGuards_test`, oracle `B_` :

  | Entrée | En mémoire | Dans `io.xml` | Au redémarrage |
  |---|---|---|---|
  | une valeur `head` + zéro + `tail` | 9 octets | `t358_nul="head"` | la valeur **est** `head` |
  | un **nom** `name` + zéro + `squat` | deux params distincts | l'attribut `name` **écrasé** | l'IO a **changé de nom** |
  | une action d'autoscénario `a` + zéro + `b` | l'étape entière | `as_s0_actions="t358_string=a"` | l'étape est **amputée** de sa 2ᵉ action |

  ⭐ **Le troisième cas est un effet de structure** : le codec de params d'`E4.6b` empaquette
  **toutes** les actions d'une étape dans un seul attribut, séparées par `|`, et son
  percent-encoding échappe `%`, `|` et `=` — **pas l'octet nul**. Un zéro dans la première action
  emporte tout le reste de l'étape.

  ⚠️ **Le correctif n'est PAS « passer la longueur »** : `pugixml` 1.14 a bien
  `set_value(const char_t *, size_t)`, et **le substituer ne change strictement rien** (mesuré,
  suite verte 114/114, les trois cas `B_` compris) — `pugixml` stocke et écrit par chaîne C de bout
  en bout. Et il ne pourrait pas l'être : un octet zéro brut dans un fichier XML est du XML
  invalide, que `pugixml` refuserait de relire. Le vrai correctif est un **codage réversible** dans
  l'écrivain **et** son décodage dans le lecteur, ou une **garde dans `IOBase::set_param()`**.
  **Options chiffrées et recommandation argumentée dans [`T3.58.md`](T3.58.md), volet (b).**
  ⛔ **Aucune n'a été implémentée : la décision revient à l'utilisateur.**
  ⇒ ✅ **Tranché et livré par [`T3.66`](T3.66.md)** : ni codage XML, ni refus au parse — **garde
  dans `IOBase::set_param()`**, sur décision utilisateur du 2026-09-04
  ([`DECISIONS.md`](DECISIONS.md)).

- ⚠️ **[F-XML-2] `set_param()` n'est pas le seul chemin d'écriture d'un paramètre d'IO, et les deux
  autres restent hors de la garde de [`T3.66`](T3.66.md) — délibérément.**
  `IOBase::SaveToXml()` écrit **tout** le `param` de l'IO ; y arrivent, sans passer par
  `set_param()` :
  - `AutoScenarioDef::saveToParams()` (`AutoScenarioDef.cpp:348`), appelé depuis
    `Scenario::SaveToXml()` (`IO/Scenario.cpp:186-192`), qui empaquette les actions d'une étape
    **au moment de la sauvegarde** — c'est le **troisième cas de `F-XML-1`**, et il n'est **pas**
    fermé ;
  - `ListeRoom::createIO(Params, Room*)` depuis `buildAutoscenarioCreate()`
    (`JsonApi.cpp:2457`), qui porte le **nom** du scénario ;
  - plus généralement `get_params()`, qui rend une référence **mutable**.

  ⭐ **Pourquoi c'est laissé** : les fermer imposerait un refus dans `parseScenarioPayload()`, et ce
  refus **ferait basculer le cas d'E4.6d** `AnEmbeddedNulInAnActionIsCarriedWholeByScenarioToJson`,
  qui a travaillé pour que l'octet traverse l'API **entière**. La garde serait posée **trop haut**.
  ⛔⭐ **CORRIGÉ AU MERGE DE [`T3.66`](T3.66.md) — l'argument ci-dessus est VRAI pour la moitié
  ACTIONS et TROP LARGE pour la moitié NOM.** Vérifié au source : le cas d'E4.6d
  `AnEmbeddedNulInAnActionIsCarriedWholeByScenarioToJson` pose le NUL dans une **action**, et son
  `name` vaut `"nul"` — propre. **Aucun cas d'E4.6d ne pose un NUL dans un NOM de scénario.**
  Fermer `buildAutoscenarioCreate()` sur le seul **nom** (`params.Add("name", payload.name)` puis
  `createIO()`, `JsonApi.cpp:2461-2468`) **ne demanderait donc pas de toucher à
  `parseScenarioPayload()`** et ne ferait basculer aucun cas. C'est la moitié **actions** — celle
  qui traverse `saveToParams()` — qui est réellement bloquée par E4.6d.

  ⚠️ **Conséquence assumée et visible** : `autoscenario modify` **refuse** un nom porteur d'un zéro,
  `autoscenario create` en écrit une version **tronquée**. ⛔ **Et ce demi-chemin n'est épinglé par
  AUCUN test** : `B_ANulInAnAutoscenarioActionCutsTheWholeEncodedStep`, resté **vert exprès**,
  mesure l'**action**, pas le nom (la phrase « les deux sont épinglés par » de la première rédaction
  était fausse). Le résidu reste modeste — un nom d'affichage **tronqué**, donc une VALEUR, sans
  squat de l'attribut voisin — mais l'API refuse ici et tronque là, sans que rien ne le mesure.
  ⇒ ✅ **MOITIÉ NOM FERMÉE par [`T3.71`](T3.71.md)** (2026-09-05, livrée avec
  [`T3.72`](T3.72.md) dans les mêmes commits) : `buildAutoscenarioCreate()` consulte
  `XmlUtils::isWritableAsAttribute(payload.name)` **avant `createIO()`** et rend le **même document**
  que `modify` (`{"error":"invalid payload: name refused"}`). ⭐ **Le défaut a été reproduit avant
  d'être corrigé** : sur `"nul\0squat"`, `create` répondait `{"data":{"id":"io_0"}}` et `io.xml`
  portait `name="nul"`. ⭐ **Et la garde de `T3.72` ne l'absorbait pas** — mesuré par contre-mutation :
  `createIO()` reçoit le `Params` **entier**, `set_param()` n'est jamais appelé sur ce chemin.
  ⛔ **La moitié ACTIONS reste ouverte**, épinglée comme telle par un cas témoin **vert des deux
  côtés** (`S_AnActionCarryingAControlByteStillCrossesTheApi`) : elle ne se rouvre que si quelqu'un
  décide que le NUL ne doit plus traverser l'autoscénario, et c'est alors E4.6d qu'on rediscute.
  ⭐⛔ **ET ELLE EST PLUS LARGE QUE `io.xml` — mesuré à la revue de merge de `T3.72`** : la valeur
  d'action devient aussi l'attribut `val` d'une action de règle (`ActionStd::SaveToXml()`), donc
  **`rules.xml` porte le même `&#01;`**. Le témoin l'asserte désormais sur les **deux** fichiers.
  ⇒ ce n'est pas « `rules.xml` n'est pas gardé » comme trou séparé : `JsonApi` n'a **aucun verbe
  d'écriture de règle** (les règles viennent de `calaos_installer`), et cette moitié-ci est le seul
  chemin par lequel des octets d'un tiers y arrivent. La fermer ferme les deux fichiers —
  [`T3.104`](T3.104.md).
  ⇒ **`F-XML-2` reste OUVERT sur cette moitié-là.**

  *Rédaction d'origine de la proposition* : refuser le zéro sur le seul **nom** dans
  `buildAutoscenarioCreate()`, avant `createIO()`, et l'épingler. La moitié **actions** reste
  ouverte et ne se rouvre que si quelqu'un décide que le NUL ne doit plus traverser l'autoscénario
  non plus ; c'est alors E4.6d qu'on rediscute, pas cette garde-ci.

- ✅ **[F-XML-3] FERMÉ par [`T3.72`](T3.72.md)** (2026-09-05). Sur l'arbitrage utilisateur du même
  jour : **refus à l'écriture**, rupture de compatibilité assumée. `XmlUtils::isWritableAsAttribute()`
  dit ce qu'une valeur d'attribut a le droit de porter — la production `Char` de XML 1.0 (5ᵉ éd., §2.2)
  — et deux appelants le consultent : `IOBase::set_param()` (élargi de l'octet nul à
  `[#x00-#x08]` ∪ `{#x0B,#x0C}` ∪ `[#x0E-#x1F]`) et `JsonApi::buildAutoscenarioCreate()`.
  ⭐ **`#x9`, `#xA`, `#xD` restent acceptés — ils SONT des `Char` — et `#x7F` aussi** (seul XML 1.1 le
  restreint). ⭐⭐ **Ce que ça NE change PAS, et c'était le vrai risque** : une `io.xml` existante qui
  porte déjà `&#01;` **se charge et se ré-enregistre à l'identique** — `IOFactory::readParams()` fait
  `Add()` et `IOBase::SaveToXml()` écrit sans consulter la garde. Seule une valeur **neuve** venue de
  l'API est refusée. C'est l'**endroit** de la garde (le modèle, pas le sérialiseur) qui l'obtient, et
  la contre-mutation qui la déplace dans `setAttribute()` fait rougir le cas qui le dit.
  ⛔ **Ce qui reste** : la moitié **actions** de `F-XML-2` (ci-dessus) écrit toujours `&#01;`, donc
  une maison qui s'en sert garde une `io.xml` non conforme ; et les points de code non-`Char`
  **au-dessus** de `#x7F` (`#xFFFE`, `#xFFFF`, demi-surrogates) ne sont pas vus. Voir
  [`T3.72.md`](T3.72.md) §9.

- ℹ️ **[F-XML-3, constat d'origine — le cas qui le mesurait N'EXISTE PLUS]** Les autres contrôles C0
  ne cassaient pas l'écriture, mais `io.xml` cessait d'être du XML 1.0 conforme. **Mesuré** à l'époque par
  `core/IoParamNulGuard_test::M_AnotherC0ControlByteIsEscapedAndSurvivesTheRoundTrip`, ⚠️ **supprimé par**
  **[`T3.72`](T3.72.md)** : il épinglait le comportement que la rupture retire. Ce qu'il décrivait — un `0x01`
  dans une valeur est écrit **`t366_ctrl="head&#01;tail"`** — une **référence de caractère**, pas
  l'octet brut — rien n'est coupé, aucun attribut voisin n'est touché, et l'aller-retour par le
  disque est **fidèle**. ⇒ **le zéro est bien le seul octet qui casse l'écriture**, parce qu'il est
  le seul qui termine la chaîne C avec laquelle `setAttribute()` résout le nom.
  ⚠️ **Ce qui reste faux** : XML 1.0 n'a aucune façon d'écrire un contrôle C0, donc `&#01;` est une
  référence qu'**aucun outil XML conforme n'est tenu d'accepter**. `pugixml` relit ce qu'il a
  écrit ; un éditeur tiers ouvrant `io.xml` peut refuser. ⛔ **Non élargi par [`T3.66`](T3.66.md)** :
  ce serait refuser des octets que l'API accepte aujourd'hui, sur un chemin que personne n'a
  signalé. ✅ **Confirmé au merge** : U+0001 n'appartient pas à la production `Char` de XML 1.0,
  donc `&#01;` est une référence à un caractère que la grammaire interdit — aucun analyseur
  conforme n'est tenu de l'accepter. ⇒ **Ticket proposé [`T3.72`](T3.72.md)**, priorité basse et
  **arbitrage utilisateur requis** : le fermer veut dire refuser des octets que l'API transporte
  aujourd'hui.

- ⚠️⭐ **[F-XML-4] Le `bool` neuf de `IOBase::set_param()` est lu par 2 appelants sur ~100, et
  3 des sites qui l'ignorent sont atteignables par des octets venus de l'extérieur.**
  ⚠️ **RAYON ÉLARGI par [`T3.72`](T3.72.md)** (2026-09-05, non corrigé) : la garde ne refuse plus le
  seul octet nul mais **29 points de code**, si bien que l'ensemble des entrées produisant un **no-op
  muet** aux trois sites ci-dessous grandit d'autant. Ce n'est pas une régression neuve — c'est le même
  défaut avec une porte plus large — et `T3.72` le dit dans sa section « nu » plutôt que de le laisser
  découvrir. ⭐ **Vérifié à la revue de merge** : les trois sites sont bien sur des chemins
  d'écriture atteignables de l'extérieur, mais `set_param()` **journalise chaque refus** sur ses deux
  branches — ce qui manque là-bas est la **réponse au client**, pas la trace, et le fichier de
  configuration reste sain dans tous les cas. ⇒ **`F-XML-4` reste OUVERT.** Recompté au
  merge de [`T3.66`](T3.66.md) : **~100 appels** dans `src/`, **2 testent le retour**
  (`JsonApi::buildJsonSetParam()`, `JsonApi::buildAutoscenarioModify()`). ⭐ **La quasi-totalité des
  ~98 autres passe des littéraux internes** (`set_param("gui_type", "light")`, `"visible"`,
  `"log_history"`, …) : le refus y est **impossible par construction**, et l'ignorer est légitime.
  **Trois sites sortent de ce lot** :
  - `IO/IntValue.cpp:379` — `Internal::Save()`, cas `TSTRING` : `set_param("value", svalue)` où
    `svalue` vient de `set_value()`, donc de l'API. ⚠️ **Le plus net** : la ligne suivante fait
    `Config::SaveValueIO(get_param("id"), get_param("value"))`. Le refus laisse `param["value"]`
    **inchangé**, si bien que le cache d'état persiste **l'ANCIENNE valeur** au lieu de la nouvelle.
    Avant le correctif il persistait une valeur **tronquée** ; dans les deux cas c'est muet.
  - `RemoteUI/RemoteUIProvisioningHandler.cpp:174-185` — `device_type`, `device_manufacturer`,
    `device_platform`, `device_version`, `mac_address` viennent du corps JSON de l'appareil qui se
    provisionne ; le refus est un **no-op silencieux** et la réponse reste un succès.
  - `RemoteUI/RemoteUIWebSocketHandler.cpp:77,82` — mêmes clés depuis les en-têtes de la connexion.

  ⭐ **Ce n'est pas une régression** : sur ces trois sites l'octet est en position de **VALEUR**, donc
  l'ancien comportement était une troncature, jamais le squat d'attribut qui renommait un IO. La
  garde **supprime la corruption du fichier partout** ; ce qui reste est un **silence côté
  appelant**, exactement le motif que [`T3.25`](T3.25.md) a mesuré sur `Utils::from_string()`
  (310 sites sur 319 ignorant le retour). ⇒ **À instruire avec [`T3.71`](T3.71.md)** : décider, site
  par site, si le refus doit remonter à l'appelant ou rester un no-op journalisé.

- **[F-JSON-3] La perte de précision sur les nombres n'est pas au parseur, elle est au contrat
  d'aplatissement, et elle mord bien en deçà d'`int64`.** `Utils::to_string(double)`
  (`src/lib/StringUtils.h:301`) est un `ostringstream` nu : sa précision par défaut est de **six
  chiffres significatifs**. Mesuré à travers `set_param` : `1234567` est stocké `"1.23457e+06"`,
  `9223372036854775807` devient `"9.22337e+18"`, et l'entier hors `int64` de la note d'E4.1s
  devient `"1.23457e+29"` — **exactement le même mécanisme**. Seul `42` survit intact.
  ⇒ **Une garde sur les entiers hors `int64` fermerait une fenêtre dans un mur qui n'existe pas.**
  ⭐ **Et l'échappatoire existe déjà, mesurée** : les mêmes trente chiffres envoyés comme **chaîne
  JSON** sont stockés exacts, parce que tout param est une chaîne une fois stocké.
  `Utils::to_string(double)` est **gelée exprès** et épinglée par des goldens : la rouvrir est un
  ticket à elle seule. **Recommandation dans [`T3.58.md`](T3.58.md), volet (c).**

- **[F-TEST-3] Le nom d'un cas d'E4.6d ment sur ce que le cas asserte.**
  `AnEmbeddedNulInAnActionIsTruncatedByScenarioToJson`
  (`tests/core/JsonApiScenarioWireBytes_test.cpp:671`) **asserte désormais l'inverse de son nom** :
  E4.6d a réécrit son corps pour exiger que le NUL traverse **entier**, ce qui est le bon
  comportement — mais le nom est resté celui de l'état tronqué. Un lecteur pressé conclura que la
  troncature vit encore. ✅ **RENOMMÉ au merge de T3.58** en
  `AnEmbeddedNulInAnActionIsCarriedWholeByScenarioToJson` (une ligne, corps inchangé), avec ses deux
  renvois vivants recalés (`docs/04_scenarios.md`, `T3.58.md`). ⚠️ **Les fiches `E4.1s.md` et
  `E4.6.md` citent encore le nom d'origine : ce sont des récits d'époque, où le cas assertait bien
  la troncature. Ils n'ont pas été réécrits.**

- **[F-PYIMG-1] L'image de développement publiée est en retard sur son propre `Dockerfile`, et
  aucune mesure ne le dit.** ⇒ ✅ **VOLET « aucune mesure » FERMÉ par [T3.67](T3.67.md)** :
  `scripts/pydeps-conformance-probe.py` compare les `.dist-info` réellement posés dans
  l'environnement à ce que `pyproject.toml` déclare, câblée en entrée `TESTS`
  (`check-pydeps-conformance.sh`, `77` par défaut / `1` sous `CALAOS_PYDEPS_STRICT`), dans `ci.yml`,
  et **dans la chaîne `RUN` des deux `Dockerfile`** (3 blocs, l'étage `runner` compris) — le seul des
  trois points qui puisse empêcher la publication d'une image en dérive, ⚠️ **par déduction de la
  sémantique `&&` : aucun `docker build` n'a été fait**. ⭐ **Mesurée rouge sur la dérive RÉELLE**, nommant **huit** paquets absents : la
  sonde en a trouvé **un de plus (`websockets`) que l'énumération manuelle ci-dessous**. ⛔ **La « trouvaille »
  de T3.67 — `roonapi`/`reolink-aio` présents ⇒ « le `pip` de la recette a tourné » ⇒ « ce n'est pas
  une vieille image » — a été RÉFUTÉE à la revue de merge par datation** : image créée le
  **2026-05-28 20:29**, `.dist-info` de `/usr/local` tous horodatés `2026-05-28 18:29`, et la ligne
  `pip install "mcp[cli]" uvicorn fastapi` n'entre dans `.devcontainer/Dockerfile` que le
  **2026-06-04** (`c57ec9be`) ; `/usr/local` ne contient QUE la clôture transitive de `roonapi` +
  `reolink-aio` (ni `anyio`, ni `click`, ni `h11`, ni `httpcore`, ni `sniffio`). ⇒ **c'est bien une
  vieille image**, antérieure au serveur MCP lui-même — le diagnostic d'origine de `F-PYIMG-1` tient,
  et le remède reste **reconstruire l'image**. ⛔ **VOLET « reconstruire l'image » TOUJOURS OUVERT** :
  `push` interdit et image non republiée sur `T3.67`, la dérive est encore là.
  Constat d'origine : `.devcontainer/Dockerfile` installe depuis `T3.23` le jeu déclaré par
  `src/bin/calaos_mcp/pyproject.toml`. **L'image montée n'en a rien** : mesuré dedans, `mcp`,
  `fastapi`, `uvicorn`, `pydantic`, `starlette` et `httpx` répondent tous `ModuleNotFoundError`
  (seul `colorama`, qui vient de l'`apt`, est là). ⇒ **le sidecar MCP ne peut pas démarrer dans
  l'image où on le développe**, et les 11 cas de `test_auth.py` ne pouvaient pas y tourner.
  ⚠️ **Rien dans le dépôt ne compare l'image publiée à son `Dockerfile`** : la dérive est
  indétectable jusqu'à ce que quelqu'un `import fastapi`. **Non corrigé** — reconstruire l'image
  n'est pas dans le périmètre de `T3.47` et coûte une reconstruction complète du devcontainer de
  l'utilisateur. ⇒ **[T3.47](T3.47.md) §5.1**, ticket proposé **[T3.67](T3.67.md)** (fiche courte,
  non instruite). ⛔ **Le vrai sujet n'est pas l'image mais l'absence de mesure** : `ci.yml` ne
  construit **aucun** des deux `Dockerfile` du dépôt, que `T3.47` a pourtant modifiés.

- **[F-CIENV-1] Une variable d'environnement globale de `make check` traverse le méta-oracle qui
  teste le script qu'elle pilote.** `T3.47` fait de `CALAOS_PYTHON_TESTS_REQUIRED=1` un
  « ne pas pouvoir exécuter est une erreur ». La variable est posée sur le `make check` entier, donc
  elle est **héritée** par les sous-invocations de `tests/check-python-tests-reporting.sh` — dont
  **treize** fabriquent un arbre dont le verdict honnête est `77` et l'assertent. Mesuré au premier
  `make check` strict : `FAIL: check-python-tests-reporting.sh`, **13 checks rouges**, sur un
  correctif dont ce n'était pas le sujet. ⭐ **Trouvé uniquement parce que le mode strict a été
  réellement exercé** ; une relecture de diff ne l'aurait pas vu, et un `push` aurait rougi sur un
  test sans rapport. ✅ **Corrigé** : le script désarme la variable en tête, `C6` la ré-exporte dans
  son propre sous-shell. ⚠️ **La leçon dépasse ce cas** : tout futur drapeau d'environnement lu par
  un script de `TESTS` doit être désarmé par les oracles qui invoquent ce script.

- **[F-PYTEST-1 — seconde couche, fermée]** `T3.44` avait réparé le **rapport** ; les 42 cas ne
  tournaient toujours **sur aucune machine**. ⭐ **Deux causes distinctes, pas une** : la CI n'a
  **aucun `python3`** (le harnais court-circuite sur `PYTHON=:`, `0/42`), l'image de dev en a un
  mais **aucun des modules** (`23/42`, RC 77, les 19 cas manquants nommés). ✅ **Fermée par
  [T3.47](T3.47.md)** : `42/42` exécutés et verts, mesuré dans les deux images, et un `SKIP` sur une
  machine qui doit les exécuter est désormais un **échec de build**, pas une colonne que personne ne
  lit. ⚠️ **La cible `42/42` n'est pas vérifiée chez GitHub** : `push` interdit sur ce ticket.

- ✅ **[F-CCACHE-1] Le canal `CC` n'était pas audité du tout, et la docstring de la sonde déclarait
  fermé un trou qui restait ouvert.** ⭐ **Deux défauts d'un seul fichier, de nature opposée.**
  (a) `scripts/ccache-honesty-probe.py` n'auditait que `CXX`. L'arbre porte **12 fichiers `.c`** et
  `configure.ac` appelle `AC_PROG_CC` : **la moitié du build n'avait aucune garde d'honnêteté de
  cache**, et rien ne le disait — la sonde imprimait un `PASS` qui se lisait comme couvrant le
  build. ✅ **Corrigé** ([T3.51](T3.51.md) §10, `f93e7471`) : les deux canaux sont audités, chaque
  message nomme son canal, et un canal **sans** cache en service est **déclaré non audité** sur sa
  propre ligne. Mesuré dans les deux sens, dont ⭐ **un enrobage menteur posé sur le SEUL canal `CC`
  ⇒ `1`** — invisible avant.
  (b) ⛔ **Le mode *fail-open* ⑫ était marqué « PROUVÉ corrigé » et ne l'était pas.** La docstring
  citait `CXX='$(CXX)'; export CXX;` dans `AM_TESTS_ENVIRONMENT` ; **vérifié sur `master` comme sur
  la branche parquée**, `tests/Makefile.am` n'exporte que `abs_top_srcdir`, `abs_top_builddir` et
  `PYTHON`. ⭐ **Le correctif retenu est de corriger la docstring, PAS de fabriquer l'export** :
  ajouter trois lignes non éprouvées pour donner raison à un commentaire est l'inverse de la
  discipline du dépôt. Le trou est réécrit en **TODO ouvert** avec sa mesure de fermeture
  (§10.2) — **il reste ouvert**.
  ⚠️ **La leçon dépasse ce cas** : un commentaire qui écrit « PROUVÉ » nomme un fichier ; le seul
  coût de la vérification est d'ouvrir ce fichier, et c'est ce qui a manqué pendant trois jours.
  Une passe trouvée **non commitée** dans un worktree n'a été relue par personne — la mettre à
  l'abri sur une branche a été le bon geste, la merger telle quelle aurait propagé le mensonge.

## T3.62 — la projection RemoteUI (2026-09-04)

- ✅ **[F-REMOTEUI-1] FERMÉ par [T3.68](T3.68.md)** (voir la section T3.68 en fin de fichier).
- ⛔⭐ **[F-REMOTEUI-1] → ticket [`T3.68`](T3.68.md). `remote_ui_get_config` est SANS RÉPONSE sur tout écran dont l'`io.xml`
  n'a ni `brightness` ni `timeout`, et le journal accuse le mauvais coupable.**
  `RemoteUI::getRemoteUIConfigMessage()` (`IO/RemoteUI/RemoteUI.cpp:496-497`) lit ces deux params
  par un **`std::stoi` nu** : `get_param()` rend `""` pour un param absent, `std::stoi("")` lève
  `std::invalid_argument`, et l'exception remonte dans le `try` de
  `RemoteUIWebSocketHandler::processApi()` — celui qui existe pour les erreurs de parse. L'écran ne
  reçoit **rien**, et la ligne écrite est `RemoteUIWebSocketHandler: JSON parse error: stoi`, qui
  désigne une trame parfaitement valide. ⚠️ **Mesuré, pas raisonné** : la fixture de
  `RemoteUiConfigProjectionTest` ne recevait aucune réponse tant qu'elle n'a pas posé les deux
  params, et le journal du binaire porte la ligne mot pour mot. Le jumeau `getBrightness()`
  (`:522`) fait pourtant déjà la bonne chose (`from_string_or_keep`, défaut 100), et
  `sendConfigUpdate()` passe par `parseGridDimension()` pour la même raison. **Non corrigé** :
  `IO/RemoteUI/RemoteUI.cpp` est hors du périmètre déclaré de T3.62, le défaut porte le
  numéro [`T3.68`](T3.68.md). Correctif :
  `from_string_or_keep` sur les deux, avec les défauts de `getBrightness()`.

- 🟡 **[F-REMOTEUI-2] INSTRUIT par [`T3.69`](T3.69.md) — mergé sur `master` en `fbaad93c`
  (2026-09-04, non poussé). LE VOLET « LISTE » ET LE VOLET « CODE » SONT FERMÉS ; IL RESTE DEUX
  QUESTIONS UTILISATEUR.** La **liste** des params publiés avait été ramenée à une source unique par
  [`T3.62`](T3.62.md) ; le **code** l'est maintenant à son tour — `buildJsonIO()` prend la projection
  qu'elle construit et `sendConfigUpdate()` l'appelle, les trois politiques sont trois conditions
  dans une seule fonction au lieu de deux boucles tenues en phase à la main. ⛔ **Aucun wire ne
  bouge**, et c'est délibéré : vérifié au merge, les goldens 5454 sont identiques des deux côtés et
  la branche `device` reproduit l'ancienne boucle terme à terme.

  ⭐ **Mesure décisive : il n'existe AUCUNE négociation de version avec l'écran.** Le serveur connaît
  `device_version` (`RemoteUIProvisioningHandler.cpp:183`, en-tête `X-Device-Version` via
  `HMACAuthenticator.cpp:42`) mais son **seul** usage est l'OTA (`RemoteUIManager.cpp:344`) : elle ne
  conditionne aucune trame. `protocol_version` / `api_version` / `wire_version` dans le protocole
  RemoteUI : **0 site**. ⇒ le serveur ne peut pas servir deux formes ; tout changement de
  `remote_ui_config_update` est **global et irréversible** pour le parc déjà posé.

  - **Delta `state`/`var_type` : JUSTIFIÉ, refermé sans changement.** `set_param("state")` /
    `Add("state")` / `paramAdd("state")` : **0 site** dans `src/`, rien sous `data/` — aucun IO que
    le serveur construit ne porte ces noms comme params, donc **l'écran n'en reçoit aucun** et tient
    ses états de `remote_ui_io_states`. Côté 5454 le calcul sert des consommateurs réels
    (`calaos_mcp/tools/io.py:25`, `rooms.py:53`, `_home.py:12` ; 18 `"state": ""` dans 4 goldens).
    **Un delta justifié n'est pas une divergence à supprimer.**
  - ❓ **QUESTION UTILISATEUR 1 — param présent mais vide.** 5454 émet `"unit": ""`, l'écran omet la
    clé. Niveler vers « omettre » retire 18 valeurs de 4 goldens et contredit le contrat E4.1m ;
    niveler vers « émettre » pose des clés vides neuves sur un écran déjà livré. **Non tranché.**
  - ❓ **QUESTION UTILISATEUR 2 — `status_info`.** L'ajouter au payload de configuration y fait
    entrer un **objet imbriqué** là où les `io_items` n'ont jamais porté que des chaînes ; le retirer
    de 5454 ampute l'API. **Non tranché.**

  ⚠️ **La réserve qui a dicté le conservatisme : le dépôt ne contient pas le micrologiciel de
  l'écran.** Il est donc impossible de prouver ici qu'un écran livré **ignore** une clé inconnue
  plutôt que de **refuser la trame** — ni la clé vide de la question 1, ni l'objet imbriqué de la
  question 2. Trancher demande soit le micrologiciel, soit un essai sur un appareil réel.

  Les deux décisions se posent maintenant sur **une** ligne de `buildJsonIO()`, et exigeront une note
  de version le jour où elles tombent. Garde-fou en place :
  `RemoteUiConfigProjectionTest.ApartFromTheThreeKnownDeltasBothProjectionsAgree` — chaque delta est
  affirmé avoir mordu **avant** d'être soustrait, le reste doit être égal octet pour octet, une
  quatrième politique rougit.

- 📜 **[F-REMOTEUI-2, énoncé d'origine] Les deux projections d'un IO ne suivent pas la même politique de valeur, et
  une seule des deux est justifiée.** `buildJsonIO()` calcule `state` et `var_type` à partir de la
  valeur de l'IO, émet une **chaîne vide** pour un param présent mais vide, et ajoute
  `status_info` ; `sendConfigUpdate()` lit `state` et `var_type` **comme des params** (donc
  quasiment jamais présents) et **laisse tomber tout param vide**. T3.62 ramène la **liste** à une
  source unique — c'est elle qui avait divergé — mais **pas la politique**, parce que l'unifier
  changerait la charge utile envoyée à un **appareil physique** non mis à jour en même temps que le
  serveur : trois deltas, dont un objet imbriqué (`status_info`). ⇒ **Ticket [`T3.69`](T3.69.md)** : décider ce
  que l'écran doit recevoir (il reçoit déjà ses états par `remote_ui_io_states`, donc `state` et
  `var_type` y sont probablement du bruit), puis faire appeler `buildJsonIO()` par
  `sendConfigUpdate()` — avec une note de version, parce que c'est un changement de wire.

## T3.68 — le silence de `remote_ui_get_config` (2026-09-04)

- ✅ **[F-REMOTEUI-1] FERMÉ — mergé sur `master` en `befa8297` (2026-09-04).** `getRemoteUIConfigMessage()` passe par `getBrightness()` et par un
  `getTimeout()` neuf, tous deux bâtis sur `Utils::from_string_or_keep()` — non lançants. Un écran
  provisionné et jamais réglé reçoit sa configuration. ⭐ **Reproduit avant correction** : le commit
  de caractérisation rougit sur `"the screen received nothing at all"` et sur la ligne
  `[WRN] remote_ui (RemoteUIWebSocketHandler.cpp:160) … JSON parse error: stoi`.

  🔒 **Rejoué à la revue de merge** : les trois fichiers `src/` de `master` remis en place, le cas
  `AnUnadjustedScreenIsAnsweredWithUsableDefaults` rougit sur `the screen received nothing at all`
  et `AValidFrameIsNeverBlamedOnTheJsonParser` sur la ligne `JSON parse error: stoi` — **les deux
  moitiés d'invariant (55/45, trame tronquée) restent vertes**, elles ne dépendent pas du correctif.
  Restauration par copie **vérifiée au `cmp`** (rc 0 ×3), `std::stoi(get_param` **0 site**.

- ⚠️ **Le défaut de `timeout` n'est étayé par AUCUN code, et c'est consigné plutôt que masqué.**
  `set_param("timeout")` : **0 site** dans tout `src/` ; `ioDoc` ne déclare pas le param ; le dépôt
  ne contient pas le micrologiciel de l'écran. **30** est la seule valeur que l'arbre énonce — tous
  les exemples de `src/bin/calaos_server/RemoteUI/remote-ui.md` (modèle d'`io.xml`, réponse de
  provisioning, réponse REST) la portent. Retenue faute de mieux, **déclarée dans la fiche et dans
  le code**. `brightness` = 100 est d'un autre statut : c'est le défaut que `getBrightness()` porte
  depuis [T3.25](T3.25.md), et le correctif **appelle** cette fonction au lieu de recopier le
  nombre. ⭐ **Le sens même de `timeout` n'est écrit nulle part** (extinction ? veille ?).

- ⭐ **Ce que le `try` de `processApi()` masquait EN PLUS du `stoi`.** Il englobait tout le service
  du message, pas seulement le parse : les **écritures d'état déclenchées par une trame
  `remote_ui_relay_state`** (`handleRelayState()` → `RemoteUIOutputRelay::updateStateFromDevice()`,
  donc le moteur de règles et ce qu'il appelle) et la **sérialisation de la réponse** dans
  `sendJson()`. Toute exception venue de là était journalisée « JSON parse error ». Le `try` est
  coupé en deux : le parse garde son nom, le reste est nommé
  `unhandled failure while serving <msg>`. ⚠️ **La chute vers le parent après une exception est
  laissée telle quelle** — c'est le comportement d'avant, et le changer serait un changement de
  wire hors périmètre.

- ✅ **[F-REMOTEUI-3] FERMÉ par [`T3.70`](T3.70.md) — mergé sur `master` en `ba4a3e66` (2026-09-04), non poussé** — texte d'ouverture conservé ci-dessous, la mesure du mode d'échec est plus bas.
  ⚠️ **Le dernier `std::stoi` non gardé de `src/`.**
  Balayage complet des conversions lançantes de `src/` **hors bibliothèques tierces**
  (`src/lib/cpptui`, `src/lib/exprtk`) : **5 sites**, dont **4 déjà gardés** par un `try`
  (`IO/RemoteUI/RemoteUI.cpp:332`, `RemoteUI/OtaFirmwareManager.cpp:81`, `IOBase.cpp:226`,
  `JsonApi.cpp:435`). Le cinquième, `IO/RemoteUI/RemoteUI.cpp:213`, convertit les attributs `x`/`y`
  d'un widget dans `LoadFromXml()` **sans aucun `try` sur le chemin** : un `x=""` ou un `x="haut"`
  dans l'`io.xml` lève dans le chargement de la configuration. ⛔ **Non corrigé ici, et
  volontairement** : le mode d'échec est le chargement, pas la réponse due à un appareil, et le
  remède demande de trancher entre « widget ignoré » — ce que le bloc voisin fait déjà pour un
  `x`/`y` absent — et « position à 0 ». Le mélanger au correctif de `remote_ui_get_config` ferait un
  diff que personne ne relit.

## T3.61 — re-cléage du marqueur d'IO (2026-09-04)

- **[F-T361-1] `Scenario::captureDefinitionFromRules()` est devenu inatteignable.** Ses deux gardes
  sont `!auto_scenario` puis `auto_scenario_def->isDefined()`. Depuis le re-cléage, un
  `AutoScenario` n'existe que si l'IO porte `autoscenario_uid`, donc `loadFromParams()` a réussi,
  donc la définition **est** définie : le second `return` tombe toujours. Le bootstrap
  « reconstruire une définition à partir de sa projection » ne tourne plus jamais.
  **Conservé volontairement** (version conservatrice) : c'est la seule fonction du serveur capable
  de refaire ce chemin, et la supprimer — avec ses deux helpers `scenarioMachineryIds()` et
  `collectRuleActions()` — est une décision de conception, pas un nettoyage. Le commentaire de
  `IO/Scenario.h` le dit désormais. **Candidat à un ticket de suppression.**

- **[F-T361-2] La machinerie d'un scénario neuf porte encore `auto_scenario`.**
  `AutoScenario::createInput()` (`Scenario/AutoScenario.cpp`) frappe les IOs internes avec la clé
  historique, dont la valeur est maintenant l'uid. Personne ne la lit : c'est l'état inerte que
  §5.3 d'`E4.6.md` décrit pour la production. **Non re-clé volontairement** — `JsonApi::
  ioProjectionParams()` publie `autoscenario_uid`, donc la re-cléer ferait apparaître l'uid du
  scénario sur ses 3 à 5 IOs de machinerie dans `get_home`/`get_io` et dans le payload RemoteUI,
  et bouger des goldens, pour un param que rien ne consomme. Le retirer entièrement est l'autre
  option ; elle mérite son propre ticket, avec la question « un outil tiers s'en sert-il pour
  regrouper les IOs d'un scénario ? ».

- **[F-T361-3] ⚠️⚠️ Le chemin `master intermédiaire → T3.61` duplique les règles — MESURÉ,
  `configs/raoulh` passe de 125 à 141.** Un serveur bâti sur un `master` d'entre E4.6b et T3.61
  écrit, au premier enregistrement, un `autoscenario_uid` et un `autoscenario_steps` dans l'`io.xml`
  d'une config héritée (mesuré aussi : les 4 scénarios de `configs/raoulh` en gagnent 8 à 9 params).
  Rechargée par T3.61, cette config **est** reconnue comme portant 4 définitions, et le générateur
  s'arme : il écrit **16 règles neuves à côté des 18 anciennes**, qui portent
  `auto_scenario="scenario_N"` et qu'il ne trouve plus (`scenarioRules()` cherche l'uid). La mise en
  retrait d'E4.6c ne se déclenche pas, faute de voir ces règles. **Rien n'est détruit** — les 18
  survivent — mais les scénarios joueraient leurs actions **deux fois**.
  **Non traité, et l'état n'est pas atteignable depuis une version publiée** : aucun serveur
  distribué n'a jamais écrit `autoscenario_uid`. ⭐ **Vérifié au merge, pas repris sur parole** :
  `autoscenario_uid` entre dans `src/` le **2026-09-01** (`3f6aae3c`, E4.6b) et **aucune étiquette
  du dépôt ne contient ce commit** — la plus récente, `4.4.3-dev.11`, date du 2026-08-24.
  ⚠️ **En revanche l'état EST atteignable depuis `master` lui-même**, qui écrivait ces params
  jusqu'au merge de T3.61 : toute configuration démarrée sur un build de développement de la série
  est concernée. ⛔ **À trancher avant de publier un build intermédiaire de cette série.** Il n'est atteignable qu'en démarrant un build
  intermédiaire de la branche de refonte sur une config réelle, puis en la rechargeant. Si un tel
  fichier existe quelque part, le remède est de retirer les params `autoscenario_*` / `as_*` des 4
  IOs scénario avant de démarrer. **À trancher avant toute publication d'un build intermédiaire.**

## T3.70 — le dernier `std::stoi` non gardé (mergée sur `master` en `ba4a3e66`, 2026-09-04)

- ⛔⭐ **[F-REMOTEUI-3] LE MODE D'ÉCHEC N'ÉTAIT PAS CELUI QUE LA FICHE ANNONÇAIT : le serveur ne
  démarre pas du tout.** L'ouverture disait « lève dans le chargement de la configuration ». Le
  chemin a été suivi maillon par maillon et **aucun ne porte de `try`** :
  `RemoteUI::LoadFromXml()` → `IOFactory::CreateIO(node)` (`IOFactory.cpp:65`) →
  `Room::LoadFromXml()` (`Room.cpp:174`) → `Config::LoadConfigIO()` (`CalaosConfig.cpp:310`) →
  `main.cpp:150`, où **`grep -c try main.cpp` = 0**. ⇒ `std::invalid_argument` sort de `main()`,
  `std::terminate()`, **la boucle d'événements n'est jamais atteinte**. Un seul `x=""` ou
  `x="haut"` dans l'`io.xml` met **toute l'installation** par terre, pas un écran.
  ⭐ **Mesuré avant correctif** (commit `baeb917a`, zéro ligne de `src/`) : `it throws
  std::invalid_argument with description "stoi"`, puis `the screen holding the misspelled widget
  was lost whole` et `the screen of the NEXT ROOM was lost too: the load stopped there` — les deux
  dernières visibles parce que les cas emploient `EXPECT_NO_THROW` et non `ASSERT_`.

- ⭐ **Le remède ne crée pas de convention : il branche le widget sur celle qui existait déjà.**
  `Utils::from_string_or_keep()` n'écrit la coordonnée que si toute la chaîne se lit comme un
  entier ; sinon l'attribut reste **absent** et le widget tombe dans le bloc voisin, écrit avant ce
  ticket, qui écarte tout widget sans `type`/`x`/`y`. C'est aussi la convention de tout le
  chargement : `IOFactory::CreateIO()` avertit et ne crée rien sur un `type` inconnu,
  `Room::LoadFromXml()` ignore les éléments dont il ne connaît pas le nom, `RulesFactory` laisse
  tomber une condition/action mal formée. ⛔ **« Position à 0 » écartée** : un widget empilé en
  haut à gauche, indiscernable d'un `x="0"` légitime. ⛔ **« Garder la chaîne brute » écartée** :
  elle satisferait le `contains("x")` du bloc voisin et enverrait `"x": "haut"` à un appareil qui
  attend un nombre — changement de wire déguisé.

- ⭐ **Un widget écarté est désormais ANNONCÉ, et l'ancien cas silencieux avec.** Le widget écarté
  ne revient pas : le `SaveConfigIO()` suivant réécrit la page sans lui. Le `cWarningDom` seul ne
  suffisait pas, alors le drop dépose **un** message par écran sur le canal différé mail/push du
  chargement de configuration (`Config::reportConfigAlert()`, public depuis E4.6h, le même que le
  fichier corrompu d'E4.6f et les règles désactivées d'E4.6h), nommant l'écran, la page et chaque
  widget. ⚠️ **Élargissement assumé** : un widget sans `type` — écarté en silence depuis toujours —
  déclenche maintenant la même alerte. Les deux catégories partagent le même chemin après ce
  ticket ; les distinguer n'aurait servi qu'à préserver un silence.

- ⭐ **Le dépôt possédait DÉJÀ un oracle pour « `x="0"` est une position », et il l'a prouvé.** La
  contre-mutation M4 (0 traité comme illisible) a fait rougir non seulement le cas de contrôle de
  ce ticket mais **le témoin `core/JsonApiStateWireBytes_test`** : sa fixture
  (`JsonApiStateWireBytes_test.cpp:822-823`) pose deux widgets en `x="0" y="0"` et `x="1" y="0"`,
  et les assertions de wire de [T3.68](T3.68.md) les épinglent. ⚠️ **Conséquence pour la méthode** :
  ⛔ **cette phrase était fausse et la revue de merge l'a mesurée** :
  **30** binaires de test lient `IO/RemoteUI/RemoteUI.$(OBJEXT)` (comptés dans `tests/Makefile.am`,
  T3.36 les fait tous relinker), pas deux. Ce qui est vrai : **deux sources seulement construisent
  un `<calaos:widget>`** (`tests/core/RemoteUIDeviceInfo_test.cpp`,
  `tests/core/JsonApiStateWireBytes_test.cpp`), donc aucun témoin vert **exerçant le chemin muté**
  n'existait pour M4 — mais 28 autres binaires liant l'objet muté auraient servi de témoin de
  spécificité, et n'ont pas été lus. Le relink reste prouvé par le `CXXLD` lu et par le changement
  de verdict. ⭐ **La découverte, elle, tient** : l'oracle « 0 est une position » préexistait bien à
  ce ticket.

- 🔒 **Rejoué à la revue de merge, sur l'arbre REBASÉ.** Le `std::stoi` nu de `master` remis en
  place par copie (jamais un `git` dans le conteneur), `make -j32` puis les deux suites :
  `CXXLD core/RemoteUIDeviceInfo_test` et `CXXLD core/JsonApiStateWireBytes_test` **lus**,
  **4 rouges / 1 vert** dans la suite du ticket et le témoin **PASS**. Les trois lignes de la
  reproduction sont revenues mot pour mot : `it throws std::invalid_argument with description
  "stoi"`, `the screen holding the misspelled widget was lost whole`, `the screen of the NEXT ROOM
  was lost too: the load stopped there`. Restauration **`cmp` rc 0**, `std::stoi(attr_value)`
  **0 site**. Le bloc voisin qui écarte un widget sans `type`/`x`/`y` a été **lu sur `master`
  (`6841880a`)** : il préexiste au ticket, la convention n'est pas inventée.

- ⚠️ **Ce sur quoi T3.70 reste nu.** (1) Le widget fautif **disparaît définitivement de l'`io.xml`**
  au premier enregistrement — l'alerte prévient, elle ne restaure pas ; conserver la ligne
  demanderait un modèle « attributs bruts » que ce loader n'a pas. (2) **`w`/`h` restent des
  chaînes** sur le wire, comme avant ce ticket : seuls `x`/`y` sont convertis, et rien n'est changé
  là. (3) **Aucun `io.xml` réel porteur du défaut n'a été observé** ; le cas part d'un fichier
  construit pour, comme la fiche d'ouverture le demandait.

## T3.65 — la ligne de journal rédigée, construite pour rien (2026-09-04)

- ⭐⭐ **[F-LOG-1] `LogStream` évalue TOUT ce qu'on lui donne et ne décide qu'au destructeur.**
  Ce n'est pas propre à `dumpJsonRedacted()` : c'est le contrat de **toutes** les macros
  `cDebugDom()` du dépôt. `LogStream::operator<<` (`src/lib/Logger.h:82`) écrit dans un
  `ostringstream` sans rien demander, et `~LogStream` (`Logger.cpp:157`) compare enfin
  `logData->level` au plafond du domaine. ⇒ **un argument coûteux est payé à tous les niveaux et
  imprimé à un seul.** Le niveau par défaut est `4` (INFO), `LOG_LEVEL_DEBUG` vaut 5 : sur un
  serveur de série, **tout argument de `cDebugDom()` est construit puis jeté**.

  ⭐ **Mesuré sur le chemin de requête** : à 2048 niveaux — la profondeur que T3.58 laisse passer —
  une requête HTTP coûtait **37,7 ms et 77,7 Mo de pic RSS** pour une ligne de journal que personne
  ne lit ; après le garde, **0,45 ms et 13 Mo**. Une requête ordinaire ne bouge pas (0,19 ms).
  **`dumpJsonRedacted()` n'a pas été touchée** : elle rend les mêmes **16 769 109 octets** sur le
  document à 2048 niveaux.

  ⚠️ **T3.65 n'a gardé QUE ses deux appels.** `cDebugDom()` est utilisé partout ailleurs sans
  garde, et le prédicat neuf (`Logger::isLevelEnabled()`, macro `cDebugDomEnabled()`) est
  disponible pour les autres sites coûteux. **Aucun autre site n'a été recensé ni mesuré.**

- ⚠️ **[F-JSON-2, ce qui reste] `dumpJsonRedacted()` reste quadratique quand le débogage est
  vraiment allumé.** Le garde ferme le chemin par défaut ; il ne rend pas la fonction linéaire.
  Avec `debug_domains network:5`, une requête à 2048 niveaux produit **toujours** 16,8 Mo de ligne,
  recopiés une fois de plus dans l'`ostringstream` du `LogStream` avant d'atteindre `stdout`.
  **Le correctif restant tient en un caractère** — `dump(4, …)` → `dump(-1, …)` — et il est **une
  décision de produit**, pas une réparation : deux cas nommés épinglent la forme indentée
  (`JsonApiRedact.RedactedDumpKeepsRawUtf8AndStaysIndented`, qui dit « c'est encore la forme
  INDENT(4) qu'un humain lit », et `HidesCredentialFieldsWhateverTheKeyCase`, qui asserte
  `"CN_Pass": "***"`). ⛔ **Non fait : l'utilisateur n'a pas été consulté, et `E4.1m` avait
  documenté ce choix de forme.**

- ⭐ **[F-TEST] Cinq des onze noms de la liste sensible n'étaient épinglés par rien.**
  `"passwd"`, `"pass"`, `"old_password"`, `"new_password"` et `"secret"` pouvaient disparaître de
  `dumpJsonRedacted()` **sans faire rougir un seul cas**. Mesuré par contre-mutation : échanger
  `"secret"` contre un littéral de message de journal du même fichier laissait la suite **verte**
  avant ce ticket. `JsonApiRedact.MasksEveryKeyOfTheSensitiveList` balaie les onze, avec un contrôle
  (`"passenger"`) qui refuse le match par sous-chaîne.

- ⚠️ **Ce sur quoi T3.65 reste nu.** (1) **Le garde lui-même n'est pas épinglé par un cas** : il ne
  change **rien** d'observable — même journal, même wire — et seule la mesure le voit. Ce qui est
  épinglé, c'est que `isLevelEnabled()` répond **exactement** ce que `~LogStream` imprime, à chaque
  niveau (`JsonApiRequestLog.TheDebugGuardAnswersWhatTheLogPrints`). Un mutant qui **retire** le
  garde reste vert ; un mutant qui **ment** sur le niveau rougit. (2) **Le domaine `"network"` écrit
  au site d'appel n'est vérifié par personne** : une faute de frappe y ferait taire le garde sans
  rien casser. (3) **Aucune mesure sur une vraie box** : tous les chiffres viennent du conteneur de
  développement.

## T3.65 mergée — ce que le merge a vérifié, et ce qui reste ouvert (2026-09-04)

- ⭐⭐ **[F-JSON-2] PARTIELLEMENT FERMÉ.** Le chemin est **inatteignable** depuis l'API
  (plafond 2048, [T3.58](T3.58.md)) **et** la ligne rédigée n'est **plus construite pour rien**
  (le garde, T3.65). ⚠️ **Ce qui reste ouvert : la fonction est toujours quadratique** le jour où
  `debug_domains network:5` est réellement allumé — 16,8 Mo par requête au plafond.
  **Le correctif restant tient en un caractère**, `dump(4, …)` → `dump(-1, …)` dans
  `JsonApi::dumpJsonRedacted()`, et c'est une **décision de produit** : deux cas nommés épinglent
  la forme indentée — `JsonApiRedact.RedactedDumpKeepsRawUtf8AndStaysIndented` (« c'est encore la
  forme INDENT(4) qu'un humain lit ») et `JsonApiRedact.HidesCredentialFieldsWhateverTheKeyCase`
  (qui asserte `"CN_Pass": "***"`, séparateur indenté compris). Défaire une forme documentée par
  `E4.1m` n'appartient pas à un agent. **Arbitrage prêt, non pris.**

- 🔒 **La prémisse a été revérifiée au source au merge, et elle tient.** `LogStream::operator<<`
  (`src/lib/Logger.h:81-85`) écrit dans son `ostringstream` **sans demander le niveau** ;
  `~LogStream` (`Logger.cpp:157`) est le **seul** endroit qui compare, et il sort avant même de
  formater. Sur `master` d'avant le ticket, `grep` de `isLevelEnabled|maxLevel` dans `Logger.h`
  rendait **0** : le niveau n'était **demandable par personne**. L'ajout est de **13 lignes** au
  total, sans effet de bord — `maxLevel()` délègue au `maxLevelPrintable()` statique qui servait
  déjà au destructeur, donc le prédicat et le journal lisent **la même table de domaines**.

- 🔒 **Mesures rejouées au merge**, garde retiré puis remis (restaurations `cmp` rc 0), même
  binaire, mêmes entrées, un processus par cas : requête HTTP servie au plafond
  **32,3 ms / pic RSS 76,3 Mo → 0,51 ms / pic 13,4 Mo** ; requête ordinaire **1,28 → 1,29 ms** ;
  ⭐ `dumpJsonRedacted()` rend **16 769 061 octets identiques** avant et après — **la sortie n'a
  pas bougé d'un octet**, ce qui est la preuve que l'optimisation n'a rien changé d'observable.

- ⚠️ **[F-LOG-1] OUVERT — et le recensement a été refait au merge, pas cru sur parole.**
  `src/` porte **468** sites `cDebug()` / `cDebugDom()`, dont **141** évaluent un appel dans leur
  argument. Tous rendent une chaîne bornée (`get_param()`, `Utils::to_string()`, `.size()`,
  `what()`) **sauf un** : `src/bin/calaos_server/Audio/Squeezebox.cpp:792`,
  `cDebug() << SqueezeboxWire::prettyPrint(json)`, qui est un `dump(4, ' ', …)` **indenté et non
  gardé**, payé à chaque réponse du serveur LMS. ⇒ **même forme que le défaut de T3.65, tout autre
  ordre de grandeur** : le document vient d'un appareil du réseau local, il n'est pas imbriqué, et
  aucun chemin pré-authentification n'en dépend. **Consigné ici comme note, pas ouvert en ticket** ;
  le prédicat neuf (`cDebugDomEnabled()`) est disponible si quelqu'un veut le garder.

- ⚠️ **[F-LOG-1, ce que rien n'épingle] Le garde peut être retiré sans qu'un cas bronche.** Jugé au
  merge et **accepté en l'état, avec une réserve** : le garde ne change **rien** d'observable —
  même journal, même wire, même octet — donc aucun oracle de sortie ne peut le voir, et un oracle
  de **coût** (temps ou pic mémoire) serait instable en intégration continue. Ce qui EST épinglé :
  `isLevelEnabled()` répond exactement ce que `~LogStream` imprime, à chaque niveau
  (`JsonApiRequestLog.TheDebugGuardAnswersWhatTheLogPrints`). ⭐ **Ce qui manque, et qui serait
  déterministe** : une **sonde statique** dans la famille de `tests/check-test-deps.sh` et de
  `check-pydeps-conformance.sh` — un script `dist_check_SCRIPTS` qui rougit si
  `dumpJsonRedacted(` apparaît sous un `cDebugDom` sans `cDebugDomEnabled` au-dessus. **Proposé,
  non écrit** : c'est un choix de convention de dépôt, pas une réparation.

- ⚠️ **[F-BACKUP-1] OUVERT — rien ne purge `<config>/backups`, et T3.64 n'a rien inventé.**
  Mesuré au source pendant [T3.64](T3.64.md) : aucun code de `src/` ne supprime quoi que ce soit
  sous `<config>/backups` — ni borne sur le nombre de dossiers, ni sur leur âge, ni sur
  `backups/corrupt/` (cette dernière limite était déjà consignée dans
  `docs/11_config_persistence.md`). Chaque `config put` y laisse trois fichiers pour toujours.
  T3.64 supprime la **collision** de nom, donc **il peut créer un dossier de plus** là où deux
  téléversements dans la même seconde n'en produisaient qu'un — au pire un par envoi scripté.
  ⛔ **La purge n'a délibérément pas été écrite dans T3.64** : borner un historique de
  sauvegardes est un choix d'exploitation (combien de générations, sur quel critère, et que fait
  le serveur quand le disque est plein), pas une réparation de défaut. **Ce qui manque, et qui
  serait mesurable** : une borne configurable et un cas qui prouve que la sauvegarde la plus
  ancienne encore présente reste **exploitable** après la purge — c'est-à-dire la même assertion
  de contenu que celle de T3.64, jouée après l'élagage.

## Lecture du micrologiciel `calaos_remote_ui` (2026-09-04)

Le dépôt de l'écran (`/home/raoul/repos/calaos/calaos_remote_ui`, HEAD `da80d09`) a été lu en
entier pour répondre aux questions qu'`E4.6`, `T3.62`, `T3.68` et `T3.69` avaient dû laisser
ouvertes faute de l'avoir. **Ces entrées remplacent des hypothèses par des mesures.**

### ✅ [F-RUI-1] Un écran dont l'économiseur n'est pas réglé perd TOUTE sa configuration — **FERMÉ par [T3.73](T3.73.md)**

Le serveur émet `screensaver_timeout` et `screensaver_dimming` en `get_param()` **brut**
(`RemoteUIWebSocketHandler.cpp:316-317`) : les clés sont **toujours présentes**, et **vides** quand
le param n'a jamais été réglé. Le micrologiciel fait `std::stoi()` dessus
(`main/calaos_websocket_manager.cpp:805,813`) ; `std::stoi("")` lève `invalid_argument`, l'exception
est attrapée en `:885`, et **la charge de configuration entière est jetée** — pages et widgets
compris.

⭐ **Atteignable aujourd'hui**, sans rien de spécial : il suffit qu'un écran n'ait pas ses params
d'économiseur. **C'est la réponse mesurée à la question que `T3.69` avait laissée en arbitrage** —
émettre un param présent-mais-vide n'est pas une préférence de forme, **ça casse l'appareil**.

**Même famille, autre chemin** : la géométrie d'un widget (`main/calaos_protocol.cpp:68-106`) fait
aussi `stoi("")`, et là ce n'est **pas** une `json::exception` — elle **échappe au `catch`** de
`:160` et remonte aux appelants (`calaos_page.cpp:47,391`) ⇒ **toutes les pages perdues**.

⇒ Ticket proposé **`T3.73`**. Le sens du correctif est désormais mesuré : **omettre** côté serveur,
ou envoyer une valeur que le micrologiciel sait lire.

✅ **FERMÉ par [T3.73](T3.73.md)** (branche `fix/t3.73`, non poussée). **Omettre** a été retenu :
c'est le seul des deux qui soit prouvé par la mesure — une clé absente retombe sur le défaut du
micrologiciel, une valeur « que le micrologiciel sait lire » demanderait de deviner laquelle.
⭐ **Le comptage a été refait au source, et il ne s'arrête pas aux deux clés qui plantent** :
**11 clés** de la charge étaient posées à partir d'un `get_param()` brut — `name` et les **8**
`screensaver_*` sur le push, `name`/`room`/`theme` sur la réponse. **Les deux constructeurs sont
corrigés**, bien que la réponse `remote_ui_config` soit un chemin mort côté appareil (`F-RUI-3`) :
elle est bâtie sur les mêmes params bruts. `RemoteUI::putIfSet()` porte la règle une seule fois.
⭐⭐ **L'observable épinglé est le CONTRAT, pas l'appareil** : « aucune valeur de chaîne de la charge
n'est vide », par un **balayage récursif** et non par une liste de clés — une contre-mutation qui
ajoute une clé neuve en `get_param()` brut le fait rougir sans que le test la nomme.

⭐⭐ **La réserve « rien ne prouve qu'un build antérieur ne réclame pas la clé » est LEVÉE au merge,
par l'historique du micrologiciel.** `handleConfigUpdate()` lit sa configuration en *pull* **depuis
le premier commit qui l'écrit** — `e813dfc`, **2025-12-11** : `configJson.value(clé, défaut)`
d'emblée. Les **14** révisions du fichier ont été relues une à une : **aucune** n'emploie un
`data["clé"]` nu sur un scalaire optionnel, et les seuls indexages directs du parse (`room`,
`pages`, `io_items`) sont **tous** gardés par un `contains()`. Les **18 étiquettes** du dépôt vont
de `waveshare-86-panel-0.0.1-dev.1` (**2026-02-10**) à `waveshare-touchlcd-*` (**2026-05-26**),
**toutes postérieures** à ce premier commit ⇒ **aucun binaire jamais étiqueté ne peut exiger la
clé**. Omettre est donc sûr pour un parc réel, et le correctif ne troque **pas** un défaut contre
un autre.

⚠️ **Ce que T3.73 laisse ouvert, et qui mérite un arbitrage** :
- **`pages` reste un passage à travers.** Un attribut de widget écrit vide dans `io.xml` traverse le
  serveur intact et atteindrait le `stoi` de la géométrie — celui qui **échappe au `catch`** et fait
  perdre **toutes les pages**. Le remède est côté lecture d'`io.xml` ⇒ famille de
  [T3.70](T3.70.md), qui n'a traité que `x`/`y`. **Aucun cas ne l'exerce aujourd'hui.**
  ⭐ **Confirmé au merge, et ça mérite un ticket ⇒ `T3.75` proposé.** Le mécanisme est **vérifié au
  source** du micrologiciel : `CalaosProtocol::PagesConfig::fromJson()` convertit `x`, `y`, `w`,
  `h`, `width` et `height` par `std::stoi()` — **six** sites — et sa **seule** clause de rattrapage
  est `catch (const json::exception &)`. `std::stoi("")` lève `std::invalid_argument`, qui **n'est
  pas** une `json::exception` : elle **sort de `fromJson()`**, la charge de pages est perdue en
  entier. C'est **exactement** le mécanisme fermé par `T3.70`, atteint par une **autre porte** :
  `T3.70` a gardé `x`/`y` à la **lecture d'`io.xml`**, mais `w`/`h`/`width`/`height` restent des
  chaînes recopiées telles quelles, et ce sont **elles** qui restent nues. Le finding est donc
  toujours vivant après `T3.73`.
- **La documentation du protocole décrit toujours `remote_ui_get_config` comme le chemin nominal**
  (`src/bin/calaos_server/RemoteUI/remote-ui.md`) alors qu'aucun micrologiciel ne l'emploie. La
  corriger — ou retirer le message du wire — est une décision d'exploitation, **pas faite**.
- ⛔ **Non prouvable sans matériel** : que l'écran physique s'affiche effectivement. Ce qui est
  épinglé est le contrat lu dans le source du micrologiciel.

### ✅ [F-RUI-2] Le défaut `brightness` du serveur contredit celui du micrologiciel — **FERMÉ par [T3.74](T3.74.md)**

`RemoteUI::getBrightness()` retourne **100** par défaut (`IO/RemoteUI/RemoteUI.cpp:582`). Le
micrologiciel porte **80** (`main/calaos_protocol.h:136`), et sa documentation aussi
(`doc/remote-ui.md:85-86,258-259,319-320`).

Avant [T3.68](T3.68.md), la charge n'était pas envoyée du tout à un écran non réglé, qui gardait
donc son 80. Depuis, il reçoit **100**. ⇒ **T3.68 a déplacé la luminosité effective de 80 à 100**
sur les écrans jamais réglés. Ticket proposé **`T3.74`**.

⭐ **En revanche `timeout = 30` est CORROBORÉ** par le micrologiciel (`calaos_protocol.h:137`) — ce
choix de T3.68 était juste. ⚠️ Nuance : côté écran, `timeout` est **parsé puis jamais utilisé**,
aucun consommateur.

✅ **FERMÉ par [T3.74](T3.74.md)** (branche `fix/t3.74`, non poussée). **Omettre** a été retenu, comme
pour les clés d'économiseur de [T3.73](T3.73.md), et le param n'était plus que le **dernier** de la
charge encore émis avec une valeur inventée.

⛔⭐ **DEUX AFFIRMATIONS DE CE FINDING ÉTAIENT FAUSSES, ET LA MESURE LES CORRIGE.**
(1) **Ce n'est pas T3.68 qui a mis 100 sur le wire vivant.**
`data["brightness"] = getBrightness()` est sur le **push** — le seul chemin qu'un écran écoute
(`F-RUI-3`) — depuis `350018ca` (**2025-12-23**, un commit amont), et `getBrightness()` y portait
`100` dès ce jour-là ; T3.68 n'a touché que la réponse `remote_ui_config`, c'est-à-dire le chemin
**mort**. ⭐ **Le ticket qui a fait ATTERRIR ce 100 est `T3.73`** : avant elle, un écran jamais
réglé jetait la charge entière (`F-RUI-1`) et gardait donc son 80. Le défaut est bien réel et bien
neuf pour l'utilisateur — son auteur n'est pas celui désigné ici.
(2) **`doc/remote-ui.md` du micrologiciel n'existe pas** : le dépôt de l'écran ne porte aucun `.md`
de protocole. Les `80` cités sont ceux de la spécification de **ce** dépôt
(`src/bin/calaos_server/RemoteUI/remote-ui.md:85`, `:546`, `:607`) — ce qui rend l'écart plus fort,
pas moins : le serveur contredisait **sa propre** documentation.

⭐ **Ce que la mesure a établi, et qui a tranché (A) contre (B) :** le parse de l'appareil est *pull*
(`data.value("brightness", 80)`, `main/calaos_websocket_manager.cpp:797`) **dans les 14 révisions**
du fichier, depuis le premier commit qui lit la clé (`e813dfc`, **2025-12-11**), **antérieur aux 18
étiquettes** du dépôt (2026-02-10 → 2026-05-26) ; **aucun autre consommateur** ne lit la clé, ni dans
`src/` ni côté appareil (`ScreenSaver::applyConfig()` lit le **champ** de la structure, déjà défauté
à 80, `main/screensaver.cpp:309,344`) ; et `getBrightness()` n'avait **que deux appelants**, les deux
émissions — il est **supprimé**, le `100` quitte l'arbre au lieu d'être déplacé.

⭐⭐ **Les deux corrections donnent le MÊME octet au rétroéclairage** (clé absente ⇒ 80 ; clé à 80
⇒ 80), et ce n'est pas une déduction : la contre-mutation **M2 EST la variante (A)**, et elle laisse
**verts** les deux cas qui portent le niveau appliqué. Ce qui les départage est donc ailleurs — (A)
recopierait dans ce dépôt une constante qui vit dans l'autre, et rejouerait ce même défaut, en
silence, au premier changement d'usine.

⛔ **Non prouvable sans matériel** : qu'un écran physique s'allume à 80 %. Ce qui est épinglé est la
charge émise d'un côté et le source qui la lit de l'autre.

🔒 **REVÉRIFIÉ À LA REVUE DE MERGE (`5d13eeca`) — les deux corrections d'archive sont exactes.**
(1) Le dépôt du micrologiciel ne porte **aucun** fichier `remote-ui.md`, à aucun chemin : la citation
`doc/remote-ui.md:85-86,258-259,319-320` de ce finding ne renvoyait à rien. Les trois `80` sont ceux
de `src/bin/calaos_server/RemoteUI/remote-ui.md` **de ce dépôt** (`:85`, `:546`, `:607` ; `:475` est
un écran réglé à 90) — relus un à un.
(2) `data["brightness"] = getBrightness()` est bien apparu sur le **push** avec `350018ca`
(2025-12-23) et n'y a jamais été retouché depuis ; T3.68 n'a touché que la réponse, le chemin mort.

⭐ **La branche coquille `brigtness` de l'appareil ne change pas la conclusion.**
`main/calaos_websocket_manager.cpp:794-797` essaie d'abord `data["brigtness"].get<int>()` — un
indexage **nu, sans défaut ni garde de type** — et ne retombe sur le *pull* que si la clé est absente.
Vérifié : `brigtness` n'a **jamais** été émis par `calaos_base`, dans aucune révision (`git log -S`
sur tout l'historique ⇒ 0 commit). La lecture passe donc toujours par
`data.value("brightness", 80)`, forme présente dans **14/14** révisions depuis `e813dfc`
(2025-12-11). ⚠️ À connaître tout de même : si un serveur émettait un jour cette clef **en chaîne**,
le `get<int>()` lèverait et emporterait la charge entière — le mode d'échec de `F-RUI-1`. Défaut
côté appareil, hors périmètre de ce dépôt.

⚠️ **Fausse assurance trouvée à la revue et FERMÉE (4ᵉ commit).** La suite livrée ne voyait qu'un
param `brightness` **absent** ou un `"55"` propre : la moitié « écrite et **illisible** » de la garde
n'atteignait aucune assertion, et une garde réduite à `!get_param("brightness").empty()` laissait la
suite **entièrement verte** (`CXXLD` des deux binaires lu). Or `" "`, `"abc"`, `"12abc"` et un
dépassement laissent tous un entier abandonné (0 ou 12) que cette garde-là aurait émis ⇒
**rétroéclairage à 0** pour trois des quatre, atteignable par un `brightness` écrit à la main dans
`io.xml`. `ABrightnessTheServerCannotReadLeavesNoKeyEither` balaie les quatre formes sur les **deux**
charges ; la mutation rejouée rougit ce cas, seul.

ℹ️ **Résiduel nommé et accepté** : `brightnessTheScreenApplies()` du test **recopie la constante 80
de l'appareil** — c'est précisément ce que le correctif refuse de faire dans `src/`. Le jour où le
micrologiciel choisit un autre niveau d'usine, `B1`/`B1bis` resteraient **vertes en étant périmées**.
C'est inhérent : cette constante ne vit pas dans ce dépôt. Le contrepoids est que la **politique**
(omettre) est tenue par `B2` et par `B5`, qui ne dépendent d'aucune valeur de l'autre dépôt. Et le
**type entier de la clé sur la réponse** n'est épinglé qu'indirectement (`value<int>()` lèverait) —
il l'est directement sur le push.

### [F-RUI-3] Le chemin `remote_ui_config` est mort côté appareil

`remote_ui_config` **n'a aucune branche** dans le dispatch de l'écran
(`main/calaos_websocket_manager.cpp:396-414` — seul `remote_ui_config_update` existe), et
`requestConfig()` (`:284`) **n'est appelé nulle part**. La réponse du serveur à
`remote_ui_get_config` (`RemoteUIWebSocketHandler.cpp:195`) tomberait donc dans
`"Unknown message type"`. **L'écran n'attend que le push.**

Le correctif de [T3.68](T3.68.md) reste juste — le `std::stoi` qu'il supprime sert les deux
chemins — mais **son cadrage était faux** : aucun écran ne demande sa configuration.

### ✅ [F-RUI-4] La géométrie d'un widget traverse le serveur sans garde — **FERMÉ par [T3.75](T3.75.md)**

`PagesConfig::fromJson()` (`main/calaos_protocol.cpp:65-109`) convertit `x`, `y`, `w`, `width`, `h`
et `height` par **six `std::stoi()`** sous un **unique `catch (const json::exception &)`**.
`std::stoi("")` lève `std::invalid_argument`, qui **n'est pas** une `json::exception` : elle sort de
`fromJson()` et emporte **toute** la charge de pages.

⚠️ **Précisé à la revue de merge de T3.75** : l'appareil ne tombe pas pour autant. Les deux appelants
de `getParsedPages()` attrapent en `catch (const std::exception &)` (`main/calaos_page.cpp:47` et
`:391`). À la **première** configuration (`:44`) aucune page n'est construite ⇒ écran **vide** ; à une
**mise à jour** (`:383`) le `destroyPages()` est **après** le parse ⇒ l'écran garde ses pages
**d'avant** et la nouvelle configuration est perdue **en silence**. Le défaut est réel des deux
côtés ; « toutes les pages perdues » décrit le premier chemin, pas le second.

Côté serveur, `RemoteUI::LoadFromXml()` recopiait `w`, `h`, `width` et `height` **en chaînes, sans
garde** — `x`/`y` étaient déjà gardés par [T3.70](T3.70.md), qui fermait un défaut **serveur** (le
`std::stoi` était alors *ici*) et avait explicitement laissé les quatre autres.

⭐ **Un champ, deux noms** : l'appareil lit `w` **ou**, à défaut, `width`, dans le **même**
`WidgetConfig::w` ; idem `h`/`height`. Le serveur ne produit que `w`/`h` (0 occurrence de
`width`/`height` dans `src/`), mais il **émet ce que l'`io.xml` porte** ⇒ l'alias est atteignable,
et une garde posée sur `w`/`h` seuls ne garde rien.

⭐ **Une taille absente reste légitime** : `w`/`h` ont toujours été facultatifs et l'appareil les
défaut à `1` depuis son premier commit. Seule une taille **écrite et illisible** écarte le widget.

⭐ **L'invariant de [T3.73](T3.73.md) en attrapait déjà la moitié** — sa réserve « `pages` reste un
passage à travers » était exacte, il ne manquait qu'une fixture qui en porte un. Il ne voit
cependant que les chaînes **vides** ; un `w="large"` plante l'écran sans être vide.

⚠️ **Et la revue de merge a dû fermer cette moitié-là POUR DE BON** : la fixture de charge livrée ne
déclarait qu'un `w=""`, donc sur le wire l'invariant neuf n'était discriminé que par le cas vide —
mesuré, une garde réduite à `!attr_value.empty()` rendait **1 rouge** et laissait
`core/JsonApiStateWireBytes_test` **vert**. Un widget `h="large"` a été ajouté à la fixture ⇒ **4
rouges** sur la même mutation. Voir [T3.75](T3.75.md), section de revue.

### ✅ Questions fermées par la mesure

- ⭐ **Une clé inconnue est IGNORÉE en silence.** Le parse est *pull-based* (`data.value(k, def)`,
  `data.contains(k)`), sans schéma ni balayage des clés restantes
  (`main/calaos_websocket_manager.cpp:775-889`). Les clés inconnues d'un widget sont même
  **délibérément collectées** (`main/calaos_protocol.cpp:111-133`). ⇒ **Le serveur peut ajouter un
  champ sans casser un écran déployé.**
- ⭐ **Aucune version de protocole côté appareil non plus.** Seul `APP_VERSION`
  (`main/version.h:7`), envoyé en en-tête `X-Device-Version` et dans le provisioning ; zéro
  `protocol_version`/`api_version`/`wire_version`. ⇒ **Confirmé des deux bouts : le serveur ne peut
  pas servir deux formes, tout changement de wire est global et irréversible.**
- **`status_info` et `var_type` ne sont JAMAIS lus** par l'écran. `state` l'est, mais seulement dans
  `remote_ui_io_states` / `io_state` / `event` ; dans les `io_items` de la configuration l'écran ne
  lit que `id`, `type`, `gui_type`, `name`, `visible`, `rw` et **force `state="false"`**
  (`:849-857`).
- **Ni `auto_scenario` ni `autoscenario_uid` ne sont lus** ⇒ le re-cléage de [T3.62](T3.62.md)
  **n'a rien retiré à l'écran**.

**Non déterminé** : si les écrans déployés tournent bien ce HEAD, et le comportement d'un build
antérieur.

### T3.33 — les deux sidecars, recensés et refermés ; ⛔ **le chiffre de la fiche est le bon, la conséquence ne l'est plus**

**Recensement fait au source sur `9356fe13`, pas repris de la fiche.**

| Fichier | Destinations `from_string` sans initialiseur | Fiche |
|---|---|---|
| `IO/Wago/WagoExternProc_main.cpp` | **12** (6 `address`, 4 `count`, 1 `value`, 1 `vv` de boucle) | 12 ✅ |
| `IO/KNX/KNXExternProc_main.cpp` | **6** (`a`,`b`,`c` × 2 fonctions) | 6 ✅ |

⭐ **CE QUE T3.25 A DÉJÀ FAIT ET QUE LA FICHE NE POUVAIT PAS SAVOIR.** Depuis T3.25,
`Utils::from_string()` **écrit sa destination sur TOUS les chemins** (`StringUtils.h`,
`dest = tmp;` après un `T tmp{}`). Les 18 destinations ne sont donc **plus indéterminées** sur
`master` : elles valent **0**. **Mesuré**, pas déduit — le commit de caractérisation sème `0x5555`
dans chaque destination et le motif **ne survit à aucun décodage** (`TheSeededPatternNeverReachesThePlc`,
`TheSeededComponentsNeverReachTheBus`). ⇒ **le titre « variables jamais écrites » n'est plus exact ;
le défaut qui reste est « valeur par défaut sur une adresse matérielle », et il est aussi grave.**

**Ce que faisait le sidecar d'un message malformé — MESURÉ, trois régimes distincts :**

1. **JSON illisible / racine non-objet** → `WagoWire::decodeMessage()` rend `false`, journal
   `cWarningDom("wago")`, `return`. **Déjà correct.** Idem KNX (`is_discarded()`, E4.1e).
2. **Action inconnue** → aucune branche, `res` vide, **aucune réponse du tout**. Silence délibéré,
   figé par `AnUnknownActionTouchesNothingAndAnswersNothing`.
3. ⭐ **Action connue, champ manquant / vide / illisible** → **rien ne le remarquait** : la trame
   partait sur le bus avec **0**, et le statut renvoyé était **`true`**. C'est le seul des trois
   régimes qui était faux.

**La convention des sidecars voisins, relevée avant de choisir** : `calaos_ola`
(`OLAExternProc_main.cpp` + `OLAWire.h`, E4.1f) journalise et **ignore l'entrée** — mais il
**zéro-initialise** son canal, ce qui est acceptable sur du DMX et ne l'est pas sur un registre
modbus ni sur un groupe KNX. `calaos_knx` et `calaos_wago` journalisent et `return` sur une trame
illisible. ⇒ **la convention « journaliser et ignorer » existe, elle est suivie ; le « zéro par
défaut » d'OLA est explicitement NON suivi** — l'adresse est refusée, jamais remplacée.

**Ce qui a été livré** : `WagoWire::decodeRequest()` (en-tête partagé, donc testable) décode et
contrôle les six actions ; le sidecar répond un statut en échec (`buildStatusReply`/`buildReadReply`
selon l'action) **et ne contacte pas l'automate**. Côté KNX les deux convertisseurs deviennent
`knxGroupAddrFromString()` / `knxPhysicalAddrFromString()`, libres et inline dans l'en-tête, qui
**laissent `out` intact** en cas de refus ; les `& 0x0F`/`& 0xFF` cessent d'être des gardes.

**F-T333-1 — ⛔ le masque n'était pas une garde, et il envoyait sur un groupe QUI EXISTE.**
`eKnxGroupAddr("1/2/300")` rendait `1/2/44` (`300 & 0xFF`). Ce n'est pas « une adresse invalide
ignorée », c'est **une écriture sur une autre adresse valide**. Figé par
`AnOutOfRangeGroupComponentIsRefusedInsteadOfMasked_DECLARED_DELTA`.

**F-T333-2 — `eKnxPhysicalAddr()` n'avait AUCUN appelant** dans l'arbre (mesuré). Conservé sous sa
forme neuve parce que le contrat est le même et qu'il est maintenant couvert ; à supprimer le jour
où quelqu'un décide que le mode moniteur n'en aura jamais besoin.

**F-T333-3 — ⚠️ la forme « entier 16 bits » que `calaos_knx --help` annonce n'a jamais existé.**
`KNXExternProc_cli.cpp` (doRead) faisait `knx_addr = knx_addr & 0xffff;` — une affectation de
`knx_addr` à lui-même, alors qu'il vaut 0. Une adresse de groupe donnée en entier partait donc sur
**0/0/0**. ⛔ **NON corrigé ici** : c'est un chemin **CLI**, il faudrait décider si on implémente la
forme annoncée ou si on retire la phrase du `--help`. La ligne morte est supprimée et le chemin
refuse désormais une adresse `x/y/z` malformée ; une adresse **sans `/`** reste traitée comme
avant (`knx_addr` = 0). **Dit ici plutôt que corrigé en passant.**

**F-T333-4 — la suite `WagoWire_test` n'a pas 31 cas mais 35 sur `master`** (E4.1h en avait écrit
31, T3.31/T3.46/T3.50 ont ajouté les sondes de typage). `KNXExternProcWire_test` : **18**, conforme
à la fiche. Après T3.33 : **52** et **27**.

**Contre-mutations, dix, ensembles rouges deux à deux distincts** (`WagoWire_test` et
`KNXExternProcWire_test` **relinkés** — ligne `CXXLD` exigée à chaque cycle, restauration par
`cp` + `cmp`, jamais `git checkout` dans le conteneur) :

| # | Mutation | Rouge |
|---|---|---|
| M1 | garde retirée, `ReadBits` | `AReadBitsRequestWithNoAddressIsRefused` |
| M2 | garde retirée, `ReadWords` | `AReadWordsRequestWithAnEmptyAddressIsRefused` |
| M3 | garde retirée, `WriteBit` | `AWriteBitRequestWithNoAddressIsRefused` |
| M4 | garde retirée, `WriteBits` | `AWriteBitsRequestWithNoCountIsRefused` |
| M5 | garde retirée, `WriteWord` | `AWriteWordRequestWithAnUnreadableValueIsRefused` |
| M6 | garde retirée, entrée de `WriteWords` | `AWriteWordsRequestWithAnUnreadableEntryIsRefused` |
| M7 | **échange** `address` ↔ `count` au décodage | 9 cas, dont **les 7 cas d'acquis** |
| M8 | contrôle de plage KNX remplacé par les masques d'origine | `AnOutOfRangeGroupComponentIsRefusedInsteadOfMasked` |
| M9 | retours de `from_string` ignorés, groupe | 3 cas |
| M10 | retours de `from_string` ignorés, physique | `APhysicalAddressWithOneComponentIsRefused` |
| témoin | — | **`TheSeededPatternNeverReachesThePlc` et `TheSeededComponentsNeverReachTheBus` verts sur les 10 cycles**, binaires relinkés à chaque fois |

⚠️ **Le témoin a dû être RÉÉCRIT entre les deux commits, et c'est un piège à consigner** : « le
motif semé n'atteint jamais le bus » devenait **vacuant** après le correctif (rien n'est décodé, le
motif reste en place). Il asserte maintenant les **deux** régimes — `if (reached) motif absent;
else motif intact` — donc quelque chose de vrai et de non vacuant avant **et** après.

## T3.28a — le blanc qui coupe un argv, et la classe qui reste ouverte (2026-09-04)

Recensement fait **au source sur `397e5b7a`**, pas repris de la fiche.

**⭐ Ce que la mesure a corrigé dans l'énoncé de départ.** La fiche T3.28a — et le commentaire de
`RoonArgs.h` lui-même — disaient « `startProcess()` redécoupe sur **l'espace** » d'un côté et
« splits this string on **whitespace** » de l'autre. Les deux ne peuvent pas être vrais.
`Utils::CStrArray(const string &)` appelle `Utils::split(s, v, " ")`, et `Utils::split()` atteint
ses délimiteurs par **`find_first_of` / `find_first_not_of`** : le troisième argument est un
**ensemble de caractères** à **un seul membre**, `U+0020`. ⇒ **tabulation, saut de ligne, retour
chariot, tabulation verticale et saut de page ne coupent rien** — ils restent dans leur argument
jusqu'à `execv`, et aucun shell ne les relit (`uv_spawn`, pas `system()`). La garde livrée porte
donc sur `{ ' ' }` seul, et le contrat du découpeur est **épinglé** plutôt que supposé
(`TheSidecarCommandLineIsCutOnTheSpaceAndOnNoOtherBlank`).

**⚠️ Mesuré par contre-mutation, et pas cherché** : échanger le délimiteur de `CStrArray` pour
`" \t\n\r\v\f"` — une classe de `src/lib` que **tout** l'arbre traverse — ne fait rougir que
**`core/RoonArgs_test`**. **Aucune autre suite de l'arbre n'épingle sur quoi `CStrArray` découpe.**

### Les appelants de `startProcess()` — 16 sites, 8 fichiers (recomptés à l'identique par [T3.78](T3.78.md), verdict site par site dans sa fiche §2)

**13 sites sur 16, dans 6 fichiers, portent un argument venant de la configuration** :
`WagoMap.cpp` (2, `get_param("host")` + port) · `MqttCtrl.cpp` (2, un JSON de `host`, `port`,
`keepalive`, `user`, `password`) · `KNXCtrl.cpp` (4, `get_param("host")`) · `OLACtrl.cpp` (2,
`get_param("universe")`) · `OWCtrl.cpp` (2, `get_param("ow_args")`) · `RoonPlayer.cpp` (1, **le
seul gardé**). **3 sites sur 16, dans 2 fichiers, n'en portent aucun** : `ReolinkCtrl.cpp` (2) et
`ScriptExec.cpp` (1). Les « six sidecars » de la fiche sont donc exactement `calaos_wago`,
`calaos_mqtt`, `calaos_knx`, `calaos_ola`, `calaos_1wire`, `calaos_roon`.

### ✅ [F-EXTPROC-1] Le même défaut sur un **mot de passe MQTT** — pire que celui de Roon — **FERMÉ par [T3.78](T3.78.md)**

`MqttWire::encodeConfig()` sérialise `host`, `port`, `keepalive`, `user` et `password` en **un
seul** JSON compact passé comme **un seul** argument ; `MqttExternProc_main.cpp:171` exige
**`argc == 2`** et lit `argv[1]`. Un espace dans n'importe laquelle de ces cinq valeurs coupe le
JSON en deux argv ⇒ `"Unable to read configuration"`, sortie, **même boucle de relance à 100 ms**.

**Pourquoi c'est plus grave que le cas Roon** : un mot de passe contenant un espace est un **usage
normal**, pas une faute de frappe. Et la parade de T3.28a — refuser le champ — serait ici **pire
que le défaut** : on refuserait une configuration légitime. **Seule la correction (3)
(`vector<string>` dans `ExternProcServer`) ferme ce cas.**

✅ **FERMÉ par [T3.78](T3.78.md).** `startProcess()` prend un `vector<string>` et l'ancienne surface
a disparu : une `std::string` ne s'y convertit pas, donc tout appelant de la forme d'avant est une
**erreur de compilation**. Mesuré **à travers un vrai `MqttCtrl`**, pas par appel direct : un mot de
passe `mon mot de passe` donnait **argv 9** (⇒ `argc 5`) et donne **argv 6** (⇒ `argc 2`, ce
qu'exige `MqttExternProc_main.cpp:171`), le JSON comparé **octet pour octet** à `encodeConfig()` sur
**chaque** lancement, respawn compris — `core/ExternProcArgv_test.
AMqttPasswordCarryingSpacesReachesTheSidecarWhole`. Une mutation posée **au site expédié**
(`MqttCtrl.cpp`, l'argument redécoupé) le fait rougir.

### ✅ [F-EXTPROC-2] `ow_args` rend la correction (3) NON mécanique — **TRAITÉ par [T3.78](T3.78.md)**

Le paramètre `ow_args` de `OWTemp` est **documenté** comme une liste d'arguments owfs
(*« Additional parameter used for owfs initialization. For example you can use -u… »*) et
`OWTemp.cpp:52-55` y préfixe `"--use-w1 "` **avec l'espace**. Le découpage sur l'espace y est
**porteur**. ⇒ (3) ne peut pas être un `vector<string>{args}` appliqué en bloc : ce champ demande
son propre arbitrage (garder un découpage explicite, ou basculer son ioDoc sur une liste). C'est
le vrai coût du ticket (3), et il ne se voit pas dans le compte des appelants.

✅ **TRAITÉ par [T3.78](T3.78.md) : arbitrage retenu = GARDER LE DÉCOUPAGE, et le rendre EXPLICITE au
site.** `OwCtrl` appelle `Utils::split(args, procArgs, " ")` lui-même, là où l'intention se lit, au
lieu de la subir dans le transport ; l'ioDoc n'est pas touché. **Non-régression prouvée trois fois** :
(a) analytiquement — `split()` saute les délimiteurs de tête, regroupe les runs et ne produit jamais
de jeton vide, donc découper la concaténation le long d'un délimiteur **est** la concaténation des
découpages ; (b) par mesure — le corps **verbatim** de `Utils::split()` compilé hors des tests,
**13 entrées** (chaîne vide, blancs seuls, runs, espace finale, tabulation, saut de ligne) ⇒
**0 différence** ; (c) au site de production — un vrai `OwCtrl` dont l'argv est relu au noyau
(`TheOneWireArgumentListStillReachesTheSidecarAsSeparateArguments`, **vert des deux côtés du
correctif** : c'est le témoin de non-régression). Le port mécanique que ce finding annonçait
(`vector<string>{args}`) a été **rejoué en contre-mutation M2** : **1 rouge**, ce cas-là.

### Ce qu'un passage en `vector<string>` toucherait

**14 fichiers** : `IO/ExternProc.h` + `IO/ExternProc.cpp` (la concaténation et
`Utils::CStrArray arr(cmd)` cèdent la place au constructeur `CStrArray(vector<string>)` **qui
existe déjà**, `StringUtils.cpp:385`) · les **8** appelants · les **2** assembleurs qui rendent
aujourd'hui une chaîne (`Audio/RoonArgs.h`, `IO/Mqtt/MqttWire.h`) · **2** fichiers de test
(`tests/core/IoLifetimeTimer_test.cpp` `:1096` et `:1125` appellent `startProcess()` directement ;
`tests/core/RoonArgs_test.cpp` porte une tripwire source qui épingle **l'épellation littérale**
`process->startProcess(exe, "roon", procArgs);` et rougira sur toute nouvelle signature).
**Aucun** des six `*_main` de sidecar n'est touché : l'argv qu'ils reçoivent est identique, c'est
le chemin qui l'amène qui cesse de le recomposer.

### ⭐ Ce que la **revue de merge** a mesuré elle-même (2026-09-04)

**`F-EXTPROC-1` est EXACT — re-vérifié aux sources, pas repris de la fiche.** `MqttWire.h:221`
(`encodeConfig()`) met `user` et `password` dans **un seul** JSON ; `MqttCtrl.cpp:31` et `:63` le
passent en **un seul** `args` ; `ExternProc.cpp:184` le concatène et `:283` le redécoupe par
`Utils::CStrArray` ; `MqttExternProc_main.cpp:171` exige **`argc == 2`** — et c'est bien 2, la
classe de base ayant retiré `--socket` et `--namespace` (`ExternProc.cpp:429-436`, `argc -= 2`
deux fois). Un espace dans le mot de passe donne donc `argc == 3` ⇒ `"Unable to read
configuration"` ⇒ sortie ⇒ **relance à 100 ms sans fin**. La conclusion « refuser le champ serait
pire que le défaut » **tient** : l'espace y est un usage normal.

⚠️ **Une trouvaille de plus, faite en vérifiant celle-ci** : `MqttExternProc_main.cpp:187-189`
journalise `argv[1]` — **le JSON de configuration en clair, mot de passe compris** — sous
`cDebugDom("mqtt")`. Le même `argv[1]` repart en clair dans le message d'erreur de `:183`. À traiter
avec la correction (3), ou avant.

**`F-EXTPROC-2` est EXACT.** `OWTemp.cpp:41-42` documente `ow_args` comme *« Additional parameter
used for owfs initialization. For example you can use -u »*, et `:53-55` y préfixe `"--use-w1 "`
**espace compris**. Le découpage y est **porteur**, donc (3) ne peut pas envelopper `args` dans un
`vector<string>` d'un seul élément sans casser ce champ.

### ⛔ [F-EXTPROC-3] Le mot de passe MQTT part **en clair dans le journal du serveur** — ticket `T3.79`

La revue de merge de T3.28a avait relevé `MqttExternProc_main.cpp:183` et `:187-189` (le sidecar
journalise `argv[1]`). ⭐ **Le site qui compte est ailleurs, et il est plus grave** :
`IO/ExternProc.cpp:286`, `cInfoDom("process") << "Starting process: " << arr.toString()` — dans le
**serveur**, **générique** (tout argument de tout sidecar), et au niveau **INFO** alors que le défaut
de `debug_level` est **4 = INFO** (`src/lib/ConfigOptions.cpp:602-608`, `.def("4")`). Sur une
installation de série, personne n'ayant rien allumé, **chaque lancement et chaque relance du sidecar
MQTT écrivent le mot de passe du courtier**. Relevé **verbatim** dans la sortie de
`core/ExternProcArgv_test` en écrivant [T3.78](T3.78.md), pas déduit.

⚠️ **Quatrième exposition, hors journal** : la configuration étant un argument de ligne de commande,
elle est lisible dans `/proc/<pid>/cmdline` par tout compte de la machine, pour la vie du sidecar.
Seule la sortir de l'argv (socket ou environnement) la fermerait — changement de protocole des
sidecars, **arbitrage utilisateur**. ⇒ **[`T3.79`](T3.79.md)**, fiché, **non corrigé**.

### ✅ [F-EXTPROC-4] Quatre appelants de `startProcess()` ne sont épinglés par RIEN — **FERMÉ par [T3.80](T3.80.md)**

Contre-mutation **M6** de [T3.78](T3.78.md), au site OLA : l'univers vide devient un `argv[1]` vide
que `from_string("")` lit ⇒ **0 rouge**, `TOTAL 119 / PASS 118 / FAIL 0`, avec
`CXXLD    calaos_server` et `CXXLD    core/ExternProcArgv_test` **lus** — le vert n'est pas un défaut
de relink. ⇒ **rien dans l'arbre n'observe l'argv d'`OLACtrl`, `WagoMap`, `KNXCtrl` ni
`ReolinkCtrl`.** Leur conversion en `vector<string>` a été faite **au raisonnement** et relue contre
le `*_main` correspondant, pas exercée. ⚠️ `KNXCtrl` **est** construit pour de vrai par
`core/KnxIo_test`, qui ne lit simplement pas l'argv. Le harnais nécessaire existe désormais
(`tests/core/ExternProcSpawnHarness.h`, un journal d'argv par nom de sidecar).
⇒ **ticket proposé [`T3.80`](T3.80.md)**.

⭐ **CONFIRMÉ par la revue de merge de T3.78 (2026-09-05), sur DEUX des quatre au lieu d'un, et par
une preuve statique sur les deux autres.** Deux contre-mutations indépendantes, restaurations au
`cmp` (rc 0), `CXXLD` **lus** : **R1** — les deux positionnels de `WagoMap.cpp` **permutés**, donc
le port là où `WagoExternProc_main.cpp:240` lit l'hôte ⇒ **0 rouge**, avec `CXXLD calaos_server`,
`core/WagoPortDefault_test`, `core/WagoReadReply_test` et `core/WagoUdpReply_test` **lus** · **R2** —
drapeau et valeur KNX **recollés** aux **quatre** sites, donc un `--server` que `argvOptionParam()`
(qui compare un argv **entier**) ne trouve plus ⇒ **0 rouge**, avec `CXX IO/KNX/KNXCtrl.o`,
`CXXLD calaos_server`, `KNXCtrlWire_test`, `core/KnxIo_test` et `core/IoLifetimeTimer_test` **lus**.
⛔ **Et pour OLA et Reolink le constat est STATIQUE, donc définitif** : `tests/Makefile.am` ne relie
`IO/OLA/OLACtrl.$(OBJEXT)` ni `IO/Reolink/ReolinkCtrl.$(OBJEXT)` à **aucun** binaire de test —
aucune mutation ne pourra jamais y rougir quoi que ce soit. Les six suites qui relient `WagoMap.o`
ou `KNXCtrl.o` ne mentionnent ni `process_args`, ni `startProcess`, ni un argv (balayage complet).

⚠️ **Corollaire noté au passage, non corrigé** : un `host` Wago **vide** faisait avaler le champ par
le redécoupage et le **port** atterrissait dans `argv[1]`, là où `WagoExternProc_main.cpp:240` lit
l'hôte. T3.78 **reproduit** ce comportement plutôt que de le changer en silence : c'est un défaut de
configuration vide, pas de transport.

✅ **FERMÉ par [T3.80](T3.80.md)** : `core/SidecarArgv_test` construit un vrai `WagoMap`, un vrai
`KNXCtrl`, un vrai `OLACtrl` et un vrai `ReolinkCtrl`, laisse tourner leur relance et relit l'argv
que **le noyau** a remis à l'enfant. `tests/Makefile.am` relie enfin `IO/OLA/OLACtrl.$(OBJEXT)` et
`IO/Reolink/ReolinkCtrl.$(OBJEXT)`, que **rien** ne reliait — c'est la moitié structurelle du
finding, et elle ne pouvait se fermer que là. ⭐ **Les quatre mesures à 0 rouge ont été rejouées et
rougissent toutes** :

| Contre-mutation, **par échange** | Avant | Après |
|---|---|---|
| **R1** positionnels Wago **permutés** | 0 rouge | **1 rouge** |
| **R2** drapeau et valeur KNX **recollés** aux 4 sites | 0 rouge | **2 rouges** |
| **R3** garde OLA **inversée** (M6 et davantage) | 0 rouge | **2 rouges** |
| **R4** espace de noms Reolink **échangé** dans les arguments, au site de relance | 0 rouge | **1 rouge** |

Plus **R5** (garde Wago **inversée**) : **2 rouges**, ensemble **distinct** de R1. Les cinq
ensembles rouges sont deux à deux distincts, restaurations **prouvées au `cmp` (rc 0 × 6)**, témoin
vert **`TOTAL 121 / PASS 120 / SKIP 1 / FAIL 0 / ERROR 0`** avec les `CXXLD` **lus**.
⛔ **Le contraste est le résultat** : sous R1 les trois binaires Wago préexistants ont relinké
(`CXXLD` lus) et sont restés **verts**, et sous R2 les trois binaires KNX aussi. Les six suites qui
construisent ces contrôleurs pour de vrai ne regardent toujours pas leur argv.
⚠️ **Ce que le filet ne dit toujours pas** : que les sidecars **font** quelque chose de correct avec
cet argv. Les enregistreurs sont des bouchons — aucun bus KNX, aucun automate, aucune caméra, aucun
`olad`.

### ⛔ [F-EXTPROC-8] Le sidecar **moniteur KNX** est lancé sous l'espace de noms `knx` — ticket proposé [`T3.84`](T3.84.md)

`KNXCtrl` tient deux `ExternProcServer` — `new ExternProcServer("knx")` et
`new ExternProcServer("knx_monitor")` — et lance **les deux** avec le **même** nom : les quatre
`startProcess(exe, "knx", …)` de `IO/KNX/KNXCtrl.cpp`. ⭐ **Seul écart de l'arbre**, recompté sur les
**16** sites des 8 fichiers appelants : partout ailleurs le préfixe de l'`ExternProcServer` et le nom
passé à `startProcess()` sont le même mot.

⛔ **Rien de fonctionnel** : la socket vient de `--socket` et les deux sockets diffèrent bien (le
préfixe `knx_monitor` est dans `sockpath`). Dans `ExternProcClient`, `name` ne sert qu'à
`initLogger(name.c_str())`.
⚠️ **Ce qui est perdu est un diagnostic, et un seul** : les deux sidecars KNX appellent
`initLogger("knx")`, donc ils journalisent sous le même domaine, `CALAOS_LOG_DOMAINS` ne peut pas
isoler le moniteur, et leur stdout est réinjecté dans celui du serveur.

⛔⭐ **Corrigé à la revue de merge de [T3.80](T3.80.md) : « une boucle de relance est indiscernable »
était FAUX.** Mesuré dans le journal du binaire neuf, **trois** champs séparent déjà les deux
sidecars — le `--socket` de la ligne réduite porte le préfixe `knx_monitor`, le compte d'arguments
vaut **2** pour la commande et **3** pour le moniteur, et les deux avertissements de relance
côté serveur sont deux textes distincts. Le finding reste réel, il est **mineur** : ce qui manque
est un domaine de journal propre, pas la lisibilité d'une relance.

Trouvé en écrivant [T3.80](T3.80.md), **fiché et non corrigé** : T3.80 est un ticket de
caractérisation. ⚠️ `core/SidecarArgv_test` vérifie `argv[4]` sur **chaque** lancement, donc la
correction **fera rougir** la suite — c'est la trace attendue de la décision.

### ✅ [F-EXTPROC-5] Le lancement d'un sidecar journalisait l'**argv entier**, mot de passe compris — **FERMÉ par [T3.79](T3.79.md)**

`ExternProcServer::startProcess()` écrivait `cInfoDom("process") << "Starting process: " <<
arr.toString()`, c'est-à-dire **tout l'argv recollé par des espaces**. `debug_level` vaut **4** par
défaut (`src/lib/ConfigOptions.cpp:608`), `LOG_LEVEL_INFO` vaut **4** et le filtre est
`level > maxLevelPrintable(domain)` (`src/lib/Logger.cpp:164`) ⇒ **INFO passe sur une installation
de série, personne n'ayant rien allumé**. `MqttWire::encodeConfig()` mettant `user` et `password`
dans l'argument du sidecar MQTT (`MqttWire.h:231-235`), **le mot de passe du courtier partait en
clair à chaque lancement et à chaque relance** — et sept contrôleurs sur huit relancent sans backoff
(E4.5d), donc ~10 fois par seconde sur un courtier injoignable.

⭐ **Le recensement qui a tranché l'arbitrage** : sur les sept contrôleurs, **un seul** porte un
secret dans son argv. Wago (`host`/`port`), KNX (`--server ip:…`), OLA (l'univers), OneWire (les
arguments owfs), Roon (`--host`/`--port`) et Reolink (**aucun argument**) n'en portent pas ; le
jeton Roon vit dans un fichier de cache (`Audio/ExternProcRoon_main.py:96,126`) et les identifiants
Reolink passent par la socket (`IO/Reolink/ReolinkWire.h:85`). ⇒ **une liste de champs à caviarder
vaudrait UN nom et serait fausse, silencieusement, au premier driver qui en ajoute un** — le
transport ne voit pas des champs, il voit des `std::string`.

✅ **FERMÉ par [T3.79](T3.79.md)** : la ligne publie désormais le binaire, la socket, l'espace de
noms et le **nombre** d'arguments — on énumère ce qu'on publie au lieu de deviner ce qu'on cache.
⛔ **Descendre la ligne en DEBUG a été rejeté ET mesuré** : `CALAOS_LOG_LEVEL` est propagé au sidecar
et le stdout de l'enfant est réinjecté dans celui du serveur, donc un seul geste ouvrirait les
**trois** sites dans le même journal ; et la mutation `cInfoDom` ↔ `cDebugDom` donne **2 rouges**,
dont la mort du diagnostic. Les deux sites du sidecar `calaos_mqtt` (`:183` à **ERROR**, sur le
chemin d'échec, et `:187-189` à DEBUG) cessent de streamer `argv[1]`.
⚠️ **Ce qui reste ouvert, et c'est `F-EXTPROC-7`** : `/proc/<pid>/cmdline` publie toujours l'argv
complet, et la revue de merge l'a **mesuré** — mode **444**, relu **verbatim** par un compte tiers.
Ce finding est **fermé pour les journaux, pas pour l'argv**.

### ✅ [F-EXTPROC-7] FERMÉ par [`T3.82`](T3.82.md) — le secret du courtier était dans l'**argv**, donc lisible par **tout compte** du boîtier

`MqttWire::encodeConfig()` met `user` et `password` dans l'argument remis au noyau par
`startProcess()`. [T3.79](T3.79.md) a fermé la fuite **par les journaux** ; celle-ci survit intacte.

⭐ **Mesuré à la revue de merge de T3.79, et l'énoncé courant le sous-estimait.** Dans l'image de
développement, `/proc` monté **sans `hidepid`** :

| Fichier | Mode | Lu par un compte tiers ? |
|---|---|---|
| `/proc/<pid>/cmdline` | **444** | ⛔ **OUI**, argv relu **verbatim**, mot de passe compris |
| `/proc/<pid>/environ` | **400** | non |

Ce n'est donc pas « lisible par le même utilisateur » : c'est **lisible par n'importe quel compte de
la machine**, pour toute la vie du sidecar, et un `ps` suffit. ⇒ l'argv ne protège de personne ·
l'environnement protège d'un autre compte, ni du même UID ni de `root` · la socket ne laisse rien
dans `/proc`. ⭐ **La forme existe déjà dans l'arbre** : Reolink passe ses identifiants caméra par
la socket (`IO/Reolink/ReolinkWire.h:85`).

⛔ **Et c'est la seule fermeture possible d'une classe entière** : la revue a mesuré **deux chemins
d'erreur à découvert, 0 rouge chacun** — le site 2 de `MqttExternProc_main.cpp` (**ERROR**, imprimé
par défaut, chemin d'échec, réinjecté dans le journal du serveur) se rouvre par une simple
**reformulation** (`const char *cfg = argv[1];`) que le tripwire `<< argv[1]` ne voit pas, et le
`once<uvw::ErrorEvent>` de `startProcess()` peut republier la ligne de commande entière à
**CRITICAL**. Tant que le secret est dans l'argv, tout site qui touche l'argv est un site de fuite,
en nombre non borné, et **aucun tripwire supplémentaire ne les couvre tous**.

⭐ **Périmètre borné** : sur les sept contrôleurs, **MQTT seul** porte un secret dans son argv
(recensement de [T3.79](T3.79.md) §2, **recompté à la revue, aucun écart**), donc le changement de
protocole se limite à `calaos_mqtt`. ⚠️ **Arbitrage utilisateur requis.**

✅ **Fermé le 2026-09-05, par la socket** (arbitrage utilisateur, [`DECISIONS.md`](DECISIONS.md)). Le
sidecar se connecte, le serveur lui écrit sa configuration **en premier message** — la forme de
`ReolinkWire::buildRegisterMessage()`, reprise et non inventée, avec une clef `action` en plus parce
que MQTT a **deux** types de message serveur → sidecar. **Recensement recompté sur l'arbre livré :
plus aucun sidecar ne porte un secret dans son argv.** Ce qui reste au lancement est le binaire, la
socket et l'espace de noms, **dont aucun ne vient d'`io.xml`**.

⭐ **Et la classe est fermée par CONSTRUCTION, pas par des lignes** : les deux chemins d'erreur que la
revue de `T3.79` avait mesurés à **0 rouge** — la reformulation `const char *cfg = argv[1];` et le
`once<uvw::ErrorEvent>` republiant la ligne de commande à CRITICAL — n'ont plus de secret à publier.
Le tripwire de source passe de « le sidecar ne streame pas `argv[1]` » à « **il ne lit aucun
argument** ».

⚠️ **Le prix, mesuré et assumé** : le contrat du sidecar change **des deux côtés** et le chemin
d'échec « configuration illisible » quitte le contrôle d'`argc` pour une attente bornée à 5 s. Les
trois chemins d'échec de cette attente sont mesurés sur le binaire livré ([`T3.82`](T3.82.md) §4) :
**jamais** et **malformée** terminent le processus, donc bouclent par la relance sans backoff
(E4.5d) — mais le premier à **une relance toutes les 5 s** et chaque tour imprime sa cause ;
**deux fois** ne boucle pas. Compatibilité mesurée dans **les deux sens** : chaque direction échoue
bruyamment, aucune ne dégrade en silence, aucune ne publie le secret.

⭐⭐ **CONFIRMÉ À LA REVUE DE MERGE, sur le binaire livré et hors des suites** (serveur bouchonné écrit
pour la revue, cadrage à la main) : recensement des **16** sites de `startProcess()` recompté, aucun
écart — Wago passe `host`/`port`, KNX `--server ip:<host>`, OLA l'univers, OneWire `ow_args`, Roon
`--host`/`--port`, Reolink et Lua rien ; **aucun secret nulle part**. Argv de `calaos_mqtt` relu :
binaire, `--socket`, `--namespace`, **zéro argument**, aucun des trois ne venant d'`io.xml`.
⭐ **Et le refus du cas « serveur ancien » est plus fort que la fiche ne le dit** : il tombe **avant
`connectSocket()`**, donc le sidecar ne lit ni ne publie la configuration qu'il a pourtant dans son
argv. ⛔ **Le motif écrit pour la clef `action` était faux et a été corrigé** : `resolveBroker()`
matérialise toujours les trois défauts, donc une configuration « toute par défaut » porte quand même
ses trois clefs ; le cas atteignable est **l'inverse** — sans `action`, une publication arrivée la
première serait appliquée **comme une configuration sur ses défauts**, en silence. La clef reste
nécessaire, et son usage **côté sidecar** est désormais tenu par un cas (il ne l'était par rien :
mesuré **0 rouge** en la retirant).

### ✅ [F-EXTPROC-9] FERMÉ par [`T3.103`](T3.103.md) — `calaos_mqtt` sortait avec le code **0 et sans une ligne** quand la connexion au courtier échouait en asynchrone

Mesuré en écrivant [`T3.82`](T3.82.md), sur le binaire livré, contre un serveur bouchonné.

`MqttProcess` enregistre le descripteur de mosquitto par `appendFd()` et ne le retire jamais. Quand
la connexion TCP au courtier échoue **après** que `connect_async()` a répondu `MOSQ_ERR_SUCCESS` —
c'est-à-dire pour tout hôte injoignable, le cas courant d'un courtier éteint — la bibliothèque ferme
ce descripteur, `select()` de `ExternProcClient::run()` rend **`EBADF`**, la boucle `break`, et
`procMain()` répond **0**.

⛔ **Le serveur relance alors le sidecar sans backoff (E4.5d) et RIEN n'est écrit** : ni le sidecar,
qui n'a pas d'erreur à publier, ni le serveur, dont `processExited` ne regarde pas le statut. C'est
une boucle de relance **muette**, la pire forme du trou d'E4.5d.
**Mesuré** : `EXIT=0` sur `192.0.2.42:1883` (TEST-NET-1), une seule ligne de journal en tout,
`Connect to : 192.0.2.42socket 6`.

⚠️ **Même site, second défaut** : le diagnostic de l'autre branche appelle `strerror()` sur un **code
`MOSQ_ERR_*`**, jamais sur un `errno`. Un port refusé donne `Error connecting : Bad address`
(`MOSQ_ERR_ERRNO` vaut 14, et `strerror(14)` est `EFAULT`).

⭐ **Les deux sont sur `master` et [`T3.82`](T3.82.md) ne les touche pas** : ils sont dans le `switch`
que ce ticket a déplacé sans le modifier, et ils sont antérieurs à la série.

⭐ **Les deux re-mesurés à la revue de merge, verbatim** : `EXIT=0` après **23 ms** sur
`192.0.2.42:1883`, journal du sidecar réduit à `Connect to : 192.0.2.42socket 6` ; et
`Error connecting : Bad address` sur un port refusé.
⛔ **Une précision, et elle change le mot** : « boucle **muette** » vaut pour le **sidecar**, pas pour
le couple. `MqttCtrl` imprime `process exited, restarting...` à **WARNING** à chaque tour, donc à
la cadence de 100 ms de la relance sans backoff : ce n'est pas du silence, c'est **du bruit sans
cause**, dix lignes par seconde qui ne disent jamais pourquoi. Pas meilleur, différent — et c'est
la forme que le ticket devra traiter.

✅ **Fermé le 2026-09-05.** ⭐ **Un TROISIÈME cas, absent de la fiche, a été mesuré et il se comporte
comme le premier** : le courtier qui **tombe en cours de session** (CONNACK rendu, puis la connexion
lâchée) — `EXIT=0` à 704 ms, une seule ligne. C'est le cas fréquent en production. Les trois sont
désormais **non nuls et nommés** : `Network is unreachable (errno 101)` · `Connection refused
(errno 111)` · `The connection was lost.`

⭐ **Le descripteur est rendu ET `EBADF` cesse d'être une fin normale, et les deux ont été mesurés
seuls** : rendre le descripteur sans plus donne un sidecar qui **tourne à vide pour toujours** — plus
silencieux et strictement pire, puisque rien ne reconnecte côté sidecar ; ne refuser qu'`EBADF`
nomme `Bad file descriptor`, c'est-à-dire le symptôme de la comptabilité du sidecar, et laisse un
descripteur fermé dans le `select()` qu'un `open()` ultérieur peut se voir réattribuer.
⚠️ **Mesuré aussi** : sur les trois cas c'est **toujours** le code de retour de `mosquitto_loop()`
qui mord, jamais `EBADF` — le filet vaut pour la classe, pas pour ces cas-là. Et `EINTR` cessait la
boucle des **sept** familles de sidecars avec un statut 0.

⚠️ **Le second défaut est plus précis que « la mauvaise table »** : `mosqpp::strerror(MOSQ_ERR_ERRNO)`
rend **déjà** `strerror(errno)`. La faute était **l'argument** — `strerror(res)` là où `res` est un
code de retour. Recensement de l'arbre **recompté à la revue de merge**, commentaires et littéraux
exclus : sur `master`, **58** appels — 35 `strerror(errno)`, 15 `strerror(err)` avec `int err = errno;`
au même site (15/15 relus), 6 sur une table dédiée correcte (`hstrerror` ×2, `curl_easy_strerror` ×2,
`curl_multi_strerror`, `uv_strerror` vendorisé) — et **exactement 2** mal posés, tous deux dans ce
fichier, tous deux corrigés ; sur l'arbre livré, **59** appels et **0** mal posé. Le second était
`on_connect()`, où un code CONNACK **5** (mot de passe du courtier faux) sortait `Input/output error`,
**à DEBUG**. ⚠️ La répartition « 59 / 55 / 4 / 2 » publiée d'abord ici faisait **61** et mêlait le
total d'après au détail d'avant.

⛔⭐ **CE QUE LA REVUE DE MERGE A INVALIDÉ, ET QUI CHANGE LA PORTÉE DU CORRECTIF :**
- **`appendFd()` a UN SEUL appelant dans tout l'arbre**, `MqttExternProc_main.cpp`. « Six autres
  familles s'en servent » est faux : le filet `EBADF` vaut pour un point d'extension public, pas pour
  des appelants existants. Corollaire : le `select()` des cinq autres sidecars C++ **ne peut pas
  échouer** aujourd'hui.
- **`EINTR` n'est atteignable par aucun sidecar livré** : aucun n'installe de gestionnaire de signal,
  et un `SIGSTOP`/`SIGCONT` sur `calaos_script` ne fait sortir sa boucle **ni avant ni après** le
  correctif (mesuré des deux côtés). Le correctif est bon ; « un signal suffisait » décrivait le code,
  pas le produit.
- **Ni la copie de `userFds` ni la branche `EINTR` ne sont tenues par un cas** (contre-mutations de la
  revue, 0 rouge sur 134 chacune), et **aucune famille autre que MQTT n'est tenue par quoi que ce
  soit** (les trois sidecars bâtis sans condition sortant avant leur boucle ⇒ 0 rouge). ⇒
  [`T3.108`](T3.108.md).

⛔ **Ce qui reste ouvert, et ça dépasse MQTT ⇒ [`T3.105`](T3.105.md)** : `processExited` est un
`sigc::signal<void>` **sans statut**, et les **neuf** abonnés de l'arbre relancent à l'identique quel
que soit le code de sortie. `T3.103` fait **nommer** un statut non nul par le transport, à WARNING,
pour les sept familles ; il ne fait **rien décider** au serveur.

### ✅ [F-EXTPROC-6] FERMÉ par [`T3.81`](T3.81.md) — le transport journalise la **charge utile** des messages, secrets compris

`ExternProcServer::sendMessage()` écrit `cDebugDom("process") << "client writing data: " << data`,
donc la charge utile sortante **entière**. Pour Reolink, c'est le message d'enregistrement de
caméra, qui porte `username` et `password` **en clair** (`IO/Reolink/ReolinkWire.h:85`).
⭐ **La consigne est écrite trois fois dans l'arbre et le transport générique la contredit** :
`ReolinkWire.h` (« Never log the message itself »), `ReolinkCtrl.cpp` (« never log it, here or
anywhere downstream ») et le sidecar Python qui **caviarde vraiment**
(`IO/Reolink/ExternProcReolink_main.py:1606-1608`). Le transport ne sait pas ce qu'il transporte.
Niveau DEBUG, donc pas imprimé par défaut — mais [T3.79](T3.79.md) §3.1 a mesuré que ce n'est pas
une protection. ⚠️ Même famille, site voisin : la charge utile **entrante** est journalisée de la
même façon. ⭐ La forme qui reste vraie a un précédent **dans le même fichier** : `processData()`
journalise `data.size()`.

✅ **Fermé le 2026-09-05.** Les deux sens sont corrigés : le transport publie le sidecar, le type de
trame et le nombre d'octets, jamais la charge. ⭐ **Le recensement des onze émetteurs côté serveur a
tranché le caviardage comme au §2 de T3.79** — un seul porte un identifiant (Reolink), et Lua fait
passer du **texte libre de configuration**, donc une liste de champs serait fausse au premier
driver qui en ajoute un. ⚠️ **Et le caviardage Python de Reolink, la seule règle de ce genre en
service dans l'arbre, est déjà incomplet chez lui** : deux noms, premier niveau seulement. Épinglé
par `core/ExternProcPayloadSecret_test` à travers un vrai `ReolinkCtrl`.

### ✅ [F-LOGSECRET-1] FERMÉ par [`T3.81`](T3.81.md) — un jeton d'appareil journalisé au niveau **imprimé par défaut**

`Audio/AVRRose.cpp:126` : `cInfoDom("hifirose") << "Registered with device, roseToken: " <<
roseToken`. `roseToken` est le jeton d'authentification rendu par l'amplificateur Hifi Rose et porté
par toutes les requêtes ultérieures (`Audio/hifi_rose_API.md:82,141,293`). À **INFO**, donc
**imprimé sur une installation de série** : **même gravité** que [F-EXTPROC-5], et trouvé en
balayant l'arbre pour [T3.79](T3.79.md) §4. ⭐ Le retirer ne coûte **aucun** diagnostic de
configuration : le jeton est obtenu au vol, il n'est pas dans la configuration, et savoir que
l'enregistrement a réussi suffit.

✅ **Fermé le 2026-09-05.** La ligne nomme l'appareil et non le jeton, et une réponse **sans** jeton
— jusque-là parfaitement invisible — devient un avertissement. ⛔ **Tenu par un tripwire de source,
et le prix en est mesuré** : la contre-mutation qui recopie le jeton sous un autre nom
(`const string issued = roseToken;`) donne **0 rouge**. Atteindre la ligne à l'exécution demande une
réponse HTTPS bouchonnée de l'amplificateur.

⛔⭐ **Et la revue de merge a mesuré PIRE que cela** : la ligne de succès qui gagne `<< data` — le
**corps de réponse entier**, celui d'où le jeton est extrait — republie le secret **à INFO, donc au
niveau imprimé par défaut**, et donne **0 rouge**. Le tripwire est orthographié sur l'identifiant
`roseToken` et ne voit pas une valeur qui ne le cite pas. ⇒ ce qui est tenu est **la ligne**, pas la
classe « le jeton n'atteint aucun journal » ; la classe se ferme du côté de [`T3.83`](T3.83.md).
ℹ️ **Précision de mécanisme, mesurée** : le niveau d'un boîtier neuf ne vient **pas** du `.def("4")`
de `ConfigOptions.cpp` quand l'option n'a jamais été écrite — `Utils::get_config_option()` prend en
second argument `no_logger_out`, pas « rendre le défaut » — mais du repli `LOG_LEVEL_INFO` codé dans
`Logger::maxLevelPrintable()`. La conclusion est la même, la citation non.

### ⚠️ [F-STRSPLIT-1] `Utils::CStrArray` n'a aucun filet propre — ticket proposé `T3.77`

Balayage de **tous** les `.cpp`/`.h` de `tests/` : `tests/core/RoonArgs_test.cpp` est le **seul**
source de l'arbre à mentionner `CStrArray`, et il ne le fait que **depuis T3.28a**. Les trois
autres suites qui touchent `Utils::split` (`IOControllers_test`, `JsonPathSyntax_test`,
`KNXExternProcWire_test`) lui passent **leurs propres délimiteurs** et ne disent donc rien du `" "`
que `CStrArray` code en dur. ⇒ élargir le délimiteur d'une classe de `src/lib` que **tout** l'arbre
traverse ne fait rougir qu'une seule suite, et cette suite appartient à un ticket Roon. La lacune
n'est **pas** de T3.28a — qui l'a au contraire révélée — et mérite sa propre suite de
caractérisation (`split` avec `max`, le remplissage final `while (tokens.size() < max)`, l'absence
de tout échappement, et les deux constructeurs de `CStrArray`).

ℹ️ Corollaire pour la correction (3) : `Utils::escape_space()` existe (`StringUtils.cpp:350`) et
**n'est pas une porte de sortie** — `Utils::split()` ne connaît ni backslash ni guillemet, donc un
`mon\ core` échappé produirait `mon\` **et** `core`, soit le défaut plus un hôte corrompu.

### ✅ [F-URLDL-1] FERMÉ par [`T3.83`](T3.83.md) — `UrlDownloader` publiait **tout corps de réponse HTTP**

`src/lib/UrlDownloader.cpp:692` : `cDebugDom("urlutils") << "Response data: " << m_downloadedData`.
Le corps entier, quel que soit le driver qui a lancé le transfert. Même famille que [F-EXTPROC-6] :
un transport générique qui journalise ce qu'il ne comprend pas. ⭐ Trouvé en balayant l'arbre pour
[`T3.81`](T3.81.md) §4 : **le corps qui passe par là est celui d'où `AVRRose` extrait le jeton
Hifi Rose**, donc T3.81 ferme le site qui journalisait la valeur et celui-ci publie le document
dont elle vient. ⚠️ **Le site croit déjà se protéger** : le même fichier caviarde les identifiants
d'**URL** partout (`Utils::maskUrlCredentials`, `:446,477,733`) et jamais le corps. ⚠️ Le
recensement des autres drivers HTTP qui y passent **reste à faire**, et le coût en diagnostic est
réel — le corps est ce qu'on lit pour comprendre un décodage qui échoue.

✅ **Fermé le 2026-09-05.** Le transport publie le **statut** et la **taille** du corps, jamais le
corps ; le type de contenu reste sur les lignes d'en-tête juste au-dessus. ⭐ **Le recensement des
quinze sites d'appel a tranché le caviardage comme en T3.79 et T3.81, mais par l'excès inverse** :
là-bas **un seul** émetteur portait un secret, ici il y en a **quatre**, de quatre protocoles
différents — le jeton Hifi Rose (`Audio/AVRRose.cpp:421`), la liste des autorisations influxdb v2
(`DataLogger.cpp:58`), l'identifiant de session Synology (`IPCam/SynoSurveillanceStation.cpp:210`),
et les URL libres de Lua et du Web IO. Une liste de champs sensibles devrait couvrir quatre
vocabulaires aujourd'hui et tous ceux de demain ; le transport ne voit qu'une `std::string`, qui est
parfois un JPEG. ⭐ **Niveau MESURÉ dans un enfant forké** (`AStockInstallDoesNotPrintTheUrlutilsDebugLines`) :
`urlutils` **imprime à INFO** et **n'imprime pas à DEBUG** sur une installation de série — la fuite
était donc **un cran sous** le niveau par défaut, contrairement à F-LOGSECRET-1. Épinglé par
`UrlDownloaderLogSecret_test` sur un **transfert HTTP réel**. ⚠️ **Ce qui reste ouvert est fiché en
`F-URLDL-2`** : le bloc d'en-têtes de réponse, le masquage d'URL par liste de dix noms, et six sites
de drivers qui republient un corps entier — trois d'entre eux **au-dessus** de DEBUG.

⭐ **Revue de merge (2026-09-05)** — le recensement est **sous-évalué** : l'arbre porte **24
lancements** hors `src/lib` (20 `new UrlDownloader` + 4 appels aux fabriques statiques), dont **22**
traversaient la ligne fautive, contre « treize des quinze » annoncés — l'argument contre le
caviardage en sort **renforcé**. ⛔⭐ **Le capteur cherchait une valeur connue d'avance** : la ligne
réduite gagnant les **24** premiers octets du corps, ou ses 24 derniers, donnait **0 rouge** — seul
un extrait assez large pour contenir le jeton entier (31 octets, commençant au 21ᵉ) rougissait.
**Corrigé dans la branche** : les cas de fuite bornent désormais la plus longue suite d'octets du
corps que le journal rend. ⚠️ **Ce qui reste ouvert du côté du filet** : aucun **chemin d'échec** du
transport n'est exercé — le corps republié à WARNING sur la branche non-2xx donne **0 rouge**, et
WARNING est imprimé par défaut. Le niveau annoncé est confirmé **DEBUG**.

### ✅ [F-URLDL-2] FERMÉ (pour l'URL) par [`T3.87`](T3.87.md) — ce que `UrlDownloader` publie **à côté du corps**

Trouvé en mesurant [`T3.83`](T3.83.md), qui a fermé le corps et **pas** ces trois canaux.

1. ⛔ **Le bloc d'en-têtes de réponse sort en entier**, ligne à ligne : `getResponseHeaders()` fait
   `cDebugDom("urlutils") << line` pour **chaque** ligne. `Set-Cookie`, `WWW-Authenticate` avec son
   nonce, un `Authorization` renvoyé en écho y passent verbatim, au **même niveau et dans le même
   domaine** que la fuite que T3.83 vient de fermer. ⚠️ Et la méthode est **publique** :
   `SynoSurveillanceStation` la rappelle depuis son callback, ce qui **republie tout le bloc une
   seconde fois**.
2. ⭐ **L'URL n'est masquée que par une liste de dix noms, et seulement dans sa requête.**
   `Utils::maskUrlCredentials` couvre `usr, pwd, user, username, password, passwd, account,
   loginuse, loginpas, _sid` — **ni `token`, ni `api_key`, ni `apikey`, ni `key`, ni
   `access_token`**. Et elle ne touche pas le **chemin** : la clef d'API du pont Hue est un segment
   de chemin (`http://<host>/api/<clef>/lights/<id>`), donc elle part **en clair** sur la ligne
   `"UrlDownloader: "`, qui est à **INFO** — **imprimée sur une installation de série**, un cran
   **au-dessus** de ce que T3.83 a fermé. ⭐ C'est la démonstration, dans le même fichier, de ce que
   la doctrine dit d'une liste de secrets : elle est fausse dès qu'on regarde ailleurs.
3. ⚠️ **Six sites de drivers republient un corps de réponse entier**, dont **trois au-dessus de
   DEBUG** : `IPCam/SynoSurveillanceStation.cpp:302` (**WARNING**, sur l'échec de parsage de
   `parseJsonResult()`, appelée par `login()` — donc le corps qui porte le `sid`), `:189`
   (**WARNING**), `IO/Hue/HueOutputLightRGB.cpp:71,78` (**ERROR**), puis `:82,137,157` et
   `IO/Web/WebCtrl.cpp:434` (qui publie en prime l'URL **non masquée**) à DEBUG.

⭐ **Revue de merge (2026-09-05) — le point 2 est confirmé maillon par maillon, et il est PLUS
GRAVE que `F-URLDL-1`.** `Utils::maskUrlCredentials` **retourne avant toute analyse quand l'URL ne
porte pas de `?`**, donc elle ne regarde jamais le chemin ; `HueOutputLightRGB` construit
`"http://" + m_host + "/api/" + m_api + "/lights/" + m_idHue` où `m_api` est le paramètre `api`
d'`io.xml` ; et le constructeur d'`UrlDownloader` publie l'URL par `cInfoDom("urlutils")`, soit le
repli d'un boîtier neuf. ⇒ **la clef d'API du pont Hue part en clair, à INFO, sur une installation
de série, à chaque tour d'une minuterie de 2 s.** Les niveaux du point 3 sont vérifiés eux aussi :
`WARNING` vaut 3 et `ERROR` vaut 2, tous deux sous le défaut de 4 ⇒ **imprimés sans que personne
n'allume rien**.

⚠️ **Le coût en diagnostic est réel pour le point 1** : les en-têtes sont ce qu'on lit pour
comprendre une redirection ou un type de contenu inattendu — et T3.83 s'appuie explicitement sur
`Content-Type` comme contrepoids. La forme qui garde les deux est **d'énumérer les en-têtes
publiés** (statut, `Content-Type`, `Content-Length`, `Location`) et de ne rendre que le **nom** des
autres. ⛔ Le harnais existe : `tests/UrlDownloaderLogSecret_test.cpp` monte déjà un pair HTTP dont
la réponse est choisie — lui faire poser un `Set-Cookie` est une ligne.

✅ **Le point 2 est FERMÉ le 2026-09-05 par [`T3.87`](T3.87.md).** ⛔ **Les points 1 et 3 restent
ouverts sur `master`** et sont repris tels quels sous **`F-URLDL-3`** ⇒ [`T3.88`](T3.88.md).

⭐ **L'arbitrage a été tranché PAR LA MESURE, pas par doctrine.** Les huit lignes du transport qui
publiaient une URL ont été relues une par une : ⛔ **la ligne d'achèvement (`Finished with status
code:` puis `Response body: N bytes`) ne porte NI l'URL NI l'adresse de l'objet**, donc deux
transferts concurrents y sont **déjà** indiscernables. ⇒ **corréler une requête et sa réponse n'est
pas un service que cette URL rendait**, et ses deux usages réels — quel appareil, l'appel a-t-il
abouti — sont rendus par l'**hôte**. Le transport publie désormais schéma, hôte et port, et le reste
comme une **forme** (segments, paramètres, octets) plus une **empreinte salée par processus** des
seuls octets retenus ; l'empreinte existe parce qu'un pont à douze lampes donne douze URL qui ne
diffèrent que par un segment de chemin.

⭐ **Le recensement dit pourquoi allonger la liste ne pouvait pas tenir** : **les quatre formes
coexistent dans l'arbre** et la liste n'en couvrait qu'**une et demie** — **CHEMIN 3** lancements
(Hue), **REQUÊTE 4** (`Foscam.cpp:128,130` ; `SynoSurveillanceStation.cpp:175,210`), **5
polymorphes** résolus en **IDENTIFIANTS `user:pass@`** chez Axis (5 sites) et Planet (**23** appels à
`camGet()` depuis un userinfo fabriqué en `Planet.cpp:123`), **EN-TÊTE 2** (`DataLogger.cpp:58,202`),
**LIBRE 4**, **aucun secret 6**. ⚠️ **Et « 22 traversent la ligne » est le chiffre de `F-URLDL-1`** :
les deux téléchargements vers un fichier évitent la ligne du **corps**, pas celle du
**constructeur**, qui journalise inconditionnellement ⇒ **24** traversent la ligne fermée ici.

⭐ **`maskUrlCredentials` n'est pas touchée, et c'est mesuré** : **21 assertions** de deux suites
épinglent sa sortie exacte (14 dans `IPCamUrl_test`, 7 dans `UrlDownloader_test`). Ses deux appelants
de journal passent à `Utils::urlForLog` ⇒ elle survit **sans appelant de production**, un commentaire
pour seule barrière. **Dix sites** publiant une URL sont convertis, dont **cinq imprimés par défaut**
et `IPCam.cpp:118` à **ERROR** ; `WebCtrl.cpp:434` et `SynoSurveillanceStation.cpp:268` publiaient
l'URL **brute**.

⭐ **Le capteur ne dépend ni d'un nom ni d'une longueur** : il borne la plus longue **suite d'octets**
de la partie porteuse de l'URL que le journal rend, **et la cherche aussi percent-décodée** —
recouvrement fortuit **3**, plafond **8**, **118** sur `master`. **Le chemin d'échec est exercé** (le
pair accepte et n'écrit rien ⇒ la ligne WARNING), et le niveau par défaut est **mesuré** dans un
enfant forké : `urlutils` **imprime à INFO** sur un boîtier neuf.

⛔⭐ **Une contre-mutation a mesuré 0 rouge et corrigé la fixture** : le cas « deux points d'accès du
même hôte » montait **deux** pairs, donc deux ports éphémères, et l'empreinte les distinguait toute
seule — il passait pour une raison sans rapport avec ce qu'il prétendait mesurer. Même famille que
les 0 rouge des revues de T3.83 et T3.85. Corrigé : un seul pair, plusieurs connexions, égalité des
autorités assurée ; la même mutation rend **1 rouge**.

### ✅ [F-URLDL-3] FERMÉ par [`T3.88`](T3.88.md) — le bloc d'en-têtes de réponse, et les sites qui republient un corps entier

Ce sont les points 1 et 3 de `F-URLDL-2`, **repris tels quels** : [`T3.87`](T3.87.md) n'a fermé que
l'URL.

1. ⛔ **Le bloc d'en-têtes de réponse sort en entier**, ligne à ligne (`getResponseHeaders()` fait
   `cDebugDom("urlutils") << line`) : `Set-Cookie`, `WWW-Authenticate` avec son nonce, un
   `Authorization` en écho. ⭐ **Et `Location:` d'une redirection EST une URL, publiée brute**, alors
   que le réducteur existe désormais (`Utils::urlForLog`). ⚠️ La méthode est **publique** et
   `SynoSurveillanceStation` la rappelle depuis son callback ⇒ tout le bloc sort **une seconde
   fois**. ⚠️ Contrepoids obligatoire : T3.83 §4 s'appuie explicitement sur `Content-Type`.
2. ⛔ **Six sites de drivers republient un corps de réponse entier, dont trois au-dessus de
   DEBUG** : `IPCam/SynoSurveillanceStation.cpp:302` (**WARNING**, échec de parsage de
   `parseJsonResult()`, appelée par `login()` — le corps qui porte le `sid`), `:189` (**WARNING**),
   `IO/Hue/HueOutputLightRGB.cpp:71,78` (**ERROR**), puis `:82,137,157` et `IO/Web/WebCtrl.cpp:437`
   à DEBUG.

⚠️ **L'arbitrage n'est pas celui de T3.87** : Hue publie le corps **parce qu'il n'a pas su le lire**,
donc la taille seule ne dit pas pourquoi. Il faut d'abord savoir ce qu'un intégrateur lit vraiment
sur ce chemin, et ce n'est pas mesuré.

✅ **FERMÉ le 2026-09-05 par [`T3.88`](T3.88.md)** — 8 sites fermés, `TESTS` **128 → 129**.

⭐ **Le point 2 était plus large que ce qui est écrit ci-dessus.** `parseJsonResult()` publiait le
corps **et** `e.what()` de nlohmann, et elle est appelée par `login()` **et** par `getApiInfo()` :
la réponse de `login()` **est** le document qui porte le `sid`, la session ouverte avec les
identifiants de caméra d'`io.xml`. Quatre chemins y mènent — pas un objet, `success` faux, pas de
bloc `data`, toute exception de `Json::parse` — et le **niveau est mesuré dans un enfant forké** :
le domaine par défaut imprime à **WARNING** et à **ERROR** sur un boîtier neuf, le domaine `hue`
à **ERROR**, `urlutils` **pas** à DEBUG.

⭐ **La forme est l'INVERSE d'une liste de secrets, et c'est ce qui la distingue des deux listes qui
ont échoué.** `getResponseHeaders()` énumère **ce qu'il a le droit de publier** — huit noms qui
décrivent le message (`accept-ranges`, `connection`, `content-encoding`, `content-length`,
`content-range`, `content-type`, `retry-after`, `transfer-encoding`), plus la ligne de statut brute
— passe `Location`/`Content-Location` par **`Utils::urlForLog`** parce que ce sont des URL, et rend
tout le reste comme `nom: [NB] #empreinte`, comparaison **insensible à la casse**. Un en-tête
inconnu est retenu **parce qu'il est inconnu**. Aucun réducteur parallèle : `urlForLog` et `logTag`
existaient.

⭐ **L'argument Hue est mesurable et il n'est pas « le pont met un secret dans sa réponse »** : la
clef d'API du pont est un **segment de chemin** de l'URL interrogée (acquis de `F-URLDL-2` §2), et
ce qui répond à cette adresse quand ce n'est pas un pont sert une page d'erreur qui **cite le chemin
demandé**.

⭐ **Le capteur ne dépend ni d'un nom ni d'une longueur** (`tests/core/DriverAnswerSecret_test.cpp`,
suite **neuve**, disjointe de `UrlDownloaderLog*_test` que [`T3.96`](T3.96.md) va réécrire) : il
borne la plus longue **suite d'octets** du document que le journal rend, cherchée **sous trois
formes** (clair, pourcents, base64) ; **recouvrement fortuit 4**, plafond **5** épinglé par une
**ÉGALITÉ** re-dérivée à chaque exécution, **abaissé à 4** ⇒ **3 rouges** disant `actual: 4 vs 4`.
Les deux suites de 4 sont de vrais accidents : `code` (*status code:* contre `"code":403`) et `ocat`
(*Location:* contre `/relocated`). **Par le vrai chemin** : le pair répond aux deux requêtes de
`downloadSnapshot()` et la suite exige `method=Login` **et** `passwd=` sur le fil avant de conclure.
**Un seul pair, deux connexions** : rien n'y passe en distinguant deux ports éphémères.

⭐ **Six contre-mutations par échange, ensembles rouges deux à deux distincts**, témoin à 0 rouge,
restaurations prouvées `cmp` rc 0 **et** horodatage effectivement modifié. Les deux qui comptent :
**M5** publie la valeur du cookie **en base64**, la recherche du clair reste **verte** et la borne
rougit à **68 octets** ⇒ la chasse en trois formes mord là où un capteur orthographié serait aveugle ;
**M4** efface le nom et les valeurs énumérées ⇒ **aucune** fuite ne rougit, le **contrepoids**
rougit (statut, type de contenu, cible de redirection) ⇒ le diagnostic est tenu par un test.

⛔ **Ce qui reste nu** : `Location:` **relative** n'est réduite qu'en `<url NB> #empreinte` — la
forme du chemin est perdue là où elle est gardée pour une cible absolue ; ⭐ **sept des neuf sites
fermés sont tenus par LECTURE** — les cinq Hue, le corps envoyé du Web IO et, **relevé à la revue**,
le refus d'instantané de Synology, que la suite n'atteint jamais parce qu'elle s'arrête à la
connexion refusée ; **mesuré**, une mutation à Hue et une au refus d'instantané rendent **0 rouge**
l'une comme l'autre. La **ligne de statut** sort brute, sa phrase de raison est choisie par le pair —
arbitré à la revue et laissé tel quel : octets du pair, jamais un secret de ce boîtier, et à un
niveau non imprimé sur un boîtier neuf.

⛔⭐ **UN TROU RESTAIT DANS LE CORRECTIF, FERMÉ AVANT LE MERGE — et c'est le mode d'échec de la
doctrine elle-même.** Une liste de ce qu'on publie décide sur le **nom** de l'en-tête ; une valeur
que le pair **replie sur deux lignes** arrive en continuation **sans deux-points**, donc découpée sur
`":"` elle tombe tout entière dans le nom. La tête d'un cookie était retenue et sa queue rendue
**verbatim** sur la ligne suivante — **49 octets consécutifs mesurés** :
`id_session=Zc3fH9yGq5AeUi1oPr0M; Path=/; HttpOnly: [0B] #8e296def`. ⭐ **La leçon** : *une
énumération ne vaut que si ce sur quoi elle décide est bien ce que son nom prétend être.* Corrigé —
une ligne sans deux-points sort par sa taille et son empreinte, et **reste une ligne** ; neuvième cas
de la suite, rouge sans le correctif.

⭐ **Le refus de fermer `F-URLDL-5` (`errorBuf`) éprouvé à la revue par un scénario que la fiche ne
liste pas** : l'URL de ce dépôt porte les identifiants de caméra dans son **userinfo**, et sur
`http://admin:<mot de passe>@<hôte introuvable>/api/...?key=...` libcurl 7.88.1 ne rend que
`Could not resolve host: <hôte>` — ni userinfo, ni chemin, ni requête. ⚠️ **Nuance** : le nom d'hôte
rendu peut être **choisi par le pair** via sa cible de redirection (180 octets mesurés), pas
seulement le nôtre.

⭐ **L'ÉGALITÉ DE CALIBRATION ÉPROUVÉE DANS LES DEUX SENS.** La fiche l'abaisse (5 → 4 ⇒ 3 rouges) ;
la revue la **relève** (5 → 6) ⇒ **1 seul rouge**, la calibration, qui nomme le nombre à écrire :
**5**. Abaisser montre que la borne mord, relever montre que c'est une **égalité** — c'est cette
seconde moitié qui manquait aux campagnes précédentes.

### 📋 [F-URLDL-5] `conn->errorBuf` de libcurl est concaténé verbatim à WARNING — **mesuré non porteur**, aucun ticket

`src/lib/UrlDownloader.cpp`, branche d'échec de `checkMultiInfo()` : le tampon de
`CURLOPT_ERRORBUFFER` est concaténé tel quel à la ligne *Transfer failed for …*, à **WARNING**, donc
**imprimé sur un boîtier neuf**. C'est le §libcurl de `F-URLDL-2`, resté ouvert de ticket en ticket
parce que personne n'avait regardé ce que libcurl y écrit vraiment.

⭐ **Mesuré à [`T3.88`](T3.88.md), pas déduit** — 9 scénarios contre **libcurl 7.88.1**, une aiguille
placée à chaque endroit atteignable par un pair : schéma non supporté en redirection, cible de
redirection malformée, hôte introuvable, `Content-Length` menteur, ligne de statut illisible,
`WWW-Authenticate: Digest` cassé, `chunked` cassé, `Content-Encoding` inconnu, et un **mot de passe
d'userinfo** dans la cible de redirection. **L'aiguille n'en est jamais ressortie.** Les seules
données venues du distant sont un **nom d'hôte** (`Could not resolve host: X`) et un **nom de
schéma** (`Protocol "gopher" not supported`) — deux choses que `Utils::urlForLog` publie **par
construction**.

⇒ **Fiché, non corrigé** : caviarder le tampon coûterait le diagnostic le plus utile de la ligne
(*transfer closed with N bytes remaining*, *Illegal or missing hexadecimal sequence*) contre une
fuite que la mesure ne trouve pas. ⚠️ **La réserve** : ce sont les chaînes de format de **libcurl**,
pas les nôtres ; une version future peut en ajouter une qui cite davantage, et **aucun test ne
l'observerait**.

### 📋 [F-URLDL-6] `AVRRose` publie encore une URL brute — **ne porte rien**, aucun ticket

`src/bin/calaos_server/Audio/AVRRose.cpp`, `getRequest()` et `postRequest()` :
`cDebugDom("hifirose") << "GET " << url` où `url` est `https://hôte:port/<urlPath>` **non réduite**.
[`T3.87`](T3.87.md) a converti dix sites et manqué ces deux-là.

✅ **Recensé à [`T3.88`](T3.88.md) : rien ne peut y transiter.** `urlPath` est un **littéral** aux
trois appelants (`get_current_state`, `get_control_info`, `mute.state.get`) — ni requête, ni
userinfo, ni segment variable — et le corps publié à la même ligne est `{"connectIP": <ip locale>}`.
Le jeton de l'appareil, lui, ne passe pas par là (il voyage en en-tête et n'est jamais journalisé
depuis [`T3.83`](T3.83.md)). ⇒ **fiché pour cohérence de forme, pas pour une fuite.** Niveau DEBUG,
donc pas imprimé sur un boîtier neuf.

### ⚠️ [F-URLDL-4] Le **sel par processus** de l'empreinte d'URL n'est gardé par aucune assertion — aucun ticket

`Utils::urlForLog` ne publie du reste d'une URL qu'une forme et une empreinte, et cette empreinte
est **salée par un tirage de `std::random_device`** fait une fois par processus. Le sel est la seule
chose qui empêche un lecteur de journal de **confirmer une URL devinée** en la hachant : sans lui,
l'empreinte devient un oracle d'égalité utilisable hors du journal où elle apparaît.

⛔ **Mesuré par la revue de merge de [`T3.87`](T3.87.md)** : la contre-mutation qui retire le sel
(`^ urlTagSalt()` supprimé du hachage) rend ⛔ **0 rouge** sur `make check` complet. Aucun des six
cas ne compare deux processus, et l'empreinte reste stable pour une URL donnée — ce que toutes les
assertions demandent. La propriété est **écrite dans le commentaire de `StringUtils.h`, dans la
fiche et dans les notes de version, et n'a pas de capteur**.

⚠️ **Ce n'est pas une fuite** : le code livré EST salé. C'est un **piège pour le prochain
mainteneur** — quiconque « simplifie » le hachage, ou remplace `std::random_device` par une
constante pour rendre l'empreinte reproductible entre deux exécutions, verra la suite rester verte.
⭐ **La garde tient en un cas** : forker un enfant, lui faire réduire la **même** URL, et exiger que
les deux empreintes **diffèrent**. Non écrite ici : la revue ne s'est pas donné le droit d'étendre
le périmètre du ticket.


### ✅ [F-HTTPIN-1] FERMÉ par [`T3.90`](T3.90.md) — le serveur HTTP **entrant** publiait tous les en-têtes reçus, `Authorization` compris

`src/bin/calaos_server/HttpClient.cpp:277-279` : `cDebugDom("network")` écrit la cible de la requête
puis **chaque** en-tête reçu. Un client qui s'authentifie auprès de `calaos_server` voit donc son
`Authorization` recopié dans le journal. Trouvé en recensant les publications d'URL pour
[`T3.87`](T3.87.md), **fiché sans ticket** : c'est la classe **entrante**, distincte de tout ce que
`F-URLDL-*` couvre. ⚠️ DEBUG (5) donc au-dessus du repli 4 : **pas imprimé sur un boîtier neuf**,
mais c'est le niveau qu'un utilisateur allume avant de coller un journal dans un rapport de bogue.

⭐ **VÉRIFIÉ À LA SOURCE PAR LA REVUE DE MERGE DE `T3.87` (2026-09-05), ET TICKETÉ
[`T3.90`](T3.90.md).** Les trois porteurs sont réels et ce dépôt les fabrique lui-même :
`JsonApiHandlerHttp.cpp` remet le jeton Bearer du serveur MCP sur `get_mcp_info` **avec pour
consigne explicite de le renvoyer dans `Authorization`** ; `RemoteUI/HMACAuthenticator.cpp` lit un
second Bearer accompagné de son nonce et de son HMAC ; les cookies de session d'un navigateur
passent par la même boucle. ⚠️ **Et la cible (`parse_url`) est publiée brute** sur la ligne qui
précède, alors que `Utils::urlForLog` existe depuis `T3.87`. ⭐ **C'est la seule famille ENTRANTE de
la série** : les six correctifs de sécurité de la nuit ferment tous des secrets sortants.

✅ **Fermé le 2026-09-05.** La ligne publie désormais **la méthode, le chemin, la forme de la
requête** (nombre de paramètres, octets, empreinte salée par processus) et, pour chaque en-tête,
**son nom et la taille de sa valeur** — sauf pour les **six** en-têtes sur lesquels ce serveur
**route**, dont la valeur sort. ⭐ **Ce n'est pas une liste de noms sensibles, c'est son inverse, et
la différence est la direction de l'échec** : un en-tête inconnu est **retenu** au lieu d'être
publié. Mesuré — la contre-mutation qui remplace l'énumération par la liste de caviardage
(`authorization`, `cookie`, `proxy-authorization`) rougit **sur l'en-tête dont le nom n'existe
nulle part dans l'arbre**.

⭐ **Le niveau a été MESURÉ avant de choisir, et il borne la gravité** : enfant forké avant que le
`main()` de la suite ne lève le niveau, sur une configuration vierge — `network` **imprime à
WARNING** et ⛔ **n'imprime pas à DEBUG**. ⇒ **la fuite ne partait pas sur une installation de
série**, un cran sous [`T3.87`](T3.87.md). Ce qui justifie le ticket est la **nature** du secret
(des jetons fabriqués par ce dépôt) et le **périmètre** (toute requête parsée, poignée de main
WebSocket comprise, par le même code).

⭐ **Un troisième site, absent de la fiche d'origine, fermé avec les deux autres** :
`src/lib/WebSocketFrame.cpp:315` rendait **les 40 premiers octets de la charge utile de toute trame
texte**, c'est-à-dire la requête d'API d'un client WebSocket — **avant** que `dumpJsonRedacted` n'ait
rien caviardé. Ce qui sortait d'un mot de passe dépendait de sa **position** dans le document :
**un capteur qui dépend d'une longueur, écrit dans le code de production**.

⚠️ **Nuance mesurée sur le porteur MCP** : `McpProxyHandler::sniffRequest()` ne regarde que la
**première ligne de requête de la connexion**, et une connexion `/mcp` devient un tunnel brut qui ne
journalise aucune donnée. Un client MCP qui parle `/mcp` d'emblée **ne traversait pas cette ligne** ;
il la traversait dès que la connexion avait servi autre chose d'abord. Le porteur est réel mais
**conditionnel**, contrairement au Bearer RemoteUI et aux cookies, qui la traversaient à chaque
requête.

⛔ **Non fermé, et nommé** : **sept familles** publient encore une donnée entrante à un niveau
imprimé par défaut — dont l'**identité du client**, qui derrière haproxy est la dernière entrée de
l'`X-Forwarded-For` que le client a écrit, à WARNING sur six sites. Aucune ne porte un secret
distribué par ce dépôt.

⚠️⭐ **Complément de la revue de merge (2026-09-05) — la réduction de la cible dépendait
d'abord de ce que la requête portait, corrigé avant le merge.** `Utils::requestTargetForLog()`
renvoyait à `Utils::urlForLog()` dès que la cible contenait `://` **où que ce soit**, et
`urlForLog()` republie verbatim tout ce qui précède le schéma. Une cible ordinaire dont un
paramètre porte une URL repartait donc **entière** :
`/api.php?cn_user=operateur&cn_pass=motdepasse&next=http://a.b/c` rendait
`/api.php?cn_user=operateur&cn_pass=motdepasse&next=http://a.b [path 1seg/2B] #…`. ⭐ **Même mode
d'échec que l'extrait de trame** : une règle indexée sur l'endroit où un octet se trouve. Le test
porte désormais sur la **position** du schéma (nom de schéma RFC 3986, rien de `/?#` devant), et un
dixième cas l'épingle. Contre-mutation par échange (position ↔ présence) : **1 rouge**, *the journal
gives back **99** consecutive bytes* contre un plafond de 8.

### ✅ [F-HTTPIN-2] FERMÉ par [`T3.92`](T3.92.md) — le **corps** d'une requête sortait par une liste de onze noms

`JsonApi::dumpJsonRedacted()` (`JsonApi.cpp:518`) publie le corps entier d'une requête après avoir
caviardé la valeur des clefs appartenant à une liste de **onze** noms, sur les **deux** transports
(`JsonApiHandlerHttp.cpp:238`, `JsonApiHandlerWS.cpp:251`), à DEBUG. ⭐ **C'est exactement la forme
que la revue de [`T3.87`](T3.87.md) a enterrée et que [`T3.90`](T3.90.md) a refusée** — et le trou
est ouvert par l'API de ce dépôt elle-même : `action: config`, `type: put` téléverse les fichiers de
configuration **sous des clefs qui sont des noms de fichiers**, et ⛔ **`local_config.xml` est le
fichier où ce serveur écrit `mcp_token`, `mcp_service_token` et `calaos_password`**. La clef
s'appelle `local_config.xml` ; elle n'est dans aucune des onze ⇒ **le fichier entier, avec tous ses
jetons, part au journal**. ⚠️ Un second site le refait sans même passer par le réducteur
(`JsonApiHandlerHttp.cpp:813`, `filecontent` entier). ⛔ **Le harnais existe** :
`tests/core/HttpRequestLogSecret_test.cpp` monte un vrai `HttpServer` et relit `std::cout` — le cas
décisif est **une clef qui n'est dans aucune liste**, un cas écrit sur `cn_pass` serait vert des deux
côtés.

✅ **Fermé le 2026-09-05.** `dumpJsonRedacted()` est **supprimée**, pas laissée en place sans
appelant : `JsonApi::describeRequestForLog()` la remplace et elle est construite **comme la ligne
d'en-têtes de la même requête** que `T3.90` a posée — un ensemble **fermé** de champs routés
(`action`, `msg`, `msg_id`, `type`, `hardware`) publie sa valeur, **tout le reste** est un nom, un
compte d'octets et une empreinte salée. Une clef inconnue est retenue **parce qu'elle est inconnue**.
⭐ **Le recensement des appelants a montré DEUX commandes hors de la liste et pas une** : `config`
/`put` (clefs = noms de fichiers) et ⭐ **`set_param`, où le mot « password » est la VALEUR de `param`
et le secret vit sous `value`** — deux clefs qu'aucune liste ne peut prendre sans caviarder tout
`set_param` légitime. ⛔ **Le vidage RESTE avant `checkCredentials()`, et c'est mesuré** : sur le
websocket il n'y a pas d'« après » (le `login` **est** le corps décrit), et la contre-mutation qui le
déplace sur HTTP rend **5 rouges** disant tous « le corps n'est décrit nulle part » — la ligne
mourrait exactement sur les requêtes refusées, celles qu'on débogue. Ce qui rendait le placement
dangereux était le contenu. Six contre-mutations par échange, dont une qui remet **exactement**
l'ancien comportement (**14 rouges**) ; capteurs par **borne** sur la plus longue suite d'octets, en
quatre formes, sur un jeton produit par le générateur livré, recouvrement fortuit **mesuré à ≤ 4** et
borne abaissée à 1 pour le prouver.

### ✅ [F-HTTPIN-4] FERMÉ par [`T3.92`](T3.92.md) (revue) — le contenu d'un fichier de configuration REFUSÉ sortait entier, et le site n'était tenu par aucun test

`JsonApiHandlerHttp.cpp`, branche « file content is not XML » de `config`/`put`, publiait
`filecontent` **entier** à DEBUG, sans passer par le moindre réducteur : c'est le fichier que le
client vient de téléverser, donc `local_config.xml` ou `io.xml` avec tous leurs jetons, refusé pour
la seule raison qu'il ne commence pas par `<?xml`. ✅ **Corrigé par [`T3.92`](T3.92.md)** : la ligne
publie la taille. ⛔ **Mais elle n'est tenue par AUCUNE assertion, et c'est mesuré** : la
contre-mutation qui la remet rend **0 rouge sur 128 suites**. La raison est structurelle — le site
est derrière un `config`/`put` **accepté**, qui écrit les trois fichiers puis **redémarre le
serveur** (`setNeedRestart(true)` ⇒ `uvw::Loop::stop()`), et le harnais
`tests/core/HttpRequestLogSecret_test.cpp` partage une seule boucle entre tous ses cas. ⭐ **Ce qu'il
faudrait épingler** : un binaire de test à lui seul, avec des identifiants configurés, un répertoire
de configuration jetable et un seul cas — ou bien découper la branche de refus dans une fonction que
l'on peut exercer sans traverser le redémarrage. ⚠️ **Contrepoids** : savoir *quel* fichier a été
refusé et *pourquoi* est le diagnostic, et il survit à la réduction.

✅⭐ **Fermé le 2026-09-05 à la revue de merge, et « structurel » était faux.** Le téléversement n'a
pas besoin d'être **accepté** pour que la branche soit atteinte : `setNeedRestart(true)` n'est armé
que `if (ret)`, et une seconde entrée de `config_files` sous un nom qui n'est pas un des trois
fichiers met `ret` à faux **sans toucher à la boucle de fichiers**. Un cas ordinaire de la suite
existante suffit donc — identifiants du `local_config.xml` fraîchement semé, une entrée `io.xml` au
contenu non XML qui porte l'aiguille, une entrée au nom invalide qui désarme le redémarrage. Cas
`TheContentOfARefusedConfigFileIsNotRepublished`, deux anti-vacuités (la requête **a** authentifié,
le contenu **a** été refusé pour n'être pas du XML) ; la contre-mutation qui republie le contenu
entier passe de **0 rouge sur 128** à **1 rouge qui nomme le site et ses 26 octets**.
⇒ **`T3.95` est sans objet, ne l'ouvrez pas.**

### ✅ [F-HTTPIN-3] FERMÉ par [`T3.93`](T3.93.md) — les sept sites qui publient une donnée entrante à un niveau imprimé par défaut n'étaient tenus par **aucun** test

[`T3.90`](T3.90.md) §2.2 recense sept familles qui publient une donnée choisie par le client à un
niveau `≤ 4`, donc **imprimé sur un boîtier neuf**, et argumente qu'aucune ne porte un secret que ce
dépôt distribue. ⭐ **L'argument a été vérifié site par site par la revue et il tient** : ces lignes
publient une adresse de client, un chemin de fichier statique déjà découpé, une raison de fermeture,
un nom de clef sur un chemin authentifié.

⛔ **Mais c'est une propriété du code d'aujourd'hui, et rien ne la tient.** Mesuré : la
contre-mutation qui remplace `req_url.getPath()` par `parse_url` à la ligne du refus de poignée de
main (`WebSocket.cpp:267`) fait publier **la cible brute, requête comprise, à WARNING** — et rend
⛔ **0 rouge sur les 127 suites**, relink lu (`CXXLD calaos_server`, `CXXLD WebSocketAccept_test`,
`CXXLD core/HttpRequestLogSecret_test`). Le jour où l'une de ces sept lignes glissera vers la donnée
complète, la fuite partira **sans qu'on ait rien allumé** et rien ne rougira.

⭐ **Ce qu'il faudrait épingler** : un cas par famille sur le harnais existant
(`tests/core/HttpRequestLogSecret_test.cpp` monte un vrai `HttpServer` et relit `std::cout`) — une
poignée de main refusée, une requête `/debug/…` en traversée, un `config put` à clef inventée — et
dans chacun **borner la plus longue suite d'octets** de la cible que la ligne rend, jamais chercher
un nom. ⚠️ **Contrepoids** : ces lignes existent pour dire d'où vient un abus, l'adresse et le
chemin découpé doivent survivre.

✅ **Fermé le 2026-09-05, par un filet et non par un correctif : `src/` est intouché.**
`tests/core/IncomingLogStockLevel_test.cpp`, **14 cas**, `TESTS` **128 → 129**. Chaque famille est
atteinte par une **vraie requête sur la socket** d'un `HttpServer` réel ; aucun cas n'appelle une
fonction de journalisation ni un gestionnaire.
⭐ **La trouvaille de dispositif** : le foin est un **sous-ensemble** du journal. Le binaire lève son
niveau à DEBUG pour pouvoir prouver qu'un octet est bien arrivé, et **toute borne est mesurée sur les
lignes dont le marqueur de niveau n'est pas `[DBG]`** — c'est-à-dire sur ce qu'une installation que
personne n'a configurée imprime. Le niveau de repli est **mesuré dans un enfant forké**, pour les
**trois** domaines traversés (`network`, `websocket`, `mcp`) et dans les **deux** sens.
Chaque ligne ne rend qu'**un champ désigné** — l'adresse du saut mandataire, le chemin découpé, le
nom de clef, l'identifiant d'IO — et la requête qui l'atteint porte des identifiants **partout
ailleurs** : requête d'URL, en-têtes, corps, charge de trame. ⭐ **La mutation qui rendait 0 rouge sur
128 suites** (republier `parse_url` au refus de poignée de main) **en rend 2 maintenant**.
Recouvrement fortuit **mesuré 3**, plafond **4**, re-dérivé par une **ÉGALITÉ** à chaque exécution.
**7 contre-mutations par échange**, ensembles rouges deux à deux distincts, témoin vert avec **36
lignes `CXXLD`** lues.
✅ **L'affirmation « aucune des sept ne porte un secret distribué par ce dépôt » est confirmée**, et
⭐ **deux affirmations de [`T3.90`](T3.90.md) sont corrigées au passage** : la « raison de fermeture »
publiée à WARNING est un **littéral d'une liste fermée de ce dépôt** (le membre `closeReason` de
`WebSocketFrame` n'est affecté que par neuf affectations de huit littéraux distincts ; la raison du
client sort à **DEBUG**), et deux des six sites d'identité (`OtaHttpHandler`, `RemoteUIManager`)
publient le **pair TCP** et non l'`X-Forwarded-For`. ⭐ **Les deux corrections ont été re-vérifiées
aux sources à la revue de merge, elles sont exactes, et [`T3.90`](T3.90.md) a été corrigée.**
⛔ **Ce que le filet couvre : 10 des 16 sites.** Les six autres tiennent par argument, et l'un
d'eux — l'étranglement du transport websocket — a été éprouvé par mutation à la revue : élargi à
la ligne `x-forwarded-for` brute il rend **0 rouge** dans la suite neuve. Il rougit
`core/JsonApiThrottleIdentity_test`, mais sur le **seau d'étranglement**, pas sur le journal.
⛔ **Non fermé, et nommé** : le site de la famille d'identité dans `trackPerIpCap()`, les deux sites
RemoteUI, les deux lignes d'étranglement du transport websocket et l'identifiant de scénario de
`JsonApi.cpp` ne sont atteints par **aucun** cas — ils tiennent par argument, pas par mesure.

### ✅ [F-HTTPIN-5] FERMÉ par [`T3.97`](T3.97.md) — trois sites post-authentification republiaient une valeur du corps d'une requête, dont un à un niveau imprimé par défaut

Les deux premiers sont **mesurés par la revue de merge** de [`T3.92`](T3.92.md), qui a invalidé
l'affirmation « aucun autre site ne republie un corps de l'API JSON » ; le troisième a été trouvé en
écrivant [`T3.93`](T3.93.md). Les trois sont **vérifiés aux sources et dans un journal réel** par
[`T3.93`](T3.93.md), et **fichés sans être corrigés**.

⛔ **`JsonApi.cpp:1318` (`decodeSetState`) republie la valeur de `set_state` VERBATIM à WARNING.** Le
repli de `Logger::maxLevelPrintable()` est `LOG_LEVEL_INFO` = 4, donc la ligne est **imprimée sans
que personne n'ait rien allumé** — mesuré, lu tel quel dans le journal de
`core/IncomingLogStockLevel_test` : `set_state refused for io … the value ends on its separator
("etat ")`. Or `set_state` écrit **n'importe quelle** valeur de **n'importe quel** IO : c'est la même
forme que le `set_param` fermé par [`T3.92`](T3.92.md) — le mot « password » y est la **valeur** de
`param`, le secret voyage sous `value` — sauf qu'ici la valeur sort **après** le réducteur, donc hors
de sa portée, et **un cran plus bas dans l'échelle des niveaux**.

`JsonApiHandlerHttp.cpp:1314` publie `jsonParam["pic_uid"]` à DEBUG, donc pas sur un boîtier neuf.

⚠️ **`JsonApi.cpp:2487` écrit `cout << "Adding timerange: " << p.toString() << endl`** : un `cout`
**brut**, hors du journal, donc **hors de tout filtre de niveau** — `debug_level` n'a aucune prise
dessus. Il republie l'objet décodé de chaque plage horaire envoyée par un client.

⚠️ **L'arbitrage n'est pas évident** : la première ligne existe parce que la valeur se termine sur son
séparateur, et un opérateur a besoin de voir la valeur fautive. La forme qui garde les deux est celle
de [`T3.92`](T3.92.md) : publier la **forme** (longueur, dernier octet nommé), pas les octets.
⭐ **Le harnais existe et il est neuf** : `tests/core/IncomingLogStockLevel_test.cpp` atteint déjà le
premier site par une vraie requête sur la socket, et son foin est déjà « ce qu'un boîtier neuf
imprime ». Les trois sites courent **après** `checkCredentials()`.

⭐ **Fermé, et les trois niveaux sont MESURÉS** : `decodeSetState` à **WARNING = 3**, donc imprimé
sans que personne n'allume rien ; `pic_uid` à **DEBUG = 5**, donc jamais ; et le `cout` de
`buildJsonSetTimerange` **hors de l'échelle** — aucun `debug_level` n'a prise dessus. Les trois
courent **après** `checkCredentials()`, et la fiche le dit au lieu de gonfler sa sévérité.

**L'arbitrage a été tranché par la mesure** : la ligne de `set_state` existait pour montrer la
valeur fautive, elle publie désormais la **forme** — `(22B, ends on SP) #526a86df` : la longueur, le
séparateur **nommé** dans le vocabulaire fermé de `Utils::BLANK_CHARS`, et l'empreinte salée de
`T3.92`. Contre-mutation qui retire la forme ⇒ **2 rouges** : le diagnostic est tenu par des
assertions. L'identifiant d'IO reste en clair parce que `get_io()` vient de le retrouver — c'est un
nom de la configuration, pas une chaîne inventée par le client.

⛔⭐ **Le `cout` nu publiait bien plus que « jour et heures »** : `decodeJsonObject()` recopie
**toutes** les clefs de l'objet dans `Params` et `Params::toString()` les rend toutes — ⛔ **18
octets sur 18** d'une clef inventée récupérés dans le foin « boîtier neuf » (~~20 sur 20~~ : chiffre
**corrigé et re-mesuré par la revue de merge**, l'aiguille fait 18 octets ; et **19 sur 19** d'une
seconde valeur plantée sous `start_hour`). Il est devenu un `cDebugDom`
qui dit enfin **sur quel IO** la plage atterrit et ne publie que les six bornes, bornées.
⭐ **Et convertir ne suffisait pas** : `Params::toString()` est multi-lignes, les lignes de
continuation n'ont **aucun marqueur de niveau**, donc le rendu **sur une seule ligne** est porteur
(mutation ⇒ 2 rouges). `pic_uid` est laissé tel quel — il **est** le diagnostic, il n'est pas un
secret que ce dépôt distribue — et tenu par un cas qui mesure son **niveau** (mutation vers WARNING
⇒ 1 rouge).

⭐ **Le seul trou déclaré a été FERMÉ EN REVUE DE MERGE.** Le plafond de 8 octets posé sur chaque
borne d'horaire n'était tenu par **rien** — la mutation qui les déborne rendait **0 rouge sur 131
suites** — et la cause était structurelle : la ligne est à DEBUG, or le foin que toutes les autres
bornes de cette suite mesurent est le journal **privé de ses lignes `[DBG]`**. Un cas neuf lit le
journal **complet** et exige les deux moitiés (valeur coupée à son plafond **avec** son marqueur de
troncature, et aucun préfixe plus long) ⇒ la mutation rend désormais **1 rouge**, et la même sur
`start_hour` seule aussi.

⚠️ **Ce que la fermeture laisse derrière elle**, confirmé par contre-mutation en revue : l'empreinte
de `set_state` n'est tenue par **aucun** cas (la rendre indépendante de la valeur ⇒ 0 rouge), et le
séparateur nommé n'est asseré que pour `SP` (effondrer le vocabulaire de `blankName` ⇒ 0 rouge).
Ni l'un ni l'autre ne publie d'octet du client. Voir [`T3.97`](T3.97.md) §7.

### ✅ [F-LOGRAW-1] `ExternProc` relaie la sortie de ses sept familles de sidecars hors de tout journal — **FERMÉ pour le relais** par [`T3.101`](T3.101.md), **réduit** avant lui par [`T3.102`](T3.102.md)

Relevé par le **recensement des écritures nues** de [`T3.97`](T3.97.md) §3, qui a balayé tout `src/`
suivi par git, commentaires et littéraux retirés, arbres vendorés exclus : ⛔ **13** écritures nues
sur le chemin d'exécution du serveur (~~19~~ : recompté de zéro en revue de merge, la liste
d'exclusion oubliait `uri_parser`, dont les 6 `printf` sont des littéraux fixes), dont **10**
publient une donnée d'exécution — **ce sous-total, lui, est confirmé**. **Une seule** était
sur un chemin d'API et `T3.97` l'a fermée. Des neuf restantes, **2** sont le relais d'`ExternProc`
(`std::cout << process_stdout.substr(...)`, et l'équivalent sur `stderr`), **4** sont dans
`Lua_stackDump()`, et **3** sont des messages de `ConfigStore`.

✅ **`Lua_stackDump()` : SUPPRIMÉE par [`T3.102`](T3.102.md)**, et les quatre vérifications y ont été
**refaites** — dont la décisive, mesurée cette fois contre le `lua.h` du **LuaJIT 2.1.0-beta3**
réellement lié : ses **cinq** types de rappel sont `lua_CFunction`, `lua_Reader`, `lua_Writer`,
`lua_Alloc` et `lua_Hook`, et **aucun** n'a la forme `void (*)(lua_State *)`. Un cinquième chemin a
été cherché (macro, `#ifdef`, appel commenté, script, `dlsym` sous le `-rdynamic` du serveur) :
**aucun**.

⛔ **Deux chiffres de cette fiche étaient faux et `T3.102` les corrige.** (1) La fonction portait
**six** écritures nues, pas quatre — deux sont des littéraux fixes (le séparateur et le `\n`) ;
le total de la famille baisse donc de 6, le sous-total « publiant une donnée » de 4. (2) Le **13** et
le **10** ci-dessus sont ceux d'**avant** le correctif de `T3.97`, qui a lui-même retiré un site :
sur l'arbre livré la famille comptait **12** et le sous-total **9**. Recompté à la même convention
(qui reproduit exactement le `2` de `Logger.cpp` et le `238` des huit points d'entrée), il reste
après `T3.102` **6** sites, dont **5** publiant une donnée d'exécution — les 2 d'`ExternProc` et
les 3 de `ConfigStore`. ⭐ **`LuaScript/` est à zéro.**

⛔ **Et ces quatre sites n'étaient pas dans le binaire `calaos_server`** : `ScriptBindings.cpp` est
dans `calaos_script_SOURCES`, jamais dans `calaos_server_SOURCES` (mesuré au `nm`). Le classement
« chemin d'exécution du serveur » est juste opérationnellement — le serveur lance ce sidecar par
exécution de script de règle — et faux au binaire près. ⭐ Ironie utile : si la fonction avait eu un
appelant, ses `printf` seraient sortis par le relais d'`ExternProc`, c'est-à-dire par `T3.101`.
⚠️ **`ConfigStore`** : « au démarrage » est vrai, mais **par les gardes de ses appelants** et non par
la position du site — `flushConfigErrors` est appelée de six endroits, et les quatre qui passent le
drapeau qui la fait écrire sont tous gardés « une fois par processus ».

⛔ **Ce qui rend le relais différent des huit autres** : ce qui le traverse n'est pas décidé là mais
dans **sept** familles de sidecars — dont le pont **lua**, où un `ExternProcServer` est créé **par
exécution de script de règle**, donc sur un chemin chaud —, le seul filtre de niveau qui s'applique est celui **de l'enfant**, et une
écriture nue côté enfant ne rencontre rien du tout. Le relais efface aussi le domaine et le niveau :
une ligne de pilote arrive dans le journal du serveur sans marqueur.

⚠️ **C'est un arbitrage, pas un correctif** : le relais est ce qui rend un sidecar débogable, et le
fermer touche les six pilotes. Forme candidate : passer par le journal du parent sur le domaine
`process`, qui existe déjà et est utilisé **deux lignes plus haut**. ⭐ **Le modèle est déjà dans
l'arbre** : `McpServerManager::flushStreamBuffer()` fait le même découpage ligne à ligne pour le
sidecar MCP, mais passe par le journal **et** caviarde ce qui ressemble à un jeton.

✅ **Fermé pour le relais le 2026-09-06 par [`T3.101`](T3.101.md)**, sur l'arbitrage utilisateur du
même jour : chaque ligne d'un sidecar est une ligne de journal DEBUG du domaine `process`, nommée par
le **préfixe** de son `ExternProcServer` — ce qui distingue les deux sidecars KNX, que le
`--namespace` ne distinguait pas ([`T3.84`](T3.84.md)). ⛔ **La famille passe de 5 sites publiant une
donnée d'exécution à 3**, les trois de `ConfigStore`.

⭐ **Le recensement des sept familles a été fait, binaire par binaire, et il corrige la fiche sur
trois points.** (1) ⛔ **Aucun** des huit sidecars livrés (les sept familles plus Roon) n'écrit sur sa
sortie d'erreur : la moitié `stderr` du relais ne portait rien, et reste un point d'extension, pas un
chemin chaud. (2) ⭐ **Les six sidecars C++ sortent 6 lignes NUES de `ConfigStore`, identiques au
niveau 4 et au niveau 5** — la phrase « une écriture nue côté enfant ne rencontre rien du tout » n'est
plus une déduction, elle est mesurée. (3) Les deux sidecars Python sortent 4 lignes de journal
**portant des séquences ANSI** (le parent leur passe `CALAOS_FORCE_COLOR`) : une ligne relayée peut
donc porter des octets d'échappement.

⭐ **Une conséquence non prévue, et c'est la mesure la plus honnête du coût** :
`core/MqttSidecarConfigWait_test` relisait **vraiment** le relais nu pour lire le journal du vrai
`calaos_mqtt`, et **4 de ses cas sont tombés** au premier tour vert. Réparé par
`debug_domains = process:5` dans son `main()` — **jamais** `debug_level`, qui est ce que le parent
donne à l'**enfant** et qui remettrait les lignes DEBUG du sidecar dans le journal que ses assertions
de secret relisent.

⛔ **Ce qui reste ouvert et qui n'était pas fiché** : `~ExternProcServer` ferme `pipe` **sans vider**
`process_stdout` et **ne ferme pas `pipe_stderr`** du tout ⇒ [`T3.110`](T3.110.md). Ce n'est pas la
fenêtre du plantage — celle-là est fermée, l'`EndEvent` du tuyau publie les derniers mots d'un enfant
mort en plein mot — mais celle où le **parent** part le premier, et le chemin chaud y est `lua`.

### ✅ [F-LOGSECRET-2] FERMÉ par [`T3.85`](T3.85.md) — un chemin d'erreur republiait à **ERROR** une chaîne fabriquée par le sidecar

`IO/Reolink/ReolinkCtrl.cpp` recopie `p["message"]` dans `cErrorDom("reolink")`. Le sidecar y met
`f"Failed to connect to camera {hostname}: {str(e)}"`, c'est-à-dire **le texte d'une exception de la
bibliothèque caméra tierce**, levée par l'appel qui vient d'essayer de s'authentifier **avec les
identifiants de la caméra**. Rien dans l'arbre ne contraint ce texte — il traverse une dépendance —
et ⛔ **il sort à ERROR, donc imprimé sur une installation de série**, un cran **au-dessus** de la
fuite que [`T3.81`](T3.81.md) vient de fermer. ⭐ C'est le cas concret de la phrase que T3.79 et T3.81
déclarent toutes deux à 0 rouge : *tant qu'un chemin touche la donnée, tout site qui la touche est un
site de fuite.* ⚠️ Le coût en diagnostic est réel — c'est *le* message quand une caméra ne répond
pas — donc l'arbitrage est à faire, et la forme qui garde les deux est que **le sidecar cesse
d'emballer `str(e)`**. ⛔ Le harnais existe : `tests/core/ExternProcPayloadSecret_test.cpp` a déjà un
vrai `ReolinkCtrl` et un pair sur sa socket ; la garde serait **d'exécution**, pas orthographique.

✅ **Fermé le 2026-09-05.** Le serveur ne republie plus rien de ce que le sidecar écrit : il publie
un **code de sa propre liste** (`ReolinkWire::isKnownErrorCode()`), une **caméra que ce contrôleur a
lui-même enregistrée**, et le **nombre d'octets écartés**. ⭐ **Recopier le code que l'émetteur envoie
ne suffisait pas, et c'est mesuré** : la contre-mutation qui accepte tout code donne **2 rouges** —
sinon l'émetteur choisit toujours ce que le journal dit. Le sidecar cesse d'emballer `str(e)` dans
la trame **et** sur son propre stdout, que le serveur réinjecte dans le sien.
⭐ **Recensement des six points d'émission de `message` côté sidecar : un seul porte du texte libre**
(`ExternProcReolink_main.py:1507`), et c'est celui que le serveur imprimait ; les cinq autres sont
des littéraux ou des gabarits écrits ici. Épinglé par `core/SidecarErrorSecret_test` à travers un
vrai `ReolinkCtrl` et un pair sur sa socket unix, avec le niveau par défaut **mesuré** dans un
enfant forké (`reolink` imprime à ERROR, pas à DEBUG).
⛔ **Non fermé, et nommé** : `ReolinkCtrl.cpp:79` publie `event_data` entier à DEBUG, et le
changement du sidecar Python n'est épinglé par aucun test (`tests/python/` ne peut pas importer ce
module sans `reolink_aio`, et un `importskip` serait un vert muet).

⛔⭐ **La revue de merge a mesuré que le capteur dépendait d'une LONGUEUR, et l'a corrigé.** Les
assertions cherchaient le texte entier, le mot de passe, le code et le nom d'hôte fabriqués — donc
seulement les tranches pour lesquelles elles étaient orthographiées. Au site de production, la ligne
d'erreur gardant tous ses champs sûrs et gagnant `head=` `p["message"].substr(0, 24)` ⇒ **0 rouge**
(elle emporte le compte que le texte nomme), et `tail=` `substr(size - 24)` ⇒ **0 rouge** (elle
emporte les identifiants que l'URL encode en pourcents, que la recherche du mot de passe en clair ne
voit pas). Les cas bornent désormais **la plus longue suite d'octets** du texte du sidecar que le
journal rend, recouvrement fortuit **mesuré 12** contre un plafond de **16** ; les deux mêmes
échanges rendent **1 rouge** chacun. Même classe que le vice mesuré au merge de
[`T3.83`](T3.83.md).

⛔⭐ **Le même secret partait encore, un site plus loin, et la revue l'a fermé** : la boucle de
reconnexion du sidecar écrivait `{str(e)}` de l'exception levée par l'appel qui **se
ré-authentifie avec les mêmes identifiants**, à **ERROR**, sur son propre stdout que le serveur
réinjecte dans le sien. Le correctif d'origine n'avait fermé que le chemin de **première**
connexion. La classification (`classify_connection_error`) qui accompagne la ligne est écrite ici,
donc rien du diagnostic ne part avec le texte.

⚠️ **Nommé, non corrigé, aucun ticket ouvert** : douze autres `str(e)` subsistent dans les lignes de
journal du sidecar Reolink (surveillance, rappels d'événements, ordonnancement), dont plusieurs à
ERROR. Ils ne sont pas sur un appel qui vient de s'authentifier, mais le texte reste celui d'une
dépendance et il atterrit dans le journal du serveur.

### ✅ [F-LOGSECRET-3] FERMÉ par [`T3.86`](T3.86.md) — quatre contrôleurs republiaient la **trame brute entière** de leur sidecar, à un niveau **imprimé par défaut**

Trouvé en balayant les sept contrôleurs pour [`T3.85`](T3.85.md) §3. Sur leur chemin d'échec de
parsage, `IO/Mqtt/MqttCtrl.cpp:44`, `IO/KNX/KNXCtrl.cpp:325`, `LuaScript/ScriptExec.cpp:104` et
`IO/OneWire/OWCtrl.cpp:91` écrivent la charge utile entrante **entière**. ⛔ **À WARNING, donc
imprimé sur une installation de série** (`LOG_LEVEL_WARNING` = 3 passe le repli `LOG_LEVEL_INFO` = 4
de `Logger::maxLevelPrintable()`) : **au-dessus** du niveau de la fuite que [`T3.81`](T3.81.md) a
fermée, et au même niveau d'exposition que celle de [`T3.85`](T3.85.md).
⚠️ **Le cas OneWire ne cite même pas la trame** : la ligne streame `e.what()`, mais l'exception a été
construite deux lignes plus haut en y concaténant `msg` — un capteur orthographié sur `<< msg` y
serait aveugle, exactement la classe que T3.85 §6 a mesurée ailleurs.
⭐ **Le précédent existe deux fois dans l'arbre** : `IO/Wago/WagoMap.cpp:194` ne publie rien de la
trame, et `IO/Reolink/ReolinkCtrl.cpp:53` publie `msg.size()`. ⚠️ Le coût en diagnostic est réel — une
trame illisible est le moment où l'on veut voir les octets — donc l'arbitrage est à faire.
⚠️ Voisin plus bas, même forme : `IO/Reolink/ReolinkCtrl.cpp:79` publie `event_data` entier à DEBUG.

**Fermé** : les cinq sites publient le domaine du contrôleur, le fait qu'une trame n'a pas pu être
lue, et **sa taille**. ⭐ **Ce que porte chaque trame a été mesuré avant de choisir** : le `payload`
MQTT et le `set_param` Lua peuvent porter un secret, **KNX et OneWire non** — ils sont fermés sur la
**provenance**, la ligne n'étant atteinte que par des octets qui ne sont pas ceux que ce dépôt
fabrique. ⛔⭐ **Le cas OneWire fuyait deux fois** : la concaténation, et le message de `nlohmann`
lui-même, qui cite le jeton sur lequel il a buté (**49 octets, dont l'identifiant encodé en
pourcents, sans que rien n'ait été concaténé** — recompté à la revue, la fiche annonçait 46). ⛔ **Un cinquième site absent de la fiche d'origine**
a été trouvé et fermé : `IO/Mqtt/MqttCtrl.cpp:131`, le payload entier **sans domaine**, sur le chemin
de **lecture** de tout IO MQTT qui nomme un `path`. Garde d'exécution
(`tests/core/ControllerFrameSecret_test.cpp`), niveau mesuré dans un enfant forké, assertions bornant
**la plus longue suite d'octets rendue** et aiguilles présentes **en clair et encodées**.

---

### ✅ [F-LOGSECRET-4] FERMÉ par [`T3.89`](T3.89.md) — neuf publications de `e.what()` dont le message **cite l'entrée refusée**, à un niveau imprimé par défaut

Recensé en fermant [`T3.86`](T3.86.md), dont le site OneWire était exactement ce cas. Le mécanisme
n'est **pas** lisible dans le fichier fautif : rien n'y est concaténé, c'est la **bibliothèque** qui
met l'entrée dans son message. Mesuré sur `nlohmann` :

```
[json.exception.parse_error.101] ... invalid string: forbidden character after backslash;
last read: '"nfs://t5:capteur%20jeton%2008b7f5@169.254.9.6/\q'
```

⇒ le **jeton de chaîne entier**, identifiant encodé en pourcents compris.

Comptage sur l'arbre, hors bibliothèques vendorisées : **35** publications de `e.what()`,
⛔ **toutes à un niveau imprimé par défaut** (`cWarning`, `cError`, `cCritical` ; **aucune** à
DEBUG). **16** sont dans un `catch` dont le `try` parse ou indexe une donnée d'exécution ; une est
fermée par `T3.86`, une appartient déjà à [`T3.87`](T3.87.md)
(`IPCam/SynoSurveillanceStation.cpp:302`, qui publie en outre `data` entier). **Restent 14** :

`CalaosConfig.cpp:539` · `RemoteUI/FirmwareManifest.cpp:116` ·
`RemoteUI/RemoteUIWebSocketHandler.cpp:151,219` · `IO/JsonPath.h:229,242` ·
`IO/Web/WebCtrl.cpp:190,291,297` · `Audio/AVRRose.cpp:140,363,386,405` ·
`Audio/AVRRoseNotifServer.cpp:226`.

⭐ **Le plus lourd est `Audio/AVRRose.cpp:140`** : le `catch` du parsage de la réponse
`device_connected`, **le corps d'où sort `deviceRoseToken`** — le jeton que [`T3.81`](T3.81.md) a
retiré du journal et que [`T3.83`](T3.83.md) a retiré du transport. ⚠️ La différence avec les fuites
déjà fermées est qu'il faut **un corps mal formé** pour l'atteindre : la gravité tient au fait que
c'est précisément le chemin qu'un pair qui déraille emprunte.

ℹ️ Les **11** sites de `HistLogger.cpp` sont hors classe : exceptions sqlite sur des requêtes
**paramétrées**, le message ne porte aucune valeur liée. Et **19** autres `.what()` de l'arbre sont
des `uvw::ErrorEvent` — une chaîne d'erreur libuv, sans donnée d'appelant.

⛔⭐ **AFFINÉ À LA REVUE DU MERGE — le critère qui compte n'est pas « parse ou indexe » mais
« la bibliothèque peut-elle remettre des octets de l'entrée dans son message »**, et **seul le
`parse_error` de `nlohmann` le fait** : son `out_of_range.403` cite la **clef** cherchée (donc le
chemin configuré, pas la charge utile), son `type_error` ne cite **rien**, et une
`pugi::xpath_exception` cite l'**expression**, pas le document. Reclassés sous ce critère, les 14
se coupent en deux :

- **9 portent réellement le risque** — leur `try` contient un `Json::parse()` d'une donnée
  d'exécution : `Audio/AVRRose.cpp:140,363,386,405` · `Audio/AVRRoseNotifServer.cpp:226` ·
  `RemoteUI/RemoteUIWebSocketHandler.cpp:151` · `IO/Web/WebCtrl.cpp:190` ·
  `RemoteUI/FirmwareManifest.cpp:116` · `CalaosConfig.cpp:539`.
- **5 ne le portent pas** : `IO/JsonPath.h:229,242` (`at()` sur une clef venant du chemin
  configuré) · `IO/Web/WebCtrl.cpp:291,297` (pugixml, l'expression) ·
  `RemoteUI/RemoteUIWebSocketHandler.cpp:219` (`get<int>()`, message sans donnée).

⚠️ **Le message de `nlohmann` est vérifié à la source par la revue**, sur la trame exacte de la
suite : `last read` rend **49 octets** de l'entrée, l'identifiant **encodé en pourcents** compris, et
⛔ **une recherche de l'aiguille en clair y reste aveugle**.

ℹ️ Trois `catch` fourre-tout ont été examinés et **écartés** — `RemoteUI/OtaHttpHandler.cpp:112`,
`RemoteUI/RemoteUIProvisioningHandler.cpp:80`, `RemoteUI/RemoteUIWebSocketHandler.cpp:180` : leur
`try` traite bien de la donnée d'exécution, mais le `Json::parse()` du corps est capté **plus bas**
sans publier `e.what()`, et ce qui remonte jusqu'à eux ne cite pas l'entrée.

⇒ **La classe reste réelle et sa tête l'est aussi** (`Audio/AVRRose.cpp:140`), mais elle compte
**9 sites**, pas 14. **Ticket proposé : [`T3.89`](T3.89.md)** — la garde y est d'exécution
(remettre un corps mal formé au pair `AVRRose` et relire `std::cout`), le harnais des trois derniers
correctifs s'y transpose, et l'arbitrage y est le même qu'ici : la taille et le domaine survivent,
le message de la bibliothèque non.

**Fermé** : les neuf sites publient le **code d'erreur**, l'**octet où l'analyseur s'est arrêté** et
la **taille** du document, par un point de passage unique, `Utils::jsonErrorForLog()`
(`src/lib/StringUtils.cpp`, à côté d'`urlForLog`). ⭐ **La bibliothèque expose bien la position et le
type séparément du texte cité** — `nlohmann::json::parse_error` porte `.byte` et `.id`, et
`json::exception` porte `.id` pour toutes ses sous-classes : le diagnostic survit **entier** sans un
octet de l'entrée. ⭐ **Ce que chaque document peut porter a été mesuré avant de choisir** : quatre
sites portent un secret ou des octets choisis par un tiers (le jeton de l'amplificateur, le port de
notification **ouvert à tout le réseau local**, la trame websocket du panneau déporté dont la classe
mère lit `cn_user`/`cn_pass`, la réponse d'un service web tiers), et **cinq sont fermés sur la seule
provenance** — dont ⚠️ **le cache d'états, où la fiche supposait à tort des identifiants** : vérifié
aux deux écrivains et à leurs **six** sites d'appel, il ne porte que des valeurs d'IO et de la
comptabilité interne. ⚠️ Une réserve mesurée à la revue de merge : un IO `Internal` de type chaîne
avec `save="true"` y range la valeur qu'une règle ou un script Lua lui a donnée.
⭐ **L'arbre avait déjà tranché la question une fois** : `JsonApiHandlerWS::processApi()` parse avec
la forme **non lançante** et n'écrit que `Error loading json` à DEBUG ; la sous-classe
`RemoteUIWebSocketHandler::processApi()` ajoutait **devant** un parse lançant et en publiait le
message à WARNING.
Garde d'exécution (`tests/core/ParseErrorSecret_test.cpp`, **22 cas**) : les quatre sites de
l'amplificateur sont nourris par un **vrai pair TLS** levé par la suite, le site de notification par
un client TCP sur le port d'écoute, le site websocket par l'appel de `WebSocket.cpp` mot pour mot,
les trois sites de fichier par un vrai fichier — le cache d'états par le **constructeur** de
`Config`. Niveau mesuré dans un enfant forké, assertions bornant **la plus longue suite d'octets
rendue** (33 à 53 sur `master`) et aiguilles présentes **en clair et encodées**, la recherche du
clair restant ⛔ **verte sur les neuf**.

⛔ **Ce que le correctif ne ferme PAS** : la **classe**. Rien n'empêche un dixième `<< e.what()` —
voir `F-LOGSECRET-6`.

### ⛔ [F-LOGSECRET-6] Rien n'interdit un dixième `e.what()` dans une macro de journal — ticket proposé `T3.91`

Relevé en fermant [`T3.89`](T3.89.md). Les neuf sites passent désormais par
`Utils::jsonErrorForLog()`, qui a sa propre garde d'exécution, mais **le point de passage n'est pas
obligatoire** : un `catch` écrit demain qui streame `e.what()` rouvre la classe en une ligne, et
aucune assertion de l'arbre ne la verrait — c'est précisément le mode d'échec de `F-LOGSECRET-4`,
dont le mécanisme n'était lisible dans aucun des fichiers fautifs.

Ce qui la fermerait est une **sonde statique** de la forme de `tests/check-test-deps.sh`
([`T3.36`](T3.36.md)) : interdire `.what()` à l'intérieur d'une macro de journal hors d'une liste de
sites **énumérés**. ⚠️ Elle porterait sur les **27** `e.what()` de l'arbre (recomptés à la revue de merge : **47**
occurrences de `<ident>.what()` sous `src/` hors `json.hpp`, `exprtk/` et `sqlite_modern_cpp/`, soit
19 `ev`, 27 `e`, 1 `event`), dont les onze de
`HistLogger.cpp` (sqlite, requêtes paramétrées) et ceux de `src/bin/calaos_server/Http*` que
[`T3.90`](T3.90.md) réécrit.

**Aucun ticket ouvert.**

### ✅ [F-LOGSECRET-7] Le plafond des capteurs à borne est un nombre écrit à la main que rien ne re-mesure — **FERMÉ** par [`T3.94`](T3.94.md)

Mesuré à la revue de merge de [`T3.89`](T3.89.md). **Trois** suites bornent la plus longue suite
d'octets de l'entrée que le journal rend — `core/ControllerFrameSecret_test` (**10**),
`core/SidecarErrorSecret_test` (**16**), `core/ParseErrorSecret_test` (**10**). C'est la bonne
forme : un capteur orthographié sur la valeur d'un secret est aveugle par construction quand c'est
la bibliothèque qui décide où elle coupe. Mais le plafond est une **constante**, justifiée par un
recouvrement fortuit **mesuré une fois, à la main**, et rien ne le re-mesure.

⛔ **La fenêtre est ouverte et rien ne la montre** : neuf octets de l'aiguille **encodée en
pourcents** publiés au site de notification de l'amplificateur — joignable **sans authentification**
depuis tout le réseau local — laissent **128 suites sur 128 vertes**. ⛔ **Et le recouvrement dépend
de la fixture** : la même revue a trouvé les neuf documents de `T3.89` partageant encore
`Libelle":"`, **exactement dix octets**, soit la borne elle-même — une modification de fixture peut
élargir la fenêtre aveugle sans qu'aucune assertion ne bouge.

✅ **FERMÉ par [`T3.94`](T3.94.md).** Chaque suite mesure désormais, à chaque exécution, la plus
longue suite d'octets que son propre journal rend sur **toutes** les sondes que le plafond couvre, et
épingle `plafond == maximum + 1` ; un second cas borne la plus longue suite commune à deux documents
de la fixture. **Recouvrement réel re-mesuré** : **8** (`manifest`), **7** (`payload`), **12**
(`192.168.7.51`) ⇒ plafonds re-dérivés **9**, **8**, **13** — les trois étaient trop larges de 2, 3
et 4 octets. ⭐ **La mutation de 9 octets au site de notification, qui donnait 0 rouge sur 128,
rougit maintenant** ce site et sa calibration. ⭐ **Et une dérive à la BAISSE** (la ligne mqtt cesse
de nommer `payload`, aucun octet de secret publié) rougit **un seul cas dans tout l'arbre, la
calibration** — rien d'autre ne la voit.

⚠️ **Ce qui reste ouvert** : la fenêtre est resserrée, pas fermée (8, 7, 12 octets passent encore) ;
un capteur à borne ne peut pas descendre sous le bruit que le journal a le droit d'écrire. Et le
plafond reste **global à la suite**, pas par site.

⛔⭐ **Corrigé à la revue de merge** : le bornage « structurel » de l'aléa donné à l'appui de la
stabilité était faux. La plus longue suite hexadécimale d'une fixture ne fait pas 6 caractères mais
**8** — le `%20` de l'encodage en pourcents précède chaque jeton de 6 hex (`207f31c9`, `208f3a2c`…).
⚠️ `ControllerFrameSecret` (recouvrement 7, plafond 8) n'est donc **pas** hors d'atteinte par
construction : un uuid de socket de 32 hex rendrait l'égalité rouge avec une probabilité ≈ 2·10⁻⁸
par exécution. Stable en pratique (90 exécutions + 6 `make check`), **pas** par construction.

### ✅ [F-LOGSECRET-8] Trois AUTRES capteurs à borne, même défaut, jamais recensés — **FERMÉ** par [`T3.96`](T3.96.md)

Balayé en fermant [`T3.94`](T3.94.md) : `F-LOGSECRET-7` recensait **trois** capteurs à borne là où
`tests/` en porte **six**. Les trois autres portent **13 assertions bornées** et un plafond écrit à la
main que rien ne re-mesure : ⛔ `core/HttpRequestLogSecret_test.cpp` (`kMaxEcho` = **8**, **7**
assertions, sur le chemin HTTP **entrant**) · `UrlDownloaderLogUrl_test.cpp` (`kMaxUrlEcho` = **8**,
4) · `UrlDownloaderLogSecret_test.cpp` (`kMaxBodyEcho` = **12**, 2).

⚠️ **Une réserve mesurée d'avance** : `UrlDownloaderLogUrl_test` fabrique des URL portant un port
**éphémère**, donc du matériau aléatoire **dans la sonde** — ce que les trois suites de `T3.94`
n'avaient pas. L'égalité peut y être instable et sa stabilité doit être mesurée avant d'être écrite.

⛔⭐ **RECENSEMENT CORRIGÉ ET AGGRAVÉ À LA REVUE DE [`T3.92`](T3.92.md) (2026-09-05).** Ce ticket
touchait `HttpRequestLogSecret_test` et **ne l'a pas calibré** : le plafond y reste écrit à la main,
et la suite passe de **7** à **13** assertions bornées — plus que les deux autres réunies, sur le
chemin HTTP **entrant**. Le total du finding est donc **19** assertions, pas 13.

⭐ **Et l'angle mort n'est plus une déduction, il est mesuré sur ce capteur-là.** Borne abaissée à 1 :
recouvrement fortuit maximum **4** sur toute la suite (10 cas rouges sur 17, chacun disant son
chiffre) ⇒ un plafond re-dérivé vaudrait **5**, il vaut **8**. Contre-mutation qui **ajoute** sept
octets de chaque valeur retenue à la ligne, tous les littéraux de forme conservés : **0 rouge dans la
suite à socket**, y compris sur le téléversement **non authentifié** qui rend alors sept octets du
`mcp_token` émis par ce serveur. Les deux seuls rouges de l'arbre viennent de la suite à appel direct
et **par accident de fixture** (deux valeurs de 5 octets tiennent sous la fenêtre).

⛔⭐ **RECENSEMENT REFAIT ET CORRIGÉ UNE SECONDE FOIS À LA LIVRAISON DE [`T3.96`](T3.96.md)
(2026-09-05).** Le parc ne porte pas six capteurs à borne mais **huit**, et pas 19 assertions bornées
mais **29** : `T3.88` et `T3.93` en ont ajouté deux **nées calibrées** (`core/DriverAnswerSecret`
plafond 5, `core/IncomingLogStockLevel` plafond 4), et `HttpRequestLogSecret` en porte **12** et non
13. ⇒ **5 calibrées / 3 aveugles**, les trois aveugles portant **18 des 29** assertions.

**Recouvrements re-mesurés et fenêtres, sur l'arbre corrigé** : `HttpRequestLogSecret` plafond **8**,
recouvrement **4** (`conn`) ⇒ fenêtre **3** · `UrlDownloaderLogUrl` **8** / **3** (`er:`) ⇒ **4** ·
`UrlDownloaderLogSecret` **12** / **3** (`ose`) ⇒ **8**. Démontré par trois mutations publiant
7, 7 et 11 octets : **0 rouge** dans les trois suites avant la calibration, **4, 3 et 3** cas après.

⛔ **La réserve de la fiche visait le mauvais coupable.** Le port éphémère vit dans l'**autorité** de
l'URL, que les sondes ne mesurent pas. Ce qui rendait l'égalité instable, c'est que les **aiguilles se
terminaient par une queue hexadécimale** alors que le journal publie ses **propres** identifiants dans
cet alphabet (adresse d'objet, empreinte de requête) : **1 rouge sur 200** mesuré. Fixture dé-hexée
⇒ **500 exécutions du cas de calibration, 120 des binaires, 5 `make check` : aucune dérive.**

**Fermé.** Ticket ouvert en sortie : `F-LOGSECRET-9` (`T3.99` proposé).

### ✅ [F-LOGSECRET-9] Rien ne gardait la propriété de fixture dont dépend la stabilité des égalités de plafond — **FERMÉ** par [`T3.99`](T3.99.md)

Trouvé en livrant [`T3.96`](T3.96.md). Une égalité `plafond == recouvrement + 1` n'est stable que si
l'aléa que le journal produit lui-même ne peut pas apparier une aiguille : concrètement, si la plus
longue suite **purement hexadécimale** de chaque sonde reste **sous** le recouvrement déterministe.
C'est vrai des huit suites aujourd'hui — mesuré **4, 3 et 3** pour les trois calibrées par `T3.96` —
et **rien ne le garde**.

⚠️ **Le symptôme d'une régression serait le pire à diagnostiquer** : un rouge de calibration
**intermittent**, à 5·10⁻⁴ près par exécution, sur une suite que personne n'a touchée. Simulé sur le
seul document que `T3.96` n'a pas pu dé-hexer (le jeton de 64 caractères du générateur livré) :
collision à 5 octets **7,6·10⁻⁴** par échange, à 8 **< 5·10⁻⁶** — c'est pourquoi ce document-là garde
un plafond de **8**, écrit à la main, que rien ne re-dérive (`kMaxDrawnEcho`).

⇒ Ce qu'il faudrait : `tests/check-echo-ceilings.py` sait déjà lire les fichiers ; il pourrait
exiger, pour chaque plafond épinglé, que le fichier porte un cas qui borne la plus longue suite
hexadécimale de ses sondes sous ce plafond.

⛔⭐ **RÉDUIT ET EN PARTIE FERMÉ À LA REVUE DE MERGE DE [`T3.96`](T3.96.md) (2026-09-05) — et la
propriété était FAUSSE telle qu'elle était livrée.** La portée de l'aléa n'avait été mesurée que sur
les aiguilles **littérales**. Or `longestEchoRun` mesure aussi la forme décodée en pourcents, la
forme **base64** et la forme décodée du base64 : ce sont des aiguilles à part entière. Mesuré hors du
binaire sur les quinze documents du chemin entrant, le base64 de `kRefusedContentSecret` portait
**`c290a`**, **5** octets hexadécimaux — **un de plus** que le recouvrement déterministe de 4. La
loterie n'était donc pas fermée mais réduite d'environ deux ordres de grandeur (~5·10⁻³ ⇒ ~2·10⁻⁵ par
exécution). ⭐ **C'est le même mode d'échec que la revue de `T3.94` avait déjà nommé** : *c'est le
motif d'encodage qui porte le recouvrement possible, pas le littéral.*

✅ **Fermé pour les trois suites de `T3.96`** : le littéral fautif est changé et **chacune des trois
porte un cas** qui borne, sous son plafond, la plus longue suite hexadécimale de **chaque forme
mesurée** de chaque document. Rouge sans le correctif (`actual: 5 vs 5`, l'ensemble nomme le document
et les octets).

⚠️ **Ce qui reste, et devient `T3.99`** : les **cinq** suites déjà calibrées n'ont pas ce cas.
Mesuré : `core/SidecarErrorSecret` portée **4** / plafond 13, `core/DriverAnswerSecret` **4** / 5,
`core/IncomingLogStockLevel` **3** / 4 — tenues, sans marge pour les deux dernières — et ⛔
`core/ControllerFrameSecret` **8** / **8** : la portée hexadécimale d'un littéral encodé y **égale**
le plafond, donc un appariement complet le ferait rougir. C'est exactement le « 8 » que la revue de
`T3.94` avait nommé. La forme générale du correctif reste celle envisagée ci-dessus.

✅⭐⭐ **FERMÉ par [`T3.99`](T3.99.md) (2026-09-05) — et le compte était FAUX : trois suites étaient à
marge nulle, pas une.** Re-mesuré de première main dans les binaires, borne abaissée à 1 pour que
chaque document et chaque forme disent leur run : `core/ControllerFrameSecret` **8 / 8** (confirmé),
`core/DriverAnswerSecret` **5 / 5** (annoncé 4 / 5), `core/IncomingLogStockLevel` **4 / 4** (annoncé
3 / 4), et `core/ParseErrorSecret` **8 / 9**, que le tableau d'entrée ne citait pas. ⭐ **Les deux
écarts viennent du même endroit que le trou d'origine** : la mesure portait sur le **littéral**.
`hamac 82m glaieul` porte **2** octets hexadécimaux en clair, **4** une fois url-encodé (`2082`) ou
privé de ses blancs (`ac82`) ; et en pourcents, un délimiteur json vaut **deux** caractères
hexadécimaux, donc `"code":403` vaut **cinq** (`3A403`).

✅ **Les trois marges nulles sont fermées par le document, aucun plafond touché.** Preuve que la marge
existait : un identifiant hexadécimal tiré (`#207f31c9`) **ajouté** à une ligne de refus de `MqttCtrl`
rend la calibration de `ControllerFrameSecret` **rouge** sur la fixture de `master` (`8 vs 8`, run
`207f31c9`) et **0 rouge** sur la fixture livrée.

✅ **Et la classe est fermée** : `tests/check-echo-ceilings.sh` exige de chaque plafond épinglé un cas
bornant la portée hexadécimale de **chaque forme mesurée** — sur `master` elle rend
`9 checked, 5 unheld` et nomme les cinq suites. ⭐ **Trois des quatre faux verts qu'elle admettait sont
fermés** (épinglage contre un littéral, `#if 0`, `DISABLED_`), le quatrième (`GTEST_SKIP()`) est laissé
ouvert délibérément — `REQUIRE_CURL()` en fait un usage légitime. ⭐ **Elle porte un auto-test** de dix
fichiers écrits pour être refusés, joué avant chaque balayage : la première sonde statique de `tests/`
à en avoir un.

⚠️ **Ce qui reste ouvert et n'a pas de ticket** : la marge vaut **un octet** dans six suites sur huit,
et c'est structurel — le plafond est `recouvrement + 1`, donc une portée égale au recouvrement est au
maximum admissible. Un octet de recouvrement gagné par une ligne de journal et la propriété redevient
fausse ; ce qui change, c'est qu'elle rougira au premier `make check` en nommant le document, la forme
et les octets.

⭐ **Confirmé à la relecture de merge (2026-09-05).** Trois des quatre portées re-mesurées **hors des
binaires**, en Python sur les littéraux de `master` : **8** (`207f31c9`), **5** (`3A403`), **4**
(`ac82`), plus `core/ParseErrorSecret` **8 / 9** — la correction de la fiche de sortie est exacte et
la fiche d'entrée se trompait deux fois. **Aucun des neuf plafonds n'a bougé.** La preuve de marge a
été rejouée dans les deux sens (même mutation de `src/` : 2 rouges sur la fixture de `master`, 0 sur
celle livrée), et `9 checked, 5 unheld` reproduit en pointant la sonde livrée sur `master`.
⭐ **L'auto-test de la sonde attrape ce que son balayage ne voit pas** : l'exclusion des cas
`DISABLED_` retirée laisse l'arbre livré **vert** et fait rougir le seul auto-test.
⇒ **`T3.100`** ouvert pour le seul manque qui reste : la fixture d'une suite calibrée est un objet
**contraint**, et rien ne le dit à qui y ajoutera un document.

### ⚠️ [F-LOGSECRET-5] Trois lignes du domaine `mqtt` publient encore **une valeur lue dans le payload**, à WARNING

Relevé à la revue de [`T3.86`](T3.86.md), qui les nomme sans les fermer.
`IO/Mqtt/MqttCtrl.cpp:594`, `:690` et `:746` publient `"<valeur>" read from path <chemin> is not a
number` quand la valeur trouvée à `battery_path` / `wireless_signal_path` / `uptime_path` n'est pas
un nombre. La valeur vient du payload d'un tiers, et WARNING est imprimé sur une installation de
série.

⭐ **Ce n'est pas la même classe que les cinq sites fermés** : ce qui part n'est pas la trame que le
serveur n'a **pas su lire**, mais **une feuille désignée par un chemin que l'utilisateur a écrit
lui-même** en déclarant l'IO — et la valeur est précisément ce que la ligne existe pour faire
lire. ⚠️ Mais l'argument de provenance de T3.86 vaut ici aussi : *un payload MQTT est ce qu'un
appareil tiers a publié*, et rien n'oblige un appareil à mettre un nombre sous `battery_path`.

**Aucun ticket ouvert.**

## T3.102 — voisinage de la suppression de `Lua_stackDump()` (2026-09-05)

### ⚠️ [F-DEADCFG-1] `#ifdef CALAOS_INSTALLER` dans `ScriptBindings.cpp` : une branche qui n'est définie nulle part et **ne compilerait pas** ici

Trouvée en cherchant le « cinquième chemin » de [`T3.102`](T3.102.md) — c'est-à-dire un `#ifdef` qui
aurait pu rendre `Lua_stackDump()` vivante dans une configuration de build. Elle n'en entourait pas
la fonction supprimée, mais **le corps de `Lua_print()`** :

```cpp
#ifdef CALAOS_INSTALLER
    LuaPrinter::Instance().Print(QString::fromUtf8(msg.c_str()));
#else
    cInfoDom("script.lua") << "LuaPrint: "<< msg;
#endif
```

⛔ **`CALAOS_INSTALLER` n'a qu'une seule occurrence dans tout le dépôt : celle-ci.** Ni `configure.ac`,
ni un `Makefile.am`, ni un `.m4`, ni un en-tête ne le définissent — aucune configuration de ce dépôt
n'active cette branche. ⭐ **Et elle ne le pourrait pas** : `LuaPrinter` n'existe nulle part dans
l'arbre suivi par git, et `QString` non plus — la branche appelle deux symboles absents, donc
l'activer serait une **erreur de compilation**, pas un changement de comportement.

⇒ C'est le point de partage avec le dépôt `calaos_installer` (Qt). ⭐ **Vérifié à la revue de merge,
plus supposé** : `calaos_installer` porte `LuaPrinter` dans `src/DialogScriptEditor.{h,cpp}` et une
**copie tracée** de ce fichier sous `src/common/LuaScript/`, avec son propre Lua 5.1.4 vendoré.
⚠️ **Et c'est une duplication, pas un partage** : les deux copies ont déjà divergé (LuaJIT et
`cInfoDom` ici, Lua 5.1.4 et Qt là-bas), rien ne les synchronise, et rien dans l'un ne nomme l'autre.
**Non supprimé délibérément** : le retirer casserait silencieusement un dépôt qui n'est pas celui-ci.
ℹ️ La copie de `calaos_installer` porte, elle aussi, `Lua_stackDump` en **deux** occurrences et aucun
appelant — la suppression faite ici ne peut donc rien y casser.
⚠️ **Ce que ça coûte quand même** : c'est une branche que ce dépôt ne peut ni construire ni tester,
et rien dans le fichier ne dit à qui la lit qu'elle appartient à un autre arbre. **Aucun ticket
ouvert** — la forme utile serait un commentaire d'une ligne au-dessus du `#ifdef`, pas une
suppression.

### ✅ [F-TESTDOC-1] Un commentaire de `tests/Makefile.am` annonçait l'inverse de ce que le fichier fait dix lignes plus bas — **FERMÉ** à la revue de merge de [`T3.102`](T3.102.md)

Au-dessus du bloc `LuaCalaosApi_test`, `tests/Makefile.am` porte :

> `⚠️ _DEPENDENCIES carries libcalaos_common.la only (T3.36): a change to ScriptBindings.cpp does not
> relink this binary on its own.`

⛔ **C'est faux depuis [`T3.36`](T3.36.md), et le fichier se contredit lui-même** : la déclaration
`LuaCalaosApi_test_DEPENDENCIES` située quinze lignes plus bas liste bien
`$(CALAOS_SERVER_BUILDDIR)/LuaScript/ScriptBindings.$(OBJEXT)`, et le commentaire immédiatement
au-dessus d'elle dit l'inverse du premier (« the linked server objects are prerequisites, so touching
one relinks this binary »). ⭐ **Mesuré, pas déduit** : les trois tours de `T3.102` qui modifient
`ScriptBindings.cpp` lisent `CXXLD LuaCalaosApi_test` **à chaque fois**.

⚠️ **Pourquoi ça compte** : c'est un avertissement périmé de la famille `_DEPENDENCIES`, et il pousse
dans la **mauvaise** direction — un agent qui le croit tiendra une mesure valide pour un faux vert,
ou ajoutera un `rm -f` que la consigne d'`ORCHESTRATION.md` interdit désormais. Un faux avertissement
posé à côté du piège lui-même est exactement ce qui a coûté **65 suites sur 102** à ce projet.

✅ **CORRIGÉ à la revue de merge de [`T3.102`](T3.102.md)** : les deux lignes fausses sont retirées.
L'énoncé juste était déjà présent au-dessus de la déclaration `_DEPENDENCIES` — il n'y avait rien à
écrire, seulement à ne plus dire le contraire. `if HAVE_GTEST` / `endif` : **113 / 114** des deux
côtés, inchangés ; aucun `TESTS`, aucun binaire, aucun `LDADD` touché.

## T3.60 — la portée de service (2026-09-06)

### ⭐ [F-SCOPE-1] La liste des huit décrit ce que la WebSocket refusait, pas ce qu'une portée de service devrait refuser — `config` en est absente

⇒ **Ticket ouvert : [`T3.111`](T3.111.md).**

La règle déplacée par [`T3.60`](T3.60.md) porte exactement les huit commandes que
`JsonApiHandlerWS` gardait. **Elle est fidèle, et c'est le problème** : elle a été composée à partir
de la table de dispatch d'un transport, jamais à partir de la question « que peut faire une session
de service ». `config` — `type=get` rend `io.xml`, `rules.xml` et `local_config.xml` **en clair**,
`type=put` les **réécrit** — n'y figure pas, parce que le code correspondant est **commenté** dans
`JsonApiHandlerWS::processApi()`. C'est la seule raison.

⚠️ **Conséquence directe et aujourd'hui latente** : maintenant que la garde suit la session, une
session de service ouverte un jour sur le transport HTTP serait refusée un `set_param` sur un seul
paramètre **et autorisée à téléverser `local_config.xml` entier** — qui contient le jeton de service
lui-même. Les six autres commandes que seul HTTP dispatche (`poll_listen`, `get_cover`,
`get_camera_pic`, `camera`, `event_picture`, `get_mcp_info`) posent la même question en moins grave,
`get_mcp_info` exceptée : elle rend le **jeton porteur** du proxy MCP.

⛔ **Ce n'est pas un défaut exploitable aujourd'hui** — aucune session HTTP ne peut être en portée de
service (mesuré dans `T3.60.md` §1) — mais c'est un trou **par construction** dans la liste, et il
s'ouvrira le jour où la portée deviendra atteignable ailleurs. La question à trancher est de
produit : *une session de service est-elle autorisée à lire et écrire la configuration ?*

### ⚠️ [F-SCOPE-2] Une session de service reçoit tous les événements de la maison, et un boîtier RemoteUI reçoit toute l'API

Deux constats faits en marge, **ni l'un ni l'autre refermés**, aucun ticket ouvert :

- `JsonApiHandlerWS::handleEvents()` ne teste que `loggedin`, jamais la portée. Le cloisonnement
  n'est appliqué qu'à la moitié requête/réponse. `docs/15_mcp_server.md` le dit déjà, et un cas de
  `core/JsonApiEvents_test` l'épingle : c'est un comportement écrit, pas une découverte.
- `RemoteUIWebSocketHandler` dérive de `JsonApiHandlerWS`, hérite donc de la garde, et **n'entre
  jamais dans la portée** : un boîtier authentifié par HMAC dispose de la surface d'API complète, les
  huit comprises. À rapprocher de `F-REMOTEUI-2`.

### ⚠️ [F-SCOPE-3] `checkCredentials()` préfère `cn_user`/`cn_pass`, et la configuration livrée les pose

Un harnais qui écrit `calaos_user`/`calaos_password` pour s'authentifier laisse **toutes** ses
requêtes non authentifiées : `checkCredentials()` bascule sur `cn_user`/`cn_pass` **dès que les deux
sont non vides**, et un `ConfigStore` neuf porte déjà `cn_user`. Le `LoginThrottle` transforme
ensuite la deuxième requête en refus pour une raison qui n'a rien à voir avec ce qu'on mesure —
donc un `400` qu'on attribue au sujet du test. Coûté une passe de mise au point dans `T3.60`.
**Pas un défaut du produit** ; une note pour le harnais suivant.

### ⭐ [F-SCOPE-4] Une ancre anti-vacuité qui demande un verbe HORS de la règle laisse la règle non mesurée (revue de merge)

`NoHttpEntryOpensAServiceSession` refermait ses trois refus par une requête admin **servie** sur la
vraie socket, pour prouver que le serveur n'était pas mort. Le verbe choisi était `get_home` — que la
règle **ne nomme pas**. Une garde qui aurait refusé les huit à **toute** session, scopée ou non,
laissait donc l'ancre verte : le foin excluait précisément ce qu'on cherchait.

Mesuré : en échangeant le test de session contre une constante (toute session devient scopée), la
suite livrée par le développeur rougissait **par le dispatch appelé à la main**, jamais par la
socket. Le cas ajouté à la revue — deux des huit, demandés en administrateur sur la vraie socket —
rougit sous cette mutation, et rattache ainsi la moitié « sert » de la garde HTTP au chemin réel.

⚠️ **La moitié « refuse » reste hors de portée d'une vraie requête** et le restera tant qu'aucun
transport HTTP n'ouvrira de session de service : c'est le prix assumé de l'arbitrage, écrit en tête
de fiche.

### ⚠️ [F-BOARD-1] Deux tickets ont porté le même numéro, et seul le rebase l'a dit (revue de merge)

`T3.60` a ouvert sa suite sous le numéro `T3.109` alors que la revue de `T3.106` venait de le
prendre, sur `master`, pour un sujet sans rapport. Les deux branches vivaient en parallèle ; rien
dans l'arbre de travail du développeur ne pouvait le lui apprendre. C'est le `CONFLICT (add/add)` du
rebase qui l'a révélé — s'il n'y avait pas eu de fichier, la collision serait passée dans `BOARD.md`
en deux lignes voisines au même numéro.

**Parade** : un ticket ouvert par une fiche prend son numéro **au merge**, pas à l'écriture ; et la
ligne « numéros pris » de `ORCHESTRATION.md` se relit sur `master`, jamais sur la branche.

## T3.101 — revue de merge (2026-09-06)

- ⭐⭐ **[F-LOGRAW-2] Un flux d'enfant SANS AUCUNE FIN DE LIGNE n'était jamais publié ET grossissait
  sans borne — FERMÉ par [`T3.101`](T3.101.md), et ce n'est pas ce que le ticket visait.**
  Reproduit et mesuré **sur `master`** par la revue, avec un vrai contrôleur et un sidecar qui déverse
  **16 MiO sans un seul `\n`**, la sortie comptée puis jetée : **RSS du parent +17 016 Kio** pour
  16 384 Kio reçus, **17 869 octets publiés** (les seules lignes « Stdout data received »).
  ⭐ **Même harnais, un seul fichier échangé contre celui de la branche** : **+536 Kio**, et
  **18 989 644 octets** publiés en tranches de 512. ⇒ le tampon **retient tout ce que l'enfant écrit,
  au rythme de l'enfant**, sans plafond, tant qu'il tourne. La borne de 512 octets n'est donc pas un
  confort de lisibilité : c'est ce qui ferme la fuite. Entrée dédiée dans
  [`RELEASE_NOTES.md`](RELEASE_NOTES.md).

- ⛔ **[F-LOGRAW-3] Rien ne tient le rapport « une ligne d'enfant → une ligne de journal ».**
  Contre-mutation de revue (ce qu'une ligne trop longue **consomme** ↔ ce qu'elle a **publié**) :
  une ligne d'enfant devient ⌈longueur / 512⌉ lignes de journal et **`core/SidecarOutputJournal_test`
  reste ENTIÈREMENT VERT**. La borne tient la longueur d'**une** ligne, le plafond épinglé la
  re-dérive, mais le **volume** n'est tenu par rien — ni le nombre de lignes (déclaré nu par la
  fiche), ni maintenant le facteur de multiplication. ⚠️ **À DEBUG sur le seul domaine `process`**,
  donc là seulement où quelqu'un l'a demandé : mesuré, 16 MiO d'enfant ⇒ **19 Mo de journal**
  (×1,16, ~37 000 lignes).

- ⚠️ **[F-LOGRAW-4] Les deux vidages de fin de tuyau ne sont tenus que par leur UNION.**
  Contre-mutation de revue (les deux tampons échangés aux quatre sites de fin de tuyau) ⇒ **0 rouge**.
  C'est un mutant équivalent en pratique — les deux tuyaux d'un enfant qui meurt arrivent en fin de
  fichier **ensemble**, donc chaque tampon est vidé par l'autre gestionnaire. **Ce qu'aucun cas ne
  tient, c'est la fin d'UN SEUL des deux tuyaux** (un enfant qui ferme sa sortie standard et continue
  de tourner).

- ⚠️ **[F-FLAKY-2, FERMÉ par [`T3.112`](T3.112.md) — voir la section datée du 2026-09-07 en fin de
  fichier, qui mesure la CAUSE et corrige le taux publié ici] `core/MqttSidecarConfigWait_test` a une
  borne d'HORLOGE MURALE sur un délai de connexion TCP** — même famille que `F-FLAKY-1`, autre fichier.
  `AnUnreachableBrokerEndsTheSidecarWithACauseAndANonZeroStatus` attend qu'un sidecar visant une
  adresse de documentation (RFC 5737) meure dans le budget annoncé ; quand la pile ne rend pas
  l'erreur à temps, `r.exited` est faux. **Mesuré des deux côtés du merge, 10 exécutions chacun** :
  `master` **5 échecs sur 10 à vide, 2 sur 10 sous charge** ; la branche **0 sur 10 à vide, 1 sur 10
  sous charge**. ⛔ **Sans rapport avec le relais** : ce cas lance son sidecar par un `fork`/`exec`
  à lui et ne construit aucun `ExternProcServer`. ⇒ [`T3.112`](T3.112.md).
  ⚠️ **Il est apparu deux fois sur les huit `make check` de la revue** et **aucun `make check` n'a été
  relancé pour faire disparaître un rouge** : les tours concernés sont nommés dans
  [`T3.101`](T3.101.md) §10.

- ⛔ **[F-LOGRAW-5] La raison écrite pour `debug_domains` plutôt que `debug_level` était fausse, la
  décision non.** La fiche disait que monter `debug_level` remettrait les lignes DEBUG du sidecar
  dans le journal que les assertions de secret relisent. **Mesuré : la variante `debug_level = 5`
  laisse `core/MqttSidecarConfigWait_test` vert, 9 cas sur 9** — `CoreFixture` ré-initialise la
  configuration avant chaque cas, donc **ni l'un ni l'autre** des réglages posés dans `main()`
  n'atteint l'enfant. Le bon argument est l'**étendue** : `debug_level` monte **tous** les domaines
  du processus, et une suite dont les assertions demandent qu'un secret soit **absent** est celle
  qu'un foin élargi affaiblit. Fiche et commentaire corrigés au merge.

## T3.63 — les fabriques à cas vacuants qui restent, et celle qui est fermée

⭐ **Fermée par [`T3.63`](T3.63.md)** : un comparateur de position rend « pas trouvé » sous la forme
d'un entier ordinaire — `-1` au-dessous de tout rang, `npos` au-dessus de toute position — que la
comparaison d'ordre **accepte**. `tests/check-order-sentinels.sh` refuse désormais qu'une telle
valeur occupe le plateau acceptant sans être exclue de sa sentinelle par une assertion du même cas.
**Recompté sur `5d2ae798` : 49 emplacements, 39 gardés, 10 non gardés** — la fiche d'ouverture n'en
annonçait qu'**un**, parce qu'elle ne comptait que la forme `-1`.

- ⚠️ **[F-VACUOUS-1] Un `npos` peut produire un vert PAR DÉBORDEMENT, et là il ne rougit plus.**
  `tests/core/HttpRequestLogSecret_test.cpp`, cas `TheHeadOfAWebsocketPayloadIsNotRenderedEither` :
  `ASSERT_LT(payload.find(k) + strlen(k), (size_t)40)`. Aiguille absente ⇒ `npos + strlen(k)`
  **enroule** vers `strlen(k) - 1`, et la borne est satisfaite. L'assertion est écrite comme une
  vérification de fixture — « le secret tient bien dans les quarante premiers octets » — et c'est
  exactement le genre d'assertion qu'on ne relit jamais. **Latent seulement** : la charge est bâtie
  littéralement dans le cas. ⇒ [`T3.115`](T3.115.md).

- ⛔ **[F-VACUOUS-2] Trois formes que `check-order-sentinels.sh` ne peut pas tenir**, mesurées au même
  balayage et laissées ouvertes **délibérément** :
  1. **Les ordres d'itérateurs.** `tests/core/JsonApiScenario_test.cpp` compare deux `std::find()`
     dans le même vecteur ; l'absent rend `end()`, qui joue le rôle de `npos`. Aujourd'hui gardé par
     deux `ASSERT_TRUE(sawEvent(…))`. La polarité tient au conteneur, pas au texte : une sonde
     textuelle ne peut pas décider de quel côté `end()` tombe.
  2. **La sentinelle « zéro ».** `pickFreePort()` (`tests/core/JsonApiServiceScope_test.cpp`) rend
     `0` pour un échec, et `0` est au-dessous de tout port réel. Gardé à son unique site. Troisième
     polarité ; l'ajouter ferait crier la sonde sur tout `EXPECT_LT(0, …)` de l'arbre.
  3. **La mesure aveugle.** `EXPECT_LT(longestEcho(journal, secret), kMaxEcho)` est vert quand le
     journal est **vide** : `0` satisfait toute borne. Même famille — une valeur « rien vu » que la
     comparaison accepte — mais aucun ordre de positions n'y est en jeu. C'est le trou que
     `check-echo-ceilings.sh` déclare déjà ne pas voir, et **aucune** des trois sondes statiques ne
     le ferme.

- ℹ️ **[F-VACUOUS-3] Le mode d'échec INVERSE, à ne pas confondre.** `tests/core/SidecarArgv_test.cpp`
  déréférence `shapes.find(x)->second` : une clé absente n'y produit pas un faux vert mais un
  comportement indéfini. Cité pour que le prochain balayage ne le range pas dans cette classe.

- ⭐ **À recopier — ce que la démonstration a appris** : au site que la fiche d'ouverture nommait, la
  démonstration **ne se produit pas**. `K_GetHomeEnvelopeAndItsThreeMembersAreSorted` : la seule clé
  dont la disparition satisfait ses deux `EXPECT_LT` est `data`, et le cas lit ensuite `doc["data"]`
  sur un `ordered_json` **const**, ce qui avorte. *L'assertion était vacuante, le cas ne l'était pas.*
  ⇒ **un audit de vacuité qui s'arrête à l'assertion surestime le risque ; un qui s'arrête au cas le
  sous-estime.** La classe se reproduit en revanche, verte sur `master`, aux deux sites `msg_id` que
  le recomptage a trouvés — et ceux-là, personne ne les avait vus.

### Ce que la revue de merge de `T3.63` a ajouté (2026-09-06)

- ⛔⭐ **[F-VACUOUS-4] UN AUTO-TEST NE VAUT QUE LA COUVERTURE DE SES FICHIERS, et trois décisions de
  `check-order-sentinels.sh` n'en avaient aucune.** Mesuré par échange, sur la sonde **livrée** :
  les deux formes de commentaire échangées dans son masquage ⇒ **43 des 49 emplacements
  disparaissent, les 10 vraies plaintes avec, et `make check` reste VERT sur 139**, auto-test
  compris ; `RETURNS_POSITION` rétréci d'un jeton ⇒ la famille `keyPos()` cesse d'être reconnue,
  **10 plaintes → 8**, muet des deux côtés ; et son test d'équilibre de parenthèses comptait les
  crochets écrits **dans un littéral**, si bien qu'une aiguille comme `find("{\"data\"")` rendait
  l'emplacement **invisible** — deux emplacements de `WagoBits_test.cpp` l'étaient déjà.
  ✅ **Fermé au merge** : équilibre corrigé (**51** emplacements vus, 51 gardés) et **5 fichiers
  d'auto-test de plus** (21 : 12 refusés, 9 acceptés), chacun pinçant une des trois décisions.
  *À recopier : une sonde à auto-test se contre-mute là où l'auto-test ne regarde pas — le masquage
  lexical et la reconnaissance des comparateurs, pas la règle qu'elle énonce.*

- ⭐ **[F-VACUOUS-2.3 — mesuré, et il devient [`T3.116`](T3.116.md)] La mesure aveugle n'est pas une
  hypothèse.** L'emballage `longestEcho()` rendu aveugle (`return 0;`) dans les **sept** suites qui
  en portent un laisse **toutes** leurs bornes vertes : aucun capteur à borne ne rougit sur 139,
  **8 `CXXLD` lus**. ⭐ **Et la parade est déjà écrite dans l'arbre, sur l'autre fonction** : cinq
  suites sur huit épinglent la mesure **profonde** par `ASSERT_EQ("bcdef", longestEchoRun(…))` et
  `ASSERT_EQ("", longestEchoRun(…))` ; **aucune** ne le fait pour la fonction que les bornes
  appellent, et trois suites n'épinglent ni l'une ni l'autre — `core/HttpRequestLogSecret` porte à
  elle seule **13** appels de l'emballage.

- ℹ️ **La sonde ne dépend pas de son auto-test pour être JUSTE.** Auto-test court-circuité sur une
  sonde saine, le balayage refuse toujours les mêmes 10 et accepte les mêmes 39. Ce que l'auto-test
  apporte est un **diagnostic** : sur une sonde cassée (plateaux acceptants échangés) il rougit sur
  4 de ses fichiers et le balayage n'est jamais atteint ; court-circuité, le balayage seul porte
  **7 accusations FAUSSES** contre `TcpSocket_test`, `WagoBits_test` et `MqttSidecarConfigWait_test`.
  *Il ne rend pas la sonde correcte, il empêche une sonde cassée d'accuser des innocents.*

## T3.57 — durée de vie des IOs de machinerie de scénario (2026-09-06)

- ⭐⭐ **Le segfault est DANS `deleteIO()`, pas dans l'opération suivante.** `gdb` sur `master` :
  `AutoScenario::stopBrokenRun()` ← `ListeRoom::refreshBrokenScenarios()` ← `ListeRoom::deleteIO()`.
  ASan, même arbre : *heap-use-after-free*, **READ of size 8**, libéré par `Internal::~Internal()`
  ← `Room::RemoveIO()` ← `delete_io()` ← **le même `deleteIO()`**. *À recopier : une note qui dit
  « et l'opération suivante déréférence » n'a pas été jouée sous debugger ; celle-ci l'a été et le
  chemin était plus court d'un cran.*
- ⭐ **`Room::RemoveIO(pos, del)` avec `del == true` est LE point de passage de toute destruction
  d'IO** : `~Room` et `ListeRoom::delete_io()` y passent, donc `ListeRoom::deleteIO()` aussi.
  ⛔ **Sauf `delete_io(io, del=false)`**, qui rend la propriété à l'appelant : celui-ci détruit hors
  de `Room`, et rien ne le tient. Aucun appelant de production aujourd'hui, deux suites de `tests/`.
  Mesuré par contre-mutation : poser le désenregistrement dans `~Room` **au lieu de** `RemoveIO()`
  laisse les **quatre** cas passant par `deleteIO()` rouges et ne garde que celui de la pièce.
- **`ScenarioAction::io` n'est pas un membre** : `getRealAction()` renvoie la structure par valeur.
  Et `AutoScenario::ioScenario` est un lien de **propriété** — `~AutoScenario` le déréférence, donc
  il ne doit surtout pas être annulé par un balayage de destruction.
- ⭐ **`core/JsonApiScenario_test` tient déjà une part de cet invariant** : échanger les cibles
  d'affectation `ioStep` ⇄ `ioTimer` dans le désenregistrement la fait rougir, alors que rien dans
  cette suite ne parle de durée de vie. La suite neuve n'est pas seule à garder la propriété.
- **Les deux configurations réelles portent ZÉRO auto-scénario** depuis le re-cléage de `T3.61` :
  toute mesure de coût qui les prend telles quelles parcourt une liste **vide** et ne mesure rien.
  Bornage du cas non vide en ajoutant 20 scénarios avant la destruction : 134 IOs × 20 scénarios
  démolis en **0,55 ms**, contre **0,55 ms** sans le balayage.

### Ce que la revue de merge y a ajouté (2026-09-06)

- ⭐⭐ **Le maillon qui rend la porte réelle n'était pas écrit** : `scenario_id` **est**
  l'`autoscenario_uid`, et les cinq ids de machinerie en dérivent par suffixe. C'est de là que
  vient le partage — pas d'un hasard de nommage. Un ticket qui voudra refuser le doublon doit
  savoir que l'uid n'est pas qu'un marqueur : c'est la **clef de la machinerie**.
- ⭐ **Une contre-mutation qui ne rougit nulle part est une information, pas un échec.** Le garde
  `if (del)` retiré du désenregistrement — donc l'oubli appliqué aussi au chemin de **transfert** —
  laisse `make check` **entièrement vert sur 140**. Le « rien ne le tient » que la fiche déclarait
  est désormais **mesuré**. *À recopier : une propriété qu'on documente comme nue se mesure comme
  les autres ; la déclarer ne coûte rien, la mesurer dit si le jour où elle aura un appelant on le
  saura.*
- ⛔ **Un binaire qui meurt ne rend pas d'ensemble rouge.** Comparer deux campagnes « cas par cas »
  quand la moitié des cas segfaute demande **une exécution filtrée par cas** des deux côtés —
  sinon la campagne compare un premier mort à un autre premier mort. C'est ce qui a permis de
  vérifier que la contre-mutation du développeur rend **exactement** le comportement de `master`.
- ✅ **Le nouvel appel ne crée pas de danger d'extinction**, et la raison est dans `~ListeRoom` :
  il vide `rooms` **dans son corps**, donc ses membres — dont le cache de scénarios que le balayage
  parcourt — sont encore vivants quand `~Room` s'exécute. C'est ce qui distingue ce balayage de
  l'appel voisin à la liste de règles, qui, lui, touche un **autre** singleton déjà détruit.
- ⚠️ **Une non-régression mesurée sur des configurations qui n'exercent pas le chemin corrigé ne
  prouve que l'absence de coût.** Les deux maisons du dépôt portent zéro auto-scénario ; la mesure
  est bonne pour ce qu'elle mesure, et ne dit **rien** de la correction.

## T3.54 + T3.55 — les deux dernières paires adjacentes et nues de la chaîne de typage

- ⭐⭐ **Confirmation — l'atténuation de `T3.54` TIENT, et elle est maintenant MESURÉE.** La fiche
  disait qu'une permutation des deux arguments au site d'appel de `WagoBits::setBufferBit()` serait
  *« très probablement »* rougie par un test existant, sans que personne l'ait mutée. **Mutée sur
  l'arbre `5d2ae798` intégral** (`src/` **et** `tests/` de `master`, pour qu'aucun cas neuf ne
  pollue l'ensemble rouge) : la permutation **compile en silence** (`rc=0`, 0 `error:`) et fait
  rougir **`WagoBits_test` et elle seule**, **5 cas sur 21**,
  `TOTAL 138 / PASS 136 / SKIP 1 / FAIL 1`. ⇒ le ticket **reste petit** et son correctif est un
  **confort de type**. ⛔ **Mais le filet est `packBits`, pas `setBufferBit`** : le nom n'apparaît
  dans **aucun** test, la couverture est **indirecte**, et un **second** appelant n'hériterait de
  rien. ⇒ [`T3.54`](T3.54.md).
  *À recopier : une atténuation déduite se mesure avant d'être invoquée — celle-ci a tenu, ce qui
  ne rend pas la mesure inutile : elle a montré que le filet couvre UN site, pas la fonction.*

- ⛔⭐⭐ **[F-SQUEEZE-2] LE PÉRIMÈTRE DE `T3.55` ÉTAIT FAUX, ET C'EST LE BUILD QUI L'A DIT.** La
  fiche écrivait : *« un balayage de tout `src/` trouve `SqueezeRequest_cb` /
  `SqueezeRequest_signal` dans **2 fichiers seulement** »*. **La phrase est exacte et la question
  était mauvaise** : ce qui se lie à cette fente n'en porte pas le nom, ce sont des fonctions
  membres de la bonne **FORME**, prises par `sigc::mem_fun`. Retyper les deux `typedef` a produit
  **une** erreur de compilation, et elle nommait `SqueezeboxDB`. **Balayage refait sur la forme**
  `(bool …, string …, string …, AudioPlayerData …)` : `Audio/SqueezeboxDB.{h,cpp}` porte **23
  déclarations, 21 définitions, 22 enregistrements** — tous liés à la fente, tous absents du
  recensement. ⇒ **150 sites et 4 fichiers**, contre les **84 sites et 2 fichiers** annoncés.
  *À recopier, et c'est la même classe que l'infirmation R2 du merge de `T3.50` : une portée de
  balayage doit être écrite avec la phrase qu'elle porte. « Le nom du `typedef` n'apparaît que dans
  deux fichiers » n'est pas « le défaut ne vit que dans deux fichiers ». Pour une fente sigc++, le
  balayage qui compte est celui de la FORME des paramètres, pas celui du nom du `typedef`.*

- ⭐⭐ **[La cécité de `T3.55` n'est plus une analogie de forme, elle est MESURÉE.]** La fiche
  affirmait *« par analogie de forme avec T3.53 »* que la permutation compile en silence, et
  déclarait ne pas l'avoir vérifiée. **Vérifiée** : `sig.emit(status, cmd.request, cmd.result, …)`
  → `(…, cmd.result, cmd.request, …)` sur `master` compile avec **0 `error:`** et laisse
  **`TOTAL 138 / PASS 137 / FAIL 0`** — **zéro rouge**. ⭐ **Le contraste avec `T3.54` est le fait
  utile** : la **même** mutation formelle — permuter deux arguments adjacents à leur site — rougit
  **5 cas** côté Wago et **zéro** côté Squeezebox. C'est toute la différence entre une paire de
  types **distincts** couverte par un test et une paire de types **identiques** que rien n'atteint.
  Sur l'arbre typé, la même mutation **ne compile plus**.

- ⚠️ **[F-SQUEEZE-1] `Audio/RoonPlayer` porte 14 sites de la même forme, qu'aucune fente ne lie.**
  Ses 7 callbacks `(bool status, string request, string result, AudioPlayerData data)` sont
  **déclarés** (`RoonPlayer.h`) et **définis** (`RoonPlayer.cpp`), et **aucune ligne de l'arbre ne
  prend leur adresse** : `sigc::mem_fun` ne les nomme **jamais** (2 `mem_fun` dans le fichier,
  aucun des deux sur eux). ⇒ **le typage de [`T3.55`](T3.55.md) ne les atteint pas et ne pouvait
  pas les atteindre.** Fiché, **non touché** : leur sort est un changement de périmètre, pas un
  durcissement de type.
  ⛔ **Corrigé à la revue de merge : « chacun n'apparaît qu'une seule fois, sa propre définition »
  était FAUX.** Chacun des 7 apparaît **deux** fois — sa définition **et un appel direct**, sur le
  chemin d'erreur de sa propre requête : `get_volume_cb(true, "", "", data);` et ses six jumeaux.
  Ils ne sont donc pas du code mort, ils sont du code **appelé sans passer par une fente**.
  ⭐ **Et ce que la correction change est à l'avantage du constat** : leur unique appelant passe
  **deux littéraux vides** aux deux positions adjacentes, si bien qu'une permutation y est un
  no-op **textuel**. La paire y est nue **et** vacante — c'est la raison pour laquelle rien ne
  pourrait la mesurer, pas l'absence d'appelant.

- ⚠️ **[Trois déclarations mortes de plus dans la chaîne Squeezebox, confirmées.]**
  `get_album_cover_id_cb` (`Squeezebox.h`) — l'écart entre 27 déclarations et 26 définitions dans ce
  fichier —, plus `getRandoms_cb` et `getRandomType_cb` (`SqueezeboxDB.h`) — l'écart entre 23 et 21.
  **Déclarées, jamais définies, jamais enregistrées.** ⛔ **Aucune supprimée** : `T3.53` §7.4 a
  refusé le même geste pour la même raison, c'est un arbitrage à part.

- ⭐ **[La sonde au site de `T3.55` re-mesurée par un instrument DIFFÉRENT, et les deux concordent.]**
  La fiche avait mesuré `result` lu par les 26 implémentations et `request` par aucune, au
  `-Wunused-parameter`. Le script de retypage refusait de continuer s'il trouvait `request` lu dans
  un corps : **47 corps parcourus** (`SqueezeboxDB.cpp` inclus), **0 lecture de `request`**,
  **49 lectures du second paramètre** (26 + 23). ⇒ **47 analyseurs sur 47** liraient la commande
  envoyée à la place de la réponse reçue. ⭐ **Et cette asymétrie ouvre un remède que `T3.53` ne
  pouvait pas avoir** : **retirer** le paramètre mort ferait de la fente `(bool, string,
  AudioPlayerData)` — trois types distincts — et supprimerait la paire **par effacement**, pour
  ~98 lignes et **zéro corps touché**. **Non retenu** : jeter une information que le contrat
  transporte est une décision de **produit**. ⇒ [`T3.113`](T3.113.md).

- ⭐⭐ **[Revue de merge — LES DEUX MESURES QUI DÉCIDENT DES TICKETS ONT ÉTÉ REJOUÉES SUR `master`
  À 140 SUITES, et elles tiennent toutes les deux.** Permutation au site d'appel de `packBits` :
  **compile, 0 `error:`**, `TOTAL 140 / PASS 138 / SKIP 1 / FAIL 1`, **`WagoBits_test` seule**,
  **les 5 cas nommés**. Permutation à l'émission Squeezebox : **compile, 0 `error:`**,
  `TOTAL 140 / PASS 139 / SKIP 1 / FAIL 0` — **zéro rouge**. Sur l'arbre livré, les deux **ne
  compilent plus** (`could not convert 'WagoBits::BitState' to 'WagoBits::BitIndex'` ;
  `cannot convert 'SqueezeResult' to 'sigc::type_trait_take_t<SqueezeRequest>'`). Et le
  RED→GREEN des 5 cas neufs a été rejoué en portant les deux fichiers de `tests/` sur `master`
  intact : **3 rouges dans `Squeezebox_test`, 2 dans `WagoBits_test`, aucune autre suite**.
  *À recopier : la même mutation formelle rougit 5 cas d'un côté et zéro de l'autre — c'est la
  différence entre une paire de types distincts qu'un test atteint et une paire de types
  identiques que rien n'exécute.*

- ⛔⭐⭐ **[Revue de merge — LE CONTOURNEMENT `W3` N'EST PLUS DÉCLARÉ, IL EST MESURÉ, ET IL COÛTE
  DIFFÉREMMENT DES DEUX CÔTÉS.** Envelopper la **mauvaise** variable a été joué aux deux sites
  d'appel, ce que la fiche n'avait pas fait.
  Côté Wago — `setBufferBit(&out[0], BitIndex(values[i]), BitState(i))` — **compile, 0 `error:`**,
  et rougit **exactement les 5 mêmes cas** que la permutation nue sur `master` : le type n'y ajoute
  **aucun** rouge, c'est la suite comportementale de `packBits` qui tient le site, comme avant.
  Côté Squeezebox — `sig.emit(status, SqueezeRequest(cmd.result), SqueezeResult(cmd.request), …)` —
  **compile, 0 `error:`**, et l'ensemble rouge est **VIDE** sur 140 (le seul rouge du tour est
  `F-FLAKY-2`). ⇒ **le seul site que le typage de `T3.55` ne peut pas protéger est aussi le seul
  que rien n'exécute** ⇒ [`T3.118`](T3.118.md).
  *À recopier : une enveloppe déplace la faute du site d'appel vers l'endroit où un humain nomme
  la valeur ; ce que ça vaut se mesure en demandant ce qui rougit QUAND l'humain se trompe là.*

- ⚠️ **[Revue de merge — la raison écrite pour laisser `countIsWritable(int, size_t)` nue est
  FAUSSE ; la décision, elle, tient.]** La fiche invoque « un avertissement de rétrécissement que
  le projet ne demande pas ». **Mesuré** : `countIsWritable(values.size(), nb)` dans `packBits`
  compile avec **0 `error:` et 0 `warning:` provenant de `WagoBits.h`** sous les drapeaux du
  projet — la permutation y est **exactement aussi silencieuse** que celle qui vient d'être typée.
  Ce qui la tient est ce qui tenait l'autre avant le typage : **la suite**, et elle mord
  (**3 cas** rouges). La paire reste nue par **périmètre**, pas parce qu'un compilateur veillerait.

- ⛔ **[Revue de merge — `F-LINK-1` ne se relie PAS à peu de frais, et c'est chiffré.]**
  `nm -Cu Audio/Squeezebox.o` : **168** symboles indéfinis, dont `Calaos::SqueezeboxDB::SqueezeboxDB`,
  `UrlDownloader::httpPost`, `Calaos::ListeRoom::Instance`, `EventManager::create`, `Timer::Timer`,
  `IODoc::*`, `Calaos::Registrar`, `Calaos::AudioPlayer` et `Calaos::IOBase`. Le harnais avait déjà
  **refusé cet objet par écrit** pour `core/JsonApiCharacterization_test` (« il traîne `SqueezeboxDB`,
  `UrlDownloader` et les neuf objets AVR, et lie `uv_tcp_connect`/`uv_write` »). ⇒ relier l'objet de
  production tel quel n'est pas une ligne de `LDADD` ; c'est un ticket.

- ⭐⭐ **[T3.59 — LE RECENSEMENT DES SOUS-DISPATCHS DES DEUX HANDLERS, et il en restait TROIS
  muets, pas un.]** Balayé sur la **forme** (« chaîne de `if / else if` qui choisit une
  sous-commande ») et non sur un nom : **13 sites**, **8** dans `JsonApiHandlerHttp`, **5** dans
  `JsonApiHandlerWS`, ⚠️ **`JsonApi.cpp` n'en porte AUCUN** — c'est un fichier de constructeurs, ses
  chaînes de `==` portent sur des valeurs (type d'IO, jour de la semaine), jamais sur une
  sous-commande. **10 avaient déjà une branche par défaut** (dont deux posées par E4.6e), **3 étaient
  silencieux** — `processCamera()` (HTTP), `processSettings()` (WS), et la **racine** du dispatch WS —,
  **1 n'a pas d'`else` sans être muet** : `processPolling()` répond `{}`, l'objet vide construit avant
  la chaîne tombant dans le `sendJson()` qui la suit.
  *À recopier : « pas de branche par défaut » et « silencieux » ne sont pas la même mesure, et un
  recensement qui confond les deux se trompe dans les deux sens.*

- ⛔⭐ **[T3.59 — LE SILENCE HTTP EST UN DESCRIPTEUR QUI N'EST JAMAIS RENDU, et le plafond par
  source le compte.]** `requestReadTimeout()` (30 s) **ne couvre que le délai AVANT que la première
  requête soit analysée** : une requête qui atteint un sous-dispatch est analysée depuis longtemps,
  aucune minuterie ne la surveille plus. La seule chose qui ferme une connexion inactive est la
  minuterie de 500 ms armée en construisant une réponse portant `Connection: Close` — **pas de
  réponse, pas de minuterie**. Mesuré : un client peut ainsi immobiliser
  `maxConnectionsPerIp` = **50** connexions, c'est-à-dire **tout son propre budget** (sa 51ᵉ requête
  reçoit un **429**), et deux sources prennent les `maxConnections` = **100** places globales (503
  pour tout le monde). La fuite est donc **bornée** : c'est un auto-déni de service d'abord.
  *À recopier : un délai d'expiration de lecture ne protège pas d'un silence applicatif — il n'est
  plus armé quand le silence commence.*

- ⚠️ **[T3.59 — le compte de la fiche d'ouverture était bas d'un tiers, sur le site qu'elle avait
  elle-même mesuré.]** Elle annonçait « **deux** cas épinglent déjà le silence de `camera` » : il y en
  avait **trois**, le troisième dans `JsonApiCameraSnapshot_test` (T3.17d). Et le silence de
  `processSettings()` était **déjà nommé en toutes lettres** dans l'en-tête de `JsonApiSession_test`
  avec **deux cas** de plus, sans que la fiche le voie.
  *Troisième recensement de fiche pris en défaut de la série, après `T3.55` (84 annoncés / 150 réels)
  et `T3.63` (1 / 10). Compter soi-même n'est pas une précaution, c'est la mesure.*

- ⛔ **[T3.59 — `poll_listen` répond un objet vide sur un `type` inconnu, et rien ne distingue cette
  réponse d'un `register` qui aurait échoué.]** `JsonApiHandlerHttp::processPolling()` n'a pas de
  branche par défaut ; son `Json::object()` initial traverse la chaîne intact jusqu'au
  `sendJson(jret)` final. Ce n'est pas un silence — la socket est libérée — donc ce n'est pas le
  défaut que `T3.59` ferme, et le ticket ne le change pas. Épinglé tel quel par
  `PollListenWithAnUnknownTypeAnswersAnEmptyObject`.

- ⛔⭐⭐ **[Revue de merge de T3.59 — L'EN-TÊTE DE FERMETURE EST UN TÉMOIN PAR PROCURATION, et le
  maintien de connexion n'est épinglé dans AUCUN sens.]** ⇒ [`T3.122`](T3.122.md).
  `HttpClient::buildHttpResponse()` écrit l'en-tête `Connection` **puis** en déduit `conn_close`, le
  drapeau qui arme la minuterie de fermeture de 500 ms. Les **deux affectations** de ce drapeau
  échangées — une réponse portant `Connection: Close` ne ferme plus, une réponse sans lui ferme de
  force —, l'ensemble rouge de l'arbre entier est **1 cas / 1 binaire** :
  `TheAnswerReturnsThePerSourceSlotSoTheNextRequestIsServed`, **le cas que `T3.59` vient d'ajouter**.
  ⛔ **Sur `master` avant ce ticket il aurait été VIDE.** Et **toutes** les assertions d'en-tête
  restent **vertes**, y compris les deux dont le nom promet la libération de la socket et leur
  jumeau d'`E4.6e` §8.7. ⚠️ Ce n'est **pas** un défaut de production — les deux lignes sont dérivées
  l'une de l'autre — mais la propriété sur laquelle repose toute la gravité de `T3.59` n'avait
  **aucun** capteur avant lui, et sa moitié symétrique n'en a toujours pas.
  *À recopier : un nom de cas qui promet un effet et une assertion qui lit le texte annonçant cet
  effet sont deux choses différentes. La question à poser n'est pas « qu'est-ce que la réponse
  DIT ? » mais « qu'est-ce que le serveur FAIT ensuite ? » — et seule une contre-mutation sur le
  faire les sépare.*

- ✅⭐ **[Revue de merge de T3.59 — la règle de confiance qui BORNE la fuite est déjà gardée, et
  l'hypothèse de trou était fausse.]** `effectiveClientIp()` ne lit l'en-tête de transfert que si le
  pair TCP est la boucle locale ; c'est ce qui empêche un client distant de se donner autant de
  seaux qu'il veut, et donc ce qui rend la fuite de `T3.59` **bornée à 50** au lieu d'illimitée. La
  fonction pure est testée à part, mais **l'ordre de ses arguments au site d'appel** ne l'était
  peut-être pas : les deux **échangés**, l'ensemble rouge est **13 cas / 3 binaires**
  (`JsonApiThrottleIdentity_test`, `PeerAddressFamily_test`, `IncomingLogStockLevel_test`), dont
  `AForgedHeaderFromTheLanCannotChooseItsBucket`. ⚠️ **La suite neuve de `T3.59` y reste verte** —
  son astuce d'identité continue de lui donner son seau —, le mérite revient entièrement aux cas
  antérieurs.
  *À recopier : « la fonction est pure et testée » ne dit rien du câblage de ses arguments. Ici le
  câblage était gardé ; le vérifier a coûté un tour et a converti une réserve en mesure.*

- ⚠️ **[Revue de merge de T3.59 — les payloads par défaut des deux transports sont DÉGÉNÉRÉS, et
  c'est ce qui rend l'ordre circulaire porteur.]** Sur les neuf branches par défaut, **quatre**
  répondent la même chaîne `unkown audio_action` (`processAudio` et `processAudioDb`, des deux
  côtés) et **deux** la même `unknown autoscenario type`. Apparier deux de celles-là dans une
  permutation serait **un no-op textuel** — la variante neuve du mensonge n° 4 relevée à la revue de
  `T3.38` : *un test peut être aveugle à une permutation qui ne change aucune valeur*. L'ordre
  circulaire de `M3` l'évite explicitement, et c'est **là** qu'est sa valeur, pas dans le nombre 44.
  *À recopier : quand une famille de payloads porte des doublons, ce n'est pas la mutation qui
  choisit la couverture — c'est le choix de l'ordre, et il doit être écrit.*

## T3.38 — le recensement des `ioDoc` de l'arbre entier (2026-09-06)

**508 appels `ioDoc->…` dans 62 fichiers**, dont **405** nommés+décrits et **65** bornés
(balayage `python3` sur les fichiers suivis de `src/`, arguments découpés à la profondeur de
parenthèse, littéraux concaténés recollés — jamais `grep`). La fiche de `T3.38` annonçait **deux**
chaînes ; les trois formes de mensonge cherchées en donnent **1 + 3 + 5**.

- ✅ **`F-OLA-6` FERMÉ** par [`T3.38`](T3.38.md) — `channel_red` passe de `0..9999` à `0..512`.
  ⭐ Et le constat qui manquait au finding : **rien dans `calaos_server` ne relit un `min`/`max`**.
  Ils quittent `IODoc` par `genDocJson()`/`genDocMd()` **seulement** ; `IOBase::set_param()` ne les
  consulte pas ; **0 lecteur de `"min"`/`"max"` hors `IODoc.cpp`** dans tout l'arbre. La borne
  publiée est donc **la seule barrière existante**, pas un commentaire.

### F-IODOC-1 ⭐ `grid_h` et `grid_w` publient chacun la description de l'autre — `IO/RemoteUI/RemoteUI.cpp`

`grid_h` est documenté « Grid **horizontal** size » et `grid_w` « Grid **vertical** size ».
**Le code tranche** : `RemoteUIWebSocketHandler.cpp` remplit `data["grid_height"]` depuis
`get_param("grid_h")` et `data["grid_width"]` depuis `get_param("grid_w")`. Les deux phrases sont
**échangées**, donc le défaut est symétrique et ne se rattrape pas par tâtonnement : un installeur
qui pose un bandeau 4 colonnes × 2 lignes saisit les deux nombres à l'envers, et une grille carrée
le cache entièrement. **Non corrigé par `T3.38`** (hors de son périmètre `IO/OLA/` + `IO/Mqtt/`)
⇒ [`T3.120`](T3.120.md).

### F-IODOC-2 ⛔ Trois familles de bornes contredisent ce que le code accepte — **5 déclarations**

| Site | La doc promet | Le code accepte |
|---|---|---|
| `OLA/OLAOutputLightDimmer.cpp` `channel`, `OLA/OLAOutputLightRGB.cpp` `channel_green`/`channel_blue` | `0..512` | `0..511` |
| `KNX/KNXBase.cpp` `eis` | `0..15` | `1..15` (`KNXExternProc_cli.cpp` refuse `eis < 1` à **3** endroits) |
| `IO/InputTimer.cpp` `msec` | `0..999` | plancher de **50 ms** sur le total (`if (msec < 50) msec = 50;`) |

⭐ **La borne DMX est MESURÉE, pas déduite** — programme lié à la `libola` de l'image, exécuté
dans le conteneur : `Blackout()` ⇒ `Size=512` ; `SetChannel(511, 200)` ⇒ `Get(511)=200` ;
`SetChannel(512, 201)` ⇒ **`Get(512)=0`** ; `SetChannel(9999, 202)` ⇒ **`Get(9999)=0`**, aucune
exception, aucune valeur de retour, et `SendDmx()` part quand même. **`512` est documenté comme
valide et ne fait rien.**
⛔ **Non corrigé** : le nombre d'`io.xml` sert d'**indice de tampon** (base 0) alors que tous les
manuels de projecteur numérotent à partir de **1**. Passer `0..512` à `0..511` bénit la base 0
dans la documentation ; passer à `1..512` avec un `-1` à l'émission décale **toutes** les
installations existantes. C'est un arbitrage de produit sur **4 sites** ⇒ [`T3.120`](T3.120.md).

### Ce que le recensement N'A PAS trouvé, et qui vaut d'être écrit

- **Forme 1 (borne recopiée de la ligne voisine) : un seul site dans tout l'arbre**, celui de la
  fiche. Les autres égalités entre déclarations voisines sont justes, vérifiées une à une
  (`port_web`≡`port_cli`, `gpio_down`≡`gpio_up`, `sec`≡`min`, `var`≡`port`, `var_down`≡`var_up`,
  `time_down`≡`time_up`, les trois triplets de `WODaliRVB` cohérents avec `WODali`).
  ⭐ **Et la contre-épreuve du défaut était à un répertoire de distance** :
  `OLAOutputLightDimmer.cpp` écrit la même paire `universe`/`channel` en `0..9999`/`0..512`.
- **Aucune description ne renvoie à un paramètre inexistant** : 5 candidats bruts sur 161 noms
  connus, **5 faux positifs** (clés JSON d'exemple, un fragment d'URL).
- ⚠️ **`WODali`/`WODaliRVB` déclarent leurs adresses sur `1..612`** là où une adresse courte DALI
  va de 0 à 63. Le chiffre est **suspect** mais il n'entre dans aucune des trois formes : il est
  cohérent sur les **4** sites, et **aucune garde de l'arbre ne le contredit** — la valeur part en
  chaîne dans une commande `WAGO_DALI_*` vers un processus externe. À instruire avec le matériel,
  pas depuis le code.

### ⭐⭐ Le résultat de contre-mutation de T3.38 qui vaut au-delà du ticket

**Échanger les déclarations complètes de `channel_green` et `channel_blue`** — deux canaux DMX
entiers permutés — laisse **les trois cas de borne VERTS**. C'est arithmétique et ça n'a rien à
voir avec la forme du test : les deux canaux **partagent leur borne**, donc l'échange **ne change
aucun nombre**. Seul le cas qui exige que la description de `channel_red` dise « red » et jamais
« green » ni « blue » voit la permutation, parce que les **couleurs**, elles, ont bougé.
*À recopier : quand deux champs partagent leur valeur, l'échange se lit dans ce qui les NOMME, pas
dans ce qui les borne. Un oracle en relation ferme le « recopié de la ligne voisine » ; il ne
ferme pas la permutation de deux frères équivalents, et il faut un second capteur pour ça.*

### F-IODOC-3 ⛔⭐ Une borne d'`ioDoc` ne mord nulle part — 63 littéraux sur 65, alors que l'arbre sait déjà la faire mordre

Ouvert par la **revue de merge** de [`T3.38`](T3.38.md). Sur les **65** déclarations bornées de
l'arbre : **1** nomme une constante que son consommateur lit aussi (`debounce`, borné par
`GpioCtrl::DEBOUNCE_TIME_MAX` dans l'appel lui-même), **1 famille** est re-dérivée à la main côté
code (`WagoConfigParse::valueValid()` refait le `0..65535` des `paramAddInt` Wago et le dit dans son
commentaire), et les **63** autres sont des littéraux qu'aucune ligne de code ne re-dérive.

⭐ **Et le modèle manquant est déjà dans l'arbre** : `ConfigOptions` publie la même paire `min`/`max`
dans le JSON de l'interface **et la fait respecter** — une valeur hors plage y est refusée avec un
message. Deux systèmes de configuration cohabitent, d'apparence identique côté client, dont un seul
a une garde. ⇒ [`T3.121`](T3.121.md).

**Mesuré, pas déduit** (contre-mutation CM-1 de la revue) : les **quatre** bornes de canal DMX
portées **ensemble** à `0..1024` — valeur que `libola` jette en silence — laissent `make check`
**entièrement vert sur 141**, `core/IoDocRgbBounds_test` comprise. Un oracle en relation ferme la
borne recopiée de la ligne voisine ; il ne voit pas la borne qui ment à l'unisson, c'est-à-dire
exactement la forme que `F-IODOC-2` recense.

### ⭐⭐ Ce que la revue de `T3.38` a corrigé au recensement, et ce qu'elle a confirmé

- ✅ **Les 508 appels / 62 fichiers / 65 bornes sont exacts à l'unité**, et la ventilation par
  fonction l'est aussi. ⚠️ **Le balayage qui les trouve doit chercher DEUX receveurs** : `ioDoc->`
  en manque **13**, écrits `doc->` dans `KNXIo.cpp` et `WagoIOBase.h`.
- ✅ **Forme 2 = 3**, recomptée par familles d'antonymes sur toutes les paires de paramètres frères
  de l'arbre ; les 34 autres paires sont justes, relues une à une.
- ⚠️ **Forme 3 = 5, mais `eis` demande une nuance** : `0` n'est refusé que sur le chemin
  d'**écriture**. Sur le chemin de **lecture**, `KNXValue::setValue()` traite `eis == 0` comme
  « déduire le type de la longueur de la donnée » et y répond pour les tailles 1, 2, 3, 4, 5 et 15.
  La borne n'est fausse **que pour les sorties**.
- ⭐ **`DmxBuffer::SetChannel()` rend `void`** — vérifié au compilateur, la première écriture de la
  sonde de mesure ne compilait pas. Le silence n'est pas une négligence de l'appelant : il est dans
  la signature, il n'y a aucune valeur de retour à ignorer.

### ⭐⭐ M5 n'ouvre pas une septième façon de mentir : c'est la QUATRIÈME, sous une forme nouvelle

La quatrième de la liste canonique est la **fixture fausse** — une donnée d'essai qu'une coïncidence
de valeurs rend incapable de distinguer le défaut. C'est exactement M5 : les deux canaux **partagent
leur borne**, donc les permuter **ne change aucun nombre**.

⭐ **Ce qui est neuf est que la fixture n'appartient pas au test.** Les listes précédentes
désamorçaient ce piège en **choisissant** mieux la donnée (« trois octets deux à deux distincts,
dernier octet non nul »). Ici c'est impossible : la donnée mesurée **est le document livré**, et sa
dégénérescence — deux frères à la même valeur — est ce que le ticket vient d'installer. La seule
parade est un **second capteur sur un champ que les frères ne partagent pas**.
*À recopier : quand la fixture est la donnée de production et qu'elle est dégénérée par
construction, on ne corrige pas la fixture — on ajoute un capteur sur ce qui reste distinct.*

---

## ⭐ Revue de merge de `T3.32` — une sonde qui n'accuse rien parce que rien ne l'alimente (2026-09-06)

- ⛔⭐ **[F-DOCS-1] `scripts/check-docs.py` : son auto-test n'épingle nulle part qu'elle ait LU
  quelque chose.** Les 28 corpus appellent `scan()` **directement**, la liste des documents en
  argument ; la sélection du corpus réel — le `os.listdir()` de `main()` — n'est exercée par aucun
  d'eux. Mesuré par un **échange d'opérandes** dans cette sélection, `n.endswith('.md')` →
  `'.md'.endswith(n)` : la sonde rend **`0`**, ne porte **aucune** des 4 plaintes de l'arbre, et son
  auto-test **PASSE** en annonçant ses 14 refus et ses 13 acceptations. Le seul reste est le résumé,
  `0 references over 0 documents`, que personne n'est tenu de lire puisque la cible est non
  bloquante **par décision**. ⇒ [`T3.127`](T3.127.md).

  ⭐ **C'est une troisième forme, distincte des deux déjà fichées.** À la revue de `T3.63`, une sonde
  se cassait dans son **masquage lexical** ; à celle de `T3.22`, dans sa **table de reconnaissance**.
  Ici la règle est intacte, la reconnaissance est intacte, et c'est **l'entrée** qui a disparu.
  *À recopier : un auto-test qui reçoit son corpus en argument ne dit rien de la façon dont le corpus
  réel est choisi — et c'est le seul endroit où une sonde peut devenir muette sans se tromper une
  seule fois.*

- ⚠️ **Mutant équivalent déclaré** : les deux opérandes de `cited_span()` échangés
  (`nums[0] <= nums[1]` → `nums[1] <= nums[0]`) rendent **exactement la base** — 4 plaintes,
  auto-test vert. L'**intérieur** d'un intervalle cité n'est lu ni par un corpus ni par une ancre de
  l'arbre : les deux seules ancres de `docs/*.md` visent une ligne unique.

- ✅ **Le refus de deviner est gardé, et son prix est mesuré.** `len(found) == 1` → `>= 1` est
  **refusé par l'auto-test** (2 corpus). Mesuré au-delà de lui, sur l'arbre réel, deviner ferait
  passer les citations non résolues de **26 à 15** pour **une seule** accusation de plus — et
  celle-là est vraie (`__init__.py:17`, le fichier en a 8). Les 26 silences ne cachent donc pas un
  trou.

---

## T3.21 + T3.25a — quatre mesures, et une prémisse de fiche qui était périmée

### ⭐⭐ `F-UB-1` (fermé à l'ouverture) — le comportement indéfini annoncé n'existait plus, et c'est la SONDE qui le dit

`T3.25a` §1 annonçait un **débordement d'entier signé** sur `impulse_action_time + impulse_time`.
`T3.34`, mergé entre-temps, calcule la somme en `double` aux **quatre** sites des deux classes de
volet. La fiche était donc en défaut — **le sixième recensement de fiche pris en défaut de la
série** (ordinal corrigé au merge : le **cinquième** est celui de la revue de [`T3.32`](T3.32.md),
mergée entre l'écriture de ce paragraphe et son arrivée sur `master`), après `T3.55`, `T3.63`,
`T3.59` et deux autres — et `T3.21` lui-même en ajoute un septième (§ ci-dessous).

⭐ **Ce qui rend la mesure porteuse n'est pas le zéro, c'est le deux.** Un balayage sous
`-fsanitize=signed-integer-overflow` qui ne trouve rien ne prouve rien : il peut ne rien trouver
parce que le code est sain, ou parce que le chemin n'est pas atteint, ou parce que la sonde n'est pas
armée. La campagne a donc **remis la somme en `int`** et remesuré :

| Arbre | `runtime error` |
|---|---|
| livré | **0** |
| somme remise en `int` | **2**, `signed integer overflow: 2147483647 + 35 cannot be represented in type 'int'` |

*À recopier : une mesure d'ABSENCE ne vaut que si l'on a fait rougir la même sonde sur le même
chemin dans le même tour. Sinon on publie le silence d'un outil, pas l'état du code.*

⚠️ **Et le corollaire qui dérange** : le débordement de `T3.34` n'était **pas** invisible à un
`make check` vert. Sous la sonde, `ShutterImpulseLifetimeTest.PlainOutOfRangeImpulseLeavesNoTimerArmedForEver`
rougit **aussi** — la somme repassée négative arme une minuterie que libuv ne déclenchera jamais.
Un comportement indéfini **peut** avoir un capteur observable ; ne pas le supposer invisible.

### ⭐ `F-SHUT-1` (fermé) — le refus doit se mesurer par sa CONSÉQUENCE, et une conséquence se mesure par contraste

Le scénario « grande valeur ⇒ le volet part en course complète » est **observable par le
propriétaire de l'installation** : aucune minuterie d'arrêt n'est armée, le volet va à sa butée, et
le serveur répond `{"success":"true"}` pendant ce temps.

⭐ **Un seul volet ne suffit pas à le mesurer.** « Le volet n'a pas été vu s'arrêter dans le budget »
est aussi la réponse d'une boucle que personne ne pompe, d'un oracle cassé, ou d'une machine trop
lente. `AnOverflowingImpulseSendsNoShutterOnItsFullTravel` pilote donc **deux volets de la même
fixture dans un seul cas et un seul budget** : le premier reçoit une impulsion bien formée et **est
vu s'arrêter**, le second reçoit la valeur démesurée. C'est la première jambe qui rend la seconde
porteuse.
*À recopier : une assertion « rien ne s'est produit » n'est porteuse que si le MÊME tour montre que
quelque chose de comparable, lui, s'est bien produit.*

⚠️ **Et la fixture voisine ne pouvait pas héberger ce cas.** `core/SetStateGarbage_test` fixe
`time="0"` **délibérément**, précisément pour qu'aucune minuterie ne soit jamais armée : une fixture
qui n'arme rien ne peut pas voir une course complète. C'est la **quatrième** façon de mentir — la
fausse fixture — sous sa variante « la fixture était juste pour son ticket et fausse pour le
suivant ».

### ⭐ `F-T321-1` (fermé) — « une ligne » et « deux appelants », deux fois faux dans la même phrase

La fiche d'ouverture de `T3.21` annonçait « **correctif d'une ligne**, aucun appelant cassé (seuls
`InputString.cpp:37` et `InputAnalog.cpp:99` appellent `del_param` par ailleurs) ».

- **Trois** autres appelants, pas deux : `Scenario/AutoScenario.cpp` était omis. Recompté par un
  balayage sur la **forme** (`del_param` **et** `Params::Delete`), pas sur un nom — la même méthode
  que le recensement juste de `T3.38` et de `T3.59`.
- **Une ligne ne suffit pas**, et la raison se nomme : `del_param()` rendait `void`. ⛔ **Une garde
  muette ne peut pas dire non** — rebrancher l'appel sans lui donner une réponse aurait rendu
  `{"success":"true"}` pour un refus, c'est-à-dire le défaut d'origine déplacé d'un cran.

*À recopier : un correctif « d'une ligne » sur une garde EXISTANTE se vérifie en demandant d'abord
ce que la garde peut DIRE. Une garde qui rend `void` ne se rebranche pas, elle se complète.*

### ⭐ `F-ANALOG-1` (fermé) — `Exists()` prouve la clef, jamais le nombre, et le renommage PERSISTE sa décision

`InputAnalog::readConfig()` : un `period=` ou un `interval=` **présent et vide** donnait une période
de **0**, donc `if (sec >= frequency)` toujours vrai, donc `readValue()` **à chaque tour de la boucle
de règles** — mesuré, **20 lectures en 20 tours**. Sur une entrée Wago, 1-Wire ou Web c'est une
scrutation permanente du matériel : pas une valeur fausse, une **charge**.

⭐ **Et la branche de renommage est la seule des trois à écrire** : elle lisait `frequency` dans le
membre puis sérialisait ce membre dans `period`. Mesuré sur `master` : `period="0"` **dans la
configuration**. Le défaut survivait au redémarrage et **survivait au paramètre qui l'avait causé**.
*À recopier : trier les défauts de lecture de configuration selon qu'ils sont RELUS ou ÉCRITS. Un
défaut qui repart sur le disque n'est pas de la même famille que celui qui se recalcule au démarrage.*

⚠️ **Ce qui n'est pas décidé** : un `period="0"` écrit à la main vaut toujours « à chaque tour ».
C'est dans le sens documenté du paramètre ; le refuser serait une décision de produit.


### ⭐⭐ Revue de merge — la comparaison croisée est éprouvée DANS LES DEUX SENS, et c'est ce qui la distingue d'un littéral

`T3.21` §4 affirme que `TheRefusalIsTheOneSetParamAlreadyGivesForTheSameKey` compare les **deux
réponses entre elles** et non chacune à un littéral. Une affirmation pareille ne se relit pas, elle
se mesure — et il faut **deux** tours, parce qu'un seul ne sépare pas les deux hypothèses :

| Tour | Mutation | Le cas croisé | Les cas à littéral du même fichier |
|---|---|---|---|
| **CR-1** | le message de refus de `buildJsonSetParam()` **seul** renommé | ⛔ **ROUGE, et seul rouge du fichier** | **verts** |
| **CR-2** | le **même** message renommé **des deux côtés** | ✅ **VERT** | ⛔ **3 rouges** |

Un littéral recopié aurait rougi aux deux tours ; un cas vacuant aurait été vert aux deux. Le
croisement est donc réel : il voit les deux moitiés d'une règle **diverger**, et rien d'autre.
*À recopier : une assertion « ces deux réponses sont la même » se prouve par la paire de tours
— casser un côté DOIT rougir, casser les deux à l'identique DOIT rester vert. Un seul tour ne
distingue pas une comparaison croisée d'un littéral bien choisi.*

### ✅⭐⭐ `F-SHUT-2` (fermé) — la grammaire NON gardée d'`OutputShutterSmart` n'avait pas seulement une saturation : elle n'avait **aucun capteur du tout**

`T3.25a` §8 et [`T3.123`](T3.123.md) §1 fichent les grammaires en pourcentage d'`OutputShutterSmart`
(`set <n>`, `up <n>`, `down <n>`) comme **sans garde** contre la saturation. La revue a mesuré ce que
ce « sans garde » coûte réellement, par une contre-mutation que le développeur n'avait pas faite :
**échanger les deux directions** de ces grammaires — l'`up <n>` fait descendre le volet, le
`down <n>` le fait monter, `cmd_state` inchangé.

⇒ **`make check` reste VERT : `TOTAL 145 / PASS 144 / FAIL 0`, 0 cas rouge**, avec **90 `CXXLD`**
lus, donc le relink est vivant et le vert est porteur.

Ce n'est donc pas seulement une valeur absurde qui peut sortir par là : **le sens de marche du volet
n'est épinglé nulle part** sur cette classe. Un correctif futur qui poserait la garde de deux lignes
fermerait la saturation et laisserait ce trou-là entier.
*À recopier : avant d'écrire « X n'est pas gardé », échanger deux branches de X. « Pas gardé contre
une valeur » et « pas mesuré du tout » ne se réparent pas par le même geste.*

⭐⭐ **FERMÉ par [`T3.123`](T3.123.md), et le trou était PLUS LARGE que ce paragraphe ne le disait.**
La même mutation rejouée sur `master` avant d'écrire une ligne rend bien **0 cas rouge**
(`TOTAL 145 / PASS 144`, **4 `CXXLD`** — les quatre binaires qui relient `OutputShutterSmart.o`, donc
le relink est vivant). Mais une **seconde** mutation, sur un worktree `master` jetable, va plus loin :
croiser les **deux bornes physiques** dans `Up()` et `Down()` — `setOutputUp(true)` devient
`setOutputDown(true)` et réciproquement, ce qui fait tourner le moteur à l'envers pour **toutes** les
commandes de la classe, `up`, `down`, `toggle` et les deux impulsions comprises — laisse `make check`
**entièrement vert lui aussi** : **0 cas rouge**, **146 `CXXLD`**. ⇒ ce n'était pas le sens de marche
des trois grammaires qui n'était pas mesuré, c'était **celui de la classe entière**.

⭐ **L'oracle qui ferme les deux, et pourquoi ce n'en est pas un de plus qui relit le code.** Épingler
`sens == SHUTTER_UP` n'aurait relu que l'énumération que la branche mutée vient elle-même d'écrire.
`core/ShutterPercentGrammar_test` regarde à la place les deux choses qui existent **hors de la
logique** : la **borne alimentée**, comptée séparément pour chaque côté — ⚠️ `core/ImpulseOverflow_test`
les **additionne** dans un compteur unique et ne peut donc pas les distinguer —, et la **position
servie par `get_state`, échantillonnée à chaque tour de boucle pendant le mouvement**, dont l'excursion
entière est épinglée depuis une position **décentrée** (75 % pour une ouverture, 25 % pour une
fermeture). L'asymétrie est ce qui rend l'oracle porteur : les deux directions produisent des états de
même forme et de mêmes nombres, seul le **mouvement** diffère.
⭐ **La paire de tours prouve la fermeture** : la mutation qui rendait **0** rouge en rend **3**, et le
croisement des bornes dans `Up()` seul en rend **2**.

⚠️ **Et le recensement de la saturation, lui, était faux dans les deux sens.** Mesuré : le
`2147483647` **ne sort jamais** par le champ `state` — celui-là est `get_value_string()`, et
`writePosition()` borne la position à la course. Il sort par le **cache d'état** (`updateCache()`,
relu au démarrage suivant) et par `Rules/ActionStd.cpp`, et seulement quand `Up()`/`Down()` sortent
tôt : sur le chemin ordinaire, `cmd_state` est **réécrit** en un `"up"`/`"down"` nu deux instructions
plus loin. Il a fallu commander depuis **100 %** — un volet qui n'a plus de course devant lui — pour
que la valeur survive, et elle survit : `get_command_string()` et `cached["cmd_state"]` valent tous
deux `"set 2147483647"`.
*À recopier : « l'état publié » n'est pas un canal, c'en est plusieurs. Avant d'écrire qu'une valeur
est servie aux applications, nommer LE champ, et vérifier qu'aucune écriture ultérieure ne la
recouvre sur le chemin normal.*

### ⭐ Revue de merge de `T3.123` — la classe entière refaite, et ce que l'oracle ne couvre pas encore

⭐⭐ **`M-RELAY` est refaite sur `master` (`5a68cb53`), worktree jetable, `make distclean` puis build
complet : 0 cas rouge**, `TOTAL 146 / PASS 145 / SKIP 1 / FAIL 0`, **147 `CXXLD`**. Croiser les deux
bornes physiques dans `Up()` **et** `Down()` fait tourner le moteur à l'envers pour **toutes** les
commandes de la classe, et rien dans l'arbre ne le dit. ⇒ le trou était bien celui de la **classe
entière**, et non des trois grammaires.

⭐ **Et ce 0 rouge est porteur, parce que le même arbre sait rougir** : contrôle positif sur le même
fichier et le même worktree, `ImpulseUp(v)` ↔ `ImpulseDown(v)` ⇒ **8 cas rouges**
(`core/ShutterImpulse_test`).
⛔ **Deux cécités de plus, mesurées en passant sur `master`** : le littéral `"stop "` de
`get_value_string()` renommé ⇒ **0 rouge** ; ses mots `"up"` / `"down"` échangés ⇒ **0 rouge**. La
position servie par `OutputShutterSmart` n'était épinglée nulle part — le filet neuf ferme les deux
(le littéral renommé y rend **2 cas rouges**).

⭐⭐ **L'oracle voit le mouvement, et rien d'autre.** `CM-1` rejouée ⇒ **3 cas** nommés
(`AnUpByPercentOpensTheShutter`, `ADownByPercentClosesTheShutter`,
`ASetBelowThePositionOpensAndASetAboveItCloses`) ; `CM-4` ⇒ **2 cas**, les deux qui passent par `Up()` ;
la **perte** (les trois gardes retirées) ⇒ **6 cas**, exactement ceux que la fiche énumère ; le
**témoin** — le fichier réécrit **octet pour octet**, horodatage déplacé — ⇒ **0 rouge** avec
**5 `CXXLD`** lus. *Une permutation qui ne change aucune valeur rougit ; une réécriture qui n'en
change aucune non plus ne rougit pas.* C'est la 7ᵉ façon de mentir, et elle est fermée pour ces trois
grammaires.

⛔⭐ **CE QUI RESTE OUVERT, ET CE N'EST PAS CE QUE LA FICHE DISAIT.** « Les grammaires sans nombre sont
couvertes par accident par `CM-4` » ne vaut que pour les **bornes partagées** ; l'**aiguillage**, lui,
n'est couvert que pour les impulsions. Mesuré sur l'arbre livré, deux contre-mutations neuves qui ne
changent aucune valeur : `UpWait()` ↔ `DownWait()` ⇒ ⛔ **0 cas rouge** ; `ImpulseUp(v)` ↔
`ImpulseDown(v)` ⇒ **8 cas rouges**. ⇒ **les trois mots nus `up`, `down`, `toggle` n'ont toujours aucun
capteur de direction**, et ils partent avec un numéro plutôt qu'avec une ligne de « Nu »
(`T3.131`), avec le `set_state <n>` qui répond `true` sans rien faire.
*À recopier : « couvert par accident » se vérifie, et il se vérifie au NIVEAU où la mutation est
possible — une borne partagée et un aiguillage ne sont pas le même endroit.*

✅ **Les deux moitiés de la saturation sont vérifiées dans le code, pas seulement par le filet** : le
champ `state` est `get_value_string()`, dont `writePosition()` borne la matière à `[0, time_up]` ⇒ le
nombre saturé **ne peut pas** en sortir ; `cmd_state` part par `updateCache()` dans
`Config::SaveValueParams("<id>_<type>")`, que le constructeur **relit**, et `Rules/ActionStd.cpp` en
lit `get_command_string()` pour toute sortie `TSTRING` — les deux classes de volet en sont. Le chemin
d'échec précoce est `Down()` qui rend la main sur `pos >= total_time`, donc un volet **à 100 %**.
✅ **Et un volet part bien à sa butée par ce chemin** : depuis 25 %, `Down()` passe la garde, ferme sa
borne et arme `timer_end` à **42 949 672 s ≈ 497 jours**. La note de version antérieure disait le
contraire ; elle est corrigée.

### ⭐ Revue de merge — les trois autres mesures, refaites et non relues

- **La prémisse périmée** : `-fsanitize=signed-integer-overflow` sur les **4** binaires de la famille
  volet ⇒ **`TOTAL 4 / PASS 4`, 0 `runtime error`** sur l'arbre livré ; la somme remise en `int` aux
  **4** sites ⇒ **exactement 2** `runtime error`, `2147483647 + 35 cannot be represented in type
  'int'`, aux **deux** sites d'`OutputShutter.cpp` — `OutputShutterSmart` n'est jamais atteint, comme
  la fiche le déclare — et **1** cas rouge, `ShutterImpulseLifetimeTest.PlainOutOfRangeImpulseLeaves
  NoTimerArmedForEver`. **Le zéro et le deux, dans le même tour d'outil.**
- **La course complète** : la garde du volet simple retirée, `AnOverflowingImpulseSendsNoShutterOn
  ItsFullTravel` rend les **deux** symptômes annoncés — `victim->relayPulses` attendu 0 **obtenu 1**,
  et `pumpUntilSince(..., victim->isStopped, 1200 ms)` **obtenu -1** — pendant que la **première**
  jambe du même cas, le volet témoin, est bien vue s'arrêter dans le même budget. Le contraste
  fonctionne.
- **Le point de passage** : `set_value(std::string)` n'est défini que dans `OutputShutter.cpp` et
  `OutputShutterSmart.cpp` pour toute la famille. Balayage de **tous** les `.h`/`.cpp` suivis de
  `src/bin/calaos_server` : **aucune** des 7 classes matérielles (Wago ×2, Gpio ×2, KNX ×2, Mqtt ×1)
  ne le redéfinit — leurs gabarits (`WOVoletBase`, `ThinIo`, `GpioOutputShutterBase`, `MqttIOBase`,
  `KNXIo`) ne redéfinissent que `set_value_real()` et `readConfig()`. Les deux sites de base les
  couvrent toutes les sept. ⚠️ Les deux classes `MySensors*OutputShutter*` citées par de vieux
  artefacts de compilation **n'existent plus dans l'arbre** : seuls des `.o` périmés en portent le
  nom.
- **Le recensement de `T3.21` recompté** : `del_param()` a **3** appelants hors du site du ticket
  (`AutoScenario.cpp`, `InputString.cpp`, `InputAnalog.cpp`) — la fiche d'ouverture en annonçait 2 —,
  **aucune** sous-classe ne le redéfinit, et il reste **6** `Params::Delete()` ailleurs, tous hors des
  paramètres d'un IO vivant. Les deux comptes de la fiche livrée tombent juste.
- **Le rouge de départ** : les fichiers de production ramenés à `master`, tests de la branche en
  place ⇒ **5 / 7 / 6 = 18** cas rouges dans les trois filets neufs, plus **2** dans les deux suites
  modifiées (le cas renommé et `PlainOutOfRangeImpulseLeavesNoTimerArmedForEver`). Le compte annoncé
  tombe juste.

### ⚠️ `F-FLAKY-2` a reparu — deux fois, et sur les deux tours où le fichier muté ne le concerne pas

`core/MqttSidecarConfigWait_test` a flanché aux tours **CM-5** et **CM-6** de la campagne, toujours
seul, toujours sur `AnUnreachableBrokerEndsTheSidecarWithACauseAndANonZeroStatus`. CM-5 ne mute que
`JsonApi.cpp` : le lien de cause est exclu, c'est bien `F-FLAKY-2` / [`T3.112`](T3.112.md).
⛔ **Aucun `make check` n'a été relancé pour l'effacer.** Il n'a flanché à **aucun** des cinq
`make check` de l'arbre livré.

⭐ **Recompté à la revue de merge, et le taux est plus haut qu'aux revues précédentes** : sur les
**11** `make check` complets de cette revue, il a flanché **2 fois**, toutes deux dans la **même**
campagne de deux tours consécutifs, toujours **seul** et toujours sur le même cas
(`r.exited` faux, « the sidecar stayed alive with a broker it never reached »). Les **trois** tours
de référence finaux et les **cinq** tours de contre-mutation sont à `PASS 144`. ⛔ **Aucun `make
check` n'a été relancé pour faire disparaître ce rouge** : les deux tours rouges étaient les deux
tours prévus, et le tour suivant était une contre-mutation, pas une reprise. ⚠️ **L'utilisateur le
verra en CI**, et il faut le lire avec le fait qu'un `push` publie sans attendre les tests.

## T3.112 — `F-FLAKY-2` **FERMÉ** : le budget n'était pas trop court, le verdict était pris hors de l'arbre (2026-09-07)

- ⭐⭐ **[F-FLAKY-2, FERMÉ par [`T3.112`](T3.112.md)] — une adresse RFC 5737 ne mesure pas un
  sidecar, elle mesure la table de routage de la machine.** `AnUnreachableBrokerEndsTheSidecarWith
  ACauseAndANonZeroStatus` attendait qu'un `calaos_mqtt` visant `192.0.2.42` meure dans 8 000 ms.
  ⭐ **Mesuré, 300 `connect()` non bloquants depuis le conteneur** : l'erreur (`ENETUNREACH` dans
  tous les cas) arrive à **≈ 4 ms 286 fois**, **≈ 1 s 6 fois**, **≈ 2 à 4 s 6 fois**, **≥ 5 s
  2 fois**. ⇒ **c'est l'échelle de retransmission du SYN — 1 s, 2 s, 4 s, 8 s — et le budget du cas
  était posé sur un de ses barreaux.** Allonger le budget vise le barreau suivant.
  ⭐⭐ **ET LES DEUX ROUGES SE PRODUISENT À LA DEMANDE, SANS TOUCHER UNE LIGNE DE CODE** — seule la
  table de routage du conteneur change : `192.0.2.0/24` renvoyé vers une interface `dummy` (le SYN
  part et disparaît) ⇒ **3 échecs / 3**, `r.exited` faux à 8 019 ms, avec le message exact que ce
  journal relève depuis trois revues ; `ip route add blackhole 192.0.2.0/24` ⇒ l'échec devient
  **synchrone** et c'est l'**autre** ancre du cas qui rougit, **3 échecs / 3**. Même machine, même
  charge, même binaire.
  ⛔ **Corollaire de méthode, et il vaut au-delà de ce cas** : *un flottement dont la cause est hors
  de l'arbre n'a pas de taux — il a un taux par machine et par instant.* Sur le réseau du jour, le
  cas rendait **0 échec sur 100 à vide et 0 sur 30 sous charge**, là où la revue de `T3.101` avait
  mesuré **5 sur 10**. Les deux chiffres sont vrais. **Une campagne de comptage seule aurait conclu
  « plus de défaut » sur un arbre inchangé** ; ce qui prouve quelque chose, c'est la reproduction à
  la demande.
  ⭐ **Le remède est côté test, ZÉRO ligne de `src/`** : le pair est désormais sur la boucle locale
  et appartient au fichier — il lit le CONNECT puis raccroche **avant** son CONNACK, ce qui produit
  la même perte asynchrone (descripteur enregistré par `connect_async()`, puis fermé par la
  bibliothèque) **au premier tour de boucle**. **0 échec / 40 à vide, 0 / 25 sous charge**, durées
  **27 à 43 ms** au lieu de **22 à 8 019 ms** ; et **0 / 3 sous chacun des deux routages forcés** qui
  rougissaient 3 fois sur 3.
  ✅ **Les trois moitiés sont gardées** — la mort, la cause, le statut — et trois assertions
  s'ajoutent (pas de session acceptée, CONNECT vu, pas de sortie sur le délai de configuration).
  ⛔ **Ce qui est PERDU, et il faut le dire** : une vraie panne de routage n'est plus jouée. Le texte
  que le sidecar imprime pour ces codes reste épinglé par `ARefusedPortIsNamedARefusalAndNotAn
  UnrelatedErrno`, seul défaut que ce chemin ait jamais eu.

- ⭐ **[F-FLAKY-2] La contre-mutation qui compte est celle qui REMET le défaut d'origine, et elle
  rougit.** Les deux issues de `pumpBroker()` échangées — un courtier sain déclaré perdu, un
  courtier perdu déclaré sain — c'est-à-dire *« le sidecar survit à une perte asynchrone du
  courtier »*, le défaut même que ce cas existe pour tenir ⇒ **4 cas rouges**, dont le cas
  déterministe. ⚠️ **C'est la vérification qui sépare une fixture rendue déterministe d'une fixture
  rendue muette** : un cas déterministe *en cessant de mesurer* aurait été vert là. Deux autres
  échanges, ensembles rouges deux à deux distincts : les deux titres de journal du courtier
  échangés ⇒ **3** ; les deux réponses de `procMain()` échangées ⇒ **2**. Témoin (les 2 fichiers
  réécrits à l'identique, horodatage déplacé) ⇒ **0 rouge**, **2 `CXXLD`** lus.

- ⚠️ **[F-FLAKY-1 — mis à jour : la famille est RECENSÉE, et son dernier membre EXPOSÉ vient de
  tomber]** `F-FLAKY-1` est fermé depuis [`T3.49`](T3.49.md) pour son fichier
  (`core/ShutterImpulse_test`), mais la **forme** — un `EXPECT` posé juste après une échéance
  d'horloge murale que rien ne garantit — n'avait jamais été comptée dans tout `tests/`.
  ⭐ **Balayage de `T3.112`** : **52** `sleep_for`/`usleep`/`time.sleep` bruts dans **30** fichiers ;
  **171** attentes à budget en millisecondes dans **28** fichiers (`runLoopUntil` 54, `waitUntil`
  33, `pumpLoopFor` 32, `pumpUntilSince` 23, `pumpLoopUntil` 21, `pumpUntil` 4, `goIdleFor` 4 — et
  **aucun harnais partagé**, chaque fichier redéfinit le sien, sauf `ExternProcSpawnHarness.h` et
  `RoonSpawnHarness.h`) ; ~**70** adresses non-loopback en dur dans **18** fichiers.
  ⭐⭐ **Une seule de ces ~70 adresses était réellement COMPOSÉE** — donnée à un `connect()` plutôt
  qu'analysée comme une chaîne ou vouée à `EADDRNOTAVAIL` : `192.0.2.42`, celle de `F-FLAKY-2`.
  **Elle disparaît, et il n'en reste aucune.**
  ⚠️ **Le sous-ensemble encore EXPOSÉ est de 16 cas, tous de la même famille** : `UrlDownloader_test`
  (6), `UrlDownloaderLogUrl_test` (6), `UrlDownloaderLogSecret_test` (4) attendent un **`curl`
  forké** par `runLoopUntil(…, 15000)`. ⛔ **Aucun n'a jamais été vu rouge** et la marge est de deux
  ordres de grandeur — c'est une **forme** à retirer, pas un rouge à éteindre ⇒
  [`T3.128`](T3.128.md), **neuve**. Le reste des 171 attentes est **non exposé** : l'événement
  attendu est produit dans le même processus par une boucle que le test pompe.
  ⛔ **Et rien ne défend l'arbre contre le retour d'une adresse hors boucle locale** : le balayage
  est un relevé daté, pas une sonde. Une sonde statique de la famille de `check-order-sentinels.sh`
  fermerait la classe — **non écrite**.

- ⭐ **[F-FLAKY-2 → `T3.125`] Ce ticket est le prérequis de la seule garde qui protégerait les
  livraisons.** `docker-publish-dev.yml` publie sans attendre le moindre test (`F-DEP-8`), et
  [`T3.125`](T3.125.md) B et C — poser un `workflow_run` ou une protection de branche — étaient
  explicitement **bloqués** par ce flottement : conditionner la publication à une CI verte fait
  **manquer des livraisons** tant qu'une suite échoue par intermittence. ⚠️ **La CI n'a toujours
  jamais tourné** : ce ticket retire la seule raison connue pour laquelle elle aurait clignoté, il
  ne dit rien des `SKIP 4` attendus sur un exécuteur sans IPv6.

## T3.112 — ce que la revue de merge a mesuré en plus, et les deux corrections (2026-09-07)

- ⭐⭐ **[F-FLAKY-2, FERMÉ] Les deux rouges ont été refaits par la revue, sur le binaire de `master`,
  et ils reviennent à la demande.** Conteneur `--cap-add=NET_ADMIN`, sans toucher une ligne :
  `192.0.2.0/24` renvoyé vers une interface `dummy` ⇒ **3 rouges / 3**, `r.exited` faux à
  **8 018-8 019 ms** ; `ip route add blackhole 192.0.2.0/24` ⇒ **3 rouges / 3** à **22-23 ms**, sur
  l'**autre** ancre. Le binaire livré, sous les **mêmes** deux routages : **3 verts / 3**, et la
  **suite entière 9/9 trois fois** sous le routage `dummy`.
  *À recopier : ce qui prouve qu'un flottement est compris n'est pas un taux « après » à zéro, c'est
  la capacité à rallumer le rouge d'origine à volonté et à le voir s'éteindre sur l'arbre corrigé.*

- ⛔⭐ **[F-FLAKY-2] La queue de l'échelle de retransmission dépend de la façon dont on la mesure —
  et c'est une correction de la fiche, pas un désaccord.** Refaite par la revue, 300 `connect()` non
  bloquants sur la même image et la même adresse : **128 < 50 ms**, **85 ≈ 1 s**, **65 à 2-4 s**,
  **22 ≥ 5 s** dont **11 au-delà de 12 s** ; **289 `ENETUNREACH`**. La fiche annonce **286/6/6/2**.
  Les deux relevés sont vrais : en **rafale**, l'ICMP *unreachable* est limité en débit et le SYN
  part sur son échelle (1 s, 2 s, 4 s, 8 s) ; **en isolation**, l'erreur revient en 4 ms. ⇒ le
  raisonnement du ticket tient — **le budget de 8 s est posé entre deux barreaux** — et le corollaire
  se durcit : *un taux publié sans dire s'il a été mesuré en rafale ou en isolation ne se compare à
  rien.*

- ⛔⭐ **[F-FLAKY-2] Une assertion neuve sur trois est MASQUÉE par l'ancre qui la précède, et ne peut
  jamais parler.** `EXPECT_FALSE(logCarries(r, "waiting for its configuration"))` : toute ligne
  portant ce texte vient d'un `awaitConfiguration()` qui a rendu `false`, donc d'un `setup()` qui
  abandonne **avant** de connecter ⇒ l'`ASSERT_TRUE(logCarries(r, "Connect to : …"))` placée avant
  elle a déjà arrêté le cas. **Mesuré** : l'échéance de configuration inversée ⇒ **1 seul cas
  rouge**, `AConfigurationThatNeverArrivesEndsTheSidecarAfterItHasWaited`, jamais celui-ci. Elle ne
  nuit pas, elle documente — mais **elle ne compte pas comme un capteur**, et la fiche est corrigée.
  *À recopier : une assertion placée après un `ASSERT` qui exclut déjà son cas d'échec n'est pas un
  oracle de plus, c'est un commentaire exécutable. Compter les assertions gagnées sans vérifier
  laquelle peut parler, c'est publier de l'assurance.*

- ⭐⭐ **[F-FLAKY-2] Le cas déterministe exige encore une VRAIE perte — mesuré par une
  contre-mutation que la fiche n'avait pas faite.** Le courtier local qui ferme son **auditeur** au
  lieu de sa **connexion** — donc la connexion acceptée reste ouverte, et il n'y a plus rien à
  perdre — rend **exactement** le rouge historique : `r.exited` faux à **8 028 ms**, *« the sidecar
  stayed alive with a broker it never reached »*. ⇒ **un bouchon qui ne perd rien ne donne pas un
  vert** ; la fixture n'est pas un décor, et le budget de 8 s reste utile comme garde-fou contre une
  régression qui **pendrait** `make check` au lieu de le faire rougir.
  ⭐ Et la contre-mutation jumelle **CR-A** — le courtier répond son CONNACK là où il devait
  raccrocher — laisse les **trois moitiés d'origine vertes** (une session établie puis perdue tue
  aussi le sidecar) et ne rougit que sur `ASSERT_FALSE(broker.sentConnack)` : **l'assertion neuve est
  bien l'oracle qui sépare ce cas de son voisin**, c'est-à-dire la parade à la *fixture fausse*.

- ⚠️ **[F-FLAKY-2] Précision sur CM-1, corrigée au merge.** La fiche écrit que CM-1 dit que le cas
  « voit encore la **mort** ». Rejouée : les 4 cas annoncés rougissent bien, mais dans le cas neuf
  l'assertion qui parle est la **cause** — `"Lost the connection to the broker"` absent — pendant que
  `r.exited` reste vrai. Le sidecar meurt quand même, par l'`EBADF` du descripteur périmé que
  `brokerLost()` n'a plus retiré ; ce qu'un opérateur perd est **la ligne qui dit pourquoi**. C'est
  bien le défaut d'origine, et le cas déterministe le rougit — mais par son autre moitié.

- ⛔⭐ **[F-HARNESS] Un pilote de contre-mutation en `set -e` meurt entre la mesure et la
  restauration — septième membre de la famille du `| head`.** Vécu à cette revue : le pilote portait
  `set -e`, le premier tour rouge — et un tour de contre-mutation est *fait* pour être rouge — a
  rendu un code de sortie non nul au `docker run`, et le script s'est arrêté **avant** sa
  restauration, laissant l'arbre muté. Rattrapé par le `git status` sur l'**HÔTE**, exactement comme
  les deux fois précédentes. **Parade** : `set +e` dans un pilote de contre-mutation, et une
  restauration qui ne dépend d'**aucun** code de sortie.
  *À recopier : dans un harnais de contre-mutation, `set -e` n'est pas une sécurité — le seul code de
  sortie qu'on attend est un échec.*

- ⚠️ **[F-FLAKY-1] Le recensement des adresses est recompté, et un piège de comptage est nommé.**
  Balayage indépendant : **80** littéraux non-loopback (RFC 5737/3849, `example.*`) dans **15**
  fichiers de `tests/`, **aucun** donné à un `connect()` sur la branche livrée — en-têtes
  `X-Forwarded-For`, charges comparées en mémoire, argv de sidecars bouchonnés, `bind()` voués à
  `EADDRNOTAVAIL`. Tous les vrais `connect()` visent `INADDR_LOOPBACK`, `in6addr_loopback` ou
  `AF_UNIX`. ⇒ le « une seule composée » du ticket **tombe juste**.
  ⛔ **Le piège à nommer pour le prochain balayage** : `core/MqttConfigTransport_test.cpp` porte **le
  même littéral `192.0.2.42`**, mais son sidecar est remplacé par le script enregistreur
  d'`ExternProcSpawnHarness.h` — l'adresse ne quitte jamais la trame de configuration. Un balayage
  **textuel** en aurait compté deux ; ce qui décide, c'est le sort du littéral, pas sa présence.

## T3.124 — la garde du modèle, mesurée jusqu'au disque (2026-09-07)

- ⭐⭐ **[F-STRUCT-1] La clef du cache d'états n'est épinglée par RIEN, et deux paramètres
  structurels restent hors de toute garde.** `T3.124` ferme la famille que la **fabrique** consulte
  (`type`) en demandant à `IOFactory` si l'IO serait encore reconstructible après l'écriture. Deux
  trous mesurés au même passage :
  - **La clef du cache d'états** — `get_param("id") + "_" + get_param("type")`, construite **trois
    fois** dans l'arbre (`IOBase`, `OutputShutter`, `OutputShutterSmart`) sans constante partagée.
    **Contre-mutation CM-6** : les deux opérandes **échangés** dans `IOBase` laissent `make check`
    **entièrement vert — `TOTAL 146 / PASS 145`, 0 cas rouge**, 90 `CXXLD` lus. La position
    persistée des volets et l'étranglement des notifications de batterie sont indexés dessus.
  - **La seconde famille de paramètres structurels** — `autoscenario_uid` et l'espace `as_*` sont
    relus par `AutoScenarioDef::loadFromParams()`, **pas** par `IOFactory` : `canCreate()` répond
    oui sans eux, donc `del_param(io, "autoscenario_uid")` répond toujours `{"success":"true"}` et
    l'IO revient du disque **sans sa définition de scénario**. ⛔ Établi **par lecture** : le cycle
    disque a été joué pour `type`, pas pour celui-là.
  ⇒ [`T3.129`](T3.129.md). ⚠️ `OutputShutter*` appartient à [`T3.123`](T3.123.md) : ordonner après
  lui, ou se limiter au site d'`IOBase`.

- ⭐ **La fiche d'ouverture de `T3.124` nomme le mauvais site de chargement** — septième recensement
  de fiche pris en défaut de la série. Elle attribue la perte à `ListeRoom::createIO()`, qui sert la
  **création** (`autoscenario create`, `JsonApi`). Une `io.xml` est relue par
  `Room::LoadFromXml()` → `IOFactory::CreateIO(pugi::xml_node)`. Les deux perdent l'IO, mais c'est le
  second qui porte la perte de données, et c'est le seul qui journalise quelque chose — avec un nom
  de type **vide**, ce qui ne nomme pas l'équipement perdu.

- ⭐⭐ **La liste de noms et le critère refusent la même chose, À UNE MIGRATION PRÈS — et c'est tout
  ce que la mesure départage.** La contre-mutation **CM-5** remplace le critère
  (`IOFactory::canCreate()` sur les paramètres candidats) par la liste nominative (`opt == "type"`)
  et rend **un seul cas rouge** : celui qui change le type d'un IO vers un **autre type
  enregistré**, que la liste refuserait et que le critère accepte parce que rien n'est perdu. Le
  critère n'est donc pas plus **large** sur cet arbre ; il est **dérivé du chargeur** au lieu d'être
  **déclaré**, et c'est la seule raison de le préférer. *À recopier : quand on remplace une liste par
  un critère, mesurer la liste — sinon on publie une élégance, pas une couverture.*

- ⭐ **[revue de merge] La conséquence de la SECONDE famille structurelle est densément mesurée ;
  ce qui manque est le chemin d'API.** Contre-mutation neuve **CR-A** : `autoscenario_uid` et
  `autoscenario_steps` **échangés** dans `AutoScenarioDef::loadFromParams()` — une définition revient
  du disque sans ses étapes — rougit **61 cas sur 8 binaires**. ⇒ un cas qui jouerait
  `del_param(io, "autoscenario_uid")` + `saveConfig()` + `reloadFromDisk()` atterrirait sur des
  assertions **qui existent déjà** : `F-STRUCT-1` se ferme, côté scénario, par quelques lignes de
  test et non par un filet neuf. *À recopier : « rien ne le garde » et « rien ne le mesure » sont
  deux constats différents, et on ne les répare pas au même prix.*

- ⭐ **[revue de merge] Les deux moitiés de la garde ont chacune leur oracle.** Contre-mutation neuve
  **CR-B** : les deux arguments du candidat de `set_param()` **échangés** (`Add(val, opt)`) rendent la
  moitié `set_param` aveugle et **2 cas rouges seulement**, `ATypeNoDriverRegistersIsRefusedLikeA
  MissingOne` et le cas croisé — les littéraux `del_param` restent verts. ⇒ aucune des deux moitiés
  ne se repose sur les capteurs de l'autre, ce qu'une contre-mutation du seul prédicat partagé
  (CM-1) ne pouvait pas dire.

- ✅ **[revue de merge] La rupture d'API assumée n'a pas de victime dans `calaos_installer`.**
  Dépôt frère lu : **0 occurrence** de `del_param`, aucun verbe de l'API JSON en écriture de
  paramètre, et un unique canal réseau d'écriture qui pousse `io.xml`/`rules.xml` **entiers**. Son
  éditeur générique clef/valeur refuse déjà `type` en suppression **et** en modification, et n'est
  ouvert que pour les pièces et les règles ; les valeurs de `type` viennent de listes fermées
  alimentées par l'`iodoc`. ⇒ la rupture reste vraie pour un script tiers, et pour lui seul.

- ⭐ **La quatrième manière de mentir s'est produite, et seule l'exécution sur l'arbre NON corrigé
  l'a dit.** Le cas qui lit les octets d'`io.xml` cherchait `type="` dans le nœud — sous-chaîne de
  `gui_type="` **et** de `io_type="`, que tout nœud d'IO porte. Il était **VERT sur `master`**, où
  l'attribut a réellement disparu. L'aiguille porte désormais son espace de tête.

- ⛔⭐ **[F-TOOL-1] Un harnais de contre-mutation rangé dans le scratchpad PARTAGÉ de la session se
  fait écraser par un agent voisin.** Vécu ici entre les tours CM-5 et CM-6 : un agent concurrent a
  écrit son propre `cm.py` par-dessus. Aucune mesure faussée — la campagne s'est arrêtée sur une
  `KeyError` au lieu de muter, et les sha256 des instantanés, de `HEAD` et de l'arbre coïncidaient
  encore — mais **rien dans la sortie ne l'aurait signalé** si le fichier écrasé avait été un *autre
  harnais du même nom* plutôt qu'un script incompatible. C'est le 5ᵉ piège (l'instantané réutilisé)
  déplacé d'un cran : *le harnais lui-même se nomme par un chemin qui lui appartient, et n'a rien à
  faire dans un répertoire que d'autres agents écrivent.*
  ⚠️ **Numérotation corrigée à la revue** : la fiche l'appelait `F-TOOL-7`, mais aucun `F-TOOL-1`
  à `F-TOOL-6` n'existe dans cet arbre — c'est le **premier** de sa famille de findings. Et dans la
  série des pièges d'outillage d'`ORCHESTRATION.md` il est le **huitième**, pas le septième : le
  septième est le harnais qui meurt avant sa restauration sous `set -e` (revue de `T3.112`).

- 🟡 **[F-DEP-10] `docker-publish.yml:25` porte encore le commentaire faux que `T3.22` avait corrigé
  dans l'autre fichier.** Relevé à [T3.125](T3.125.md). La ligne
  `# run only when code is compiling and tests are passing` annonce une garde qu'aucun `needs:` ni
  aucun `if:` n'implémente — c'est mot pour mot celle qui a ouvert `F-DEP-8`, et elle survit dans le
  workflow qui publie le **plus** : `:latest` et un paquet **non** prerelease. Elle est moins
  dangereuse ici, parce que le déclencheur est un `workflow_dispatch` manuel : c'est un humain qui
  décide. ⚠️ Mais un humain qui lit cette ligne croit que la CI le retiendra. ⇒ soit corriger le
  texte, soit poser la garde ; le ticket ne l'a **pas** touché, pour que sa vérification « aucun
  autre workflow modifié » reste probante.

- 🟡 **[F-DEP-11] Chaque publication relance la CI complète.** Relevé à [T3.125](T3.125.md).
  `docker-publish-dev.yml` crée un tag git, et le `on: push:` de `ci.yml` n'a **aucun** filtre de
  ref : une poussée de tag déclenche donc tous ses jobs. Coût préexistant. ⚠️ Depuis le
  conditionnement c'est aussi une **source de déclenchement** du workflow de publication, écartée
  par `branches: [ master ]` — le `head_branch` d'une exécution née d'un tag est le nom du tag.
  ⛔ **Non observé** : c'est l'un des points que seul le premier `push` tranchera.

- ⚠️ **[F-DOCS-2] Un marqueur de conflit `<<<<<<< HEAD` a survécu dans `BOARD.md`.** Trouvé à
  [T3.125](T3.125.md), ligne 293, introduit par la résolution « les deux côtés gardés » du merge de
  `T3.123` (`d0123b42`). Le fichier est du Markdown : rien ne le compile, rien ne le rougit, et
  `check-docs.py` ne lit que les citations `Fichier.cpp:ligne`. Retiré ici. *À recopier : une
  résolution qui garde les deux côtés se relit en cherchant les trois marqueurs, pas seulement en
  vérifiant que le contenu attendu est là.*

- ⛔⭐⭐ **[F-DEP-12] Les deux conditions les plus difficiles de la garde de publication ne sont
  tenues par rien.** Mesuré à la revue de merge de [T3.125](T3.125.md) ⇒ [T3.133](T3.133.md).
  `.github/check-workflow-gating.py` refuse **exactement** les trois formes qu'il annonce — contrôle
  positif : les quatre contre-mutations du ticket rendent **1 plainte chacune**, témoin **0**. Cinq
  contre-mutations **neuves** sur le même arbre rendent **0 plainte** : `workflow_run.event ==
  'push'` retiré (⛔ la **porte des secrets** se rouvre : la complétion d'une PR de fork lève un
  `workflow_run` sur le dépôt de base, avec `secrets.ACTION_DISPATCH`) · `head_sha == github.sha`
  retiré (⛔ `negz/create-tag@v1` étiquette alors un commit **non testé**) · `branches: [ master ]`
  retiré (⛔ **boucle** : le tag que la publication crée redevient déclenchant) · `ref:` du
  `checkout` retiré (⛔ l'image se construit sur la branche par défaut, pas sur l'arbre testé) · le
  job `workflow-gating` **entier** retiré. ⇒ ces quatre lignes ne sont tenues **que par le fichier
  lui-même**, ni en local ([T3.132](T3.132.md)) ni en CI. *À recopier : un contrôle mécanique dit ce
  qu'il refuse ; il ne dit jamais ce qu'il laisse passer.*

- ⚠️ **[F-DEP-13] `actionlint` ne typecheck pas `github.event.*`.** Mesuré à la revue de merge de
  [T3.125](T3.125.md). Il type l'objet `github` (`github.evnt` ⇒ plainte) et les types d'activité
  (`types: [ compleeted ]` ⇒ plainte), mais `event` y vaut `object` : `workflow_run.conclusion` mal
  orthographié passe **sans un mot**, et la garde devient alors toujours fausse — donc plus rien ne
  publie, silencieusement. ✅ Le contrôle du dépôt l'attrape (mesuré : 1 plainte) ; l'analyseur, non.
  ⚠️ Le « 12 diagnostics avant / 12 après » de la fiche est le compte d'un `actionlint` **sans
  `shellcheck`** ; avec, il y en a **14** (deux `SC2046` préexistants dans `ci.yml`) — les listes
  restent identiques des deux côtés dans les deux configurations.

- ⚠️ **[F-DEP-14] `declared_name()` retombe sur le nom de base, GitHub sur le chemin.** Relevé à la
  revue de merge de [T3.125](T3.125.md). Quand un workflow n'a pas de `name:`,
  `.github/check-workflow-gating.py` l'enregistre sous `ci.yml`, alors que GitHub le nomme
  `.github/workflows/ci.yml`. Un `workflow_run` citant `"ci.yml"` contre un workflow sans `name:` est
  donc **accepté** alors qu'il ne se déclencherait jamais. Étroit — il faut deux fautes — et fermé
  par une ligne. Issue **C** de [T3.133](T3.133.md).

## T3.116 — la mesure d'écho aveugle, et le foin vide (2026-09-07)

- ⛔⭐⭐ **[F-LOGSECRET-10] Un foin VIDE satisfait les huit bornes d'écho, et aucune anti-vacuité de
  l'arbre ne le voit.** [`T3.116`](T3.116.md) ferme la moitié « la mesure est cassée » — chaque
  mesure qu'une borne lit est épinglée par deux littéraux, et `check-echo-measures.sh` tient la
  classe. La seconde moitié est **mesurée ouverte** : le premier argument remplacé par
  `std::string()` aux **39** sites de borne des huit suites, mesure intacte et ancres vertes, laisse
  `TOTAL 148 / PASS 147 / SKIP 1 / FAIL 0`, ⛔ **0 rouge**. Les quatre anti-vacuités de l'arbre
  (`ASSERT_TRUE(obs.parseFailureSeen)`, `ASSERT_NE(npos, ex.log.find("network"))`,
  `ASSERT_FALSE(stock.empty())`, `ASSERT_TRUE(onTheWire)`) gardent **l'observation**, jamais
  **l'argument que la borne reçoit** — entre les deux il y a une expression, et rien ne la tient.
  ⇒ [`T3.134`](T3.134.md). *À recopier : épingler ce qu'un capteur SAIT FAIRE ne dit rien de ce
  qu'on lui DONNE ; ce sont deux aveuglements, et une seule parade n'en ferme qu'un.*

- ⛔⭐ **[F-TEST-4] Un recensement qui cherche un NOM manque ce qui porte un autre nom, et le
  huitième recensement de fiche de la série est pris en défaut par là.** La fiche d'entrée de
  `T3.116` annonçait **sept** suites portant l'emballage d'écho ; il y en a **huit** —
  `UrlDownloaderLogSecret` appelle le sien `longestBodyEcho`, et sa borne a exactement la même
  propriété. Le recomptage corrige aussi **12** appels et non 13 pour `core/HttpRequestLogSecret`
  (la treizième occurrence est la **définition**) et **deux** suites qui n'épinglent rien du tout au
  lieu de trois. ⭐ **La sonde livrée n'a pas ce défaut par construction** : elle part des **bornes**
  qu'elle trouve et remonte à la mesure qu'elles lisent, au lieu de chercher un nom — c'est
  précisément ce qui a fait sortir `longestBodyEcho`. *À recopier : un recensement par nom mesure
  l'orthographe qu'on avait en tête, pas le parc.*

- ⭐ **[revue interne] Un auto-test de sonde qui n'exerce pas la SÉLECTION DE SON CORPUS ne dit rien
  de son silence — et c'est réparable en deux cas.** `check-echo-measures.py` appelle son propre
  balayage (`scan()`) sur un répertoire **vide** puis sur un répertoire d'**un** fichier écrit pour
  être refusé, et `main()` refuse un balayage qui n'a lu aucun fichier. C'est le trou de
  `check-docs.py` ([`T3.127`](T3.127.md)) fermé au moment d'écrire la sonde plutôt qu'après ; les
  quatre sondes antérieures passent toujours leur corpus **en argument**.

- ⭐ **[mesure] Ce qui distingue une borne à protéger d'une borne qui se garde toute seule : le
  nombre de côtés.** `EXPECT_LT(m, k)` est **unilatérale** — « rien » la satisfait — tandis que la
  calibration `EXPECT_EQ(k, worst.size() + 1)` est **bilatérale** : une mesure aveuglée la déplace
  autant qu'une mesure élargie. Mesuré des deux côtés : `longestHexRun` rendue `""` dans les neuf
  suites ⇒ **6 binaires rouges**, exactement celles qui l'épinglent, et **3 muettes**. C'est la règle
  que la sonde énonce, et elle explique pourquoi cinq suites étaient déjà protégées sans que
  personne l'ait décidé.

- ⭐ **[revue de merge] La règle d'exemption tient — mesurée, pas raisonnée — mais sa RECONNAISSANCE
  est plus étroite que la notion qu'elle énonce.** Les **cinq** mesures que `check-echo-measures.sh`
  exempte (celles qu'aucune borne unilatérale ne lit) ont été aveuglées une à une : **1 binaire / 1
  cas rouge à chaque fois**. ⛔ En revanche la sonde ne reconnaît comme unilatérale que
  `(EXPECT|ASSERT)_(LT|LE)(<mesure>, <plafond>)` : une borne **retournée**
  (`EXPECT_GT(<plafond>, <mesure>)`) ou **emballée** (`EXPECT_TRUE(<mesure> < <plafond>)`) est tout
  aussi unilatérale et lui est invisible — la mesure n'est même pas comptée. Aucun site de l'arbre
  n'est dans ce cas. *À recopier : une sonde énonce une notion et reconnaît une forme ; ce sont deux
  périmètres, et c'est l'écart entre eux qui se mesure.*

- ⛔⭐ **[F-TEST-5] Un auto-test de sonde ne couvre que les FORMES qu'il écrit, et celle qui manque
  ici est un site réel.** Contre-mutation neuve à la revue de `T3.116` : le balayage de
  `check-echo-measures.py` restreint aux corps de **cas gtest** — les corps d'aide écartés — laisse
  l'**auto-test vert** et l'arbre livré **vert**, tout en perdant en silence une mesure réelle (25 →
  24 ; 13 → 12 plaintes sur l'arbre d'avant), celle de `core/IncomingLogStockLevel`, dont l'unique
  borne vit dans une fonction d'aide. **Aucun** des 11 fichiers de l'auto-test ne porte de borne hors
  d'un cas. C'est exactement la classe fichée à `T3.63` — une sonde se casse là où son auto-test ne
  regarde pas — et **un douzième fichier la ferme**.

- ⭐ **[mesure] Une ancre littérale épingle une VALEUR, jamais un RÔLE : les paramètres des huit
  emballages intervertis ⇒ 0 rouge.** Les mesures d'écho sont des plus longues sous-chaînes
  communes, donc symétriques : la permutation est un **mutant équivalent en valeur**. Mais les deux
  littéraux choisis par les ancres sont eux-mêmes **symétriques** (`5` dans les deux sens, `0` dans
  les deux sens), donc une ancre de cette forme ne distinguerait pas non plus une mesure
  **asymétrique**. *À recopier : quel argument est le foin n'est tenu par rien — c'est la même
  nudité que le foin vide, prise par l'autre bout.*

- ⭐ **[mesure] Le foin vide se ferme à peu de frais, et la fiche neuve le craignait à tort.**
  [`T3.134`](T3.134.md) redoutait qu'un foin sans nom (`ex.stock()`) empêche la règle statique.
  Compté : les **39** opérandes de foin des huit suites sont **39 identifiants** (`ex.log`,
  `run.log`, `obs.log`, `failure`, `stock`, `reduced`) et **0** un appel, pour **10** couples
  (suite, nom) distincts. L'anti-vacuité coûte une dizaine d'assertions, pas 39, et le lien lexical
  que la sonde suit déjà suffit à la tenir.

## T3.117 — un identifiant dupliqué, et le harnais qui a menti sur sa propre mutation (2026-09-07)

- ⭐⭐ **[F-SCEN-1] Un `autoscenario_uid` dupliqué rendait un des deux scénarios INERTE, sans
  suppression et sans un mot.** La fiche d'instruction ne portait que la moitié « suppression ». Or
  la passe de démarrage reconstruit dans l'ordre du fichier et `rebuildRules()` commence par
  `destroyRules()`, **qui détruit par uid** : la reconstruction du second détruit les règles que le
  premier vient de générer et régénère les siennes sous **les mêmes noms**. Mesuré : les deux
  scénarios sont listés, `autoscenario get` rend à chacun **sa propre** définition, et **le bouton de
  l'un n'exécute aucun pas**. Aucun drapeau `broken`, aucun `disabled_missing_io` — à définitions de
  même taille `expectedRuleCount()` tombe juste, puisqu'il compte les règles de l'autre. ✅ **FERMÉ**
  par [T3.117](T3.117.md) (re-cléage du second, sur le modèle entier, avant toute dérivation).

- ⛔ **[F-SCEN-2] `set_param` écrit `autoscenario_uid` sur n'importe quel IO, et rien ne l'en
  empêche.** `IOBase::set_param()` n'a **aucune liste blanche de clefs** : seuls `id` et les deux
  gardes (octets non écrivables en XML, IO que la fabrique ne saurait plus construire) refusent
  quelque chose. La commande est atteignable sur les **trois** transports — WebSocket, HTTP et Lua.
  ⚠️ Elle est **inerte sur le moment** (l'`AutoScenario` est bâti par le constructeur de l'IO), donc
  le doublon n'apparaît qu'au démarrage suivant — ce qui est précisément pourquoi une garde posée sur
  un verbe de création ne l'aurait jamais vue. `T3.117` **répare** au démarrage suivant ; il ne
  **refuse** pas à l'écriture. → [T3.137](T3.137.md).

- ⛔ **[F-SCEN-3] Trois lignes du correctif de `T3.117` ne sont tenues par rien**, mesurées à 0 rouge
  par deux contre-mutations par échange. `declareDefinition()` ré-alloue un uid dès que `def->uid` est
  vide et réécrit `scheduleIoId` depuis `ioTimeRange` au premier `rebuildRules()`, et
  `saveToParams()` réécrit le param `autoscenario_uid` depuis `def->uid` au premier enregistrement —
  que la passe de démarrage fait elle-même. Les trois valeurs sont **re-dérivées avant d'être lues**.
  Elles restent parce que la fenêtre entre le re-cléage et l'enregistrement est réelle et que
  `buildJsonIO()` publie la clef ; mais rien ne rougirait si elles disparaissaient.

- ⛔⭐⭐ **[F-TOOL-1] NEUVIÈME membre de la famille `_DEPENDENCIES` : un harnais de contre-mutation qui
  relit le fichier POUR CHAQUE ÉDITION efface ses propres éditions.** Vécu en `T3.117` : deux éditions
  du même fichier étaient calculées chacune depuis le contenu du disque, puis écrites séparément — la
  seconde écrasait la première. Une contre-mutation « déplacer un appel » (= *retirer ici* + *remettre
  là*) a donc été mesurée avec seulement *remettre là* : l'arbre appelait la passe **deux fois**, dont
  une au bon endroit, et le tour est revenu **VERT**. ⚠️ **Les preuves des huit pièges étaient TOUTES
  vraies** : `cmp` rc 0, horodatage déplacé, instantané neuf jamais réutilisé, aucun `| head`,
  `git status` de l'hôte vide, et la ligne `CXX ListeRoom.o` bien présente. Le fichier muté **est**
  celui qui a compilé ; c'est la mutation qui n'était pas celle que le descripteur annonçait.
  *À recopier : une preuve de restauration ne dit rien de ce qui a été écrit **entre** l'instantané et
  elle — et un « vert » sur une contre-mutation qu'on croit destructrice doit être soupçonné avant
  d'être publié.* **Parade** : éditions **séquentielles par fichier**, et un auto-test de harnais qui
  porte une quatrième forme — *deux éditions d'un même fichier doivent TOUTES DEUX survivre*.
  ✅ **Installé dans [`ORCHESTRATION.md`](ORCHESTRATION.md) § pièges d'outillage à la revue de merge**,
  comme **9ᵉ** membre de la famille — avec ce qui le rend le plus dangereux des neuf : les huit autres
  font **grossir** l'ensemble rouge (le cumul a l'air *meilleur*), celui-ci le fait **rétrécir jusqu'au
  vide**, et un ensemble vide se publie comme « mutant équivalent », c'est-à-dire comme un résultat.

- ⛔⭐ **[F-SCEN-4] L'identifiant neuf du re-cléage n'est tenu par rien — mesuré à la revue de merge.**
  `AutoScenarioDef::newUid()` remplacé par une clef dérivée (`<uid>_dup`), unique dans la passe mais
  garantie libre par **rien** ⇒ ⛔ **0 rouge sur 149**. La sûreté du remède repose sur le fait que
  l'allocateur est semé au-delà de tout uid du fichier — c'est vrai, c'est lu, et **aucun cas ne
  l'épingle**. Une clef dérivée qui heurterait un scénario du même `io.xml`, ou un troisième scénario
  partageant le même uid, passerait entière. *À recopier : une garantie qui tient à l'appelée et non à
  l'appel se perd au premier remaniement.*

- ⭐ **[F-SCEN-5] « Qui garde l'uid est arbitraire » vaut pour le produit, pas pour l'arbre.** Le
  parcours du modèle **inversé** — c'est le dernier scénario de l'`io.xml` qui garde l'uid, le premier
  qui est re-clé — rougit **5 cas / 2 suites**. Le choix *premier vu* est donc **tenu**, et il ne peut
  plus changer sans réécrire ces cas. ℹ️ En revanche l'**ordre** de la passe par rapport à
  `reportAutoScenariosLostByUpload()` n'est tenu par rien (déplacement ⇒ 0 rouge).

## T3.105 — la relance des sidecars (2026-09-07)

- ⛔⭐⭐ **[F-EXTPROC-10] La rampe de relance de `WagoMap` ne monte JAMAIS, et son alarme n'imprime
  jamais.** C'est la seule rampe par abonné de l'arbre (1 / 2 / 3 / 5 s, plafond 5 s, plus un
  `still failing after N attempts` à chaque multiple de **10**), et elle remet son compteur à zéro
  sur `processConnected`. Or `processConnected` est la **socket IPC**, pas l'automate :
  `setup()` de `calaos_wago` appelle `connectSocket()` en **première instruction**. Tout lancement
  qui atteint le binaire reconnecte donc et remet le compteur à zéro ⇒ **le délai vaut constamment
  1,0 s** (2, 3 et 5 sont inatteignables) et **l'alarme des 10 tentatives n'imprime jamais**. La
  rampe ne monte que quand le sidecar **ne démarre pas du tout**, c'est-à-dire là où elle sert le
  moins. ⇒ [`T3.136`](T3.136.md), laissée intacte par `T3.105` (elle est épinglée par
  `ExternProcDrivers_test`). ⚠️ Les deux délais **s'additionnent** : le régime permanent de la
  famille Wago est de **31,1 s** là où les autres sont à 30,1 s.
  *À recopier : un signal de « ça va mieux » doit prouver que le TRAVAIL a repris, pas que le
  transport est branché — sinon un compteur d'échecs se remet à zéro à chaque échec.*

- ⛔⭐⭐ **[F-EXTPROC-11] Cinq familles de sidecars sur sept ne peuvent PAS ralentir, parce qu'elles
  ne savent pas échouer.** Mesuré à la source en écrivant `T3.105` : `procMain()` de `calaos_wago`,
  `calaos_1wire`, `calaos_knx`, `calaos_ola` et `calaos_script` rend **`0` inconditionnellement** —
  c'est [`T3.108`](T3.108.md), vu ici par l'autre bout. La rampe de relance décide sur le statut ;
  un automate ou un bus perdu **en cours de service** laisse donc un statut 0, la rampe repart à
  zéro, et ces cinq familles gardent la cadence de 100 ms **malgré** le correctif. Ce qui ralentit
  vraiment aujourd'hui : `calaos_mqtt` (depuis [`T3.103`](T3.103.md)), un `setup()` qui échoue (les
  six familles C++, statut 1 par `EXTERN_PROC_CLIENT_MAIN`), un `uv_spawn` qui échoue, et une
  exception non rattrapée d'un sidecar Python.
  *À recopier : une décision prise sur une valeur ne vaut que ce que vaut la valeur — corriger le
  lecteur ne sert à rien tant que cinq écrivains sur six répondent la même chose quoi qu'il arrive.*

- ⛔⭐ **[F-TOOL-1] Le 4ᵉ piège mord AUSSI en python, et il mord DANS le `finally`.** Le harnais de
  contre-mutation de `T3.105` respectait les sept règles connues — restauration dans un `finally`,
  pas de `set -e`, instantané neuf nommé par chemin complet, calcul avant ouverture — et il est
  quand même mort avant sa restauration sous `| head -4` : le `print()` de sa propre ligne
  « RESTAURATION » a levé `BrokenPipeError` **à l'intérieur du `finally`**, laissant l'arbre muté.
  La parade « restaurer dans un `finally` » ne suffit donc pas : **le journal doit être incapable de
  tuer le harnais**. Écrire dans un fichier d'abord, et n'écrire sur le terminal que dans un
  `try/except`. Mesuré des deux côtés : avant, `cmp` sans rc 0 et fichier muté ; après, `cmp` rc 0,
  horodatage déplacé, arbre propre.
  *À recopier : ce qui tue un harnais entre la mesure et la remise en état n'est pas toujours la
  commande mesurée — ici c'était sa propre trace.*

- ⛔⭐⭐ **[F-EXTPROC-12] Une mort par SIGNAL laisse un statut 0, donc la rampe de relance ne la voit
  pas.** Mesuré à la **revue de merge** de [`T3.105`](T3.105.md). Sous Linux, un enfant tué par
  `SIGSEGV`, `SIGABRT` ou l'OOM-killer rend `exit_status = 0` et `term_signal = <n>` ;
  `uvw::ExitEvent` porte **les deux** et `nextFailureCount()` ne lit que le premier, où un zéro vaut
  « arrêt volontaire ». ⇒ **un sidecar qui plante en boucle garde la cadence d'avant**, pour **les
  six familles** et donc y compris `calaos_mqtt`, la seule que `T3.105` ralentit vraiment. Même
  boucle, même fenêtre de 5 s : `kill -9` en boucle ⇒ `master` **588 lancements/min**, arbre livré
  **576/min** ; le témoin positif du même tour (enfant qui sort **3**) ⇒ **72/min**. Fermé par une
  condition — compter un échec quand `signal` n'est ni 0 ni `SIGTERM`, celui de `terminate()` ⇒
  [`T3.138`](T3.138.md).
  *À recopier : « le processus a rendu 0 » et « le processus s'est arrêté normalement » ne sont pas
  la même phrase, et l'écart entre les deux est exactement l'ensemble des plantages.*

- ⛔⭐ **[F-EXTPROC-13] Une règle qui lit une durée n'est tenue que si l'ORIGINE de cette durée l'est
  aussi.** Contre-mutation neuve à la revue de `T3.105` : `spawned_at` posé en tête de
  `startProcess()` au lieu de juste avant le `spawn` — un déplacement d'une ligne — fait entrer
  **l'attente** dans la durée de service. Au plafond, un enfant qui meurt aussitôt serait crédité de
  ≈ 30 s de service, la remise à zéro mordrait à chaque tour et la rampe **cesserait de tenir**.
  Mesuré : ⛔ **0 cas rouge**, `TOTAL 149 / PASS 148` (campagne jouée avant le rebase sur `T3.117`). Les six cas de politique lisent des fonctions
  pures et ne voient pas le site d'appel ; les trois cas à enfant travaillent dans une fenêtre de
  1600 ms où les attentes valent 0,1 à 0,4 s, très loin du seuil de 30 s. ⇒ [`T3.138`](T3.138.md).
  *À recopier : la remise à zéro était épinglée comme FONCTION et jamais comme COMPOSITION — deux
  bornes justes sur une soustraction ne disent rien de ce qu'on soustrait.*

- ⚠️ **[mesure] Une borne basse sur un COMPTE dans une fenêtre fixe dépend de l'horloge murale, même
  quand elle sert de contrepoids.** La fiche de `T3.105` annonçait « trois comptes bornés par le
  haut, deux bornes basses qui sont des durées ». Recompté à la revue : **une seule** borne haute sur
  un compte (`launched ≤ 4` en 1600 ms), **une seule** borne basse qui soit une durée
  (`ran ≥ 0,25 s`), et **trois** attentes de vivacité qui sont bien des comptes dans une fenêtre
  fixe. Elles sont légitimes — sans elles, un transport qui cesserait de relancer passerait toutes
  les bornes hautes — mais elles se **mesurent** : 20 exécutions consécutives sous une charge moyenne
  de **244 sur 64 cœurs** ⇒ **9/9 vertes à chaque fois**, marges nominales de 7 à 12 fois.
  *À recopier : « aucun cas ne mesure une durée » se recompte assertion par assertion ; un
  contrepoids reste une borne, et une borne basse sur un compte est une borne de temps déguisée.*


## Premier `push` — ce que la CI a appris (2026-09-07)

- ✅ **Le run est vert** : `34125920118`, `TOTAL 150 / PASS 149 / SKIP 1 / FAIL 0 / ERROR 0`, seul
  `SKIP` `check-ccache-honesty.sh`. Six contrôles, dont ⭐ **`Workflow gating check` PASS** — première
  exécution réelle de la garde de publication posée par `T3.125`. `check-pydeps-conformance.sh`
  **PASS** sous `CALAOS_PYDEPS_STRICT: 1`, et `run-python-tests.sh` **PASS** et non `SKIP`.

- ⛔ **F-CI-1 — [OUTILLAGE, OUVERT, mesuré au premier `push`] la CI bâtit un sous-ensemble de
  l'arbre et n'en dit rien.**

  Référence locale `TOTAL 151`, CI `TOTAL 150`. Le manquant est `KNXExternProcWire_test`, sous
  `if HAVE_LIBKNX` : sans `eibclient.h`, la règle n'est pas engendrée. **Ce n'est pas un `SKIP`,
  c'est une absence** — aucune ligne, aucun avertissement, un total inférieur d'une unité que rien
  ne compare à rien.

  Résumé de `configure` lu dans le journal du run : Eib/KNX **no**, MQTT **no**, RoonAPI **no**,
  Reolink **no**. ⇒ **`calaos_knx` et `calaos_mqtt` ne sont jamais compilés**, alors que `T3.103`,
  `T3.105`, `T3.138`, `T3.84` et `T3.108` portent sur ce code.

  ⭐ C'est la forme CI de *« un capteur qui ne mesure rien passe pour un capteur qui ne trouve
  rien »*. ⇒ [T3.150](T3.150.md).

## T3.137 — `set_param` accepte n'importe quelle clef, y compris celles que le serveur dérive

- ✅⭐⭐ **[F-SCEN-2, FERMÉ] Le remède à une liste de noms n'est pas une liste blanche : c'est de
  demander l'ensemble à ce qui le PRODUIT.** `set_param` / `del_param` acceptaient toute clef sur
  tout IO, `autoscenario_uid` compris. Le recensement des clients — `calaos_installer`,
  `calaos_mobile`, `calaos_remote_ui`, `calaos_windex`, scripts Lua — trouve **zéro** appel légitime
  à `set_param`, et surtout : **aucune source de l'arbre ne peut énumérer ce qui est écrivable**
  (l'éditeur de propriétés de l'installateur crée n'importe quelle clef, un script Lua choisit la
  sienne, chaque pilote a les siennes). Une liste blanche aurait donc été **devinée**. L'**ensemble de
  définition**, lui, a exactement un producteur — `AutoScenarioDef::isDefinitionParam()`, la même
  fonction qui décide ce que `saveToParams()` a le droit de supprimer et de re-dériver, si bien que
  le prédicat de la garde est **partagé** avec l'écrivain et tenu par construction.

  ⚠️ **« Producteur unique » vaut pour l'ensemble de DÉFINITION, et pas au-delà.** L'arbre porte des
  paramètres qui sont **dérivés ET écrivables par l'API** : `auth_token` et `device_secret`
  (`RemoteUI/RemoteUIProvisioningHandler.cpp:199`, `generateDeviceSecret()`), `cycle`, `disabled` et
  `disabled_missing_io` (`Scenario/AutoScenario.cpp:180`, `:191`, `:279`). Généraliser la phrase à
  « ce que le serveur dérive a un producteur unique » va **plus loin que le mesuré** — le refus de
  `T3.137` ne couvre qu'un ensemble, celui qu'une classe sait énumérer.

  ⚠️ **Et la garde est un PRÉFIXE** : `autoscenario_<n'importe quoi>` est refusé sur **n'importe
  quel** IO, pas seulement sur un `Scenario` ni seulement pour les six clefs nommées. **0 collision
  mesurée dans tout `src/`**, donc inoffensif — mais l'énumération de six noms ne décrit pas ce que
  le code fait.

  ⚠️ **La contrepartie, que la doc annonce désormais avec le refus** : `config put` atteint les mêmes
  clefs **sous la même authentification**. La garde ne ferme pas une porte ; elle supprime le chemin
  **muet** — `config put` réécrit un fichier, produit un instantané et demande un redémarrage, là où
  `set_param` écrivait sans trace.
  ⭐ **Confirmation indépendante** : `calaos_installer` implémente **déjà** le même refus de son côté
  (`DialogListProperties.cpp`, préfixes `autoscenario_` et `as_`).
  *À recopier : quand on ne peut pas énumérer ce qu'on publie, on n'a pas le droit de deviner ce
  qu'on cache — mais on a souvent le droit de demander à celui qui dérive.*

- ⛔ **[F-SCEN-3] Un IO `Scenario` SANS définition ne voit aucun de ses paramètres re-dérivé, et un
  seul attribut le promeut en autoscénario.** `AutoScenarioDef::saveToParams()` rend la main quand
  l'uid est vide, donc ce qu'on écrit sur un scénario écrit à la main part **tel quel** dans
  `io.xml` ; au démarrage suivant `loadFromParams()` répond `true` et le générateur **possède** ses
  règles. La garde d'API de `T3.137` ferme le chemin client ; **le `config put` et le fichier édité
  restent ouverts**, et la réparation de `T3.117` ne répare que le **doublon** — un uid **frais** ne
  déclenche rien. ⇒ [`T3.145`](T3.145.md).
  *À recopier : « le serveur re-dérive de toute façon » est vrai d'un objet DÉFINI et faux du même
  objet vide — et c'est l'objet vide qui traverse le disque.*

- ⛔ **[F-LUA-1] Le troisième transport de `set_param` n'est mesuré par aucun cas.** WS, HTTP et Lua
  partagent `buildJsonSetParam()`, mais `tests/LuaCalaosApi_test.cpp` observe `setIOParam` sur un
  espion : il mesure la trame émise, jamais ce que le serveur en fait, et la ligne d'appel de
  `ScriptExec` n'est couverte par rien. Sur les trois transports de `T3.137`, **deux** sont mesurés
  et le troisième est **déduit d'une lecture du code**. ⇒ [`T3.146`](T3.146.md).
  *À recopier : « les trois transports passent par la même fonction » est une lecture, pas une
  mesure — et c'est exactement la phrase qui dispense d'écrire le cas.*

- ⚠️ **[mesure] `get_io` publie une PROJECTION FIXE, et une seule clef de définition en fait
  partie.** `JsonApi::ioProjectionParams()` énumère 16 paramètres ; `autoscenario_uid` y est, les
  cinq autres clefs `autoscenario_*` et tout l'espace `as_*` n'y sont pas. Deux conséquences pour
  qui écrit une suite : un cas qui lit une clef de définition dans `get_io` mesure **l'absence**,
  pas la valeur ; et la définition ne descend dans les `Params` de l'IO qu'à la **sauvegarde**
  (`Scenario::SaveToXml`), donc avant elle il n'y a rien à lire non plus.
  *À recopier : un capteur qui ne mesure rien passe pour un capteur qui ne trouve rien — ici deux
  fois, et pour deux raisons différentes.*

- ✅⭐⭐ **[F-EXTPROC-12 / F-EXTPROC-13] FERMÉS par [`T3.138`](T3.138.md)** — la rampe voit un
  plantage, et l'origine de l'uptime est tenue. `nextFailureCount()` prend le signal en quatrième
  paramètre ; un échec est compté dès qu'il n'est ni 0 ni `SIGTERM`. `CR-1` rejouée passe de
  **0 rouge** à **1** : le cas neuf conduit la vraie boucle jusqu'à une attente que la rampe
  **calcule elle-même** et exige que la durée de service reste **sous** elle.
  ⭐ **L'instrument est un COMPTE, pas un débit** : forme `master` **16 lancements** dans une fenêtre
  de 1600 ms, arbre livré **4** — borne par le haut, donc la charge ne peut que la rendre plus
  facile à tenir, jamais plus difficile.
  *À recopier : quand la propriété mesurée est une CADENCE, publier des « lancements par minute »
  c'est publier une lecture d'horloge murale ; le même fait se dit en comptant les événements d'une
  fenêtre fixe, et ce chiffre-là survit à une machine partagée.*

- ⛔ **[F-EXTPROC-14] Deux chemins vers la même fonction ne sont pas la même mesure.**
  `ExternProc.cpp` appelle `noteChildGone()` depuis l'`ExitEvent` **et** depuis l'`ErrorEvent` d'un
  `spawn()` qui n'a jamais abouti (binaire absent). Le second est retiré ⇒ ⛔ **0 cas rouge**
  (`TOTAL 152`, `106 CXXLD` lus), alors que la production le commente comme « the cheapest way there
  is to loop forever » et que les notes de version l'annoncent nommément.
  ⇒ [`T3.143`](T3.143.md). *À recopier : un cas qui prend le premier chemin ne dit rien du second,
  et le harnais qui sert au premier peut être inutilisable sur le second — ici aucun enfant ne
  démarre, donc aucun journal n'est écrit.*

- ⛔ **[F-EXTPROC-15] Un compteur remis à zéro par un chemin ASYNCHRONE ne l'est pas pour l'appelant
  SYNCHRONE qui suit.** `terminate()` ne touche pas `respawn_failures` ; la remise à zéro arrive par
  l'`ExitEvent` du `SIGTERM`, après le `startProcess()` qu'un contrôleur enchaîne. Mesuré :
  `respawn_failures = 0;` ajouté dans `terminate()` ⇒ ⛔ **0 cas rouge** — **ni le comportement
  actuel ni son contraire** n'est tenu, et l'écart vaut jusqu'à une demi-minute d'attente en
  production. ⇒ [`T3.144`](T3.144.md).

- ⛔⭐⭐ **[F-EXTPROC-16] L'exemption `SIGTERM` porte sur la VALEUR du signal, jamais sur son
  origine** (mesuré par la revue de merge de [`T3.138`](T3.138.md), lot 147).
  `crashed = termSignal != 0 && termSignal != SIGTERM` (`ExternProc.cpp:285`), et la ligne d'alerte
  `was killed by signal N` (`:359`) est gardée par la même condition. Un `killall -TERM`, un
  superviseur ou systemd qui tue un sidecar est donc exempté **de la rampe ET du journal** : le
  serveur lit comme volontaire un arrêt que personne, chez lui, n'a demandé — un tiers qui tue en
  boucle retrouve exactement le comportement d'avant [`T3.105`](T3.105.md).
  **Sonde `CM-PROV`** (exemption conditionnée à la génération) ⇒ ⛔ **0 cas rouge** : aucun cas
  n'observe la provenance. ⚠️ **Rien ne le nommait** — ni le code (son commentaire dit « SIGTERM is
  what terminate() sends », ce qui est vrai et ne dit rien de la réciproque), ni la fiche, ni les
  notes de version. ⭐ **Fermable** : `terminate()` incrémente déjà `respawn_generation`
  (`:147`) avant d'envoyer son signal. ⇒ [`T3.153`](T3.153.md), qui recoupe l'arbitrage de
  [`T3.144`](T3.144.md).
  *À recopier : exempter une VALEUR, c'est exempter tout le monde qui sait l'écrire.*

- ⛔ **[F-EXTPROC-17] Trois propriétés à 0 rouge ne vivaient que dans la prose d'une fiche**
  (rejouées et confirmées par la revue de merge de [`T3.138`](T3.138.md), lot 147) : la **garde de
  génération** (`CR-2`), le texte **`holding the relaunch`** (`CR-3`), et la ligne neuve
  **`was killed by signal N`**. Les trois mutations ont été réécrites indépendamment et relues
  **hors de l'arbre** avant publication ⇒ ni re-dérivation, ni artefact de harnais (9ᵉ piège) : ce
  sont de vrais trous. ⭐ **Le pire est le troisième** — `RELEASE_NOTES.md` cite
  `mqtt was killed by signal 11` **mot pour mot**, dans un bloc de code, alors qu'aucun cas ne tient
  cette ligne. ⇒ [`T3.154`](T3.154.md).
  *À recopier : une chaîne citée verbatim dans les notes de version est une promesse ; si aucun cas
  ne la tient, la documentation devient le seul oracle — et elle ne rougit jamais.*

  ⚠️ **Réserve non bloquante, du même tour** : `EXPECT_GE(launched, 2)` et `EXPECT_GE(failures, 2)`
  (`core/ExternProcCrashBackoff_test.cpp:290`, `:293`) sont des bornes **basses** sur des comptes,
  donc sensibles à la charge **dans le mauvais sens** — faux **rouge** possible, faux vert non.
  Marge mesurée **× 5**.


## Bug rapporté par l'utilisateur — la lecture d'état DALI (2026-09-07)

- ⛔ **F-DALI-1 — [CORRECTION, OUVERT, lu et non exécuté] `WAGO_DALI_GET` perd le drapeau de groupe
  que `WAGO_DALI_SET` transmet.**

  L'écriture envoie `WAGO_DALI_SET <line> <group> <address> <val> <fade>` ; la lecture envoie
  `WAGO_DALI_GET <line> <address>`. L'automate attend le drapeau en **3ᵉ** position et
  `GET_PARAM_DINT` rend **0** pour un paramètre absent, franchement. La réponse est donc bâtie sur
  `DaliSendValue647[]` (ballasts, `1..64`) au lieu de `DaliSendValueGrp647[]` (groupes, `1..16`).

  ⭐ **L'ordre des paramètres diffère entre les deux commandes** — drapeau avant l'adresse à
  l'écriture, après à la lecture — et le programme automate porte le même commentaire hésitant
  `(* Short addr or group? *)` aux deux endroits.

  ⚠️ **Ce que la forme du symptôme apprend** : le serveur ne lit jamais un niveau
  (`tokens[1] == "0"` sinon **100**), donc l'état est **binaire** et tout non-zéro devient « allumé ».
  Et le chemin de timeout laisse le défaut `0 = éteint` — ⭐ **un automate muet donnerait « éteinte » :
  c'est ce qui exclut la panne de communication et impose une réponse reçue mais fausse.**
  ⇒ [T3.156](T3.156.md).

- ⛔ **F-DALI-2 — [FIABILITÉ, OUVERT] l'état d'une sortie DALI est lu UNE FOIS, à la construction, et
  jamais corrigé.**

  `WAGO_DALI_GET` n'est émis que depuis quatre constructeurs (`WODali.cpp:61`,
  `WODaliRVB.cpp:72/74/76`). Le battement périodique n'envoie que `WAGO_SET_SERVER_IP` et
  `WAGO_HEARTBEAT` ; les trames `WAGO INT` entrantes n'atteignent que les **entrées**. Une lecture
  fausse au démarrage le reste jusqu'à une action manuelle — ce qui transforme un défaut ponctuel en
  défaut permanent, et explique le « tout le temps au reboot » du rapport utilisateur.
  ⇒ [T3.156](T3.156.md) §4.

- ⛔ **F-DALI-3 — [CORRECTION, OUVERT] le champ `address` porte trois sémantiques que la lecture ne
  distingue pas.**

  Adresse courte DALI, numéro de groupe DALI, adresse DMX au-delà de 100 — une seule valeur, trois
  sens, et aucun moyen de savoir lequel au retour. Conséquences mesurées à la lecture : la relecture
  DMX lit son adresse dans le paramètre jamais émis (`p3 - 100` ⇒ sous-débordement, **tout DMX** est
  touché), et la frontière DALI/DMX diffère entre les deux gestionnaires (`p3 > 99` à l'écriture,
  `p2 < 99` à la lecture) ⇒ l'adresse 99 serait écrite en DALI et relue en DMX.

  Recoupe la zone d'ombre déjà consignée plus haut : « `WODali` déclare ses adresses sur `1..612` là
  où une adresse courte DALI va de 0 à 63 ». ⇒ [T3.157](T3.157.md).


## Bug rapporté par l'utilisateur — la bascule mode dégradé de l'automate (2026-09-07)

- ⛔ **F-WAGO-1 — [RÉGRESSION, OUVERT, lu et non exécuté] le miroir serveur→dégradé est faux depuis
  la 1.8 : il n'écrit pas ce qu'il croit écrire, et les sorties non couvertes gardent 0.**

  Dans `PLC_PRG`, `j := j + 2` s'exécute **avant** les deux lignes de recopie : `OutArrState[0..1]`
  n'est jamais synchronisé et chaque tour recopie le mot d'après celui qu'il vient d'écrire. Et la
  recopie indexe `OutArrState` en **relatif** là où `SetOutput()` et la branche dégradée l'indexent
  en **absolu** (`start_addr_out / 8 + …`) : dès qu'un module analogique de sortie précède le premier
  digital, tout le rack est décalé.

  ⭐ **La 1.7 était correcte** : `FOR i := start_addr_out/8 TO 512 DO byOutArr[i] := OutArrState[i]`
  et l'exact inverse — pleine largeur, absolu, **bidirectionnel**. La 1.8, en déplaçant l'image
  serveur de `%QB0` vers `%IB512`/`netOutStandard`, a cassé l'aller et **supprimé le retour**. Le
  bloc est identique octet pour octet de 1.8 à 3.0. ⇒ [T3.160](T3.160.md).

- ⛔ **F-WAGO-2 — [FIABILITÉ, OUVERT] le danger de la bascule est un NIVEAU LATCHÉ rejoué, pas un
  front — et un seul type de sortie y est exposé.**

  ⭐ **L'hypothèse du front parasite est réfutée** : `event[cpt](IN := GetInput(…))` vit dans
  `SendInput`, appelé à chaque cycle **dans les deux modes**, donc aucun `R_TRIG` ne gèle. Le seul
  type dangereux est le **`VOLET` simple** : son `SetOutput(… MONTE/DESCENTE)` est **à l'intérieur**
  du `IF (event[cpt].ON)`, donc les bits relais restent latchés entre deux appuis et rien ne les fera
  retomber avant un appui. `VOLET_IMPULSE` est sûr (son `SetOutput` est hors du garde) ; le
  télérupteur aussi (il n'agit que sur `event.ON`).

  ⭐ Un volet occupe **deux bits relais, à 0 au repos** : recopier une image au repos est inoffensif.
  ⇒ [T3.160](T3.160.md) §4.

- ⛔ **F-WAGO-3 — [FIABILITÉ, OUVERT] personne ne réaffirme l'image de sortie au retour du serveur.**

  `netOutStandard` n'est écrite que par Modbus : figée pendant la panne, elle est réécrite telle
  quelle au retour, **ré-excitant tout relais de volet actif au moment du crash**. Côté serveur,
  `WOVoletBase::voletInit()` ne lit ni n'écrit rien, `WODigital` **adopte** l'état du PLC au lieu de
  le réaffirmer, et `WAGO_SET_OUTPUT` n'est utilisé que par `src/bin/tools/wago_test.cpp`.

  ⚠️ **Une seule des deux bascules suffit à faire bouger un volet** : ce défaut est indépendant de
  `F-WAGO-1` et se corrige séparément. ⇒ [T3.161](T3.161.md).
