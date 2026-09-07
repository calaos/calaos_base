# Décisions — journal durable

> Décisions prises par l'utilisateur (Raoul) pendant le refactoring. But : qu'un Claude neuf
> **ne les re-demande pas** et respecte les contraintes. Format : date, décision, pourquoi,
> comment l'appliquer. Ajouter en tête (plus récent en haut).

## 2026-08-25 — OTA RemoteUI : le **downgrade est voulu**, `calaos_server` pousse ce qu'on lui donne

**Décision** : `OtaFirmwareManager::checkDeviceForUpdate()` **garde son égalité de chaînes**
(`OtaFirmwareManager.cpp:249-253`). Toute version **différente** de celle de l'appareil — **plus
ancienne comprise** — est proposée, et l'appareil **l'installe seul, sans confirmation**.
⛔ **Ce n'est pas un défaut, c'est le contrat.**

**Mot de l'utilisateur, textuel** :

> « l'appareil installe seul, sans confirmation utilisateur, on peut vouloir un downgrade c'est
> accepté. Le process de downgrade (et upgrade) est à la charge de l'utilisateur et
> `calaos_server` fait juste son travail pour pousser la version qu'il a aux devices
> `remote_ui` »

**Pourquoi** : `calaos_server` est un **distributeur, pas un arbitre**. Ce que l'administrateur
dépose dans `/usr/share/calaos/firmwares/<hardware_id>/` **est** la version voulue pour ce parc ;
la décision de monter ou de descendre est prise **en amont**, au moment du dépôt. Le fichier
déposé **est** l'opt-in.

**Ce que cela écarte, nommément** (options pesées par [`T3.26`](T3.26.md) §4, première rédaction) :
- **A — n'offrir que strictement supérieur** : ⛔ **casse l'usage**. Redescendre un parc devient
  impossible, et c'est un geste légitime — le commentaire de `:247-248` le disait déjà
  (« *This allows switching between dev and release branches* »).
- **B — downgrade opt-in par une clé du manifeste** : ⛔ **négociation de format avec le firmware**
  (autre dépôt) pour un besoin **qui n'existe pas**.
- **C — journaliser** : ✅ retenue, mais **requalifiée en confort de diagnostic**, sous sa forme
  minimale (nommer les deux versions, dire « change » et non « update ») — **sans** comparateur
  sémantique, qui réintroduirait par la porte du journal la machinerie écartée par la porte du
  comportement.

**Comment l'appliquer** : ⚠️ **un ticket futur qui « corrigerait » `:249-253` en comparaison
relationnelle RÉGRESSE le produit.** [`T3.26`](T3.26.md) a été **requalifiée** en conséquence
(titre, constat, priorité ⬇️, suite de tests annulée) : c'est exactement la faute qu'elle allait
commettre. Cette entrée existe pour l'empêcher une seconde fois.

## 2026-08-25 — ⭐ **DEUX canaux de publication** : `master` = préversion, *release* = tout le monde. Ne pas protéger `master`

**Décision** : ⛔ **la recommandation de protéger la branche `master` sur GitHub est RETIRÉE.**
Elle était fondée sur une **méconnaissance du modèle de publication**, pas sur un risque réel.

**Mot de l'utilisateur, textuel** :

> « Pourquoi proteger master? c'est par la qu'on build+push un nouveau .deb/docker. On a 2 canaux:
> push sur master fera des prerelease que seul moi ou les gens qui dev utilisent, et si je fait
> une release quand le code est clean, ca pousse pour tout le monde. »

**Le modèle, à retenir avant toute analyse de CI** :

| Canal | Déclencheur | Public |
|---|---|---|
| **préversion** | **tout push vers `master`** → `docker-publish-dev.yml` : bump de version, tag git, `ghcr.io/calaos/calaos_base:dev` + tag versionné, `repository_dispatch build_deb` vers `calaos/pkgdebs` | **les développeurs**, et l'utilisateur lui-même |
| **release** | une **release** faite délibérément, quand le code est propre | **tous les utilisateurs** |

⇒ **`master` n'est pas la branche de production : c'est le canal de préversion.** La protéger
**empêcherait son usage prévu** — on ne pourrait plus produire de préversion sans cérémonie.
L'absence de *required status check* n'est donc **pas un trou**, c'est la conséquence directe du
rôle de la branche.

