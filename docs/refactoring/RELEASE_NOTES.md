# Notes de version — changements visibles utilisateur

> Accumulés pendant le refactoring (phases 1→4). À reprendre dans le changelog de la prochaine
> release de calaos_server. Ne liste **que** ce qu'un utilisateur ou un intégrateur peut
> observer — les corrections internes (UAF, fuites, durcissements) ne sont pas ici.
> Ordre : impact décroissant.

## ⚠️ Comportements qui changent sur une installation existante

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
