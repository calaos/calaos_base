# CLIM-10 — Plancher réversible (calaos_base)

## Objectif
Décision 22 (analyse de solanora du 15/10) : une vanne par zone pour le chaud et le froid, modes restreints par zone, humidité facultative pour le froid au sol, template dédié.

## Livré
PR calaos/calaos_base#210 et calaos_installer#78.
- Rôle `output` (vanne de zone) d'InternalThermostat, exclusif de output_heat/output_cool (les deux liés = erreur au chargement, `output` ignoré) ; décision 17 et sorties protégées (§8.3) appliquées. ON pour toute demande, sens donné par le mode ; changement de sens = fermeture après min_on_time puis réouverture après min_off_time ; `action` heating/cooling ; `lockout.output` = sens à venir. Une vanne tenue pour un changement de sens est fermée sur-le-champ quand le mode refroidit sous la coupure d'humidité.
- Param `modes` (heat, cool, auto ; off toujours offert) : restreint modes et capacités, donne ses modes à la vanne (heat seul sans lui) ; `auto` seulement avec heat et cool ; mode hors liste refusé ; valeur inconnue journalisée et ignorée. Consignes basse/haute seulement si auto.
- Froid au sol sans sonde d'humidité permis, avec un avertissement au chargement ; avec sonde, coupure humidity_max inchangée ; un lien humidité qui ne résout pas compte comme lié (coupure).
- IODoc : `replaces` publié sur `output`, plus de `required_when` sur humidity ; libellés et fr.
- Template `thermostat_floor_zone` « Zone de plancher réversible » (vanne, humidité facultative, modes off/heat/cool, 0,5 / 900 / 900, humidity_max 60).
- Spec §3.2, §6.2, §6.3, §8.1–8.3 ; docs 01 et 17.
- calaos_installer#78 : data/doc régénéré depuis master (vanne, `modes`, `valid_min`/`valid_max` de CLIM-9, 6 templates) ; tests adaptés, `required_when` testé par un rôle construit dans le test.
- Clients : aucune PR, web, mobile et panneau construisent le choix de mode depuis `capabilities.modes` (vérifié par la revue).

## Mesuré
CI verte après rebases sur CLIM-9 et CLIM-8 ; make check 210 tests (1 échec d'environnement, mcp). `InternalThermostatValve_test` 18 cas. Installer : CI 5/5 dont l'IO doc synchronisée, 21 suites locales. Revue opus : un bloquant (coupure humidité ignorée pendant le maintien d'un changement de sens) corrigé avec tests rouges d'abord, plus 4 non bloquants traités.

## Contre-mutation
CM0 : 0 rouge. 19 mutants, 19 tués.

## Reste ouvert
- INST-11 : modes du formulaire de commande alignés sur le serveur par un signal IODoc.
- DOC-7 : recettes plancher réversible et générateur commun.
