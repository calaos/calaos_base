# FIX-BASE-7 — Condition de sortie sur un device (calaos_base)

## Objectif
Une règle `<calaos:condition type="output"><calaos:output id="plancher_cuisine" oper="==" val="heat"/>` ne se déclenchait jamais (validation de DOC-7 sur un vrai serveur). C'est la seule condition que l'installer permet de construire sur un device ; la spec §5.1 promet `salon_ac == off`.

## Livré
PR calaos/calaos_base#211.
- `IOBase::check_condition_value` compare par défaut l'état string à la valeur (==, !=), comme ConditionStd : device (état = mode), InternalString, MqttOutputString, AVReceiver. Volets, dimmers et RGB gardent leur surcharge.
- `OutputString` (donc WebOutputString) expose sa valeur.
- `hasComparableState()` : AudioPlayer, IPCam et RemoteUI n'ont pas d'état comparable et restent jamais vrais (sans quoi `!=` serait devenu toujours vrai).
- Avertissement au chargement, une fois : opérateur autre que ==/!= sur une string, IO sans état comparable, type non évaluable ; `val="changed"` exclu.

## Mesuré
Tests rouges d'abord : ClimateBase_test (parité avec ConditionStd, règle XML de l'installer déclenchée au changement de mode, pas de redéclenchement sur un changement de consigne), ConditionEval_test, nouveau ConditionOutputState_test (OutputString, classes sans état, `changed`, volets et dimmers sans avertissement). CI verte. Revue opus : retours traités.

## Contre-mutation
CM0 : 0 rouge. 13 mutants tués (M1–M6, N1–N7).

## Reste ouvert
- État AudioPlayer (onplay/onpause) non implémenté : `lecteur == onplay` reste impossible, avec avertissement.
- `AVReceiver == 3` se redéclenche à chaque changement de volume (sémantique normale du trigger).
- run-python-tests.sh rouge en local (paquet `mcp` du conteneur de dev).
