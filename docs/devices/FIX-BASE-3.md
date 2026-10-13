# FIX-BASE-3 — `MqttCtrl` garde les callbacks d'un IO détruit (calaos_base)

## Objectif
Après `deleteIO` ou un rechargement, le message suivant sur le topic d'un IO Mqtt détruit appelait son callback (topic_sub et topics de statut capturent l'IO) : heap-use-after-free. Relevé par la revue de CLIM-2.

## Livré
PR calaos/calaos_base#193.
- `MqttCtrl::subscribeTopic()` rend un `MqttCtrl::Subscription` (déplaçable, non copiable) : le détruire ou `reset()` retire le callback ; jeton faible sur le contrôleur, donc sans effet si le `MqttCtrl` est détruit avant. `subscribeStatusTopics()` rend les jetons des six topics de statut.
- `MqttIOBase` garde ses jetons (topic_sub + statut) en membre : ils meurent avec l'IO. Plusieurs IOs sur un même topic gardent chacun le leur ; la souscription broker ne change pas (le sidecar s'abonne à `#`).
- Distribution : ids relus un à un et callback appelé par copie, un callback peut donc supprimer un autre abonné du même message, ou le sien.
- `~MqttCtrl` détruit son `ExternProcServer` (ses handlers capturent le contrôleur).
- `subscriptionCount(topic)` pour les tests.
- Revue CLIM-2 : garde nullptr dans `ClimateBase::writeRangeBound` ; ProxyClimate logue « has a command_io that is not an output » quand le `command_io` existe mais est une entrée.

## Mesuré
- `make check` 189 tests (186 PASS, 3 SKIP). `MqttSubscriptionLifetime_test` : 6 cas. `ClimateBase_test` : 25 cas (+1). `ProxyClimate_test` : 29 cas (nouveau device `mm_ci`).
- Avant correctif : la sonde compte un appel après `delete` (rouge dans tout build) ; sous ASan, heap-use-after-free dans `MqttInputString` (topic_sub) et dans `IOBase::setStatusInfo` (callback de statut).
- Après correctif, sous `--enable-asan` : MqttSubscriptionLifetime, MqttOutputString, ProxyClimateMqtt, MqttConfigTransport, MqttSidecarConfigWait, MqttIO, ClimateBase, ProxyClimate propres.
- Porte de couverture différentielle verte (après un premier refus : 5 fins de lambda de statut jamais atteintes, chaque topic de statut est maintenant piloté sur un IO vivant). Format vert, CI verte.

## Contre-mutation
CM0 : 0 rouge (build normal et ASan). Chaque mutant est construit à partir des objets propres, avec un oracle à l'exécution ([ PASSED ]/[ FAILED ], crash, rapport ASan). 12 mutants, tous tués :
- M1 `reset()` qui n'efface pas → ADestroyedIoIsNoLongerCalled, ASubscriptionLastsAsLongAsItsHandle, ACallbackCanDropSubscriptionsOfTheMessageItHandles (FAILED) ; ADeletedIoIsNotReadByTheNextMessage, StatusTopicsOfADeletedIoAreDropped (crash, build normal).
- M2 jeton batterie fuité dans `subscribeStatusTopics` → StatusTopicsOfADeletedIoAreDropped.
- M12 jetons de statut fuités dans `MqttIOBase` → StatusTopicsOfADeletedIoAreDropped.
- M3 `active()` qui ignore la mort du contrôleur → ASubscriptionOutlivingItsControllerIsHarmless (crash ; ASan heap-use-after-free).
- M4 distribution sans relecture des ids → ACallbackCanDropSubscriptionsOfTheMessageItHandles.
- M5 callback appelé sans copie → ACallbackCanDropSubscriptionsOfTheMessageItHandles (ASan heap-use-after-free).
- M6 `~MqttCtrl` sans `delete process` → ASubscriptionOutlivingItsControllerIsHarmless.
- M7 déplacement qui copie (source encore active) → ASubscriptionLastsAsLongAsItsHandle.
- M8 affectation par déplacement sans `reset()` → ASubscriptionLastsAsLongAsItsHandle.
- M11 topic vide enregistré → ASubscriptionLastsAsLongAsItsHandle.
- M9 garde nullptr de `writeRangeBound` retirée → ClimateBaseTest.ARangeBoundWithoutAnIoIsNotWritten (crash).
- M10 message « no command_io » systématique → ProxyClimateTest.ConfigurationMistakesAreLoggedAtLoad.

Mutant équivalent écarté : un déplacement qui ne remet pas la source à zéro alors que le `weak_ptr` est déplacé. La source est déjà inactive, donc ce mutant est remplacé par M7.

## Reste ouvert
- Un `MqttCtrl` détruit depuis l'un de ses propres callbacks n'est pas couvert (aucun appelant aujourd'hui, `MqttBrokersList` ne libère jamais ses contrôleurs).
- Le jeton de l'IO est libéré dans `~MqttIOBase`, après les destructeurs dérivés : sans danger tant qu'aucun destructeur ne fait tourner la boucle (invariant mesuré par IoLifetimeSourceGuardTest).
