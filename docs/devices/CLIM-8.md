# CLIM-8 — Double consigne émulée (calaos_base)

## Objectif
Décision 21 : une clim à consigne unique pilotée en auto par Calaos, avec des consignes basse et haute gardées par le device ; l'unité ne reçoit jamais `auto`.

## Livré
PR calaos/calaos_base#209.
- `ProxyClimate`, param `emulate_range` (défaut off) : `target_temp_low`/`_high` en `InternalInt` `save="true"` créés par le proxy au chargement, bornés comme `target_temp` ; capacité `target_temp_range` et `auto` ajouté aux modes. Refusé et journalisé sans `target_temp` inscriptible, sans heat et cool dans les modes, ou avec une plage non inscriptible.
- En auto : référence `remote_temp_source`, sinon `current_temp` (valeur non finie ignorée). Sous `low − hysteresis`, `heat` (consigne basse écrite avant le mode) ; au-dessus de `high + hysteresis`, `cool` ; `off` dès le retour dans la bande ; une bascule passe par `off`. `target_temp` refusé pendant auto.
- Consigne de l'unité réécrite si sa relecture s'écarte de plus d'un demi `step`, au plus une fois par `readback_hold` ; toujours différente après, elle est laissée telle quelle et journalisée une fois, jusqu'à la prochaine consigne décidée par Calaos (unité qui arrondit, minimum de son mode).
- Verrous `min_on_time` / `min_off_time` / `changeover_delay` (300 / 300 / 600 s) via `ClimateBase::lockoutWait()`, partagé avec le thermostat. Après un redémarrage : arrêt après `min_on_time`, démarrage après `min_off_time`, délai de bascule seulement vers le mode opposé au dernier chaud/froid (cache `<auto|manual> <dernier envoyé> [<dernier chaud/froid>]`).
- Sonde pas fraîche ou unité déconnectée : rien ne change, `fault` dans l'état ; un départ attendu publie `lockout`.
- Mode relu différent de l'envoi : auto suspendu et journalisé, sauf le mode d'avant relu pendant `readback_hold` (ESPHome le republie après l'écriture de la consigne) et sauf unité déconnectée.
- Template ESPHome : option « Double consigne émulée » ; note YAML CN105 alignée sur la recette validée (`mqtt_subscribe` en `remote_temperature_source`, `remote_temperature_timeout`). IODoc, fr.po, spec §5.3/§7, docs/01 et docs/17.

## Mesuré
CI verte. ProxyClimateRange_test 34 cas, 2 cas MQTT de bout en bout (dont un mode republié avec un seul `MqttOutputString` comme le template), 1 variante de template. Deux revues opus : un bloquant (auto suspendu à chaque démarrage par la republication d'ESPHome) et une boucle d'écriture de consigne, corrigés avec tests rouges d'abord.

## Contre-mutation
CM0 : 0 rouge. 26 mutants tués, 1 équivalent (filtre NaN : les comparaisons avec NaN sont déjà fausses).

## Reste ouvert
- Installer : empêcher « Deux consignes » et « Double consigne émulée » ensemble (le format de template ne l'exprime pas) ; sinon refus au chargement.
- Une référence non numérique tient l'unité sans `fault` publié.
- Un mode changé à la télécommande dans la fenêtre `readback_hold`, s'il s'agit du mode d'avant, n'est vu qu'au premier événement suivant (≤ ~70 s).
- Essai sur la vraie clim (VAL-1).
