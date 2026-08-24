# Scénarios — AutoScenario et Plages Horaires

> **Provenance.** Chaque affirmation vérifiable de ce document porte sa source :
> `(dérivé, Fichier.cpp:L-L)` pour une lecture du code, `(capturé, <golden>)` pour un payload
> produit par le code. Les extraits de golden sont marqués `intégral` ou `extrait`.

## Vue d'ensemble

Les scénarios Calaos sont des séquences d'actions programmables avec temporisation, déclenchables
manuellement ou selon une plage horaire. Ils n'ont **pas** de moteur d'exécution propre : ils sont
implémentés comme des ensembles de règles ordinaires (`Rule` + `ConditionStd` + `ActionStd`)
générées et adoptées par `AutoScenario` (dérivé, `AutoScenario.cpp:547-832`).

C'est ce qui rend le [moteur de règles](03_rules_engine.md) et sa notion de **règle désactivée**
directement applicables ici — avec une différence de fond, détaillée plus bas : **un scénario, lui,
ne se réactive jamais tout seul.**

---

## AutoScenario

**Fichier :** [src/bin/calaos_server/Scenario/AutoScenario.h](../src/bin/calaos_server/Scenario/AutoScenario.h)

Classe qui abstrait un scénario multi-étapes. Chaque `AutoScenario` **adopte ou crée** un ensemble
de `Rule` dans `ListeRule` — il ne les possède pas (dérivé, `AutoScenario.h:46-61`).

### IOs internes

Ils sont créés à la demande, dans la **pièce du `Scenario`**, avec des ids **déterministes**
dérivés de `scenario_id` (dérivé, `AutoScenario.cpp:398-424`, `:571-611`). Ils sont
`visible="false"` et `save="false"`, sauf `_is_schedule_enabled` dont la valeur est persistée
(dérivé, `AutoScenario.cpp:403-410`, `:609-610`).

| Membre | Id de l'IO | Type créé | Rôle |
|---|---|---|---|
| `ioScenario` | l'id du `Scenario` lui-même | `Scenario` | bouton de déclenchement (TBOOL) |
| `ioIsActive` | `<scenario_id>_is_active` | `InternalBool` | scénario en cours d'exécution |
| `ioStep` | `<scenario_id>_step` | `InternalInt` | étape courante (`-1` = terminé) |
| `ioTimer` | `<scenario_id>_timer` | `InputTimer` | minuterie entre étapes |
| `ioTimeRange` | `<scenario_id>_schedule` | `InPlageHoraire` | plage horaire (optionnel) |
| `ioScheduleEnabled` | `<scenario_id>_is_schedule_enabled` | `InternalBool` | planification active ou non |

⚠️ Si l'un des trois IOs `_is_active` / `_step` / `_timer` ne peut pas être créé,
`checkScenarioRules()` **abandonne** et renvoie `false` — aucune règle n'est construite
(dérivé, `AutoScenario.cpp:584-590`).

`ioScheduleEnabled` n'existe **que** si le scénario a une plage horaire ; sinon il est détruit,
avec les règles `_time_start` / `_time_stop` qui en dépendent
(dérivé, `AutoScenario.cpp:612-632`).

### Règles générées

Chaque règle porte les paramètres `auto_scenario=<scenario_id>` et `auto_scenario_type`
(dérivé, `AutoScenario.cpp:739-742` et suivantes) ; les règles d'étape portent en plus
`auto_scenario_step`.

| `auto_scenario_type` | Nom de la règle | Conditions | Actions |
|---|---|---|---|
| `button_start` | `<id>_button_start` | `ioScenario == true` **ET** `ioIsActive == false` | `ioScenario=false`, `ioIsActive=true`, `ioStep=0`, `ioTimer=0`, `ioTimer=start` |
| `button_stop` | `<id>_button_stop` | `ioScenario == true` **ET** `ioIsActive == true` | `ioScenario=false`, `ioStep=-1`, `ioTimer=0`, `ioTimer=start` |
| `step` | `<id>_step` | `ioIsActive == true` **ET** `ioStep == i` **ET** `ioTimer == true` | actions utilisateur de l'étape, `ioTimer=<pause>`, `ioTimer=start`, `ioStep=i+1` |
| `step_end` | `<id>_step_end` | `ioIsActive == true` **ET** `ioStep == -1` **ET** `ioTimer == true` | actions utilisateur de sortie, `ioIsActive=false` |
| `time_start` | `<id>_time_start` | `ioIsActive == false` **ET** `ioScheduleEnabled == true` **ET** `ioTimeRange == true` | `ioScenario=true` |
| `time_stop` | `<id>_time_stop` | `ioIsActive == true` **ET** `ioScheduleEnabled == true` **ET** `ioTimeRange == false` | `ioStep=-1`, `ioTimer=0`, `ioTimer=start` |

(dérivé, `AutoScenario.cpp:643-715` pour l'adoption, `:737-808` pour la création,
`:834-881` pour `addStep()`, `:1100-1126` pour `createRuleStepEnd()`)

Points que le tableau seul ne dit pas :

- **`button_stop` se déclenche sur `ioScenario == true`, pas sur `false`.** Le même appui sur le
  bouton démarre le scénario s'il est à l'arrêt et l'arrête s'il tourne ; les deux règles sont
  discriminées par `ioIsActive` (dérivé, `AutoScenario.cpp:645-646` vs `:659-660`).