**Requalification de l'épisode du sidecar MCP mort** (F-DEP-1 / [`T3.23`](T3.23.md)) : l'image
`ghcr.io/calaos/calaos_base:dev` publiée avec `mcp 2.0.0` **n'a pas atteint les utilisateurs**.
C'était **une préversion cassée sur le canal fait pour ça**. ⇒ **la gravité tombe** ; le correctif
T3.23 reste **entièrement juste** (le manifeste doit être la source unique, la sonde doit sonder
l'API réellement importée), seule sa **justification** change : on corrige parce qu'une préversion
cassée fait perdre du temps aux développeurs, **pas** parce qu'un artefact cassé aurait été livré.

**Comment l'appliquer** : la phrase « **la fenêtre qui a laissé publier une image cassée reste
ouverte** » (ORCHESTRATION, T3.23) est **périmée** — il n'y a pas de fenêtre, il y a un canal.
Le geste utile n'est pas un verrou de branche mais **la porte de sortie qui existe déjà** :
`DECISIONS.md`, « ⚠️ Pousser master publie des artefacts », qui impose de **demander avant tout
push**. Elle reste, et elle suffit.
⚠️ **Plusieurs agents ont ignoré ce modèle et en ont tiré une mauvaise analyse.** C'est une
information **d'architecture**, à lire avant de juger un workflow.

## 2026-08-25 — `X-Forwarded-For` : la confiance se conditionne au **pair TCP**, pas à `listen_address`

**Décision** : la mitigation de **F-XFF-1** (exposition du throttle de login créée par
[`T3.24`](T3.24.md)) est **la confiance conditionnée au pair TCP loopback**, ticket
[`T3.39`](T3.39.md). ⛔ **`listen_address = 127.0.0.1` est ÉCARTÉ**, dans le code **comme** dans la
configuration de calaos-os.

**Pourquoi `listen_address` est mort — vérifié au source, et c'est net** : **la même clé gouverne
DEUX serveurs**.

| Site | Ce qu'il bind |
|---|---|
| `HttpServer.cpp:29-31` | l'API HTTP/WebSocket, port `port_api` (5454) |
| **`UDPServer.cpp:58-61`** | **le serveur de découverte UDP**, *même ligne, même repli `"0.0.0.0"`* |

`UDPServer::processRequest()` répond à `CALAOS_DISCOVER` par `CALAOS_IP <ip>`, où l'adresse est
`TCPSocket::GetLocalIPFor(remoteIp)` — **l'adresse LAN joignable depuis le client**. Il reçoit
**aussi** les trames `WAGO INT` / `WAGO KNX` des automates (`UDPServer.cpp:88-118`), qui sont un
**chemin d'IO vivant**.

**Et la question « qui se connecte directement au 5454 ? » a une réponse : les clients
légitimes.** Vérifié dans les dépôts voisins :

| Client | Site | Chemin |
|---|---|---|
| **RemoteUI** (firmware ESP32) | `calaos_remote_ui/main/calaos_protocol.h:28` (`WS_PORT = 5454`), `provisioning_requester.cpp:161`, `.h:92` | découverte **UDP**, puis `http://<ip>:5454/api/v3/provision/request` et WS sur 5454, **en clair, sans proxy** |
| **application mobile en LAN** | `calaos_mobile/src/CalaosConnection.cpp:307-308` | hôte nu ⇒ `ws://<h>:5454/api` + `http://<h>:5454/api.php` (le mode `https` passe, lui, par haproxy) |
| **calaos_installer** | `dialogautodetect.cpp:74` | `CALAOS_DISCOVER` en **broadcast UDP** (le transfert de config, lui, passe par `https://<ip>/api.php`) |
| sidecar MCP | `calaos_mcp/config.py:41` | `ws://127.0.0.1:5454/api` — **loopback**, seul client indifférent |

⇒ poser `127.0.0.1` **casserait la découverte ET la connexion de tout le parc RemoteUI**, les apps
mobiles en LAN, et les entrées Wago. **Le port 5454 ouvert n'est pas un défaut : c'est le produit.**

**Ce qu'on fait à la place** : `X-Forwarded-For` n'est lu que **si le pair TCP est le loopback**
(`127.0.0.1` / `::1`), c'est-à-dire haproxy ; sinon on prend l'adresse du pair et **l'en-tête est
ignoré**. C'est la « liste de proxys de confiance » dont T3.24 avait constaté l'absence dans tout
l'arbre — réduite à **une entrée**. Elle ferme **entièrement** l'exposition (plus d'exonération du
throttle, plus de victime accablée) **sans rien casser** : RemoteUI et les apps LAN restent
identifiés par leur **vraie** adresse, ce qui est exactement ce qu'un throttle veut.

**Ce qui reste ouvert, et c'est accepté** : un attaquant **sur la machine elle-même** garde la
capacité de forger l'en-tête, puisqu'il est loopback. À écrire, pas à corriger.

**Comment l'appliquer** : ⚠️ **ne reproposez pas `listen_address`.** Il est documenté
(`docs/16_config_options.md`), il a l'air gratuit, et il ne l'est pas — c'est exactement pourquoi
cette entrée existe. La question a été instruite deux fois ; la deuxième a trouvé `UDPServer.cpp`.

## 2026-08-25 — `T3.36` (relink des suites de tests) : **priorité haute, mais APRÈS `E4.1x`**

**Décision** : [`T3.36`](T3.36.md) — les ~~**47 suites sur 80**~~ **49 suites sur 83 binaires de
test** (recompté en `python3` sur `fb9d064c` par T3.27, cf. `FINDINGS.md` **F-LINK-1** ; = 49 des
**84** `check_PROGRAMS`, sur **86** entrées `TESTS` dont 3 scripts shell — ⚠️ **le ratio monte à
chaque suite ajoutée, toujours dire sur quel arbre on l'a mesuré**) qui portent
`<suite>_DEPENDENCIES = libcalaos_common.la` et ne relient donc pas les `.o` serveur qu'elles
testent — est **prioritaire**, et **planifiée après la clôture d'E4.1** (`E4.1x`). ⛔ **Ne pas la
lancer avant.**

**Le constat** : plus d'une suite sur deux **peut répondre vert sans avoir relié le code
modifié**. Reproduit **deux fois dans la même journée, dans les deux sens** (faux vert *et* faux
rouge). C'est la **cause racine des cinq variantes de faux vert/rouge** rencontrées depuis deux
jours.

⛔ **CORRECTION (2026-08-25, T3.27) — il y a une SIXIÈME variante, et elle n'a PAS cette cause
racine.** ~~« la cause racine des CINQ variantes »~~ reste exacte **pour ces cinq-là** ; la
sixième, rencontrée pendant la campagne de contre-mutation de T3.27, est **un défaut de harnais** :
la mutation **n'a pas été appliquée** (motif de remplacement faux d'un fragment, `assert` du
`python3` en échec, harnais sans `set -e`) ⇒ **le cas a tourné NON MUTÉ**, donc vert. Elle **survit
à tous les garde-fous de la famille `_DEPENDENCIES`** : `rm -f` fait, `CXXLD` présente, `.o`
recompilé, code de sortie cohérent. ⭐ **Falsifiée en revue** : en mode laxiste, `LAX_M2` donne
`CXX LuaScript/ScriptBindings.o` = 1, `CXXLD LuaCalaosApi_test` = 1, **0 `FAILED`, sortie 0** —
**un faux vert parfait sur un mutant qui n'a jamais existé.** ⇒ **Remède distinct, à porter dans
tout brief de contre-mutation** : **`cmp` d'application** (refuser de scorer une mutation
identique à l'original) **et comparaison des ENSEMBLES de rouges au témoin**, jamais de leurs
cardinaux — dans l'épisode réel, seuls les ensembles l'ont vu (`M0` et `M2` **identiques**, deux
comptes à 0 n'auraient rien dit). Détail et reproduction : `FINDINGS.md` **F-HARN-1**.
⇒ **Formulation juste : cinq variantes de la famille `_DEPENDENCIES`, plus une sixième d'une autre
famille.** T3.36 ferme les cinq, **pas la sixième**.
⚠️ **Piège de comptage à consigner avec** : `CORE_TEST_LDADD` **contient** `CORE_SERVER_OBJECTS`,
donc chercher `$(CALAOS_SERVER_BUILDDIR)` en toutes lettres donne **27** au lieu de **47**.

**Pourquoi après** : la réparation touche **`tests/Makefile.am`**, et **huit tickets sérialisés
(`E4.1l` → `E4.1s`) y appendent chacun leur bloc**. La faire maintenant produirait **un conflit à
chaque merge de la chaîne**, sur le fichier dont la résolution naïve est déjà connue pour **perdre
le `endif` extérieur** et casser `automake` (cf. ORCHESTRATION, « Pattern récurrent : conflit
`tests/Makefile.am` »).

**La contrepartie, assumée** : d'ici là **la protection repose sur la discipline**. Chaque brief
d'agent doit porter le contournement — `rm -f` du **binaire de test** *et* des **`.o` serveur
touchés**, puis **exiger la ligne `CXXLD` et le code de sortie du binaire**, jamais compter les
`FAILED`. Ce n'est **pas une garantie**, c'est une consigne qu'on peut oublier. Le risque est
accepté **pour la durée de la chaîne**, pas au-delà.

**Risque connu de la réparation elle-même, à écrire dans le ticket** : en rétablissant le relink,
**on découvrira peut-être des suites qui ne passaient que grâce à son absence**. C'est un argument
**pour** la faire, pas contre — mais son premier `make check` **peut rougir**, et **ces rouges
seront des trouvailles**, pas des régressions.

## 2026-08-25 — T3.25 : `from_string` corrigée **globalement**, puis audit des défauts — et la frontière d'API **dans le même ticket**

**Décision utilisateur**, prise contre la recommandation de la fiche (qui proposait « étage 1 + 3,
pas 2 ») : **les deux, dans cet ordre**.

1. **`Utils::from_string` écrit une valeur définie (`T{}`) quand elle ne peut rien lire, et rend
   `false` sur une chaîne vide ou blanche.** Les **312 appelants qui ignorent le code de retour**
   (sur 319) deviennent sûrs **immédiatement**, sans qu'on ait à toucher une seule de leurs lignes.
2. **Puis auditer les destinataires pré-initialisés**, parce que la correction du point 1
   **écraserait leur défaut par 0, en silence**. C'est **la régression à ne pas introduire**, et
   c'est exactement pour ça que cette voie a été choisie plutôt que la correction locale : elle
   rend le danger visible et bornée à une population qu'on peut nommer et vérifier.
3. **La validation à la frontière d'API est DANS CE TICKET**, pas dans un suivant. Ceinture **et**
   bretelles : `from_string` protège les appelants d'aujourd'hui, la frontière protège **même si un
   appelant futur réintroduit le motif**.

**Pourquoi cet ordre et pas l'inverse** : initialiser 236 destinataires un par un (l'« étage 3 »
de la fiche) est sûr mais lent, et pendant tout ce temps chaque site non encore traité reste
exploitable. Corriger la fonction ferme tout d'un coup ; l'audit ne sert plus qu'à ne pas casser
les rares sites qui vivaient du contrat implicite « je ne t'écris pas si je ne sais pas lire ».

**Ce qui a été livré sur cette base** (branche `fix/t3.25`) :
- `from_string` **conserve les lectures partielles** (`"12abc"` → 12) : n'écrire `T{}` qu'en cas
  d'échec **total** est ce qui rend la correction sans dommage collatéral. Une correction naïve
  `dest = ok ? tmp : T{}` aurait transformé `"12abc"` et `"12 "` en 0.
- `is_of_type` corrigée **dans le même commit**, et ce n'est pas un bonus : dix sites portant un
  défaut utile (`step = 1.0`, `interval = 15000`, `LOG_LEVEL_INFO`) ne sont protégés **que** par
  elle. Démontré par contre-mutation : la remettre en `iss.eof()` seul rend rouges deux suites
  qui n'ont rien à voir avec l'analyse syntaxique (`StaticLogShutdown_test`,
  `RuleDisabledMissingIo_test`) parce que le niveau de journalisation par défaut retombe à 0.
- **`from_string_or_keep()`** ajoutée et utilisée à **20 sites** : « analyse, mais ne touche pas
  la destination s'il n'y a rien à analyser ». C'est le contrat implicite d'avant, écrit noir sur
  blanc, là où un défaut non nul devait survivre.
- **`from_string_or()`** ajoutée pour du code **neuf** et **délibérément retrofittée nulle part** :
  elle jette aussi les lectures partielles, et un comportement documenté en dépend
  (`per_page="1,5"` lu comme 1, T3.19).
- **Frontière d'API** : `set_state` refuse une valeur qui **finit sur son séparateur**. Coût
  assumé et écrit dans les notes de version : une variable de type texte ne peut plus être réglée
  à une valeur finissant par une espace via cette commande.

## 2026-08-25 — E4.1 : la règle vaut pour **TOUS** les `dump()`, journaux compris

**Décision** : `dump(N, ' ', /*ensure_ascii=*/true, Json::error_handler_t::replace)` **partout**,
y compris quand la sortie part dans un `cDebug()`/`cDebugDom()` ou dans un fichier sur disque.
**`ensure_ascii = false` n'est admis que dans deux cas nommés**, et aucun autre ne s'ouvre sans
une entrée datée ici :

1. **un wire tiers déjà en service en UTF-8 brut** — `Audio/RoonPlayer.cpp` (7 sites),
   `Audio/AVRRose.cpp` (9), `IO/OneWire/OWExternProc_main.cpp` (1) : changer leurs octets serait
   exactement la faute que l'invariant 3 interdit ;
2. **une sortie destinée à un œil humain ou à un fichier relu par un humain** —
   `bin/tools/calaos_config.cpp:347` et `:616` (`std::cout` d'un outil interactif),
   `lib/ConfigOptions.cpp:1496`, et `CalaosConfig.cpp:558` (le cache `iostates.cache` écrit à
   4 espaces). ⚠️ **Ce dernier n'est pas une « sortie d'outil interactif »** : c'est un **fichier
   sur disque**. La ligne de partage réelle a donc **trois** côtés, pas deux, et il faut l'écrire
   ainsi sous peine de voir le prochain ticket ranger un cache dans la mauvaise case.

**⛔ Le gestionnaire d'erreur, lui, n'a AUCUNE exception.** Il est sur les 38 sites.

**Pourquoi — l'argument décisif, et il ne porte pas sur l'échappement.** Ce qui finit par laisser
passer un `dump()` **nu** dans un journal, c'est de **dissocier les trois invariants site par
site** : dès qu'un ticket se met à arbitrer « ici c'est un log, donc je n'applique pas la règle »,
il arbitre aussi, sans le dire, sur `error_handler_t::replace` — et un `dump()` nu dans une trace,
c'est le `std::terminate` de KNX **déplacé dans le chemin de debug**, où personne ne le cherchera.
`NotifManager.cpp:258` est le précédent : le `cDebugDom` y est en `ensure_ascii = true` **et** en
`replace`, parce que le corps de notification vient de `rules.xml` et n'a jamais vu de parseur.

**Mesuré sur `refactor/e4.1d`** (en `python3`, hors `rtk`) : **38 sites `.dump()`** dans `src/`
(`json.hpp` exclu ; une 39ᵉ occurrence est une ligne de commentaire de `KNXCtrl.h:97`).
**ZÉRO nu** — les 38 portent `error_handler_t::replace`. Répartition : **17 en `ensure_ascii =
true`**, **21 en `false`**, et les 21 sont **exactement** les deux cas nommés ci-dessus.

**Comment l'appliquer** : sur un `dump()` neuf, on n'arbitre pas. On écrit la forme complète. Si
on croit tenir une exception, on la fait entrer dans l'une des deux catégories ci-dessus **ou** on
ouvre une entrée datée dans ce fichier. Les onze sous-tickets restants d'E4.1 n'ont donc plus à
rejouer l'arbitrage à chaque `cDebug()`.

**Origine** : soumis en réserve par E4.1d, qui avait mis son unique `dump()` — la trace
`cDebug()` de `SqueezeboxWire::prettyPrint()` — en `ensure_ascii = true` **sans** que la règle
existe, et qui déclarait l'arbitrage « log contre wire » comme non tranché. Le recensement
ci-dessus montre que l'arbre l'avait déjà tranché, mais nulle part par écrit.

## 2026-08-24 — E4.1 : l'échappement du wire JSON sera `ensure_ascii = true`

**Décision** : tout `dump()` nlohmann d'un payload sortant du serveur s'écrit
`dump(-1, ' ', /*ensure_ascii=*/true, Json::error_handler_t::replace)`.
**Alternative écartée** : les octets UTF-8 bruts, qui sont le défaut de `nlohmann`.

**Pourquoi — le delta minimal.** Aucune des deux options nlohmann ne reproduit jansson à l'octet
près, donc **le wire change de toute façon**. Mesuré par le tripwire d'E4.1a, sur la chaîne brute :

| Forme | Producteur | `é` = U+00E9 | U+001F |
|---|---|---|---|
| 1 | jansson `JSON_ENSURE_ASCII` (aujourd'hui) | `\u00E9` hex **MAJUSCULE** | `\u001F` |
| 2 | `dump()` nu | octets UTF-8 **bruts** | `\u001f` |
| 3 | `dump(…, ensure_ascii = true)` ⬅️ **retenu** | `\u00e9` hex **minuscule** | `\u001f` |

La forme 3 garde le wire **ASCII pur**, comme aujourd'hui : **la seule différence avec jansson est
la casse de l'hexadécimal**. Un parseur JSON correct ne voit rien ; seul un analyseur maison
sensible à la casse serait touché — et c'est le risque résiduel, nommé et déclaré en
`RELEASE_NOTES.md`. La forme 2 cesserait de garantir l'ASCII-only : changement de forme bien plus
large, dans une zone drivers **sans filet**.

⚠️ **Correction d'une idée reçue** : les caractères de contrôle **divergent aussi**. La divergence
apparaît dès que l'hexadécimal contient une **lettre** (`U+001F` diverge, `U+0001` non). Ce n'est
donc pas « seulement le non-ASCII ».

**La porte reste ouverte** : passer aux octets bruts est possible **plus tard, comme changement
délibéré et déclaré** — jamais comme effet de bord d'une migration.

**⚠️ Exception nommée** : sur les wires **tiers déjà en service en UTF-8 brut**
(`Audio/RoonPlayer.cpp`, `Audio/AVRRose.cpp`, `IO/OneWire/OWExternProc_main.cpp`,
`bin/tools/calaos_config.cpp`, `lib/ConfigOptions.cpp`), on ajoute **le gestionnaire d'erreur
seul**, **pas** `ensure_ascii` : changer les octets d'un wire que l'épique ne migre pas serait
exactement la faute que cette décision interdit.

**⚠️ Fait mesuré qui a motivé la décision** : `nlohmann` **émet déjà** sur l'API, aujourd'hui —
`JsonApiHandlerHttp.cpp:253` et `JsonApiHandlerWS.cpp:75` dument **à nu**, sans gestionnaire
d'erreur, sur les chemins `login` et `scope denied`. L'API sert donc **deux formes d'échappement
différentes selon le chemin de code**. La décision les unifie ; elle n'introduit pas l'écart.

**Appliquer** : [E4.1](E4.1.md) § « Invariants de l'épique » ; [E4.1b](E4.1b.md) (durcissement des
émetteurs existants, **avant** toute migration) ; [E4.1s](E4.1s.md) (bascule du wire et déclaration
de risque). ⛔ **Le tripwire doit basculer vers la FORME 3, pas la 2** — un implémenteur qui le fait
rougir dans la mauvaise direction croirait avoir réussi.

## 2026-08-24 — E4.1 : découpage en 17 sous-tickets, et ce que le filet ne couvre pas

**Décision de conception** (pas un arbitrage utilisateur, consignée ici parce qu'elle corrige une
croyance qui circulait) : **les 145 goldens ne couvrent PAS la forme d'octets.** Ils comparent des
**documents JSON parsés** — contrat d'oracle sémantique posé par E4.0a, choisi délibérément parce
qu'un test byte-exact aurait échoué intégralement à la bascule à cause du tri des clés, pour une
raison déjà acceptée.

⇒ **Aucun golden ne rougira sur un changement d'échappement**, ni pour l'API ni pour les drivers.
**La zone « sans filet » sur cette dimension, c'est toute la migration**, pas seulement les
drivers. Chaque fiche de sous-ticket distingue donc **deux dimensions** : structure/valeurs
(couvert) et forme d'octets (nu).

**Le verdict sur les wires drivers, établi au source, fichier par fichier** : sur les 8 wires,
**6 sont internes aux deux bouts** (Wago, OLA, MQTT, KNX, Reolink, Lua — le processus externe et le
serveur sont dans ce dépôt, construits par le même `Makefile.am`, livrés par le même paquet) et
**2 sont en lecture seule depuis un tiers** (Squeezebox, Hue — ils ne construisent aucun JSON
sortant). **Aucun wire driver n'est exposé à un tiers en écriture**, et chaque extrémité décode avec
un vrai parseur JSON, jamais par recherche de sous-chaîne. ⇒ **le risque « un parseur maison en
aval » n'existe sur aucun wire driver** ; il ne subsiste que sur l'API publique, où il est déclaré.

**Appliquer** : [E4.1](E4.1.md) (découpage, vagues, 5 questions ouvertes) et les fiches
`E4.1b.md` → `E4.1x.md`.

## 2026-08-24 — AutoScenario : la définition vit dans **`io.xml`**, portée par les **params de l'IO**

**Décision** : la définition des auto-scénarios est persistée **dans `io.xml`**, et **non** dans un
nouveau fichier `scenarios.xml`. **Alternative écartée** : le fichier séparé, qui était la
recommandation de l'agent de conception **et** de l'orchestrateur.

**Pourquoi — les mots de l'utilisateur** :

> « Toute l'infra calaos et ses outils tournent autour de `io.xml`/`rules.xml`. C'est ce qui est
> backup, ce qui est download/upload par `calaos_installer`, etc. Donc pour remettre une
> installation en route, **2 fichiers que tout le monde connaît et ça roule**. Si on ajoute
> `scenarios.xml` ça casse ce principe. On modifiera `calaos_installer` en fonction. »

**L'arbitrage, à ne pas re-proposer dans six mois** : le contrat « deux fichiers » est un
**invariant d'exploitation**, pas un accident historique. Un troisième fichier n'aurait pas
supprimé le risque de perte, il l'aurait **déplacé** : chaque outil, script de sauvegarde et
procédure de restauration aurait dû apprendre son existence, et **le premier qui l'oublie perd les
scénarios en silence**, sans même une erreur au démarrage. Le code confirme l'invariant :
`JsonApiHandlerHttp.cpp:631-633` n'accepte au téléversement que `io.xml`, `rules.xml` et
`local_config.xml`, en liste blanche codée en dur.

**⭐ Le porteur, et c'est lui qui rend la décision SÛRE : les params de l'IO, pas des nœuds XML
enfants.** Mesuré au source de `calaos_installer` :

| | lecture | écriture | verdict |
|---|---|---|---|
| **params (attributs)** | `Params` construit depuis **tous** les attributs, **sans liste blanche** — `projectmanager.cpp:611-618` | **tous** les params réémis — `projectmanager.cpp:197-204` | ✅ **préservés** |
| **nœuds enfants** | consommés et **jetés** — `projectmanager.cpp:653-658` | **aucun** réémis (hors cas spécial `RemoteUI`) | ❌ **perdus** |

Preuve empirique dans la config de production : `cycle="false"` et **78** attributs `log_history=`
survivent dans `configs/raoulh/io.xml` — des params dont `calaos_installer` n'a aucun modèle.

**Conséquence de premier ordre** : la modification de `calaos_installer` **n'est PAS une dépendance
dure** de la refonte. Un installeur **ancien** — et il en restera en circulation longtemps, sans
qu'aucune mise à jour puisse les rattraper — **préserve la définition sans rien en savoir**.
Le ticket **I4.1** reste recommandé, sur ses propres mérites : 4 `if (x)` sans `else`
(`projectmanager.cpp:1023`, `:1054`, `:1082`, `:1125`) purgent silencieusement les entrées/sorties
à id non résolu, ce qui concerne **toutes** les règles et pas seulement les scénarios.

**⚠️ Le risque assumé, écrit sans atténuation.** Si la définition avait été portée par des **nœuds
XML enfants**, un `save-online` depuis un `calaos_installer` non corrigé aurait **détruit purement
et simplement tous les auto-scénarios** de la configuration de production — perte franche, sans
message, sans sauvegarde côté installeur. Ce risque est **écarté par le choix du porteur « params »,
pas par le choix du support**. Il reste **deux** risques résiduels, tous deux nommés :
1. `DialogListProperties` (`calaos_installer/src/DialogListProperties.cpp:85-90`) permet la
   **suppression manuelle** de n'importe quel param d'IO sauf `type` et `name` : un utilisateur peut
   casser un scénario depuis cette boîte de dialogue avancée. Action délibérée, pas perte
   silencieuse. Correctif en I4.1.
2. Si un futur ticket devait malgré tout déplacer la définition vers des nœuds enfants, **le risque
   de perte franche reviendrait intégralement**. À ne pas faire sans corriger l'installeur d'abord,
   et sans accepter de perdre les installeurs anciens.

**Défense en profondeur côté serveur, indépendante de tout cela** : auto-réparation des règles
générées (elles sont régénérées depuis la définition à chaque chargement) ; sauvegarde avant
écrasement, **déjà en place** (`JsonApiHandlerHttp.cpp:624` → `Config::BackupFiles()`,
`CalaosConfig.cpp:614`) mais **non testée ni documentée** ; détection et alerte quand un
téléversement fait disparaître un scénario connu. **Pas de refus de téléversement** — il bloquerait
la suppression légitime, même raison que Q1 ci-dessous.

**Appliquer** : [E4.6](E4.6.md) §4 (D2, D10), §7bis (I4.1), sous-tickets **E4.6b** et **E4.6h**.

## 2026-08-24 — AutoScenario : les 4 autres questions de conception tranchées

**Q1 — le refus de `modify` décidé le matin même est ABANDONNÉ.** `autoscenario modify` **accepte**
un payload citant un IO absent et répond `success` ; le scénario **reste `broken`**, `missing_ios`
rempli. **Ce n'est pas un oubli** : ce refus (entrée « `autoscenario modify` refuse un **payload**… »
ci-dessous, même date) existait pour compenser **une perte d'information** — `toJson()` escamotait
l'action dont l'IO manque, donc un aller-retour blanchissait un scénario amputé. **La refonte
supprime la perte** : l'action est conservée partout, avec `resolved:"false"`. Blanchir devient
impossible **par construction**, et le refus n'ajouterait plus aucune protection tout en empêchant
une modification légitime (renommer, changer une pause) tant qu'une étape est cassée.
*Règle générale : une garde qui compense une perte d'information doit disparaître avec la perte.*

**Q2 — le payload reste TOUT EN CHAÎNES.** Cohérence avec les 19 autres domaines de l'API, et
l'oracle du harnais de test est **type-strict** (`3 != "3"`) : un payload à types mixtes
multiplierait les faux rouges à chaque golden régénéré. Si ce changement doit avoir lieu, c'est
partout à la fois, et c'est une décision d'E4.1.

**Q3 — l'étape terminale devient un champ séparé `final_step`.** `len(steps)` vaut enfin
`steps_count` ; l'invariant piégeux `+1`, sur lequel la documentation d'E4.0f s'était déjà trompée
**dans le mauvais sens**, devient inexprimable. `steps_count` disparaît du payload.

**Q5 — `IO/Scenario.cpp` est EXCLU du périmètre d'E4.1** et migré directement par E4.6 en
`nlohmann::json`. Tout le JSON du fichier est dans `toJson()`, que E4.6d réécrit intégralement :
le migrer d'abord serait le migrer deux fois, dont une sur du code condamné. ⚠️ Le suivi d'E4.1 doit
porter la ligne « `IO/Scenario.cpp` — exclu, migré par E4.6 » **explicitement**, sinon le fichier
compte comme migré alors qu'il ne l'est pas. Note posée dans `E4.1.md`.

**Appliquer** : [E4.6](E4.6.md) §10.

## 2026-08-24 — AutoScenario : refonte complète (API + modèle), **rupture assumée**

**Décision** : la fonctionnalité AutoScenario est **refondue entièrement, API ET modèle interne**.
Rupture d'API totale, changement de format de persistance, changement de sémantique. **Aucune
compatibilité ascendante, aucun convertisseur.**

**Le mandat, dans les mots de l'utilisateur** : les 4 auto-scénarios de sa config de production
étaient **des essais avec une vieille UI** qui consommait cette API. **Les scénarios fonctionnent
encore car ce sont des Rules classiques.** Il propose qu'on **change les ids et le fonctionnement**
pour que les futurs auto-scénarios soient enregistrés d'une manière différente, et que **les
actuels ne soient plus considérés comme des auto-scénarios**. « *On peut donc casser entièrement
cette API et rajouter/modifier/corriger ce qu'il faut.* »

**Pourquoi c'est sans risque de régression client** :
- **aucun consommateur.** 6 dépôts voisins (`calaos_mobile`, `calaos_remote_ui`, `calaos_windex`,
  `calaos-build`, `calaos-container`, `calaos_docker`) : **zéro occurrence**. `calaos_installer` :
  **0 appel** — ses 4 occurrences sont l'ioDoc généré, et il n'écrit pas par l'API mais en
  **téléversant `io.xml`/`rules.xml` entiers** (`dialogsaveonline.cpp:100-122`). Le passe-plat MCP
  (`client.py:156-158`) est câblé mais **aucun tool ne l'appelle**. **Littéralement aucun appelant
  first-party.**
- **des essais d'une vieille UI**, pas une fonctionnalité en service ;
- **les données survivent comme règles ordinaires** : un IO `type="scenario"` dont le marqueur n'est
  plus reconnu reste déclenchable (`IO/Scenario.cpp:63-106`, les deux portes T3.18 sont gardées par
  `auto_scenario &&`), et ses règles restent des `Rule` évaluées normalement.

**⛔ La condition unique, à ne jamais perdre de vue.**
`ListeRoom::checkAutoScenario()` (`ListeRoom.cpp:320-330`) **détruit toute règle portant le param
`auto_scenario` qu'aucun `AutoScenario` n'a adoptée**, puis `SaveConfigRule()` (`:341`) persiste la
suppression. `Params` est une correspondance **exacte** (`src/lib/Params.cpp:31-37`) : renommer le
marqueur laisse `param_exists("auto_scenario")` **vrai** sur les anciens fichiers et
`isAutoScenario()` **faux**. ⇒ **sans re-cléage de ce balayage, les 18 règles de
`configs/raoulh/rules.xml` sont détruites au premier démarrage, en silence.**
La promesse « elles continuent de fonctionner » **n'est vraie que si ce balayage est re-clé ou
supprimé**. Épinglé par un test dédié écrit **avant** toute ligne de `src/` (E4.6a).

**Conséquences** :
- les 4 anciens auto-scénarios **disparaissent** de `autoscenario list` / `get` et **continuent de
  fonctionner** : les 2 planifiés se déclenchent toujours à leur créneau, les 2 boutons restent
  visibles et déclenchables, y compris par MCP (`tools/scenario.py` filtre sur `gui_type`, pas sur
  le marqueur) ;
- **rien n'est touché sur le disque de production** : les params `auto_scenario` orphelins restent,
  inertes (`IOBase::isAutoScenario()` n'a **aucun** consommateur) ;
- **10 goldens sur 145 bougent**, nommément, jamais par régénération de masse ;
- **T3.20 est parké ⛔** : ses corrections R3/R5 sont absorbées par la refonte. **Exception
  extraite en T3.21** : le durcissement de `buildJsonDelParam` (`JsonApi.cpp:724`, qui
  court-circuite `IOBase::del_param()`) est **indépendant des scénarios** et doit être livré seul ;
- ⛔ **E4.6 est séquencée après E4.1** : décision du même jour, plus aucun code neuf en jansson.
  Les fichiers rouverts portent **41 %** de tout le jansson du dépôt.

**Appliquer** : **[E4.6](E4.6.md)**, 7 sous-tickets `a`→`g`. **5 questions ouvertes** en fin de
ticket, dont la plus structurante : **où vit la définition du scénario** — un nouveau
`scenarios.xml` hors de portée de l'installeur, ou dans `io.xml` (amputable).

## 2026-08-24 — `autoscenario modify` refuse un **payload** qui référence un IO absent

**Décision** : `autoscenario modify` **refuse** de reconstruire un scénario dont le **payload
reçu** cite un IO qui ne résout pas, et la réponse d'erreur **nomme les ids manquants**, dans la
même forme que le refus de `autoscenario reenable`.

**Pourquoi** : sans ce refus, un aller-retour `modify` **blanchit** un scénario amputé. La chaîne
est mesurée : `deleteRules()` détruit la référence morte (`JsonApi.cpp:2034`), la reconstruction
**saute silencieusement** l'action dont l'IO manque (`if (out)` sans `else`, `:2075`),
`checkScenarioRules()` recollecte des règles toutes saines, donc `isBroken()` **redevient faux** —
et `tryReenable()` **réussit** sur un scénario ayant perdu une étape. Le drapeau collant décidé le
2026-08-16 est alors levé **légitimement, par le mécanisme prévu, sur le résultat même que la
décision voulait rendre impossible**. `missing_ios` prévient avant le round-trip, mais après il n'y
a plus rien à voir.

**Le point de conception qui commande tout : le refus porte sur ce qu'on ÉCRIT, jamais sur l'état
d'AVANT.** Conséquence directe et voulue :
- **réparer** un scénario cassé — envoyer un payload d'où l'étape morte a été retirée — reste
  **possible**, c'est même le chemin de réparation nominal ;
- **blanchir** — le réécrire à l'identique, étape morte comprise — devient **impossible**.

Un refus qui porterait sur l'état d'avant (« ce scénario est cassé, donc je refuse de le modifier »)
**enfermerait l'utilisateur** : il ne pourrait plus éditer un scénario cassé, donc plus jamais le
réparer, donc plus jamais le réactiver. Le drapeau collant redeviendrait le « piège sans clé de
sortie » que la décision du 2026-08-16 avait justement levé en posant la réactivation manuelle
comme clé.

**Conséquences à connaître** :
- **changement de contrat d'API observable** : un `modify` qui répondait `{"success":"true"}`
  répond désormais `{"error": …}`. Goldens `e40c_*` à faire bouger **nommément**, jamais par
  régénération de masse, sur les **deux** transports.
- la validation doit tomber **avant `deleteRules()`** (`JsonApi.cpp:2033`, avant `:2034`). Posée
  après, elle refuse un scénario déjà démoli : l'utilisateur perd son scénario **et** reçoit une
  erreur.
- `buildAutoscenarioCreate` porte le **même** `if (out)` silencieux (`JsonApi.cpp:1981`).
  Non couvert par cette décision, recommandé en extension — cf. T3.20, « Hors périmètre ».

**Appliquer** : **T3.20**.

## 2026-08-24 — `disabled_missing_io` : en **lecture seule** côté API

**Décision** : le param `disabled_missing_io` reste persisté et relu comme aujourd'hui, mais toute
tentative d'écriture **venant d'un client** est **ignorée**. Les deux seuls écrivains légitimes
sont **le moteur** (à la détection d'une étape amputée) et **`autoscenario reenable`**.

**Pourquoi** : `buildJsonSetParam` (`JsonApi.cpp:694`) et `buildJsonDelParam` (`:724`) acceptent
n'importe quel couple `(io, param)`, sans liste blanche. Un client authentifié peut donc **poser**
le drapeau sur un scénario **sain**. Le booléen en mémoire n'est lu qu'une fois, dans le
constructeur (`AutoScenario.cpp:112`) : le scénario continue de tourner normalement **jusqu'au
reboot**, où il se réveille désactivé. **L'écriture ne prend effet qu'au redémarrage alors que la
lecture est immédiate** — une asymétrie qu'aucune UI n'affiche et qu'aucun log ne signale sur le
coup. C'était classé « DoS par client authentifié, déjà assumé » ; l'utilisateur ferme la porte.

**Comportement retenu : ignorer + logguer, et répondre `success` comme aujourd'hui.** Alignement
sur le **précédent de l'arbre**, pas sur une invention : l'immuabilité de `set_param("id")`
(`IOBase.cpp:82-109`, `del_param` `:112-123`) logge `cErrorDom` et `return`, et `set_param()` étant
**`void`** l'API répond quand même `success` (déjà documenté en `JsonApi.h:159-162`, déjà testé,
trace visible dans `IOIdIntegrity_test.log:30`). Deux params protégés de la même classe qui
répondraient différemment au même appel générique feraient une API qui ment sur elle-même ; et
rendre `set_param` capable de refuser exige de changer une signature virtuelle surchargée dans tout
l'arbre des IO, pour un gain que le log couvre déjà.

**Conséquences à connaître** :
- ⭐ `buildJsonDelParam` **court-circuite** `IOBase::del_param()` : il appelle
  `o->get_params().Delete(...)` (`JsonApi.cpp:724`). Une garde posée dans l'IO **n'est donc pas
  atteinte sur le chemin d'effacement**. Le routage par la méthode virtuelle fait partie de la
  décision, sans quoi elle n'est appliquée qu'à moitié.
- **effet de bord assumé et souhaitable** : ce routage ferme aussi un trou **pré-existant** —
  `del_param id` par l'API supprimait réellement l'id des `Params` en laissant `io_table` clé sur
  un id disparu (`IOIdIntegrity_test` ne couvrait ce cas que par appel direct, `:95`).
- le **moteur** doit garder une porte d'écriture (`AutoScenario::setDisabledMissingIo()` écrit par
  `ioScenario`) : la garder fermée pour lui annulerait T3.18 **en silence**.
- `disabled` (choix utilisateur, planification) vit dans le **même `Params`** et reste
  **librement modifiable**. La garde ne doit jamais l'attraper.

**Appliquer** : **T3.20**.

## 2026-08-16 — T3.18 séquencé APRÈS T3.17 (ergonomie de réactivation complète)
**Décision** : ne pas livrer T3.18 avec une réactivation par la commande générique
`set_param disabled_missing_io=false`. On **attend que T3.17 libère** `JsonApi.{h,cpp}` et les deux
handlers, puis T3.18 intègre directement la commande dédiée `autoscenario reenable`, avec son
**refus explicite** nommant les ids encore manquants.
**Pourquoi** : `set_param` ne sait pas refuser. Par cette voie, réactiver un scénario encore cassé
« réussit » sans rien faire — le scénario ne démarre simplement pas (porte `isBroken()`), avec le
diagnostic seulement dans les logs et le payload. C'est précisément le défaut que T3.18 existe pour
supprimer, redéplacé d'un cran vers le haut. L'utilisateur préfère attendre et livrer l'ergonomie
complète du premier coup.
**Conséquence** : **T3.18b est annulé** (son contenu réintègre T3.18). T3.17 (5 sous-tickets) passe
sur le **chemin critique** de T3.18. Ne pas lancer T3.18 avant que T3.17 soit mergé.

## 2026-08-16 — Scénario amputé : désactiver le scénario entier (cohérent avec E4.2e)
**Décision** : quand un IO utilisé par une **étape de scénario** est supprimé, le scénario ne doit
plus être **amputé silencieusement** et continuer à tourner en séquence plus courte. Il doit être
**désactivé entièrement**, comme une règle dont une dépendance manque.
**Pourquoi** : c'est exactement la même classe de danger que les règles amputées, déjà tranchée le
2026-08-15. Un scénario qui perd une étape reste actif et exécute une séquence *différente* de
celle que l'utilisateur a écrite — silencieusement. Mieux vaut qu'il ne fasse rien de visible que
quelque chose de faux : l'utilisateur constate la panne et corrige, au lieu de subir un
comportement altéré sans le savoir.
**Portée** : l'amputation elle-même est faite par `ListeRoom::detachIOFromRules` /
`ListeRule::RemoveRule`, **hors périmètre E4.2f** (qui n'a fait que rendre la lecture mémoire-sûre)
et **pré-existante** : la règle d'étape est détruite puis `SaveConfigRule()` est appelé deux lignes
plus bas (`JsonApi.cpp:1726-1733`), donc la perte est déjà persistée sur disque.
**Précision de l'utilisateur (2026-08-16, après cadrage)** — je lui avais remonté que le sens du
risque s'inverse par rapport aux règles (une règle amputée agit *plus*, un scénario amputé agit
*moins*) et que le cadrage concluait à « zéro nouvel état persisté ». **Il maintient et durcit** :

> « On désactive le scénario, on le flag avec **un nouveau paramètre dans la config** pour que ça
> survive à un reboot, et **un user doit corriger le scénario manuellement en le réactivant**.
> Si un IO disparaît c'est un problème, on ne peut pas le résoudre sans intervention manuelle,
> et un IO dans Calaos ne se supprime pas comme ça. »

**Ce que ça tranche, contre la proposition du cadrage** : le ticket avait écarté un drapeau
persistant en le qualifiant de « piège sans clé de sortie ». **L'objection tombe — la clé de
sortie est la réactivation manuelle, et elle est voulue.** La désactivation ne doit donc PAS
s'effacer d'elle-même au rechargement quand l'IO redevient résolvable : elle est **collante**
jusqu'à action explicite de l'utilisateur. C'est délibéré : la disparition d'un IO est un
incident, pas un état transitoire à rattraper tout seul.

**Appliquer** : voir **T3.18**, à réviser en conséquence. Nouveau paramètre persisté sur le
scénario + chemin de réactivation explicite exposé par l'API. Ne pas re-demander. Le mécanisme
d'E4.2e (référence conservée verbatim + trace du manquant) reste le substrat pour *détecter* le
manque ; le drapeau persistant s'ajoute par-dessus pour *retenir* la désactivation.

## 2026-08-17 — E4.1 : UTF-8 invalide → **remplacé par U+FFFD**
**Décision** : à la migration, toute sérialisation de données influencées par le client utilise
`nlohmann::json::error_handler_t::replace`. Une valeur contenant de l'UTF-8 invalide est émise avec
le caractère de remplacement **U+FFFD**, au lieu d'être supprimée (comportement actuel) ou de faire
lever une exception (comportement par défaut de nlohmann).
**Pourquoi** : le comportement actuel est un **silence** — jansson refuse les octets invalides
**à la construction** (`json_string()` rend `NULL`, `json_object_set_new()` rend `-1`, **aucun des
deux codes n'est testé**), donc la paire est supprimée et le client reçoit un **200 avec un payload
amputé**. Mesuré par E4.0e : un IO peut revenir de `get_home` **sans sa clé `name`**, indiscernable
d'un IO qui n'en a jamais eu. Le remplacement rend le problème **visible** sans casser la réponse.
**Ce qu'on évite** : `nlohmann::dump()` **lève `type_error.316`** par défaut, et
`grep -n "try\|catch"` sur les deux handlers **ne renvoie rien** — une exception non attrapée dans
un callback libuv, c'est `std::terminate` **sur une connexion vivante**. Canal d'injection trivial :
un paramètre d'URL percent-décodé (`hef_uri_syntax.cpp` décode **avant** que `HttpClient` ne découpe).
**Appliquer** : `error_handler_t::replace` sur **chaque** `dump()` de données client. Le test
`Utf8Trap_NlohmannDumpThrowsWhereJanssonDrops` (E4.0e) épingle le code **316** exactement — il
devra être adapté **en le disant**, pas supprimé. Ne pas ajouter de `try/catch` à la place : le
handler d'erreur traite la cause, un `catch` ne traiterait que le symptôme.

## 2026-08-16 — E4.1 (JSON) : caractérisation AVANT migration, jansson supprimé à terme
**Décision** : l'objectif final est la **suppression totale de jansson**, `nlohmann::json` seul.
Mais la migration ne démarre **qu'après** l'écriture d'une série de tests de caractérisation qui
valident les entrées/sorties de l'API JSON **actuelle**. On écrit les tests sur le comportement
existant, *puis* on migre sous ce filet. Cette série préalable devient **E4.0**, dépendance dure
de E4.1.
**Pourquoi** : sans comportement de référence enregistré, un payload qui change après migration
est indiscernable d'un payload qui a toujours été comme ça — on ne saurait pas distinguer une
régression d'un comportement d'origine. Le prérequis « filet de tests » qu'E4.1 invoquait
pointait sur E4.3, fermé en ✅ mais dont la couverture (règles, XML, lifecycle IO,
WebSocketFrame, tcpsocket, ExternProc, Timer, base64, Calendar, Params, Lua) **ne contient pas
`JsonApi`** : le prérequis était coché pour un autre périmètre que celui dont E4.1 a besoin.
**Appliquer** : ne pas lancer un seul sous-ticket E4.1 tant que E4.0 n'est pas livré. Les tests
de caractérisation comparent les payloads **sémantiquement** (arbre JSON parsé), **jamais octet
à octet** — l'ordre des clés change par décision assumée (voir l'entrée « nlohmann standard »
plus bas), un test byte-exact échouerait à la bascule pour une raison déjà acceptée.

## 2026-08-15 — Transport (T2.11) : cap connexions par client, header cap fixe
**Décision** : cap de connexions **par client** basé sur l'identité X-Forwarded-For (dernière
entrée de la dernière ligne XFF — hop haproxy de confiance, règle T1.8) avec fallback pair TCP ;
défaut **50** connexions, configurable via `max_connections_per_ip`. Le cap de taille des
headers HTTP est **fixe à 32 KiB, non configurable** (aligné sur le rationale haproxy
`tune.bufsize` : une limite compile-time saine, pas un bouton de config).
**Pourquoi** : 50 couvre les UI multiples derrière un même NAT/proxy sans laisser un client
épuiser le serveur ; un header cap configurable n'a pas de cas d'usage légitime.
**Appliquer** : ne pas re-demander ces valeurs ; ne pas exposer le header cap dans la config.

