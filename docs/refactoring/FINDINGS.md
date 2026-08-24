# Findings bonus — backlog

> Découvertes faites **en marge** des tickets (hors périmètre du ticket en cours, donc **non
> corrigées**). Candidates à de futurs tickets. Sorti du job tmp éphémère → durable + partagé.

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

  **Le vrai correctif, et il est gratuit — suite à ouvrir hors de ce dépôt (calaos-os)** : poser
  **`listen_address = 127.0.0.1`** dans la configuration de calaos-os. L'option **existe déjà** et
  est documentée ; seul haproxy pourrait alors joindre le port, ce qui rend la confiance en
  `X-Forwarded-For` **saine** au lieu d'hypothétique. À défaut, une liste de proxys de confiance
  côté `calaos_server` (ou la normalisation de l'en-tête par `McpProxyHandler`, cf. T1.8).

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
- **[FOOTGUN] `Utils::from_string("")` retourne true** avec dest zéro-initialisée
  (`iss.eof()` vrai sur entrée vide) — `src/lib/Utils.h:308-315`. Chaque appelant doit se
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
   opposables.** Rien n'appelle `Squeezebox`, `RoonPlayer` ni `MqttCtrl` dans la suite — ces
   objets ne sont même pas liés au binaire. Les **8** payloads audio et `io_status_changed` sont
   donc épinglés depuis des events **fabriqués à la main**, ce qui ne prouve rien de la forme que
   la production émet réellement. Quatre payloads audio du golden ont d'ailleurs été **corrigés**
   dans ce ticket parce qu'ils gelaient des formes que la production n'émet pas : les corriger ne
   les rend **pas** opposables pour autant ; ce que ça achète, c'est que le golden **cesse
   d'affirmer une forme fausse**. Ce qui **est** opposable : l'enveloppe, la numérotation,
   l'encodage (UTF-8 accentué) et la stringification pour les **19** types atteignables, et les
   **formes de payload** pour les **7** types réellement déclenchés par du code de production
   traversé par la suite.

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

- **[BUG, ⭐ visible par tous les utilisateurs] La syntaxe d'index de tableau publiée par l'ioDoc
  ne fonctionne pas.** Les descriptions des paramètres `path` de MQTT (`MqttCtrl.cpp:415`, et les
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
- **[BUG] `RoonPlayer` : `port` est déclaré obligatoire sans défaut, et `--port 0` part au
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
- **[FIABILITÉ] `RoonPlayer.cpp:39-50` — le respawn perd `--host` et `--port`.** Le premier
  `startProcess(exe, "roon", args)` (`:50`) passe `--host <ip> --port <n>` construits depuis la
  configuration, mais le handler `processExited` (`:43`) rappelle `startProcess(exe, "roon")`
  **sans args**. Après le premier redémarrage, le sous-processus retombe donc sur la découverte
  automatique `RoonDiscovery` (`ExternProcRoon_main.py:75-83`) au lieu du core configuré — et se
  connecte potentiellement au mauvais core, ou à aucun. → mini-ticket : capturer `args` dans le
  lambda.
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
- **[COMPORTEMENT, plus grave qu'il n'y paraît] La décision de mise à jour OTA est une *égalité*
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

- **[API, conséquence identifiée] `setIOParam` et `waitForIO` déclarent `return 1` sans rien
  empiler** sur leur chemin de succès (`ScriptBindings.cpp:293` et `:333` ; corps vérifiés
  `:254-292`, `:300-331`). Ce n'est pas « une valeur indéterminée » : `Lunar::thunk` retire
  `self` puis laisse **les arguments de l'appel** sur la pile avant d'invoquer la méthode
  (`Lunar.h:132-136`), et `return 1` demande à Lua de prendre le **sommet de pile** comme unique
  résultat. Le script récupère donc, de façon parfaitement reproductible, **le dernier argument
  qu'il vient de passer** — `calaos:setIOParam(id, key, val)` renvoie `val`, et
  `calaos:waitForIO(id)` renvoie `id`. Un script qui teste ce retour croit lire un statut de
  succès et lit en réalité son propre argument, ce qui est **toujours vrai** pour une chaîne non
  vide. → soit `return 0`, soit empiler un vrai statut.
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
