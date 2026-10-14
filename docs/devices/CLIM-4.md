# CLIM-4 — `InternalThermostat` : sûreté (calaos_base)

## Objectif
Spec §8.2 : sorties OFF sans lecture fraîche de la sonde ou en cas de risque de condensation, `min_off_time` appliqué au démarrage. ProxyClimate n'envoie plus `remote_temp` depuis une source muette.

## Livré
PR calaos/calaos_base#195.

**Fraîcheur**
- `IOBase::lastReading()` date la dernière valeur reçue depuis le démarrage.
- `ClimateBase::freshness()` renvoie Fresh, NotRead, Silent ou Disconnected : `connected=false` d'abord, puis une lecture depuis le démarrage, la dernière datant de moins de `sensor_timeout` (défaut 9000 s).
- Ne sont pas des lectures : le cache d'état, et un message MQTT retenu. Le sidecar transmet `"retain": true`, `MqttCtrl::replayingRetained()` le signale, et `IOBase::replayed` empêche `EmitSignalIO()` de dater la valeur.
- Une valeur répétée est datée par le driver, là où la lecture arrive : MQTT, Wago, OneWire, KNX et Web 2xx. Pas dans InputAnalog : son poll relit le cache du driver.
- `datesEveryReading()` vaut faux par défaut ; une sonde qui ne date que ses changements est journalisée au chargement (Web sur fichier local, IO inconnu).
- Une sonde jamais lue, ou plus vieille que la moitié de `sensor_timeout`, reçoit `requestReading()`. Pour KNX, c'est une lecture de groupe, une seule en vol par IO (réponse ou 10 s). Le thermostat vérifie à chaque tour (min(`sensor_timeout`/4, 60) s), le proxy à chaque keep-alive.

**Thermostat** (coupures immédiates, `min_off_time` au redémarrage)
- Sorties OFF tant que `current_temp` n'est pas fraîche ; une erreur par épisode (silence ou déconnexion).
- Point de rosée, `emitter=floor` : `output_cool` est coupé si l'humidité vaut au moins `humidity_max` (défaut 65), n'a pas été lue, est muette ou déconnectée.
- Plancher sans lien `humidity` : pas de `cool`, d'`auto` ni de `target_temp_range` (refusés par `isWritable()`), erreur au chargement ; `output_cool` reste tenu OFF.
- Le premier tour de boucle compte comme un passage à OFF. Après un démarrage, rien ne chauffe avant la première lecture, plus `min_off_time` ; avec une sonde Zigbee non retenue qui émet une fois par heure, l'attente peut aller jusqu'à 1 h (documenté).

- Les entrées MQTT, OneWire, KNX et Web comparent la valeur **convertie** (`calc_expr`, `coeff`, `offset`) avant de la signaler : avant, le poll périodique re-signalait la même valeur à chaque tour et la datait comme lecture, une sonde rejouée ou muette restait fraîche pour toujours (revue).
- IODocJson_test ne parcourt plus un temporaire détruit (rouge sous ASan sur master).

**ProxyClimate** : pas de relais depuis une source non fraîche ; reprise à la lecture suivante.

**IODoc** (API DOC-1) : `sensor_timeout` (famille climate), `emitter`, `humidity_max`, rôle `humidity` requis si `emitter=floor` et `output_cool` lié ; descriptions de `current_temp` et `remote_temp_source` mises à jour. docs/01_core_data_model.md mis à jour.

**Écarts avec la spec, acceptés** (§8.2 mise à jour) : « aucune lecture » plutôt que « pas de changement », défaut 9000 s, humidité muette qui coupe le froid, même param pour le proxy, lecture exigée depuis le démarrage.

## Mesuré
- `make check` : 199 tests (196 PASS, 3 SKIP).
- Nouveaux binaires : InternalThermostatSafety_test (14 cas), OneWireReading_test (1), KnxThermostatRead_test (1). Ajouts : ProxyClimate +4, ProxyClimateMqtt +2, MqttSidecarConfigWait +1 (sidecar réel, faux broker), MqttWire +1, KnxIo +1, WebIO +1, WagoReadReply +2.
- Couverture différentielle (CI) : 488 lignes ajoutées sous src/, 242 couvertes, 0 non couverte, 7 exclues (gestionnaire du sidecar, non construit en CI).
- ASan : 0 erreur sur 11 binaires ; seule la fuite préexistante du singleton WagoMap. CI verte.

