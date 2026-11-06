# FIX-BASE-4 — Contrôleurs qui gardent le slot d'un IO détruit (calaos_base)

## Objectif
Un contrôleur partagé par les IOs d'un hôte leur survit. Quand il garde un callback qui capture un IO, l'événement qui suit la destruction de cet IO (deleteIO, device écarté au chargement, rechargement) appelle de la mémoire libérée. Relevé par CLIM-4 sur KNX. Ticket : corriger KNX sur le patron de FIX-BASE-3, auditer les autres familles, corriger chaque cas confirmé.

## Livré
PR calaos/calaos_base#197.
- `ScopedConnection` (src/lib) : une `sigc::connection` coupée à la destruction de son porteur, réaffectable, non copiable. La coupure est sans effet si le signal est déjà détruit, donc sûre si le contrôleur meurt avant l'IO (destruction des statiques à l'arrêt).
- KNX : `KNXIo` garde sa connexion à `KNXCtrl::valueChanged` dans un `ScopedConnection` membre.
- Wago : WIAnalog, WITemp, WOAnalog, WODigital, WODali, WODaliRVB et WOVoletBase dérivent de `sigc::trackable`. Le handler de reconnexion devient un `sigc::mem_fun` (`readValue`, `readBack`) au lieu d'une lambda. Le trackable coupe le slot de `onWagoConnected` et les slots de réponse des requêtes Modbus/DALI en vol.
- Web : `WebCtrl::Add` rend un id et `Del(id)` le reprend. L'input garde le `WebCtrl` sur lequel il s'est inscrit. Avant, `Del(path)` retirait l'autre input d'un même path (et sautait l'entrée qui suivait chaque match), et un `set_param` de url/path laissait le callback derrière lui.
- Roon : `RoonCtrl::subscribeZone` prend et garde un `sigc::slot` (le `std::function` construit depuis `mem_fun` perdait le lien trackable de RoonPlayer). `processNewMessage` devient public pour les tests.

## Mesuré
- `make check` : 195 tests, 192 PASS, 3 SKIP.
- Nouveaux cas : KnxIo_test 5 (+3), WagoIoLifetime_test 5 (nouveau), WebIO_test 5 (+3), RoonZoneSubscription_test 2 (nouveau), ScopedConnection_test 3 (nouveau).
- Avant correctif : KNX rouge dans tout build (compte des slots, puis SIGSEGV) ; Wago rouge dans tout build pour la reconnexion, heap-use-after-free sous ASan dans le lambda de reconnexion, `WIAnalog::WagoReadCallback`, `WODigital::WagoWriteCallback` et `WODali::WagoUDPCommand_cb` ; Web heap-use-after-free dans le lambda de `webRegisterPolling` ; Roon heap-use-after-free sous ASan.
- Sous `--enable-asan`, `make check` complet : aucun rapport AddressSanitizer (seul SidecarLoopExitStatus_test échoue, son shim LD_PRELOAD est refusé par le runtime ASan ; non touché).
- Couverture différentielle : 34/34 lignes exécutables couvertes. Format vert. CI verte.

## Contre-mutation
CM0 : 0 rouge. Build ASan, oracle à l'exécution. 15 mutants, tous tués :
- M1 `~ScopedConnection` sans disconnect → ScopedConnectionTest.ADestroyedHolderCutsItsSlot ; KnxIoTest.ADeletedIoIsNoLongerCalledByTheNextTelegram (ASan).
- M2 `operator=` qui ne coupe pas l'ancienne connexion → ScopedConnectionTest.ReassigningCutsThePreviousSlot.
- M3 connexion KNX non gardée → KnxIoTest.ADeletedIoIsNoLongerCalledByTheNextTelegram.
- M4/M5/M9/M10 WIAnalog/WODigital/WOAnalog/WITemp sans trackable, M15 reconnexion WIAnalog remise en lambda → WagoIoLifetimeTest.ADeletedWagoIoIsNotCalledWhenThePlcReconnects (ASan).
- M6/M7 WODali/WODaliRVB sans trackable → WagoIoLifetimeTest.ADeletedDaliBallastIgnoresTheReplyToItsPendingRead (ASan).
- M8 WOVoletBase sans trackable → WagoIoLifetimeTest.ADeletedWagoOutputIgnoresTheReplyToItsPendingWrite (ASan).
- M11 `WebCtrl::Del` sans effet, M13 id constant → WebIOTest.DeletingAWebInputKeepsTheOtherInputOfItsPathPolled (ASan).
- M12 `Del` sur le contrôleur résolu depuis les params courants → WebIOTest.AWebInputWhoseUrlChangedIsStillUnregistered (ASan).
- M14 paramètre de `subscribeZone` remis en `std::function` → RoonZoneSubscriptionTest.ADeletedSubscriberIsNotCalledByTheNextState (ASan).

Mutant équivalent écarté : M16, valeur de la map `subscribeCb` en `std::function` (enveloppe une copie du `sigc::slot`, toujours suivie).

## Audit des autres contrôleurs
À l'exécution, `deleteIO` ne touche que les IOs internes et les scénarios, et il n'y a pas de rechargement à chaud. Un IO matériel n'est détruit qu'au chargement (device écarté, id dupliqué) ou à l'arrêt.

Corrigés : KNX, Wago (reconnexion et requêtes en vol), Web, Roon.

Vérifiés sains : Wago WIDigitalBase (trackable, `signal_wago` déconnecté) ; OneWire (pas d'abonné à `valueChanged`, polling) ; OLA (pas de callback) ; Gpio (contrôleur possédé, debounce par jeton) ; LAN (handles possédés, lambdas sans capture) ; Reolink (`weakAlive`, désinscription) ; RemoteUI (envoi par id) ; IOs internes (Timers possédés, `ioAlive`, `IOChangeHub::Subscription`, `detachIOFromRules`) ; AVReceiver (trackable + mem_fun), AVRRose (`aliveTag`) ; IPCam (downloader possédé) ; EventManager (connexions coupées) ; Rules (`aliveToken`/`alive`, `~Download`) ; DataLogger et NotifManager (leur propre singleton).

## Reste ouvert
Repris dans FIX-BASE-5.
- Requêtes HTTP en vol qui capturent l'IO (UrlDownloader autodelete) : HueOutputLightRGB.cpp:52 et la chaîne SynoSurveillanceStation. Correctif : jeton faible vérifié à chaque saut.
- Ordre de destruction à l'arrêt : ReolinkCtrl et AVRManager, statiques construites après ListeRoom, détruites avant elle.
- `HistLogger` : `sqlite::database db(dbname)` hors du try (HistLogger.cpp:190), un cantopen fait std::terminate.
- OutputShutterSmart : `timer_calib` réalloué sans libérer le précédent, non libéré au destructeur.
- Squeezebox : le destructeur ne ferme que les handles `active()`. `RemoteUIWebSocketHandler::authenticated_remote_ui` en pointeur brut. `WagoMap::udpProcessError` ne recrée jamais la socket UDP.
