# Décisions — journal durable

> Décisions prises par l'utilisateur (Raoul) pendant le refactoring. But : qu'un Claude neuf
> **ne les re-demande pas** et respecte les contraintes. Format : date, décision, pourquoi,
> comment l'appliquer. Ajouter en tête (plus récent en haut).

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