## Contre-mutation
CM0 : 0 rouge. 51 mutants, 51 tués (objets recompilés propres, oracle [ FAILED ]).
- CM1 `known` = `hasValue()` → AValueFromTheStateCacheIsNotAReading, AStaleSensor×2, ARepeatedReadingKeepsTheSensorFresh, ASensorThatSaysItIsDisconnectedIsNotFresh
- CM2 toujours Fresh → AStaleSensor×2, ARepeatedReading…, ASilentHumiditySensorCutsTheCooling, ASilentSourceIsNoLongerRelayed
- CM3 `EmitSignalIO` sans horodatage → 50 tests
- CM4 vérification toutes les 60 s, CM5 sans timer de vérification → AStaleSensor×2, ARepeatedReadingKeepsTheSensorFresh, AnAgingSensorIsReadOneRequestAtATime
- CM6 erreur sonde répétée → AStaleSensor×2
- CM7 `>` au lieu de `>=` → HumidityAtTheMaximumCutsTheCoolingAtOnce
- CM8 hook qui autorise tout, CM9 humidité non abonnée → 4 cas point de rosée
- CM10 humidité périmée ignorée, CM11 humidité sur `hasValue`, CM23 journal humidité répété → ASilentHumiditySensorCutsTheCooling
- CM12 plancher refroidit sans humidité, CM22 tout emitter vaut floor → FloorCoolingWithoutHumidity×2
- CM13 sans verrou de démarrage → TheFirstSwitchOnWaitsForMinOffTimeFromTheStart, InvalidParamsAreLoggedAndReplaced
- CM14 proxy sur `hasValue` → ADisconnectedSourceIsNotRelayed, ASilentSourceIsNoLongerRelayed ; CM15 erreur proxy répétée → ASilentSourceIsNoLongerRelayed
- CM16 MQTT erreur de parse datée → ARepeatedReadingIsStillAReading ; CM17 souscription sur `readValue` → ARepeatedReadingIsStillAReading, ARetainedMessageIsNotAReading
- CM18 WITemp, CM19 WIAnalog sans marquage → ARepeatedTemperature/AnalogReplyIsStillAReading
- CM20 `sensor_timeout=0` accepté, CM33 source du proxy non vérifiée → ConfigurationMistakesAreLoggedAtLoad
- CM21 sans erreur plancher sans humidité → FloorCoolingWithoutHumidityIsRefusedAtLoad
- CM24 OWTemp sans marquage, CM25 sans mémoriser le compteur → ARepeatedScanIsStillAReading
- CM26 KNX télégramme non daté → ARepeatedTelegramIsStillAReading, AnAgingSensorIsReadOneRequestAtATime
- CM27 toute réponse HTTP prise pour un téléchargement, CM28 Web marque hors téléchargement → ARepeatedDownloadIsStillAReading
- CM29 pas d'avertissement « que des changements », CM32 humidité non vérifiée au chargement → ASensorThatOnlyReportsChangesIsLoggedAtLoad
- CM30 fichier local déclaré daté → AllWebTypesInstantiateFromIoXml
- CM31 plage du plancher offerte → FloorCoolingWithoutHumidityOffersNoRange
- CM34 `connected=false` ignoré → ASensorThatSaysItIsDisconnectedIsNotFresh, ADisconnectedSourceIsNotRelayed
- CM35 sidecar sans retain, CM39 MqttWire sans retain → ARetainedMessageIsSaidToBeOne
- CM36 MqttCtrl ne signale jamais un retenu, CM37 retenu daté par l'entrée, CM38 `replayed` ignoré → ARetainedMessageIsNotAReading
- CM40 aucune demande de lecture, CM41 demande seulement si jamais lue → AnAgingSourceIsAskedForAReading, AnAgingSensorIsReadOneRequestAtATime
- CM42 plusieurs lectures KNX en vol, CM43 réponse qui ne libère pas, CM44 le thermostat ne demande pas, CM45 vérification à `sensor_timeout` → AnAgingSensorIsReadOneRequestAtATime
- CM46 le proxy ne demande pas → AnAgingSourceIsAskedForAReading
- CM47/CM48 MqttInputTemp/Analog comparent la valeur brute → ARereadOfAConvertedValueIsNotAReading
- CM49 OWTemp compare la valeur brute → OneWireReadingTest.AConvertedValueReadAgainIsNotAReading
- CM50 Web compare la valeur brute → WebIOTest.AConvertedValueReadAgainIsNotAReading
- CM51 KNX compare la valeur brute → KnxIoTest.AConvertedValueReadAgainIsNotAReading

## Reste ouvert
- WebInput sur fichier local : impossible de savoir si le fichier est encore réécrit ; signalé au chargement.
- La CI ne construit pas `calaos_mqtt` (pas de libmosquitto) : le chemin retain du sidecar n'est testé qu'en local.
- WITemp/WIAnalog comparent encore la valeur brute à la valeur convertie : avec une conversion, les règles sont re-signalées à chaque réponse Modbus (la fraîcheur n'est pas en cause). Repris dans FIX-BASE-5.
