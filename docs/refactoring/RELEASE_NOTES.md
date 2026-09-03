# Notes de version — changements visibles utilisateur

> Accumulés pendant le refactoring (phases 1→4). À reprendre dans le changelog de la prochaine
> release de calaos_server. Ne liste **que** ce qu'un utilisateur ou un intégrateur peut
> observer — les corrections internes (UAF, fuites, durcissements) ne sont pas ici, **sauf quand
> le défaut se manifeste par un plantage ou un comportement erratique du serveur** : ce que
> l'utilisateur observe alors, c'est le symptôme, et il a besoin de savoir qu'il a disparu.
> Ordre : impact décroissant.

## 🔴 Volets et variateurs : une commande incomplète pouvait déclencher un mouvement que personne n'avait demandé

### Un volet pouvait partir en course complète sur une commande d'impulsion **tronquée** (T3.25)

Les volets Calaos acceptent une commande d'**impulsion** : « monte pendant 500 ms », par exemple,
pour entrouvrir sans aller jusqu'à la butée. La durée est envoyée avec la commande.

Jusqu'ici, si cette durée **manquait** — la commande envoyée s'arrêtait juste après « monte
pendant », sans nombre derrière — le serveur ne s'en apercevait pas. Il lisait une durée
**quelconque**, prise dans une zone de mémoire qui n'avait jamais été remplie : parfois un très
grand nombre, parfois un nombre négatif, et **jamais deux fois la même**. Le volet partait alors
pour une durée qui n'avait aucun rapport avec la commande. Dans le cas le plus fréquent — une
grande valeur — **aucune minuterie d'arrêt n'était armée du tout** : au lieu d'un à-coup de
quelques centaines de millisecondes, le volet **montait jusqu'à sa butée**.

La valeur inventée était en plus **publiée dans l'état de l'équipement** et **renvoyée à toutes
les applications connectées**, qui affichaient donc une durée que personne n'avait choisie.

Les variateurs d'éclairage avaient la même faiblesse sur leurs commandes à argument (« règle à
… % », « impulsion de … ms ») : la lampe changeait de niveau, vers un niveau arbitraire.

> ### Êtes-vous concerné ?
>
> Il faut réunir deux conditions, et il est important de dire les deux :
>
> - **N'importe quel compte ayant accès à l'API suffit.** Aucun privilège particulier n'est requis
>   et aucun réglage ne pouvait le refuser : la commande de pilotage `set_state` est ouverte à
>   toute session authentifiée, sur les deux transports (WebSocket et HTTP), **y compris une
>   session en portée « service »**. C'est le point qui rend cette correction prioritaire.
> - **mais il faut que la commande soit malformée**, c'est-à-dire tronquée juste après le mot de
>   la commande. **Aucune application Calaos ne produit ce message** : ni Calaos Home, ni
>   l'interface web, ni les écrans tactiles. En pratique, cela vous concerne si un **script**, une
>   **automatisation maison**, une **intégration tierce** ou un outil de test construit ses
>   commandes lui-même — c'est-à-dire un cas réel, mais pas un cas courant.
>
> **Ce n'est pas une prise de contrôle** : la valeur n'était pas choisie par celui qui envoyait la
> commande, seulement imprévisible. **Et ce n'est pas anodin non plus** : un compte ordinaire
> pouvait provoquer un mouvement de volet **différent de celui qu'il avait demandé**, de façon
> **non reproductible**, sur du matériel qui pince les doigts.

**Ce qui change.** Deux protections, à deux endroits :

- **Une commande TRONQUÉE est désormais refusée** par l'API, avec la réponse d'échec habituelle
  (`success: false`), et l'équipement n'est **pas** touché. Le refus est **journalisé** avec le
  nom de l'équipement et la valeur reçue.
- **Et si une telle commande arrivait quand même** par un chemin interne (une règle, un scénario,
  un script Lua), la durée lue vaut maintenant **zéro** au lieu d'être imprévisible : elle est
  **définie et reproductible**.

