# INST-11 — Modes du formulaire de commande = ceux du serveur

## Objectif
Revue de CLIM-10 : DeviceCommand appliquait au thermostat la règle des proxys (modes = liste complète). `modes=heat,cool` retirait `off` (toujours offert), une vanne `output` sans `modes` proposait heat/cool/auto (le serveur n'offre que heat), `auto` ne demandait pas heat et cool. Le serveur publie sa règle dans IODoc, l'installer la lit sans copie en dur.

## Livré
PR calaos/calaos_base#212 et calaos_installer#79.
- IODoc : `list_rule` sur un param liste : `always_includes`, `default_when_linked` (rôle → liste si le param est absent), `value_requires_values` (valeur offerte seulement si les autres le sont). Thermostat : off toujours, output → heat, auto → heat + cool. Source unique : `supportedModes()` et le `list_rule` publié lisent la même structure `ModesRule`. docs 01 et 17.
- Installer : `DeviceCommand::offer()` lit `list_rule` de façon générique (couple champ `mode` / param `modes`, noté dans AGENTS.md) ; un param sans `list_rule` (proxy) garde la liste complète. L'infobulle d'un mode écarté dit pourquoi : absent de `modes`, défaut d'une vanne sans `modes`, ou auto sans heat et cool (textes fr testés).

## Mesuré
Test d'oracle : un client qui dérive les modes du seul document retombe sur `supportedModes()` pour 15 instances. Installer : tst_devicecommand 81 cas, nouveaux cas rouges d'abord ; data/doc identique au master après le merge de #212 ; CI 5/5 dont iodoc-sync. Revue opus : aucun bloquant, retours traités.

## Contre-mutation
CM0 : 0 rouge. 12 mutants tués (base 7, installer 5), un équivalent écarté.

## Reste ouvert
- Les liens refusés par le serveur (vanne déjà prise, `output` avec `output_heat`) ne sont pas visibles par un client qui ne lit que les liens.
