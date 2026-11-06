# CORE-1 — Hook de changement typé et synchrone (calaos_base)

## Objectif
Un abonnement typé et synchrone aux changements d'un IO (spec §4.3), dont les écritures sont
sérialisées avec les actions de règles.

## Livré
PR calaos/calaos_base#180.
- `IOChangeHub` : singleton jamais détruit, pour qu'un handle libéré pendant la destruction des
  statiques trouve encore le hub. `subscribe(id, function<void(IOBase&)>)` renvoie une
  `Subscription` RAII déplaçable.
- Abonnement par id : il peut précéder l'IO, survit à sa suppression et suit un nouvel IO créé sous
  le même id. Un IO renommé perd ses abonnements (voulu : les devices stockent des ids).
- Diffusion depuis `ListeRule::executeTrigger()`, **après** les règles du même déclencheur. Les
  écritures faites depuis un callback passent par `pendingTriggers` (FIFO, sans récursion).
- Diffusion sur un instantané ; le callback lit l'état **courant** de l'IO.
- `ListeRoom::deleteIO()` refuse toute suppression pendant la marche des règles : le
  use-after-free trouvé en revue (règle A→B, un abonné de B supprime A) est désormais impossible.
- Doc : `docs/10_events_notifications.md`. La spec §4.3 est alignée.

## Mesuré
- `make check` : 177 tests, 174 PASS, 3 SKIP.
- ASan : aucune erreur (IOChangeHub 14/14, RuleLifecycle, ListeRoomOwnership, AutoScenarioRules,
  ScenarioInternalIoLifetime).
- Porte de couverture : 67 lignes couvertes, 0 non couverte, 1 exclue avec motif. CI entièrement
  verte.
- Revue en deux tours : un bloquant (contrat de suppression), corrigé et vérifié.

## Contre-mutation
| Mutant | Tests rouges |
|---|---|
| CM0, code intact | aucun |
| CM1 : `if (!s->active) continue` retiré | SubscribeAndUnsubscribeDuringDispatch |
| CM2 : pas d'instantané | SubscribeAndUnsubscribeDuringDispatch (SEGV ASan) |
| CM3 : callbacks avant les règles | CallbackWritingAnotherIoIsQueuedNotRecursive, RulesOfATriggerRunBeforeItsCallbacks |
| CM4 : le destructeur ne désabonne pas | SubscriberReceivesEveryEmissionWithTheIo, puis ASan |
| CM5 : entrée vide gardée | les 14 (invariant du TearDown) |
| CM6 : IO résolu une seule fois | IoRemovedThroughDeleteIoBypassSkipsLaterSubscribers |
| CM7 : diffusion hors de la marche des règles | heap-use-after-free ASan, plus trois tests d'ordre |
| CM8 : garde de `deleteIO` retirée | DeleteIoIsRefusedDuringDispatch, puis UAF ASan |

## Reste ouvert (pour CORE-2)
- La suppression en cascade d'un device doit passer par `deleteIO()` pour chaque IO possédé.
- Ajouter la même garde dans `delete_io()` quand `del == true` (tout chemin qui libère vraiment un
  IO), pas dans `RemoveIO()` (`del=false` sert à déplacer). Adapter `IOChangeHub_test.cpp:277`.
