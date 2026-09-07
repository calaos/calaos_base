# Scénarios — AutoScenario et Plages Horaires

> **Provenance.** Chaque affirmation vérifiable de ce document porte sa source :
> `(dérivé, Fichier.cpp:L-L)` pour une lecture du code, `(capturé, <golden>)` pour un payload
> produit par le code. Les extraits de golden sont marqués `intégral` ou `extrait`.

## Vue d'ensemble

Un auto-scénario est une **séquence d'actions déclarée**, avec une temporisation par étape,
déclenchable manuellement ou selon une plage horaire.

**La définition est la donnée ; les règles en sont une projection.** Un objet `AutoScenarioDef`
porte l'intégralité du scénario — ses étapes, leurs pauses, leurs actions — et il est **persisté**
dans les paramètres de l'IO scénario, à l'intérieur d'`io.xml`. `AutoScenario` est un
**générateur** : il détruit les règles qu'il a écrites et les **régénère** toutes depuis la
définition (dérivé, `Scenario/AutoScenario.h:46-64`, `AutoScenario.cpp:549-652`).

Ce qui en découle, et qui commande tout le reste de ce document :

- **rien n'est jamais deviné à partir de `rules.xml`.** Il n'y a plus d'appariement de motifs, plus
  d'adoption de règle, plus de balayage d'orphelines. Une règle éditée, ou détruite par un tiers,
  est **réécrite** au chargement suivant depuis la définition ;
- **une règle qui ne vient pas du générateur n'est jamais touchée.** Le commentaire posé à l'endroit
  exact où l'on serait tenté de remettre un balayage le dit mot pour mot
  (dérivé, `ListeRoom.cpp:347-353`) ;
- **aucun accesseur de lecture ne mute.** Lire un scénario deux fois rend deux fois la même chose
  (épinglé par `tests/core/AutoScenarioRules_test.cpp:700`,
  `ReadingAScenarioTwiceAnswersTheSameAndChangesNothing`).

Les règles générées restent des `Rule` ordinaires (`ConditionStd` + `ActionStd`), donc la notion de
**règle désactivée** du [moteur de règles](03_rules_engine.md) s'applique telle quelle — avec une
différence de fond, détaillée plus bas : **un scénario, lui, ne se réactive jamais tout seul.**

---

## La définition — `AutoScenarioDef`

**Fichier :** [src/bin/calaos_server/Scenario/AutoScenarioDef.h](../src/bin/calaos_server/Scenario/AutoScenarioDef.h)

Elle est **possédée par l'IO `Scenario`** et n'est jamais nulle
(dérivé, `IO/Scenario.h:45-51`, `IO/Scenario.cpp:39`, `:74`).

```
AutoScenarioDef
  uid          : string  opaque, jamais recyclé — le marqueur de la définition
  cycle        : bool
  enabled      : bool    l'ancien `disabled`, inversé une fois pour toutes
  scheduleIoId : string  vide si le scénario n'est pas planifié
  steps        : vector<AutoScenarioDefStep>
  finalStep    : AutoScenarioDefStep   déclarée, plus synthétique

AutoScenarioDefStep
  stepId  : string  opaque, stable, jamais réutilisé (vide sur l'étape finale)
  pause   : double
  actions : vector<AutoScenarioDefAction>

AutoScenarioDefAction
  ioId  : string   un ID, jamais un pointeur
  value : string   texte utilisateur quelconque
```
(dérivé, `AutoScenarioDef.h:129-145`, `:236-242`)

### Les trois propriétés qui comptent

- ⭐ **Une action est désignée par un `ioId`, jamais par un pointeur, et ce codec ne le résout
  jamais.** Une action dont l'IO n'existe plus est **chargée, conservée en mémoire, réécrite sur le
  disque et émise sur le fil**. Rien ne l'escamote (dérivé, `AutoScenarioDef.h:121-135`).
- **`uid` et `stepId` sont opaques et jamais recyclés.** Les deux allocateurs sont des compteurs
  monotones à l'échelle du processus, et **tout id lu dans une configuration est réinjecté dedans**
  (`observeUid()` / `observeStepId()`), de sorte qu'un id déjà présent dans `io.xml` ne peut pas
  être redistribué (dérivé, `AutoScenarioDef.cpp:214-240`, `:305`, `:331`). Aucun code ne doit
  déduire quoi que ce soit de leur forme.
- **Un `stepId` est aussi un fragment de nom de paramètre**, donc restreint à `[A-Za-z0-9_]` et
  jamais égal à `final` (dérivé, `AutoScenarioDef.cpp:191-206`).

⚠️ `AutoScenarioDef::resetIdAllocatorsForTests()` (`AutoScenarioDef.h:210`) existe **pour le
harnais de test uniquement** : il remet les compteurs dans l'état d'un processus qui vient de
démarrer. Un appelant de production qui l'invoquerait sous une configuration vivante casserait
l'invariant « jamais recyclé ».

---

## Où la définition est écrite — les params de l'IO scénario, dans `io.xml`

**Décision utilisateur** : l'infrastructure Calaos tourne autour de **deux fichiers**, `io.xml` et
`rules.xml`. Un troisième fichier n'aurait pas supprimé le risque de perte, il l'aurait déplacé vers
chaque outil et chaque procédure de sauvegarde qui l'aurait ignoré. Le serveur lui-même n'accepte au
téléversement que ces deux fichiers plus `local_config.xml` — liste blanche en dur
(dérivé, `JsonApiHandlerHttp.cpp:783-785`).

**Le porteur — des params, pas des nœuds enfants — est une mesure**, pas une préférence :
`calaos_installer` régénère `io.xml` en entier à chaque sauvegarde et **réémet verbatim tous les
attributs qu'il ne modélise pas**, alors qu'il **perd** les nœuds enfants. Un scénario porté par des
params survit donc à un installeur **qui n'en sait rien**, y compris un installeur ancien
qu'aucune mise à jour ne rattrapera (raisonnement et sites mesurés :
`AutoScenarioDef.h:46-61`, et `docs/refactoring/E4.6.md` § D2).

### L'encodage

