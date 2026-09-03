# Rules Engine — Règles, Conditions, Actions

> **Provenance.** Chaque affirmation vérifiable de ce document porte sa source :
> `(dérivé, Fichier.cpp:L-L)` pour une lecture du code, `(capturé, <golden>)` pour un payload
> produit par le code. Les extraits de golden sont marqués `intégral` ou `extrait`.

## Vue d'ensemble

Le moteur de règles est le cœur de l'automatisation Calaos. Une **règle** (`Rule`) contient :
- une liste de **conditions** (`Condition`) — toutes doivent être vraies
  (dérivé, `Rule.cpp:155-165` — **toutes** sont évaluées, sans court-circuit, et une seule fausse suffit) ;
- une liste d'**actions** (`Action`) — exécutées si les conditions sont remplies.

L'**ordre d'insertion est porteur de sens** : c'est l'ordre d'évaluation des conditions et
d'exécution des actions (dérivé, `Rule.h:76-81`).

Les règles sont déclenchées par :
1. le signal d'un IO qui change de valeur → `ListeRule::ExecuteRuleSignal(id)`
   (dérivé, `ListeRule.cpp:388-408`) ;
2. un balayage périodique des IOs qui se sont **eux-mêmes inscrits** dans `in_event` —
   `InputTime`, `InputAnalog` et `InPlageHoraire`, et **eux seuls** (dérivé, recherche exhaustive
   de `ListeRule::Instance().Add(this)` sur `src/bin/calaos_server/IO/` :
   `InputTime.cpp:52`, `InputAnalog.cpp:64`, `InPlageHoraire.cpp:48`) →
   `ListeRule::RunEventLoop()`, armé par un `Timer` de **0,1 s**
   (dérivé, `main.cpp:194`, `ListeRule.cpp:208-221`).
   ⚠️ **`InputTimer` n'en fait pas partie** : il porte son propre `Timer` et appelle
   `hasChanged()` lui-même à l'expiration (dérivé, `IO/InputTimer.cpp:140-190`) ;
3. au démarrage, pour les règles portant une `ConditionStart` → `ListeRule::ExecuteStartRules()`
   (dérivé, `ListeRule.cpp:464-487`).

⚠️ `RunEventLoop()` n'est **pas** une boucle : c'est **une passe** sur la liste `in_event`, qui
appelle `hasChanged()` sur chaque IO enregistré. C'est le `Timer` de `main.cpp` qui la répète.

---

## ListeRule

**Fichier :** [src/bin/calaos_server/ListeRule.h](../src/bin/calaos_server/ListeRule.h)

Singleton. **Propriétaire** de toutes les règles : `std::vector<std::unique_ptr<Rule>>`
(dérivé, `ListeRule.h:50-70`). Rien d'autre dans l'arbre ne détruit une `Rule`.

```cpp
ListeRule &lr = ListeRule::Instance();
lr.Add(rule);                        // PREND la propriété, ajout en fin de liste
lr.ExecuteRuleSignal("id-io");       // marche sur la liste pour cet id d'IO
lr.ExecuteStartRules();              // au boot, règles portant une ConditionStart
lr.RunEventLoop();                   // une passe sur les IOs temporels enregistrés
```
(dérivé, `ListeRule.h:134`, `:181`, `:188`, `:212`)