## 2026-08-15 — Config corrompue (T2.4) : restore + préservation + notification
**Décision** : sur io.xml/rules.xml corrompu, restauration automatique en remontant les
backups du plus récent au plus ancien ; le fichier corrompu est **préservé** dans
`<config>/backups/corrupt/<nom>.<timestamp>` (jamais écrasé silencieusement) ; et **une seule**
notification agrégée mail+push (via NotifManager, inconditionnelle, ~30 s après le boot)
rapporte fichier / chemin préservé / backup restauré-ou-config-vide.
**Limitation connue** : local_config.xml non couvert (parsing dans Utils.cpp) — à replier
dans T2.2 (split Utils).

## 2026-08-15 — MySensors : suppression complète (code mort)
**Décision** : MySensors n'est plus utilisé par personne — supprimer tout le support de la
codebase (ticket T2.12). Rend T3.2b obsolète.
**Comment l'appliquer** : ne plus investir aucun effort dans le code MySensors (fix, revue,
refactor) ; toute découverte le concernant pointe vers T2.12.

## 2026-08-15 — GPIO : honorer le paramètre `debounce` de la config
⚠️ **Nom de la clé, corrigé le 2026-08-17** : la clé de configuration s'appelle **`debounce`**
(`GpioInputBase.h:67` `get_param("debounce")`, publiée sous ce nom dans l'ioDoc via
`paramAddFloat("debounce", …)`). **`debounce_time` est le nom de la variable C++**, pas celui de la
clé. Cette entrée disait `debounce_time` : une doc écrite d'après elle aurait enseigné une clé
**sans aucun effet** — la classe de défaut exacte qu'E4.0f a trouvée sur les clés de config mail.
`RELEASE_NOTES.md` disait déjà `debounce`, correctement.
**Décision** : câbler le paramètre `debounce` (aujourd'hui décoratif, 0.05 s codé en dur).
Fallback 0.05 s si absent/invalide. Ticket T2.13.

## 2026-08-15 — OTA : clamp de l'intervalle de rescan validé
**Décision** : le clamp [1 min, 30 jours] introduit par T1.10 est validé tel quel.

## 2026-08-15 — Wago : respawn infini, backoff court
**Décision** : le respawn du process externe Wago ne doit **jamais** abandonner, et le backoff
doit rester **court** (rampe 1,2,3 puis plafond 5 s).
**Pourquoi** : le Wago est la pièce maîtresse de l'installation. En cas de maintenance ou de
coupure réseau temporaire par l'installateur, la reprise doit être immédiate — pas d'attente
d'un long backoff, pas de redémarrage de calaos requis.
**Comment l'appliquer** : implémenté dans `WagoMap` (T1.17). Toute logique de reconnexion
future sur le chemin Wago suit le même principe : retry perpétuel, délai plafonné bas,
log d'erreur périodique (pas de silence, pas de flood).

## 2026-08-15 — MQTT : payloads non-UTF8 délivrés avec '?'
**Décision** : les payloads MQTT non-UTF8, auparavant supprimés silencieusement du JSON,
sont délivrés avec les octets invalides remplacés par `?`. Validé tel quel.

## 2026-08-15 — OneWire : filtre device hex MAJUSCULES, bornes corrigées
**Décision** : le filtre OWFS reste hex majuscules uniquement (pas `isxdigit`, qui classerait
les dossiers virtuels `alarm/`, `bus.0/` comme devices), avec les bornes 0/9/A/F corrigées.
Des capteurs jusque-là invisibles (familles 0x0*, 0x9*, 0xA*, 0xF*) peuvent apparaître — assumé.

## 2026-08 — Throttle de login derrière haproxy
**Décision** : `calaos_server` est **toujours** derrière haproxy dans calaos-os.
**Pourquoi** : sans lecture du proxy, tous les clients partagent une seule IP → un seul bucket de
backoff, un client bloque tout le monde.
**Comment l'appliquer** : le throttle de login **doit lire `X-Forwarded-For`** (faire confiance au
proxy) pour distinguer les clients. À intégrer dans le ticket qui touche l'auth JSON/login.

