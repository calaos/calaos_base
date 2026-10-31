# Notes de stack Calaos

Faits durables qui traversent les dépôts et que le code seul ne dit pas. Chaque point : le fait, ce qu'il évite, où le lire.

## Carte de la stack

| Dépôt | Branche par défaut | Build |
|---|---|---|
| calaos_base | master | uniquement dans le dev container (`.devcontainer/`, Debian 12, comme la CI) |
| calaos_remote_ui | main | uniquement dans le dev container `calaos-remote-ui-dev` |
| calaos_installer | master | Qt 6 de l'hôte (pas de devcontainer) |
| calaos_mobile | master | Qt 6 de l'hôte |
| calaos-web-app | master | node |
| doc-website | master | Hugo (devcontainer ou image Docker) |

Règle d'équipe : rien n'est installé sur l'hôte, ni toolchain ni dépendances de build.

## Chaîne IODoc

- Le serveur génère `io_doc.json`, `io_doc.md` et `device_templates.json` : `--gendoc` / `--gentemplates` sont lus avant le démarrage du serveur (`src/bin/calaos_server/main.cpp:129-141`), `scripts/create_iodoc` les lance en `en` puis `fr`.
- L'artefact CI `io-doc` n'est téléversé que hors pull request (`.github/workflows/ci.yml:115-122`, `if: github.event_name != 'pull_request'`), plus un run mensuel sur master (`schedule`) ou `workflow_dispatch` (`docs/17_device_templates.md:27-29`). La génération, elle, tourne aussi en PR.
- `data/doc` de calaos_installer est produit par `generate_iodoc.sh` (qui appelle `scripts/fetch_iodoc_artifact.sh` ou prend un dossier local, puis `check_iodoc_sync.py`) : ne jamais l'éditer à la main, le serveur n'est jamais lancé par ce script.
- Le job `iodoc-sync` de l'installer (`.github/workflows/pr_ci.yml:100`) compare avec la branche calaos_base de même nom que la branche de la PR ; à défaut seulement, avec le dernier run vert de master. Si la branche existe sans run vert hors PR, le job échoue : seuls les runs push/schedule/dispatch font foi (`scripts/fetch_iodoc_artifact.sh:16-42`). `io_doc.json` et `device_templates.json` sont comparés en JSON parsé, `io_doc.md` ne l'est pas.
- Conséquence sur l'ordre de merge : calaos_base d'abord, attendre que le run de master publie `io-doc`, puis régénérer et pousser l'installer.

## Traductions

