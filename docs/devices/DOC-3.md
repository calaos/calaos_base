# DOC-3 — Génération en CI et import dans l'installer (calaos_base + calaos_installer)

## Objectif
Le serveur publie sa documentation d'IO et ses templates ; l'installer les importe et sa CI échoue quand ses copies s'écartent de la sortie du serveur.

## Livré
PR calaos/calaos_base#202 et calaos/calaos_installer#66, mergées dans cet ordre (installer après le run master 38669713736, lu par iodoc-sync).
- calaos_base : le job « Build & test » génère `io_doc.json`, `io_doc.md` et `device_templates.json` (en et fr) avec `scripts/create_iodoc` après `make check` et `make -C po install`, contrôle `devices`, les 5 templates et fr ≠ en (un locale ou un .mo absent donne de l'anglais sans erreur), puis publie l'artefact `io-doc` (90 jours).
- `scripts/create_iodoc [dossier]` : `--config`/`--cache` vers un dossier temporaire (sans ça `--gentemplates` s'arrête quand le HOME n'est pas inscriptible), `CALAOS_SERVER` pour le binaire.
- Params, conditions et actions triés par nom dans le md et dans le JSON (les tableaux sortaient dans l'ordre de l'`unordered_map`, donc dépendants de libstdc++).
- Build croisé du MCP : `make IODOC_PREBUILT=/chemin/io_doc.json` installe le document d'un build hôte ou de l'artefact `io-doc` au lieu de `{}` ; un fichier sans `devices` fait échouer le build.
- calaos_installer : `data/doc/{en,fr}` régénérés et `device_templates.json` importé (aussi dans `data/iodoc.qrc`) ; `generate_iodoc.sh [dossier]` (artefact par défaut, une boucle pour tous les fichiers ; le bug du .json fr copié deux fois était déjà corrigé par 3470619).
- Job `iodoc-sync` de `pr_ci.yml` : télécharge l'artefact `io-doc` du dernier run vert de calaos_base master (`scripts/fetch_iodoc_artifact.sh`) et compare le JSON parsé (`scripts/check_iodoc_sync.py`). Artefact plutôt que build de base : 9 s contre ~45 min, pas de seconde chaîne d'outils (Arch ici, Debian là-bas), et on compare avec ce que la CI du serveur a testé. Sur une PR, une branche de base de même nom est préférée à master.
- Seuls les runs `push`, `schedule` et `workflow_dispatch` de calaos_base sont lus, jamais un run de pull request (celui d'un fork porte le nom de branche du fork ; `-e push` aurait exclu le run mensuel, le filtre est fait sur `event`, `test_fetch_iodoc_artifact.py` avec gh simulé, 9 cas). Côté base, `io-doc` n'est publié que hors pull request, et un run mensuel (`schedule`, ou `workflow_dispatch`) le republie avant les 90 jours d'expiration (revue).
- `IODoc::getDeviceObject(impl)` lit `devices[impl]` (sans UI : INST-4, INST-5) ; `getIOObject("devices")` reste vide ; `tst_servertypes::fixtureCoversTheShippedCatalogue` ignore la clé `devices`.

## Mesuré
Base : `make check` 207 en CI (206 PASS, 1 SKIP) ; `IODocJson_test` 40 cas ; `check-iodoc-generation.sh` ; diff coverage et format verts. Les fichiers de l'installer sont octet pour octet ceux de l'artefact de la CI. Installer : `tst_iodoccatalogue` 18, `tst_servertypes` 35, `test_check_iodoc_sync.py` 13, `test_fetch_iodoc_artifact.py` 9 ; CI Linux, Flatpak, Windows, macOS, iodoc-sync verts. Écart volontaire (PR jetable installer#67, supprimée) : `iodoc-sync` rouge en 5 s, avec le chemin en écart et le template retiré.

## Contre-mutation
Base. CM0 : 0 rouge.

| Mutant | Rouge |
|---|---|
| md : params / conditions / actions dans l'ordre du hash (3 mutants) | IODocJson.TheEntriesAreListedByName |
| JSON : params / conditions / actions dans l'ordre du hash (3 mutants) | IODocJson.TheEntriesAreListedByName |
| tri par description (clés en collision) | TheEntriesAreListedByName, AParamAddedAgainKeepsItsMark, AParamInAChildElementIsMarkedAndTheOthersAreNot, AParamMarkedBeforeItIsAddedIsMarked, TheMarkdownListsRolesFieldsAndChildParams |
| create_iodoc sans --config/--cache, sans --gentemplates, qui ignore le dossier, qui ne range que .json/.md | check-iodoc-generation.sh |
| Makefile ignore IODOC_PREBUILT, accepte un fichier sans devices | check-iodoc-generation.sh |
| create_iodoc génère fr avec le locale C | survit à make check ; attrapé par l'étape CI « fr ≠ en » |

Installer. CM0 : 0 rouge.

| Mutant | Rouge |
|---|---|
| getDeviceObject sensible à la casse | aDeviceImplIsFoundWhateverItsCaseAndAnUnknownOneIsEmpty |
| getIOObject répond pour la clé devices | devicesAreNeverTakenForAnIoType |
| getDeviceObject lit une autre clé | aDeviceIsReadByItsImpl, aDeviceImplIsFound…, everyShippedTemplateBuildsADocumentedDevice |
| getDeviceObject renvoie le premier device | aDeviceIsReadByItsImpl, aDeviceImplIsFound…, devicesAreNeverTakenForAnIoType |
| tst_servertypes ne saute pas devices | fixtureCoversTheShippedCatalogue |
| check_iodoc_sync : seulement en / sans les templates | test_a_device_the_installer_lacks_fails, test_the_templates_are_compared_too, test_a_missing_file_fails |
| check_iodoc_sync : scalaires non comparés | test_a_changed_description_is_named, test_an_array_in_another_order_fails, test_the_templates_are_compared_too |
| check_iodoc_sync : clé du serveur / de l'installer ignorée | test_a_device_the_installer_lacks_fails / test_an_entry_the_server_dropped_fails |
| check_iodoc_sync : fichier absent toléré | test_a_missing_file_fails, test_a_missing_server_file_fails |
| check_iodoc_sync : tableaux comparés comme ensembles | test_an_array_in_another_order_fails |
| fetch_iodoc_artifact : événement non filtré / seulement push | test_a_pull_request_run_is_never_trusted, test_only_pull_request_runs_is_a_failure_without_download, test_the_monthly_and_manual_runs_count |
| fetch_iodoc_artifact : -s success ou -w retiré / repli sur master retiré | test_master_by_default_and_only_green_runs / test_an_unknown_branch_falls_back_to_master |

## Reste ouvert
- Passer `IODOC_PREBUILT` dans la recette du build croisé de la box (calaos-os / yocto) ; le document installé est en anglais.
- DOC-4 : traduction fr des templates (`fr/device_templates.json` est identique à `en`) et contrôle fr ≠ en pour device_templates.json.
- L'artefact `io-doc` expire à 90 jours ; le run mensuel le republie, sauf si GitHub a désactivé le workflow planifié (60 jours sans activité). Quand master de base évolue, les PR de l'installer sont rouges jusqu'à `./generate_iodoc.sh` (voulu).
- Relancer la CI de l'installer avant la fin du run master post-merge échoue (« no io-doc artifact »).
- `io_doc.md` n'est pas comparé (rien ne le lit).