- **`time_stop` n'est créée que si le scénario est cyclique** (`ioTimeRange && cycle`)
  (dérivé, `AutoScenario.cpp:703`, `:792`).
- **Le chaînage des étapes est recalculé à chaque `checkScenarioRules()`** : l'étape `i` pousse
  `ioStep` à `i+1`, la dernière à `0` si le scénario est cyclique, à `-1` sinon
  (dérivé, `AutoScenario.cpp:811-829`).

### API principale

```cpp
AutoScenario *sc = ioScenario->getAutoScenario();

// Étapes
sc->addStep(5.0);                           // nouvelle étape, pause 5 s
sc->addStepAction(0, outputLight, "true");  // step 0 : allume la lumière
sc->addStepAction(AutoScenario::END_STEP, outputAlarm, "false"); // action de SORTIE
sc->setStepPause(1, 10.0);

// Options
sc->setCycling(true);
sc->setDisabled(false);                     // « ne pas jouer ce scénario sur son horaire »

// Commit
sc->checkScenarioRules();                   // renvoie false si les IOs internes manquent

// Horaire
sc->addSchedule();
sc->deleteSchedule();
```
(dérivé, `AutoScenario.h:207-303`, `AutoScenario.cpp:891-906`, `:1071-1098`)

`END_STEP` vaut `0xFEDC1234` et vise la règle `step_end` (dérivé, `AutoScenario.h:202`,
`AutoScenario.cpp:897-900`).

`AutoScenario` est construit par `Scenario` lui-même quand l'IO porte un paramètre
`auto_scenario` non vide (dérivé, `IO/Scenario.cpp:48-52`).

### Étapes mortes : compaction, et son piège

Depuis E4.2f, les back-pointers vers les règles sont des `RuleRef` — un `Rule*` doublé d'un
`weak_ptr` qui expire à la destruction de la règle (dérivé, `AutoScenario.h:62-96`). Tout
accesseur de lecture — `getRuleSteps()`, `stepRule()`, `getCategory()` — appelle
`purgeDeadSteps()`, qui **efface** les entrées mortes en préservant l'ordre des survivantes
(dérivé, `AutoScenario.cpp:52-86`).

⚠️ **La compaction efface la preuve.** `Scenario::toJson()` émet `category` **avant** `broken`, et
`getCategory()` purge : un scan brut d'`isDangling()` répondait donc « pas cassé » pour un scénario
dont la règle d'étape venait d'être détruite — **sérialiser le scénario effaçait la preuve qu'il
était cassé**. La correction tient en deux moitiés, toutes deux nécessaires : `purgeDeadSteps()`
**mémorise** ce qu'elle retire dans `stepRuleDestroyed`, et `isBroken()` **lit cette mémoire avant**
de lancer son scan (dérivé, `AutoScenario.cpp:57-64`, `:185-193`, `AutoScenario.h:151-165`).

`stepRuleDestroyed` est **entièrement dérivé et jamais persisté** : `checkScenarioRules()` le remet
à zéro en re-collectant les règles (dérivé, `AutoScenario.cpp:558-564`), il est privé, sans
accesseur en écriture, et absent de la sérialisation. **Ne pas le confondre avec
`disabled_missing_io`, qui est son exact inverse : persisté, collant, et voulu.**

### Catégorisation

```cpp
string cat = sc->getCategory();
```
Rend `"light"`, `"shutter"`, `"other"`, ou une **combinaison triée par nombre d'actions
décroissant** (`"light-shutter"`). Le classement se fait sur `gui_type` (`light`, `light_dimmer`,
`light_rgb`) puis sur `type` (`shutter`, `shutter_smart`), tout le reste tombe dans `other`
(dérivé, `AutoScenario.cpp:1016-1069`).

⚠️ Un scénario **sans aucune action d'étape** rend la **chaîne vide**, pas `"other"`
(capturé, `tests/core/golden/e40c_ws_autoscenario_list.json`, extrait — second scénario :
`"category": ""` avec `"steps_count": "0"`).

---

## ⚠️ Scénario désactivé (T3.18, décision utilisateur)

Décision utilisateur du 2026-08-16, **durcie** après cadrage.

### Le problème

Quand l'IO piloté par une étape était supprimé, l'étape **disparaissait en silence** et le scénario
continuait de tourner en **séquence raccourcie** : même nom, mêmes horaires, mais il **sautait** ce
qu'il ne pouvait plus faire. Un scénario « départ en vacances » qui fermait les volets puis coupait
le chauffage se contentait de fermer les volets, sans que rien ne le signale.

Le payload le confirmait : `Scenario::toJson()` filtre les actions par `if (!sa.io) continue;`,
si bien qu'une étape ayant perdu son IO est rendue **sans son action et sans la moindre
indication** — indistinguable d'une étape laissée vide exprès (dérivé, `IO/Scenario.cpp:156-158`,
`:181`).

**Raison de la décision : mieux vaut qu'il ne fasse rien de visible que quelque chose de faux.**
Et, mot pour mot :

> « On désactive le scénario, on le flag avec **un nouveau paramètre dans la config** pour que ça
> survive à un reboot, et **un user doit corriger le scénario manuellement en le réactivant**.
> Si un IO disparaît c'est un problème, on ne peut pas le résoudre sans intervention manuelle,
> et un IO dans Calaos ne se supprime pas comme ça. »