Les clés, telles que `saveToParams()` les produit
(dérivé, `AutoScenarioDef.cpp:31-43` pour les noms, `:348-403` pour l'écriture) :

| Param | Écrit | Sens |
|---|---|---|
| `autoscenario_uid` | toujours | le marqueur de la définition, `as_<n>` |
| `autoscenario_schema` | toujours, `"1"` | version de format |
| `autoscenario_cycle` | toujours | `"true"` / `"false"` |
| `autoscenario_enabled` | toujours | l'ancien `disabled`, **inversé** |
| `autoscenario_schedule` | **seulement si planifié** | id de l'IO de plage horaire |
| `autoscenario_steps` | toujours, **même vide** | l'**ordre et l'identité** des étapes, `"s0\|s1"` |
| `as_<stepId>_pause` | une par étape | |
| `as_<stepId>_actions` | une par étape | `"io_77=up\|io_78=up"` |
| `as_final_actions` | **seulement si non vide** | les actions de l'étape terminale |

Forme obtenue sur un IO scénario (dérivée de l'écriture ci-dessus, ce n'est pas un fichier capturé) :

```xml
<calaos:input type="scenario" id="input_18" name="Monter volets matin" visible="false"
              autoscenario_uid="as_0" autoscenario_schema="1"
              autoscenario_cycle="false" autoscenario_enabled="true"
              autoscenario_steps="s0|s1"
              as_s0_pause="1.5"  as_s0_actions="io_77=up|io_78=up"
              as_s1_pause="0"    as_s1_actions="io_9=42"
              as_final_actions="io_9=false"/>
```

Trois raisons à **un param par étape** plutôt qu'un blob JSON unique, dans cet ordre : une étape
corrompue n'emporte pas la définition ; `io.xml` est lu par des humains en dépannage et un blob JSON
dans un attribut XML devient une bouillie de `&quot;` ; les lignes restent bornées
(dérivé, `AutoScenarioDef.h:76-79`).

### `autoscenario_steps` est **seule autoritaire**

L'ordre et l'identité des étapes viennent de cette clé et de rien d'autre : ni de l'ordre
alphabétique des params, ni des clés `as_*` qui existent par ailleurs. Un jeton invalide ou répété
est refusé, et les params d'une étape que la liste ne nomme pas sont **ignorés au chargement** puis
**retirés à la sauvegarde** (dérivé, `AutoScenarioDef.cpp:317-343`, `:385-399`).

⚠️ C'est le **seul nettoyage automatique** du modèle, et il ne porte que sur les deux espaces de
noms possédés (`autoscenario_*` et `as_<id>_pause|actions`), reconnus par un prédicat **serré** —
pas « tout ce qui commence par `as_` » (dérivé, `AutoScenarioDef.cpp:160-189`). Le marqueur
historique `auto_scenario` n'est dans **aucun** des deux et n'est jamais touché.

### Échappement

`|` sépare les éléments et `=` sépare un id de sa valeur. Les ids d'IO sont sûrs par construction,
**mais pas les valeurs d'action** : ce sont des chaînes utilisateur quelconques. `%`, `|` et `=`
sont donc **percent-encodés** (`%25`, `%7C`, `%3D`) dans les ids comme dans les valeurs, le `%` en
premier puisqu'il est l'échappement (dérivé, `AutoScenarioDef.cpp:112-128`, `:252-265`).

Deux normalisations à connaître (dérivé, `AutoScenarioDef.cpp:130-158`, `:267-296`) :

- un `%` **non** suivi de deux chiffres hexadécimaux est un `%` littéral, et il ressort ré-encodé
  `%25` ;
- un élément **sans `=`** est un id avec une valeur vide — il est **conservé**, jamais jeté.

⇒ l'aller-retour est **exact octet à octet** sur les params que ce codec écrit, et **idempotent
après une passe** sur des params écrits à la main.

### Un IO scénario qui n'est pas un auto-scénario ne bouge pas

`saveToParams()` ne fait **rien du tout** — ni écriture ni suppression — quand la définition ne
porte pas d'uid (dérivé, `AutoScenarioDef.cpp:350-354`). Un IO `type="scenario"` ordinaire ressort
d'une sauvegarde **octet pour octet** tel qu'il y est entré.

### Le premier chargement d'une configuration ancienne

`Scenario::captureDefinitionFromRules()` construit **une première** définition à partir des règles
que l'`AutoScenario` porte, et **seulement si la définition est encore vide**
(dérivé, `IO/Scenario.cpp:140-183`, la garde en `:149`). Une fois la définition établie, elle est la
source de vérité et n'est plus jamais re-dérivée : le faire à chaque sauvegarde remettrait un
aller-retour à perte entre le client et la donnée.

⚠️ Ce relevé lit les ids d'action par `ActionStd::get_output_id()` et **non** par `get_output()` :
l'id d'une action dont l'IO a disparu est **gardé** (dérivé, `IO/Scenario.cpp:108-136`).

---

## AutoScenario — le générateur

**Fichier :** [src/bin/calaos_server/Scenario/AutoScenario.h](../src/bin/calaos_server/Scenario/AutoScenario.h)

`AutoScenario` est construit par `Scenario` lui-même quand l'IO porte un paramètre `auto_scenario`
non vide (dérivé, `IO/Scenario.cpp:60-61`).

> ⛔ **`auto_scenario` est toujours LE marqueur d'IO, et il ne doit pas être re-clé.** Le re-clé et
> aucun `AutoScenario` n'est construit pour un scénario existant : ses règles cessent d'être
> revendiquées, sa définition n'est jamais amorcée depuis elles, et le scénario cesse d'exister en
> silence. Le commentaire du code le dit à l'endroit exact
> (dérivé, `IO/Scenario.cpp:54-59`). `autoscenario_uid` vit **à côté** de lui, jamais à sa place.

Le `scenario_id` (la valeur d'`auto_scenario`) sert aussi de **préfixe aux ids dérivés** des IOs
internes, ce qui l'empêche d'être renommé seul (dérivé, `AutoScenario.h:69-71`,
`AutoScenario.cpp:54`).

### IOs internes

Créés à la demande, dans la **pièce du `Scenario`**, avec des ids **déterministes** dérivés de
`scenario_id`. Ils sont `visible="false"` et `save="false"`, sauf `_is_schedule_enabled` dont la
valeur est persistée (dérivé, `AutoScenario.cpp:394-417`, `:511-547`, `:543-544`).

| Membre | Id de l'IO | Type créé | Rôle |
|---|---|---|---|
| `ioScenario` | l'id du `Scenario` lui-même | `Scenario` | bouton de déclenchement (TBOOL) |
| `ioIsActive` | `<scenario_id>_is_active` | `InternalBool` | scénario en cours d'exécution |
| `ioStep` | `<scenario_id>_step` | `InternalInt` | étape courante (`-1` = terminé) |
| `ioTimer` | `<scenario_id>_timer` | `InputTimer` | minuterie entre étapes |
| `ioTimeRange` | `<scenario_id>_schedule` | `InPlageHoraire` | plage horaire (optionnel) |
| `ioScheduleEnabled` | `<scenario_id>_is_schedule_enabled` | `InternalBool` | planification active ou non |

Points que le tableau ne dit pas :

- **`prepareInternalIos()` tourne AVANT toute destruction**, et un échec fait sortir
  `rebuildRules()` sur `false` sans que rien n'ait été détruit : un build refusé laisse la
  configuration telle qu'il l'a trouvée (dérivé, `AutoScenario.cpp:551-552`, `:517-524`,
  `AutoScenario.h:133-137`).
- **`ioTimeRange` n'est jamais créé ici** : il est seulement **cherché** par son id dérivé
  (dérivé, `AutoScenario.cpp:527-528`). Seul `addSchedule()` le crée (`:849-856`).
- **`ioScheduleEnabled` n'existe que si le scénario a une plage horaire.** Sinon un IO résiduel
  portant cet id est détruit — `Destroy` et pas `Disable`, parce que `addSchedule()` reconstruit
  exactement le même id et qu'une copie désactivée des règles d'horaire serait dupliquée au build
  suivant (dérivé, `AutoScenario.cpp:561-571`, `:858-869`).

### Règles générées

Chaque règle porte **deux marqueurs** : `auto_scenario=<scenario_id>` (celui que des lecteurs plus
anciens attendent encore) et `autoscenario_uid=<uid>` (celui de la définition). Les règles d'étape
portent en plus `auto_scenario_step` (dérivé, `AutoScenario.cpp:474-483`, `:609`).

⭐ **Seul l'uid décide de ce qui peut être détruit** : une règle qui ne le porte pas a été écrite par
quelqu'un d'autre et n'est jamais touchée (dérivé, `AutoScenario.cpp:485-497`,
`AutoScenario.h:50-53`).

| `auto_scenario_type` | Nom de la règle | Conditions | Actions |
|---|---|---|---|
| `button_start` | `<sid>_button_start` | `ioScenario == true` **ET** `ioIsActive == false` | `ioScenario=false`, `ioIsActive=true`, `ioStep=0`, `ioTimer=0`, `ioTimer=start` |
| `button_stop` | `<sid>_button_stop` | `ioScenario == true` **ET** `ioIsActive == true` | `ioScenario=false`, `ioStep=-1`, `ioTimer=0`, `ioTimer=start` |
| `step_end` | `<sid>_step_end` | `ioIsActive == true` **ET** `ioStep == -1` **ET** `ioTimer == true` | `ioIsActive=false`, puis les actions de `finalStep` |
| `step` | `<sid>_step` | `ioIsActive == true` **ET** `ioStep == i` **ET** `ioTimer == true` | `ioStep=<suivant>`, `ioTimer=<pause>`, `ioTimer=start`, puis les actions de l'étape |
| `time_start` | `<sid>_time_start` | `ioIsActive == false` **ET** `ioScheduleEnabled == true` **ET** `ioTimeRange == true` | `ioScenario=true` |
| `time_stop` | `<sid>_time_stop` | `ioIsActive == true` **ET** `ioScheduleEnabled == true` **ET** `ioTimeRange == false` | `ioStep=-1`, `ioTimer=0`, `ioTimer=start` |

(dérivé, `AutoScenario.cpp:579-586`, `:588-594`, `:596-602`, `:604-627`, `:631-635`, `:639-646`)

- **`button_stop` se déclenche sur `ioScenario == true`, pas sur `false`.** Le même appui démarre le
  scénario s'il est à l'arrêt et l'arrête s'il tourne ; les deux règles sont discriminées par
  `ioIsActive` (dérivé, `AutoScenario.cpp:580-581` vs `:589-590`).
- **`step_end` est générée avec les autres**, avant les étapes, et porte les actions de
  `finalStep` : elle n'est plus une règle à part synthétisée après coup
  (dérivé, `AutoScenario.cpp:596-602`).
- **Le chaînage** : l'étape `i` pousse `ioStep` à `i+1` ; la dernière à `0` si le scénario est
  cyclique, à `-1` sinon — et `-1` passe la main à `step_end`
  (dérivé, `AutoScenario.cpp:615-621`).
- **`time_stop` n'est créée que si le scénario est cyclique** (dérivé, `AutoScenario.cpp:637`).
- ⭐ **Le numéro d'étape et sa condition sont écrits ensemble**, dans la même passe, depuis le même
  `i` : ils ne peuvent plus diverger (dérivé, `AutoScenario.cpp:609` et `:612`). L'ancienne
  renumérotation, qui réécrivait la condition sans toucher au param, n'existe plus.
- **Une action dont l'id ne résout pas est quand même écrite dans la règle**, et la règle est
  marquée comme référençant un IO manquant, pour que le moteur la saute au lieu d'exécuter une
  liste d'actions amputée (dérivé, `AutoScenario.cpp:439-455`).

### ⛔ La mise en retrait — ce qui protège une configuration écrite avant la définition

Tant qu'**une seule** règle du scénario ne porte **aucun** uid, le générateur **ne fait rien du
tout** : il ne détruit rien, ne crée rien, et rend `true` (dérivé, `AutoScenario.cpp:554-559`,
`hasLegacyRules()` en `:116-128`).

Le test porte sur les **règles**, jamais sur la définition. C'est essentiel : une sauvegarde frappe
un `autoscenario_uid` dans `io.xml` toute seule (`Scenario::SaveToXml()`,
`IO/Scenario.cpp:185-191`), et cela ne doit pas suffire à armer le générateur sur des règles qu'il
n'a pas écrites. Le prédicat est **« ne porte aucun uid »**, pas « porte un uid différent du
nôtre » : au tout premier démarrage le nôtre est vide lui aussi
(dérivé, `AutoScenario.cpp:118-122`, épinglé par
`tests/core/AutoScenarioRules_test.cpp:601`, `TheStandDownHoldsWhenNEITHERTheRulesNORTheIoCarryAUid`).

**On ne sort de la mise en retrait que par un acte d'écriture explicite** — `addStep()`,
`addStepAction()`, `setStepPause()`, `deleteRules()`, `addSchedule()`, `deleteSchedule()` —, qui
prend possession de l'ensemble et le remplace ; c'est ce que `autoscenario modify` a toujours fait
(dérivé, `AutoScenario.cpp:654-689`, `:380-392`, `:849-869`, `AutoScenario.h:151-157`).

### API principale

```cpp
AutoScenario *sc = ioScenario->getAutoScenario();

sc->addStep(5.0);                           // nouvelle étape, pause 5 s
sc->addStepAction(0, outputLight, "true");  // step 0 : allume la lumière
sc->setStepPause(1, 10.0);

sc->setCycling(true);
sc->setDisabled(false);                     // « ne pas jouer ce scénario sur son horaire »

sc->rebuildRules();                         // false si les IOs internes manquent
sc->addSchedule();
sc->deleteSchedule();
```
(dérivé, `AutoScenario.h:172-189`, `:270-284`)

`checkScenarioRules()` est conservé comme **alias historique** de `rebuildRules()` : il n'y a plus
rien à vérifier (dérivé, `AutoScenario.h:179-180`).

⚠️ Chacun de ces mutateurs **régénère les règles immédiatement** ; il n'y a plus d'appel de commit
séparé (dérivé, les six sites de `rebuildRules(true)` : `AutoScenario.cpp:391`, `:664`, `:674`, `:688`, `:855`, `:869`).

### Catégorisation

```cpp
string cat = sc->getCategory();
```
Rend `"light"`, `"shutter"`, `"other"`, ou une **combinaison triée par nombre d'actions
décroissant** (`"light-shutter"`). Le classement se fait sur `gui_type` (`light`, `light_dimmer`,
`light_rgb`) puis sur `type` (`shutter`, `shutter_smart`), tout le reste tombe dans `other`
(dérivé, `AutoScenario.cpp:794-847`).

⚠️ **`category` est calculée depuis la PROJECTION, pas depuis la définition** : elle parcourt les
règles d'étape et lit leurs actions par pointeur, donc une action dont l'IO ne résout pas **n'est
pas comptée** (dérivé, `AutoScenario.cpp:800-810`, `getRealAction()` en `:739-764`,
`isScenarioInternalIO(nullptr) == true` en `:702-706`). C'est un champ **dérivé, indicatif** ; c'est
`missing_ios` qui dit ce qui manque, jamais `category`.

⚠️ Un scénario **sans aucune action d'étape** rend la **chaîne vide**, pas `"other"`
(capturé, `tests/core/golden/e40c_ws_autoscenario_list.json`, extrait — second scénario :
`"category": ""` avec `"steps": []`).

⚠️ **L'étape finale n'est pas catégorisée** : `getCategory()` ne parcourt que les étapes standard.
`getEndStepActionCount()` reste le seul observable de cette étape côté `AutoScenario`
(dérivé, `AutoScenario.h:275-281`, `AutoScenario.cpp:778-781`).

---

## ⚠️ Scénario désactivé (T3.18, décision utilisateur)

Décision utilisateur du 2026-08-16, conservée **intégralement** par la refonte.

### Le problème

Quand l'IO piloté par une étape était supprimé, l'étape **disparaissait en silence** et le scénario
continuait de tourner en **séquence raccourcie** : même nom, mêmes horaires, mais il **sautait** ce
qu'il ne pouvait plus faire. Un scénario « départ en vacances » qui fermait les volets puis coupait
le chauffage se contentait de fermer les volets, sans que rien ne le signale.

**Raison de la décision : mieux vaut qu'il ne fasse rien de visible que quelque chose de faux.**
Et, mot pour mot :

> « On désactive le scénario, on le flag avec **un nouveau paramètre dans la config** pour que ça
> survive à un reboot, et **un user doit corriger le scénario manuellement en le réactivant**.
> Si un IO disparaît c'est un problème, on ne peut pas le résoudre sans intervention manuelle,
> et un IO dans Calaos ne se supprime pas comme ça. »

(capturé, `docs/refactoring/DECISIONS.md:37-40`, intégral)

⭐ **Ce que la refonte a changé, c'est la cause, pas la décision** : l'action **ne disparaît plus**
du payload. Elle est nommée par son id et marquée `resolved: "false"` (voir « Le payload » plus
bas). Le drapeau collant, son refus de se lever tout seul et la commande `reenable` sont, eux,
inchangés.

### Les deux portes

Un scénario démarre **si et seulement si** `!isBroken()` **ET** `!isDisabledMissingIo()`
(dérivé, `AutoScenario.h:191-217`, appliqué en `IO/Scenario.cpp:210-222`).

| | **`isBroken()`** — porte 1 | **`isDisabledMissingIo()`** — porte 2 |
|---|---|---|
| Nature | **dérivée**, calculée à chaque appel | **persistée**, paramètre `disabled_missing_io` du `Scenario` |
| Stockage | aucun | `io.xml` |
| Durée de vie | disparaît dès que l'IO revient | **collante** : ne s'efface jamais toute seule |
| Qui peut l'écrire | **personne** — pas d'accesseur en écriture, absente de la config | `refreshBrokenScenarios()` (qui ne fait que **poser**) et `tryReenable()` (qui ne fait que **lever**) |
| Survit au reboot | non | **oui** |

Sans la porte 1, la porte 2 serait **forgeable** : `set_param` / `del_param` acceptent n'importe
quel couple (io, paramètre) **sauf** les paramètres de la définition, refusés depuis
[`T3.137`](refactoring/T3.137.md) — et `disabled_missing_io` n'en fait délibérément pas partie. Sans la porte 2, la désactivation
**s'effacerait toute seule** au retour de l'IO — ce que l'arbitrage utilisateur refuse
(dérivé, `AutoScenario.h:191-201`).

### `isBroken()` — trois raisons, et il en faut trois

```
broken(scénario) ≡  ∃ action ∈ (def.steps ∪ def.finalStep) : findIO(action.ioId) == nullptr
                 ∨  ∃ règle vivante du scénario : rule->isDisabled()
                 ∨  (projection construite ∧ |règles portant notre uid| < |attendues|)
```
(dérivé, `AutoScenario.cpp:201-227`, `AutoScenario.h:203-210`)

1. **La fonction pure de la définition.** Aucun aller-retour par `rules.xml` ne peut la blanchir :
   l'id est dans la définition, pas dans la règle.
2. **Le chemin à chaud** (E4.2e), quand un IO est supprimé pendant que le serveur tourne.
3. **Une de nos règles a été détruite par un tiers.** Gardée par `rulesGenerated` : avant la
   première construction, ne posséder aucune règle est l'état normal, pas une casse
   (dérivé, `AutoScenario.h:114-116`, `expectedRuleCount()` en `AutoScenario.cpp:130-140`).

⚠️ **La troisième raison ne nomme aucun id.** `broken: "true"` avec `missing_ios: ""` reste donc
un état **atteignable, et voulu** : c'est le seul signal qu'un tiers a détruit une règle. Épinglé
par `tests/core/ScenarioDisabledMissingIo_test.cpp:426`
(`ARuleDestroyedUnderTheScenarioBreaksItWithNoMissingId`).

`getMissingIoDescription()` agrège les ids non résolus, **dédupliqués**, au format `"id_a, id_b"` :
d'abord ceux de la définition (étapes dans l'ordre, puis l'étape finale), puis ceux des règles
vivantes — les règles en second pour qu'une configuration dont les règles précèdent la définition
nomme quand même ce qui lui manque (dérivé, `AutoScenario.cpp:229-268`).

### La détection, et pourquoi elle ne fait que poser le drapeau

`ListeRoom::refreshBrokenScenarios()` parcourt le cache de scénarios ; pour chacun qui est cassé
**et pas déjà marqué**, elle pose `disabled_missing_io`, arrête proprement un scénario cassé en
cours d'exécution, et émet un `EventScenarioChanged` (dérivé, `ListeRoom.cpp:603-637`).

**Elle ne lève jamais le drapeau.** C'est le cœur de la décision : remettre l'IO ne suffit pas.

Elle tourne à deux moments, et à deux moments seulement :
- au démarrage, dans `checkAutoScenario()`, **après** que `rebuildRules()` a régénéré les règles et
  **avant** les deux `Save…()` qui persistent le drapeau — c'est la raison même de sa position,
  aucun `Save` supplémentaire n'a été ajouté (dérivé, `ListeRoom.cpp:355-360`) ;
- après une suppression d'IO **réussie** et **seulement** en politique `Disable`
  (dérivé, `ListeRoom.cpp:597-598`).

`stopBrokenRun()` remet `ioIsActive=false`, `ioStep=-1` et arrête le timer — `ioIsActive` **en
premier**, pour que l'écriture de `ioStep` juste après ne puisse rien relancer. Sans lui, un
scénario cassé **pendant** son exécution resterait « en cours » pour toujours : ses règles d'étape
ne se déclenchent plus, `ioStep` est bloqué, `step_end` (qui exige `ioStep == -1`) n'arrive jamais
et `button_start` (qui exige `ioIsActive == false`) ne peut plus le relancer
(dérivé, `AutoScenario.h:236-242`, `AutoScenario.cpp:329-345`).

### La persistance du drapeau

Le drapeau est lu **dans le constructeur d'`AutoScenario`**, à côté de `cycle` et `disabled`
(dérivé, `AutoScenario.cpp:54-56`, `:67`). C'est le seul point garanti **avant** tout
`SaveConfigIO()` : `ListeRoom::checkAutoScenario()` se termine par un `SaveConfigIO()`
(`ListeRoom.cpp:358-359`), donc un drapeau relu trop tard — ou pas relu du tout — serait **effacé du
disque au premier démarrage**, silencieusement et sans aucune action de l'utilisateur.

`setDisabledMissingIo(false)` **supprime le paramètre** au lieu d'écrire `"false"`, pour qu'un
scénario sain produise **exactement l'`io.xml` qu'il a toujours produit**
(dérivé, `AutoScenario.cpp:270-291`).

Ce drapeau n'est **pas** `disabled`. `disabled` est un choix utilisateur qui signifie « ne pas
jouer ce scénario sur son horaire », il est exposé sous le nom **`enabled`** (sa négation) dans le
payload et **réécrit par chaque `autoscenario modify`** (dérivé, `AutoScenario.h:93-94`,
`AutoScenario.cpp:186-196`, `IO/Scenario.cpp:291`, `JsonApi.cpp:2541-2542`). Le réutiliser aurait laissé
le premier `modify` de n'importe quel client relancer un scénario cassé.

### La réactivation manuelle : `autoscenario reenable`

Disponible sur les **deux transports** (dérivé, `JsonApiHandlerWS.cpp:680-681`,
`JsonApiHandlerHttp.cpp:1104-1105`).

```json
{ "msg": "autoscenario", "msg_id": "…", "data": { "type": "reenable", "id": "io_0" } }
```
(⚠️ en HTTP, `type` et `id` sont **à la racine** du document, pas sous `data` — voir
« Pièges pour les clients » plus bas.)

Trois issues (dérivé, `JsonApi.cpp:2630-2657`, `AutoScenario.cpp:293-327`) :

| Situation | Réponse |
|---|---|
| id inconnu, ou IO qui n'est pas un auto-scénario | `{"error": "wrong input"}` |
| encore cassé | `{"error": "scenario still references missing IOs: <ids>"}` |
| réparé, ou déjà actif (idempotent) | `{"success": "true"}` |

**Le refus est le but de la commande.** Une réactivation qui répondrait « succès » puis se
laisserait redésactiver par la passe de détection suivante reproduirait, d'un cran plus haut, le
no-op silencieux que cette décision supprime (dérivé, `AutoScenario.cpp:295-306`). C'est aussi
pourquoi c'est une **commande** et non un `set_param` : `set_param` ne sait pas refuser.

⭐ **Et le refus tient désormais après un aller-retour de lecture.** Relire un scénario cassé et
renvoyer le payload verbatim **ne l'assainit plus** : l'action morte revient dans le document, est
réécrite dans la définition, `broken` reste vrai et `reenable` **refuse** en nommant les ids
(épinglé par `tests/core/AutoScenarioMigration_test.cpp:1325`,
`ReadingBackAndEchoingThePayloadRestartsAnAmputatedScenarioWithTwoSuccessTrue`).

⚠️ **Ce nom de cas, et deux autres cités plus bas, décrivent le défaut D'AVANT et non ce qu'ils
assertent aujourd'hui.** C'est la convention de la série : un cas de caractérisation **garde son
nom** quand il bascule, pour qu'on puisse le suivre d'un ticket à l'autre. Chacun porte une bannière
`✅ FLIPPED` juste au-dessus de son corps. **Ne pas déduire un comportement du nom d'un cas.**

En cas de succès, le drapeau est levé, un `EventScenarioChanged` est émis, et **seul `io.xml`** est
réécrit — aucune règle n'a été touchée (dérivé, `AutoScenario.cpp:318-325`, `JsonApi.cpp:2653-2655`).

### La règle d'étape n'est plus détruite

`rules.xml` **conserve l'id mort verbatim**. La politique par défaut de `ListeRule::RemoveRule()` est
`RuleDetachPolicy::Disable` — la règle est conservée, l'ordre d'évaluation est strictement préservé,
et `ActionStd::SaveToXml()` réécrit la référence morte telle quelle
(dérivé, `ListeRule.cpp:437-461`, `ActionStd.cpp:360-372`).

---

## Le payload de scénario

Produit par `Scenario::toJson()` (dérivé, `IO/Scenario.cpp:270-326`). Servi par `autoscenario get`
et `autoscenario list`, sur les deux transports.

⭐ **Un seul schéma, dans les deux sens** : chaque clé émise par `get` est relue par `create` et
`modify`, et aucune clé lue n'est absente de `get`. Les clés **dérivées** (`category`, `broken`,
`disabled_missing_io`, `missing_ios`, `schedule`, et `resolved` sur chaque action) sont **acceptées
et ignorées** à l'écriture, ce qui est précisément ce qui garde l'aller-retour identitaire
(dérivé, `IO/Scenario.cpp:280-284`, `JsonApi.cpp:179-280`).

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
    "final_step": {
      "actions": [
        {
          "io": "e40c_string",
          "resolved": "true",
          "value": "done"
        }
      ]
    },
    "id": "io_0",
    "missing_ios": "",
    "name": "Soirée",
    "room_name": "E4.0c room",
    "room_type": "salon",
    "schedule": "false",
    "steps": [
      {
        "actions": [
          {
            "io": "e40c_bool",
            "resolved": "true",
            "value": "true"
          }
        ],
        "pause": "1.5",
        "step_id": "s0"
      },
      {
        "actions": [
          {
            "io": "e40c_target",
            "resolved": "true",
            "value": "true"
          },
          {
            "io": "e40c_int",
            "resolved": "true",
            "value": "42"
          }
        ],
        "pause": "0",
        "step_id": "s1"
      }
    ],
    "visible": "false"
  },
  "msg": "autoscenario",
  "msg_id": "e40c-get"
}
```

En HTTP, le même objet est renvoyé **sans l'enveloppe** `msg` / `msg_id` / `data`
(capturé, `tests/core/golden/e40c_http_autoscenario_get.json` — mêmes 14 clés, sans enveloppe).
`autoscenario list` renvoie ces mêmes objets dans un tableau sous `data.scenarios`
(capturé, `tests/core/golden/e40c_ws_autoscenario_list.json`).

**Toutes les valeurs sont des chaînes**, y compris les booléens et les pauses.

| Clé | Sens | Lue en écriture ? |
|---|---|---|
| `id` | id du `Scenario` | oui, pour désigner la cible de `modify` |
| `name` | nom du scénario (dérivé, `IO/Scenario.cpp:286`) | **oui** |
| `room_name` / `room_type` | la pièce qui contient l'IO (dérivé, `IO/Scenario.cpp:287-288`) | **oui, et obligatoires** |
| `visible` | visibilité de l'IO (dérivé, `IO/Scenario.cpp:289`) | **oui** |
| `cycle` | scénario cyclique (dérivé, `IO/Scenario.cpp:290`) | **oui** |
| `enabled` | **négation de `disabled`** : « ce scénario est joué sur son horaire » (dérivé, `IO/Scenario.cpp:291`) | **oui** |
| `schedule` | id de l'IO de plage horaire, ou la chaîne `"false"` s'il n'y en a pas (dérivé, `IO/Scenario.cpp:292-294`) | non — `add_schedule` / `del_schedule` |
| `category` | voir « Catégorisation » ; chaîne vide si aucune action | non |
| `broken` | porte 1, **dérivée et vivante** | non |
| `disabled_missing_io` | porte 2, **persistée et collante** | non |
| `missing_ios` | ids non résolus, `"id_a, id_b"`, vide s'il n'y a rien à réparer | non |
| `steps[]` | les étapes, et **rien d'autre** : `step_id`, `pause`, `actions` | **oui** |
| `final_step` | l'étape terminale, **un champ à part**, sans pause ni `step_id` | **oui** |

### ⭐ `final_step` est un champ séparé — l'invariant `+1` est inexprimable

`steps` contient les étapes et rien d'autre : **sa longueur est le nombre d'étapes**. L'étape
terminale est émise dans son propre champ (dérivé, `IO/Scenario.cpp:308-323`).

Le champ `steps_count` **n'existe plus**, et avec lui l'ancien piège `len(steps) == steps_count + 1`
que produisait une étape `end` ajoutée artificiellement en fin de tableau. Il n'y a plus non plus de
clé `step_type` ni de `step_pause` : une étape porte `step_id`, `pause` et `actions`.

Le cas dégénéré est visible au golden : un scénario sans étape rend `"steps": []` et
`"final_step": {"actions": []}`
(capturé, `tests/core/golden/e40c_ws_autoscenario_list.json`, extrait — second scénario).

⚠️ **Contraste à garder en tête** : dans `get_playlist`, `count` **est** bien la longueur du
tableau ; et le `total_count` d'`audio_db` est un compte **fourni par la base musicale**, sans
rapport garanti avec la longueur de `items`.

### ⭐ Une action n'est jamais escamotée

Une action est `{"io": …, "value": …, "resolved": …}`. Elle est émise **à sa place** même quand son
IO n'existe plus, avec `resolved: "false"` (dérivé, `IO/Scenario.cpp:243-256`).

Contraste mesuré entre le golden sain et le golden cassé — **même scénario, l'IO `e40c_target`
supprimé** (capturé, `e40c_ws_autoscenario_get.json` et `e40c_ws_autoscenario_get_broken.json`,
extraits de la deuxième étape) :

```
sain    : { "io": "e40c_target", "resolved": "true",  "value": "true" }
cassé   : { "io": "e40c_target", "resolved": "false", "value": "true" }
```

et, au niveau de l'en-tête :

```
broken               "false"  →  "true"
disabled_missing_io  "false"  →  "true"
missing_ios          ""       →  "e40c_target"
```

L'étape cassée rend donc bien **ses deux actions**, la morte comprise. C'est ce qui rend l'aller-
retour sûr : un client qui relit et renvoie **réécrit** l'action au lieu de la perdre.

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
clé par clé par `tests/core/ScenarioDisabledMissingIo_test.cpp:916` et, à travers l'API,
par `tests/core/JsonApiScenario_test.cpp:2090`)

**La troisième ligne est la raison d'être de la décision.** `broken=false` avec le drapeau encore
posé signifie « l'équipement est revenu, mais personne n'a encore confirmé que la séquence est de
nouveau celle qu'on croit ». C'est cet état qu'une interface doit transformer en bouton
« Réactiver ». **Avec une seule clé, il serait indistinguable d'un scénario sain**
(dérivé, `IO/Scenario.cpp:297-306`).

La quatrième ligne existe pour la raison symétrique : aucune des deux clés ne peut se cacher
derrière l'autre.

Contraste mesuré entre `e40c_ws_autoscenario_get_broken.json` et
`e40c_ws_autoscenario_get_repaired.json` (extraits, mêmes clés dans le même ordre) :

```
broken               "true"          →  "false"
disabled_missing_io  "true"          →  "true"
missing_ios          "e40c_target"   →  ""
```

⚠️ Un cas connu où `broken` est vrai **sans aucun id à nommer** : quand la casse vient de la
troisième raison d'`isBroken()` — une de nos règles détruite par un tiers. Voir plus haut.

### ⭐ L'aller-retour est une identité

`get` → `modify` du **même document** → `get` rend **exactement le même document**, y compris sur un
scénario cassé (épinglé par `tests/core/JsonApiScenario_test.cpp:2159`,
`AGetModifyGetRoundTripIsAnIdentityOnlyWhileNothingIsMissing` — nom d'avant la bascule, voir
l'avertissement plus haut : **les deux moitiés sont des identités**, la saine comme la cassée).

⚠️ **La seule brèche connue** : un IO scénario qui n'appartient à **aucune** pièce est émis avec
`room_name` et `room_type` vides, et ce document-là, renvoyé tel quel, est **refusé** — la pièce est
obligatoire et vérifiée (dérivé, `JsonApi.cpp:192-199`). L'aller-retour est une identité pour tout
scénario rangé dans une pièce, c'est-à-dire tous ceux que l'API sait créer.

### ⭐ Valider, puis muter

`create` et `modify` refusent le document **avant de toucher quoi que ce soit**
(dérivé, `JsonApi.cpp:179-280`, et la bannière de `JsonApi.cpp:2495-2498`). Le refus dit ce qui
cloche, sous la forme `invalid payload: …` :

| Refus | Site |
|---|---|
| le document n'est pas un objet | `JsonApi.cpp:181-185` |
| pièce inconnue ou absente | `JsonApi.cpp:192-199` |
| `steps` n'est pas un tableau | `JsonApi.cpp:205-209` |
| une étape n'est pas un objet | `JsonApi.cpp:216-220` |
| `step_id` invalide | `JsonApi.cpp:229-233` |
| `step_id` en double | `JsonApi.cpp:241-245` |
| `pause` qui n'est pas un nombre fini | `JsonApi.cpp:247-251`, `parseScenarioPause()` en `:121-135` |
| `actions` qui n'est pas un tableau, ou une action qui n'est pas un objet | `JsonApi.cpp:142-155` |
| une action qui ne nomme pas d'`io` | `JsonApi.cpp:158-163` |
| `final_step` qui n'est pas un objet | `JsonApi.cpp:266-270` |

⛔ **Un id qui ne résout pas n'est PAS un motif de refus.** Refuser rendrait un scénario cassé
impossible à renommer, et jeter l'action effacerait la seule trace de ce qui manque
(dérivé, `JsonApi.cpp:165-170`).

---

## Les sous-commandes `autoscenario`

Huit, identiques sur les deux transports (dérivé, `JsonApiHandlerWS.cpp:661-684`,
`JsonApiHandlerHttp.cpp:1085-1108`) :

| `type` | Effet |
|---|---|
| `list` | tous les auto-scénarios, sous `data.scenarios` |
| `get` | un scénario, par `id` |
| `create` | crée l'IO, la définition et les règles ; rend `{"id": …}` |
| `delete` | détruit le scénario, ses IOs internes et ses règles |
| `modify` | remplace la définition entière, puis régénère |
| `add_schedule` | crée l'IO de plage horaire ; rend `{"id": …}` |
| `del_schedule` | le supprime ; tolérant s'il n'y en a pas |
| `reenable` | lève le drapeau collant, ou **refuse** en nommant les ids |

### ⚠️ `autoscenario` est sous le contrôle de portée de service

Le message rejoint les sept autres commandes gardées (`set_param`, `del_param`, `audio_db`,
`set_timerange`, `eventlog`, `register_push`, `settings`) : une session de portée **service**
reçoit `{"error": "scope denied"}` (dérivé, `JsonApiHandlerWS.cpp:360-367`).

⚠️ **Le filtre porte sur le MESSAGE, pas sur la sous-commande** : `list` et `get` sont donc refusés
eux aussi, alors qu'ils ne font que lire. C'est délibéré — inventer une exception par sous-commande
créerait une seconde grammaire d'autorisation pour un seul domaine, là où tout le reste de l'API se
filtre au message. Épinglé par `tests/core/JsonApiScenario_test.cpp:858`
(`SetTimerangeAndAutoscenarioAreBothScopeDenied`), avec `get_timerange`, resté ouvert, comme
contraste.

⚠️ **Le transport HTTP n'a aucune notion de portée de service** : la même commande y passe avec les
identifiants administrateur.

### ⚠️ Une sous-commande inconnue répond une erreur

Un `type` inconnu, vide, absent, ou d'un type autre qu'une chaîne, répond
`{"error": "unknown autoscenario type"}` sur les deux transports
(dérivé, `JsonApiHandlerWS.cpp:682-683`, `JsonApiHandlerHttp.cpp:1106-1107`).

Côté HTTP, c'est **la réponse elle-même qui libère la socket** — `sendJson()` pose
`Connection: Close`. Auparavant ce transport ne répondait rien **et** ne fermait rien : le client
tenait la connexion jusqu'à son propre délai d'expiration (dérivé, le commentaire de
`JsonApiHandlerHttp.cpp:1075-1083`).

---

## Scenario (IO)

**Fichier :** [src/bin/calaos_server/IO/Scenario.h](../src/bin/calaos_server/IO/Scenario.h)

IO virtuel de type TBOOL représentant le bouton de déclenchement d'un scénario. Enregistré dans
`IOFactory` sous le type `"Scenario"`, avec `gui_type="scenario"` posé **inconditionnellement**
(dérivé, `IO/Scenario.cpp:33`, `:52`).

Par défaut `visible="true"` et `log_history="true"` s'ils ne sont pas déjà présents
(dérivé, `IO/Scenario.cpp:67-68`).

`set_value(true)` **est la coupure de T3.18**, et elle ferme les deux chemins d'entrée : le bouton
(ou `set_state`) **et** la planification — `time_start` n'a qu'une action, `ioScenario = "true"`,
donc elle passe ici aussi (dérivé, `IO/Scenario.cpp:193-238`, `AutoScenario.cpp:635`).

Trois détails qui comptent (dérivé, `IO/Scenario.cpp:197-222`) :
- la coupure ne porte **que** sur `val == true`. `set_value(false)` doit rester possible, sinon un
  scénario déjà lancé deviendrait impossible à arrêter ;
- elle renvoie **`true`** : c'est la convention de la garde `isEnabled()` juste au-dessus — la
  commande a été **acceptée** et n'a délibérément rien fait ;
- elle logue lequel des deux verrous a refusé, et nomme les IOs manquants.

Après un déclenchement accepté, la valeur retombe à `false` au bout de **250 ms**, pour simuler un
appui-relâchement. Le rappel est armé sur le jeton de durée de vie de l'IO, sans quoi un `deleteIO()`
dans cette fenêtre écrirait dans un `Scenario` détruit (dérivé, `IO/Scenario.cpp:232-235`).

⚠️ Un IO `type="scenario"` **sans** paramètre `auto_scenario` reste un IO parfaitement normal et
parfaitement déclenchable : aucun `AutoScenario` n'est construit, les deux portes ne sont donc pas
évaluées, et `set_value(true)` va droit à `EmitSignalIO()`
(dérivé, `IO/Scenario.cpp:60-61`, `:210`, `:224-225`). Il est en revanche **invisible** de
`autoscenario list` et `autoscenario get` (dérivé, `JsonApi.cpp:2379`, `:2389-2394`).

---

## InPlageHoraire (Plage horaire)

**Fichier :** [src/bin/calaos_server/IO/InPlageHoraire.h](../src/bin/calaos_server/IO/InPlageHoraire.h)

> ⚠️ **Portée de cette section.** E4.6 ne touche pas `InPlageHoraire` : ses citations ont été
> **recalées** contre le fichier actuel, son contenu n'a **pas** été réaudité. Ce qui suit reste
> l'état écrit par E4.5b, pas une relecture neuve.

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
minuit** (dérivé, `IO/InPlageHoraire.cpp:136-188`).

Le contrat exact : **une plage appartient au jour auquel elle est attachée et court jusqu'au
lendemain matin.**

> `23:00 → 01:00` **le lundi** est vraie de **lundi 23 h à mardi 1 h**, et **jamais** le lundi
> entre 00 h 00 et 01 h 00.

(dérivé, `IO/InPlageHoraire.cpp:159-184`, et la documentation d'IO exposée aux clients,
`IO/InPlageHoraire.cpp:38-39`)

L'implémentation évalue **deux jours** à chaque passe : les plages d'aujourd'hui (`previousDay =
false`, une plage inversée couvre alors `[début, fin de journée]`) **et** celles d'hier
(`previousDay = true`, elle couvre `[début de journée, fin]`)
(dérivé, `IO/InPlageHoraire.cpp:208-216`, `:172-183`).

Deux conséquences à connaître :

- **Une borne relative au soleil peut changer de côté selon la saison.** `coucher du soleil →
  23:00` est une plage ordinaire la plus grande partie de l'année, mais devient **wrappante** dès
  que le coucher passe après 23 h. La configuration n'a pas bougé, le comportement si. Le serveur
  le signale **une fois par plage**, en nommant l'IO et le jour de semaine concernés
  (dérivé, `IO/InPlageHoraire.cpp:169-170`, `TimeRange.h:102-110`, `TimeRange.cpp:127-137`).
- **Une plage dont une borne ne se parse pas n'est pas évaluée du tout.** Les bornes retomberaient
  silencieusement sur `00:00:00`, ce qui est indistinguable d'une vraie borne à minuit :
  `isValid()` sert précisément à faire la différence (dérivé, `IO/InPlageHoraire.cpp:151-154`,
  `TimeRange.h:96-100`).

Enfin, si le **mois** courant n'est pas coché, l'IO est toujours `false`, quelles que soient les
plages (dérivé, `IO/InPlageHoraire.cpp:201-202`).

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
`IO/InPlageHoraire.cpp:253-255`).

⚠️ Un attribut `start_offset` / `end_offset` **vide** ne remet pas l'offset à zéro : il **garde** la
valeur en place. La borne à `1` de la classe et le clamp qui suit ne savent pas réparer un `0`, et
un attribut vide annulait donc l'offset solaire (T3.25, dérivé, `IO/InPlageHoraire.cpp:249-255`).

### Format XML

Les jours sont des éléments **en français**, et les mois un attribut de l'input
(dérivé, `IO/InPlageHoraire.cpp:368-415` en lecture, `:417-469` et `:471-502` en écriture) :

```xml
<calaos:input id="id-plage" type="InPlageHoraire" months="110000000001">
  <calaos:lundi>
    <calaos:plage start_type="0" start_hour="23" start_min="0" start_sec="0"
                  end_type="0" end_hour="1" end_min="0" end_sec="0"/>
  </calaos:lundi>