> ### ⚠️ Ce que cette version ne corrige PAS — le périmètre exact
>
> La correction porte sur la commande **tronquée** — celle qui s'arrête juste après « monte
> pendant », **sans rien derrière**. C'est le cas qui lisait une durée **jamais initialisée**, et
> c'est celui-là qui est fermé.
>
> **Une commande complète mais absurde n'est pas concernée**, et il faut le dire : une durée
> **numériquement trop grande** pour être représentée — par exemple `impulse up 99999999999999999999` —
> **passe la validation** (elle n'est pas tronquée), et le serveur la ramène à la plus grande durée
> représentable. **Le scénario « très grande valeur ⇒ le volet monte jusqu'à sa butée » reste donc
> ouvert par ce chemin-là.** Il n'est pas nouveau, il n'est pas aggravé, et il demande une commande
> qu'aucune application Calaos ne produit — mais il n'est **pas** fermé par cette version, et la
> version précédente de cette note laissait croire le contraire.
>
> Le correctif durable est identifié et tient en deux lignes par commande dans l'équipement
> lui-même (rejeter l'argument s'il n'est pas un nombre lisible, plutôt que de le deviner) ; il est
> **suivi séparément** parce qu'il touche un fichier en cours de modification par un autre travail.

> ⚠️ **Un changement de comportement à connaître si vous scriptez l'API.** Le refus ci-dessus
> porte sur **toute** valeur de `set_state` qui **se termine par une espace ou une tabulation**,
> quel que soit le type d'équipement. C'est volontairement une règle simple, sans exception : elle
> continuera de protéger les équipements ajoutés plus tard. La conséquence est qu'une variable de
> type **texte** ne peut plus être réglée à une valeur **finissant par une espace** (`"note "`)
> par cette commande — retirez l'espace de fin, ou ajoutez un caractère après. Les espaces au
> **début** et **à l'intérieur** de la valeur sont conservées comme avant.

### Effets de bord bénéfiques de la même correction

La lecture des nombres depuis la configuration a été corrigée au même endroit pour tout le
serveur. Conséquences visibles :

- **Un équipement Wago dont le paramètre d'adresse (`var`) est absent est désormais signalé dans
  le journal** au lieu d'être lu silencieusement comme l'adresse 0. Le comportement, lui, ne
  change pas : c'est bien l'adresse 0 qui continue d'être utilisée.
- **Un équipement GPIO ou un écran distant dont un réglage numérique est vide retombe désormais
  sur sa valeur par défaut documentée** au lieu de retomber sur zéro.
- Les valeurs de configuration **vides** ne sont plus confondues avec la valeur **zéro** : une
  ligne oubliée dans un fichier de configuration se comporte maintenant comme « non renseignée »,
  et non plus comme « réglée à 0 ».

**Et une seconde série d'équipements, trouvée à la relecture, où un réglage vide donnait une
valeur qui n'existe pas** — ils prennent maintenant leur défaut documenté :

- **Relais d'un écran RemoteUI** : un `relay_num` **absent** commandait le relais **0**, qui
  n'existe pas (les relais sont numérotés à partir de 1). C'est le plus facile à rencontrer de la
  série : ce paramètre n'avait **aucune garde**, il suffisait qu'il manque.
- **Équipements Wago** : un paramètre `port` **présent mais vide** faisait parler le serveur sur
  le **port Modbus 0** au lieu du 502 habituel — c'est-à-dire plus du tout.
- **Amplificateurs audio-vidéo** : un paramètre `zone` vide donnait la **zone 0**, qui ne
  correspond à aucune des trois zones réelles ; l'équipement **ne remontait alors plus jamais de
  changement d'état**. Un `port` vide donnait de même le port 0.
- **Entrées analogiques** : une `precision` vide donnait **0 décimale** au lieu de 2.
- **Plages horaires au lever/coucher du soleil** : un décalage (`start_offset`/`end_offset`) vide
  **annulait le décalage** au lieu de conserver son sens — la plage se déclenchait à l'heure
  exacte du lever ou du coucher.

### ⚠️ Changement de comportement à connaître : un nombre TROP GRAND est maintenant refusé, là où il était silencieusement ramené à la plus grande valeur possible

C'est le pendant du point précédent, et il vaut la peine d'être lu par quiconque **scripte l'API**
ou **écrit sa configuration à la main**.

Le serveur teste très souvent « est-ce que ce texte est un nombre ? » avant de s'en servir. Ce test
répondait **oui** à un nombre qui ne **tient pas** dans un entier — `99999999999`, par exemple.
Le serveur le ramenait alors, sans rien dire, à **2 147 483 647** (le plus grand entier qu'il sache
manipuler) et continuait comme si de rien n'était. Il répond désormais **non**, et le nombre est
traité comme ce qu'il est : une valeur illisible.

**Ce que vous verrez changer, concrètement :**

- **Bibliothèque musicale.** Une requête de liste avec un `count` (ou un `from`) trop grand —
  `{"from":"0","count":"99999999999"}` — était **acceptée** et interrogeait la base avec
  2 147 483 647. Elle reçoit maintenant `{"error":"wrong from/count"}` et la base n'est pas
  interrogée. C'est le cas le plus susceptible d'être rencontré par un script : **14 requêtes de
  liste** (albums, artistes, genres, années, listes de lecture, radios, recherche…) partagent
  exactement cette validation. ⚠️ **Attention** : la validation n'a jamais vérifié les **bornes**,
  et elle ne le fait toujours pas — un `count` **négatif** passe comme avant. Ce qui change est
  uniquement qu'un nombre qui ne tient pas dans un entier a cessé d'être appelé un entier.
- **Volets et variateurs.** `impulse 99999999999` armait une minuterie de **2 147 483 secondes,
  soit près de 25 jours**, avec la lampe allumée ; `set_state 99999999999` sur un variateur le
  poussait à **100 %** (la valeur ramenée puis bornée). Ces commandes ne font désormais **plus
  rien du tout** — l'équipement ne bouge pas.
- **KNX.** Une valeur entière hors bornes reçue du bus laisse maintenant le champ à **0** au lieu
  de le remplir avec 2 147 483 647. ⚠️ **Les deux sont faux** : la trame ne disait ni l'un ni
  l'autre. **0 est le moins dangereux des deux sur un bus KNX**, et c'est celui qui a été retenu ;
  aucune borne n'a été ajoutée.
- **Amplificateurs audio-vidéo.** Une trame de volume aberrante venant du réseau est maintenant
  **ignorée** (le volume précédent est conservé) au lieu de produire un événement de volume
  absurde.
- **Sonde de présence réseau (ping).** Un `timeout` ou un `interval` trop grand retombe sur la
  valeur par défaut documentée au lieu d'être passé tel quel à la commande système ou de suspendre
  la scrutation pendant 25 jours.

**Ce qui ne change PAS** : les valeurs à la limite exacte restent valides — `2147483647` est
toujours accepté, `2147483648` ne l'est plus. Et un nombre **suivi de texte** (`12abc`, `1,5`)
continue d'être lu comme avant, c'est-à-dire partiellement : ce ticket n'y a pas touché.

## 🔴 Caméras Reolink : corruption mémoire à chaque enregistrement de caméra

### Le serveur écrivait dans de la mémoire libérée dès qu'une caméra Reolink était enregistrée (E4.1i)
Si vous avez au moins un `ReolinkInputSwitch` dans votre configuration et que `calaos_server`
plantait, se figeait ou se comportait de façon inexplicable **sans rapport apparent avec les
caméras**, la cause pouvait être ici.

Chaque fois que Calaos annonçait une caméra au processus `calaos_reolink`, il **libérait** le
message qu'il venait de construire, puis **écrivait à nouveau dedans**. Le bloc de mémoire venait
d'être rendu à l'allocateur : entre-temps, n'importe quelle autre partie du serveur pouvait l'avoir
repris. Le plus souvent l'écriture retombait sur un bloc encore libre et **ne se voyait pas** ;
mais dès que le bloc avait été **repris par autre chose**, elle abîmait *les données de quelqu'un
d'autre*, et le symptôme — plantage, valeur aberrante, blocage — apparaissait **ailleurs et plus
tard**, ce qui rend ce genre de défaut particulièrement difficile à relier à sa cause. C'est bien
pour ça qu'il est corrigé plutôt que toléré : on ne peut pas savoir d'avance dans quel camp on
tombe.

Ce n'était pas un cas rare : cela se produisait à l'enregistrement de **chaque caméra**, et de
nouveau **pour toutes les caméras à chaque redémarrage** du processus `calaos_reolink` — lequel se
relance automatiquement, en boucle, quand il s'arrête. Une installation avec plusieurs caméras et
un processus instable déclenchait donc le défaut en continu.

Le message est désormais libéré **une seule fois**. Aucun changement de configuration, aucun
changement de comportement visible côté caméras : les événements de détection, la reconnexion
automatique et l'API restent identiques.

→ **Rien à faire de votre côté.**

---

## 🔴 Le sidecar MCP ne démarrait pas

### L'assistant MCP était mort dans les images publiées (T3.23)
Si vous avez activé le sidecar MCP — celui qui permet à un assistant IA de piloter votre
installation Calaos — et qu'il ne répondait pas, **ce n'est pas votre configuration**.

Les images `ghcr.io/calaos/calaos_base` reconstruites récemment embarquaient une version de la
bibliothèque `mcp` **incompatible** avec le code de Calaos. Le sidecar s'arrêtait immédiatement au
démarrage, sur une erreur d'import, **avant même** d'ouvrir sa socket. Le reste de calaos_server
(règles, IOs, API JSON, interface web) n'était pas affecté — seul l'accès MCP l'était, et il
l'était **totalement** : aucun assistant ne pouvait se connecter.

Cause : l'image installait ses dépendances Python **sans figer leurs versions**. Chaque
reconstruction attrapait ce que PyPI publiait ce jour-là ; le jour où `mcp` est passé en 2.0, le
module dont Calaos a besoin a disparu et l'image a été publiée cassée. Rien ne l'a signalé : le
contrôle au moment de la compilation se contentait de vérifier que la bibliothèque était
**installée**, pas qu'elle offrait encore ce que Calaos lui demande.

Les versions sont désormais figées dans un seul fichier, et ce fichier est celui que l'image
installe réellement. Le contrôle de compilation, lui, importe maintenant l'API que le sidecar
utilise : si une mise à jour la retire, **la construction échoue** au lieu de publier un sidecar
qui ne démarre pas. Un `calaos_mcp --help` suffit désormais à vérifier qu'une image est saine.

→ **Rien à faire de votre côté** : mettez à jour vers une image postérieure à ce correctif.

---

## 🔴 MQTT et Web : une faute de frappe dans un `path` arrêtait le serveur

### Un `path` contenant un crochet isolé faisait s'arrêter `calaos_server` (T3.35)
Si `calaos_server` **s'arrêtait net** peu après un démarrage ou après avoir modifié une
entrée/sortie **MQTT** ou **Web**, sans message d'erreur exploitable, regardez le paramètre
**`path`** des entrées que vous veniez de toucher.

Il suffisait qu'un `path` contienne un crochet ouvrant **tout seul** — `[`, ou
`weather/[/description` : une **faute de frappe**, une parenthèse effacée à moitié, un copier-coller
tronqué — pour que le serveur **s'arrête** au moment où il lisait la valeur, c'est-à-dire à chaque
message reçu du capteur ou à chaque interrogation de la page web. Ce n'était pas un cas rare à
provoquer : la syntaxe des index de tableau **contient** des crochets, donc c'est exactement dans ce
paramètre-là qu'on en tape.

⛔ **Rien à craindre de l'extérieur** : un `path` n'est écrit que depuis `calaos_installer`, jamais
par un client de l'API, un navigateur ou un appareil du réseau. Il fallait donc y avoir accès pour
déclencher le défaut — mais il fallait aussi n'avoir fait qu'**une faute de frappe**.

**Ce que vous verrez désormais.** Le serveur **continue de tourner**. L'entrée/sortie concernée rend
une valeur **vide**, comme pour n'importe quel chemin qui ne mène nulle part, et le journal de
`calaos_server` nomme le jeton fautif :

```
[WRN] (MqttCtrl.cpp) Error in path weather/[/description, malformed array index [ :
      an array index must be written [n], as in weather/[0]/description
```

### Et quand le chemin ne trouve rien, le journal dit maintenant quoi corriger
Deuxième changement, au même endroit. Si vous aviez copié l'**ancienne** syntaxe d'index — celle que
`calaos_installer` affichait à tort avant la correction ci-dessous — vous obteniez une valeur vide
et un message qui se contentait de dire que la clé n'existait pas. Le journal ajoute désormais la
correction à faire :

```
[WRN] (MqttCtrl.cpp) Error in path weather[0]/description, subpath not found weather[0] : …
[WRN] (MqttCtrl.cpp) Error in path weather[0]/description, did you mean weather/[0] ?
      array indices are their own path segment, not glued to the key that precedes them
```

⚠️ **Cette aide ne se déclenche jamais à tort.** Elle n'apparaît que lorsque la recherche a
**réellement échoué**. Si votre appareil publie un payload dont une clé s'appelle *vraiment*
`action[0]` — c'est le cas de certains boutons Zigbee2MQTT — ce chemin **fonctionne**, continue de
fonctionner exactement comme avant, et **ne reçoit aucun message**. Rien de ce qui marchait ne
change.

⚠️ **Là où le message arrive** : dans le journal de **`calaos_server`**, pas dans
`calaos_installer`. Si vous configurez depuis l'installeur et que la valeur reste vide, c'est le
journal du serveur qu'il faut ouvrir.

→ **Rien à faire de votre côté**, sauf si le serveur s'arrêtait sans raison apparente : vérifiez
alors vos paramètres `path` — la faute de frappe est probablement toujours là, elle est simplement
devenue inoffensive.

### Un `path` fautif ne peut plus inventer un niveau de batterie ni une couleur (T3.35b)

Corriger le point ci-dessus avait un effet de bord qu'il a fallu fermer à son tour. Le serveur ne
s'arrêtait plus — mais l'entrée/sortie **croyait avoir lu une valeur** alors qu'elle n'avait rien lu.
Concrètement, si le `path` d'un **niveau de batterie**, d'une **qualité de signal** ou d'un
**uptime** ne menait nulle part, la valeur remontée dans l'interface était **quelconque** : un
nombre sans rapport, parfois énorme, qui avait toutes les apparences d'une mesure. Vos **règles**
pouvaient s'en servir. Même chose pour les ampoules **couleur** MQTT dont l'un des trois chemins
(`path_x`, `path_y`, `path_brightness`) était mal saisi : la couleur affichée était fabriquée à
partir de valeurs jamais lues.

**Ce que vous verrez désormais.** Un chemin qui ne trouve rien **ne met plus rien à jour** : la
valeur précédente reste en place, et le journal de `calaos_server` dit ce qu'il n'a pas su lire.
C'est le comportement que vous aviez déjà pour n'importe quel autre chemin invalide.

⚠️ **Un cas peut vous surprendre, et c'est voulu** : si un `path` de batterie pointait sur un texte
ou sur un objet JSON au lieu d'un nombre, l'entrée affichait jusqu'ici une valeur (fausse) et
n'affiche désormais **plus rien**, avec un message dans le journal. Si une valeur d'état a disparu
de votre interface après cette mise à jour, **c'est ce paramètre-là qu'il faut corriger** — elle
n'a jamais été juste.

**Trois autres formes de `path` qui mentaient en silence** sont corrigées au même endroit : un index
de tableau **sans crochet fermant** (`weather/[5`, `weather/[12`) lisait un élément **au hasard**
(le premier, le deuxième…) sans rien dire ; il est maintenant refusé et signalé. Et un index
**illisible** (`weather/[zz]`, `weather/[]`) continue de lire le premier élément — comportement
inchangé, pour ne casser aucune configuration existante — mais le journal le **dit** désormais, au
lieu de laisser croire à une lecture normale.

### Les messages de `path` dans le journal changent de préfixe (T3.37)
⚠️ **À lire si vous filtrez le journal de `calaos_server`.** Les deux analyseurs de `path` — celui
des entrées/sorties **MQTT** et celui des entrées/sorties **Web** — étaient **deux copies du même
code**. Ils n'en font plus qu'une, ce qui veut dire que la correction du plantage ci-dessus n'aura
plus à être écrite deux fois. **Conséquence visible** : tous les messages de `path` cités plus haut
sortent désormais sous le préfixe **`(JsonPath.h)`**, et non plus `(MqttCtrl.cpp)` ou
`(WebCtrl.cpp)` :

```
avant  [WRN] (MqttCtrl.cpp:151) Error in path weather/[/description, malformed array index [ : …
avant  [WRN] (WebCtrl.cpp:221)  Error in path weather/[/description, malformed array index [ : …
après  [WRN] (JsonPath.h:169)   Error in path weather/[/description, malformed array index [ : …
```

- **Si vous avez un filtre ou un `grep` sur `MqttCtrl.cpp` ou `WebCtrl.cpp`**, il cessera de voir
  ces lignes-là. Les messages eux-mêmes sont **inchangés au caractère près**.
- ⛔ **Le préfixe ne distingue plus MQTT de Web.** C'est le prix d'un seul exemplaire du code, et
  c'est assumé : ce qui identifie la ligne reste le **chemin**, qui est écrit dedans et qui vient
  de la configuration d'une entrée précise. L'ancien préfixe ne nommait de toute façon que le
  *pilote*, jamais l'entrée.
- ⚠️ **Mais le `chemin` n'est pas une clé unique.** Si vous avez une entrée/sortie **Web** et une
  entrée/sortie **MQTT** configurées sur le **même** `path` — ce qui est banal, par exemple
  `weather/[0]/description` sur les deux —, leurs lignes de journal deviennent **strictement
  indiscernables** : même préfixe `(JsonPath.h:NNN)`, même texte, même chemin. Avant, le préfixe
  les séparait. Pour lever le doute il faut désormais recouper avec les lignes **voisines**, qui
  gardent, elles, leur préfixe d'origine (`Failed to open WebCtrl file`, `Error parsing …` côté
  MQTT).
- Les messages qui **ne** viennent pas de l'analyseur de chemin — ouverture du fichier Web, payload
  MQTT illisible — gardent leur préfixe d'origine.

→ **Rien à faire de votre côté**, sauf à mettre à jour un filtre de journal si vous en avez un.

### Un `path` qui ne contient que des barres obliques est maintenant signalé (T3.37)
Petit changement, **de journal uniquement**. Un `path` fait uniquement de séparateurs — `/`, `///`,
ou vide côté **Web** — ne désigne aucune clé : il n'y a rien à aller chercher. Une entrée/sortie
**MQTT** rendait dans ce cas une valeur vide **sans rien écrire du tout** dans le journal, et une
entrée/sortie **Web** écrivait `Error emtpy path not allowed`, qui ne disait ni ce qui n'allait pas
ni **quel** paramètre était en cause — impossible à rattacher à une entrée quand plusieurs
interrogent en même temps. Les deux disent désormais la même chose, et nomment le chemin :

```
[WRN] (JsonPath.h) Error in path ///, no path segment to resolve : a path must name at
      least one key or index, as in weather/[0]/description
```

⛔ **Aucune valeur ne change.** Les entrées/sorties MQTT et Web rendent exactement ce qu'elles
rendaient avant, dans tous les cas ; seul le message du journal est ajouté ou reformulé.

⚠️ **Côté MQTT, un `path` VIDE reste un cas à part et n'est pas concerné** : il veut dire « la
charge utile est la valeur » — c'est ainsi qu'on lit un appareil qui publie un nombre nu au lieu
d'un document JSON — et il continue de rendre la charge brute, sans message.

→ **Rien à faire de votre côté.**

---

## 🔴 MQTT et Web : la syntaxe d'index de tableau affichée par `calaos_installer` était fausse

### Si votre paramètre `path` contient des crochets, il ne lisait probablement rien (T3.29)
Le paramètre **`path`** d'une entrée/sortie **MQTT** ou **Web** sert à aller chercher une valeur
dans un document JSON envoyé par un équipement. Quand cette valeur est rangée dans un **tableau**
— c'est le cas courant avec Zigbee2MQTT, Tasmota ou OpenWeather — il faut donner l'indice de
l'élément voulu.

**L'aide de paramètre affichée par `calaos_installer` donnait l'exemple `weather[0]/description`.
Cette forme ne fonctionne pas.** Rien ne vous le dit là où vous travaillez : l'IO reste simplement
**vide**, pour toujours. L'échec *est* bien journalisé (`[WRN] … subpath not found`), mais dans le
log de **`calaos_server`** — alors que la faute se commet dans **`calaos_installer`**, où vous ne
voyez rien. C'est le pire des deux mondes — la source que l'on consulte *au moment exact* où l'on
configure l'IO enseignait une syntaxe sans effet.

**La forme qui marche met l'indice dans son propre segment de chemin, entre crochets :**

| Charge utile reçue | ❌ ce qui était documenté | ✅ ce qu'il faut écrire |
|---|---|---|
| `{"weather":[{"description":"pluie"}]}` | `weather[0]/description` | **`weather/[0]/description`** |
| `{"e":[{},{"t":21.5}]}` | `e[1]/t` | **`e/[1]/t`** |

La règle, en une phrase : **un indice de tableau est un segment de chemin à lui seul**, séparé par
des `/` comme n'importe quelle clé. Elle est maintenant écrite dans l'aide de chaque paramètre
concerné (les 7 `*path*` du MQTT et le `path` du Web), en anglais comme en français.

### Ce que vous avez à faire
**Rien ne change dans le serveur** : le comportement du parseur est **exactement le même
qu'avant**, seule la documentation est corrigée. Autrement dit, une configuration écrite d'après
l'ancien exemple **ne marchait déjà pas** — la corriger ne casse rien, ça la fait marcher.

1. Ouvrez `calaos_installer` et regardez les IOs **MQTT** et **Web** dont le `path` (ou
   `battery_path`, `connected_status_path`, `wireless_signal_path`, `uptime_path`,
   `ip_address_path`, `wifi_ssid_path`) contient des crochets.
2. Insérez un `/` **devant** le crochet ouvrant : `weather[0]/description` →
   `weather/[0]/description`.
3. Les `path` sans crochets (`temperature`, `color/x`, `main/temp`…) sont corrects et **ne doivent
   pas être touchés**.

⚠️ **Un seul cas où il ne faut PAS corriger** : si votre équipement envoie réellement une clé
*nommée* `weather[0]` (des crochets dans le nom de la clé, pas un tableau), alors
`weather[0]/description` était et reste la bonne écriture. C'est rare, mais c'est aussi la raison
pour laquelle le serveur **n'a pas** été rendu tolérant aux deux formes : il ne peut pas deviner
laquelle des deux vous vouliez dire.

---

## 🔴 Roon (hôte statique) : mauvais port au démarrage, adresse perdue à la première relance

### Qui est concerné, et qui ne l'est pas (T3.28)

**Vous n'êtes concerné que si vous avez rempli le champ `host` de votre lecteur Roon** dans
`calaos_installer` pour désigner un core précis sur votre réseau.

**Si vous avez laissé `host` vide — le mode par défaut, celui que l'aide du paramètre recommande —
tout fonctionnait, et rien ne change pour vous.** Calaos ne passait alors aucune option au
processus `calaos_roon`, qui cherchait le core tout seul sur le réseau. C'est le cas de la grande
majorité des installations. Nous le disons explicitement parce qu'un premier diagnostic interne
avait conclu, à tort, que « l'intégration Roon est inutilisable » : **c'est faux**, seule la
configuration à hôte statique l'était.

### Ce qui se passait avec un hôte statique

Deux défauts se cumulaient sur ce chemin, et un seul suffisait à le casser.

1. **Le port partait en vrac.** Le numéro de port n'était jamais lu correctement : Calaos
   transmettait au processus `calaos_roon` une valeur **prise au hasard dans la mémoire**. D'un
   démarrage à l'autre elle changeait, ce qui explique pourquoi le symptôme pouvait sembler
   capricieux — une fois sur mille, la valeur tombait par chance sur le bon numéro. Le port par
   défaut de Roon (9330) ne pouvait pas rattraper le coup : il n'entre en jeu que lorsque Calaos
   ne dit **rien** du port, et Calaos disait toujours quelque chose dès que `host` était rempli.
2. **Au premier redémarrage du processus, l'adresse était oubliée.** Le processus `calaos_roon`
   se relance automatiquement quand il s'arrête ; il repartait alors **sans `host` ni port**,
   c'est-à-dire en recherche automatique. Votre lecteur pouvait donc finir par fonctionner —
   mais **sur le core que le réseau a bien voulu rendre**, pas forcément celui que vous aviez
   désigné. Sur une installation avec plusieurs cores Roon, ce n'est pas la même chose.

### Ce qui change

- L'adresse **et** le port que vous configurez sont désormais transmis, **au premier démarrage
  comme à chaque relance**.
- Un port laissé **vide**, ou saisi de travers (`abc`, `0`, `70000`…), retombe proprement sur le
  port standard de Roon, **9330**, au lieu de partir en valeur aléatoire.
- Dans `calaos_installer`, la fiche du paramètre **`port`** change : il était affiché
  **« obligatoire, sans valeur par défaut »**, ce qui contredisait sa propre description
  (« laisser vide pour détecter automatiquement »). Il est maintenant affiché **facultatif, avec
  la valeur par défaut 9330 et la plage 1–65535**.

### Ce que vous avez à faire

**Rien, dans la plupart des cas** — mettez à jour et redémarrez `calaos_server`.

1. Si vous aviez **renoncé** à l'hôte statique parce qu'il ne marchait pas et que vous êtes repassé
   en détection automatique : vous pouvez le réessayer, il fonctionne.
2. Si vous aviez rempli `host` **et** `port` : vérifiez simplement que le port est bien celui de
   votre core (**9330** sauf configuration particulière). Il est maintenant **réellement utilisé** —
   avant, il ne l'était pas, et une valeur fausse laissée là passait donc inaperçue.
3. Si votre champ `port` est **vide** : laissez-le vide, 9330 sera pris.

⚠️ **Cette correction n'a pas été essayée sur un core Roon réel.** Ce qui est vérifié par les
tests, c'est que Calaos transmet bien l'adresse et le port configurés, et qu'il les retransmet à
chaque relance. Si un problème de connexion subsiste chez vous après la mise à jour, il est
ailleurs — signalez-le.

---

## 🔴 Le serveur pouvait tomber après une modification de configuration

### Ce que vous avez pu observer (T3.40)
Si `calaos_server` **s'est arrêté, figé ou comporté bizarrement peu après** que vous ayez
**supprimé ou modifié un équipement** — depuis `calaos_installer`, depuis l'interface, ou par
n'importe quel outil qui parle à l'API — sans rapport apparent avec ce que vous veniez de toucher,
la cause pouvait être ici.

Quand Calaos supprime un équipement, il détruit l'objet correspondant **immédiatement**. Or
plusieurs types d'équipements programment des petites actions **différées** : un scénario ou un
bouton remet son état à zéro **250 ms** après avoir été déclenché, un équipement **KNX** demande la
valeur courante au bus **1,5 seconde** après son démarrage, un lecteur **Roon** prépare sa réponse
pour le tour suivant. Ces actions différées **n'étaient rattachées à rien** : plus rien ne pouvait
les annuler. Si l'équipement disparaissait entre-temps, l'action différée s'exécutait quand même,
**sur un objet qui n'existait plus**.

Ce qui se passait alors n'est pas prévisible — c'est ce qui rend ce genre de défaut difficile à
relier à sa cause :

- souvent **rien de visible**, la mémoire libérée n'ayant pas encore été réutilisée ;
- parfois un **arrêt brutal** du serveur ;
- parfois, plus insidieux, l'action retombait sur **un autre équipement** entre-temps installé à la
  même adresse mémoire, et **agissait à sa place**. Un cas de ce genre a été observé sur les volets
  au ticket précédent : un volet vivant **arrêté** par la minuterie d'un volet supprimé.

⚠️ **Il ne fallait aucune commande douteuse pour y arriver.** Une action parfaitement normale, suivie
d'une modification de configuration dans la seconde qui suit, suffisait. La fenêtre est courte
(un quart de seconde pour la plupart des équipements, **une seconde et demie** pour le KNX au
démarrage), mais un rechargement de configuration en pleine activité tombe exactement dedans.

**Types d'équipements concernés** : scénarios, boutons *appui long* et *triple appui*,
**tous les équipements KNX** (entrées, sorties, variateurs, RVB, volets) et les lecteurs **Roon**.
S'y ajoute la **surveillance des processus auxiliaires** (KNX, Wago, OLA, 1-Wire, Roon), dont
l'avis d'arrêt différé pouvait de la même façon retomber sur un objet déjà détruit.
Les volets avaient déjà été traités séparément.

⚠️ **Les caméras IP sont dans le même cas et sont corrigées de la même façon, mais elles sont la
seule famille de cette liste dont le défaut n'a PAS pu être reproduit par un test** : le déclencher
demande un téléchargement réellement en cours, et la destruction de la caméra emporte ce
téléchargement en vol. Leur présence ici est **déduite du code**, pas mesurée — contrairement à tout
le reste de ce paragraphe.

### Ce qui change
Chaque action différée vérifie désormais que son équipement **existe encore** avant de s'exécuter,
et ne fait rien s'il a disparu. **Aucun changement de configuration n'est nécessaire, et aucun
comportement normal ne change** : tant que l'équipement est là, tout se passe exactement comme
avant.

⚠️ **Ce qui n'est pas promis.** Ce défaut est de ceux qui, la plupart du temps, **ne se voient
pas** : il est impossible d'affirmer qu'un plantage que vous avez connu venait de là. Ce qui est
mesuré, c'est que l'action différée **ne s'exécute plus** une fois l'équipement supprimé — vérifié
par des tests qui détruisent l'objet puis surveillent sa mémoire, et par un lecteur Roon détruit
qui, avant la correction, **répondait encore** à une requête de l'API. Ce qui n'est pas mesuré,
c'est le comportement sur du matériel réel : ni bus KNX, ni core Roon, ni caméra n'ont été
impliqués — **ni le cas des caméras IP**, signalé ci-dessus.

---

## 🔴 Volets : l'action `impulse down` n'a jamais respecté la durée demandée

### Ce que vous avez pu observer (T3.34)
Si vous pilotez un volet avec `impulse down <durée>` — depuis une règle, un scénario, un bouton de
l'interface ou l'API — **la durée que vous demandiez était ignorée**, et ce depuis toujours. La
commande jumelle `impulse up <durée>`, elle, fonctionnait. Ce qui se passait dépendait de votre
configuration :

- **Volet configuré avec `impulse_time`** (volet à impulsions) : le volet **partait puis s'arrêtait
  presque aussitôt**, après la seule durée d'impulsion du matériel — quelques dizaines ou centaines
  de millisecondes. En pratique, un frémissement au lieu d'un mouvement. Facile à mettre sur le
  compte du matériel ou du relais.
- **Volet sans paramètre `impulse_time`** (volet à relais ordinaire, le cas le plus courant) : c'est
  l'inverse. Le volet **partait et ne s'arrêtait plus** : il descendait **jusqu'à sa fin de course**,
  au lieu des quelques secondes demandées. Si vous avez renoncé à `impulse down` parce que « ça
  ferme tout le volet à chaque fois », c'était ça.

Dans les deux cas la commande **répondait `success`** et rien n'était journalisé comme une erreur.
Le seul indice était visible dans l'état de l'IO remonté par l'API, qui affichait
`impulse down 0` quelle que soit la durée envoyée.

La cause : en retirant le préfixe `impulse down ` de la commande, le serveur enlevait **deux
caractères de trop peu**. La durée arrivait donc illisible à la conversion, qui rendait **0**.

### Ce qui change
`impulse down <durée>` fait maintenant ce qu'elle annonce : le volet descend pendant la durée
demandée, puis s'arrête — exactement comme `impulse up` le faisait déjà. **Aucun changement de
configuration n'est nécessaire.**

⚠️ **Vos automatismes vont changer de comportement, et c'est le but.** Si vous aviez contourné le
défaut — en encadrant `impulse down` d'un `stop` temporisé, en réglant `impulse_time` pour obtenir
la course voulue, ou en remplaçant la commande par `down` suivi d'un `stop` — **ces contournements
vont maintenant s'ajouter à la durée qui est enfin respectée**. Relisez les règles qui utilisent
`impulse down` avant de mettre à jour.

⭐ **Et seulement celles-là.** `impulse up` **fonctionnait déjà correctement** et **n'est pas touchée
par cette correction** : sa durée était honorée avant, elle l'est après, à l'identique. Vous n'avez
donc **pas** à relire les règles, scénarios ou scripts qui n'utilisent que `impulse up`, `up`,
`down`, `stop` ou `toggle`. **La seule commande dont le comportement change est `impulse down`** —
cherchez cette chaîne, et arrêtez-vous là.

⚠️ Le paramètre `impulse_time` de vos volets, lui aussi, **garde exactement le sens qu'il avait** :
il n'est ni à régler ni à retirer. Si vous l'aviez détourné pour obtenir une course donnée avec
`impulse down`, c'est ce détournement — pas le paramètre — qui est à défaire.

Deux cas limites changent aussi, tous deux sans effet sur une configuration saine :
`impulse down 0` (ou une durée négative) arrête le volet immédiatement au lieu de le laisser aller
en fin de course, et une durée absurdement grande laisse simplement le volet aller au bout de sa
course au lieu de dérégler la minuterie d'arrêt. Ces deux entrées faisaient en outre **fuir une
ressource interne à chaque appel** ; répétées depuis l'API, elles finissaient par peser sur le
serveur. C'est corrigé.

### Et le serveur ne plante plus si un volet est supprimé pendant une impulsion (T3.34)
Quand un volet reçoit `impulse up` ou `impulse down`, le serveur arme une minuterie interne chargée
de l'arrêter à l'échéance. Si le volet disparaissait **entre-temps** — suppression d'un équipement
depuis l'interface ou l'API, suppression d'une pièce, rechargement de configuration — cette
minuterie retombait sur un équipement qui n'existait plus : le serveur pouvait **s'arrêter
brutalement**, ou **arrêter un autre volet à la place** de celui qui avait disparu.

⚠️ **Ce défaut n'a pas été introduit par la correction ci-dessus — il existait déjà** : un
`impulse up <durée>` parfaitement correct suffisait à l'ouvrir, de longue date. Corriger la durée de
`impulse down` ne fait qu'**allonger la fenêtre** pendant laquelle il pouvait se produire, et c'est
précisément pourquoi il est refermé dans la même livraison plutôt que reporté. Les deux types de
volets (`OutputShutter` et `OutputShutterSmart`) sont couverts.

## ⚠️ Comportements qui changent sur une installation existante

### Une règle dont un équipement a disparu ne s'exécute plus (décision utilisateur)
Jusqu'ici, si un IO référencé par une règle était supprimé ou renommé, la condition qui le visait
était **rejetée au chargement** et la règle continuait de tourner **amputée**, donc plus
permissive — `si absence ET après 22h → tout éteindre` devenait `si après 22h → tout éteindre`,
se déclenchant tous les soirs.

Désormais une règle dont au moins une condition ou action référence un équipement introuvable est
**entièrement désactivée**. Elle reste **visible et intacte** dans la configuration (rien n'est
perdu à la sauvegarde), est **journalisée** avec son nom et les ids manquants.
⚠️ **Elle ne se réactive PAS toute seule quand l'équipement réapparaît** : la liste des ids
manquants est constituée **au chargement** et n'est jamais vidée (`missingIoIds` est
append-only — vérifié : aucun `clear()` dans tout `src/`). Il faut **recharger la configuration**,
c'est-à-dire redémarrer le serveur. `Rule.h:106-109` le dit correctement ; cette note affirmait
l'inverse jusqu'au 2026-08-17. Une **notification mail + push** (le canal
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

### ⚠️ Scripts Lua — `calaos:waitForIO()` et `calaos:setIOParam()` ne renvoient plus rien (T3.27)

**C'est la seule note de ce fichier qui peut faire changer de comportement un script que vous avez
écrit vous-même.** Lisez-la si vous avez des règles ou des scénarios avec du Lua.

**Ce qui se passait.** Ces deux fonctions annonçaient à l'interpréteur Lua qu'elles laissaient une
valeur de retour, mais n'en déposaient jamais aucune. Lua reprenait alors ce qui traînait à sa
place : **le dernier argument de votre propre appel.** `calaos:waitForIO("io_0042")` vous rendait
la chaîne `"io_0042"`, et `calaos:setIOParam(id, cle, valeur)` vous rendait `valeur`.

En Lua, **toute chaîne non vide est vraie**. Donc ceci :

```lua
if calaos:waitForIO("io_0042") then
  -- « l'attente a réussi »
end
```

prenait la branche vraie **à tous les coups** — y compris là où vous pensiez vous protéger. Ce
n'était pas un test qui se trompait de temps en temps : c'était un test qui n'en était pas un.

**Ce qui se passe maintenant.** Les deux fonctions ne renvoient **rien** (`nil`), comme
`calaos:setIOValue()`, `calaos:requestUrl()` et `calaos:sendPushNotif()` à côté d'elles. C'est la
règle de toute l'API : les fonctions qui **lisent** (`getIOValue`, `getIOParam`, `getEnv`) rendent
une valeur, celles qui **agissent** n'en rendent pas.

⚠️ **Conséquence directe, et elle est silencieuse** : le `if` ci-dessus, qui était **toujours
vrai**, devient **toujours faux**. Rien ne vous préviendra — pas d'erreur, pas de ligne de
journal. Le script tournera simplement dans l'autre branche.

**Pourquoi pas un booléen de succès plutôt que `nil` ?** Parce qu'il n'y aurait rien à mettre
dedans. Ces deux fonctions **n'ont aucun moyen de savoir si elles ont réussi** : `setIOParam()`
envoie sa demande au serveur sans attendre de réponse, et `waitForIO()` ne rend la main que quand
l'IO a effectivement changé. Un booléen n'aurait pu valoir que `true`, toujours — c'est-à-dire
exactement le défaut d'avant, avec un nom plus rassurant. Mieux vaut ne rien rendre que rendre une
promesse vide.

**Comment un échec se signale alors ?** Par une **erreur Lua**, et ça n'a pas changé : un `id` d'IO
inconnu, une valeur d'un type refusé ou un script interrompu **font échouer le script**. Vous
n'avez donc jamais eu besoin de tester le retour pour être protégé — le script s'arrêtait de
toute façon. Si vous voulez au contraire *survivre* à l'erreur, c'est `pcall()` qu'il faut :

```lua
local ok, err = pcall(function() calaos:waitForIO("io_0042") end)
if not ok then
  print("waitForIO a echoue : " .. tostring(err))
end
```

### Ce que vous avez à faire
1. Ouvrez vos scripts Lua (règles à condition/action « script », dans `calaos_installer` ou via
   l'API JSON) et **cherchez `waitForIO` et `setIOParam`**.
2. Pour chacun, regardez si la valeur de retour est **utilisée** : dans un `if`, dans un `while`,
   affectée à une variable (`local ok = calaos:waitForIO(...)`), passée à `assert()`, ou combinée
   avec `and` / `or` / `not`.
3. **Si le retour n'est pas utilisé** — c'est le cas courant, `calaos:waitForIO("io_0042")` seul
   sur sa ligne — **il n'y a rien à faire.** Le comportement est identique.
4. **Si le retour est utilisé**, l'écriture ne voulait déjà rien dire : supprimez le test et
   laissez l'appel nu. `if calaos:waitForIO(io) then A end` devient `calaos:waitForIO(io)` suivi
   de `A`. Si vous vouliez vraiment rattraper une erreur, passez par `pcall()` comme ci-dessus.
5. Même chose pour `setIOParam` : `if calaos:setIOParam(id, k, v) then …` n'a jamais testé quoi
   que ce soit ; enlevez le `if`.

Le détail complet, avec les exemples, est dans `docs/09_lua_scripting.md`.

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

### Journal d'événements — un accent de travers ne coupe plus le serveur (E4.1b)

Le journal d'événements enregistre l'état de chaque équipement au moment où il change. Si cet état
contenait du texte **mal encodé** — un accent envoyé dans un vieux format par un équipement, un
nom recopié depuis un fichier d'une autre époque, ou simplement une valeur transmise telle quelle
par un capteur bavard — alors la **consultation du journal**, depuis l'application ou depuis
l'interface web, **arrêtait `calaos_server` net**. Pas un message d'erreur, pas une réponse
incomplète : le serveur s'arrêtait, et il fallait attendre son redémarrage. La même consultation
refaite juste après le redémarrage l'arrêtait de nouveau, aussi longtemps que l'événement fautif
restait dans le journal.

**Êtes-vous concerné ?** Il faut réunir deux conditions, et il est important de dire les deux :

- **N'importe quel compte ayant accès à l'API suffit** — aucun privilège particulier n'est requis,
  et la commande qui déclenche l'enregistrement est une commande de pilotage tout à fait
  ordinaire ;
- **mais votre installation doit posséder au moins un équipement de type TEXTE dont
  l'historique est activé.** C'est la condition qui limite la portée : un éclairage, un volet, un
  variateur ou un scénario **refuse** une valeur qui n'est pas la sienne, donc rien de mal encodé
  ne peut être enregistré à son sujet.

Vérifié sur deux installations réelles : l'historique y est activé sur **quasiment tous** les
équipements (78 et 48 respectivement), mais **aucun** n'est de type texte — **ces deux
installations n'étaient donc pas exposées**. Une installation qui utilise des équipements de type
texte avec historique, elle, l'était.

Le même défaut guettait ailleurs, pour les mêmes raisons : les notifications push, la réponse
d'appairage d'un écran déporté, la mise à jour de son micrologiciel, et la trace enregistrée
lorsqu'une règle envoie une notification.

Désormais, un caractère que Calaos ne sait pas relire est **remplacé par le point d'interrogation
en losange (�)** que tous les navigateurs et téléphones affichent dans ce cas, et **la réponse
part normalement**. Le reste du message est intact, la connexion reste ouverte, et le serveur
continue de tourner.

→ **Rien à faire de votre côté.** Si vous aviez un journal d'événements qui « faisait tomber »
Calaos à chaque consultation, il redevient consultable après la mise à jour.

**Détail pour les intégrateurs** : les réponses de l'API JSON restent, comme avant, en **ASCII
pur** — les caractères accentués continuent d'être transmis sous leur forme échappée (`\u00e9`
pour `é`). Une poignée de réponses les transmettait jusqu'ici en UTF-8 brut selon la commande
appelée ; elles rejoignent la forme commune. Toute bibliothèque JSON lit les deux formes de façon
identique ; seuls les outils qui cherchent une sous-chaîne dans le texte brut de la réponse — ce
qu'aucun client Calaos ne fait — pourraient s'en apercevoir.

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
  configuration qui les référence encore **démarre normalement** : l'IO inconnu est ignoré avec un
  avertissement dans les logs (`<type>: Unknown Input type !`), le reste de l'installation
  fonctionne. Vérifié au code (`IOFactory.cpp:44-53`, `Room.cpp:176-181`) et épinglé par
  `tests/core/CoreSmoke_test.cpp:86-99`.

  ⚠️ **Mais la configuration ne survit pas au démarrage, et cette note l'a laissé croire jusqu'au
  2026-08-24.** L'IO inconnu n'est jamais ajouté à sa pièce ; or `Config::SaveConfigIO()` est
  l'**unique** écrivain d'`io.xml` et reconstruit un document **neuf** à partir des seules pièces
  de `ListeRoom` (`CalaosConfig.cpp:313-330`) — aucun mécanisme de préservation n'existe
  (`grep -r 'rawXml\|preserveUnknown' src` → 0 résultat). **La ligne XML est donc effacée du
  fichier à la première réécriture**, et celle-ci n'attend aucune action de l'utilisateur :
  `main.cpp:196` planifie `ListeRoom::checkAutoScenario()` **0,1 s après le démarrage**, qui se
  termine par `SaveConfigIO()` (`ListeRoom.cpp:339-341`). **Neuf autres** sites la déclenchent :
  huit dans l'API JSON (ajout/suppression d'IO, opérations de pièce) et un dans le provisioning
  RemoteUI.

  Autrement dit : **redémarrer une seule fois suffit à perdre définitivement les entrées
  MySensors et Gadspot du fichier de configuration.** Aucune migration n'est prévue — il faut
  relever ces équipements **avant** la mise à jour et les remplacer. Les règles et scénarios qui
  les référençaient tombent, eux, sous la règle générale de la dépendance manquante (règle
  désactivée E4.2e, scénario désactivé T3.18).