(capturé, `docs/refactoring/DECISIONS.md:37-40`, intégral)

### Les deux portes

Un scénario démarre **si et seulement si** `!isBroken()` **ET** `!isDisabledMissingIo()`
(dérivé, `AutoScenario.h:218-228`). Les deux sont nécessaires, et pour des raisons opposées.

| | **`isBroken()`** — porte 1 | **`isDisabledMissingIo()`** — porte 2 |
|---|---|---|
| Nature | **dérivée**, calculée à chaque appel | **persistée**, paramètre `disabled_missing_io` du `Scenario` |
| Stockage | aucun | `io.xml` |
| Durée de vie | disparaît dès que l'IO revient | **collante** : ne s'efface jamais toute seule |
| Qui peut l'écrire | **personne** — pas d'accesseur en écriture, absente de la config | `refreshBrokenScenarios()` (qui ne fait que **poser**) et `tryReenable()` (qui ne fait que **lever**) |
| Survit au reboot | non | **oui** |

Sans la porte 1, la porte 2 serait **forgeable** : `set_param` / `del_param` acceptent n'importe
quel couple (io, paramètre) sans liste blanche. Sans la porte 2, la désactivation
**s'effacerait toute seule** au retour de l'IO — ce que l'arbitrage utilisateur refuse
(dérivé, `AutoScenario.h:218-228`).

`isBroken()` est vraie si (a) une règle d'étape enregistrée ici a été détruite — latch
`stepRuleDestroyed` ou `RuleRef::isDangling()` — ou (b) **n'importe quelle** règle du scénario
(étapes, start, stop, step_end, schedule) répond `Rule::isDisabled()`, c'est-à-dire le mécanisme
d'E4.2e (dérivé, `AutoScenario.cpp:170-210`). Une règle **nulle** n'est pas une cassure : elles le
sont toutes avant le premier `checkScenarioRules()`, et les règles d'horaire sont légitimement
absentes sur un scénario sans plage.

`getMissingIoDescription()` agrège les `Rule::getMissingIoIds()` de toutes les règles du scénario,
dédupliqués, au format `"id_a, id_b"` (dérivé, `AutoScenario.cpp:212-247`).

### La détection, et pourquoi elle ne fait que poser le drapeau

`ListeRoom::refreshBrokenScenarios()` parcourt le cache de scénarios ; pour chacun qui est cassé
**et pas déjà marqué**, elle pose `disabled_missing_io`, arrête proprement un scénario cassé
en cours d'exécution, et émet un `EventScenarioChanged`
(dérivé, `ListeRoom.cpp:445-478`).

**Elle ne lève jamais le drapeau.** C'est le cœur de la décision : remettre l'IO ne suffit pas.

Elle tourne à deux moments, et à deux moments seulement :
- au démarrage, dans `checkAutoScenario()`, **après** que `checkScenarioRules()` a adopté les
  règles et **avant** les deux `Save…()` qui persistent le drapeau — c'est la raison même de sa
  position, aucun `Save` supplémentaire n'a été ajouté (dérivé, `ListeRoom.cpp:332-341`) ;
- après une suppression d'IO **réussie** et **seulement** en politique `Disable`
  (dérivé, `ListeRoom.cpp:421-442`).

`stopBrokenRun()` remet `ioIsActive=false`, `ioStep=-1` et arrête le timer. Sans lui, un scénario
cassé **pendant** son exécution resterait « en cours » pour toujours : ses règles d'étape ne se
déclenchent plus, `ioStep` est bloqué, `step_end` (qui exige `ioStep == -1`) n'arrive jamais et
`button_start` (qui exige `ioIsActive == false`) ne peut plus le relancer
(dérivé, `AutoScenario.h:261-266`, `AutoScenario.cpp:308-324`).

### La persistance du drapeau

Le drapeau est lu **dans le constructeur d'`AutoScenario`**, à côté de `cycle` et `disabled`
(dérivé, `AutoScenario.cpp:100-112`). C'est le seul point garanti **avant** tout
`SaveConfigIO()` : `ListeRoom::checkAutoScenario()` se termine par un `SaveConfigIO()`, donc un
drapeau relu trop tard — ou pas relu du tout — serait **effacé du disque au premier démarrage**,
silencieusement et sans aucune action de l'utilisateur.

`setDisabledMissingIo(false)` **supprime le paramètre** au lieu d'écrire `"false"`, pour qu'un
scénario sain produise **exactement l'`io.xml` qu'il a toujours produit**
(dérivé, `AutoScenario.cpp:256-269`).

Ce drapeau n'est **pas** `disabled`. `disabled` est un choix utilisateur qui signifie « ne pas
jouer ce scénario sur son horaire », il est exposé sous le nom **`enabled`** (sa négation) dans le
payload et **réécrit par chaque `autoscenario modify`** (dérivé, `AutoScenario.h:122-127`,
`AutoScenario.cpp:155-164`, `IO/Scenario.cpp:117`). Le réutiliser aurait laissé le premier
`modify` de n'importe quel client relancer un scénario cassé.

### La réactivation manuelle : `autoscenario reenable`

Nouvelle sous-commande, disponible sur les **deux transports**
(dérivé, `JsonApiHandlerWS.cpp:493-494`, `JsonApiHandlerHttp.cpp:899-900`).

