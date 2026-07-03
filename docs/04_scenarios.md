# Scénarios — AutoScenario et Plages Horaires

## Vue d'ensemble

Les scénarios Calaos sont des séquences d'actions programmables avec temporisation, déclenchables manuellement ou selon une plage horaire. Ils sont implémentés comme des ensembles de règles ordinaires (Rule + Condition + Action) générées et gérées par `AutoScenario`.

---

## AutoScenario

**Fichier :** [src/bin/calaos_server/Scenario/AutoScenario.h](../src/bin/calaos_server/Scenario/AutoScenario.h)

Classe qui abstrait un scénario complexe multi-étapes. Chaque `AutoScenario` génère automatiquement un ensemble de `Rule` dans `ListeRule`.

### IOs internes créés

Chaque scénario possède ses propres IOs de contrôle :

| IO | Classe | Rôle |
|---|---|---|
| `ioScenario` | `Scenario` | Bouton de déclenchement (TBOOL) |
| `ioIsActive` | `Internal` | État actif/inactif |
| `ioScheduleEnabled` | `Internal` | Plage horaire activée ou non |
| `ioStep` | `Internal` | Étape courante |
| `ioTimer` | `InputTimer` | Minuterie entre étapes |
| `ioTimeRange` | `InPlageHoraire` | Plage horaire (optionnel) |

### Règles générées

| Règle | Condition | Action |
|---|---|---|
| `ruleStart` | `ioScenario == true` | Actions de l'étape 0 + démarrage timer |
| `ruleStop` | `ioScenario == false` | Arrêt du scénario |
| `ruleStepEnd` | `ioTimer expire` | Actions étape suivante |
| `ruleSteps[]` | selon étape | Actions de chaque étape |
| `rulePlageStart` | `ioTimeRange == true` | Active le scénario selon horaire |
| `rulePlageStop` | `ioTimeRange == false` | Désactive selon horaire |

### API principale

```cpp
AutoScenario *sc = new AutoScenario(ioScenarioInput);

// Étapes
sc->addStep(5.0);                        // nouvelle étape, pause 5s
sc->addStepAction(0, outputLight, "true");  // step 0 : allume lumière
sc->addStepAction(1, outputShutter, "up"); // step 1 : monte volet
sc->setStepPause(1, 10.0);               // pause 10s après step 1

// Options
sc->setCycling(true);    // répétition cyclique
sc->setDisabled(false);

// Commit
sc->checkScenarioRules(); // reconstruit les Rule générées

// Horaire
sc->addSchedule();        // active la plage horaire
sc->deleteSchedule();
```

### Catégorisation

```cpp
string cat = sc->getCategory();
// retourne "light", "shutter", "other" ou combinaison "light-shutter"
// basé sur les types d'IOs utilisés dans les actions
```

---

## Scenario (IO)

**Fichier :** [src/bin/calaos_server/IO/Scenario.h](../src/bin/calaos_server/IO/Scenario.h)

IO virtuel de type TBOOL représentant le bouton de déclenchement d'un scénario. Affiché dans l'UI comme un bouton.

Enregistré dans `IOFactory` avec le type `"Scenario"`.

---

## InPlageHoraire (Plage horaire)

**Fichier :** [src/bin/calaos_server/IO/InPlageHoraire.h](../src/bin/calaos_server/IO/InPlageHoraire.h)

IO virtuel de type TBOOL qui retourne `true` si l'heure actuelle est dans la plage configurée.

Utilisé par :
- `AutoScenario` pour le déclenchement horaire
- Directement dans des règles (condition `InPlageHoraire == true`)

### Paramètres

| Paramètre | Description |
|---|---|
| `timerange` | Définition XML de la plage (via `TimeRange`) |
| `use_dst` | Prise en compte de l'heure d'été |

### TimeRange

**Fichier :** [src/lib/TimeRange.h](../src/lib/TimeRange.h)

Modèle de plage horaire supportant :
- Heures fixes
- Lever/coucher du soleil (via `sunset.h`)
- Décalages (ex: "30 min avant le coucher")
- Récurrence hebdomadaire

---

## InputTimer

**Fichier :** [src/bin/calaos_server/IO/InputTimer.h](../src/bin/calaos_server/IO/InputTimer.h)

IO minuterie. Déclenche `hasChanged()` après un délai configurable. Utilisé comme timer entre les étapes d'un `AutoScenario`.

| Paramètre | Description |
|---|---|
| `delay` | Délai en secondes |
| `auto_reset` | Redémarrage automatique |

---

## Gestion du cache de scénarios

`ListeRoom` maintient un cache des scénarios :

```cpp
void ListeRoom::addScenarioCache(Scenario *sc);
void ListeRoom::delScenarioCache(Scenario *sc);
list<Scenario*> ListeRoom::getAutoScenarios();
void ListeRoom::checkAutoScenario();  // vérifie la cohérence des scénarios
```

---

## Persistence XML

Les scénarios sont sauvegardés dans `local_config.xml` comme des pièces dédiées contenant leurs IOs internes, puis dans `rules.xml` pour leurs règles générées. Les règles d'auto-scénario sont marquées `auto_sc_mark = true`.