## 2026-08 — Pas de forçage des identifiants par défaut
**Décision** : **ne pas** forcer le changement du user/pass par défaut (« non pas de forçage
pour les identifiants »).
**Pourquoi** : choix produit de l'utilisateur.
**Comment l'appliquer** : ne pas ajouter de logique bloquante / d'avertissement forcé sur les
identifiants par défaut. Le durcissement auth reste optionnel/non-bloquant.

## Contraintes permanentes (rappel)
- **Ne jamais push sans demander** — chaque push brûle du crédit CI. Commit librement.
- **Demander validation** sur tout changement de comportement / d'API.
- Implémentation, revue, merge, investigations → **toujours en subagent** (orchestrateur pur).

## 2026-08-15 — Gadspot : suppression (code mort)
**Décision** : supprimer le driver Gadspot (T3.6) — caméra obsolète, plus d'utilisateurs.
Échec propre des configs qui le référencent (chemin null-guard IOFactory, précédent MySensors).

## 2026-08-15 — Push différé
Le push des ~74 commits (Phases 1+2+3-core) est explicitement différé par l'utilisateur.

## 2026-08-15 — TLS UrlDownloader (T2.17)
**Décisions** : (1) URLs utilisateur Web/Lua : vérifiées par défaut, avec option par-IO
`insecure="true"` à ajouter (ticket T2.19). (2) Politique caméra insecure étendue à TOUS les
consommateurs d'URLs caméra : MJPEG relay + pièces jointes mail/push (ActionCameraDownload) —
cohérence avec les snapshots, comportement pré-T2.17 conservé pour les caméras auto-signées.