```json
{ "msg": "autoscenario", "msg_id": "…", "data": { "type": "reenable", "id": "io_0" } }
```
(⚠️ en HTTP, `type` et `id` sont **à la racine** du document, pas sous `data` — voir
« Pièges pour les clients » plus bas.)

Trois issues (dérivé, `JsonApi.cpp:2197-2226`, `AutoScenario.cpp:272-306`) :

| Situation | Réponse |
|---|---|
| id inconnu, ou IO qui n'est pas un auto-scénario | `{"error": "wrong input"}` |
| encore cassé | `{"error": "scenario still references missing IOs: <ids>"}` |
| réparé, ou déjà actif (idempotent) | `{"success": "true"}` |

**Le refus est le but de la commande.** Une réactivation qui répondrait « succès » puis se
laisserait redésactiver par la passe de détection suivante reproduirait, d'un cran plus haut, le
no-op silencieux que ce ticket supprime (dérivé, `AutoScenario.cpp:274-286`). C'est aussi pourquoi
c'est une **commande** et non un `set_param` : `set_param` ne sait pas refuser
(dérivé, `JsonApiHandlerWS.cpp:491-492`).

En cas de succès, le drapeau est levé, un `EventScenarioChanged` est émis, et **seul `io.xml`** est
réécrit — aucune règle n'a été touchée (dérivé, `AutoScenario.cpp:297-305`, `JsonApi.cpp:2220-2222`).

### La règle d'étape n'est plus détruite

Conséquence directe de la décision : `rules.xml` **conserve l'id mort verbatim**. La politique par
défaut de `ListeRule::RemoveRule()` est désormais `RuleDetachPolicy::Disable` — la règle est
conservée, l'ordre d'évaluation est strictement préservé, et `ActionStd::SaveToXml()` réécrit la
référence morte telle quelle (dérivé, `ListeRule.cpp:441-455`, `ActionStd.cpp:360-372`).

Un test de contrat a vu son assertion **s'inverser** à cette occasion : l'id mort devait
auparavant **disparaître** de `rules.xml`, il doit désormais y **survivre**.

---

## Le payload de scénario

Produit par `Scenario::toJson()` (dérivé, `IO/Scenario.cpp:108-195`). Servi par
`autoscenario get` / `autoscenario list`, sur les deux transports.

Payload complet d'un scénario sain
(capturé, `tests/core/golden/e40c_ws_autoscenario_get.json`, **intégral**) :

```json
{
  "data": {
    "broken": "false",
    "category": "other",
    "cycle": "false",
    "disabled_missing_io": "false",
    "enabled": "false",
    "id": "io_0",
    "missing_ios": "",
    "schedule": "false",
    "steps": [
      {
        "actions": [
          {
            "action": "true",
            "id": "e40c_bool"
          }
        ],
        "step_pause": "1.5",
        "step_type": "standard"
      },
      {
        "actions": [
          {
            "action": "true",
            "id": "e40c_target"
          },
          {
            "action": "42",
            "id": "e40c_int"
          }
        ],
        "step_pause": "0",
        "step_type": "standard"
      },
      {
        "actions": [
          {
            "action": "done",
            "id": "e40c_string"
          }
        ],
        "step_type": "end"
      }
    ],
    "steps_count": "2"
  },
  "msg": "autoscenario",
  "msg_id": "e40c-get"
}
```

En HTTP, le même objet est renvoyé **sans l'enveloppe** `msg` / `msg_id` / `data`
(capturé, `tests/core/golden/e40c_http_autoscenario_get.json` — mêmes clés, sans enveloppe).
`autoscenario list` renvoie ces mêmes objets dans un tableau sous `data.scenarios`
(capturé, `tests/core/golden/e40c_ws_autoscenario_list.json`).

**Toutes les valeurs sont des chaînes**, y compris les booléens et les compteurs.

| Clé | Sens |
|---|---|
| `id` | id du `Scenario` |
| `cycle` | scénario cyclique (dérivé, `IO/Scenario.cpp:116`) |
| `enabled` | **négation de `disabled`** : « ce scénario est joué sur son horaire » (dérivé, `IO/Scenario.cpp:117`) |
| `schedule` | id de l'IO de plage horaire, ou la chaîne `"false"` s'il n'y en a pas (dérivé, `IO/Scenario.cpp:118-120`) |
| `category` | voir « Catégorisation » ; chaîne vide si aucune action |
| `broken` | porte 1, **dérivée et vivante** |
| `disabled_missing_io` | porte 2, **persistée et collante** |
| `missing_ios` | ids non résolus, `"id_a, id_b"`, vide s'il n'y a rien à réparer |
| `steps_count` | **nombre d'étapes réelles** — voir l'invariant ci-dessous |
| `steps[]` | étapes réelles (`step_type: "standard"`, avec `step_pause`) **plus** une étape `"end"` |

### ⚠️ `steps_count` n'est PAS la longueur du tableau `steps`

`steps_count` vaut `getRuleSteps().size()`, c'est-à-dire les **seules étapes réelles**
(dérivé, `IO/Scenario.cpp:141`). L'étape `step_type: "end"` est ajoutée **hors de la boucle**
(dérivé, `IO/Scenario.cpp:171-190`).

> **Invariant : `len(steps) == steps_count + 1`, toujours.**

