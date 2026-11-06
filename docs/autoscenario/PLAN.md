# Éditeur d'auto-scénarios dans les UI Calaos

État : analyse, **non lancé**. Sans rapport avec les plans admin et MLSH.

## Contexte
Aucune UI ne permet aujourd'hui d'éditer les auto-scénarios. Le serveur expose pourtant une API complète, mais aucun client ne l'appelle (`docs/refactoring/T3.137.md:60-67`).

Objectif : un éditeur à plusieurs niveaux.
- **calaos_installer** : tout éditer.
- **calaos_mobile** : seulement une partie, essentiellement les plages horaires et l'activation.
- **webapp** : à définir.
- **remote_ui** : rien.

## Ce qui existe côté calaos_base
- **Modèle et stockage.** Un auto-scénario est un IO `type="scenario"` qui porte le param `auto_scenario` (`IO/Scenario.cpp:60-61`).
  - Sa définition (`AutoScenarioDef`) est persistée en params dans io.xml : `autoscenario_uid/_cycle/_enabled/_schedule/_steps`, `as_<step>_pause/_actions`, `as_final_actions` (`docs/04_scenarios.md:98-125`).
  - Les règles de rules.xml ne sont qu'une projection, régénérée au chargement et à chaque mutation (`AutoScenario.cpp:549-652`).
- **IOs internes**, dans la pièce du scénario (`AutoScenario.cpp:394-417`) :
  - `<sid>_is_active` ;
  - `_step` ;
  - `_timer` ;
  - `_schedule`, une plage horaire, créée uniquement par `add_schedule` ;
  - `_is_schedule_enabled`.
- **Exécution.** Tout passe par le moteur de règles. Deux portes bloquent le démarrage : `isBroken` et `disabled_missing_io`.
- **API.** Le message `autoscenario` a 8 types en WS et en HTTP : `list`, `get`, `create`, `delete`, `modify`, `add_schedule`, `del_schedule`, `reenable`.
  - Le payload est validé (`parseScenarioPayload`, `JsonApi.cpp:179-280`).
  - Plages horaires : `get_timerange` / `set_timerange` sur `_schedule`, avec jours, mois et bornes solaires.
  - Événements émis : `scenario_added`, `scenario_deleted`, `scenario_changed`, `timerange_changed`.
  - Gardes :
    - le scope `login_service` est refusé ;
    - un `set_param` sur `autoscenario_*` ou `as_*` est refusé.
- **Manques.**
  - `modify` remplace tout : renommer oblige à renvoyer la pièce et toutes les étapes.
  - Il n'y a pas d'enable, de disable ni de changement de cycle atomiques.
  - `schedule_enabled` n'apparaît pas dans le payload. Il faut faire un `set_state` sur l'IO interne.
  - `set_timerange` ne valide rien : un jour hors de 1..7 est perdu en silence.
  - Le serveur ne valide pas les `value` des actions.
  - Il n'existe aucun catalogue des IOs et actions autorisées.
  - La configuration n'a ni version ni verrou.

## État des UI
- **calaos_mobile.** `ScenarioModel` affiche une liste avec un bouton play. Les signaux `eventScenarioNew/Del/Change` sont déclarés mais jamais émis. `CalaosConnection::sendJson` est réutilisable.
- **calaos-web-app.** Bouton play seulement. `messages.ts` ne connaît que `get_home`, `set_state` et `audio`.
- **calaos_installer.**
  - `DialogNewScenario` ne crée que des scénarios simples.
  - Les params `autoscenario_*` sont protégés.
  - En revanche :
    - **les règles générées sont affichées et éditables comme des règles ordinaires** ;
    - **les IOs internes apparaissent comme des IOs normaux**.
  - Supprimer `_schedule` fait perdre la plage horaire, car l'IO n'est jamais recréé.
  - Un `config put` suivi d'un redémarrage écrase les changements faits en direct.
- **remote_ui.** Clic pour jouer, rien d'autre.

## Lisibilité dans calaos_installer : regroupement visuel
**Problème.** Les IOs internes et les règles générées s'affichent à plat, au milieu des IOs ordinaires. Avec plusieurs auto-scénarios, la liste devient illisible, et c'est dangereux : on peut éditer une règle générée, que le serveur régénère de toute façon, ou supprimer `_schedule`, ce qui fait perdre la plage.

**Principe générique.** Tout IO possédé, que ce soit par un device ou par un auto-scénario, s'affiche sous son propriétaire. On réutilise le mécanisme d'INST-2 : l'arbre pièce → device → IOs possédés, `QTreeWidgetItemDevice` (`src/qtreewidget_addition.h:98`) et la suppression en cascade.

**Arbre**
- Un auto-scénario devient un nœud propriétaire dans sa pièce, avec dessous `_is_active`, `_step`, `_timer`, `_schedule` et `_is_schedule_enabled`.
- Ce nœud est replié par défaut, ses IOs sont en lecture seule, et on ne peut pas les supprimer séparément : on supprime le scénario entier.
- On reconnaît ces IOs à leurs ids dérivés et à `autoscenario_uid`.

