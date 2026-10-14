# FIX-BASE-5 — Restes de l'audit FIX-BASE-4 (calaos_base)

## Objectif
Corriger les défauts préexistants relevés par l'audit FIX-BASE-4 : ouverture de l'historique, requêtes HTTP en vol, ordre de destruction des contrôleurs, minuterie de calibration, callbacks de WebCtrl, fuite LSan de Wago, puis Squeezebox et les entrées analogiques Wago converties.

## Livré
PR calaos/calaos_base#198.
- HistLogger : `sqlite::database` est construit dans un try. Si l'ouverture échoue (dossier absent ou en lecture seule, disque plein), le worker journalise, ignore les écritures et répond « history is disabled » à chaque lecteur (`refuseAll()`). Plus de `std::terminate`. Les contournements `log_history="false"` des tests KNX et Wago sont retirés.
- Requêtes en vol : `LifetimeTag::token()` expose le jeton faible de l'IO. La lecture périodique de Hue le vérifie ; les 4 sauts de Synology passent par un seul `httpGet()` qui le vérifie.
- ReolinkCtrl et AVRManager deviennent des singletons sur le tas, jamais détruits (destructeur `= delete`). Les construire avant ListeRoom lancerait le sidecar Reolink sur tous les serveurs.
- OutputShutterSmart : `timer_calib` devient un `unique_ptr` (le vrai défaut était le destructeur).
- WebCtrl : `notifyDownloaded(bool)` prend les ids, relit chaque entrée et appelle une copie, comme MqttCtrl (#193).
- Wago, fuite LSan : elle venait du `WagoMapSignals` de chaque requête, pointeur brut libéré seulement sur la réponse Modbus (fuite réelle en production). Passage à un `shared_ptr`.
- Squeezebox : le destructeur vide les listeners des deux connexions LMS et les ferme quel que soit leur état.
- WagoMap : `~WagoMap()` vide les listeners de la socket UDP avant de la fermer.
- WIAnalog et WITemp : le registre est converti avant d'être comparé (patron de CLIM-4) ; chaque réponse date toujours une lecture.

## Mesuré
- Nouveaux binaires : HistLoggerOpenFailure_test (2), InFlightDownload_test (4), ControllerTeardownOrder_test (2), WagoCommandRelease_test (4), SqueezeboxLifetime_test (2). Cas ajoutés : ShutterImpulse_test (+2), WebIO_test (+4), WagoIoLifetime_test (+1). Tous rouges sur master (terminate, heap-use-after-free, slots jamais libérés, valeur re-signalée).
- `make check` (couverture) : 204 tests, 201 PASS, 3 SKIP.
- ASan avec LSan (`detect_leaks=1`) : 27 binaires touchés ou voisins sans rapport ; les binaires Wago étaient rouges sous LSan avant.
- Couverture différentielle : 71/71. Format OK. CI verte.

## Contre-mutation
CM0 : 0 rouge. Build ASan. 18 mutants, tous tués :
- M1 catch de l'ouverture qui relance, M2 `refuseAll` sans `send()` → HistLoggerOpenFailureTest.* (2).
- M3 Hue sans test du jeton → InFlightDownloadTest.ADeletedHueLightIgnoresThePollItHadInFlight (ASan).
- M4 `httpGet` Synology sans jeton → InFlightDownloadTest.ADeletedSynologyCameraStopsItsChainAtEveryHop (ASan).
- M5 ReolinkCtrl statique local → ControllerTeardownOrderTest.AReolinkSwitchLeftAtExitFindsItsController.
- M6 AVRManager statique avec destructeur → ControllerTeardownOrderTest.AnAvReceiverLeftAtExitFindsItsManager.
- M7 `timer_calib.release()` → SmartShutterImpulseTest.ADeletedSmartShutterLeavesNoCalibrationTimerArmed ; M8 sans `reset()` → .ACalibrationThatEndsFreesItsTimer.
- M9 appel sans copie → WebIOTest.ACallbackThatRegistersAnotherOneKeepsItsOwnState (ASan) ; M10 `continue`→`break` → .ACallbackRemovedByAnEarlierOneIsNotCalled ; M11 boucle par index → .AnHttpDownloadReachesTheCallbackAfterARemovedOne, .ARepeatedDownloadIsStillAReading.
- M12 `shared_ptr` à deleter vide → WagoCommandReleaseTest (3 cas) ; M13 `~WagoMap` sans `clear()` → .ADestroyedMapIsNotCalledByItsClosingSocket (ASan).
- M14 Squeezebox sans `clear()`, M15 fermeture seulement si `active()` → SqueezeboxLifetimeTest.APlayerDeletedWhileConnectingIsNotCalledByItsConnections.
- M16/M17 WIAnalog/WITemp comparent le brut, M18 WITemp sans `markReading()` → WagoIoLifetimeTest.AConvertedValueReadAgainIsNotSignalledAgain.

## Reste ouvert
- `RemoteUIWebSocketHandler::authenticated_remote_ui` reste un pointeur brut (latent : `deleteIO` ne touche pas les RemoteUI).
- `WagoMap::udpProcessError` : recréer la socket serait nuisible (bind sur l'adresse de l'automate, EADDRNOTAVAIL). À trancher sur matériel : binder sur INADDR_ANY, ou ne plus binder.
- HistLogger : un échec SQL pendant une lecture ne répond toujours pas au lecteur (préexistant).
- `SidecarLoopExitStatus_test` incompatible avec ASan (shim LD_PRELOAD).
- Synology : si la caméra est supprimée en cours de chaîne, le `cb` du demandeur de snapshot n'est jamais appelé (le client HTTP attend son timeout).
