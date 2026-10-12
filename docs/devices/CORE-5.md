# CORE-5 — `MqttOutputString` et bornes analogiques (calaos_base)

## Objectif
Ajouter `MqttOutputString` (publication d'une chaîne par le gabarit `data`) et des bornes sur la
famille `OutputAnalog` et sur `InternalInt`, pour borner les consignes (spec §12, §6.3), sans
régénérer `io_doc.json` (T3.247).

## Livré
PR calaos/calaos_base#179.
- `MqttOutputString` : publication sur `topic_pub` via `data`, relecture d'état via
  `topic_sub` + `path`. `MqttCtrl::renderData` échappe la valeur en JSON quand `data` commence par
  `{`, `[` ou `"` (sortie identique octet pour octet pour les utilisateurs existants, qui ne
  publient que des nombres).
- Bornes **`value_min` / `value_max`** (décision 14) sur OutputAnalog (Mqtt, Wago, Web, KNX) et
  InternalInt : appliquées avant coeff/calc_expr, après inc/dec et à la relecture du cache ;
  `value_min > value_max` journalisé, bornes ignorées.
- Les vieux `min`/`max` écrits par l'installateur ne bornaient rien : `dropLegacyBounds` les
  supprime à la construction des IOs analogiques (OutputAnalog, InputAnalog, InternalInt), avec un
  log par IO ; ils disparaissent d'`io.xml` à la sauvegarde suivante. Le `min` (minutes) de
  `InputTime`/`InputTimer` n'est pas touché.

## Mesuré
- `make check` : 179 tests, 176 PASS, 3 SKIP. CI entièrement verte, Diff coverage compris.
- `AnalogBounds_test` 18 cas, `MqttOutputString_test` 12 cas (stables sur 10 exécutions).
- Revue en deux tours : bloquant `min`/`max` historiques (tranché : décision 14) et cache non
  borné, tous deux résolus.

## Contre-mutation
| Mutant | Tests rouges |
|---|---|
| CM0 | aucun |
| `value_max` non appliqué | 9 cas AnalogBounds + MqttOutputAnalogIsClampedBeforeItIsPublished |
| `value_min > value_max` non détecté | MinGreaterThanMaxIgnoresBothBounds, InternalIntWithMinGreaterThanMaxIsUnbounded |
| pas d'échappement JSON | AJsonTemplatePublishesValidJson, JsonTemplatesGetAnEscapedValue, InvalidUtf8DoesNotThrow |
| InternalInt / OutputAnalog non bornés | tests InternalInt / 8 cas AnalogBounds + Mqtt |
| borne non numérique lue comme 0 | ANonNumericBoundIsIgnoredAndTheOtherOneKept |
| pas de relecture d'état | StateIsReadBackFromTopicSubThroughPath, WithoutPathTheRawPayloadIsTheState |
| cache non borné | InternalIntRestoredFromCacheIsClamped |
| `dropLegacyBounds` sans effet (revue) | les deux tests Legacy |

## Reste ouvert
- INST-6 doit cesser d'écrire `min`/`max` (sinon le log de suppression revient à chaque boot).
- `OutputString`/`WebOutputString` n'exposent pas `get_value_string`.