Deux conteneurs auxiliaires, tous deux **non propriétaires** (dérivé, `ListeRule.h:57-76`) :
- `in_event` — les IOs qui se sont eux-mêmes inscrits au balayage périodique ;
- `rules_scenarios` — index des règles d'auto-scénario. Il admet **deux clés** : une règle y entre
  si elle porte `auto_scenario` **ou** `autoscenario_uid` (dérivé, `ListeRule.cpp:120-126`). Deux
  lectures le servent : `getRuleAutoScenario()` (par `auto_scenario`, la clé historique) et
  `getRulesOfScenarioUid()` (par l'uid de la définition, la seule qui autorise une destruction)
  (dérivé, `ListeRule.cpp:502-530`, `ListeRule.h:214-222`). ⚠️ Un uid **vide** n'est pas une
  identité : `getRulesOfScenarioUid("")` rend une liste vide plutôt que toutes les règles dont le
  param est absent (dérivé, `ListeRule.cpp:521-524`).

### Réentrance

Une action modifie un IO, qui re-signale dans `ExecuteRuleSignal()` **pendant** que la marche
courante est encore sur la pile. Les déclencheurs reçus dans cette fenêtre sont **différés** dans
`pendingTriggers` et rejoués par la marche la plus externe quand elle rend la main
(dérivé, `ListeRule.cpp:388-408`, `:377-386`, `ListeRule.h:95`). Les conditions script asynchrones
ne tiennent **pas** ce verrou : leur protection est le jeton de durée de vie de la `Rule`
(`Rule::aliveToken()`, dérivé, `ListeRule.cpp:332-344`, `Rule.h:114-122`, `:131-132`).

---

## Rule

**Fichier :** [src/bin/calaos_server/Rule.h](../src/bin/calaos_server/Rule.h)

```cpp
class Rule {
    vector<std::unique_ptr<Condition>> conds;   // la Rule est LE propriétaire
    vector<std::unique_ptr<Action>> actions;    // idem
    Params params;                              // "type", "name", + attributs XML libres
    vector<string> missingIoIds;                // E4.2e — voir « Règle désactivée »
    std::shared_ptr<bool> alive;                // jeton pour les callbacks asynchrones
};
```
(dérivé, `Rule.h:80-122`)

⚠️ **Il n'y a plus de drapeau « règle d'auto-scénario » en mémoire.** L'ancien `Rule::auto_sc_mark`
et ses accesseurs `isAutoScenario()` / `setAutoScenario()` ont été supprimés : l'appartenance d'une
règle à un scénario se lit **uniquement** dans ses params, par les deux index de `ListeRule`
ci-dessus. Un bit posé à chaque reconstruction et jamais sérialisé donnait deux sources de vérité
pour la même question.

`get_condition(i)` / `get_action(i)` rendent des pointeurs **non propriétaires**, valides tant que
la `Rule` les détient (dérivé, `Rule.h:148-150`).

### Exécution

```
Rule::Execute()
  → [refus si isDisabled()]
  → Rule::CheckConditions()       // refuse aussi si isDisabled()
    → chaque Condition::Evaluate()
  [si toutes vraies]
  → Rule::ExecuteActions()        // refuse aussi si isDisabled()
    → chaque Action::Execute()
```
(dérivé, `Rule.cpp:120-138`, `:140-166`, `:233-265`)

⚠️ `Rule::Execute()` n'est employée **que** par `ExecuteStartRules()`
(dérivé, `ListeRule.cpp:485`). Le chemin de déclenchement ordinaire ne passe pas par elle : il
collecte les règles avec `collectTriggeredRules()` — qui évalue les conditions — puis appelle
directement `ExecuteActions()` (dérivé, `ListeRule.cpp:316-330`).

Version asynchrone, pour les règles portant une `ConditionScript` :
```cpp
rule->CheckConditionsAsync([](bool ok) { /* … */ }, triggerId);
```
Elle évalue d'abord toutes les conditions **non script** et sort court au premier échec, puis lance
tous les scripts en parallèle (dérivé, `Rule.cpp:168-231`).

---

## ⚠️ Règle désactivée : une dépendance manquante arrête la règle (E4.2e, décision utilisateur)

**C'est le changement de comportement le plus important de ce moteur.** Il vient d'une décision
utilisateur du 2026-08-15.

### Le problème

Avant, une condition dont l'IO n'existait plus était **rejetée au chargement** et la règle
continuait de tourner **amputée**. Une conjonction amputée est **plus permissive** que ce que
l'utilisateur a écrit : `absence == true ET heure > 22h → tout éteindre`, privé de son IO de
présence, devient `heure > 22h → tout éteindre`, et se déclenche **tous les soirs**
(dérivé, `Rule.h:85-97`).

**Raison de la décision : mieux vaut qu'une règle ne fasse rien de visible que quelque chose de
faux.**

### Le contrat

Une règle dont **au moins une** condition ou action référence un IO introuvable est **entièrement
désactivée**. `Rule::isDisabled()` est vrai dès que la liste `missingIoIds` est non vide
(dérivé, `Rule.h:166`).

Une règle désactivée reste **chargée, visible et intacte** — elle est toujours dans `ListeRule`,
toujours sérialisée avec toutes ses conditions et actions. Elle est seulement exclue de
l'exécution, et le refus est posé à **six endroits indépendants** :

| Porte | Emplacement | Effet |
|---|---|---|
| Collecte des déclencheurs | `ListeRule.cpp:241-242` | la règle n'est même pas regardée |
| `Rule::Execute()` | `Rule.cpp:124-130` | refus + log |
| `Rule::CheckConditions()` | `Rule.cpp:147-153` | renvoie **false** (fail closed) |
| `Rule::CheckConditionsAsync()` | `Rule.cpp:171-178` | `cb(false)` **avant** tout spawn de script |
| `Rule::ExecuteActions()` | `Rule.cpp:239-245` | aucune action n'est exécutée |
| `ExecuteStartRules()` | `ListeRule.cpp:472-473` | la règle ne tourne pas non plus au boot |

La porte de `CheckConditions()` est indispensable : une règle dont **toutes** les conditions
avaient été rejetées répondait `true` — pour zéro condition (dérivé, `Rule.cpp:142-146`).

### La référence est conservée verbatim

Les loaders `ConditionStd`, `ConditionOutput`, `ConditionScript` et `ActionStd` **n'amputent
plus** : ils gardent l'id, l'opérateur, la valeur et `val_var` tels que le fichier les décrit, et
enregistrent l'id dans `missingIoIds` (dérivé, `ConditionStd.cpp:476-513`,
`ActionStd.cpp:322-348`, `ConditionOutput.cpp:237-273`, `ConditionScript.cpp:118-143`).