</calaos:input>
```

Éléments de jour reconnus : `calaos:lundi`, `calaos:mardi`, `calaos:mercredi`, `calaos:jeudi`,
`calaos:vendredi`, `calaos:samedi`, `calaos:dimanche` ; tout autre nom est ignoré
(dérivé, `IO/InPlageHoraire.cpp:395-412`). Un jour sans plage **n'écrit pas son élément**
(dérivé, `IO/InPlageHoraire.cpp:419`).

`months` est une chaîne de **12 caractères**, **janvier à gauche** : elle est la représentation du
`bitset<12>` **inversée** à l'écriture comme à la lecture
(dérivé, `IO/InPlageHoraire.cpp:375-393`, `:482-491`). Une chaîne illisible active **tous** les
mois (dérivé, `IO/InPlageHoraire.cpp:386-392`).

⚠️ Les bornes solaires ne sont écrites **que** si l'offset est non nul : un type `1`/`2`/`3` avec
`0:0:0` ne réécrit ni les heures ni l'offset (dérivé, `IO/InPlageHoraire.cpp:436-447`,
`:456-467`).

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
`TimeRange` (où `SUNDAY = 0`, `MONDAY = 1`, `SATURDAY = 6`, `TimeRange.h:58`). Le producteur émet
`day + 1` sur un index 0 = lundi (dérivé, `JsonApi.cpp:2251-2262`, `TimeRange.cpp:432-436`) ; le
consommateur relit `"1"` → lundi … `"7"` → dimanche (dérivé, `JsonApi.cpp:2328-2334`). **Toute
table écrite depuis les noms de l'enum C++ sera fausse.**

⚠️ **L'ordre du tableau `ranges` est sémantique** : il est aplati sur les sept jours dans l'ordre
lundi → dimanche, et la **position** d'une entrée porte donc son jour au même titre que sa clé
`day` (dérivé, `JsonApi.cpp:2220-2227`, `:2251-2263`).

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
  lecture, et un `ranges` absent — ou qui n'est pas un tableau — reste un no-op sans erreur. Une
  requête qui ne voulait changer que `months` vide donc l'agenda
  (dérivé, `JsonApi.cpp:2304-2317`).
- **Un `months` plus court que 12 est accepté sans erreur** (zéro-extension implicite du
  `bitset<12>`), ce qui éteint silencieusement les mois manquants
  (dérivé, `JsonApi.cpp:2338-2353`).
- **Un `day` hors 1..7 est silencieusement perdu** : sept `if` indépendants, aucun `else`, aucune
  erreur, et le client reçoit un succès (dérivé, `JsonApi.cpp:2328-2334`).

⚠️ Modifier la plage horaire d'un scénario émet **deux** événements : `timerange_changed` sur l'IO,
puis `scenario_changed` sur le scénario qui le porte — retrouvé par **lookup** dans le cache, jamais
par un back-pointer (dérivé, `JsonApi.cpp:2355-2363`, `autoScenarioOfTimeRange()` en `:282-298`).

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
`AutoScenario` utilise pour poser la pause d'une étape (dérivé, `AutoScenario.cpp:622`).

L'IO passe à `true` à l'expiration et à `false` au démarrage
(dérivé, `IO/InputTimer.cpp:47-49`).

---

## Gestion du cache de scénarios

`ListeRoom` maintient un cache des scénarios, alimenté par le constructeur et le destructeur
d'`AutoScenario` (dérivé, `AutoScenario.cpp:76`, `:81`) :

```cpp
void ListeRoom::addScenarioCache(Scenario *sc);
void ListeRoom::delScenarioCache(Scenario *sc);
list<Scenario *> ListeRoom::getAutoScenarios();
void ListeRoom::checkAutoScenario();      // au démarrage : régénère + détecte + sauvegarde
void ListeRoom::refreshBrokenScenarios(); // T3.18 : pose le drapeau, ne le lève jamais
```
(dérivé, `ListeRoom.h:169-172`, `:233`)

`checkAutoScenario()` ne tourne **qu'une fois, au démarrage** (armé par
`Timer::singleShot(0.1, …)`, `main.cpp:198`) et fait, dans cet ordre
(dérivé, `ListeRoom.cpp:332-360`) :

1. `reportAutoScenariosLostByUpload()` — **avant** que le générateur ne touche à quoi que ce soit,
   pour mesurer sur la configuration telle qu'elle a été chargée ;
2. `rebuildRules()` sur chaque scénario du cache ;
3. `refreshBrokenScenarios()`, **avant** les deux sauvegardes qui persistent le drapeau ;
4. `SaveConfigIO()` + `SaveConfigRule()`.

> ⛔ **Il n'y a plus de balayage d'orphelines, et il ne faut pas en remettre.** Il en existait un,
> qui détruisait toute règle portant le param `auto_scenario` qu'aucun `AutoScenario` n'avait
> adoptée — et le `SaveConfigRule()` deux lignes plus bas persistait la destruction, au premier
> démarrage, sans aucune action utilisateur. Il n'avait de sens que tant que les règles étaient
> **adoptées** ; elles sont **régénérées**, donc une règle non revendiquée est la donnée de
> quelqu'un, pas un déchet (dérivé, `ListeRoom.cpp:347-353`).

### Défense en profondeur sur le chemin de téléversement

`calaos_installer` ne pilote pas les scénarios par l'API : il télécharge `io.xml` et `rules.xml`,
les régénère entièrement depuis son propre modèle, et les renvoie. Trois niveaux répondent à ça,
tous côté serveur :

- **Niveau 0 — l'auto-réparation.** La définition n'est **pas** dans `rules.xml`, et les règles sont
  régénérées à chaque chargement : ce qu'un aller-retour ampute est **réécrit** au démarrage suivant.
- **Niveau 1 — la sauvegarde avant écrasement.** Un `config put` appelle `BackupFiles()` **avant**
  d'écrire le moindre fichier reçu (dérivé, `JsonApiHandlerHttp.cpp:768-771`).
- **Niveau 2 — l'alerte.** Au même instant, le serveur **enregistre** les scénarios qu'il s'apprête
  à écraser (uid, nom, nombre d'étapes) ; le démarrage suivant **consomme** cet enregistrement,
  compare, et lève **une** alerte de configuration nommant ce qui a disparu
  (dérivé, `JsonApiHandlerHttp.cpp:773-779`, `ListeRoom.cpp:380-463`).

⛔ **Rien n'est jamais refusé** : supprimer un scénario depuis l'installeur est légitime, et un
serveur qui refuserait un téléversement au motif qu'il y manque quelque chose qu'il connaissait
enfermerait l'utilisateur dans sa configuration précédente. **Le serveur signale, il n'arbitre pas.**

⛔ **L'enregistrement n'est PAS un troisième fichier de configuration.** C'est une miette que le
serveur écrit pour lui-même sous `backups/`, **supprimée à la lecture**, et dont l'absence signifie
simplement « aucun téléversement à expliquer » (dérivé, `ListeRoom.cpp:37-46`, `:424-428`).
Il **fallait** qu'elle traverse le redémarrage : un `config put` réussi arrête la boucle
d'événements dès la réponse sortie, alors que le canal d'alerte est différé de 30 s — une alerte
mise en file pendant le put est jetée à tous les coups
(dérivé, `JsonApiHandlerHttp.cpp:773-778`, `CalaosConfig.cpp:41`, `:233`).

Texte exact de l'alerte (dérivé, `ListeRoom.cpp:449-462`) :

```
The configuration that was uploaded no longer carries scenario data this server had:

