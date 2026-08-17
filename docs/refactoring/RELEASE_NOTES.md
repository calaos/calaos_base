# Notes de version — changements visibles utilisateur

> Accumulés pendant le refactoring (phases 1→4). À reprendre dans le changelog de la prochaine
> release de calaos_server. Ne liste **que** ce qu'un utilisateur ou un intégrateur peut
> observer — les corrections internes (UAF, fuites, durcissements) ne sont pas ici.
> Ordre : impact décroissant.

## ⚠️ Comportements qui changent sur une installation existante

### Une règle dont un équipement a disparu ne s'exécute plus (décision utilisateur)
Jusqu'ici, si un IO référencé par une règle était supprimé ou renommé, la condition qui le visait
était **rejetée au chargement** et la règle continuait de tourner **amputée**, donc plus
permissive — `si absence ET après 22h → tout éteindre` devenait `si après 22h → tout éteindre`,
se déclenchant tous les soirs.

Désormais une règle dont au moins une condition ou action référence un équipement introuvable est
**entièrement désactivée**. Elle reste **visible et intacte** dans la configuration (rien n'est
perdu à la sauvegarde), est **journalisée** avec son nom et les ids manquants, et **redevient
active d'elle-même** dès que l'équipement réapparaît. Une **notification mail + push** (le canal
déjà utilisé pour les configurations corrompues) le signale au démarrage.

Vérifié : les configurations réelles testées n'ont aucune référence orpheline, donc aucune règle
n'y est désactivée.

### Un scénario dont une étape a perdu son équipement ne démarre plus du tout (décision utilisateur, T3.18)
Jusqu'ici, si l'équipement piloté par une étape de scénario était supprimé, l'étape disparaissait
en silence et le scénario continuait de tourner **en séquence raccourcie** : il s'annonçait avec le
même nom, se déclenchait aux mêmes horaires, mais **sautait** ce qu'il ne pouvait plus faire. Un
scénario « départ en vacances » qui fermait les volets puis coupait le chauffage se contentait de
fermer les volets, sans que rien ne le signale.

Désormais un scénario dont au moins une étape référence un équipement introuvable est
**entièrement désactivé** : il ne démarre plus, ni à l'heure programmée, ni sur commande. Comme
pour les règles, il reste **visible et intact** dans la configuration et est **journalisé** avec
les ids manquants.

**Trois différences avec les règles, et ce sont des choix délibérés :**

- **L'état survit au redémarrage.** La désactivation est enregistrée dans la configuration
  (paramètre `disabled_missing_io` du scénario) ; redémarrer `calaos_server` ne remet pas le
  scénario en marche.
- **Remettre l'équipement ne suffit pas.** Contrairement à une règle, un scénario désactivé ainsi
  **ne se réactive jamais tout seul**. Recréer l'équipement manquant lève l'obstacle mais **pas la
  désactivation**.
- **Il faut une réactivation manuelle explicite**, par la nouvelle commande d'API
  `autoscenario reenable` (disponible sur les deux transports, WebSocket et HTTP). Une réactivation
  demandée **trop tôt est refusée**, avec un message qui **nomme les équipements manquants** — le
  refus est le but de la commande : réactiver un scénario encore amputé le ferait redésactiver au
  contrôle suivant, ce qui ne serait qu'un no-op silencieux de plus.

Le raisonnement : un équipement qui disparaît d'un scénario est un problème de configuration qui ne
peut pas être résolu sans intervention humaine. Un scénario qui repartirait tout seul repartirait
peut-être **incomplet** ; la réactivation manuelle force à constater que la séquence est de nouveau
celle qu'on croit.

> ⚠️ **Rupture de contrat d'API pour les clients.** Le payload de scénario (`get_scenarios` /
> `get_scenario`, autoscénarios) gagne **trois clés** : `broken` (une étape référence un équipement
> introuvable), `disabled_missing_io` (le drapeau persistant décrit ci-dessus) et `missing_ios`
> (les ids concernés). Tout client qui valide strictement la forme de ce payload, ou qui rejette
> les clés inconnues, doit être mis à jour. Sept fichiers de référence de l'API ont été
> régénérés en conséquence.

