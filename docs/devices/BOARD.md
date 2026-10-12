# Devices / climate — board

Spec : [`CALAOS_DEVICES_FEAT.md`](../../CALAOS_DEVICES_FEAT.md) · décisions :
[`DECISIONS.md`](../refactoring/DECISIONS.md) (2026-10-12) · processus et reprise :
[`PROCESS.md`](PROCESS.md).

Ce board est la **source unique du statut** pour tous les dépôts concernés. Les boards propres à
calaos_mobile et calaos-web-app ne reçoivent qu'une ligne de renvoi au moment où un ticket y démarre.

Statuts : 📋 à faire · 🔨 en cours · 👀 PR ouverte · ✅ mergé · ⛔ bloqué.
Agent : modèle / effort / skills de l'agent d'implémentation. La revue est toujours faite par un
agent séparé (voir `PROCESS.md`).

## Graphe

```
OUT-1 ─┬─ CORE-1 ─┬─ CORE-2 ─┬─ CORE-3 ──────────────── INST-3
       │          │          └─ CORE-4 ─┬─ CLIM-1 ─┬─ CLIM-2
       │          │                     │          ├─ CLIM-3 ─┬─ CLIM-4
       │          │                     │          │          └─ CLIM-5 ── MCP-1
       │          │                     │          └─ DOC-1 (+T3.247) ─ DOC-2 ─ DOC-3
       │          │                     ├─ WEB-1 ─ WEB-2
       │          │                     ├─ MOB-1 ─ MOB-2
       │          │                     └─ RUI-1 ─ RUI-2 (+OUT-3) ─ RUI-3
       └─ CORE-5 ── INST-6
OUT-2 ─ INST-1 ─┬─ INST-2 ─ INST-4 (+DOC-3)
                ├─ INST-3
                └─ INST-5 (+DOC-1)
Tout ─ VAL-1
```

---

## OUT — outillage préalable

