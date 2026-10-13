# INST-6 — MqttOutputString, value_min/value_max, catalogue régénéré (calaos_installer)

## Objectif
Enregistrer `MqttOutputString`, exposer `value_min`/`value_max` (décision 14), cesser d'écrire et
retirer à la lecture les `min`/`max` hérités ; régénérer le catalogue IODoc.

## Livré
PR calaos/calaos_installer#60.
- `MqttOutputString` dans l'IOFactory (`gui_type` « string_out ») et le menu Mqtt.
- `IOFactory::CreateIO` retire `min`/`max` sur exactement la liste du serveur (17 types), hors
  InputTime/InputTimer (minutes) ; les dialogues Wago/Web ne les écrivent plus.
- `io_doc.json`/`.md` (en, fr) régénérés depuis calaos_base master, tels que générés :
  `MqttOutputString`, `value_min`/`value_max`, DMX 612→355 (garde T3.247), OLA 9999→512, `insecure`,
  `debounce`, `autoscenario_uid` ; Gadspot et MySensors retirés (pilotes supprimés du serveur).
  `generate_iodoc.sh` corrigé.
- Éditeur générique : un param numérique optionnel sans défaut est un champ texte (vide = absent),
  nombre invalide et `value_min > value_max` refusés ; bornes posables sur InternalInt et toutes les
  sorties analogiques. Formulaire Wago/Web : champs masqués ignorés, saisie validée.
- Traductions fr à jour (y compris les chaînes d'INST-2).

## Mesuré
tst_analogbounds 28, tst_iodoccatalogue 13+ ; 10 suites vertes ; CI 4/4. Revue en deux tours
(bloquant : catalogue non régénéré, bornes inaccessibles hors création Wago/Web), corrigé.

## Contre-mutation
11 mutants, tous rouges (suppression des `min`/`max` absente, InputTimer dans la liste, type non
enregistré, bornes non écrites, éditeur float non optionnel, effacement sans retrait, champ caché
pris en compte, min > max accepté, saisie invalide acceptée…).

## Reste ouvert
L'installateur propose encore de créer des types retirés du serveur → FIX-INST-8.