### IOs Web — les expressions XPath renvoient enfin les bonnes valeurs (E4.4b)
Le moteur XPath (TinyXPath, non maintenu) est remplacé par pugixml. TinyXPath violait XPath 1.0
sur plusieurs points ; les configurations concernées étaient **silencieusement cassées** et vont
se mettre à fonctionner :
- Une expression sélectionnant un **élément** (`/weather/city/temp`, sans `text()`) renvoyait le
  **nom de la balise** → `getValueDouble()` lisait **0** en permanence. Elle renvoie désormais le
  contenu texte, donc la vraie valeur.
- Les **chemins relatifs** multi-étapes (`humidity/text()`, `location/temp[1]/@value`) ne
  renvoyaient **rien**. Ils fonctionnent.
- `string()`, `number()`, `boolean()`, `local-name()`, `round()` n'étaient **pas implémentées**
  et renvoyaient vide. Elles fonctionnent.
- **L'arithmétique tronquait en entier** et débordait en int32 : `//temp/@value + 0` donnait
  `21` au lieu de `21.5` ; `1000000 * 1000000` donnait `-727379968`. Les calculs sont désormais
  en flottant, conformes à XPath 1.0. **C'est le seul cas où une valeur qui « marchait » change.**
- Correction de sécurité au passage : un document sans élément racine (payload réduit à un
  commentaire) faisait **planter le serveur** — c'était un déni de service à distance depuis une
  URL configurée par l'utilisateur.

### Médiathèque et journal d'événements — deux commandes ordinaires tuaient le serveur (T3.19)
Deux appels parfaitement légitimes de l'API JSON **arrêtaient net `calaos_server`**, sans
déconnexion, sans course, sans manipulation particulière : il suffisait d'être **authentifié** et
d'envoyer la commande.

- **Consulter la base musicale d'un lecteur qui n'en a pas.** Toutes les commandes `audio_db`
  (albums, artistes, genres, années, playlists, radios, dossiers, recherche, titres, détail d'une
  piste, statistiques — seize en tout) lisaient la base musicale du lecteur **sans vérifier
  qu'il en avait une**. Un seul type de lecteur en fournit réellement une ; **tous les autres**
  — dont le lecteur Roon de la configuration de référence — faisaient tomber le serveur dès la
  première de ces commandes. Ouvrir l'écran Médiathèque sur un tel lecteur suffisait.
- **Demander une page du journal avec un `per_page` invalide.** `per_page` était utilisé comme
  diviseur sans être vérifié. Or une valeur illisible (`abc`, `true`) est lue comme **zéro**, tout
  comme `0` lui-même : division entière par zéro, dans le thread de la base d'historique, **le
  serveur meurt**. Contre-intuitif, et c'est pourquoi le défaut a survécu si longtemps : une valeur
  **énorme** était inoffensive, c'est la valeur **absurde** qui était fatale.

Les deux sont désormais **refusés proprement**, avec un message qui **nomme la cause**
(`no music database`, `per_page is out of range`) — la forme d'erreur que ces mêmes commandes
produisent déjà pour un `player_id` inconnu ou une plage `from`/`count` invalide. Aucune nouvelle
forme de réponse n'est introduite.

**Ce qui ne change pas** : un lecteur qui **possède** une base musicale répond exactement comme
avant, avec les mêmes champs et la même pagination ; le journal d'événements répond exactement
comme avant pour tout `per_page` qui recevait déjà une réponse — un `per_page` **absent ou vide**
garde la valeur par défaut de 100, et une valeur trop grande reste plafonnée et échouée comme
avant. Aucune liste n'est tronquée, et **une base vide reste une base vide** : elle renvoie une
liste vide, pas une erreur — seule l'**absence** de base est refusée.