## Sécurité & réseau
- **TinyXML 2.5.3 (non maintenu, 2 CVE) remplacé par pugixml** — 14 242 lignes de bibliothèque
  tierce retirées du dépôt. Les deux vulnérabilités (plantage du serveur sur XML malformé,
  boucle infinie sur UTF-8 tronqué), atteignables depuis une URL configurée par l'utilisateur via
  les IOs Web, ne sont plus atteignables : le code vulnérable n'existe plus.
- **Limite de connexions par client** : 50 par défaut (`max_connections_per_ip`), auparavant
  illimité. Le client est identifié via `X-Forwarded-For` (haproxy). Au-delà : `429`.
- **La protection anti-force-brute du login protège enfin *par client*.** Elle existait déjà —
  après un mot de passe erroné, l'adresse fautive est ralentie (1 s, puis 2 s, 4 s… jusqu'à 60 s,
  effacé dès qu'un login réussit) — mais elle ne distinguait pas les clients : Calaos étant
  installé derrière un reverse-proxy (haproxy), **toutes les connexions lui semblaient venir de la
  même adresse, celle du proxy**. Conséquence sur une installation réelle : quelqu'un qui se
  trompait de mot de passe, ou qui essayait d'en forcer un, **ralentissait la connexion de tous les
  autres utilisateurs** — application mobile, écrans muraux, intégrations — alors qu'à l'inverse sa
  propre limite était remise à zéro par le premier login réussi de n'importe qui d'autre.
  Désormais chaque client a son propre compteur, sur **l'interface web/API comme sur le websocket**.
  ⚠️ **La réserve qui figurait ici — « un appareil du LAN peut annoncer l'identité de son choix,
  réglez `listen_address` sur `127.0.0.1` » — est levée, et le remède qu'elle proposait était
  mauvais** : voir l'entrée suivante.