## 2026-08-15 — TLS : insecure par DÉFAUT (SUPERSÈDE la décision T2.17 précédente)
**Décision utilisateur** : `insecure` doit être **true par défaut** — la majorité des caméras
sont en HTTPS auto-signé et les devices WebIO sont sur le LAN ; un défaut « vérifié » casse
toutes les installations existantes.
**Politique** : toute URL **configurée par l'utilisateur** (caméras, Hue, Web IOs, Lua,
DataLogger/influx, Squeezebox, AVRRose…) → insecure par défaut, param par-device
`insecure="false"` pour opt-in au TLS vérifié (T2.19, défaut true). Seuls les services
**codés en dur** (push.calaos.fr, calaos.fr — vrais certificats) restent vérifiés.
**Migration** : configs existantes sans l'option → true (grandfathering). calaos_installer
(repo externe) ajoutera une option écrivant `insecure="false"` pour les NOUVEAUX devices,
existants inchangés. Suggestion ouverte : matérialiser l'implicite en explicite à la
sauvegarde installer pour permettre un futur flip du défaut code.

## 2026-08-15 — Phase 4 : arbitrages
**E4.1 (JSON unique)** : migrer vers `nlohmann::json` **standard** (clés triées). Le changement
d'ordre des clés dans les réponses de l'API 5454 est **assumé** — l'ordre n'est pas sémantique
en JSON ; les clients comparant des chaînes brutes devront s'adapter.
**E4.2 (ownership)** : série des 6 sous-tickets **lancée** (prérequis « filet de tests » levé,
voir PHASE4.md). Sérialisée : revue + merge de chaque étape avant la suivante.
**E4.4 (TinyXML2)** : en attente — l'utilisateur a demandé les alternatives (bloqueur XPath
dans WebCtrl). Investigation en cours avant décision.

## 2026-08-15 — E4.4 : migration vers **pugixml** (pas TinyXML2)
**Décision utilisateur** : remplacer TinyXML 2.5.3 + TinyXPath par **pugixml**.
**Pourquoi c'est le bon choix** : pugixml embarque **XPath 1.0 nativement** → le bloqueur
`WebCtrl.cpp` (seul consommateur de TinyXPath) devient un simple portage au lieu d'une
réécriture ; lib maintenue, MIT, API DOM proche.
**Découpage** : E4.4a (build seul) → E4.4b (XPath WebCtrl) → E4.4c (sweep signatures) →
E4.4d (cœur parse/serialize, ABI config) → E4.4e (suppression du vendored).
**Vigilance imposée aux tickets** : (1) `attribute()` pugixml renvoie un objet vide (`as_int()`
== 0) là où TinyXML1 renvoyait NULL → un portage naïf réécrit la config de chaque IO ;
(2) reformat intégral des XML à la première sauvegarde (flags `save()` à caler au plus près) ;
(3) parsing plus strict → des configs tolérées avant pourraient être rejetées.

## 2026-08-15 — T3.12 (CVE TinyXML) ABANDONNÉ
**Décision utilisateur** : ne pas perdre de temps à patcher les CVE de TinyXML puisqu'il va être
remplacé — priorité **totale** à la migration pugixml (E4.4).
**Conséquence à connaître** : l'exposition (abort/boucle infinie depuis un endpoint HTTP hostile
via `WebCtrl::getValue()`) dure jusqu'à **E4.4b**, qui sort le parsing NON FIABLE de TinyXML —
et non jusqu'à E4.4e. E4.4b est donc l'étape à prioriser juste après E4.4a.

## 2026-08-16 — Plages horaires : wrap sur minuit
**Décision** : une plage inversée (fin < début, ex. `23:00 → 01:00`) doit **wrapper sur minuit**
et matcher de 23h à 1h du matin. Aujourd'hui elle est vide et ne matche jamais — l'utilisateur
qui programme un scénario nocturne n'obtient rien, silencieusement.
**Conséquence** : le test caractérisant `InvertedRangeNeverMatches` (E4.3ab) doit être RÉÉCRIT
pour le nouveau contrat. Avec masque de jours : `23:00→01:00 le lundi` = lundi 23h → mardi 1h
(continuité de la nuit), à documenter en ioDoc. Ticket T3.13.

## 2026-08-16 — RemoteUI `device_info` : à corriger
**Décision** : `device_info` doit faire l'aller-retour. `SaveToXml` l'écrit sous le nœud `room`
alors que `LoadFromXml` le cherche dans `<calaos:remote_ui>` — il n'est donc jamais relu.
Ticket T3.15, **sérialisé après E4.4cd** (le portage pugixml retype ces mêmes fonctions).
Point ouvert à trancher dans le ticket : récupérer les `device_info` orphelins des configs
existantes, ou assumer leur abandon et le documenter.

## 2026-08-16 — ⚠️ Pousser master publie des artefacts (à savoir avant tout push)
La répétition CI locale a établi que `.github/workflows/docker-publish-dev.yml` se déclenche sur
**tout push vers master**, **sans `needs:` sur build-and-test** : il incrémente la version, crée
un **tag git**, publie `ghcr.io/calaos/calaos_base:dev` + un tag versionné, et dispatche un
`build_deb` vers `calaos/pkgdebs`. Un push n'est donc **pas** une simple validation CI, c'est une
**publication**. À rappeler à l'utilisateur avant chaque demande de push.
Validé par ailleurs : `build-and-test` PASSE (première exécution réelle du chemin pugixml
**système 1.13**, jusque-là jamais construit — tous les builds locaux prenaient le vendored 1.14)
et le job `coverage` produit un vrai rapport (26,7 % lignes).

## 2026-08-25 — ⚠️ F-PYTEST-1 : `SKIP` visible plutôt qu'échec franc — **arbitrage EN ATTENTE DE VALIDATION UTILISATEUR**
**Le fait** : `tests/run-python-tests.sh` rendait **`PASS`** en n'exécutant que **23 des 42** cas de
`tests/python/` (3 suites sur 6) quand `pytest` manque — et ce n'était **pas** un `SKIP` automake,
donc rien dans `# TOTAL / # PASS / # SKIP` ne le disait. Voir [T3.44](T3.44.md) et `FINDINGS.md`.

**Le principe posé** : *un test qui ne peut pas s'exécuter doit ÉCHOUER ou être VISIBLEMENT sauté,
jamais passer.* Trois voies étaient ouvertes ; **la voie retenue est le `SKIP` automake réel
(`exit 77`) accompagné d'une comptabilité publiée** (`run-python-tests: suites=N/M cases=N/M`),
plus une ligne `PYTEST_INFO` au `configure`, de la même forme que `GTEST_INFO`.

