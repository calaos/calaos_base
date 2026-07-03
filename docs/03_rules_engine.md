# Rules Engine — Règles, Conditions, Actions

## Vue d'ensemble

Le moteur de règles est le cœur de l'automatisation Calaos. Une **règle** (`Rule`) contient :
- une liste de **conditions** (`Condition`) — toutes doivent être vraies
- une liste d'**actions** (`Action`) — exécutées si les conditions sont remplies

Les règles sont déclenchées par :
1. Un signal d'un IO (entrée qui change de valeur) → `ListeRule::ExecuteRuleSignal()`
2. Une boucle événementielle pour les IO temporels (InputTime, InputTimer, InPlageHoraire) → `ListeRule::RunEventLoop()`
3. Au démarrage → `ListeRule::ExecuteStartRules()`

---

## ListeRule

**Fichier :** [src/bin/calaos_server/ListeRule.h](../src/bin/calaos_server/ListeRule.h)

Singleton. Contient toutes les règles actives.

```cpp
ListeRule &lr = ListeRule::Instance();
lr.Add(rule);
lr.ExecuteRuleSignal("io-uuid-xxx");  // déclenche toutes règles utilisant cet IO
lr.ExecuteStartRules();              // au boot
lr.RunEventLoop();                   // boucle pour IOs temporels
```

---

## Rule

**Fichier :** [src/bin/calaos_server/Rule.h](../src/bin/calaos_server/Rule.h)

```cpp
class Rule {
    vector<Condition*> conds;
    vector<Action*> actions;
    Params params;    // "type", "name"
};
```

### Exécution d'une règle

```
Rule::Execute()
  → Rule::CheckConditions()       // évalue toutes les conditions
    → chaque Condition::Evaluate()
  [si toutes vraies]
  → Rule::ExecuteActions()
    → chaque Action::Execute()
```

Version asynchrone (pour conditions avec réseau) :
```cpp
rule->CheckConditionsAsync([](bool ok) { if (ok) rule->ExecuteActions(); }, triggerId);
```

---

## Conditions

**Dossier :** [src/bin/calaos_server/Rules/](../src/bin/calaos_server/Rules/)

### Hiérarchie

```
Condition (abstraite)
  ├── ConditionStd       — compare la valeur d'un IO à une constante ou un autre IO
  ├── ConditionOutput    — compare l'état d'une sortie
  ├── ConditionStart     — vraie une seule fois au démarrage
  └── ConditionScript    — évalue un script Lua
```

### ConditionStd

**Fichier :** [src/bin/calaos_server/Rules/ConditionStd.h](../src/bin/calaos_server/Rules/ConditionStd.h)

Condition la plus courante. Compare des IOs entre eux ou à des valeurs fixes.

```cpp
class ConditionStd {
    vector<IOBase*> inputs;  // IOs impliqués
    Params params;           // valeurs de comparaison ("0", "10.5", "true"…)
    Params ops;              // opérateurs ("==", "!=", ">", "<", ">=", "<=")
    Params params_var;       // si comparaison à un autre IO (ID de l'IO)
    bool trigger;            // si true, cet IO peut déclencher la règle
};
```

Opérateurs supportés : `==`, `!=`, `>`, `<`, `>=`, `<=`

Fonctionne pour les trois types (`bool`, `double`, `string`).

**Exemple XML :**
```xml
<condition type="ConditionStd">
  <input id="id-abc" operator="==" value="true"/>
</condition>
```

### ConditionOutput

Compare l'état actuel d'une **sortie**. Utilise `IOBase::check_condition_value()` pour les types TSTRING (ex: volet ouvert/fermé).

### ConditionStart

Vraie uniquement lors de l'exécution de `ExecuteStartRules()` au démarrage. Permet des actions "au démarrage du serveur".

### ConditionScript

Exécute un script Lua. La règle est déclenchée si le script retourne `true`.

---

## Actions

**Dossier :** [src/bin/calaos_server/Rules/](../src/bin/calaos_server/Rules/)

### Hiérarchie

```
Action (abstraite)
  ├── ActionStd          — set_value sur un IO de sortie
  ├── ActionMail         — envoi d'e-mail
  ├── ActionPush         — notification push mobile
  ├── ActionScript       — exécute un script Lua
  └── ActionTouchscreen  — commande un écran tactile Calaos
```

### ActionStd

**Fichier :** [src/bin/calaos_server/Rules/ActionStd.h](../src/bin/calaos_server/Rules/ActionStd.h)

Action la plus courante. Applique une valeur à une sortie.

```
ActionStd::Execute()
  → output->set_value(value)
```

La valeur peut être :
- une constante (`"true"`, `"false"`, `"50"`, `"up"`)
- la valeur courante d'un IO source (référence)
- une expression calculée (via `ExpressionEvaluator`)

**Exemple XML :**
```xml
<action type="ActionStd">
  <output id="id-def" value="true"/>
</action>
```

### ActionMail

Envoie un e-mail via `NotifManager::sendMailNotification()`.

Paramètres : `subject`, `message`, `to`, `from`, `attachment`.

### ActionPush

Envoie une notification push mobile via `NotifManager::sendPushNotification()`.

Paramètres : `message`, `pic_uuid` (optionnel).

### ActionScript

Exécute un script Lua via `ScriptManager::ExecuteScript()`. La valeur de retour du script est ignorée (l'action est toujours considérée comme exécutée).

### ActionTouchscreen

Envoie un événement à un écran Calaos (ex: afficher une caméra).

---

## RulesFactory

**Fichier :** [src/bin/calaos_server/Rules/RulesFactory.h](../src/bin/calaos_server/Rules/RulesFactory.h)

Fabrique pour les conditions et actions. Instancie les objets depuis les nœuds XML.

---

## Format XML des règles

Fichier de config : `rules.xml`

```xml
<calaos:rules>
  <rule type="rule" name="Allumer lumière entrée">
    <condition type="ConditionStd">
      <input id="id-interrupteur" operator="==" value="true"/>
    </condition>
    <action type="ActionStd">
      <output id="id-lumiere" value="true"/>
    </action>
  </rule>
</calaos:rules>
```

---

## Flux d'un déclenchement typique

```
[Utilisateur appuie sur interrupteur]
  → InputSwitch::hasChanged()
  → IOBase::EmitSignalIO()
  → ListeRule::ExecuteRuleSignal("id-interrupteur")
    → pour chaque Rule contenant "id-interrupteur" dans ses conditions :
      → Rule::CheckConditions()
        → ConditionStd::Evaluate() → true
      → Rule::ExecuteActions()
        → ActionStd::Execute()
          → OutputLight::set_value(true)
            → [allume la lumière physique]
          → IOBase::EmitSignalIO()          // notifie les clients
          → EventManager::create(EventIOChanged, ...)
```

---

## ExpressionEvaluator

**Fichier :** [src/lib/ExpressionEvaluator.h](../src/lib/ExpressionEvaluator.h)

Utilisé par `ActionStd` pour évaluer des expressions arithmétiques dans les valeurs d'action. Basé sur la bibliothèque `exprtk`.

```cpp
ExpressionEvaluator eval;
eval.setVariable("x", 42.0);
double result = eval.evaluate("x * 2 + 10");  // → 94.0
```