Il est vérifié par les **huit payloads de scénario capturés** (sept fichiers `e40c_*autoscenario*`,
dont `…_list.json` qui en porte deux), y compris le cas dégénéré `steps_count: "0"` qui
rend malgré tout **un** élément — l'étape `end`
(capturé, `tests/core/golden/e40c_ws_autoscenario_list.json`, extrait — second scénario).

**Un client qui dimensionne son tableau sur `steps_count` tronque silencieusement les actions de
sortie du scénario.**

⚠️ **Contraste à garder en tête** : dans `get_playlist`, `count` **est** bien la longueur du
tableau ; et le `total_count` d'`audio_db` est un compte **fourni par la base musicale**, sans
rapport garanti avec la longueur de `items`. **Trois champs de comptage, trois sémantiques.**

Une étape `"end"` n'a **pas** de clé `step_pause` : elle n'est pas temporisée
(dérivé, `IO/Scenario.cpp:171-190` — seule la boucle des étapes standard émet `step_pause`).

### `broken` et `disabled_missing_io` divergent **exprès**

C'est le cœur de la décision utilisateur : **les deux clés existent parce qu'elles se contredisent
dans deux états, et ces deux états sont ceux qui comptent.**

| État | `broken` | `disabled_missing_io` | `missing_ios` | Le scénario démarre ? |
|---|---|---|---|---|
| sain | `"false"` | `"false"` | `""` | **oui** |
| cassé, fraîchement désactivé | `"true"` | `"true"` | l'id | non |
| **réparé, en attente de réactivation** | **`"false"`** | **`"true"`** | `""` | **non** |
| cassé, drapeau levé à la main | **`"true"`** | **`"false"`** | l'id | non |

(capturé, `tests/core/golden/e40c_ws_autoscenario_get.json`, `…_get_broken.json` et
`…_get_repaired.json` pour les trois premières lignes, extraits ; les quatre états sont épinglés
clé par clé par `tests/core/ScenarioDisabledMissingIo_test.cpp:945-998`)

**La troisième ligne est la raison d'être du ticket.** `broken=false` avec le drapeau encore posé
signifie « l'équipement est revenu, mais personne n'a encore confirmé que la séquence est de
nouveau celle qu'on croit ». C'est cet état qu'une interface doit transformer en bouton
« Réactiver ». **Avec une seule clé, il serait indistinguable d'un scénario sain** — c'est-à-dire
exactement le silence que T3.18 supprime (dérivé, `IO/Scenario.cpp:123-133`).

La quatrième ligne existe pour la raison symétrique : aucune des deux clés ne peut se cacher
derrière l'autre.

Contraste mesuré entre `e40c_ws_autoscenario_get_broken.json` et
`e40c_ws_autoscenario_get_repaired.json` (extraits, mêmes clés dans le même ordre) :

```
broken               "true"          →  "false"
disabled_missing_io  "true"          →  "true"
missing_ios          "e40c_target"   →  ""
```

⚠️ Dans le golden *broken*, le tableau `steps` a **perdu l'action sur `e40c_target`** : la
deuxième étape n'y porte plus que `e40c_int`, alors que `steps_count` vaut toujours `"2"`
(capturé, `tests/core/golden/e40c_ws_autoscenario_get_broken.json`, extrait). C'est le filtre
`if (!sa.io) continue;` — et c'est précisément pourquoi `missing_ios` a dû être ajouté : le
tableau `steps` ne peut pas, à lui seul, dire ce qui manque.

⚠️ **Les trois clés sont en lecture seule.** `buildAutoscenarioModify()` n'en consomme aucune
(dérivé, `IO/Scenario.cpp:123-125`).

### Un cas connu où `broken` est vrai sans aucun id à nommer

Une règle d'étape **détruite** (démontage, `~Room`, balayage des orphelins) ne laisse **aucun id à
nommer**, contrairement à un IO simplement introuvable. Le payload porte alors `broken: "true"`
avec `missing_ios: ""`. C'est un cas réel, épinglé par
`tests/core/ScenarioDisabledMissingIo_test.cpp:432` (`ARuleDestroyedUnderTheScenarioBreaksItWithNoMissingId`).

---

## Scenario (IO)

**Fichier :** [src/bin/calaos_server/IO/Scenario.h](../src/bin/calaos_server/IO/Scenario.h)

IO virtuel de type TBOOL représentant le bouton de déclenchement d'un scénario. Enregistré dans
`IOFactory` sous le type `"Scenario"`, avec `gui_type="scenario"`
(dérivé, `IO/Scenario.cpp:28`, `:46`).

Par défaut `visible="true"` et `log_history="true"` s'ils ne sont pas déjà présents
(dérivé, `IO/Scenario.cpp:54-55`).

`set_value(true)` **est la coupure de T3.18**, et elle ferme les deux chemins d'entrée : le bouton
(ou `set_state`) **et** la planification — `time_start` n'a qu'une action, `ioScenario = "true"`,
donc elle passe ici aussi (dérivé, `IO/Scenario.cpp:63-92`, `AutoScenario.cpp:787`).

Trois détails qui comptent :
- la coupure ne porte **que** sur `val == true`. `set_value(false)` doit rester possible, sinon un
  scénario déjà lancé deviendrait impossible à arrêter ;
- elle renvoie **`true`** : c'est la convention de la garde `isEnabled()` juste au-dessus — la
  commande a été **acceptée** et n'a délibérément rien fait ;