**Règles**
- Les règles générées, marquées `auto_scenario` et `autoscenario_uid`, sont rangées dans un dossier par scénario, replié et en lecture seule.
- Un filtre « Afficher les règles générées » est désactivé par défaut.

**Sélecteurs d'IO de l'éditeur de règles**
- Les IOs internes utiles dans une règle (`_is_active`, `_is_schedule_enabled`) sont proposés sous leur parent, avec un libellé lisible, par exemple « Scénario Réveil › actif ».
- Les autres (`_step`, `_timer`) sont cachés.

**Plus tard.** Le même mécanisme couvre les plages et consignes internes du programme climate, voir `docs/climate/PROGRAM_PLAN.md`.

**Effort.** Environ 3 à 5 jours, à ajouter à la phase 2 (installer).

## Concurrence
Scénario problématique, de bout en bout :
1. L'installer fait `config get`.
2. Le mobile modifie une plage horaire en direct.
3. L'installer fait `config put`.

La modification du mobile est alors **écrasée en silence**. Le serveur n'alerte que si des scénarios ou des étapes disparaissent.

Il faut donc :
- un jeton de version renvoyé par `config get` et vérifié par `config put` (`if_match`) ;
- un événement `config_changed` sur le WS ;
- côté installer, un rechargement suivi d'un diff avant le `put`.

## Matrice de capacités proposée

| Opération | Installer | Mobile desktop | Mobile téléphone | Webapp | Remote UI |
|---|---|---|---|---|---|
| Créer / supprimer | oui | **oui** | non | oui (vue réglages) | non |
| Nom, pièce, visible | tout | tout | nom, visible | nom, visible | non |
| Étapes et actions | oui, tout IO commandable (IODoc) | **oui** (IODoc via WS) | non en phase 1 | comme le mobile téléphone | non |
| Pauses | oui | oui | oui | oui | non |
| Cycle | oui | oui | non | oui | non |
| Plages horaires (semaine, solaire, mois) | oui | oui | **oui, c'est le cœur du mobile** | oui (composant partagé) | non |
| Activer / désactiver la planification, `reenable` | oui | oui | oui | oui | non |
| Catégorie | lecture (dérivée) | lecture | lecture | lecture | — |

Les niveaux par UI sont **des choix d'interface, pas de la sécurité** : il n'y a pas de modèle de rôles. Le serveur impose seulement la validation, et le mot de passe admin (plan séparé) pourra servir de porte optionnelle, sans dépendance.

## Travaux par dépôt
- **calaos_base**
  - `set_timerange` qui valide et renvoie une erreur.
  - `schedule_enabled` dans `get` et `modify`.
  - Version de la config : `config get` / `config put` avec `if_match`, plus l'événement `config_changed`.
  - Exposer l'IODoc par WebSocket (`get_iodoc`) pour le mobile desktop et la webapp.
  - Tests : étendre `JsonApiScenario_test`, `TimeRangeCalendar_test` et `AutoScenarioUploadGuard_test`.
- **calaos_installer** (hors ligne uniquement)
  - Dialogue « Auto-scénario » : étapes, actions, pauses, cycle, plage horaire. Il écrit les params `autoscenario_*` selon le format de `docs/04:98-158`.
  - Masquer ou verrouiller les règles générées et les IOs internes.
  - Garde de version avant le `put`.
  - Tests : `tst_projectmanager` avec des fixtures.
