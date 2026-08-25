# Décisions — journal durable

> Décisions prises par l'utilisateur (Raoul) pendant le refactoring. But : qu'un Claude neuf
> **ne les re-demande pas** et respecte les contraintes. Format : date, décision, pourquoi,
> comment l'appliquer. Ajouter en tête (plus récent en haut).

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