- elle logue lequel des deux verrous a refusé, et nomme les IOs manquants.

Après un déclenchement accepté, la valeur retombe à `false` au bout de **250 ms**, pour simuler un
appui-relâchement (dérivé, `IO/Scenario.cpp:103`).

---

## InPlageHoraire (Plage horaire)

**Fichier :** [src/bin/calaos_server/IO/InPlageHoraire.h](../src/bin/calaos_server/IO/InPlageHoraire.h)

IO virtuel de type TBOOL, `true` si l'heure courante tombe dans l'une des plages configurées.
Enregistré sous le type `"InPlageHoraire"`, avec le type utilisateur **`"TimeRange"`**
(dérivé, `IO/InPlageHoraire.cpp:27-28`). Toujours `visible="false"`, `gui_type="time_range"`
(dérivé, `IO/InPlageHoraire.cpp:51-52`).

Il s'inscrit lui-même au balayage périodique de `ListeRule`
(dérivé, `IO/InPlageHoraire.cpp:48`) : c'est `RunEventLoop()` qui appelle son `hasChanged()`
toutes les 0,1 s.

### ⚠️ Les plages nocturnes wrappent sur minuit (T3.13, décision utilisateur)

Une plage **inversée** — fin avant début, `23:00 → 01:00`, la façon naturelle d'écrire « la
nuit » — était auparavant **vide et ne se déclenchait jamais**. Elle **wrappe désormais sur
minuit** (dérivé, `IO/InPlageHoraire.cpp:125-188`).

Le contrat exact : **une plage appartient au jour auquel elle est attachée et court jusqu'au
lendemain matin.**

> `23:00 → 01:00` **le lundi** est vraie de **lundi 23 h à mardi 1 h**, et **jamais** le lundi
> entre 00 h 00 et 01 h 00.

(dérivé, `IO/InPlageHoraire.cpp:128-137`, et la documentation d'IO exposée aux clients,
`IO/InPlageHoraire.cpp:38-39`)

L'implémentation évalue **deux jours** à chaque passe : les plages d'aujourd'hui (`previousDay =
false`, une plage inversée couvre alors `[début, fin de journée]`) **et** celles d'hier
(`previousDay = true`, elle couvre `[début de journée, fin]`)
(dérivé, `IO/InPlageHoraire.cpp:208-216`, `:159-184`).

Deux conséquences à connaître :

- **Une borne relative au soleil peut changer de côté selon la saison.** `coucher du soleil →
  23:00` est une plage ordinaire la plus grande partie de l'année, mais devient **wrappante** dès
  que le coucher passe après 23 h. La configuration n'a pas bougé, le comportement si. Le serveur
  le signale **une fois par plage**, en nommant l'IO et le jour de semaine concernés
  (dérivé, `IO/InPlageHoraire.cpp:169-170`, `TimeRange.h:102-110`, `TimeRange.cpp:120-130`).
- **Une plage dont une borne ne se parse pas n'est pas évaluée du tout.** Les bornes retomberaient
  silencieusement sur `00:00:00`, ce qui est indistinguable d'une vraie borne à minuit :
  `isValid()` sert précisément à faire la différence (dérivé, `IO/InPlageHoraire.cpp:151-154`,
  `TimeRange.h:83-100`).

Enfin, si le **mois** courant n'est pas coché, l'IO est toujours `false`, quelles que soient les
plages (dérivé, `IO/InPlageHoraire.cpp:202`).

### TimeRange

**Fichier :** [src/lib/TimeRange.h](../src/lib/TimeRange.h)

Une plage est une paire de bornes. Chaque borne a un **type** et, pour les types solaires, un
**offset** signé :

| `start_type` / `end_type` | Sens |
|---|---|
| `0` | `HTYPE_NORMAL` — heure fixe |
| `1` | `HTYPE_SUNRISE` — lever du soleil |
| `2` | `HTYPE_SUNSET` — coucher du soleil |
| `3` | `HTYPE_NOON` — midi solaire |

(dérivé, `TimeRange.h:69`) Un type hors `[0,3]` est ramené à `HTYPE_NORMAL` au chargement
(dérivé, `IO/InPlageHoraire.cpp:243-245`).

`start_offset` / `end_offset` valent **`1` ou `-1`** — le signe du décalage, l'amplitude étant
portée par `hour`/`min`/`sec` (dérivé, `TimeRange.h:73-74`,
`IO/InPlageHoraire.cpp:247-252`).

### Format XML

Les jours sont des éléments **en français**, et les mois un attribut de l'input
(dérivé, `IO/InPlageHoraire.cpp:363-410`, `:412-464`) :

```xml
<calaos:input id="id-plage" type="InPlageHoraire" months="110000000001">
  <calaos:lundi>
    <calaos:plage start_type="0" start_hour="23" start_min="0" start_sec="0"
                  end_type="0" end_hour="1" end_min="0" end_sec="0"/>
  </calaos:lundi>