- scenario 'Soirée' (as_0) is gone from the uploaded configuration
- scenario 'Réveil' (as_1) lost 2 of its 3 steps

Nothing was refused and nothing was undone: the configuration is the one that was uploaded.
The one that was in place before it was backed up first, under <config>/backups.
```

Le **nom** d'abord, l'uid en repli : le nom est ce que l'utilisateur reconnaît, l'uid est ce que le
modèle range (dérivé, `ListeRoom.cpp:482-488`).

---

## Persistance

Deux fichiers, et **aucun des deux n'est `local_config.xml`** :

- **`io.xml`** — les pièces, leurs IOs, et donc le `Scenario` avec **toute sa définition** portée par
  ses params (`autoscenario_*`, `as_*`), plus les paramètres historiques `auto_scenario`, `cycle`,
  `disabled` et, le cas échéant, **`disabled_missing_io`** ; ainsi que les IOs internes du scénario,
  rangés dans la **pièce du `Scenario`** (pas dans une pièce dédiée)
  (dérivé, `Constants.h:37`, `IO/Scenario.cpp:185-191`, `AutoScenario.cpp:394-417`, `:513`).
- **`rules.xml`** — les règles générées, qui sont une **projection** : les détruire ne coûte pas la
  définition, elles reviennent au chargement suivant (dérivé, `Constants.h:38`).

Ce qui identifie une règle d'auto-scénario **dans le fichier**, ce sont ses **attributs** :
`auto_scenario="<scenario_id>"`, **`autoscenario_uid="<uid>"`**, `auto_scenario_type`, et
`auto_scenario_step` pour les étapes (dérivé, `AutoScenario.cpp:474-483`, `:609`). Tous les
attributs d'un nœud `<calaos:rule>` sont chargés et réécrits tels quels
(dérivé, `Rule.cpp:311-315`, `:361-366`).

⚠️ **Il n'y a plus aucun drapeau en mémoire côté règle.** L'ancien `Rule::auto_sc_mark` /
`isAutoScenario()` a été supprimé : l'appartenance d'une règle à un scénario se lit **uniquement**
dans ses params, par les deux index non-possédants de `ListeRule` — `getRuleAutoScenario()` (clé
`auto_scenario`) et `getRulesOfScenarioUid()` (clé `autoscenario_uid`)
(dérivé, `ListeRule.cpp:502-530`, `ListeRule.h:214-222`).

---

## ⚠️ Pièges pour les clients

Écarts **mesurés** et **gelés** — c'est-à-dire présents dans le code tel qu'il est livré.

1. ⛔ **`disabled` n'est plus lu, et son absence n'est pas neutre.** Un client qui envoie encore
   `{"disabled": "false"}` en croyant activer son scénario ne reçoit **aucune erreur** : la clé est
   ignorée, `enabled` est absent, et **`enabled` vaut `false` par défaut**. Le scénario est donc
   créé — ou laissé — **désactivé**, en silence (dérivé, `JsonApi.cpp:114-117`, `:190`). C'est le
   seul endroit où l'ancien nom échoue sans le dire.
2. ⛔ **La pièce est obligatoire et vérifiée**, sur `create` comme sur `modify` : un `room_name` /
   `room_type` qui ne désigne aucune pièce fait répondre
   `invalid payload: no room "…" of type "…"` (dérivé, `JsonApi.cpp:192-199`). Un `modify` partiel
   — renommer seulement — qui passait autrefois échoue désormais s'il ne nomme pas correctement la
   pièce. Corollaire : un scénario qui n'appartient à **aucune** pièce est émis avec les deux clés
   vides et son propre payload est alors **refusé** au renvoi.
3. **Les arguments ne sont pas au même endroit selon le transport** : sous `data` en WebSocket,
   **à la racine** en HTTP — et l'argument ainsi déplacé est le `type` lui-même, c'est-à-dire le
   sélecteur de sous-commande (dérivé, `JsonApiHandlerWS.cpp:663` vs
   `JsonApiHandlerHttp.cpp:1087`). Une requête HTTP construite comme une requête WebSocket répond
   désormais `unknown autoscenario type` au lieu de ne rien répondre.
4. **`list` et `get` sont refusés aux sessions de portée service**, alors qu'ils ne font que lire :
   le contrôle de portée porte sur le message entier (voir plus haut).
5. **`category` n'est pas un diagnostic.** Elle est calculée depuis les règles et ne compte pas une
   action dont l'IO manque ; seul `missing_ios` dit ce qui manque.
6. **`step_id` est opaque.** Il est stable et jamais réutilisé, mais rien de sa forme n'est un
   contrat. Un `step_id` renvoyé par le client est accepté s'il respecte `[A-Za-z0-9_]`, refusé s'il
   est en double, et **alloué par le serveur** s'il est absent (dérivé, `JsonApi.cpp:223-245`).
7. **Poser `disabled_missing_io` à la main sur un scénario sain n'a pas d'effet immédiat.** Le
   booléen en mémoire n'est pas touché : le scénario **continue de tourner** jusqu'au prochain
   redémarrage, où il se retrouve désactivé (dérivé, `AutoScenario.cpp:67` — le param n'est relu
   qu'au chargement). L'écriture ne prend effet qu'au reboot, alors que la lecture est immédiate.
8. **La valeur d'une action est du texte quelconque**, octet zéro compris : elle traverse
   `toJson()` **entière** et ressort échappée par le dump (dérivé, `IO/Scenario.h:88-95`,
   `IO/Scenario.cpp:252`, épinglé par `tests/core/JsonApiScenarioWireBytes_test.cpp:671`,
  `AnEmbeddedNulInAnActionIsCarriedWholeByScenarioToJson`).
