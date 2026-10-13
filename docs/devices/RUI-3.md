# RUI-3 — Widget Climate du panneau mural (calaos_remote_ui)

## Objectif
Widget du panneau piloté par les capacités (spec §5.2, §5.3, §6, §9.3), mêmes règles que les
widgets web et mobile ; un device supprimé ou reconfiguré ne reste plus affiché jusqu'au reboot du
panneau (revue RUI-1).

## Livré
PR calaos/calaos_remote_ui#5.
- `config_update` : le lot `io_items` remplace l'ensemble stocké (IOs absents retirés, pas de
  clignotement des autres) ; le lot `io_states` qui suit fait foi, une entrée sans `device` efface
  le device stocké (`io_changed` le garde).
- Widget « Climate » (`registerWidgetAnySize`) : en 1x1 consigne ou plage (−/+ déplacent les deux
  poignées), température, action, bouton de mode cyclique ; en plus grand, un bouton par mode, −/+
  par poignée, fan et swing ; lignes retirées quand elles ne tiennent pas. Boutons 52 px, puces
  48 px.
- Règles communes : off grise, `fan_only` masque la consigne, plage seule dans tous les modes, plage
  à demi connue non éditable, poignées envoyées ensemble et `min_range` respecté, une commande par
  geste, refus (`msg_id` `ui_cmd:<io>`) qui revient en arrière aussitôt, drapeau « en vol » pour ne
  jamais perdre un retour à la valeur serveur, envoi à la destruction.
- `ClimateLogic` sans LVGL ; `widget_tests` rend les widgets en mémoire.

## Mesuré
200 tests hôte ; chaque cas tient en 1x1, 2x1, 1x2, 2x2, 3x1 sur cellules 240×240 et 213×266 ;
ESP32 waveshare-86-panel +42 224 o (+0,48 %), 44 % libres ; simulateur SDL contre un faux serveur.
Revue en deux tours (commande perdue en revenant à la valeur serveur, cibles tactiles de 44 px),
corrigés et vérifiés par sondes et captures.

## Contre-mutation
20 mutants tués (prune retiré, règle plage seule, `min_range`, borne seule envoyée, coalescence,
refus ignoré, `success` ignoré, `fan_only`, off, mode en JSON, écho jamais purgé, poignée figée,
`msg_id` absent, IO supprimé gardé, `target_temp` sur plage seule, drapeau en vol ×3, lot d'états
sans device ×2).

## Reste ouvert
Un libellé de mode long serait tronqué dans la puce 1x1 ; libellés en anglais seulement ;
`build-flash-package.sh` échoue dans l'image de dev (zip absent).
