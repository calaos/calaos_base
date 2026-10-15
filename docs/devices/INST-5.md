# INST-5 — Formulaire de commande généré (calaos_installer)

## Objectif
Dans `FormActionStd`, l'action « mettre un IO à une valeur » d'une règle affiche, pour un device dont IODoc déclare `command_format: json`, un formulaire généré depuis `command_fields` à la place du champ texte. La valeur stockée reste la chaîne JSON.

## Livré
PR calaos/calaos_installer#68.
- `DeviceCommand` (sans widget) : `offer(deviceDoc, instance)` donne les champs proposés et ceux masqués. Un champ n'est proposé que si tous ses `requires_links` existent sur l'instance (lien avec un `io` non vide). Une valeur d'enum n'est proposée que si ses `value_requires_links` existent. `values_param` lit la liste dans les params du device ; un enum sans valeur est masqué. Le param `modes` restreint `mode` seulement si le doc du device le déclare (proxy oui, thermostat non). Un nombre prend bornes, pas et unité de l'IO lié (`value_min`, `value_max`, `step`, `unit`), sinon ceux du doc ; une paire inversée est ignorée. `compose` écrit le JSON compact dans l'ordre de déclaration, nombres réels, puis les clés conservées triées. `classify` distingue vide, objet JSON, mode simple et autre.
- `DeviceCommandForm` (modèle WidgetIOProperties) : une case par champ, seuls les champs cochés sont envoyés (formulaire partiel) ; modifier un champ le coche. Enum = liste déroulante, nombre = `QDoubleSpinBox` avec bornes, pas, décimales du pas et unité. Libellés et valeurs traduits (fr), description en infobulle. Une ligne « N champ(s) de plus ont besoin d'un lien » et « Sent as is : … » pour ce que le formulaire ne montre pas.
- Relecture sans perte : une règle lue n'est jamais réécrite. Les clés inconnues, les champs masqués faute de lien et les types inattendus sont gardés et renvoyés à l'écriture. Une valeur hors liste est ajoutée et signalée ; un nombre hors bornes ou plus précis que le pas élargit le spin au lieu d'être arrondi.
- `FormActionStd` : formulaire seulement si le texte est vide ou un objet JSON et sans valeur dynamique. Un mode simple, un texte illisible ou un val_var gardent le champ texte. Bouton « Edit as text » / « Use the form » ; basculer n'écrit rien. « Dynamic value… » masqué en mode formulaire (spec §5.2). `FormActionStd` émet `actionChanged`, branché par `FormRules` sur `updateItemAction`.
- Revue :
  - un nombre non touché est renvoyé à l'identique (valeur brute gardée tant que le champ n'est ni modifié ni décoché) ;
  - `setDynamicValue(ioId)` grise « Use the form » tant qu'une valeur dynamique existe ;
  - un rôle écrit n'est proposé que s'il est inscriptible (IO écrit = `command_io` sinon `io`, et c'est une sortie, comme `ProxyClimate::isWritable`) ; sinon masqué et annoncé « lien non inscriptible » ;
  - bornes, pas et unité lus sur l'IO écrit ;
  - `modes` vide n'offre aucun mode ;
  - un texte commençant par une espace reste texte ;
  - `activated` coche la ligne ;
  - une valeur hors liste dit sa vraie cause ;
  - une paire low/high plus proche que `min_range` est signalée (le serveur la refuse).
- Fixtures `tests/tst_devicecommand/project.xml`, catalogue réel de `data/doc`.

## Mesuré
`tst_devicecommand` : 70 cas (génération proxy et thermostat, liens manquants, `value_requires_links`, `modes`, `values_param`, bornes de l'IO, JSON compact, aller-retour de 9 formes, texte inconnu conservé, formulaire et `FormActionStd`) ; `make check` 13 suites vertes ; application compilée ; CI 5/5 (dont iodoc-sync). Captures offscreen (`INST5_SHOTS`) pour 8 cas.

## Contre-mutation
CM0 : 0 rouge. 62 mutants (45 + 17 de la revue), 61 tués, 1 équivalent (IO ordinaire traité comme device : sans param `impl`, même comportement). Tués : requires_links « un » au lieu de « tous », value_requires_links ignoré, values_param ignoré, modes non appliqué ou appliqué sans être documenté, bornes / unité / pas de l'IO ignorés, paire inversée gardée, lien sans io compté, décimales du pas à 0, isJson toujours vrai, liste non rognée, ordre inversé, clés gardées perdues, JSON non compact, classify erroné (×2), nombres en chaînes, édition qui ne coche pas, champs décochés envoyés, clé consommée gardée, valeur hors liste non affichée ou non signalée, décimales arrondies, bornes non élargies, hors bornes non signalé, drapeau survivant, mode simple non lu, setValue qui émet, masqués / clés conservées non annoncés, FormActionStd qui ouvre le formulaire sur un mode simple ou un texte illisible, valeur dynamique ignorée, formulaire qui réécrit la valeur, éditions non stockées ou non annoncées, formulaire resté visible, champ texte resté à côté, bouton de bascule toujours actif, éditions du texte non stockées.

## Reste ouvert
- Thermostat `emitter=floor` avec `output_cool` mais sans lien `humidity` : le serveur refuse low/high et cool/auto (`canCool`), le formulaire les propose encore (règle portée par `required_when`, pas par `requires_links`).
- La règle « sortie obligatoire » s'applique aussi au thermostat (le serveur ne la teste que pour le proxy) : sans effet en pratique, ses IO de consigne sont internes.
- Les clés conservées ressortent triées par nom ; le branchement `FormRules` → `actionChanged` n'est couvert que par la compilation ; popup non essayé à la main ; seul le fr est traduit.
