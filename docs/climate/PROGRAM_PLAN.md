# Programme horaire intégré aux climates

État : idée de conception, **non lancée**. Pas d'implémentation sans décision de Raoul.
Lié à : `docs/autoscenario/PLAN.md`. L'éditeur de plages est partagé, et l'installer regroupe les IOs de la même manière.

## Besoin
Programmer plusieurs consignes dans la journée, par exemple 19 °C le matin et 21 °C le soir, sans écrire de règles. Les programmes de chauffage et de climatisation doivent être distincts, car leurs horaires et leurs consignes diffèrent.

Aujourd'hui, la programmation passe par des plages horaires et des règles écrites à la main. Le mode `auto` d'un climate n'est pas un programme horaire : c'est une plage de **température**, avec une consigne basse et une consigne haute (`calaos_base/docs/17_device_templates.md:204`).

## Modèle côté serveur
On reste sur les plages `InPlageHoraire` : on n'invente pas de nouveau format de programme.

- **Niveaux.** Le climate possède une liste de niveaux. Chaque niveau a :
  - une consigne, dans un IO interne persisté ;
  - sa propre plage horaire interne, `<climate>_heat_level<n>` ;
  - une priorité.
- **Plusieurs niveaux actifs à la fois :** le plus prioritaire l'emporte.
- **Aucun niveau actif :** on applique la consigne par défaut (éco).
- **Créneaux :** une plage accepte plusieurs créneaux par jour et par jour de la semaine. Elle gère aussi les mois, les horaires solaires et le passage de minuit.
- **Limite :** 4 à 6 niveaux par sens.
- **Chaud et froid séparés :**
  - le climate porte un jeu de niveaux pour le chaud (`heat`) et un autre pour le froid (`cool`) ;
  - chaque jeu a ses propres plages, ses consignes et sa consigne par défaut ;
  - le mode courant choisit le jeu actif. C'est cohérent avec la décision 22 du chantier climate : chaque zone est en chaud ou en froid, et la sélection été/hiver est globale.
- **Mode `auto` :** le programme n'agit pas au départ (voir les questions ouvertes).
- **Dérogation :**
  - un bouton « Programme activé / désactivé », porté par un `InternalBool` persisté `<climate>_program_enabled`, sur le modèle de `_is_schedule_enabled` des auto-scénarios ;
  - désactivé, la consigne manuelle est conservée ;
  - réactivé, la consigne repart sur le niveau en cours ;
  - le bouton est aussi pilotable par les règles.
- **Logique dans le device.** Le device lui-même applique la consigne quand une plage change d'état ou quand le mode change. **Le programme ne génère aucune règle.**
- **API.**
  - Les plages se lisent et s'écrivent avec `get_timerange` / `set_timerange`, accompagnées de l'événement `timerange_changed`.
  - Les consignes et `program_enabled` se modifient avec `set_state` sur leurs IOs.
  - L'état du climate expose le programme : activé ou non, niveau actif, prochain changement.
- **IODoc :** documenter les niveaux, la limite et les IOs créés.

## UI : un seul éditeur
On fait un seul composant par technologie : `TimeRangeEditor` en QML, `ScheduleEditor` en Vue. Il est partagé avec les auto-scénarios et les plages autonomes.

- **Mode simple :** une seule plage, pour un auto-scénario ou une plage autonome.
- **Mode multi-niveaux :** une liste de niveaux (id de plage, libellé, couleur, consigne) sur **une seule frise hebdomadaire**.
  - L'utilisateur dessine des créneaux et donne une température à chacun.
  - À l'enregistrement, l'éditeur regroupe les créneaux par température : une température distincte donne un niveau, donc une plage. Il écrit ensuite les plages et les consignes.
  - À la lecture, il fait l'inverse.
- **Carte du climate :**
  - deux onglets, Chauffage et Climatisation, chacun avec sa frise ;
  - un bouton « Programme activé » ;
  - l'affichage du niveau actif et du prochain changement.
- **Webapp, mobile téléphone et mobile desktop :** édition au niveau foyer.
- **Installer :** configuration initiale (nombre de niveaux, consignes par défaut) et édition complète hors ligne.

## calaos_installer : lisibilité
- Les plages et consignes des niveaux sont des IOs possédés par le device. Elles s'affichent sous le nœud du climate, dans un sous-nœud « Programme » replié et en lecture seule (mécanisme d'INST-2, `QTreeWidgetItemDevice`).
- On ne peut pas supprimer ces IOs séparément : ils disparaissent avec le device.
- Comme il n'y a aucune règle générée, la liste des règles ne grossit pas.
- Dans les sélecteurs d'IO des règles, `program_enabled` et le niveau actif sont proposés sous le climate avec un libellé lisible. Exemple : « Salon › programme actif », pour écrire « si niveau Soir actif, alors… ».

## Questions ouvertes
1. **Mode `auto`.** Le programme n'y agit-il pas, ou chaque niveau fournit-il une paire de consignes basse/haute ?
2. **Nombre de niveaux.** Combien au maximum par sens ? 4 ? 6 ?
3. **Consigne par défaut.** Est-elle fixe, ou peut-elle être « aucune », pour arrêter le froid hors créneau ?
4. **Programme désactivé.** Que voit l'utilisateur : la dernière consigne appliquée, ou une consigne manuelle dédiée ?
5. **Devices concernés.** Tous les ClimateBase (proxy ESPHome, thermostat interne, vanne de zone), ou seulement InternalThermostat dans un premier temps ?
6. **Clims avec leur propre programme** (mode `auto` natif de l'appareil). Faut-il les exclure, ou prévenir l'utilisateur d'un conflit ?

## Estimation grossière (à affiner)
- **Serveur** (niveaux, plages internes, application, état, IODoc, tests) : 1,5 à 2 semaines.
- **Éditeur multi-niveaux** (QML et Vue) : 1 à 1,5 semaine par technologie. Le mode simple vient du plan autoscenario.
- **Installer** (regroupement, configuration) : environ 1 semaine.

## Vérification (à l'implémentation)
- Tests serveur :
  - priorité entre niveaux ;
  - passage de minuit ;
  - changement chaud/froid ;
  - bascule de `program_enabled` ;
  - persistance au redémarrage ;
  - aucune règle générée.
- UI : aller-retour frise → plages → frise sans perte.
- Recette sur la box : un plancher réversible avec des programmes chaud et froid différents.