</calaos:input>
```

Éléments de jour reconnus : `calaos:lundi`, `calaos:mardi`, `calaos:mercredi`, `calaos:jeudi`,
`calaos:vendredi`, `calaos:samedi`, `calaos:dimanche` ; tout autre nom est ignoré. Un jour sans
plage **n'écrit pas son élément** (dérivé, `IO/InPlageHoraire.cpp:414`).

`months` est une chaîne de **12 caractères**, **janvier à gauche** : elle est la représentation du
`bitset<12>` **inversée** à l'écriture comme à la lecture
(dérivé, `IO/InPlageHoraire.cpp:370-388`, `:477-479`). Une chaîne illisible active **tous** les
mois (dérivé, `IO/InPlageHoraire.cpp:381-387`).

⚠️ Les bornes solaires ne sont écrites **que** si l'offset est non nul : un type `1`/`2`/`3` avec
`0:0:0` ne réécrit ni les heures ni l'offset (dérivé, `IO/InPlageHoraire.cpp:431-442`,
`:451-462`).

### Format JSON (`get_timerange` / `set_timerange`)

Payload complet (capturé, `tests/core/golden/e40c_ws_get_timerange.json`, **extrait** — les deux
premières des quatre plages du golden) :

```json
{
  "data": {
    "months": "110000000001",
    "ranges": [
      {
        "day": "1",
        "end_hour": "12",
        "end_min": "45",
        "end_offset": "1",
        "end_sec": "5",
        "end_type": "0",
        "start_hour": "8",
        "start_min": "30",
        "start_offset": "1",
        "start_sec": "15",
        "start_type": "0"
      },
      {
        "day": "1",
        "end_hour": "23",
        "end_min": "59",
        "end_offset": "1",
        "end_sec": "59",
        "end_type": "0",
        "start_hour": "1",
        "start_min": "20",
        "start_offset": "-1",
        "start_sec": "30",
        "start_type": "2"
      }
    ]
  },
  "msg": "get_timerange",
  "msg_id": "e40c-tr"
}
```

⚠️ **`day` est numéroté de 1 à 7, lundi = 1, dimanche = 7** — et **pas** comme l'enum C++
`TimeRange` (où `SUNDAY = 0`, `MONDAY = 1`, `SATURDAY = 6`). Le producteur émet `day + 1` sur un
index 0 = lundi (dérivé, `JsonApi.cpp:1784-1795`, `TimeRange.cpp:429`) ; le consommateur relit
`"1"` → lundi … `"7"` → dimanche (dérivé, `JsonApi.cpp:1833-1839`). **Toute table écrite depuis les
noms de l'enum C++ sera fausse.**

En HTTP, le même objet sans enveloppe
(capturé, `tests/core/golden/e40c_http_get_timerange.json`).

Un changement émet l'événement `timerange_changed`
(capturé, `tests/core/golden/e40d_ws_timerange_changed.json`, **intégral**) :

```json
{
  "data": {
    "data": {
      "id": "e40d_plage"
    },
    "event_raw": "timerange_changed id:e40d_plage",
    "type": "9",
    "type_str": "timerange_changed"
  },
  "msg": "event"
}
```

⚠️ `type_str` est **`timerange_changed`**, pas le nom de l'enum C++ `EventTimeRangeChanged`.

**Trois comportements de `set_timerange` à connaître, gelés tels quels** (mesurés par E4.0c) :
- **`ranges` absent ⇒ toutes les plages sont effacées.** `o->clear()` est appelé **avant** la
  lecture, et itérer un tableau absent parcourt zéro élément. Une requête qui ne voulait changer
  que `months` vide donc l'agenda (dérivé, `JsonApi.cpp:1819-1824`).
- **Un `months` plus court que 12 est accepté sans erreur** (zéro-extension implicite), ce qui
  éteint silencieusement les mois manquants (dérivé, `JsonApi.cpp:1843-1858`).
- **Un `day` hors 1..7 est silencieusement perdu** : sept `if` indépendants, aucun `else`, aucune
  erreur, et le client reçoit un succès (dérivé, `JsonApi.cpp:1833-1839`).

---

## InputTimer

**Fichier :** [src/bin/calaos_server/IO/InputTimer.h](../src/bin/calaos_server/IO/InputTimer.h)

IO minuterie. Utilisé comme temporisation entre les étapes d'un `AutoScenario`.

| Paramètre | Description |
|---|---|
| `hour` | heures de l'intervalle (0-23) |
| `min` | minutes (0-59) |
| `sec` | secondes (0-59) |
| `msec` | millisecondes (0-999) |
| `autostart` | démarrer la minuterie au lancement de Calaos |
| `autorestart` | redémarrer automatiquement à l'expiration |

(dérivé, `IO/InputTimer.cpp:40-45`)

Actions acceptées : `start`, `stop`, et une chaîne au format **`h:m:s:ms`** (ex. `00:00:00:200`)
pour reconfigurer la durée (dérivé, `IO/InputTimer.cpp:50-52`). C'est cette dernière forme que
`AutoScenario` utilise pour poser la pause d'une étape.

L'IO passe à `true` à l'expiration et à `false` au démarrage
(dérivé, `IO/InputTimer.cpp:47-49`).

---

## Gestion du cache de scénarios

`ListeRoom` maintient un cache des scénarios, alimenté par le constructeur et le destructeur
d'`AutoScenario` (dérivé, `AutoScenario.cpp:121`, `:141`) :

```cpp
void ListeRoom::addScenarioCache(Scenario *sc);
void ListeRoom::delScenarioCache(Scenario *sc);
list<Scenario *> ListeRoom::getAutoScenarios();
void ListeRoom::checkAutoScenario();     // au démarrage : reconstruit + détecte + sauvegarde
void ListeRoom::refreshBrokenScenarios(); // T3.18 : pose le drapeau, ne le lève jamais
```
(dérivé, `ListeRoom.h:169-172`, `:198`)

`checkAutoScenario()` ne tourne **qu'une fois, au démarrage** ; il purge au passage les règles
portant un paramètre `auto_scenario` que plus aucun scénario n'a adoptées, puis appelle
`refreshBrokenScenarios()` et enfin `SaveConfigIO()` + `SaveConfigRule()`
(dérivé, `ListeRoom.cpp:320-342`).

---

## Persistance

Deux fichiers, et **aucun des deux n'est `local_config.xml`** :

- **`io.xml`** — les pièces, leurs IOs, et donc le `Scenario` avec ses paramètres
  (`auto_scenario`, `cycle`, `disabled`, et le cas échéant **`disabled_missing_io`**) ainsi que les
  IOs internes du scénario, rangés dans la **pièce du `Scenario`** (pas dans une pièce dédiée)
  (dérivé, `Constants.h:37`, `CalaosConfig.cpp:259-261` et `:312-314`,
  `AutoScenario.cpp:398-424`, `:571`).
- **`rules.xml`** — les règles générées (dérivé, `Constants.h:38`,
  `CalaosConfig.cpp:451-467`).

Ce qui identifie une règle d'auto-scénario **dans le fichier**, ce sont ses **attributs** :
`auto_scenario="<scenario_id>"`, `auto_scenario_type`, et `auto_scenario_step` pour les étapes
(dérivé, `AutoScenario.cpp:740-742`, `:857-859`). Le membre `Rule::auto_sc_mark` (accessible par
`isAutoScenario()` / `setAutoScenario()`) est **purement en mémoire** : `Rule::SaveToXml()` ne
sérialise que `params`, et le drapeau est reposé à chaque `checkScenarioRules()`
(dérivé, `Rule.h:113`, `Rule.cpp:358-376`, `AutoScenario.cpp:655`).

---

## ⚠️ Pièges pour les clients

Écarts **mesurés** et **gelés** — c'est-à-dire présents dans le code tel qu'il est livré.

1. **Le payload de `autoscenario get` n'est pas ré-injectable dans `modify`.**
   `buildAutoscenarioModify()` lit `disabled` (défaut **`"true"`**), `name` (défaut
   **`« New unnamed scenario »`**), `visible` (défaut `"false"`), `room_name` et `room_type` — or
   `Scenario::toJson()` n'émet **aucun** de ces cinq champs. Un client qui **renvoie tel quel ce
   qu'il vient de recevoir** renomme le scénario, le rend invisible et le désactive — **avec
   `success:true`**. Ce n'est pas un aller-retour, c'est une réinitialisation silencieuse.
2. **L'index du tableau JSON sert de numéro d'étape.** À la création comme à la modification, le
   numéro passé à `addStepAction()` est la **position dans le tableau `steps` reçu**, alors que
   `addStep()` n'est appelé que pour les étapes `standard`. Une étape `end` placée ailleurs qu'en
   **dernier** décale tout ce qui suit, et les actions des étapes suivantes **disparaissent sans
   erreur**, avec `success:true`.
3. **`autoscenario` n'est pas soumis au `serviceScope`.** Le contrôle de portée est posé sur
   `set_param`, `del_param`, `audio_db`, `set_timerange`, `eventlog`, `register_push` et
   `settings` — mais **pas** sur `autoscenario`, qui crée, modifie et supprime des scénarios ainsi
   que leurs règles. Une session de scope service, à qui l'on refuse d'écrire une plage horaire,
   peut donc **détruire des scénarios**.
4. **Silence total sur un `type` d'autoscénario inconnu ou absent**, sur les deux transports : la
   chaîne de `if / else if` n'a pas d'`else` (dérivé, `JsonApiHandlerWS.cpp:477-494`,
   `JsonApiHandlerHttp.cpp:883-900`). Côté HTTP c'est pire : **aucune réponse et aucune
   fermeture**, la socket reste ouverte et le client attend indéfiniment.
5. **Les arguments ne sont pas au même endroit selon le transport** : sous `data` en WebSocket,
   **à la racine** en HTTP — et l'argument ainsi déplacé est le `type` lui-même, c'est-à-dire le
   sélecteur de sous-commande (dérivé, `JsonApiHandlerWS.cpp:476` vs
   `JsonApiHandlerHttp.cpp:882`).
6. **`autoscenario modify` blanchit un scénario amputé.** Après un aller-retour `modify`,
   `deleteRules()` détruit la référence morte : `isBroken()` redevient faux et `tryReenable()`
   **réussit** sur un scénario qui a silencieusement perdu une action d'étape. Le drapeau collant
   est alors levé légitimement, par le mécanisme prévu, sur un scénario amputé. `missing_ios`
   prévient **avant** le round-trip ; le refus de `tryReenable()` ne peut **rien voir après**.
7. **Poser `disabled_missing_io` à la main sur un scénario sain n'a pas d'effet immédiat.**
   Le booléen en mémoire n'est pas touché : le scénario **continue de tourner** jusqu'au prochain
   redémarrage, où il se retrouve désactivé. L'écriture ne prend effet qu'au reboot, alors que la
   lecture est immédiate.