| ID | Dépôt | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|---|
| OUT-1 | calaos_base | **Couverture bloquante sur les PR.** Le job `coverage` (gcov/lcov) existe mais ne bloque pas. Ajouter une porte de couverture **différentielle** : toute ligne nouvelle ou modifiée dans `src/` doit être exécutée par `make check`, sinon la PR est rouge. Exclusions seulement par marqueur explicite et justifié (`LCOV_EXCL_LINE` + raison). Le job doit lister les lignes non couvertes. Auto-test : une contre-mutation du script qui le rend muet doit faire échouer un témoin. | — | sonnet / high / graft | ✅ [#178](https://github.com/calaos/calaos_base/pull/178) |
| OUT-2 | calaos_installer | **Tests et CI de PR.** Aucun test automatisé aujourd'hui, et `ci_build.yml` ne tourne que sur `push`. Ajouter un projet Qt Test (`tests/tests.pro`, sur le modèle de calaos_mobile), un premier lot de tests de caractérisation de `IOXmlReader`/`IOXmlWriter` (aller-retour des io.xml de `tests/project*`), et un workflow `pull_request` Linux qui build et lance les tests. | — | sonnet / high | ✅ [#51](https://github.com/calaos/calaos_installer/pull/51) |
| OUT-3 | calaos_remote_ui | **Tests hôte et CI de PR.** Aucun test unitaire, aucun déclencheur `pull_request`, et les 6 derniers runs `build-firmware` sur `main` sont rouges. Diagnostiquer et réparer le build, ajouter une cible de tests hôte (build Linux, gtest ou Catch2) couvrant `PagesConfig::fromJson`, le parsing `remote_ui_io_states`/`io_changed` et `AppStore`, et un workflow `pull_request` (build `linux-dev` + tests). | — | sonnet / high | ✅ [#3](https://github.com/calaos/calaos_remote_ui/pull/3) |

## CORE — socle device (calaos_base)

| ID | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|
| CORE-1 | **Hook de changement typé et synchrone** (spec §4.3). Registre id → callbacks appelé depuis `IOBase::EmitSignalIO()`, désabonnement lié à la durée de vie de l'abonné, ré-entrance sérialisée par `ListeRule`. Tests : abonnement/désabonnement, abonné détruit avant l'IO, écriture d'un autre IO dans le callback (pas de récursion), ordre relatif avec les règles. | OUT-1 | opus / high / graft | ✅ [#180](https://github.com/calaos/calaos_base/pull/180) |
| CORE-2 | **Classe `Device` : XML et propriété** (§3.1, §3.2, §4.1, §4.2). `<calaos:device>` dans la liste blanche de `Room::LoadFromXml`, IOs imbriqués créés par `IOFactory` et enregistrés dans la liste plate de la pièce, `<link>` résolus après le chargement de toutes les pièces, `<param>`, sauvegarde imbriquée (`Room::SaveToXml` saute les IOs possédés), suppression en cascade par l'API, ids et jamais de pointeurs bruts. Tests : aller-retour XML identique (fixture `roundTripIo`), lien non résolu, lien externe dans une autre pièce, suppression en cascade, IO possédé visible par les règles et `get_home`. | CORE-1 | opus / high / graft | 🔨 |
| CORE-3 | **Format 2 et refus bruyant** (§3.3). `format="2"` écrit dès qu'un device existe ; `config put` refusé avec un message explicite si des devices disparaîtraient ; la réponse de `config get` annonce `io_format: 2` (un serveur qui ne l'annonce pas est en format 1). Tests HTTP sur le harnais `JsonApiCharacterization` : put format 1 refusé, put format 2 accepté, put sans device sur une maison sans device accepté. | CORE-2 | sonnet / high / graft | 📋 |
| CORE-4 | **Commandes et publication d'état** (§5). `set_value(string)` d'un device : mode en clair ou JSON partiel, validation par champ déléguée à la sous-classe, best effort, journal ; objet `device` dans `get_home` et dans l'`io_changed` du device, regroupé par un timer membre de 100 ms ; `gui_type` = la famille (`climate`). Goldens `ws_get_home`/`http_get_home` régénérés par `CALAOS_GOLDEN_UPDATE=1`, jamais écrits à la main. Tests : chaîne simple, JSON partiel, champ inconnu, JSON invalide, rafale de changements → un seul événement. | CORE-2 | opus / high / graft | 📋 |
| CORE-5 | **`MqttOutputString` et bornes analogiques** (§12). Nouveau type MQTT (publication du gabarit `data` avec la chaîne), params `value_min`/`value_max` sur les sorties analogiques et `InternalInt` (bornage à l'écriture, documenté dans IODoc). Tests sans courtier sur le modèle de `JsonPathSyntax_test`. | OUT-1 | sonnet / medium / graft | ✅ [#179](https://github.com/calaos/calaos_base/pull/179) |

## CLIM — famille climate (calaos_base)

| ID | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|
| CLIM-1 | **`ClimateBase`** (§6). Enums mode/action et leur sérialisation, dérivation des capacités depuis les liens présents (table §6.3), objet d'état, validation des champs de commande (enum, bornes, pas, plage `low`/`high` avec `min_range`). Tests : chaque ligne de la table §6.3, clamp et arrondi, plage incohérente. | CORE-4 | opus / high / graft | 📋 |
| CLIM-2 | **`ProxyClimate`** (§7). Lecture via `io`, écriture via `command_io`, mode miroir du lien, relais `remote_temp` sur changement et par keep-alive (timer membre). Tests : relais de chaque champ, absence de `command_io`, cadence du keep-alive, arrêt du keep-alive quand le device est détruit. | CLIM-1, CORE-5 | sonnet / high / graft | 📋 |
| CLIM-3 | **`InternalThermostat` : régulation** (§8.1). `evaluate()` (off/heat/cool/auto), hystérésis, `min_on_time`/`min_off_time` par timer réarmable, bascule auto sans double activation, écriture de `action`, mode persistant dans le cache, IOs possédés recréés s'ils manquent. Durées paramétrables en millisecondes pour les tests (boucle libuv réelle, pas de temps simulé). Tests : table de vérité complète de `evaluate()`, expiration des verrous, bascule auto. | CLIM-1, CORE-1 | opus / xhigh / graft | 📋 |
| CLIM-4 | **`InternalThermostat` : sûreté** (§8.2). Sorties OFF tant que le capteur n'a rien livré, capteur périmé (`sensor_timeout`), coupure point de rosée (`emitter=floor`, `humidity`, `humidity_max`), refus du froid sans capteur d'humidité. Tests : un cas par règle, plus le retour à la normale. | CLIM-3 | opus / high / graft | 📋 |
| CLIM-5 | **Sorties protégées** (§8.3). Avertissement journalisé quand une règle (`ActionStd`), Lua ou `set_state` écrit `output_heat`/`output_cool` d'un thermostat ; jamais de blocage. Tests : chaque chemin d'écriture produit l'avertissement, et la valeur est quand même appliquée. | CLIM-3 | sonnet / medium / graft | 📋 |

## DOC — IODoc et templates (calaos_base, CI)

| ID | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|
| DOC-1 | **Format de commande dans IODoc** (§11). `command_format: json` et la liste des champs (type, valeurs ou bornes, unité, rôle de lien requis) dans `IODoc.h`, `genDocJson`, `genDocMd`. Tests : extension de `IODocJson_test`. ⛔ Ne régénère pas `io_doc.json` avant la fermeture de T3.247. | CLIM-1, **T3.247** | sonnet / high / graft | 📋 |
| DOC-2 | **Templates de devices.** Format des templates (IOs possédés, liens, params par défaut, champs demandés à l'utilisateur), les cinq templates initiaux (§10), génération par `calaos_server --gentemplates` à côté de `--gendoc`. Test de cohérence : chaque template ne référence que des types enregistrés et des rôles de lien connus, et un device généré depuis chaque template se charge et fait l'aller-retour XML. | DOC-1, CLIM-2, CLIM-4 | opus / high / graft | 📋 |
| DOC-3 | **Génération en CI et import dans l'installer.** Le CI de calaos_base produit `io_doc.json` et les templates en artefact ; le build de l'installer les importe (corriger au passage le bug de `generate_iodoc.sh`, qui copie deux fois le `.json` fr). Un écart entre le dépôt de l'installer et la sortie du serveur fait échouer la CI de l'installer. | DOC-2, OUT-2 | sonnet / medium | 📋 |

## MCP — sidecar (calaos_base, Python)

| ID | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|
| MCP-1 | **Devices dans le MCP** (§9.2). Les sorties protégées sont masquées de `list_ios` et refusées en écriture ; la description de `set_io_state` documente la commande JSON des devices depuis IODoc ; droit du scope service tranché (point ouvert §14) avant d'écrire le code. Tests pytest dans `tests/python`. | CLIM-5, DOC-1 | sonnet / medium | 📋 |

## INST — calaos_installer

| ID | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|
| INST-1 | **Modèle et XML.** Classe `Device` (IOs possédés, liens, params) dans `Room`, lecture et écriture de `<calaos:device>` et de `format="2"`, ouverture en lecture seule d'un format plus récent. Corrige au passage l'aplatissement silencieux actuel (la branche « read dummy » s'arrête au premier enfant). Tests Qt : aller-retour, fichier format 3 en lecture seule. | OUT-2, CORE-2 | opus / high | 📋 |
| INST-2 | **Arbre et suppression en cascade.** `QTreeWidgetItemDevice`, arbre pièce → device → IOs, menu contextuel, suppression du device et de ses IOs, avertissement avant de supprimer un IO externe encore lié. La suppression actuelle détruit en silence les règles qui référencent l'IO : le dialogue doit au minimum les lister. | INST-1 | sonnet / high / frontend-design | 📋 |
| INST-3 | **Format côté serveur.** Lecture de `io_format` dans la réponse de `config get`, refus d'envoyer des devices à un serveur qui ne l'annonce pas, affichage du message d'erreur du serveur sur un `put` refusé (ignoré aujourd'hui). | INST-1, CORE-3 | sonnet / medium | 📋 |
| INST-4 | **Assistant de création depuis les templates.** Dialogue qui demande les champs du template, génère le device, ses IOs, liens et params, et l'insère dans l'arbre. | INST-2, DOC-3 | opus / high / frontend-design | 📋 |
| INST-5 | **Formulaire de commande généré.** Dans `FormActionStd`, quand IODoc déclare `command_format: json`, remplacer le champ texte par un formulaire limité aux champs dont le lien existe sur l'instance (modèle : `WidgetIOProperties`). La valeur stockée reste la chaîne JSON. | INST-1, DOC-3 | sonnet / high / frontend-design | 📋 |
| INST-6 | **Nouveaux types et params.** `MqttOutputString`, `value_min`/`value_max` analogiques (et arrêt de l'écriture des vieux `min`/`max` par défaut, retirés à la lecture — décision 14), et les types de devices dans l'`IOFactory` de l'installer ; chaînes traduites en fr (lupdate dans le même ticket). | CORE-5, INST-1 | sonnet / medium | 📋 |

## WEB — calaos-web-app

| ID | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|
| WEB-1 | **Décodage.** `device?` dans les types et un guard `toDeviceInfo` qui ne lève jamais d'exception, patch dans `applyIoChanged`, fixture du mock-server avec un proxy et un thermostat. Tests Vitest sur les guards et le store. | CORE-4 | sonnet / medium | 🔨 |
| WEB-2 | **Composant `ClimateIo`.** Entrée `climate` dans `GUI_TYPES` et `IO_COMPONENTS`, rendu piloté par les capacités (boutons de mode, consigne bornée, plage en `auto`, ventilation, badge d'action, grisé selon le mode), commande JSON par `sendSetState`, transitions du mock-server. Tests : `.spec.ts` du composant et un scénario Playwright. | WEB-1, CLIM-1 | sonnet / high / frontend-design | 📋 |

## MOB — calaos_mobile

| ID | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|
| MOB-1 | **Décodage.** Exposer l'objet `device` de `get_home`, et faire transporter la map par l'`io_changed` (aujourd'hui convertie en chaîne vide) sur le modèle de `IoStatusChange`. Tests : `tst_calaoseventdecoder`, `tst_iobase`. | CORE-4 | sonnet / medium | 🔨 |
| MOB-2 | **Widget `IOClimate`.** Type `climate` dans `IOTypeRegistry` et `Common.h`, `IOClimate.qml` piloté par les capacités, variantes desktop et mobile, déclarations `qmldir`/`.qrc`/`calaos.pri`, commande JSON par `sendStringValue`. Tests Qt et qmllint. | MOB-1, CLIM-1 | sonnet / high / frontend-design | 📋 |

## RUI — calaos_remote_ui

| ID | Dépôt | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|---|
| RUI-1 | calaos_base | **Côté serveur.** `extractReferencedIOs()` étend un device à ses IOs possédés et liés ; l'objet `device` voyage dans `remote_ui_io_states` et `io_changed`. Tests sur le harnais RemoteUI existant. | CORE-4 | sonnet / medium / graft | 📋 |
| RUI-2 | calaos_remote_ui | **Protocole du firmware.** Inverser les 17 tests `*KnownBugs*` épinglés par OUT-3 (et corriger DOC-CI.md:41, qui dit à tort que tous portent `BUG (RUI-2)`). Champ device dans `IoState`, parsing aux deux endroits, fusion dans `AppStore`, détection de changement qui ne se limite plus à `state` (`app_store.cpp:333`, `calaos_widget.cpp:125`), et correction du `value("visible", true)` qui jette tout le lot sur une chaîne `"true"`. Tests hôte. | RUI-1, OUT-3 | sonnet / high | 🔨 |
| RUI-3 | calaos_remote_ui | **Widget `Climate`.** Enregistré via `registerWidgetAnySize`, mise en page adaptée aux cellules 240×240 à 213×266 et aux tailles multiples, piloté par les capacités, commande JSON. Validé sur le simulateur SDL (`linux-dev`), captures jointes à la PR. | RUI-2, CLIM-1 | opus / high / frontend-design | 📋 |

## VAL — validation de bout en bout

| ID | Ticket | Dépend de | Agent | Statut |
|---|---|---|---|---|
| VAL-1 | **Recette sur la box** (root@192.168.30.17). Un proxy ESPHome simulé (mosquitto + script qui joue le firmware ESPHome climate) et un thermostat sur IOs internes : création par l'installer, pilotage depuis webapp, mobile et panneau simulé, règles de politique (nuit, fenêtre ouverte), redémarrage du serveur, suppression en cascade. Rapport avec captures. | tous | sonnet / high | 📋 |

## Hors chantier (tickets séparés)

| ID | Dépôt | Ticket | Agent | Statut |
|---|---|---|---|---|
| SEC-1 | calaos_base | **Restreindre un panneau RemoteUI à ses IOs référencés.** `hasReferencedIO()` n'est jamais appelé : un panneau reçoit tous les `io_changed` et peut écrire n'importe quel IO. | opus / high / graft | 📋 |
| CLEAN-1 | calaos_base | **Supprimer `ListeRoom::get_chauffage_var()` côté serveur** (aucun appelant en production). Le param `chauffage_id` et sa clé JSON **restent** : l'installer s'en sert pour son assistant de règles de chauffage (`formrules.cpp:2836`). | haiku / medium / graft | 📋 |
| FIX-INST-0 | calaos_installer | ⭐ **Perte de données : `months` absent.** Une plage horaire sans `months` est lue avec tous les mois désactivés (`bitset<12>("")`) et réécrite `months="000000000000"` ; le serveur la considère alors inactive toute l'année. Un ancien projet ouvert puis sauvé une fois casse toutes ses plages, sans message. Correctif : `range_months.set()` quand le param est absent ; inverser le test d'OUT-2. À livrer avant toute release de l'installer. | sonnet / medium | ✅ [#52](https://github.com/calaos/calaos_installer/pull/52) |
| FIX-INST-1 | calaos_installer | **Défauts révélés par OUT-2.** Un id vide se lie à la 1re IO sans id (règle ré-aiguillée en silence) ; `loadIOsFromFile` renvoie `true` malgré une erreur ; `hits` d'une Room sans attribut est indéfini (`int` + `from_string("")`, non épinglé car non déterministe). Durcir aussi la suite : test de `months=""`, `cleanup()` qui vérifie qu'aucune modale inattendue n'a été fermée, valeur exacte au lieu de `size() >= 1` (l.838), image Arch datée dans `pr_ci.yml`. | sonnet / high | ✅ [#53](https://github.com/calaos/calaos_installer/pull/53) |
| FIX-BASE-1 | calaos_base | **Corruption de tas dans `core/IncomingLogStockLevel_test`** (révélée par OUT-1) : abort à la sortie (« corrupted double-linked list ») dans le build de couverture, au moment du dump gcov ; ses 19 cas passent et le build normal est vert. Localiser sous ASan (`--enable-asan`, worktree sur disque), corriger, et retirer la tolérance du job « Diff coverage ». | opus / high / graft | 🔨 |
