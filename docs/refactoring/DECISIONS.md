# Décisions — journal durable

> Décisions prises par l'utilisateur (Raoul) pendant le refactoring. But : qu'un Claude neuf
> **ne les re-demande pas** et respecte les contraintes. Format : date, décision, pourquoi,
> comment l'appliquer. Ajouter en tête (plus récent en haut).

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

## 2026-08-15 — GPIO : honorer `debounce_time` de la config
**Décision** : câbler le paramètre `debounce_time` (aujourd'hui décoratif, 0.05 s codé en dur).
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