Conséquence : **`SaveToXml()` réécrit la référence morte à l'identique** — le id **est** la
référence, la sauvegarde ne résout plus rien (dérivé, `ConditionStd.cpp:525-539`,
`ActionStd.cpp:360-372`). Réécrire la configuration ne détruit donc plus ce que l'utilisateur avait
écrit ; c'était exactement ce qui faisait disparaître l'id à la sauvegarde suivante.

### Un id vide est une dépendance manquante, sous une sentinelle

Une balise `<calaos:input>` sans attribut `id`, ou avec `id=""`, est traitée **avant** toute
tentative de résolution et enregistrée sous la sentinelle **`"<no id>"`**
(dérivé, `Condition.h:91`, `Action.h:75`, `ConditionStd.cpp:431-452`, `ActionStd.cpp:279-292`).

Deux raisons, toutes deux mesurées :
- `Add("")` est un no-op, donc laisser passer un id vide laissait la condition avec **zéro
  input** — et un `ConditionStd` sans input **s'évalue à true** ;
- la recherche de compatibilité audio/caméra compare contre `get_param("iid")`, qui répond `""`
  pour un IO qui n'a pas ce paramètre : un id vide aurait **matché le premier IO audio ou caméra
  de la configuration** (dérivé, `ConditionStd.cpp:433-445`).

Le même id vide arrivant par le chemin à chaud passe par `Rule::markIoMissing()`, qui le range
sous la même sentinelle plutôt que de le laisser tomber (dérivé, `Rule.cpp:96-105`).

### Ce n'est pas persisté — et donc la réactivation demande un rechargement

`missingIoIds` n'est **jamais écrit** dans `rules.xml` : l'état est **re-dérivé de la
configuration à chaque chargement** (dérivé, `Rule.h:106-109` ; aucune écriture de `missingIoIds`
dans `Rule::SaveToXml()`, `Rule.cpp:357-375`).

⚠️ **Précision importante, souvent mal comprise.** La liste est **append-only** : aucun chemin du
code ne la vide (vérifié : les seules écritures sont les `push_back` de
`Rule.cpp:92` et de `Condition.h:78` / `Action.h:67`). Remettre l'IO **pendant que le serveur
tourne** ne réactive donc **pas** la règle. Ce qui la réactive, c'est le **rechargement de la
configuration** — c'est-à-dire un redémarrage de `calaos_server` : le loader ne trouve plus d'id
non résolu, `missingIoIds` reste vide, la règle repart. `Rule.h:106-109` le dit ainsi :
« Restore the IO, reload, and the rule runs again. »

### Deux chemins d'entrée dans l'état désactivé