- Le `make` du conteneur relance `msgmerge` sur chaque `po/*.po` plus ancien que `calaos.pot` (stamp-po d'autopoint, `po/Makefile.in.in`) : un build laisse donc des `.po` modifiés. `calaos.pot` n'est régénéré que par `make calaos.pot-update`, `update-po` ou `make dist`. `en.po` n'est pas dans LINGUAS.
- Usage du projet (non imposé) : ne committer que `po/calaos.pot` et `po/fr.po`, et `git checkout` le reste.
- Le fr doit différer de l'en : script Python de `.github/workflows/ci.yml:80-110` (io_doc, templates, noms et descriptions des templates, devices, paramètres, rôles de liens) et gtest `tests/core/DeviceTemplates_test.cpp:1314` et `:1332`, avec une liste d'exceptions `SAME_IN_FRENCH`.

## Format sur le fil (climate / devices)

- L'objet device de `get_home` est construit dans `src/bin/calaos_server/IO/Device.cpp:455-461` (`ios` toujours présent) ; `io_changed` porte `state` (le mode) et `device` (`Device.cpp:514-522`), avec un debounce de 0,1 s.
- `gui_type`, `family` et `impl` sont en minuscules (`Device.cpp:33-38,63`) : un client qui compare en casse mixte ne matchera rien. Les devices sont visibles par défaut (`Device.cpp:64-66`).
- `set_state` accepte une chaîne JSON ou un simple nom de mode (`Device.cpp:333-352`).
- Chaque client construit son sélecteur de mode depuis `capabilities.modes` (émis par `ClimateBase.cpp:528-535`) : web `useClimate.ts:83`, remote_ui `climate_logic.cpp:121`, mobile `ClimateLogic.js`. Ne pas coder en dur la liste des modes : elle dépend de l'appareil.
- La plage n'est affichée qu'avec `target_temp_range` : web `climate.ts:27-38`, remote_ui `climate_logic.cpp:122,199`.
- Le type de widget Remote UI du climate est `Climate` (majuscule), posé par l'installer (`calaos_installer/src/WidgetModel.cpp:289,315`) ; le serveur n'en définit pas.

## Règles

- Condition standard et condition de sortie s'évaluent différemment. `ConditionStd` compare la valeur brute (`Rules/ConditionStd.cpp:213,226`) ; `ConditionOutput` passe par `IOBase::check_condition_value` (`Rules/ConditionOutput.cpp:209`, `IOBase.h:222-229`), gardé par `hasComparableState()`. Une IO sans état lisible (`IPCam.h:68`, `RemoteUI.h:113`) ne satisfait jamais une condition de sortie ; un avertissement est loggé au chargement (`ConditionOutput.cpp:280`). Le lecteur audio garde son dernier évènement (`onplay`, `onpause`…, `AudioPlayer::lastEvent`) et les deux types de condition le comparent. C'est une condition d'**évènement**, pas un état « en lecture » : `onsongchange` (à chaque piste) et `onvolumechange` remplacent `onplay` en pleine lecture et `lecteur == onplay` redevient faux. Vide avant le premier évènement, non persisté au redémarrage.
- `changed` rend le terme toujours vrai, quel que soit l'opérateur ; c'est l'IO déclenchante qui lance la règle (`ConditionOutput.cpp:93,131`).
- L'installer refuse la condition standard sur un device et n'offre que la condition de sortie (`calaos_installer/src/formrules.cpp:3296-3372`).
- `InputSwitchTriple` et `InputSwitchLongPress` retombent à 0 après 250 ms sans événement (`IO/InputSwitchTriple.cpp:96-100`, `IO/InputSwitchLongPress.cpp:86-88`) : une condition sur ces entrées, évaluée au déclenchement d'une autre IO, voit 0.

## ESPHome / MQTT

- Topics : `<prefix>/climate/<object_id>/<champ>/{state,command}` (`IO/DeviceTemplates.cpp:228-231`) et `<prefix>/status` pour la disponibilité (`:222`).
- `object_id` est l'id de l'entité ESPHome, pas son nom affiché (`:199-202`). L'installer le lit dans les topics découverts (`calaos_installer/src/EsphomeDiscovery.cpp:22-33`) ; les états retained d'un ancien `object_id` restent sur le broker, la découverte les marque comme anciens (`:226-228`).
- Une IO string lecture/écriture seule met sa valeur à jour immédiatement dans `OutputString::set_value` (`IO/OutputString.cpp:53-67`), avant la réponse de l'unité : la valeur n'est pas une confirmation.
- ESPHome republie l'ancien mode après l'écriture de la consigne ; `ProxyClimate.cpp:564` l'ignore pendant `readback_hold` au lieu de le prendre pour un changement fait sur l'unité.
- Double consigne : `two_point` / `heat_cool` génériques, aucun code propre au CN105.
- Température de pièce relayée : `mqtt_subscribe` + `remote_temperature_source` côté ESPHome (`DeviceTemplates.cpp:352-365`, `docs/17_device_templates.md:282-315`).

## Tests temps réel

Convention des tests climate : timeouts d'au moins 10x la cadence de lecture, bornes basses mesurées depuis avant la lecture, attente d'une condition plutôt que `sleep`. Voir `tests/core/InternalThermostatSafety_test.cpp:165` et, pour l'attente de condition, `runLoopUntil` (`tests/core/ProxyClimate_test.cpp:333`, usage `:703`). Un runner qui bloque quelques centaines de ms ne doit pas faire échouer un test.

## Pièges de workflow

- Pas de `git stash` : `refs/stash` vit dans le `.git` commun, partagé par tous les worktrees.
- Les builds Docker laissent des fichiers root dans les worktrees : `git worktree remove` échoue sans sudo.
- Arrêter un conteneur par son montage, jamais par image : on tuerait celui d'un autre agent.
- `/tmp` est un tmpfs en RAM : ne pas y builder.

## Publication calaos_base

- La publication est manuelle : Actions → **Publish** → Run workflow, entrée `channel` = `prerelease` (`-dev.N`, image `:dev`, deb `pre-releases`), `patch`, `minor` ou `major` (image `:latest`, release). Un push sur master ne publie rien : ni image, ni tag, ni deb.
- Le job `verify-ci` refuse un lancement hors de `master` et un commit dont « Build and Test » n'est pas vert (`.github/workflows/publish.yml`). Après un merge, attendre la CI verte, puis lancer `prerelease` avant une recette sur la box.
- `.github/check-workflow-gating.py` (job `workflow-gating` de `ci.yml`) refuse un workflow qui publie sur `push`, `pull_request` ou `workflow_run`, ou sans `needs: verify-ci`. Cela remplace la porte `workflow_run` de [T3.125](refactoring/T3.125.md).

## Box calaos-os

- Paquets : `calaos-server`, `calaos-home`, `calaos-web-app`, `calaos-remote-ui-<board>` ; service `calaos-server.service`.
- Les builds `-dev` vont dans le dépôt `experimental` (`calaos-build/scripts/upload.sh:41`).
- Config dans `/mnt/calaos/config` (`calaos-container/env/calaos.sh:7`). Le cache d'états des IO (`iostates.cache`) est dans le dossier de cache (`src/bin/calaos_server/CalaosConfig.cpp:508`) : le supprimer ou le modifier change les états au démarrage.
- Sauvegarder avant toute édition de la config.