**Pourquoi pas l'échec franc** (le plus honnête en apparence) : il casserait le build de tout
développeur sans `pytest`, **le nôtre compris**, et serait contourné dans l'heure — on aurait
troqué un faux vert contre un `TESTS` amputé, c'est-à-dire **le même silence sous un autre nom**.
**Pourquoi pas la dépendance obligatoire dans `configure.ac`** : elle déplace l'échec du test vers
la configuration, pour une dépendance **de test seulement**, et le dépôt ne le fait pour **aucune**
dépendance optionnelle — `HAVE_GTEST` inclus : sans gtest, `make check` reste vert avec 3 tests.
S'aligner sur la convention du dépôt plutôt que d'inventer.

**Ce que ça change, concrètement** : sur une machine sans `pytest`+`fastapi`+`httpx`+`colorama`,
`make check` passe de ⭐ **`95/95 PASS`** à ⭐ **`94 PASS / 1 SKIP`**, **sortie toujours 0, build non
cassé**. ⚠️ **Cette ligne a d'abord été écrite « 88/88 → 88 PASS / 1 SKIP », puis « 90/90 → 89 + 1 »** :
le compte d'entrées `TESTS` **bouge à chaque merge de la série** et doit être **recompté, jamais
recopié**. ⭐ **Recompté `python3` au merge de `T3.44` (arbre `159202b3`)** : **`94` sur `master`
`df2851d0`**, **`95`** sur la branche (l'oracle compris), **`94` en CI**, où `KNXExternProcWire_test`
quitte `TESTS` sans un mot faute de `libknx`. **Mesuré dans l'image de dev** : `# TOTAL: 95 / PASS: 94
/ SKIP: 1 / FAIL: 0`, RC 0, et **95 `.trs`** exactement. Voir [T3.44](T3.44.md) §4 et §8.1.
⇒ **personne ne casse**, mais **personne ne peut plus lire « vert » sans savoir**.

⚠️ **Le point à valider par l'utilisateur** : accepter que le `SKIP` devienne l'état **normal** de
la machine de développement et de la CI — c'est-à-dire accepter, en connaissance de cause, que
**les 42 cas Python ne soient exercés nulle part** tant que le point suivant n'est pas fait. Si
l'utilisateur préfère l'échec franc, le changement est d'une ligne dans
`tests/python-suite-runner.py` (`return 77` → `return 1`) et l'oracle
`tests/check-python-tests-reporting.sh` reste vert (il accepte 77 **ou** 1, jamais 0).

**Corollaire à ticketer séparément (non fait ici)** : `.github/workflows/ci.yml` n'installe **aucun
`python3`** (mesuré en rejouant sa liste `apt` dans `debian:12`) ⇒ les 42 cas ne tournent sur
aucune machine de CI. Un `apt-get install python3 python3-pytest python3-fastapi python3-httpx
python3-colorama` les ferait passer de **0** à **42** exécutés. `ci.yml` est propriété de `T0.1`,
d'où le ticket distinct.

### ⭐ Addendum du 2026-08-25 (revue de [T3.44](T3.44.md), `RETOUR À L'AUTEUR`) — la règle vaut AUSSI pour le harnais

L'arbitrage ci-dessus écarte **l'échec franc** parce qu'il casserait le build de tout le monde. ⚠️ La
première version du correctif l'a **réintroduit par la porte de derrière** : le méta-oracle
`tests/check-python-tests-reporting.sh` comptait les cas déclarés avec le `python3` **ambiant** et
sortait **1** quand il n'y en avait pas. Or la liste `apt` de `.github/workflows/ci.yml` n'installe
**aucun `python3`** ⇒ `make check` **RC 2**, `build-and-test` **rouge à chaque `push`** (mesuré dans
`debian:12`). ⚠️ **Le relevé de cette mesure portait le même décompte faux** (« `# TOTAL: 88 · PASS:
86 · SKIP: 1 · FAIL: 1` ») : le total de l'image CI est **89**, pas 88. **Le fait qui compte —
`FAIL: 1` et RC 2 — est intact** ; la répartition exacte `PASS`/`SKIP` de cet état-là n'est plus
remesurable (l'oracle fautif n'existe plus) et **n'est donc pas réécrite ici**, plutôt que corrigée
au jugé.

**Règle posée, générale à tous les harnais du dépôt** :

> **Un harnais qui ne peut pas mesurer n'a rien trouvé.** Il sort **77** (`SKIP`, compté dans la
> colonne visible), **jamais 1**. Un `FAIL` doit toujours vouloir dire « j'ai mesuré, et c'est
> faux » — sinon le harnais commet, un niveau plus haut, exactement la malhonnêteté de rapport
> qu'il est là pour attraper.

**Conséquence de forme** : les cas qui n'exigent **rien** (ici les deux témoins, `/bin/sh` seul) se
jouent **en premier**, avant toute recherche d'outil. On perd le moins de couverture possible sur
une machine pauvre, et l'on ne peut pas confondre « pas d'outil » avec « défaut ».

⚠️ **Corollaire pour la bascule d'une ligne** évoquée plus haut (`return 77` → `return 1` dans
`tests/python-suite-runner.py`, si l'utilisateur préfère l'échec franc) : elle reste vraie et
l'oracle reste vert — mais elle ne concerne **que** le lanceur. **Le méta-oracle, lui, doit garder
son `77`** : son rôle n'est pas de juger l'environnement, c'est de juger un rapport.

### ⭐ Addendum du 2026-08-25 (2ᵉ revue de [T3.44](T3.44.md), `MERGE SOUS RÉSERVE`) — **un `xfail` est un cas EXÉCUTÉ**

**Le fait, mesuré** : un `@unittest.expectedFailure` ordinaire sous `tests/python/` est compté
**« non exécuté »** par le back-end `pytest` (`cases=42/43`, **RC 77** — son junit le classe
`<skipped type="pytest.xfail">`) et **« exécuté »** par le back-end `unittest` (`43/43`, **RC 0** —
`addExpectedFailure`). **Les deux back-ends du même lanceur se contredisaient.**

**Arbitrage rendu : le back-end `unittest` a raison.** Un `xfail` **a tourné** — son corps s'est
exécuté et a levé, ce qui est son issue attendue. Ce n'est **pas** un cas qui n'a *pas pu*
s'exécuter, et c'est cette distinction-là, et elle seule, que la comptabilité de
`tests/python-suite-runner.py` existe pour tenir. **Seule exception** : `xfail(run=False)`, que
`pytest` préfixe `[NOTRUN]` — celui-là n'entre jamais dans le corps et compte donc comme **non
exécuté**.

⛔ **Pourquoi ça n'était pas un détail** : sans cet arbitrage, **un seul `xfail` légitime ajouté à
`tests/python/` aurait figé `make check` en `SKIP` PERPÉTUEL sur toute machine ayant `pytest`** —
c'est-à-dire sur toutes celles que [`T3.47`](T3.47.md) va créer — et aurait défait sa cible
`42/42` **sans que rien ne l'explique**.

⚠️ **Divergence résiduelle ASSUMÉE, de verdict et non de comptabilité** : un `xfail` qui passe
quand même (*xpass*) est **exécuté des deux côtés**, mais `unittest` le rend **rouge** et `pytest`
non strict le rend **vert**. Le lanceur suit **la règle de chaque back-end** ; ce qu'il publie, lui,
dit la même chose des deux côtés.

**Règle générale qui en sort** : *quand un outil a deux back-ends, toute divergence de comptabilité
entre eux est un défaut, pas une nuance — il faut trancher laquelle est juste et l'écrire, sinon
c'est l'environnement qui décide du verdict.* C'est la même famille que `F-PYTEST-1` : *le vert ment
ici et dit vrai ailleurs.*

### ℹ️ Renvoi — la liste canonique des variantes de faux vert

`FINDINGS.md` porte depuis le 2026-08-25 **LA liste canonique numérotée des QUATORZE variantes** de
faux vert/faux rouge de la série. ⚠️ **Cette phrase disait « ONZE »** : le compte était celui du
jour où la liste a été établie (`T3.44`), et il a été porté à **douze** puis **treize** par la
livraison de [`T3.40`](T3.40.md) — n° 12 *la restauration qui ne restaure rien*, n° 13 *la borne de
sûreté devenue la contrainte active*. Corrigé au merge de `T3.40` (2026-08-25). ⚠️ **Porté à QUATORZE le 2026-08-26** par le retour de revue de [`T3.51`](T3.51.md) — n° 14 *le cache de compilation qui rend un objet périmé*, ⚠️ **la seule CONDITIONNELLE de la liste** (elle n'existe que sur l'un des quatre réglages fautifs) ⛔ **mais l'un d'eux était le défaut livré**, et ⭐ **c'est la seule qui ne laisse aucune trace dans un journal**. ⚠️ **« Cause racine des CINQ variantes » reste exact** partout où c'est
écrit de `_DEPENDENCIES` ci-dessus : `T3.36` ferme les **n° 1 à 5**, et elles seules. Tout nouveau
compte ou rang se lit **dans `FINDINGS.md`**, jamais recompté à la main.

## 2026-08-26 — cache de compilation : **fermeture par défaut** de la sonde, et la liste NOIRE remplacée par une liste BLANCHE

**Contexte** : revue de la branche `tooling/ccache` ([T3.51](T3.51.md)), rendue `RETOUR À L'AUTEUR`.

⭐ **Le cache lui-même ne ment pas** — 12 scénarios d'attaque sains, 370/370 objets identiques,
aller-retour rouge, 90/90 en concurrence. **Ce qui était faux, c'est la garde.**

**Décision 1 — une sonde de garde ne rend JAMAIS PASS ni SKIP sur un mode d'échec.**
`scripts/ccache-honesty-probe.py` était *fail-open* sur **tous** les siens : elle traduisait sa propre
panne (`ccache: invalid option -- 'g'`) en `77`, elle classait `obj(A) == obj(B)` — **c'est-à-dire
exactement le mensonge qu'elle cherchait** — en « non concluant », `77`, et un build câblé par
`CXX="ccache g++"` lui faisait rendre `77` alors que le cache était **actif**. ⇒ **Désormais trois
codes seulement** : `0` PASS · `77` **uniquement** quand il n'y a aucun cache à garder · `1` **pour
tout le reste**, exception comprise. ⭐ **Généralisable** : *une garde qui ne sait pas conclure doit
échouer, pas se taire — et son silence doit être impossible à confondre avec un succès dans le
journal.* Les lignes sont préfixées `SONDE-CCACHE: PASS|SKIP|ECHEC`.