| Chemin | Quand | Mécanisme |
|---|---|---|
| **Chargement** | l'id n'existe pas dans `io.xml` | `Condition/Action::addMissingIo()` puis `Rule::AddCondition()/AddAction()` remontent l'id (dérivé, `Rule.cpp:50-83`) |
| **À chaud** | l'IO est supprimé alors que le serveur tourne | `ListeRule::RemoveRule(io)` avec la politique par défaut **`RuleDetachPolicy::Disable`** : la règle est **conservée**, seulement marquée par `Rule::markIoMissing(id)` (dérivé, `ListeRule.cpp:437-461`, `Rule.h:36-56`) |

C'est **T3.18** qui a introduit `RuleDetachPolicy`. Auparavant `RemoveRule()` **détruisait** les
règles citant l'IO. `Destroy` reste la politique **explicite** de quatre sites de démontage —
`AutoScenario::deleteAll()`, `AutoScenario::deleteSchedule()`, l'IO d'activation d'horaire résiduel
de `AutoScenario::rebuildRules()`, et `Room::~Room` —, qui reconstruisent aussitôt des règles aux
**mêmes ids** et produiraient sinon des copies désactivées que rien ne ramasse
(dérivé, `Rule.h:46-51`, `Scenario/AutoScenario.cpp:359-377`, `:561-571`, `:858-869`).

Les deux chemins produisent le **même** stockage et la **même** déduplication, si bien qu'un
cycle sauvegarde/rechargement reproduit l'état par le chemin de chargement, sans **aucun** nouveau
champ stocké côté règle (dérivé, `Rule.h:171-179`).

### Le diagnostic est remonté à l'utilisateur

`ListeRule::getDisabledRules()` donne la vue programmatique (dérivé, `ListeRule.h:230`).
`Config::LoadConfigRule()` en fait, au démarrage, un **rapport agrégé** envoyé par le canal
mail + push déjà utilisé pour les configurations corrompues (dérivé, `CalaosConfig.cpp:402-457`).

Le rapport nomme la règle **et son scénario** quand la règle porte le paramètre
**`autoscenario_uid`** — `<id>_step` ne dit rien à personne — et ajoute alors un avertissement
spécifique (dérivé, `CalaosConfig.cpp:431-444`) :

> `A SCENARIO is among them: a scenario disabled this way stays disabled even once the missing IOs
> are back, and has to be re-enabled explicitly (autoscenario reenable).`

(capturé, `CalaosConfig.cpp:451-453`, intégral)

⚠️ **Le prédicat est l'uid, pas le marqueur historique `auto_scenario`.** Ce dernier vit aussi sur
des règles qu'aucun scénario ne revendique plus ; les annoncer comme les étapes d'un scénario
envoyait l'utilisateur chercher un scénario introuvable. Seule une règle que la projection a écrite
porte l'uid (dérivé, `CalaosConfig.cpp:426-431`). Une règle héritée est **toujours signalée**, mais
comme une règle ordinaire — rien n'est masqué.

⚠️ **Et le paragraphe « *A SCENARIO is among them* » n'est ajouté que si au moins une des règles
désactivées est réellement une étape de scénario** (dérivé, `CalaosConfig.cpp:449-454`) : une règle
ordinaire, elle, se remet à fonctionner toute seule après un rechargement.

