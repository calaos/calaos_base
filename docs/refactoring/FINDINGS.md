# Findings bonus — backlog

> Découvertes faites **en marge** des tickets (hors périmètre du ticket en cours, donc **non
> corrigées**). Candidates à de futurs tickets. Sorti du job tmp éphémère → durable + partagé.

## Sécurité / correctness à traiter en priorité

- **[CORRECTNESS] Déréférencement `createIO()` sans garde nullptr** — `JsonApi.cpp:1366-1368` et
  `AutoScenario.cpp:157-160` déréférencent l'IO retourné sans vérifier null ; un miss de factory
  (type inconnu, ou collision « first-wins » qui retourne null depuis T1.11) → segfault. T1.11 a
  durci la factory + `genDocIO` mais **pas** ces sites d'appel externes (hors périmètre fichiers).
  → ticket : auditer tous les appels `createIO` / `IOFactory::CreateIO` pour garde null.

- **[SÉCURITÉ, même classe que F2] `RemoteUIManager::getRemoteUIByToken` compare `auth_token`
  avec `string ==`** — canal auxiliaire temporel sur le token lui-même (pas le MAC).
  `JsonApi::secureCompare` existe et peut être réutilisé. → ticket : comparaison constant-time
  du token RemoteUI.

## exprtk ASan (tracké T2.8)

- **stack-use-after-scope `exprtk.hpp:15688`** — dans l'opérateur d'égalité de chaînes
  `eq_op::process(const std::string&, const std::string&)`, déclenché par des conditions de règle
  du type `value == 'connected'`. C'est **interne à exprtk** ; notre `value_str`
  (`ExpressionEvaluator.cpp:134`) est vivant jusqu'à `expr.value()` (l.165).
  - **Version vendored = déjà la dernière upstream** (Author 1999-2024, version = décimales de *e*
    `2.71828…`). Un bump ne corrige **pas**.
  - **Impact prod : nul** (ASan-only ; builds release non concernés ; le résultat calculé est
    correct, suite non-ASan verte). Bloque seulement un `make check` **sous ASan**.
  - **Reco T2.8** : suppression ASan ciblée sur la frame exprtk, **ou**
    `__attribute__((no_sanitize("address")))` sur `evaluateExpressionBool`, **ou** compiler la TU
    exprtk en `-fno-sanitize=address`. Priorité basse, non bloquant.

## Qualité / design

- **[FOOTGUN] `Params::operator[]` retourne par VALEUR** — `params["k"] = v` compile et **no-op
  silencieux** (assigne à un temporaire). T1.11 a corrigé l'instance vivante (battery
  `last_battery_notif_time`, throttle qui ne marchait jamais) via `Params::Add`. D'autres sites
  `params[...] = ...` peuvent cacher le même bug. → sweep `\]\s*=` sur instances Params.

- **[DESIGN] `IOBase::get_params()` retourne un `Params&` mutable** — contourne la garde
  d'immutabilité de `set_param("id")` : un appelant pourrait réécrire "id" sans passer par
  `renameId()` (pas de rehash de la map id→IO). Aucun appelant de ce type aujourd'hui
  (vérifié grep). → envisager const ref ou mutateur dédié.

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
