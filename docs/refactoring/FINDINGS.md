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

- **[SÉCURITÉ/UAF, tous handlers WS] `JsonApi.cpp:450-537` `buildJsonState`** : les lambdas
  internes audio-player capturent `this`/`jio`/`jplayer` sans garde — UAF si le client se
  déconnecte pendant une requête audio, AVANT le completion guardé ajouté par T1.10 ; de plus
  `json_object_set(jio, id, jplayer)` fuit `jplayer` (set, pas set_new). Confirmé par revue.
  → ticket dédié (touche tous les WS handlers).
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