### Scénarios — plus de plantage après suppression d'un scénario utilisé par un autre (E4.2f)
Supprimer un scénario B dont l'équipement servait d'action d'étape à un scénario A détruisait les
règles d'étape de A **sans prévenir A** : le scénario A gardait une étape pointant sur de la
mémoire libérée. Le **premier affichage de la liste des scénarios** ensuite (`get_scenarios` /
`get_scenario`, c'est-à-dire l'ouverture de l'écran Scénarios dans l'application ou l'installeur)
relisait cette mémoire — soit un plantage du serveur, soit, pire, un nombre d'étapes et des durées
de pause fantaisistes affichés à l'utilisateur. Le défaut était **atteignable depuis l'API JSON**,
sans manipulation particulière : deux actions ordinaires de l'interface suffisaient, et le
redémarrage du serveur était le seul moyen de retrouver un état sain.

Désormais un scénario **oublie automatiquement** les étapes dont la règle a disparu : la liste
renvoyée à l'interface ne contient plus que les étapes réellement vivantes. Rien à changer dans
les configurations existantes ; un scénario amputé de cette façon affiche simplement moins
d'étapes qu'avant la suppression (le traitement complet de ce cas — désactiver le scénario amputé
plutôt que le raccourcir — est l'objet d'un ticket dédié, T3.18).

### Audio — plus de plantage en consultant la playlist d'un lecteur (T3.17a)
Afficher la playlist d'un lecteur audio (`get_playlist`, c'est-à-dire l'écran Lecteur de
l'application) interroge le lecteur **piste par piste**, avec un aller-retour réseau entre deux
pistes. Deux événements ordinaires survenant pendant cette suite d'allers-retours faisaient
**planter le serveur** :
- **le client se déconnecte** (fermeture de l'application, perte du Wi-Fi, onglet fermé) alors
  qu'une réponse du lecteur est encore en vol — la réponse revenait sur une connexion déjà
  détruite ;
- **le lecteur audio est supprimé** depuis l'installeur ou l'API pendant la consultation — les
  pistes suivantes étaient demandées à un équipement qui n'existait plus.

Le défaut était **atteignable depuis l'API JSON** sans manipulation particulière : une playlist un
peu longue et une déconnexion suffisaient. Il est corrigé dans les deux cas.

**Nouveau comportement observable** : un lecteur supprimé en cours de consultation fait répondre
`{"success":"false"}` au lieu de planter — exactement ce que `get_playlist` répond déjà pour un id
inconnu. Aucune forme d'erreur nouvelle n'est introduite, et une playlist **jamais tronquée** :
soit la liste complète, soit cette réponse d'échec. Une déconnexion en vol n'entraîne, elle,
aucune réponse — le client n'est plus là pour la recevoir.

### Audio — plus de plantage en consultant l'état d'un lecteur au moment de se déconnecter (T3.17b)
Cinq informations de l'écran Lecteur sont demandées au lecteur audio par un aller-retour réseau :
la **durée écoulée** (`get_time`, rafraîchie en continu tant que l'écran est ouvert), la **taille de
la playlist** (`get_playlist_size`), le **détail d'une piste** (`get_playlist_item`), la **pochette**
(`get_cover_url`) et les **statistiques de la médiathèque** (`get_stats`). Si le client **se
déconnectait pendant l'un de ces allers-retours** — application fermée, Wi-Fi perdu, écran quitté —
la réponse revenait sur une connexion déjà détruite et **faisait planter le serveur**.

Le défaut était **atteignable depuis l'API JSON** sans manipulation particulière, et c'est le plus
facile à déclencher de la série : `get_time` est interrogée à répétition tant que l'écran Lecteur
est affiché, si bien qu'il suffisait de quitter cet écran — ou de perdre le réseau — au mauvais
moment. Il est corrigé sur les cinq commandes, et sur les deux transports (WebSocket et HTTP).

**Ce qui ne change pas** : un client **encore connecté** reçoit toujours sa réponse **complète** et
inchangée. Aucune forme de réponse nouvelle n'est introduite, aucun message d'erreur n'est modifié,
et aucune réponse existante ne devient une erreur. Seul le client **déjà parti** ne reçoit plus
rien, ce qui est le comportement attendu puisqu'il n'est plus là pour recevoir.

### Audio — plus de plantage en parcourant la médiathèque au moment de se déconnecter (T3.17c)
Tout l'écran Médiathèque passe par un aller-retour réseau vers la base musicale du lecteur :
**albums**, **artistes**, **genres**, **années**, **playlists**, **radios**, **dossiers de
musique**, **recherche**, **titres d'un album ou d'une playlist**, **détail d'une piste** —
quinze commandes en tout. Si le client **se déconnectait pendant l'un de ces allers-retours** —
application fermée, Wi-Fi perdu, écran quitté — la réponse revenait sur une connexion déjà
détruite et **faisait planter le serveur**.

Le défaut était **atteignable depuis l'API JSON** sans manipulation particulière, et c'est la
surface la plus large de la série : naviguer dans une médiathèque enchaîne ces commandes, et une
base un peu lente à répondre suffisait à ouvrir la fenêtre. Il est corrigé sur les **quinze**
commandes, et sur les deux transports (WebSocket et HTTP).

**Ce qui ne change pas** : un client **encore connecté** reçoit toujours sa réponse **complète** et
inchangée — mêmes champs, même ordre, même pagination, mêmes messages d'erreur (`unkown player_id`
et les autres sont intacts). Aucune liste n'est tronquée, aucune réponse existante ne devient une
erreur. Seul le client **déjà parti** ne reçoit plus rien, ce qui est le comportement attendu
puisqu'il n'est plus là pour recevoir.

### Caméras — plus de plantage en demandant un instantané au moment de se déconnecter (T3.17d)
Demander l'image d'une caméra (`get_picture`, c'est-à-dire la vignette de caméra dans
l'application) déclenche un aller-retour vers la caméra. Si le client **se déconnectait pendant cet
aller-retour** — application fermée, Wi-Fi perdu, page quittée — l'image revenait sur une connexion
déjà détruite et **faisait planter le serveur**. Le défaut était **atteignable depuis l'API JSON**
sans manipulation particulière : une caméra un peu lente à répondre et une déconnexion suffisaient.

**Ce qui ne change pas, et c'est le point important** : un client **encore connecté** reçoit
toujours son image **complète**. C'est vrai même dans les cas limites — une caméra lente, et même
un équipement caméra **supprimé pendant le transfert** : l'image déjà en vol est délivrée
intégralement, jamais tronquée. Seul le client **déjà parti** ne reçoit plus rien, ce qui est le
comportement attendu puisqu'il n'est plus là pour recevoir. Aucune forme de réponse nouvelle n'est
introduite.

### Journal d'événements — plus de plantage en le consultant au moment de se déconnecter (T3.17f)
Consulter l'historique (`eventlog`, c'est-à-dire l'écran Journal / Historique de l'application, que
ce soit la **liste paginée** ou le **détail d'un événement**) déclenche une lecture de la base
d'historique **par un thread séparé**, la réponse revenant au client une fois la requête terminée.
Si le client **se déconnectait pendant cette lecture** — application fermée, Wi-Fi perdu, écran
quitté — la réponse revenait sur une connexion déjà détruite et **faisait planter le serveur**.

Le défaut était **atteignable depuis l'API JSON** sans manipulation particulière : un historique
volumineux, ou simplement une base un peu lente à répondre, suffisait à ouvrir la fenêtre. Il est
corrigé sur les **deux formes** de la commande et sur les **deux transports** (WebSocket et HTTP).

**Ce qui ne change pas** : un client **encore connecté** reçoit toujours sa réponse **complète** et
inchangée — même pagination, même tranche d'événements, mêmes messages d'erreur. Aucune liste n'est
tronquée, aucune réponse existante ne devient une erreur. Seul le client **déjà parti** ne reçoit
plus rien, ce qui est le comportement attendu puisqu'il n'est plus là pour recevoir.

### Fichiers de configuration — deux pertes de fidélité corrigées (E4.4cd)
Le lecteur/écrivain de configuration passe de TinyXML à pugixml. Deux défauts de fidélité des
données, présents de longue date, disparaissent :
- **Blocs CDATA (scripts Lua, messages de notification) : +18 caractères d'espacement parasites.**
  L'imprimeur TinyXML mettait le CDATA sur sa propre ligne, si bien qu'un corps de script de
  21 octets se relisait à 39 depuis un fichier écrit par le serveur. **Non cumulatif**
  (3 générations de sauvegarde restent à +18) et **Calaos relisait proprement** — le préjudice
  était pour les consommateurs conformes au standard : calaos_installer (QDomDocument), XSLT,
  outils tiers. Corrigé : le corps est désormais restitué à l'octet près.
- **Perte de données sur `]]>` dans un script.** Un corps contenant la séquence `]]>` était écrit
  puis relu tronqué (170 octets écrits → 89 relus) et produisait un fichier XML invalide.
  Corrigé : aller-retour intégral.

### Arrêt du serveur — plus de plantage ni de sortie corrompue à l'extinction (T3.14)
À chaque arrêt de `calaos_server`, un message parasite s'affichait, avec un **chemin corrompu**
(octets binaires suivis de la fin lisible du vrai chemin) :
`Parse error: Error document empty. In file <octets illisibles>/local_config.xml`.
La cause était de l'**UB** : des objets statiques (le chemin de configuration, le mutex de
configuration, le cache de niveaux de log, la table des loggers) étaient **réutilisés après que
leur propre destructeur a tourné**. Le message n'était que la partie visible — selon l'ordre de
destruction et les pilotes liés, le même défaut pouvait faire **planter le processus à l'arrêt**
(confirmé par AddressSanitizer sur configurations réelles, via le pilote Wago). Concrètement, un
arrêt ou un redémarrage pouvait se solder par une unité systemd en échec et des lignes illisibles
en fin de journal.

Désormais ces singletons de log et de configuration ne sont **plus jamais détruits** : ils vivent
jusqu'à la fin du processus, donc plus aucun code ne peut les lire après coup. C'est la politique
déjà en vigueur dans le projet (`AGENTS.md:74` : « the server intentionally never frees a number
of process-lifetime singletons »). Ce choix **n'ajoute aucune fuite** : le bilan LeakSanitizer est
**strictement meilleur qu'avant**, une fuite préexistante de 32 octets disparaissant au passage.
Aucun changement de configuration n'est requis.

### Journal d'événements — un `per_page` négatif est désormais refusé (T3.19)
**Ceci n'est pas une correction de plantage, c'est un changement de comportement client**, et il
est déclaré à part pour cette raison : un client qui envoyait un `per_page` **négatif** recevait
des données, il reçoit maintenant un refus.

Un `per_page` négatif était transmis tel quel au moteur de base de données. Or **un `LIMIT`
négatif signifie « pas de limite » en SQLite** (vérifié sur SQLite 3.51.2) : la requête renvoyait
**toutes les lignes** du journal, sous un document qui annonçait pourtant `per_page:-5` — une
réponse qui mentait sur elle-même. La fenêtre était plus large qu'il n'y paraît : avec
`per_page:-5`, l'arithmétique entière du contrôle de page laissait passer la requête pour un
journal de **jusqu'à 9 lignes, sauf exactement 5** ; ce seul cas refusé l'était avec un message
nommant le **mauvais paramètre** (`page is out of range` alors que c'est `per_page` qui était en
cause).

Désormais un `per_page` négatif est refusé **par son nom** (`per_page is out of range`),
immédiatement, sans atteindre la base. **Un client qui envoyait des valeurs négatives verra un
refus là où il recevait des données** — en pratique, il recevait le journal entier, pas la page
qu'il croyait demander.

### Plages horaires — les plages nocturnes fonctionnent (T3.13)
Une plage inversée (`23:00 → 01:00`, la façon naturelle d'écrire « la nuit ») était **vide et ne
se déclenchait jamais**. Elle **wrappe désormais sur minuit**. Avec un jour de semaine :
`23:00 → 01:00 le lundi` = lundi 23h → mardi 1h.
⚠️ Effet de bord à connaître : une borne relative au soleil peut changer de côté selon la
saison, donc une même configuration inchangée peut devenir wrappante une partie de l'année
(ex. `coucher du soleil → 23:00` à latitude élevée en juin). Un log le signale.

### GPIO — le paramètre `debounce` est enfin pris en compte (T2.13, T3.2f)
Il était lu depuis la configuration puis **ignoré** (0,05 s codé en dur). Les installations qui
avaient réglé une valeur vont voir leur réglage s'appliquer. Absent ou invalide → 0,05 s comme
avant.

### OneWire — des capteurs jusque-là invisibles peuvent apparaître (T1.17)
Le filtre de détection des devices avait un bug de bornes : les familles commençant par
`0`, `9`, `A` ou `F` étaient ignorées. Elles sont désormais détectées.

## Drivers retirés
- **MySensors** (T2.12) et **Gadspot** (T3.6) sont supprimés (plus d'utilisateurs). Une
  configuration qui les référence encore démarre normalement : l'IO inconnu est ignoré avec un
  avertissement dans les logs, le reste de l'installation fonctionne.

## Sécurité & réseau
- **TinyXML 2.5.3 (non maintenu, 2 CVE) remplacé par pugixml** — 14 242 lignes de bibliothèque
  tierce retirées du dépôt. Les deux vulnérabilités (plantage du serveur sur XML malformé,
  boucle infinie sur UTF-8 tronqué), atteignables depuis une URL configurée par l'utilisateur via
  les IOs Web, ne sont plus atteignables : le code vulnérable n'existe plus.
- **Limite de connexions par client** : 50 par défaut (`max_connections_per_ip`), auparavant
  illimité. Le client est identifié via `X-Forwarded-For` (haproxy). Au-delà : `429`.
- **En-têtes HTTP** limités à 32 Kio → `431` (auparavant illimité jusqu'au timeout).
- **TLS** : la vérification des certificats reste **désactivée par défaut** pour tous les
  équipements configurés par l'utilisateur (caméras HTTPS auto-signées, devices LAN) — aucune
  installation existante ne change. Nouveau paramètre par équipement `insecure="false"` pour
  activer la vérification. Seuls les services calaos.fr (notifications push) sont vérifiés.
- Les **identifiants ne fuient plus dans les logs** : les URLs de caméras (`pwd=`, `passwd=`,
  `_sid=`) sont masquées.
- `mcp_rate_limit=0` signifie désormais **désactivé** (auparavant : bloquait tout).

## Fiabilité
- **Configuration corrompue** : au lieu de refuser de démarrer, le serveur restaure
  automatiquement le backup le plus récent exploitable, **conserve une copie du fichier corrompu**
  dans `<config>/backups/corrupt/`, et envoie **une notification mail + push** ~30 s après le
  démarrage indiquant le fichier, la copie conservée et le backup restauré.
- **Wago** : le process externe est relancé **indéfiniment** avec un délai court (plafonné à 5 s)
  au lieu d'abandonner — une coupure réseau ou une maintenance ne nécessite plus de redémarrage.
- **MQTT** : les payloads non-UTF8 étaient silencieusement supprimés ; ils sont désormais
  délivrés avec les octets invalides remplacés par `?`.
- **MQTT RGB** : l'état suit désormais le retour du broker (le retour était auparavant ignoré).
  Nécessite que le broker publie sur `topic_sub`.
- **Squeezebox** : un timeout ne bloque plus la file de commandes ; la reconnexion est effective ;
  le callback en échec est désormais appelé (il ne l'était jamais).
- **Amplis Denon/Marantz/Onkyo** : les commandes de volume sont désormais correctement remplies
  à deux chiffres (`MV05` au lieu de `MV5`), conformément aux protocoles constructeurs.
- **OTA RemoteUI** : l'intervalle de re-scan est borné à 30 jours maximum.
- **Conditions script — fin des diffs fantômes dans `rules.xml`** : les déclencheurs d'une
  condition script étaient sérialisés dans l'ordre de hash des **pointeurs** (dépendant de
  l'ASLR), donc deux **exécutions** successives du serveur produisaient des
  `rules.xml` différents dès qu'une condition script avait ≥2 déclencheurs. L'ordre est
  désormais celui du document, stable. (Corrigé au passage par E4.2c.)
- **RemoteUI — le `device_info` n'est plus perdu** : les informations remontées par l'écran lors
  de la provision (modèle, fabricant, firmware, adresse MAC, capacités) étaient écrites dans
  `io.xml` sous le nœud **pièce**, alors qu'elles sont relues **à l'intérieur** de
  `<calaos:remote_ui>` : elles n'étaient donc jamais rechargées. Elles sont désormais écrites là
  où le lecteur les cherche, et celles laissées sous la pièce par les versions précédentes sont
  **récupérées automatiquement** au premier chargement puis réécrites au bon endroit — aucune
  action nécessaire.
- **Suppression d'un IO utilisé par un auto-scénario — plus de plantage** : `RemoveRule(io)`
  pouvait détruire une règle d'auto-scénario sans annuler les back-pointers (`ruleStart`…), et le
  `delete` ultérieur sur ce pointeur périmé dans `deleteAll()` était un **double free** — donc un
  crash du serveur possible en supprimant un IO référencé par un scénario. Corrigé incidemment
  par E4.2d (le nouveau `Remove(Rule*)` refuse et logge au lieu de détruire un objet qu'il ne
  possède pas).
