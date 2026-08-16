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
  (plus de subprocess) ; `docs/13_utility_lib.md` référence encore SHA1.{cpp,h} supprimés (T2.3).

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