C'est la différence de fond avec les règles ordinaires : voir
[04_scenarios.md](04_scenarios.md#-scénario-désactivé-t318-décision-utilisateur).

---

## Conditions

**Dossier :** [src/bin/calaos_server/Rules/](../src/bin/calaos_server/Rules/)

### Hiérarchie et valeurs de `type` dans le XML

| Classe | `type=` dans `rules.xml` | Rôle |
|---|---|---|
| `ConditionStd` | `"standard"` **ou attribut absent** | compare la valeur d'un IO à une constante ou à un autre IO |
| `ConditionStart` | `"start"` | vraie une seule fois, au premier `Evaluate()` |
| `ConditionScript` | `"script"` | évalue un script Lua, de façon **asynchrone** |
| `ConditionOutput` | `"output"` | compare l'état d'une **sortie** |

(dérivé, `RulesFactory.cpp:64-86` pour la fabrique, et les `SaveToXml()` respectifs :
`ConditionStd.cpp:522`, `ConditionStart.cpp:61`, `ConditionScript.cpp:152`,
`ConditionOutput.cpp:283`)

⚠️ Ce sont bien `"standard"`, `"start"`, `"script"`, `"output"` — **pas** les noms de classe C++.
Un `type` inconnu fait renvoyer `NULL` par la fabrique et la condition est simplement ignorée.

### ConditionStd

**Fichier :** [src/bin/calaos_server/Rules/ConditionStd.h](../src/bin/calaos_server/Rules/ConditionStd.h)

Condition la plus courante. Compare des IOs entre eux ou à des valeurs fixes.

```cpp
class ConditionStd {
    std::vector<std::string> inputIds;  // E4.2c : référencés PAR ID, jamais par IOBase*
    Params params;      // valeur de comparaison, clé = id de l'input
    Params ops;         // opérateur, clé = id de l'input
    Params params_var;  // id d'un AUTRE IO servant de valeur de comparaison
    bool trigger = true;// si false, cet input ne déclenche pas la règle
};
```
(dérivé, `ConditionStd.h:42-67`)

Les IOs sont référencés **par id**, jamais par pointeur : un pointeur stocké devenait pendant dès
que l'IO était détruit par un chemin ne passant pas par `RemoveRule()`. La résolution se fait une
fois par input et par évaluation, dans `Evaluate()` (dérivé, `ConditionStd.h:44-56`,
`ConditionStd.cpp:112-131`).

**Opérateurs réellement acceptés** (dérivé, `ConditionStd.cpp:283-388`, `ConditionEval`) :

| Type d'IO | Opérateurs |
|---|---|
| `TBOOL` | `==`, `!=` — tout autre opérateur est **rejeté avec une erreur** et rend `false` |
| `TSTRING` | `==`, `!=` — idem |
| `TINT` | `==`, `!=`, **`SUP`**, **`SUP=`**, **`INF`**, **`INF=`** |

⚠️ Les comparaisons d'ordre s'écrivent **`SUP` / `SUP=` / `INF` / `INF=`**, pas `>` / `>=` / `<` /
`<=`. Un `>` dans `rules.xml` tombe dans le `return false` final de `evalOperator(double,…)`
(dérivé, `ConditionStd.cpp:310-361`) : la condition est silencieusement fausse pour toujours.

**Valeur spéciale `changed`.** Pour les trois types, `val="changed"` signifie « déclencher sur
n'importe quel changement » : le terme n'est pas comparé, il est neutre
(dérivé, `ConditionStd.cpp:155-165`, `:191-201`, `:219-227`).

**Échec fermé.** Un input dont l'id ne résout plus rend la condition **fausse**, avec une erreur
dans le log. Il n'est jamais sauté silencieusement — sauter un terme d'une conjonction, c'est la
rendre plus permissive (dérivé, `ConditionStd.cpp:121-131`).

**Exemple XML** (dérivé, `ConditionStd.cpp:519-542`, c'est la forme que le serveur écrit) :
```xml
<calaos:condition type="standard" trigger="true">
  <calaos:input id="id-abc" oper="==" val="true"/>
</calaos:condition>
```
Attributs : `id`, **`oper`**, **`val`**, et `val_var` (optionnel, id d'un autre IO servant de
valeur de comparaison — écrit seulement s'il est non vide).

### ConditionOutput

Compare l'état actuel d'une **sortie**. Elle ne porte qu'**un seul** output — `outputId`, `ops`,
`params`, `params_var` sont des scalaires, pas des listes (dérivé, `ConditionOutput.cpp:280-294`).

Pour les IOs `TSTRING`, la comparaison est déléguée à `IOBase::check_condition_value(val, equal)`,
que redéfinissent `OutputShutter`, `OutputShutterSmart`, `OutputLightDimmer` et `OutputLightRGB`
(dérivé, `ConditionOutput.cpp:201-210`, `IOBase.h:181`). Seuls `==` et `!=` sont acceptés dans ce
cas (dérivé, `ConditionOutput.cpp:203-207`).

```xml
<calaos:condition type="output" trigger="true">
  <calaos:output id="id-volet" oper="==" val="up"/>
</calaos:condition>
```
(dérivé, `ConditionOutput.cpp:280-294`)

### ConditionStart

Vraie **une seule fois**, au premier appel à `Evaluate()`, qu'elle latche à `false`
(dérivé, `ConditionStart.cpp:36-51`). En pratique cela arrive dans `ExecuteStartRules()`, qui
n'exécute que les règles portant une telle condition (dérivé, `ListeRule.cpp:470-480`).

Elle ne porte aucun paramètre et ne se sérialise qu'en un nœud vide :
```xml
<calaos:condition type="start"/>
```
(dérivé, `ConditionStart.cpp:58-64`)

C'est la seule condition qui ne nomme aucun IO, et donc la seule qui n'enregistre jamais rien dans
`missingIoIds` (dérivé, `Condition.h:83-90`).

### ConditionScript

Exécute un script Lua. **`Evaluate()` renvoie toujours `false` et logue une erreur** : une
condition script doit passer par `EvaluateAsync()`, qui lance le script dans un processus détaché
(dérivé, `ConditionScript.cpp:36-50`). La règle est retenue si le script rend `true`.

Elle porte en plus une liste d'**ids déclencheurs** (`<calaos:input>`), comparés mais **jamais
résolus** par le dispatch (dérivé, `ConditionScript.cpp:84-89`). Ils sont stockés dans un
`std::vector<std::string>` et réécrits dans cet ordre, c'est-à-dire l'ordre du document
(dérivé, `ConditionScript.h:48`, `ConditionScript.cpp:154-158`). C'était auparavant l'ordre de
hash de **pointeurs**, donc dépendant de l'ASLR : deux exécutions successives du serveur
produisaient des `rules.xml` différents dès qu'une condition script avait ≥ 2 déclencheurs
(corrigé au passage par E4.2c ; voir `docs/refactoring/RELEASE_NOTES.md`, section « Fiabilité »,
« Conditions script — fin des diffs fantômes dans `rules.xml` »).

```xml
<calaos:condition type="script">
  <calaos:input id="id-declencheur"/>
  <calaos:script type="lua"><![CDATA[ return true ]]></calaos:script>
</calaos:condition>
```
(dérivé, `ConditionScript.cpp:149-166`)

Le script reçoit la variable `trigger_id` (dérivé, `ConditionScript.cpp:49`).

---

## Actions

**Dossier :** [src/bin/calaos_server/Rules/](../src/bin/calaos_server/Rules/)

### Hiérarchie et valeurs de `type` dans le XML

| Classe | `type=` dans `rules.xml` | Rôle |
|---|---|---|
| `ActionStd` | `"standard"` **ou attribut absent** | applique une valeur à une ou plusieurs sorties |
| `ActionMail` | `"mail"` | envoi d'e-mail |
| `ActionPush` | `"push"` | notification push mobile |
| `ActionScript` | `"script"` | exécute un script Lua |
| `ActionTouchscreen` | `"touchscreen"` | commande un écran tactile Calaos |

(dérivé, `RulesFactory.cpp:110-138`)

⚠️ `ActionCameraDownload` **n'est pas une action de règle** : elle n'est pas enregistrée dans la
fabrique, n'a pas de `.cpp` et sert de **helper** interne à `ActionMail` et `ActionPush` pour
télécharger un instantané de caméra (dérivé, `RulesFactory.cpp:101-148` — absente ;
`ActionMail.cpp:31`, `ActionPush.cpp:36`).

### ActionStd

**Fichier :** [src/bin/calaos_server/Rules/ActionStd.h](../src/bin/calaos_server/Rules/ActionStd.h)

Action la plus courante. Applique une valeur à une **liste** de sorties (dérivé,
`ActionStd.cpp:91-218`). Comme `ConditionStd`, elle référence les sorties **par id**, résolus une
fois par sortie et par exécution (dérivé, `ActionStd.cpp:95-97`).

Deux refus, tous deux journalisés et sans déréférencement :
- l'id ne résout plus → l'action sur cette sortie est sautée (dérivé, `ActionStd.cpp:99-106`) ;
- l'id résout vers un IO qui **n'est pas une sortie** → sautée aussi. `Add()` refuse déjà les
  non-sorties, donc ce cas signifie qu'un autre IO a repris l'id depuis la construction de la règle
  (dérivé, `ActionStd.cpp:108-118`).

La valeur appliquée peut être :
- une **constante** (`"true"`, `"false"`, `"50"`, `"up"`) ;
- la **valeur courante d'un autre IO**, via `val_var` : la valeur est lue avec
  `get_command_bool()` / `get_command_double()` / `get_command_string()` sur cet IO
  (dérivé, `ActionStd.cpp:125-136`, `:158-167`, `:188-202`). Une source `TBOOL` alimentant une
  cible `TSTRING` est convertie en `"true"` / `"false"` (dérivé, `ActionStd.cpp:197-201`).

**Exemple XML** (dérivé, `ActionStd.cpp:355-375`) :
```xml
<calaos:action type="standard">
  <calaos:output id="id-def" val="true"/>
</calaos:action>
```
Attributs : `id`, **`val`**, et `val_var` (optionnel).

### ActionMail

Envoie un e-mail via `NotifManager::sendMailNotification(subject, message, recipients, sender,
attachmentFile)` (dérivé, `ActionMail.cpp:87-94`).

Les paramètres sont des **attributs de `<calaos:mail>`** : `sender`, `recipients`, `subject`,
`attachment`. Le **corps du message est le contenu texte du nœud**, écrit en CDATA
(dérivé, `ActionMail.cpp:96-129`). Les espaces sont retirés de `recipients` au chargement
(dérivé, `ActionMail.cpp:107`).

`attachment` désigne une **caméra** : si l'id résout vers une `IPCam`, un instantané est téléchargé
puis joint ; en cas d'échec, le mail part **sans** pièce jointe plutôt que d'être perdu
(dérivé, `ActionMail.cpp:40-85`).

```xml
<calaos:action type="mail">
  <calaos:mail sender="a@b.c" recipients="d@e.f" subject="Alerte" attachment="id-camera"><![CDATA[Corps du message]]></calaos:mail>
</calaos:action>
```

### ActionPush

Envoie une notification push mobile via `NotifManager::sendPushNotification()`
(dérivé, `ActionPush.cpp:128-138`).

Un seul attribut sur `<calaos:push>` : **`attachment`** (id de caméra). Le **message est le
contenu texte du nœud**, en CDATA (dérivé, `ActionPush.cpp:141-165`). Un message vide devient
`"Calaos Notification"` (dérivé, `ActionPush.cpp:107-109`).

L'action journalise aussi un événement d'historique `EventPushNotification` dont le `event_raw`
est l'objet `{ "message": …, "pic_uid": … }` (dérivé, `ActionPush.cpp:103-119`).

### ActionScript

Exécute un script Lua via `ScriptExec::ExecuteScriptDetached()`. **La valeur de retour est
ignorée** : `Execute()` rend toujours `true`, le résultat du script n'apparaît que dans le log
(dérivé, `ActionScript.cpp:34-42`).

```xml
<calaos:action type="script">
  <calaos:script type="lua"><![CDATA[ print("hello") ]]></calaos:script>
</calaos:action>
```
(dérivé, `ActionScript.cpp:61-72`) — seul `type="lua"` est lu ; toute autre valeur laisse le script
vide (dérivé, `ActionScript.cpp:52-56`).

### ActionTouchscreen

Une seule action existe : `view_camera`. Elle émet un `EventTouchScreenCamera` portant l'id de la
caméra, et **échoue** (`return false`) si l'id ne résout pas (dérivé, `ActionTouchscreen.cpp:37-53`).

```xml
<calaos:action type="touchscreen" action="view_camera" camera="id-camera"/>
```
(dérivé, `ActionTouchscreen.cpp:76-90`)

---

## RulesFactory

**Fichier :** [src/bin/calaos_server/Rules/RulesFactory.h](../src/bin/calaos_server/Rules/RulesFactory.h)

Fabrique les conditions et les actions depuis les nœuds pugixml.

**Deux causes de rejet, portées par le pointeur de retour lui-même** (dérivé,
`RulesFactory.cpp:25-53`, `:88-97`, `:140-147`) :

| Cause | Symptôme | Sort de la règle |
|---|---|---|
| **1 — nœud inutilisable** : `type` inconnu, enfant obligatoire absent (`<calaos:script>`, `<calaos:mail>`…) | `LoadFromXml()` rend `false`, la fabrique **détruit** l'objet et rend `NULL` | la règle reste **activée**, l'objet est simplement absent |
| **2 — nœud correct, IO introuvable** | la fabrique rend un objet **non nul**, marqué `hasMissingIo()` | `Rule::AddCondition()/AddAction()` **désactivent toute la règle** |

Aucune signature n'a changé pour porter cette distinction, et aucun appelant n'a eu à apprendre un
nouveau protocole.

---

## Format XML des règles

Fichier de config : **`rules.xml`**. Racine `<calaos:rules xmlns:calaos="http://www.calaos.fr">`,
chaque règle est un `<calaos:rule>` portant obligatoirement les attributs `name` **et** `type`
— une règle à qui il manque l'un des deux est **ignorée au chargement**
(dérivé, `CalaosConfig.cpp:382-398`, et `Config::SaveConfigRule()` en `:461-495`).

Tous les autres attributs du nœud `<calaos:rule>` sont chargés tels quels dans `params` et
réécrits tels quels (dérivé, `Rule.cpp:311-315`, `:361-366`). C'est ainsi qu'une règle générée par
un auto-scénario porte ses **quatre** marqueurs : `auto_scenario` (l'id historique du scénario),
**`autoscenario_uid`** (l'uid de la définition, le seul qui autorise le générateur à la détruire),
`auto_scenario_type` et, pour les étapes, `auto_scenario_step`
(dérivé, `Scenario/AutoScenario.cpp:474-483`, `:609`).

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<calaos:rules xmlns:calaos="http://www.calaos.fr">
  <calaos:rule type="rule" name="Allumer lumière entrée">
    <calaos:condition type="standard" trigger="true">
      <calaos:input id="id-interrupteur" oper="==" val="true"/>
    </calaos:condition>
    <calaos:action type="standard">
      <calaos:output id="id-lumiere" val="true"/>
    </calaos:action>
  </calaos:rule>
</calaos:rules>
```
(dérivé, `CalaosConfig.cpp:361-363` pour l'en-tête, `Rule.cpp:357-375`,
`ConditionStd.cpp:519-542` et `ActionStd.cpp:355-375` pour le corps)

⚠️ Les noms d'éléments sont **préfixés `calaos:`** et les attributs sont **`oper`** et **`val`**.
Une configuration écrite avec `<condition>` / `<input operator= value=>` n'est pas lue : les
comparaisons de nom d'élément sont exactes (dérivé, `Rule.cpp:321`, `:327`,
`ConditionStd.cpp:422`, `ActionStd.cpp:271`) et les attributs `operator` / `value` ne sont jamais
consultés.

---

## Flux d'un déclenchement typique

```
[Utilisateur appuie sur interrupteur]
  → InputSwitch::hasChanged()
  → IOBase::EmitSignalIO()
  → ListeRule::ExecuteRuleSignal("id-interrupteur")
    → [si une marche est déjà en cours : l'id est mis en attente et rejoué après]
    → ListeRule::executeTrigger("id-interrupteur")
      → ListeRule::collectTriggeredRules()
        → saute toute règle isDisabled()
        → pour chaque ConditionStd citant cet id, avec trigger=true :
          → Rule::CheckConditions()            // évalue TOUTES les conditions
      → pour chaque règle retenue : Rule::ExecuteActions()
        → ActionStd::Execute()
          → OutputLight::set_value(true)
            → [allume la lumière physique]
          → IOBase::EmitSignalIO()             // notifie les clients
          → EventManager::create(EventIOChanged, …)
    → ListeRule::drainPendingTriggers()
```
(dérivé, `ListeRule.cpp:388-408`, `:316-330`, `:228-314`, `:377-386`)

Une règle portant une `ConditionScript` déclenchée par cet id part dans une seconde liste et est
dispatchée de façon **asynchrone**, une seule fois par règle quel que soit le nombre de conditions
script concernées (dérivé, `ListeRule.cpp:286-298`, `:332-344`).

---

## Ce que le moteur de règles ne fait **pas**

- **`ActionStd` n'évalue aucune expression arithmétique.** Il n'existe aucun appel à
  `ExpressionEvaluator` dans `Rules/` : les seuls consommateurs de cette classe sont
  `IO/AnalogIO.cpp`, `IO/Mqtt/MqttCtrl.cpp` et `IO/Mqtt/MqttOutputLightDimmer.cpp`
  (dérivé, recherche exhaustive sur `src/` ; `ExpressionEvaluator.h:6-13` — l'API est
  entièrement **statique** : `isExpressionValid()`, `calculateExpression()`,
  `evaluateExpressionBool()`). Une valeur d'action est une constante ou la valeur d'un autre IO,
  rien d'autre.
- **Il n'existe pas de type `ActionCameraDownload` dans `rules.xml`** (voir plus haut).