- **calaos_mobile**
  - Desktop : éditeur complet (création, étapes, actions à partir de l'IODoc reçu par WS).
  - `AutoScenarioModel` : list, get, événements.
  - `TimeRangeEditor.qml`, partagé entre mobile et desktop.
  - Vue de détail.
  - Câblage des signaux `eventScenario*`.
- **calaos-web-app**
  - Messages et parseurs.
  - Store `scenarios`.
  - `ScheduleEditor.vue`, avec le même modèle JSON que le QML.
  - Route `/scenarios/:id`.
  - Mock-server (`autoscenario`, `get/set_timerange`) et tests e2e.
- **remote_ui** : rien, sauf peut-être afficher `disabled_missing_io`.

## Phasage
1. **Serveur et mobile « planification » (2 à 3 sem.).** Le client renvoie le scénario complet. Côté serveur : `set_timerange` validé, `schedule_enabled`. Côté mobile : liste, détail, éditeur de plages et activation.
2. **Installer (4 à 5 sem.).** Éditeur complet hors ligne, regroupement visuel des IOs et règles générés (lecture seule), jeton de version.
3. **Webapp (2 sem.).** Éditeur de plages et étapes simples, mock et e2e.
4. **Mobile desktop, éditeur complet (2 à 3 sem.).** Création et étapes, avec l'IODoc exposé par WebSocket (`get_iodoc`, environ 1 semaine côté serveur, partagé ensuite avec la webapp).

## Extension : plages horaires autonomes (climat, chauffage…)
**Besoin.** Une plage horaire `InPlageHoraire` / `TimeRange` qui pilote des climates par des règles (« si la plage est active, mode confort ou consigne X ») doit pouvoir s'éditer depuis les UI, comme celle d'un auto-scénario.

**Ce qui existe.**
- `get_timerange` / `set_timerange` fonctionnent déjà sur **n'importe quel** IO plage horaire, pas seulement sur le `_schedule` d'un auto-scénario (`docs/08_http_api.md:827-842`).
- Le gui_type `time_range` est classé « non affiché » (`docs/02_io_drivers.md:104`) : aucune UI ne le montre aujourd'hui.
- Le mode `auto` des climates n'est pas un programme horaire : c'est la plage de consigne basse/haute (`docs/17_device_templates.md:204`). La programmation horaire d'un climat passe donc forcément par une plage horaire et des règles.

**Proposition.**
1. **Un composant d'édition de plage générique.** C'est le même `TimeRangeEditor` en QML et `ScheduleEditor` en Vue que pour les auto-scénarios, branché sur `get/set_timerange` par id d'IO, sans rien de propre aux scénarios.
2. **Afficher les plages horaires visibles.** Une plage avec `visible="true"` apparaît dans sa pièce sous forme de carte « Programmation ». La carte montre si la plage est active et les prochains créneaux, et s'ouvre sur l'éditeur. Les plages non visibles restent cachées, ce qui laisse l'installateur choisir ce que l'utilisateur peut modifier. Les `_schedule` des auto-scénarios restent éditables depuis leur scénario, et ne sont pas affichés en double.
3. **Option : lier la plage au climate.**
   - Un param facultatif `schedule_io` sur le climate pointe vers la plage.
   - La carte du climate affiche alors un bouton « Programme » qui ouvre l'éditeur de cette plage.
   - C'est purement de l'affichage : le comportement reste dans les règles, et le serveur se contente d'exposer l'id dans `get_home`.
   - Il faut aussi le déclarer dans l'IODoc du climate et dans l'installer (choix de la plage).
4. **Droits.** Éditer une plage reste au niveau foyer, comme les plages des auto-scénarios. Seule la création ou la suppression d'une plage et de ses règles est réservée à l'installer et au mobile desktop.
5. **Serveur.** Même validation de `set_timerange` qu'en phase 1, et événement `timerange_changed` diffusé pour que les autres UI se rafraîchissent.

**Hors périmètre, à noter pour plus tard.** Un vrai programme intégré au thermostat (consignes confort/éco/hors-gel par créneau, dans `InternalThermostat`), sans règles à écrire. C'est une fonctionnalité climate à part entière, plus confortable pour l'utilisateur mais plus lourde.

**Phasage.** Les points 1, 2 et 5 viennent avec la phase 1 (même composant, environ 2 à 3 j de plus par UI). Le point 3 est une petite tâche séparée : environ 1 j côté serveur et IODoc, plus 0,5 j par UI.

## Décisions de Raoul (2026-10-18)
1. **Envoi complet, sans `patch`.** Le client fait un `get`, modifie le scénario, puis renvoie tout le payload avec `modify`. Il n'y a ni `patch` ni `revision` : selon Raoul, deux éditions simultanées du même scénario n'arrivent pas en pratique. Aucun changement serveur n'est donc nécessaire pour ce point. Le client doit renvoyer à l'identique les étapes qu'il n'édite pas.
2. **Installer : pas de mode connecté.** Il écrit lui-même les params `autoscenario_*` dans io.xml, au format de `docs/04_scenarios.md:98-158`, et le serveur régénère les règles au chargement.
3. **`config put` périmé : refusé** (`409`).
   - Le jeton de version est renvoyé par `config get`, ce qui correspond au dialogue « ouvrir en ligne » de l'installer, et exigé par `config put`.
   - **Un `put` sans jeton est refusé aussi.** C'est le cas d'un ancien installer, ou d'un projet ouvert depuis un fichier local. Il faut d'abord faire un « ouvrir en ligne » ou un `config get`. Il n'y a pas de période de transition. Les anciens installers ne peuvent donc plus envoyer de config à un serveur à jour, et il faut l'écrire dans les notes de version.
4. **Catalogue d'actions : fourni par l'IODoc.** Idée à étudier : exposer l'IODoc du serveur par WebSocket (par exemple un message `get_iodoc`), pour que le mobile desktop et la webapp n'en embarquent pas de copie.
5. **Mot de passe admin : ne protège pas l'éditeur.**
6. **Conversion d'un scénario simple en auto-scénario : non.**
7. **Calaos mobile en version desktop (kiosque) doit aussi pouvoir créer et éditer complètement les scénarios.** Cela rend la décision 4 nécessaire : un catalogue d'actions via l'IODoc par WebSocket.

## Vérification (à l'implémentation)
- Tests calaos_base sur la validation de `set_timerange` et le refus `409` d'un `put` périmé ou sans jeton.
- Installer : fixtures où un `put` doit échouer s'il est périmé, et règles générées en lecture seule.
- Webapp : e2e d'édition des plages sur le mock.
- Mobile : édition d'une plage reçue en événement par un autre client.