- **Un appareil de votre réseau local ne peut plus se faire passer pour quelqu'un d'autre.**

  ⚠️ **DEUX RÉSERVES, à lire AVANT le reste de cette entrée** — elles sont détaillées plus bas :
  **(1)** cette protection **ne couvre PAS l'assistant MCP** (`/mcp`, servi sur le même port) :
  **ne le considérez pas comme protégé par cette version** ;
  **(2)** si vous avez réglé `listen_address` sur **`::`** (ce n'est pas la valeur par défaut),
  **tous vos utilisateurs partagent un compteur unique** — repassez à `0.0.0.0`.

  **Ce qui changeait le comportement.** Les deux protections ci-dessus — le ralentissement après
  mot de passe erroné et la limite de connexions — identifient un client par l'en-tête
  `X-Forwarded-For`, celui que le reverse-proxy (haproxy) ajoute pour dire de quelle adresse vient
  vraiment la demande. Le serveur croyait cet en-tête **quelle que soit la provenance de la
  connexion**. Or votre serveur Calaos répond aussi **directement** sur le port 5454 depuis votre
  réseau local — c'est nécessaire, c'est par là que passent les écrans RemoteUI et l'application
  mobile en Wi-Fi. Un appareil du LAN pouvait donc **écrire cet en-tête lui-même** et : (1)
  changer d'identité à chaque essai de mot de passe, ce qui **annulait complètement** le
  ralentissement anti-force-brute et permettait d'essayer des mots de passe sans aucune limite ;
  (2) porter l'adresse d'un autre appareil pour **le faire ralentir ou bloquer à sa place**.

  **Ce qui change.** L'en-tête n'est désormais cru **que si la connexion vient de la machine
  elle-même** (`127.0.0.1` / `::1`) — c'est-à-dire quand elle vient du reverse-proxy, qui tourne
  sur le même boîtier Calaos. Pour toute autre connexion, c'est l'adresse réelle de l'appareil qui
  compte, et l'en-tête est ignoré.

  **Qui doit vérifier sa configuration.** ⚠️ **Uniquement** ceux qui ont **déplacé haproxy (ou un
  autre reverse-proxy) sur une machine différente** de celle qui fait tourner `calaos_server` —
  un montage qu'aucune installation Calaos standard ne produit, et qui suppose d'avoir édité
  `/mnt/calaos/haproxy/haproxy.cfg` à la main ou reconfiguré le backend de `calaos_ddns`. Dans ce
  cas seulement, le serveur ne reconnaît plus votre proxy : **tous vos utilisateurs retombent dans
  un compteur unique**, celui du proxy, et se ralentissent mutuellement comme avant. Signalez-le,
  la configuration de proxys de confiance supplémentaires est prévue mais volontairement pas
  livrée sans demande.

  **Si vous ne faites rien.** Sur une installation Calaos normale — haproxy et `calaos_server` sur
  le même boîtier — **il n'y a rien à faire, rien ne change** pour vous : le proxy est reconnu, vos
  utilisateurs gardent chacun leur compteur, et la protection anti-force-brute cesse simplement
  d'être contournable depuis votre réseau local. ⛔ **Et surtout : ne réglez pas `listen_address`
  sur `127.0.0.1`.** L'ancienne version de cette note le conseillait ; c'était une erreur. Ce
  réglage gouverne **aussi** le service de découverte UDP : il rendrait votre serveur invisible
  pour `calaos_installer`, pour l'application mobile et pour tous vos écrans RemoteUI, et couperait
  les entrées Wago.

  **Ce qui reste vrai.** Quelqu'un capable d'exécuter du code **sur le boîtier Calaos lui-même**
  est vu comme le proxy et peut encore choisir son identité. C'est accepté : à ce stade il a déjà
  bien mieux à sa disposition.

  ⚠️⚠️ **CETTE PROTECTION EST INCOMPLÈTE — l'assistant MCP n'en bénéficie PAS encore.** Si vous
  avez activé le serveur MCP (l'interface qui permet à un assistant IA de piloter votre
  installation), sachez qu'il est servi **sur le même port 5454**, sous `/mcp`, et qu'il possède
  **son propre** compteur de tentatives et sa propre liste de blocage. Ce compteur-là lit toujours
  l'en-tête `X-Forwarded-For` **sans vérifier d'où vient la connexion**. Autrement dit, les deux
  abus décrits plus haut — essayer des jetons sans limite, ou faire bloquer quelqu'un d'autre —
  **restent possibles sur `/mcp` depuis votre réseau local**. Mesuré : avec une limite réglée à 5
  requêtes, 20 requêtes portant un en-tête forgé **changé à chaque fois** passent **toutes**, là où
  20 requêtes honnêtes sont bloquées après 4.
  ⇒ **Ne considérez pas `/mcp` comme protégé par cette version.** Tant que ce n'est pas corrigé,
  n'exposez pas le port 5454 hors de votre réseau local sans passer par le reverse-proxy, et
  traitez le jeton MCP comme un secret dont la seule défense est sa longueur. Le correctif demande
  une modification plus profonde du relais et fait l'objet d'une fiche séparée.

  ⚠️ **Un cas particulier de configuration.** Si vous avez réglé `listen_address` sur `::` (l'écoute
  IPv6 « toutes interfaces » — ce n'est pas la valeur par défaut et rien dans Calaos ne la pose),
  le serveur ne sait pas lire l'adresse de ses clients dans ce mode, et **tous vos utilisateurs
  retombent dans un compteur unique**, comme dans le cas du proxy déporté ci-dessus. Aucune
  identité n'est usurpable pour autant — le serveur refuse de faire confiance plutôt que de se
  tromper. Repassez à `0.0.0.0` (la valeur par défaut) en attendant le correctif.
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
- **MQTT** : un payload contenant un **octet nul** n'est plus perdu. Le process `calaos_mqtt`
  l'émettait pourtant correctement, mais le serveur **refusait le message entier** à la lecture —
  topic compris — et le message disparaissait avec un simple « Error parsing json ». Il est
  désormais délivré dans sa longueur d'origine. (E4.1g)
- **MQTT RGB** : l'état suit désormais le retour du broker (le retour était auparavant ignoré).
  Nécessite que le broker publie sur `topic_sub`.
- **KNX — une valeur ordinaire du bus aurait pu arrêter le driver, et ne le peut pas** : **rien ne
  change sur une installation existante, et il n'y a rien à faire.** Le driver KNX a changé de
  bibliothèque JSON en interne ; la mention est ici parce que ce changement, laissé à ses réglages
  par défaut, aurait suffi à arrêter le driver sur du matériel domestique ordinaire. Certaines
  valeurs venues du bus contiennent des octets qui ne forment pas du texte valide : toute valeur
  8 bits au-dessus de 127 — l'échelle des gradateurs va de 0 à 255, un gradateur réglé à 78 % vaut
  donc 200 — ou un texte envoyé en latin-1 par un équipement. L'ancienne bibliothèque jetait ces
  octets en silence ; la nouvelle **refuse d'écrire le message**, et ni le serveur ni le process
  KNX ne rattrapaient ce refus. Ils sont désormais remplacés par le caractère de remplacement
  Unicode (`�`) et le message part normalement.
  ⚠️ **L'octet d'origine n'est préservé ni avant ni après.** Mais le champ concerné n'est lu par
  aucun équipement piloté par Calaos : aucune valeur affichée ou commandée n'en dépend, ni hier ni
  aujourd'hui. C'est donc une **régression évitée**, pas la correction d'un défaut que vous auriez
  pu observer. (E4.1e)
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
  ⚠️ **Si vous voyez encore ce diff après la mise à jour** : il vient du serveur **qui a produit le
  fichier**, pas de l'outil qui l'a enregistré. Ouvrir une configuration depuis un `calaos_server`
  **plus ancien** (Calaos Installer, « ouvrir depuis le serveur ») rapporte l'ordre brassé par ce
  serveur-là, et le fichier réenregistré le conserve fidèlement — **rien n'est perdu**, seul
  l'ordre de quelques lignes change. Mettre à jour le serveur suffit.
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

## Détail pour les intégrateurs — les événements, `get_home` / `get_io`, l'état des équipements, les paramètres/plages horaires, les scénarios automatiques ET les dernières réponses de l'API changent de forme, et cessent de perdre des données en silence (E4.1l, E4.1m, E4.1n, E4.1o, E4.1p, E4.1q, E4.1r, E4.1s)

> **Rien à faire de votre côté, et aucune application Calaos ne s'en aperçoit.** Cette note existe
> parce que le changement porte sur des **octets réellement servis** sur l'API JSON (port 5454),
> et qu'un intégrateur qui a écrit son propre client a le droit de le lire avant de le découvrir.

⚠️ **Note consolidée, pas empilée.** E4.1l l'a ouverte pour les événements ; E4.1m y ajoute
`get_home` et `get_io` **sans dupliquer les cinq différences**, parce que ce sont **exactement les
mêmes cinq**, remesurées sur la chaîne d'émission de ce ticket-là (512 sondes d'un octet, en valeur
et en nom de champ, plus les formes bien et mal encodées). **E4.1n** y ajoute l'**état des
équipements**, remesuré une troisième fois sur sa propre chaîne : **les mêmes cinq**, aucune
sixième. **E4.1o** y ajoute les **paramètres d'équipement** et les **plages horaires** : **les mêmes
cinq**, toujours aucune sixième — mais c'est **le seul ticket de la série où le NOM du champ est
fourni par le client lui-même**, ce qui lui vaut un encadré à part, plus bas. **E4.1p** y ajoute le
**lecteur audio** — liste de lecture, temps de lecture, taille de liste, pochette : **les mêmes
cinq**, aucune sixième, mais avec une **source d'octets nouvelle** et elle aussi encadrée plus bas,
puisque ce texte ne vient ni du serveur ni du client mais de **votre bibliothèque musicale**.
**E4.1r** y ajoute les **scénarios automatiques**, et c'est le premier ticket de la série où le
compte n'est **pas** de cinq : voir l'encadré qui lui est consacré plus bas — il n'en a que
**trois**, et l'explication est structurelle. Les tickets suivants de la série feront de même. La
liste des **réponses** concernées à ce stade est donc :

| Réponse | Depuis |
|---|---|
| `{"msg":"event", …}` en WebSocket, et les événements de `poll_listen` en HTTP | E4.1l |
| Le **journal d'événements** enregistré par le serveur | E4.1l |
| Les **écrans déportés (RemoteUI)**, qui reçoivent les mêmes événements | E4.1l |
| ⭐ **`get_home`** — la description complète de l'installation : pièces, équipements, caméras, lecteurs audio | **E4.1m** |
| ⭐ **`get_io`** — la description d'une liste d'équipements demandés par leur identifiant | **E4.1m** |
| ⭐ Le message envoyé aux **scripts Lua** (`calaos_script`) | **E4.1m** |
| ⭐ **`get_state`** — l'état d'une liste d'équipements, **la réponse la plus demandée de l'API** | **E4.1n** |
| ⭐ **`get_states`** — tous les états d'un équipement | **E4.1n** |
| ⭐ **`query`** — l'interrogation d'un paramètre d'équipement | **E4.1n** |
| ⭐ **`set_state`** — l'accusé de réception (`{"success":"true"}`) | **E4.1n** |
| ⭐ Les **écrans déportés (RemoteUI)** à la connexion, pour leurs **états initiaux** — voir l'encadré qui leur est consacré plus bas | **E4.1n** |
| ⭐ **`get_param`** — la lecture d'un paramètre d'équipement — voir l'encadré qui lui est consacré plus bas | **E4.1o** |
| ⭐ **`set_param`** et **`del_param`** — leurs accusés de réception | **E4.1o** |
| ⭐ **`get_timerange`** — les plages horaires d'un équipement horaire | **E4.1o** |
| ⭐ **`set_timerange`** — son accusé de réception | **E4.1o** |
| ⭐ **`get_playlist`** — la liste de lecture d'un lecteur audio, piste par piste — voir l'encadré qui lui est consacré plus bas | **E4.1p** |
| ⭐ **`audio` → `get_playlist_size`, `get_time`, `get_playlist_item`, `get_cover_url`** — l'état d'un lecteur audio | **E4.1p** |
| ⭐ **`audio_db`** — la médiathèque : albums, artistes, genres, années, listes de lecture, radios, dossiers musicaux, informations de piste, statistiques | **E4.1q** |
| ⭐ **`autoscenario` → `list`, `get`** — la description d'un **scénario automatique** : ses étapes, leurs pauses et leurs actions — voir l'encadré qui lui est consacré plus bas | **E4.1r** |
| ⭐ **`autoscenario` → `create`, `delete`, `modify`, `add_schedule`, `del_schedule`, `reenable`** — leurs accusés de réception et leurs refus | **E4.1r** |
| ⭐ **`config` → `get`** — le **contenu brut** de `io.xml`, `rules.xml` et `local_config.xml`. **La plus grosse réponse de l'API**, et la seule qui reflète des **fichiers utilisateur** : voir l'encadré qui lui est consacré plus bas | **E4.1s** |
| ⭐ **`config` → `put`** — son accusé de réception | **E4.1s** |
| ⭐ **`get_mcp_info`** — l'adresse et le jeton du sidecar MCP | **E4.1s** |
| ⭐ **`get_cover`, `get_camera_pic`, `audio` → `get_cover`** — leurs **refus** (`{"error_str":…,"success":"false"}`) et l'accusé de réception qui porte l'image en base64 | **E4.1s** |
| ⭐ Le **refus de connexion** en WebSocket (`{"success":"false"}`) | **E4.1s** |

Ces réponses sont désormais fabriquées par la même bibliothèque JSON que le reste des réponses
récentes. **Cinq** différences observables, **mesurées octet à octet** ; trois sont purement de
forme, et **deux rendent des données qui étaient perdues en silence** :

- **L'ordre des membres change.** Il est maintenant **alphabétique** : `data` avant `event_raw`
  avant `type` avant `type_str`, et `data` avant `msg` dans l'enveloppe. Auparavant c'était
  l'ordre dans lequel le serveur les écrivait. **Aucune valeur, aucune clé, aucun tableau ne
  change** : les tableaux gardent leur ordre, qui lui est porteur de sens.
  ⭐ **Sur `get_home` et `get_io` (E4.1m), c'est la différence la plus visible**, parce que ces
  réponses sont grandes : les trois sections passent de `home`, `cameras`, `audio` à `audio`,
  `cameras`, `home` ; chaque pièce passe de `type`, `name`, `hits`, `items` à `hits`, `items`,
  `name`, `type` ; et chaque équipement ne commence plus par son `id`. **L'ordre des pièces et
  l'ordre des équipements dans une pièce, eux, ne bougent pas** — ce sont des tableaux, et cet
  ordre-là a un sens. ⚠️ Pour `get_io`, l'objet de réponse est **indexé par identifiant
  d'équipement** : ces identifiants sortent maintenant **triés**, et non plus dans l'ordre où vous
  les avez demandés.
- **La casse de l'échappement change.** Un caractère accentué continue de partir échappé, la
  réponse reste en **ASCII pur** comme avant ; la casse de l'hexadécimal passe de
  `\u00E9` à `\u00e9` (pour `é`). Les deux se lisent de façon identique par n'importe
  quelle bibliothèque JSON.
- **Un texte mal encodé ne fait plus disparaître son champ.** Si l'état d'un équipement contenait
  des octets qui ne forment pas du texte valide, le champ correspondant **était retiré de
  l'événement, en silence** : l'événement partait amputé. Il est désormais **présent**, avec les
  octets fautifs remplacés par le caractère de remplacement Unicode (`�`). Le journal
  d'événements enregistre la même chose. ⚠️ **Cela vaut aussi quand ce sont les octets d'un NOM
  de champ qui sont mal encodés** : la paire entière disparaissait, elle est maintenant servie
  sous un nom contenant des `�` (`k��z`).
  ⭐ **Sur `get_home` et `get_io` (E4.1m), ce cas a un nom concret : un équipement pouvait
  perdre son NOM.** Si le nom d'un équipement contenait des octets mal encodés — c'est possible
  par `set_param`, ou par une configuration importée depuis un outil tiers —, le champ `name`
  était **retiré de la réponse** et l'équipement arrivait chez le client **indistinguable d'un
  équipement qui n'a jamais eu de nom** : une application qui indexe par nom perdait simplement
  l'appareil, sans aucun message. Le nom est désormais présent, avec les octets fautifs remplacés
  par `�`. La même chose valait pour `id`, `state`, `unit` et les douze autres champs.
- ⭐ **Le caractère DEL (U+007F) part désormais échappé, et la longueur du message change.** C'est
  le seul caractère qui était servi **en octet brut** et qui ne l'est plus : `{"c":"a<DEL>b"}`
  (**11 octets**) devient `{"c":"a\u007fb"}` (**16 octets**). Un client qui compte les octets
  d'une valeur, ou qui s'appuie sur le `Content-Length` de la réponse HTTP, voit la différence —
  la valeur **décodée**, elle, est identique.
- ⭐ **Un octet nul (`%00`) ne tronque plus la valeur ni le nom du champ.** L'ancienne chaîne C
  s'arrêtait au premier zéro : `a\0b` partait en `"a"`, et un nom `k\0z` partait sous le nom
  `"k"` — c'est-à-dire **sous un autre nom que celui reçu**, en silence. La valeur est maintenant
  servie entière, le zéro étant écrit `\u0000`.

### ⭐ Les scénarios automatiques : **trois** différences et non cinq, et pourquoi (E4.1r)

`autoscenario list` et `autoscenario get` décrivent un scénario automatique : ses étapes, la pause
de chaque étape, et les actions de chaque étape. C'est la réponse la plus **imbriquée** de l'API, et
c'est là que le changement d'ordre des membres se voit le plus :

- l'objet du scénario passe de `id`, `cycle`, `enabled`, `schedule`, `category`, `broken`,
  `disabled_missing_io`, `missing_ios`, `steps_count`, `steps` à l'ordre **alphabétique** —
  `broken` d'abord, `steps_count` en dernier ;
- chaque **étape** passe de `step_pause`, `step_type`, `actions` à `actions`, `step_pause`,
  `step_type` ;
- chaque **action** passe de `id`, `action` à `action`, `id`.

⚠️ **L'ORDRE DES ÉTAPES, LUI, NE BOUGE PAS**, et l'ordre des actions à l'intérieur d'une étape non
plus : ce sont des **tableaux**, et cet ordre-là est le scénario lui-même. Il est vérifié
explicitement, sur les octets, par la suite de tests livrée avec ce changement.

Les deux autres différences de forme sont les mêmes que partout ailleurs : la **casse** de
l'échappement d'un accent, et le caractère **DEL** (U+007F) désormais échappé. Sur ce périmètre
elles ne peuvent venir que d'un endroit : le texte d'une **action** de scénario, qui est fourni par
le client et renvoyé tel quel.

**En revanche, les deux différences qui RÉCUPÈRENT des données perdues n'ont pas encore lieu ici.**
Un nom mal encodé ou contenant un octet nul dans un scénario est toujours perdu de la même façon
qu'avant, parce que la fabrication du contenu d'un scénario n'a pas encore été convertie — elle
l'est plus tard, par le chantier qui réécrit les scénarios (E4.6). **Rien ne régresse ; c'est le
gain qui n'est pas encore là.** Ce sera dit ici quand ce sera fait.

Enfin, les accusés de réception (`{"success":"true"}`), les identifiants renvoyés par `create` et
`add_schedule` (`{"id":"…"}`) et les refus (`{"error":"…"}`) **ne changent pas d'un octet** : ils
n'ont qu'un seul membre.

### Ce qui a été balayé, et ce qui ne l'a pas été

La comparaison a été faite **octet à octet, sur les deux chaînes d'émission réelles compilées**
(l'ancienne : `jansson_from_params()` + `json_dumps(JSON_COMPACT|JSON_ENSURE_ASCII)` ; la
nouvelle : `Params::toNJson()` + `dump(-1, ' ', true, error_handler_t::replace)`), sur **120
sondes** — **75 diffèrent, 45 sont identiques**.

⭐ **E4.1m a refait le balayage sur SA propre chaîne** (l'ancienne : `json_string(valeur.c_str())`
+ `json_object_set_new(objet, clé.c_str(), …)` + `json_dumps(JSON_COMPACT|JSON_ENSURE_ASCII)` ; la
nouvelle : `objet[clé] = valeur` + `dump(-1, ' ', true, error_handler_t::replace)`), sur **566
sondes** — **315 diffèrent, 251 sont identiques**. Ce sont **les cinq mêmes différences, pas une
sixième**, et le balayage a couvert **les 256 valeurs d'octet en VALEUR et les 256 en NOM de
champ** :

| Balayé par E4.1m | Résultat |
|---|---|
| Les **256 octets** en valeur, et les **256** en nom de champ | **130 diffèrent de chaque côté** : `0x00` (troncature), `0x7F` (octet brut → échappé), et les **128 octets `0x80`–`0xFF`** (champ qui disparaissait) ; **9 de plus** ne diffèrent que par la casse |
| Chaînes **longues** (100 000 caractères ASCII, 20 000 accents, nom de 10 000 caractères) | **identiques**, à la casse près — *catégorie que la note précédente laissait ouverte* |
| **Doublons de clés** (la même clé écrite deux fois) | ⭐ **identiques** — les deux bibliothèques gardent la dernière valeur à la position de la première ; *catégorie que la note précédente laissait ouverte* |
| **Profondeur d'imbrication** (4, 64 et 1024 niveaux) | ⭐ **identiques** — *catégorie que la note précédente laissait ouverte* |
| Tri sur des **clés non ASCII** | ⭐ **l'ordre est identique**, seule la casse de l'échappement change — *catégorie que la note précédente laissait ouverte* |
| `/`, `\`, `"`, `\t`, `\n`, `\r`, `\b`, `\f` | **identiques** |
| Séquences bien formées `U+0080`…`U+10FFFF`, non-caractères, surlongues, tronquées, substituts, 5 octets | conformes à la note d'E4.1l : casse pour les valides, champ conservé en `�` pour les invalides |

⇒ **Quatre des cinq catégories qu'E4.1l laissait explicitement « non balayées » le sont
maintenant, et aucune ne révèle une sixième différence.**

| Balayé | Résultat |
|---|---|
| `0x01`–`0x1F` (contrôles C0), en valeur | **9 diffèrent**, *toutes* de casse : `0B 0E 0F 1A 1B 1C 1D 1E 1F`. `\b \t \n \f \r` gardent leur forme courte des deux côtés |
| `0x00`, en valeur **et** en nom | **diffère** — troncature, voir ci-dessus |
| `0x7F` (DEL), en valeur **et** en nom | **diffère** — octet brut → `\u007f`, voir ci-dessus |
| `0x80`–`0x9F` en **octets bruts** (UTF-8 invalide isolé) | **32/32 diffèrent** — c'est le cas « champ qui disparaissait », déjà décrit |
| `U+0080`–`U+009F` **bien formés** (`C2 80`…`C2 9F`) | **12 diffèrent**, toutes de casse |
| Substituts écrits en UTF-8 (`U+D800`, `U+DC00`, `U+DFFF`), valeur et nom | **diffèrent** — UTF-8 invalide, cas du champ qui disparaissait |
| Non-caractères `U+FFFE`, `U+FFFF`, `U+FDD0`, `U+1FFFE` | **diffèrent**, toutes de casse — aucun n'est filtré, ni avant ni après |
| `U+2028` / `U+2029` | ⭐ **identiques** — échappés `\u2028`/`\u2029` des deux côtés |
| Formes invalides : surlongue `C0 AF`, tronquée `E2 80`, 5 octets `F8 88 80 80 80` | **diffèrent** — cas du champ qui disparaissait |
| `"` et `\` | **identiques** |
| `U+00E9`, `U+FFFD`, `U+1F600` (paire de substituts) | **diffèrent**, toutes de casse |

⚠️ **Ce qui n'a PAS été balayé, et reste à faire par les tickets suivants de la série** : les
chaînes **longues** (aucun effet de bord de tampon n'a été cherché), les **nombres** et les
**booléens** (hors périmètre : tout part en chaîne ici), la **profondeur** d'imbrication, les
**doublons de clés**, l'ordre de tri sur des clés **non ASCII** (il devient l'ordre des unités de
code, pas un ordre linguistique), et surtout : la mesure porte sur `Params` → JSON, **pas** sur les
autres constructeurs de l'API qui basculeront dans `E4.1m` … `E4.1s`. Rien n'a été rejoué contre un
**vrai client tiers**, ni sous ASan.

### ⭐ Un script Lua qui contenait un caractère mal encodé ne partait pas du tout — il part désormais (E4.1m)

Un **script Lua** dont le TEXTE contenait un octet mal encodé n'était pas seulement mal transmis :
la paire entière était supprimée du message envoyé au processus de script, qui recevait donc un
ordre d'exécution **sans script**. Résultat : **le script ne s'exécutait pas, en silence.**

Le texte du script arrive maintenant entier, les octets fautifs remplacés par `�`, et **le script
s'exécute**. ⚠️ **C'est un changement de comportement, pas seulement de forme** : un script qui ne
faisait rien peut se mettre à faire quelque chose. Si le caractère fautif se trouvait dans une
chaîne de caractères du script, il vaut la peine de rouvrir ce script et de le réenregistrer
proprement depuis Calaos Installer. ⚠️ **Non vérifié de bout en bout avec un vrai `calaos_script` :
la mesure a été faite au niveau du message construit, pas sur un script réellement exécuté.**

### ⭐ Les écrans déportés (RemoteUI) : ce qui change pour eux, et c'est deux choses seulement (E4.1n)

Les écrans déportés sont le **seul destinataire matériel** de cette série : ils embarquent leur
propre client, dans un dépôt voisin, et **ils ne sont pas mis à jour en même temps que le serveur**.
Le message de leurs **états initiaux** (envoyé une fois, juste après la connexion de l'écran) est
donc regardé de plus près que les autres.

**Bonne nouvelle, et c'est mesuré cas par cas** : sur les cinq différences décrites plus haut,
**trois ne les concernent pas du tout**. L'ordre des membres, la casse de l'échappement et le
traitement de `DEL` étaient **déjà** ceux de la nouvelle bibliothèque, parce que le serveur
retraduisait déjà ce message d'une bibliothèque à l'autre avant de l'envoyer — un
aller-retour complet, purement mécanique, que ce ticket supprime. Les octets qu'un écran reçoit
pour un état normal sont donc **rigoureusement identiques**, avant comme après.

**Deux différences les concernent**, et seulement quand l'état d'un équipement contient des octets
que rien n'aurait dû y mettre (voir l'encadré « comment un tel octet arrive-t-il là ? ») :

- l'état d'un équipement dont la valeur contenait un **caractère mal encodé** n'était **pas envoyé
  du tout** — l'écran affichait l'équipement sans son état. Il arrive désormais, les octets fautifs
  remplacés par `�`. ⚠️ **C'est une clé que l'écran ne recevait jamais et qui commence à
  arriver** ;
- une valeur contenant un **octet nul** était **coupée à cet octet** ; elle arrive maintenant
  entière.

⚠️ **Non vérifié sur un écran réel** : la mesure porte sur les octets que le serveur émet, pas sur
ce qu'un firmware d'écran en fait. Un écran qui affiche une valeur d'état telle quelle montrera le
caractère `�` là où il ne montrait rien.

### ⛔ `get_param` : c'est la seule réponse dont le NOM du champ est fourni par le client (E4.1o)

Sur toutes les réponses décrites plus haut, le nom des champs est écrit par le serveur. **`get_param`
est l'exception** : le nom du paramètre demandé est renvoyé **tel quel, en clé**, et il vient de la
requête. En HTTP il peut être donné en paramètre d'URL, donc **percent-décodé** :
`?action=get_param&id=<équipement>&param=%ff%80x`.

- **Avant**, ces octets faisaient disparaître la paire entière : la réponse était `{}` avec un code
  **200**. Le client recevait une réponse vide, valide, et **rien ne lui disait que sa demande avait
  été amputée**.
- **Maintenant**, la clé est **conservée**, chaque octet fautif remplacé par `�`, et le code reste
  **200**. La réponse cesse d'être silencieusement vide.

⚠️ **Un client qui interrogeait un paramètre au nom mal encodé recevait `{}` et voit maintenant une
paire.** C'est le même retournement que celui décrit plus haut pour le `name` d'un équipement dans
`get_home`, appliqué à la clé cette fois. La valeur du paramètre suit la même règle : un paramètre
dont la **valeur** était mal encodée **disparaissait de la réponse**, indiscernable d'un paramètre
jamais renseigné ; il est désormais servi, mutilé et visible.

ℹ️ **Rien de tout cela n'affecte un nom de paramètre normal** : un nom bien encodé, accentué ou non,
part exactement comme avant, à la casse de l'échappement près.

### Plages horaires — l'ordre des jours ne bouge pas (E4.1o)

`get_timerange` renvoie un **tableau** `ranges` dont la position d'une entrée **est** son jour de la
semaine. **Cet ordre est inchangé** : seuls les *membres d'un objet* sont triés, jamais les
tableaux. Ce qui change, c'est que `months` est désormais écrit **avant** `ranges` dans la réponse.
Toutes les valeurs restent des **chaînes de caractères** — heures, minutes, secondes, types et
décalages compris —, elles ne deviennent pas des nombres JSON.

### ⭐ Lecteurs audio : le texte ne vient ni de vous ni du serveur, il vient de votre bibliothèque (E4.1p)

`get_playlist` renvoie un **tableau** `items`, une entrée par piste : **cet ordre est inchangé**,
c'est celui de la liste de lecture et il a un sens. Ce qui change dans l'objet qui l'entoure, c'est
que ses trois champs sortent maintenant triés — `count`, `current_track`, `items` au lieu de
`current_track`, `count`, `items`. Tout reste des **chaînes de caractères** : le numéro de piste
courante, le nombre de pistes, la durée d'une piste et le temps de lecture ne deviennent **pas** des
nombres JSON.

**La nouveauté de ce lot, c'est d'où viennent les octets.** Sur toutes les réponses décrites plus
haut, le texte vient de votre configuration ou de votre requête. Ici, les titres, les artistes, les
albums et les URL de pochette sont **ce que votre lecteur (Squeezebox, Roon) rapporte**, c'est-à-dire
en dernier ressort **ce que votre système de fichiers contient** — et un système de fichiers ne
garantit rien sur l'encodage. Un fichier importé d'un vieux disque, ripé sous un autre système ou
nommé en latin-1 peut donc porter des octets qui ne sont pas de l'UTF-8 valide.

- **Avant**, un tel titre **disparaissait de la réponse** : la piste arrivait sans son champ
  `title`, indiscernable d'une piste sans titre, avec un code **200** et rien pour signaler la
  perte. Une URL de pochette dans ce cas donnait `{}`.
- **Maintenant**, le champ est **conservé**, chaque octet fautif remplacé par `�`, et le code reste
  **200**. Une piste dont le titre s'affichait vide s'affichera mutilée — **et visible**.
- Un titre contenant un **octet nul** n'est plus **coupé** à cet octet ; il arrive entier.

ℹ️ **Rien de tout cela n'affecte une bibliothèque normale** : un titre bien encodé, accentué ou non,
part exactement comme avant, à la casse de l'échappement près. Et la réponse reste, comme toujours,
**de l'ASCII pur** : les accents continuent de partir sous la forme `\u00e9`, jamais en octets
bruts.

⚠️ **Non vérifié sur une vraie bibliothèque** : la mesure porte sur les octets que le serveur émet,
avec des pistes fabriquées pour le test. Aucun Squeezebox ni Roon réel n'a été interrogé.

### ⭐ Base musicale : le même retournement, mais sur tout ce qui s'affiche dans le navigateur de musique (E4.1q)

**C'est le lot le plus visible en volume de toute la série.** Les réponses `audio_db` — parcourir
les albums, les artistes, les années, les genres, les listes de lecture, les **dossiers de musique**,
les radios, la recherche, les informations d'une piste — sont faites **presque entièrement de texte
que vous n'avez pas écrit** : des tags de fichiers et des **noms de répertoires**. Tout ce qui est
dit plus haut sur les lecteurs audio (E4.1p) s'applique donc ici, en bien plus grand.

- **Avant**, un nom d'album, d'artiste ou de dossier mal encodé **disparaissait de la réponse** :
  l'entrée arrivait sans son champ `name`, indiscernable d'une entrée sans nom, avec un code **200**
  et rien pour signaler la perte.
- **Maintenant**, le champ est **conservé**, chaque octet fautif remplacé par `�`, et le code reste
  **200**. Un album qui apparaissait sans titre apparaîtra avec un titre mutilé — **et visible**.
- Un nom contenant un **octet nul** n'est plus **coupé** à cet octet ; il arrive entier.
- ⭐ **Le cas le plus probable est le dossier de musique.** Un système de fichiers ne garantit rien
  sur l'encodage de ses noms : un répertoire créé sous une locale latin-1 est un répertoire
  parfaitement légal dont le nom n'est pas de l'UTF-8 valide. C'est ce cas qui est mesuré et épinglé.

**Ce qui change aussi, et qui n'est pas un octet fautif** : dans la réponse d'un parcours, les deux
champs `total_count` et `items` sortent maintenant **triés** — `items` d'abord, `total_count`
ensuite, au lieu de l'inverse. **Le tableau `items`, lui, ne bouge pas** : son ordre est celui que
la base a donné et il a un sens. Tout reste des **chaînes de caractères** : le nombre total
d'entrées et la durée d'une piste ne deviennent **pas** des nombres JSON. Et une réponse sans
compteur n'a **toujours pas** de champ `total_count` — il est **absent**, jamais `null`.

ℹ️ **Rien de tout cela n'affecte une bibliothèque normale** : un nom d'album bien encodé, accentué
ou non, part exactement comme avant, à la casse de l'échappement près, et la réponse reste **de
l'ASCII pur**.

⚠️ **Non vérifié sur une vraie bibliothèque**, comme pour E4.1p : la mesure porte sur les octets que
le serveur émet, avec une base de test fabriquée. Aucun Squeezebox ni Roon réel n'a été interrogé.

### Ce qui pourrait s'en apercevoir

⚠️ **Ce qui pourrait s'en apercevoir** : un client qui **cherche une sous-chaîne dans le texte
brut** de la réponse au lieu de la parser, et — pour le seul cas de DEL — un client qui **compte
les octets** ou vérifie lui-même le `Content-Length`. Aucun client Calaos ne fait ni l'un ni
l'autre, et aucune bibliothèque JSON n'y est sensible. Les **écrans déportés (RemoteUI)**
reçoivent les mêmes événements et sont dans le même cas.

ℹ️ **Comment un tel octet arrive-t-il là ?** Par `set_state`, dont les paramètres peuvent être
donnés en GET : le décodage pourcent (`Utils::url_decode`) ajoute l'octet tel quel à la chaîne,
donc `%7f`, `%00` et n'importe quelle séquence UTF-8 invalide arrivent intacts jusqu'à l'état d'un
équipement, **sans traverser aucun parseur JSON**. ⭐ **E4.1o a mesuré que le même chemin existe
pour `get_param`, et qu'il est encore plus court** : le nom du paramètre n'a même pas besoin d'être
stocké quelque part, il suffit qu'il soit **demandé** (`?param=%ff%80x`).

*(Les autres réponses de l'API basculeront de la même façon au fil des sous-tickets suivants de la
série. ⚠️ **Cette note est LA note unique de la série : on l'étend, on ne la duplique pas** —
E4.1m l'a fait le premier, en ajoutant deux lignes au tableau des réponses concernées et un
balayage complémentaire, sans réécrire les cinq différences ; E4.1n, E4.1o, E4.1p et E4.1q ont
fait de même.
**`E4.1s` l'a relue et la ferme** : c'est le dernier sous-ticket de la chaîne API, et il y a ajouté sa propre section — la déclaration de risque de la casse hexadécimale, et surtout **le changement de surface d'entrée**, qui n'est pas une question de forme et qui n'existait dans aucun des tickets précédents.)*

### ⭐⭐ E4.1s — la bascule est terminée, et elle apporte **une** nouveauté qui n'est pas une question de forme

`E4.1s` est le dernier sous-ticket de la série sur l'API. Il fait passer les **dernières** réponses
qui échappaient encore à la nouvelle bibliothèque (tableau ci-dessus) — donc, pour elles, **les
cinq différences décrites plus haut, et aucune sixième**. Mais il change aussi une chose que les
huit tickets précédents ne touchaient pas : **la façon dont le serveur LIT votre requête**.

#### La déclaration de risque de la casse hexadécimale (elle vaut pour toute la série)

> **La casse hexadécimale des séquences d'échappement JSON change** : un caractère accentué qui
> sortait `\u00E9` sort désormais `\u00e9`. Le wire reste **ASCII pur**, comme avant. Aucun parseur JSON
> correct ne voit de différence ; seul un analyseur maison sensible à la casse serait affecté.

**Alternative écartée, et consignée pour qu'elle ne soit pas reprise par inadvertance** : émettre
les octets UTF-8 bruts, ce qui est le comportement par défaut de la nouvelle bibliothèque. Elle a
été **rejetée** parce qu'elle cesserait de garantir un flux **ASCII pur** — un changement de forme
bien plus large, sur des wires (drivers) sans filet de test. Elle reste possible plus tard, **comme
changement délibéré et déclaré, jamais comme effet de bord d'une migration**.

#### ⛔ CE QUI EST NOUVEAU, ET QUI N'EST PAS UN CHANGEMENT DE FORME : le serveur accepte trois requêtes qu'il refusait

Jusqu'à ce ticket, la requête que vous envoyez était **analysée par l'ancienne bibliothèque**, même
quand la réponse était déjà fabriquée par la nouvelle. `E4.1s` fait passer cette analyse à la
nouvelle bibliothèque, et **les deux ne tracent pas la même frontière**. Trois entrées qui étaient
**refusées** sont désormais **servies** (mesuré, sur les deux transports) :

| Entrée | Avant | Maintenant |
|---|---|---|
| Un **octet nul échappé** dans une valeur ou dans un nom de champ : `"a\u0000b"` | requête **refusée** en entier (HTTP : `400`, WebSocket : silence) | **acceptée**, la valeur est stockée **entière** et ressort échappée `\u0000` |
| Un **entier trop grand** pour tenir sur 64 bits | requête **refusée** en entier | **acceptée**, le nombre devient un réel |
| Une **imbrication de plus de 2048 niveaux** | requête **refusée** en entier | **acceptée** |

⚠️ **Ce que cela veut dire concrètement** : une requête que votre client envoyait par erreur et qui
recevait un refus net peut maintenant **être exécutée**. Si votre client s'appuyait sur ce refus
comme sur une validation, il ne l'a plus.

**Ce qui n'a PAS bougé, et qui est le verrou important** : du **texte mal encodé** dans le corps de
la requête, un **demi-caractère Unicode** isolé, un **nombre réel hors plage**, et **du contenu
après la fin du document** sont **toujours refusés**, exactement comme avant. Un **octet nul brut**
(non échappé) termine toujours la lecture du corps, exactement comme avant. Et une imbrication de
100 000 niveaux a été mesurée : elle est analysée puis libérée **sans faire tomber le serveur** — ce
n'est pas un nouveau moyen de le mettre à genoux, c'est une entrée de plus qui est acceptée.

#### ⛔ Un octet nul dans une action de scénario est **tronqué en silence**, et c'est la conséquence directe du point ci-dessus

C'est la seule conséquence **fâcheuse** connue de l'ouverture ci-dessus, et elle est écrite ici
plutôt que découverte plus tard :

- vous pouvez maintenant faire passer un octet nul dans l'`action` d'une étape de scénario
  automatique (`autoscenario create` / `modify`), **ce qui était impossible avant** ;
- l'action est stockée **entière**, mais la réponse d'`autoscenario get` la rend **coupée au
  premier octet nul** : `a\0b` revient `"a"`. **Silencieusement.**

La cause est dans un fichier que cette série de tickets **n'a pas le droit de modifier**
(`Scenario::toJson()`), et sa réécriture est prévue par le ticket **E4.6d**. Le comportement est
**mesuré et épinglé par un test** pour qu'il ne puisse pas passer inaperçu jusque-là.
⚠️ **En attendant : n'utilisez pas d'octet nul dans une action de scénario.** Rien n'en a besoin, et
le serveur ne vous préviendra pas.

#### ⛔ `config get` : la plus grosse réponse de l'API, et la seule qui reflète des **fichiers**

`config get` renvoie le **texte brut** de `io.xml`, `rules.xml` et `local_config.xml`. Ce sont des
**fichiers utilisateur** : ils peuvent contenir n'importe quel octet, et rien en amont ne garantit
qu'ils soient bien encodés. Trois choses changent pour cette réponse :

- **un fichier mal encodé ne disparaît plus de la réponse.** Si `io.xml` contenait ne serait-ce
  qu'un octet mal encodé — un nom d'équipement importé d'un outil tiers suffit —, **la paire
  `"io.xml"` entière était retirée de la réponse, en silence**, avec un `200 OK` et un
  `"success":"true"` : le client recevait une configuration **indistinguable d'une installation
  qui n'a pas de `io.xml`**. Le fichier est désormais **présent**, les octets fautifs remplacés
  par `�` ;
- les trois noms de fichiers sortent maintenant **triés** (`io.xml`, `local_config.xml`,
  `rules.xml`) au lieu de l'ordre d'écriture (`io.xml`, `rules.xml`, `local_config.xml`) ;
- un caractère `DEL` présent dans un fichier de configuration part désormais **échappé**, donc la
  réponse **grandit** et le `Content-Length` suit.

ℹ️ **La liste blanche de `config put` n'a pas bougé** : seuls `io.xml`, `rules.xml` et
`local_config.xml` sont acceptés en téléversement, et tout autre nom est refusé — y compris un nom
qui contiendrait un octet nul, la comparaison portant sur la chaîne entière.

## 📦 `jansson` n'est plus une dépendance de compilation

*Pour l'**empaqueteur** et pour qui construit depuis les sources. Rien de ce qui suit ne change le
comportement du serveur.*

`configure.ac` ne demande plus `jansson >= 2.5` à `pkg-config` : la ligne
`requirements_calaos_common` ne le liste plus, et `CALAOS_COMMON_LIBS` ne porte plus `-ljansson`.
**`libjansson-dev` (ou son équivalent) peut être retiré des dépendances de construction du paquet**,
et le binaire ne se lie plus à `libjansson.so`.

Vérifié en construisant l'arbre complet, `distclean` compris, dans une image où `jansson.h`,
`jansson_config.h`, `jansson.pc` et `libjansson.so` avaient été effacés et où
`pkg-config --exists jansson` répond faux : `./autogen.sh && ./configure && make && make check`
passe, 111 suites, 0 échec.

Tout le JSON du serveur — l'API du port 5454, les événements temps réel, les wires des
extern-procs, le cache d'état, `eventlog`, `io_doc.json` — passe désormais par la seule
`nlohmann::json` vendorisée dans `src/lib/json.hpp`. Les changements de forme sur le fil que cette
bascule a entraînés sont décrits dans les sections précédentes ; celle-ci ne fait que retirer la
bibliothèque qui n'a plus d'appelant.

## 📦 Empaquetage — l'archive source est de nouveau constructible, et elle porte enfin les licences des bibliothèques embarquées

*Cette section ne s'adresse pas à l'utilisateur du serveur mais à l'**intégrateur** et à
l'**empaqueteur** : celui qui construit Calaos depuis un `tar.gz` et non depuis un `git clone`.
Rien de ce qui suit ne change le comportement du serveur.*

**Ce qui était cassé, et depuis quand.** Le tarball source produit par `make dist` est
**inutilisable depuis le 2025-02-16**, en deux temps :

- d'abord `make dist` **échouait** purement et simplement (`No rule to make target`, un chemin
  `EXTRA_DIST` qui pointait un niveau trop bas) — donc **aucune archive n'était produite** ;
- une fois cette faute réparée, l'archive se produisait, se dépliait, `configure` réussissait…
  et **la construction mourait** sur `fatal error: exprtk.hpp: No such file or directory`, puis
  sur `uvw/async.hpp: No such file or directory`. **417 fichiers suivis par git ne partaient pas
  dans l'archive, dont 126 sources et en-têtes C/C++.** Les bibliothèques embarquées
  (`uvw`, `exprtk`, `sqlite_modern_cpp`, `sole`, `llhttp`, `libquickmail`, `cpptui`,
  `uri_parser`) n'étaient déclarées par **aucune** variable de distribution : sur `uvw`,
  **1 fichier sur 82** partait — l'en-tête parapluie, précisément celui dont les `#include`
  échouaient ensuite.

**Personne ne l'avait vu parce que l'intégration continue ne lance ni `make dist` ni
`make distcheck`** : le chemin de release n'était couvert nulle part. Un paquet distribution
construit depuis un `git clone` n'a **jamais** été affecté ; seul le chemin tarball l'était.

**Ce qui marche maintenant.** `make distcheck` passe **de bout en bout** : l'archive se déplie,
se configure, **se construit**, **exécute la suite de tests complète depuis l'archive**
(95 tests, 95 succès — c'était une première), s'installe, se désinstalle, et **reproduit une
archive identique depuis l'arbre déplié**. Le tarball passe de **842** à **1081** entrées et
**ne dépend plus de l'état de l'arbre source** : une archive faite depuis un clone neuf et une
archive faite depuis un arbre entièrement construit ont **le même contenu**.

⚠️ Un second défaut, invisible tant que le premier tenait, a été trouvé et corrigé au passage :
`tests/check-config-docs.sh` **échouait en dur dans tout tarball**, parce qu'il compare la sortie
de `calaos_config options --markdown` à un fichier de référence commité qui, lui non plus, ne
partait pas. Un empaqueteur qui lançait `make check` après construction voyait donc un échec —
sur un fichier manquant, pas sur une régression.

**Licences — le point à retenir si vous redistribuez.** ⚠️ **L'archive ne contenait que 2 des 11
fichiers de licence** des bibliothèques tierces embarquées dans les sources. Manquaient
`exprtk/license.txt`, `libquickmail/COPYING`, `libquickmail/License.txt`, `llhttp/LICENSE`,
`llhttp/LICENSE-MIT`, `sole/LICENSE`, `sqlite_modern_cpp/License.txt`, `uvw/LICENSE` et
`uvw/docs/LICENSE`. **Une archive redistribuée sans les licences du code tiers qu'elle contient
est un problème de conformité, indépendant du fait qu'elle compile ou non** — et toute archive
produite avant cette version en souffrait. Les 11 fichiers voyagent désormais **avec le code
qu'ils couvrent**, dans leur répertoire d'origine. ⚠️ **11 sur 11 de ce qui existe dans le
dépôt** : `src/lib/uri_parser` ne contient **aucun** fichier de licence, ni avant ni après ;
ce n'est pas un fichier perdu à la distribution, c'est un fichier absent de l'import amont, et
il reste à obtenir.

**Si vous aviez contourné le problème** (patch local ajoutant des chemins à `EXTRA_DIST`,
construction depuis un export git plutôt que depuis le tarball) : le contournement n'est plus
nécessaire et un patch qui énumère des fichiers des répertoires ci-dessus entrera en conflit —
ces répertoires sont maintenant distribués **en entier**, par un mécanisme qui n'énumère rien.
(T3.45, T3.48)

## Les auto-scénarios sont enfin **écrits** dans votre configuration, au lieu d'être devinés

Jusqu'ici, un auto-scénario n'existait **nulle part** sur le disque. Ni ses étapes, ni ses pauses,
ni ses actions : le serveur les **re-devinait** à chaque démarrage, en reconnaissant des motifs
dans `rules.xml`. Un caractère changé dans ce fichier et une étape entière disparaissait,
définitivement, sans un message.

À partir de cette version, la **définition** du scénario est écrite dans `io.xml`, sur l'IO du
scénario lui-même, sous la forme d'attributs lisibles :

```xml
<calaos:input type="scenario" id="io_18" name="Monter volets matin"
              autoscenario_uid="as_0" autoscenario_cycle="false" autoscenario_enabled="true"
              autoscenario_steps="s0|s1"
              as_s0_pause="1.5"  as_s0_actions="io_77=up|io_78=up"
              as_s1_pause="0"    as_s1_actions="io_9=42"
              as_final_actions="io_9=false"/>
```

**Ce que ça change concrètement**

- **Toujours deux fichiers.** `io.xml` et `rules.xml`, comme avant. Rien de nouveau à sauvegarder,
  à téléverser ou à restaurer, et aucun outil à mettre à jour pour ne rien perdre.
- **`calaos_installer`, même ancien, préserve tout.** Ce sont des **attributs**, et l'installeur
  réécrit verbatim les attributs qu'il ne connaît pas. Il fallait le vérifier avant de choisir :
  ça a été fait, au source.
- **Un scénario qui référence un IO disparu ne l'oublie plus.** L'identifiant reste écrit dans la
  définition, donc réparable — là où auparavant un simple aller-retour par l'installeur effaçait
  l'action pour de bon, sans trace.
- **C'est lisible et réparable à la main.** Une valeur contenant `%`, `=` ou une barre verticale est
  encodée (`%25`, `%3D`, `%7C`) et redonne exactement les mêmes octets.

**Ce qui ne change pas encore, dans cette version** : ce sont toujours les règles qui **exécutent**
le scénario. La définition est écrite et conservée, mais rien ne s'en sert encore pour reconstruire
les règles — c'est l'étape suivante. Aucun comportement visible ne change : vos scénarios se
déclenchent exactement comme avant, l'API répond exactement comme avant.

**Rien n'a été supprimé de votre configuration.** L'ancien marqueur `auto_scenario` est laissé
strictement en place, et le nouvel identifiant vit **à côté** de lui. C'était la condition à tenir :
le re-clé aurait fait détruire, en silence et au premier démarrage, toutes les règles des scénarios
existants. (E4.6b)

### Un scénario n'est plus deviné à partir de ses règles : il est reconstruit à partir de sa définition (E4.6c)

Jusqu'ici, à chaque démarrage, le serveur **re-devinait** ce qu'était un auto-scénario en relisant
`rules.xml` et en comparant chaque règle, condition par condition et valeur par valeur, à la forme
qu'il attendait. Une règle qu'il ne reconnaissait pas était ignorée — puis détruite, et la
destruction écrite sur le disque. **Un seul caractère changé dans `rules.xml` coûtait une étape
entière, définitivement.**

Les règles sont maintenant **régénérées** depuis la définition, à chaque chargement. Ce qui change
pour vous :

- **Un fichier de règles abîmé se répare tout seul.** Une valeur éditée à la main, une étape dont la
  condition ne colle plus, une règle supprimée par un outil : tout est réécrit au démarrage suivant,
  à partir de la définition. Plus rien n'est perdu parce que le serveur « n'a pas reconnu ».
- ⭐ **Un aller-retour par `calaos_installer` ne peut plus amputer un scénario en silence.**
  L'installeur laisse tomber, au chargement, toute action dont l'identifiant ne résout plus, puis
  renvoie un `rules.xml` d'où la référence a simplement disparu. Auparavant le serveur ne pouvait
  pas le voir : le scénario tournait, amputé, en se déclarant sain. Maintenant l'action est
  réécrite depuis la définition, le scénario est signalé cassé, l'identifiant manquant est nommé,
  et le scénario **ne démarre pas** tant qu'il n'est pas réparé et réactivé.
- **Plus de doublons.** Une règle non reconnue était recréée à côté de l'ancienne ; deux
  reconstructions successives produisent désormais exactement les mêmes règles.
- **Les numéros d'étape ne se contredisent plus.** L'ordre d'exécution et le numéro écrit dans le
  fichier sont produits ensemble, donc ils ne peuvent plus diverger — une étape perdue au milieu ne
  décale plus les suivantes, elle est simplement reconstruite.
- **Lire un scénario ne le modifie plus.** Consulter la catégorie ou sérialiser un scénario effaçait
  auparavant, au passage, la trace du fait qu'il était cassé.

**Rien n'est détruit dans votre configuration.** Le balayage qui supprimait toute règle marquée que
le serveur n'avait pas su reconnaître **n'existe plus** : une règle qui ne vient pas du générateur
est la donnée de quelqu'un, pas un déchet. Et tant qu'une seule règle d'un scénario date d'avant
cette version, le générateur **ne touche à rien du tout** — ni destruction, ni doublon. Vérifié sur
une configuration de production réelle : 125 règles avant, 125 après, à l'octet près, y compris au
deuxième démarrage.

**Ce qui ne change pas encore** : les anciens scénarios restent des auto-scénarios visibles dans
l'API.

### ⛔ RUPTURE D'API ASSUMÉE : `autoscenario` change de format, et ce que vous lisez est enfin ce que vous pouvez renvoyer (E4.6d)

> ⛔ **Le format des réponses et des requêtes `autoscenario` change, sans compatibilité ascendante
> et sans convertisseur.** C'est une décision utilisateur : cette API n'a **aucun client**
> first-party (les 7 dépôts voisins ont été mesurés, `calaos_installer` ne l'appelle jamais), et
> l'ancien format rendait la perte de données inévitable. Un client tiers qui l'utiliserait doit
> être adapté.

**Le défaut que ça corrige, et il coûtait cher.** Ce que `autoscenario get` renvoyait n'était **pas**
ce que `autoscenario modify` acceptait. `get` émettait `enabled` ; `modify` lisait `disabled`, son
contraire, sous un autre nom. `get` n'émettait ni le nom, ni la visibilité, ni la pièce ; `modify`
les lisait tous les trois. **Une interface qui relisait un scénario et le renvoyait tel quel le
renommait en « New unnamed scenario », le rendait invisible et coupait sa planification** — avec
`success:"true"` et pas un mot d'avertissement.

Pire : une action dont l'IO avait disparu était **escamotée** de la réponse. Le scénario cassé
était renvoyé amputé, le client renvoyait l'amputation, et le serveur écrivait un scénario
définitivement privé de cette action — en se déclarant sain au passage. **Relire et renvoyer
suffisait à perdre une action pour toujours.**

**Ce que ça donne maintenant**

- ⭐ **Un aller-retour est un aller-retour.** `get` → `modify` du **même document** → `get` rend
  **exactement le même document**, y compris sur un scénario cassé. Rien ne se perd en chemin.
- **Une action n'est jamais escamotée.** Elle est décrite par `{"io": …, "value": …,
  "resolved": …}` : nommée par son identifiant, gardée **à sa place** même quand l'IO n'existe
  plus, et marquée `resolved:"false"`. Un scénario cassé se répare au lieu de s'éroder.
- **`final_step` est un champ à part.** `steps` contient les étapes et rien d'autre : sa longueur
  est le nombre d'étapes. L'ancien piège — le tableau `steps` était **plus long d'un** que le
  `steps_count` qui l'accompagnait, parce que l'étape terminale y était ajoutée artificiellement —
  n'est plus exprimable. `steps_count` disparaît : un tableau a une longueur.
- **Chaque étape porte un `step_id`** stable, jamais réutilisé. L'API adresse une étape par son
  identité, plus par sa position.
- **`enabled` est le seul nom** du choix « ce scénario suit sa planification », en lecture comme en
  écriture. `disabled` disparaît de l'API.
- **`name`, `visible`, `room_name` et `room_type` sont émis** par `get`, donc préservés par un
  renvoi.
- **Une requête mal formée est refusée AVANT que quoi que ce soit ne soit touché**, et le refus dit
  ce qui cloche (pièce inconnue, `steps` qui n'est pas un tableau, pause qui n'est pas un nombre,
  `step_id` en double, action sans `io`). Auparavant `modify` **détruisait d'abord** toutes les
  règles du scénario et découvrait ensuite qu'il ne pouvait pas les reconstruire.
- **`reenable` refuse** un scénario encore cassé, en nommant les identifiants à réparer — au lieu
  de le relancer amputé.

**Deux valeurs qui étaient silencieusement abîmées ne le sont plus**

- ⭐ **Un octet zéro dans la valeur d'une action était coupé à cet octet, en silence.** Une valeur
  `a\0b` revenait `"a"`, avec `200 OK` : ni erreur, ni journal, ni clé manquante — juste une valeur
  plus courte que celle envoyée. Elle traverse maintenant entière.
- **Une valeur en UTF-8 invalide était supprimée avec sa clé.** Elle est maintenant remplacée par le
  caractère de remplacement standard, comme partout ailleurs dans l'API depuis la migration JSON.

**Ce qui ne change pas** : les huit sous-commandes (`list`, `get`, `create`, `delete`, `modify`,
`add_schedule`, `del_schedule`, `reenable`), la sémantique du drapeau collant
`disabled_missing_io` et de la commande `reenable`, et le fait que tout reste des **chaînes**
(`"true"`, `"1.5"`) comme dans le reste de l'API Calaos.

**⛔ Deux pièges pour qui migre un client tiers — relevés au merge, à lire avant d'adapter**

- ⛔ **`disabled` n'est plus lu du tout, et son absence n'est pas neutre.** Un client qui continue
  d'envoyer `{"disabled": "false"}` en croyant activer son scénario ne reçoit **aucune erreur** :
  la clé est simplement ignorée, `enabled` est absent, et `enabled` **vaut `false` par défaut**.
  Le scénario est donc créé — ou laissé — **désactivé**, en silence. C'est le seul endroit où
  l'ancien nom échoue sans le dire ; partout ailleurs le nouveau schéma refuse explicitement.
- ⛔ **La pièce est désormais obligatoire et vérifiée, sur `create` comme sur `modify`.**
  Auparavant `create` retombait silencieusement sur **la première pièce de la maison**
  (`get_room(0)`) quand la pièce nommée n'existait pas, et `modify` tolérait une pièce absente en
  sautant simplement le déplacement. Les deux **refusent** maintenant, avec
  `invalid payload: no room "…" of type "…"`. C'est voulu — l'ancien comportement rangeait un
  scénario dans une pièce arbitraire sans le dire — mais un `modify` partiel (renommer seulement)
  qui passait avant échoue désormais s'il ne nomme pas correctement la pièce.
  ⚠️ **Corollaire connu, et c'est la seule brèche de l'aller-retour** : un IO scénario qui
  n'appartient à **aucune** pièce est émis par `get` avec `room_name` et `room_type` **vides**, et
  ce document-là, renvoyé tel quel, est **refusé**. L'aller-retour est une identité pour tout
  scénario rangé dans une pièce, c'est-à-dire tous ceux que l'API sait créer.