**Décision 2 — une garde de configuration audite TOUT ce que l'outil publie, pas une liste de clefs.**
La liste noire de chaînes interdites laissait passer `compiler_check = string:CONST`, strictement
équivalent à `none` qu'elle refusait nommément ; et elle **validait `compiler_check = mtime`, le
défaut que la branche livrait**. ⚠️ **La « liste blanche » qui l'a remplacée n'a tenu qu'une revue** :
elle exigeait **quatre** clefs sur les **44** que `ccache -p` publie, et laissait donc passer
`ignore_options`, `direct_mode`, `hard_link`, `prefix_command`, `compiler`, `disable`, `read_only`,
`recache`… ⭐ **Mesuré : `ignore_options=-D*` fait servir l'objet de `-DVAL=1` pour une compilation
`-DVAL=2`** — objets identiques à l'octet, `direct_cache_hit` au journal, `mov $0x1` au
désassemblage — **et la sonde rendait `0`.** ⇒ **La sonde DEMANDE À L'OUTIL sa configuration et exige
que CHAQUE clef publiée soit couverte** : valeur exigée (33 clefs) ou valeur explicitement libre
(11 clefs : chemins, quotas, compression du stockage). ⛔ **Toute clef publiée que la table ne connaît
pas ⇒ `rc=1`, en la nommant** — c'est ainsi qu'une version ultérieure de `ccache` qui ajoute un
réglage dangereux se signale au lieu de passer.
⭐ **Généralisable** : *une liste de choses à surveiller est toujours en retard d'une chose. Demander
à l'outil ce qu'il expose, et refuser tout ce qui n'a pas été audité, ne l'est jamais.* C'est
exactement le remède déjà employé sur `T3.48` (« toujours en retard d'un suffixe »).

**Décision 3 — une sonde doit pouvoir VOIR le défaut qu'elle prétend garder, et le PROUVER.**
La moitié empirique compilait une unité de traduction **sans aucun `#include`**, alors que le défaut
gardé porte sur les **fichiers inclus** : elle imprimait « aller-retour honnête » sur un cache
démontré menteur. ⇒ **La sonde est exercée contre le défaut, garde désarmée, et le voit** :
`sloppiness = file_stat_matches,file_stat_matches_ctime` ⇒ `1` en **4/4**, témoin honnête `0` en
**2/2** (ccache 4.12.3 comme 4.7.5).
⚠️ **Restriction mesurée, à ne pas élargir** : `file_stat_matches` **SEUL** rend `rc=0` en **4/4** —
`ccache` compare encore le `ctime`, qu'`os.utime` ne peut pas remettre en place. **Seul le couple**
se voit ainsi ; le réglage seul n'est attrapé que par la table de la décision 2.
⚠️ Et le piège ne tient qu'à un détail qui n'en est pas un : la **mtime figée dans le passé**. Avec
une mtime « maintenant », il ne mord pas de façon fiable.
⇒ ⭐ **L'audit intégral de la configuration (décision 2) est la garde PRINCIPALE** ; la moitié
empirique est un **détecteur par échantillon** qui prouve qu'un mensonge s'est produit, jamais
qu'aucun ne peut se produire. Elle ne voit ni les options ignorées, ni `direct_mode = false`.

**Décision 4 — aucun chiffre publié depuis un état partagé non attribuable.**
Le taux `416/417` avait été lu par `ccache -s` sur le `CCACHE_DIR` **partagé** pendant que **trois
agents construisaient** : c'est l'agrégat de tous. ⇒ **Retiré.** Sur cache privé : **417/420** et
**471/477**. ⛔ **Et `ccache -z` sur un cache partagé remet à zéro les compteurs de TOUS les agents**
— la version précédente du document le recommandait sans le dire.
⚠️ **Même famille** : `$CCACHE_DIR/ccache.conf` est un **état partagé mutable** — un `ccache -o`
persiste et **tout conteneur ultérieur en hérite**. Constaté : le cache partagé porte
**`max_size = 30G`** au lieu des 10G documentés, **sans attribution**, et ses **1763 fichiers sont
`uid=0`** dans un `$HOME` `uid` 1000. ⇒ **un `CCACHE_DIR` par agent**, ou les variables `CCACHE_*`
qui ne persistent rien.

**Décision 5 — un rapport de gain sans sa charge est un chiffre faux.**
Les deux rapports annoncés étaient faux **dans des sens opposés** : ×8,4 → **×6,2** (charge 10 → 125)
et ×4,3 → **×5,0**. Et « gain nul sur un `rm -f` chirurgical » est **infirmé** : **6,37 s → 2,06 s,
×3,1, −68 %**. ⇒ **Tout rapport se publie en fourchette, avec la charge relevée et la provenance.**

**Décision 6 — le cache accélère, il ne signe pas.**
Condition (5) de [T3.51](T3.51.md) : **tout RED→GREEN qui décide d'un ticket est reconfirmé UNE FOIS
SANS CACHE.** ⭐ **Et l'argument inverse, qui est le meilleur en faveur du cache** : borner le taux de
`F-FLAKY-1` sous 1 % demande **~300 verts consécutifs**, soit **8,8 h sans cache contre 2,5 h avec**
(`make check` 105 s → 29,8 s) — *le cache ne sert pas d'abord à aller plus vite, il rend faisable une
mesure qu'on renonçait à faire.*

**Décision 8 — un détecteur qui ne peut pas mordre doit ÉCHOUER, pas réussir.**
La sonde imprimait « *le piège est armé et il mord* » **sans jamais le vérifier** : elle calculait
l'information (le journal par invocation `CCACHE_STATSLOG`) et se contentait de l'imprimer.
⭐ **Mesuré : `CCACHE_DISABLE=1`, `recache`, `read_only` rendaient `0` en 3/3** avec la garde de
configuration désarmée — le cache ne servait rien, donc **rien ne pouvait être détecté**, et la sonde
concluait « honnête ». ⇒ **La sonde EXIGE désormais un succès de cache CONSTATÉ** : si l'en-tête
restauré à l'identique (3ᵉ compilation) et la source rejouée (6ᵉ) ne sont pas servis **par le cache**,
`rc=1`. Rejoué : les trois cas ci-dessus rendent **`1` en 3/3**, garde désarmée.
⭐ **Généralisable** : *une garde doit prouver que son capteur est vivant avant de publier son
verdict ; sinon elle mesure son propre silence.*

**Décision 9 — on audite le cache que le BUILD emploie, pas celui que le `PATH` désigne.**
La sonde lisait la configuration via `ccache -p` **trouvé dans le `PATH`**, alors que les
compilations passaient par le `CXX` du build. ⭐ **Mesuré** : un enrobage nommé `ccache` qui exporte
`CCACHE_IGNOREOPTIONS=-D*` puis appelle `/usr/bin/ccache` fait auditer un ccache **propre** pendant
qu'un autre ment ⇒ **`rc=0`**, alors que `-DVAL=2` reçoit bien l'objet de `-DVAL=1`. ⇒ **La sonde
interroge le binaire de la ligne `CXX`** (un enrobage répond à `-p` avec l'environnement qu'il
injecte), ou la cible du lien pour un `g++` qui est un *shim* ⇒ **`rc=1`**.
⭐ **Généralisable** : *auditer un outil par un autre exemplaire du même nom, ce n'est pas l'auditer.*

**Décision 7 — un drapeau se vérifie dans l'outil du dépôt, pas dans sa documentation en ligne.**
`AM_DISTCHECK_MAKEFLAGS` **n'existe pas** en automake 1.16.5 : **0** occurrence dans
`am/distdir.am` **et 0** dans le `Makefile` généré ; les `$(MAKE)` récursifs de `distcheck` portent
`$(AM_MAKEFLAGS)`. ⚠️ **Et il n'existe pas non plus dans automake 1.18** : la phrase « ce nom existe
en automake récent » était fausse — **0 occurrence en 1.16.5 comme en 1.18**. L'ajouter aurait été un **no-op qu'aucun diagnostic n'aurait signalé**.
⇒ [T3.52](T3.52.md), sous son vrai nom, **portée projet** et **flottement `-j32` déclarés**.

---

## Q1 d'E4.1 — les autoscénarios de `JsonApi.cpp` (2026-09-01)

**TRANCHÉE : [E4.1r](E4.1r.md) est MAINTENU. Migration MÉCANIQUE des 9 fonctions.**

Conforme à la recommandation de la fiche et de `E4.1.md` § Questions ouvertes, Q1.

**Ce que la décision autorise.** [E4.1s](E4.1s.md) supprime les surcharges `sendJson(json_t *)` et
le chemin de dispatch `json_t*` dans sa foulée, au lieu d'en hériter la contrainte ; `JsonApi.cpp`,
`JsonApiHandlerHttp.cpp` et `JsonApiHandlerWS.cpp` perdent `#include <jansson.h>` à la fin de la
chaîne API.

**Ce qu'elle n'autorise pas.** ⛔ **Aucune amélioration, aucun renommage, aucune harmonisation,
même évidente.** Payload symétrique, `final_step` séparé, validation avant mutation : tout ça reste
à [E4.6d](E4.6.md), avec son propre filet et sa propre revue. Un diff qui « en profite » double le
coût de la revue **et** crée un conflit avec E4.6d. ⛔ **Zéro golden `e40c_*` modifié** : E4.6d en
régénérera 6 **plus tard**, si l'un bouge maintenant c'est une régression, pas une anticipation.

⭐ **La nuance relevée à l'arbitrage, à ne pas perdre.** Le gain n'est pas « toute l'API sur une
seule bibliothèque » au sens strict : `IO/Scenario.cpp` reste en jansson (décision Q5) et
`buildAutoscenarioGet` (`JsonApi.cpp:1902`) appelle `sc->toJson()`, qui rend un `json_t*`. **Un
adaptateur transitoire reste donc nécessaire sur ce seul site jusqu'à E4.6d** — nom greppable,
commentaire nommant E4.6d, **exactement un appelant** (protocole d'E4.1l). Et [E4.1x](E4.1x.md)
reste bloqué par E4.6b + E4.6d quoi qu'il arrive : **jansson ne disparaît pas du dépôt avant E4.6**.
Ce qui se jouait réellement, c'est **1 site d'adaptation** contre **9 fonctions + le double chemin
d'émission + les surcharges `sendJson(json_t *)`**.

⚠️ **Incohérence de renvoi corrigée au passage** : la fiche `E4.1r.md` et le tableau des vagues
renvoyaient à « Q3 », alors que l'arbitrage est **Q1** (Q3 porte sur l'ordre des clés d'`io_doc.json`).

---

## Trois arbitrages du 2026-09-04

### Le NUL embarqué → ✅ **GARDE DANS `IOBase::set_param()`**

**Tranché : la garde va à la frontière du modèle, pas au parse.** Un nom ou une valeur portant un
zéro est refusé **avant d'atteindre le disque**.

**Pourquoi là.** T3.58 a mesuré que l'API porte le NUL entier — **c'est l'écriture XML qui coupe** :
`XmlUtils::setAttribute()` finit sur `set_value(c_str())`, donc un **nom** de param contenant un
zéro **écrase l'attribut voisin** (si c'est `name`, l'IO est renommé) et une action d'autoscénario
emporte le reste de son étape. **Invisible jusqu'au redémarrage.**

⛔ **Le refus au parse est écarté** : il défait E4.6d, qui a travaillé pour que le NUL traverse
l'API entier (`std::string` au lieu de `json_string(c_str())`), **et il ne protège pas le disque**
des autres chemins d'écriture.
⛔ **Corriger l'encodage XML d'abord est écarté aussi** : le remède évident, `set_value(ptr, size)`
de pugixml, **a été mesuré et ne change rien** ; il faudrait encoder le zéro nous-mêmes, ce qui
change ce que contiennent les fichiers de configuration.

→ Livré par [T3.66](T3.66.md).

### La lecture des scénarios par le sidecar MCP → ✅ **LAISSÉ TEL QUEL**

Depuis [E4.6e](E4.6.md), `autoscenario` est sous `scopeDenied()`, et le gate porte sur le
**message** : `list` et `get` sont donc refusés aussi aux sessions de service.

**Conforme à D8, et ne crée aucune exception** — deux précédents exacts : **`audio_db`** est un
message à sous-commandes dont les **douze sont des lectures**, refusé en bloc ; **`eventlog`** est
une lecture pure, refusée en bloc. Les paires qui distinguent lecture et écriture (`get_param` /
`set_param`, `get_timerange` / `set_timerange`) le font parce que ce sont **deux messages**, jamais
deux sous-commandes du même. Distinguer ici aurait créé l'exception.

**Mesuré** : la seule source de session `serviceScope` est le sidecar MCP, et son passe-plat
`CalaosClient.autoscenario()` a **zéro appelant**. Personne ne perd rien.

⇒ À rouvrir seulement si un assistant doit un jour **lire** les scénarios ; il faudrait alors
découper le gate par sous-commande **et** décider du sort d'`audio_db` et d'`eventlog`.

### L'indentation du journal de rédaction → ✅ **INDENTATION CONSERVÉE**

`dumpJsonRedacted()` reste quadratique **quand le débogage réseau est réellement allumé**. La rendre
linéaire imposerait `dump(-1, …)` au lieu de `dump(4, …)`, ce qui **fait basculer deux cas nommés**
d'E4.1m — `RedactedDumpKeepsRawUtf8AndStaysIndented` et `HidesCredentialFieldsWhateverTheKeyCase`.

**Tranché : on garde.** Depuis [T3.65](T3.65.md), le coût n'existe **que** si un opérateur allume
délibérément `debug_domains network:5` — c'est un choix, pas un chemin subi. Le gain ne profiterait
qu'à une session de débogage volontaire, et l'indentation est précisément ce qui rend ce journal
lisible.

⇒ **`F-JSON-2` peut être clos** : le chemin est inatteignable depuis l'API (plafond de 2048), la
construction n'a plus lieu quand personne ne lit (T3.65), et ce qui reste est un coût assumé.

---

## Quatre arbitrages du 2026-09-05

Posés à l'utilisateur en fin de session, après la série des secrets dans les journaux.

### 1. Le `push` → ⏸ **DIFFÉRÉ, une seule fois, à la fin du backlog**

⛔⭐ **PRÉCISION DE L'UTILISATEUR (2026-09-06), ET ELLE CHANGE LA NATURE DE LA DÉCISION** :
pousser ne lance pas seulement des tests — **ça déclenche un build de développement qui est
déployé**. Le `push` n'est donc **pas** une vérification bon marché qu'on pourrait faire tôt pour
dégrossir : c'est une **livraison**. Il attend la fin des tickets, sans exception, et il reste
**interdit aux agents**.


La CI GitHub n'a jamais tourné sur les 88 commits de la série. Ce qu'elle seule peut vérifier : la
**syntaxe du workflow** et le **build réel des deux `Dockerfile`** — les étapes `run:` ont été
rejouées à la main dans un conteneur neuf, ce qui n'est pas la même chose.

**Tranché : on ne pousse pas encore.** On finit le backlog, puis un seul `push`. Le premier job
cumulera davantage, mais on évite les allers-retours de CI sur un arbre qui bouge encore.
⇒ Rien ne change pour les agents : **`push` interdit** jusqu'à nouvel ordre.

### 2. [`T3.82`](T3.82.md) — le secret du courtier MQTT → ✅ **PAR LA SOCKET**

Mesuré : `/proc/<pid>/cmdline` est en **444** et un compte **tiers** relit l'argv verbatim ; un `ps`
suffit. L'environnement (400) protège d'un autre compte mais ni du même UID ni de `root`.

**Tranché : la socket.** Le sidecar se connecte d'abord, le serveur lui envoie sa configuration en
premier message — rien ne reste dans `/proc`. ⭐ **La forme existe déjà dans l'arbre** :
`ReolinkWire::buildRegisterMessage()` fait exactement cela, ce qui écarte l'argument du coût.

⚠️ **Ce que ça déplace, et qu'il faut écrire** : le sidecar attend sa configuration au lieu de la
trouver dans `main()`, donc le chemin d'échec « configuration illisible » change de place, et le
contrat du sidecar change des deux côtés.

### 3. [`T3.72`](T3.72.md) — `&#01;` n'est pas du XML 1.0 conforme → ✅ **REFUS À L'ÉCRITURE**

U+0001 n'appartient pas à la production `Char` de XML 1.0 : `&#01;` est une référence à un caractère
que la grammaire interdit, et **aucun analyseur conforme n'est tenu de l'accepter**. `pugixml` relit
ce qu'il écrit, donc Calaos s'en sort ; un éditeur tiers ouvrant `io.xml` peut refuser.

**Tranché : on refuse à l'écriture.** ⛔ **Rupture de compatibilité assumée** — l'API cesse
d'accepter des octets qu'elle transporte aujourd'hui. Motif : un `io.xml` non conforme est un
fichier que son propriétaire ne peut pas ouvrir avec ses propres outils, et personne ne saisit
sciemment un caractère de contrôle dans un paramètre.

⚠️ **Écarté explicitement** : remplacer par U+FFFD, comme `Config::saveStateCache()` le fait
ailleurs. Rien ne serait refusé et le fichier redeviendrait conforme, mais **la valeur changerait en
silence** — un refus visible vaut mieux qu'une substitution muette sur un chemin d'écriture.

### 4. `T3.71` — `create` tronque, `modify` refuse → ✅ **ALIGNER SUR LE REFUS**

Sur un nom d'auto-scénario portant un octet nul, `buildAutoscenarioCreate()` écrit une version
**tronquée** là où `modify` **refuse**. L'API se contredit, et ce demi-chemin n'est épinglé par
aucun test.

**Tranché : `create` refuse comme `modify`.** Même famille que l'arbitrage 3 et même motif : ce sont
des noms que personne ne saisit à la main, et une API qui refuse ici et tronque là est plus
surprenante que celle qui refuse partout.

---

## Cinq arbitrages du 2026-09-06

Posés à l'utilisateur après la série des journaux et la mesure de `T3.41`.

### 1. [`T3.60`](T3.60.md) — la portée de service en HTTP → ✅ **LA GARDE REMONTE SOUS LE DISPATCH**

Les **huit** commandes qu'une session de service se voit refuser en WebSocket passaient
**intégralement** en HTTP : `serviceScope` est un membre de `JsonApiHandlerWS` et de lui seul.

**Tranché : la portée de service devient une propriété de la session, pas du transport.**
Écarté : « écrire et prouver qu'HTTP n'ouvre jamais de session de service » — ça ferme la question
par une démonstration qui devra être refaite à chaque évolution du transport ; et « laisser et
documenter », seul choix qui aurait laissé un écart de sécurité connu ouvert.

⚠️ **À mesurer AVANT d'écrire** : un client HTTP ouvre-t-il aujourd'hui une session de service et
emploie-t-il l'une des huit ? Si oui, c'est une rupture et elle doit être nommée.

### 2. [`T3.101`](T3.101.md) — le relais des sidecars → ✅ **DANS LE JOURNAL, AU NIVEAU DEBUG**

Tout ce que les sept familles impriment était recopié sur la sortie standard du serveur, **hors de
son journal** : le seul filtre applicable était celui de l'enfant, `debug_level` n'avait aucune prise.

**Tranché : chaque ligne d'un sidecar devient une ligne de journal ordinaire**, avec son domaine et
un niveau que l'utilisateur contrôle. Par défaut le serveur se tait ; le débogage reste possible en
montant la verbosité. Écarté : un interrupteur dédié, qui rendrait un sidecar déraillant **invisible**
tant que personne n'y pense.

⚠️ **À traiter** : une ligne très longue ou binaire doit être bornée. Voir les capteurs à borne
(`check-echo-ceilings.sh`).

### 3. [`T3.105`](T3.105.md) — la relance des sidecars → ✅ **RALENTIR, SANS JAMAIS ABANDONNER**

Neuf abonnés relancent à l'identique toutes les 100 ms, sans regarder le statut de sortie que
[`T3.103`](T3.103.md) vient de rendre significatif.

**Tranché : délai croissant à chaque échec, plafonné (100 ms → 30 s), aucun abandon.** Un courtier
éteint puis rallumé se rattrape seul, et le journal cesse de défiler. Écarté : abandonner après N
échecs — il faudrait alors une action humaine pour relancer, donc un verbe d'API à écrire et une
panne silencieuse de plus. Écarté aussi : ne ralentir qu'après une mort précoce — plus fin, mais plus
de code pour un gain que rien ne mesure.

⭐ **Ce ticket ferme aussi le trou d'E4.5d**, qui dépassait MQTT depuis le début.

### 4. [`T3.104`](T3.104.md) — la moitié « actions » → ✅ **REFUSER À LA SOURCE, COMME `T3.72`**

**Tranché : la garde se pose sur la valeur d'action au moment où l'API l'accepte**, pas dans
l'écriture des règles. Cohérent avec l'arbitrage 3 du 2026-09-05, et ⛔ **ça n'empêche pas de
ré-enregistrer un fichier ancien** — c'est précisément l'écueil que `T3.72` avait évité et que poser
la garde dans `Rules/` aurait rouvert.

⚠️ Le vrai porteur reste `calaos_installer`, un autre dépôt : la limite doit être écrite.

### 5. [`T3.106`](T3.106.md) — les deux restes de `T3.41` → ✅ **RETOMBER EN LE DISANT**

(b) Une `listen_address` illisible **retombe sur `0.0.0.0`, avec un avertissement qui nomme la valeur
refusée**. Écarté : refuser de démarrer — une faute de frappe deviendrait une panne totale, et un
boîtier domotique qui ne démarre plus est pire qu'un boîtier trop ouvert qui le dit.

(a) N'est **pas** un arbitrage : la porte de privilège non tenue se ferme, et les deux règles de
boucle locale de l'arbre (`/24` ici, `/8` là) doivent dire la même chose ou expliquer pourquoi non.

---

## 2026-09-07 — [`T3.125`](T3.125.md) : la publication est conditionnée à la CI

### ✅ **CONDITIONNER LE WORKFLOW DE PUBLICATION À UNE CI VERTE** (issue B)

**Le fait mesuré**, par [`T3.22`](T3.22.md) puis confirmé à sa revue : `docker-publish-dev.yml` part
sur `push` vers `master`, son unique job n'a **ni `needs:` ni `if:`**, l'arbre ne porte **aucun**
`workflow_run`, et les tests vivent dans un **autre fichier** de workflow — donc hors de portée d'un
`needs:`. ⇒ **tout merge publie** (version, étiquette git, image `ghcr.io`, paquet Debian) **quel que
soit l'état de la CI**. ⛔ Et la ligne 16 du fichier **affirmait le contraire** — corrigée au merge de
`T3.22`.

**Tranché : la publication attend que les tests passent.** Le réglage vit **dans le dépôt**, donc il
est versionné, relisible et se défait par une pull request comme le reste.

⚠️ **Ce que ça change, et qui est le but** : une CI rouge **bloque la livraison**. Et si une suite
redevient instable, **plus rien ne publie** tant qu'elle n'est pas réparée.
⭐ **C'est précisément pourquoi [`T3.112`](T3.112.md) passait avant** : conditionner sur une CI qui
échoue une fois sur trois aurait installé une garde qui bloque au hasard. La cause y a été trouvée
(l'échelle de retransmission du SYN, budget posé au milieu de la queue) et le rouge est reproductible
à la demande.

⛔ **Écarté : la protection de branche seule** (issue C) — plus forte, puisqu'elle bloque le merge en
amont, mais c'est un **réglage d'interface invisible dans le dépôt**, et un agent ne peut pas le
poser. Reste disponible en complément, à la main de l'utilisateur.
⛔ **Écarté : ne rien faire** — le motif « c'est une image de développement, publier à chaque merge
est voulu » était défendable tant que le commentaire du fichier ne prétendait pas le contraire.

⚠️ **Ce que ça ne change PAS** : le `push` reste **une livraison**. Conditionner la publication à la
CI ne la rend pas gratuite — ça garantit seulement qu'on ne livre pas du rouge.

---

## 2026-09-07 — le `push` est autorisé, une fois quatre tickets faits

**Décision de l'utilisateur.** Le `push` — donc la **livraison** — part **quand ces quatre-là sont
mergés**, et pas avant :

| | |
|---|---|
| [`T3.117`](T3.117.md) | rien ne refuse un `autoscenario_uid` dupliqué ; supprimer un scénario détruit la machinerie de l'autre |
| [`T3.109`](T3.109.md) | deux mises en écoute lient toutes les interfaces **en dur** — `listen_address` ne les confine pas |
| [`T3.105`](T3.105.md) | le serveur relance ses sidecars toutes les 100 ms sans jamais lire leur statut de sortie |
| [`T3.107`](T3.107.md) | sous une `listen_address` IPv6, `UDPServer` se lie bien mais ne lit plus ses correspondants |

⛔ **Les 21 autres tickets ouverts ne bloquent pas le `push`** — dix sont mineurs ou des arbitrages,
sept sont des filets qu'aucun utilisateur ne peut observer, quatre sont de l'outillage.

⚠️ **Ce que ce `push` déclenche, et qui n'a jamais tourné** : la CI GitHub, pour la première fois de
toute la série. Depuis [`T3.125`](T3.125.md), **la publication attend qu'elle passe** — donc un rouge
ne livre pas, mais **une CI qui ne tourne pas du tout ne livre pas non plus, en silence**.
⭐ La liste de ce que seul ce `push` peut trancher est en tête de l'ÉTAT DE SORTIE d'`ORCHESTRATION.md`.

⛔ **La consigne « aucun agent ne pousse » reste entière** : c'est l'orchestrateur qui poussera, une
fois les quatre mergés, et il l'annoncera avant.


## 2026-09-07 — l'utilisateur demande trois changements de cadence, et le `push`

Question posée : « pourquoi c'est tellement long ? ». Réponse chiffrée sur un cycle réel — dév.
T3.109 **50 min**, merges T3.117 **42 min**, T3.107 **95 min**, T3.105 **113 min**. La revue coûte
deux développements. Trois changements **tranchés et appliqués** : développements en parallèle sur
fichiers disjoints, revue **graduée selon le risque**, merges **groupés**. Le détail et les gardes
qui les rendent sûrs sont dans `ORCHESTRATION.md`.

⭐ **Le `push` est autorisé sans condition résiduelle** : « push quand T3.109 est mergé ». Il l'est
(`486c590a`), les quatre tickets de la décision du matin sont sur `master`.

⛔ **Et une consigne neuve, qui prime sur la reprise du travail** : **après le `push`, surveiller la
CI jusqu'à sa conclusion avant toute autre chose.** L'ordre réel est `push` → `ci.yml` → *si vert
seulement* → publication de `ghcr.io/calaos/calaos_base:dev`. Une CI rouge ne déploie rien — mais
`T3.133` établit que les quatre conditions de cette garde ne sont tenues par aucun capteur : ce
`push` est donc aussi le premier test de la garde elle-même.
