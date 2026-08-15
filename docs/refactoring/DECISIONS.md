# Décisions — journal durable

> Décisions prises par l'utilisateur (Raoul) pendant le refactoring. But : qu'un Claude neuf
> **ne les re-demande pas** et respecte les contraintes. Format : date, décision, pourquoi,
> comment l'appliquer. Ajouter en tête (plus récent en haut).

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
