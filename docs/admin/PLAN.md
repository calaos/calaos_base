# Administration de Calaos depuis la webapp

État : plan validé en conception, **non lancé**. On ne l'implémente que sur décision de Raoul.
Historique : trois rounds d'analyse Fable (rôles, mot de passe admin léger, inventaire), puis un arbitre, une relecture et un audit de sécurité avec PoC (le rapport d'audit (hors dépôt)), plus les décisions de Raoul du 2026-10-17/18.

## Contexte
Beaucoup d'utilisateurs n'ont pas Calaos Home desktop, le kiosque qui tourne sur la box. Pour mettre à jour ou entretenir la box, ils n'ont que SSH et PuTTY, ce qui est souvent hors de leur portée.

Objectif :
- faire depuis la webapp les mises à jour, le reboot, puis la maintenance (console, logs, services, réseau) ;
- plus tard, une partie depuis Calaos mobile.

Contraintes : simple pour l'utilisateur, suffisamment sûr, pas d'usine à gaz.

## Constats qui fondent le design
- **Les mises à jour** passent par calaos-container, un daemon Go root qui écoute sur `127.0.0.1:8000`. Il est protégé par un token stocké dans `/run/calaos/calaos-ct.token`, à faible entropie. Deux failles démontrées par PoC :
  - un fichier token vide laisse tout passer ;
  - `?token=` est accepté sur toutes les routes.
- **calaos-server a déjà l'équivalent de root.** Il tourne avec `--privileged --network=host` et monte `/run/calaos` en lecture-écriture. Une RCE dans calaos_server donne root sur la box, quel que soit le design.
- **Il n'y a qu'un compte partagé** (`cn_user/cn_pass`, `user/pass` par défaut), stocké en clair. Il donne `config get/put` (et donc les secrets), `get_mcp_info` et `change_cred`.
- **Le WS n'a ni contrôle d'Origin ni session.** Le mobile ignore les erreurs TLS. haproxy présente un certificat auto-signé sur le port 443, et le port 5454 est en HTTP clair.
- **Lua `requestUrl` sort de 127.0.0.1**, sans en-têtes. Toute confiance fondée sur l'IP est donc contournable.
- **L'installer** n'utilise que des POST sur `/api.php`, sans Origin. Le mobile, remote_ui et MCP n'envoient pas d'Origin non plus. Seul le proxy Vite de dev en envoie un.
- **Auto-upgrade de calaos-container** : la postinst ne redémarre pas le daemon (`--no-stop-on-upgrade`). Le nouveau daemon n'est actif qu'après un reboot.
- **Upgrade synchrone** : le résultat est perdu par `resetStatus()`, et rien ne lance `dpkg --configure -a` au boot.
- **`/mnt/calaos` est dans le snapshot racine** : un rollback snapper restaure aussi la config.

## Le design
### Un mot de passe admin distinct, vérifié par calaos-container, fermé par défaut
- **Daemon**
  - Hash argon2id (`golang.org/x/crypto`, m = 32 à 64 MiB, t = 2 à 3, p = 1) dans `/mnt/calaos/admin/`. Ce répertoire est un subvolume btrfs créé par `init_calaosfs` et exclu de snapper, si bien qu'un rollback ne ressuscite pas un ancien hash : il le **perd**, car le répertoire est vide dans le snapshot, et on repasse alors par le bootstrap. `init_calaosfs` (exécuté en `ExecStartPre` de quatre units, avec `set -e`) procède ainsi :
    - si `/mnt/calaos` n'est pas en btrfs (`stat -f -c %T`), par exemple sur le live USB, simple `mkdir -p` ;
    - sinon, si `btrfs subvolume show` échoue, `rmdir` du répertoire vide laissé par un rollback, puis `create` ;
    - dans tous les cas, `chmod 700`.
  - Routes `GET /api/admin/status`, `POST /api/admin/verify`, `POST /api/admin/set`.
  - Le débit est limité **avant** le hachage : une seule vérification à la fois, environ 5 par minute.
  - `verify` renvoie une session opaque de 15 min émise par le daemon. calaos_server ne fait que la relayer, donc le déverrouillage survit au redémarrage de calaos_server pendant une mise à jour.
  - CLI `calaos-os admin set|reset`.
  - `admin/set` crée le hash en `O_EXCL`, pour que deux clients ne puissent pas faire le bootstrap en même temps : un second appel reçoit `admin_already_set`.
  - Le daemon n'a pas de client push. Il publie `admin_set` et `admin_reset` (en phase 2 s'y ajoute `push_code {token, message}`) sur `/events`. calaos_server, abonné en permanence (CB3), envoie le push via `NotifManager` et enregistre l'événement persistant.
- **calaos_server**
  - Message WS `admin_unlock {admin_pass}`, avec `LoginThrottle` en amont. Un échec compte comme un échec de login.
  - `admin_unlock` renvoie `{session, expires_in}`.
    - Chaque message `admin` porte la `session`, et calaos_server ne garde aucun état.
    - La webapp garde la session en mémoire et la renvoie après reconnexion et re-login. Si le daemon répond `401`, elle redemande le mot de passe admin.
    - Si le daemon répond 404 sur `/api/admin/status` (ancien daemon encore en mémoire), la webapp affiche « Redémarrez la box pour activer l'administration ».
  - Une fois l'admin défini, ces actions sont réservées à l'admin :
    - `config get/put` (en HTTP, avec un champ `admin_pass` en plus) ;
    - `get_mcp_info` ;
    - le message `admin`.
  - `change_cred` (le mot de passe du foyer) reste au niveau foyer, car il exige déjà le mot de passe actuel. L'écran de Calaos mobile continue donc de fonctionner (décision de Raoul, 2026-10-18).
  - Tant que l'admin n'est pas défini, `admin` répond `admin_not_set` et le reste fonctionne comme aujourd'hui.
  - Le scope service MCP refuse `admin_unlock` et `admin`, avec mise à jour de `JsonApiServiceScope_test.cpp`. La console n'a pas de login : seul le ticket authentifie.
- **Relais `admin`**
  - Liste blanche des deux côtés : `update/*`, `system/reboot|restart|info`, console.
  - `install/*` et `rollback_snapshot` ne sont jamais relayés.
  - `update/status` et `/events` restent lisibles avec le login simple, pour que la progression reste visible.
- **Transport**
  - Invariant : haproxy est le seul terminateur TLS et le seul chemin d'accès admin. Tout accès distant passe par le port 443, que ce soit par le DDNS aujourd'hui ou par un VPN demain (MLSH, par exemple, arrive sur l'IP overlay : `https://<ip-overlay>`).
  - On ne fait jamais confiance à une interface ni à une plage VPN comme transport sûr. Trois raisons :
    - MLSH n'a pas d'ACL : tout nœud du cluster est un pair complet ;
    - un serveur signal compromis peut injecter un pair ;
    - la plage 100.64/10 est aussi celle du CGNAT des opérateurs.
  - Le test tient dans un seul prédicat `secureTransport()` (CB2). Une option `trusted_secure_peers` pourra l'étendre plus tard sans toucher aux appelants.
  - Ces actions ne sont acceptées que si le pair est en loopback **et** que `X-Forwarded-Proto` vaut `https` (haproxy l'écrase) :
    - `admin_unlock`, `admin` et `/api/console` ;
    - une fois l'admin défini, `config` et `get_mcp_info`.
  - L'installer passe déjà par `https://IP/api.php` par défaut. Une URL en `http://…:5454` reçoit un refus, que CI1 affiche.
  - Sinon, la webapp affiche « utilisez https:// », quel que soit le chemin (LAN, DDNS, VPN).
  - Rien d'admin ne passe en clair sur le port 5454.
  - `haproxy_pre` ajoute `set-header X-Forwarded-Proto https` au `haproxy.cfg` persistant s'il ne l'a pas déjà, car ce fichier n'est généré qu'une fois.
- **Origin (CB1)**
  - On compare l'Origin à une liste d'identités de la box : IPs des interfaces, hostname (à vérifier dans le conteneur `--network=host`), `hostname.local` (valable en LAN seulement, c'est du mDNS), et `api_allowed_hosts` pour les hôtes supplémentaires (dev, noms VPN, DNS maison). Une IP overlay portée par une interface du conteneur `--network=host` entre automatiquement dans la liste.
  - Exception : si le pair est en loopback et que `X-Forwarded-Proto` vaut `https`, on accepte un Origin dont l'hôte est égal à `Host`.
    - Sur ce chemin, TLS empêche le rebinding : le certificat ne correspond pas à l'hôte rebindé et le navigateur abandonne.
    - C'est ce qui fait marcher le DDNS sans configuration.
    - Hors de ce chemin, comparer à `Host` ne suffit pas : le DNS rebinding passe, comme l'a montré le PoC.
  - Le contrôle porte sur le handshake WS **et** sur les requêtes HTTP `/api.php`. `Access-Control-Allow-Origin` n'est renvoyé que pour un Origin accepté (`HttpClient.cpp:337` reflète tout aujourd'hui).
  - `Origin: null` est refusé.
  - Casse, point final et port par défaut sont normalisés, et le port n'est comparé que si `Host` en contient un.
  - L'absence d'Origin est acceptée (clients natifs).

### Bootstrap et récupération
- **Bootstrap** : avec le mot de passe partagé, à condition qu'il ne soit plus `user/pass`, on peut définir l'admin une seule fois. Deux alertes :
  - un push « Mot de passe admin défini » envoyé à tous les tokens ;
  - un événement persistant, affiché à chaque client au login suivant.

  Il n'y a aucune condition d'IP.
- **Changer le mot de passe admin** : en phase 1, `admin/set` avec une session admin valide. À partir de la phase 2, il faut en plus le code push.
- **Récupération** : `calaos-os admin reset` en SSH, sur le LAN ou via le VPN.
  - La commande efface le hash, les approbateurs, les sessions, et tue les consoles ouvertes.
  - Elle envoie un push et affiche une bannière. On repasse ensuite par le bootstrap.
  - Pas de reset par clé USB : décision de Raoul, jugé compliqué pour rien.

### Second facteur : code par push vers un téléphone approbateur (phase 2)
- **Enrôlement**
  - Un admin déverrouillé choisit son téléphone parmi les tokens `register_push`, qui gagne un nom d'appareil.
  - Le daemon envoie un code **à ce seul token**, et l'utilisateur le tape dans la webapp : c'est la preuve qu'il possède le téléphone.
  - Le daemon stocke le token de l'approbateur et remet à l'app un secret d'appareil. Ce secret est transmis dans la réponse WS à `register_push`, sur la connexion qui porte ce token, et jamais dans un push. Stockage côté app :
    - iOS : le Keychain existant (`A0SimpleKeychain`) ;
    - Android : le QSettings privé de l'app, comme `cn_pass` aujourd'hui, en l'excluant de la sauvegarde Android (`allowBackup`). Pas de code Keystore à écrire.
- **Nombre d'approbateurs** : un seul suffit (décision de Raoul). Un second est possible en option.
- **Rotation du token push** : acceptée seulement si elle est authentifiée par le secret d'appareil.
- **Téléphone perdu ou réinstallé** : enrôlement par le second approbateur s'il existe, sinon `calaos-os admin reset`. En attendant, les mises à jour et le reboot restent possibles avec le seul mot de passe admin : seuls la console, le réseau et le changement d'approbateur sont bloqués.
- **Code**
  - 6 chiffres, valable 60 s, à usage unique.
  - Lié à la session daemon, à l'action et au hash des paramètres.
  - Invalidé après 5 essais. Un seul code en attente par session, au moins 10 s entre deux générations, environ 10 par heure au total.
  - Comparaison en temps constant.
- **Le téléphone approbateur n'a besoin que du push sortant** : il n'a pas à joindre la box. Seul l'enrôlement l'exige, sur le LAN ou via le VPN.
- **Protège** : chaque ouverture de la console, le changement du mot de passe admin, le changement d'approbateur, la config réseau.
- **Sans approbateur enrôlé**, ces actions sont indisponibles. Il reste la console SSH.

### Console web (phase 2)
- **Principe**
  - Shell root sur l'hôte, ouvert par le daemon dans un pty (`creack/pty`).
  - Endpoint dédié `/api/console` : un tunnel brut dans calaos_server, sur le modèle de `McpProxyHandler`.
  - Côté webapp, `@xterm/xterm` est chargé seulement sur la route console.
- **Ouverture**
  - Admin déverrouillé, puis un code push **par ouverture**, qui délivre un ticket à usage unique de 30 s. Il n'y a pas de flag « console activée » : c'est le code qui active la console.
  - Le ticket est envoyé dans la **première trame** WS, jamais dans l'URL (haproxy journalise les URL). Rien n'atteint le pty avant validation.
- **Limites**
  - Le pty est tué (SIGHUP au groupe) après 15 min d'inactivité, au bout d'1 h, à la coupure du WS ou à la désactivation.
  - 2 sessions au maximum.
  - `TERM` sur liste blanche, environnement explicite, resize borné, backpressure.
- **Traçabilité**
  - Push « Console ouverte » à chaque ouverture.
  - Audit persistant dans `/mnt/calaos/admin/audit.log`. Chaque entrée consigne le pair TCP, l'IP effective et XFP. Derrière un agent qui redirige vers loopback, l'identité se réduit à 127.0.0.1 : c'est la même limite que pour le MCP.
  - Ping WS toutes les moins de 45 s, à cause des `timeout client/server` de haproxy.

## Parcours utilisateur
- **Installation existante (3 étapes).** La première montée vers les versions admin se fait encore par SSH (`calaos-os upgrade`) ou par le kiosque, suivie d'un reboot. Ensuite : onglet Admin, puis « Définir le mot de passe admin » (un push part), puis « Mettre à jour ». Par la suite : Admin, mot de passe, Mettre à jour.
- **Installation neuve (4 étapes).** Login `user/pass`, changement du mot de passe foyer, définition du mot de passe admin, mise à jour.
- **Ouvrir la console (phase 2).** Admin, puis Console, puis saisie du code reçu sur le téléphone, et le terminal s'ouvre.
- **Mot de passe admin perdu.** `calaos-os admin reset` en SSH, puis on le redéfinit dans la webapp.

## Tickets
### Phase 1 (≈ 13,5 j) : mises à jour et reboot depuis la webapp

| Repo | Ticket | Contenu | Durée |
|---|---|---|---|
| calaos-container | CC1 | mot de passe admin : hash, subvolume, routes, session daemon, débit limité avant hachage, CLI `set/reset`, audit | 2 j |
| calaos-container | CC2 | token : 32 octets urandom en 0600, fail-closed si vide, comparaison constante, `?token=` uniquement sur `/events`, adresse d'écoute `127.0.0.1` codée en dur par défaut (`config.go:24` écoute aujourd'hui sur toutes les interfaces si le toml manque, ce qui exposerait l'API sur une interface VPN) | 0,5 j |
| calaos-container | CC3 | upgrade asynchrone (202 + job), dernier résultat persisté, `reboot_required` | 1,5 j |
| calaos-container | CC4 | `ExecStartPre=-/usr/bin/dpkg --configure -a` (avec le `-` : un dpkg cassé n'empêche pas le daemon de démarrer), `KillMode=process` | 0,5 j |
| calaos_base | CB1 | Origin sur WS et HTTP : liste d'identités, règle haproxy-https, `api_allowed_hosts`, `null` refusé, normalisation, ACAO conditionnel (`HttpClient.cpp:337` et `WebSocket.cpp:401`) | 1 j |
| calaos_base | CB2 | `admin_unlock`, gating, `admin_not_set`, contrôle de transport, bootstrap, push et événement persistant, refus pour le scope MCP | 2 j |
| calaos_base | CB3 | relais `admin` en liste blanche, retransmission de `/events` | 2 j |
| calaos-web-app | WA1 | vue Admin : définir / déverrouiller, mises à jour, progression, reprise après redémarrage serveur, reboot, message « utilisez https:// », écran de changement du mot de passe foyer | 3 j |
| calaos-web-app | WA2 | Origin sur le proxy de dev | 0,1 j |
| calaos_installer | CI1 | champ mot de passe admin optionnel, refus serveur affiché | 0,5 j |
| doc-website | DOC | pages update et services : la webapp remplace SSH ; procédure de reset | 0,5 j |

### Phase 2 (≈ 9 j) : console web et téléphone approbateur

| Repo | Ticket | Contenu | Durée |
|---|---|---|---|
| calaos-container + calaos_base | AP1 | approbateurs et codes push (enrôlement, secret d'appareil, limites, liaison à l'action) | 2,5 j |
| calaos_mobile | AP2 | nom d'appareil dans `register_push`, stockage du secret d'appareil, rotation de token authentifiée | 1 j |
| calaos-container | CO1 | console pty, ticket, limites | 1 j |
| calaos_base | CO2 | tunnel `/api/console` | 1 j |
| calaos-web-app | CO3 | vue console xterm | 1 j |
| calaos-web-app | WA3 | enrôlement de l'approbateur, saisie du code | 1 j |
| calaos_mobile | CM2 | épinglage TOFU du certificat à la place de `ignoreSslErrors`, par hôte (IP LAN, nom DDNS, IP overlay), avec nouvelle confirmation en cas de changement de clé (renouvellement Let's Encrypt) | 1 j |
| doc-website | DOC | console, approbateur | 0,5 j |

### Phase 3 : diagnostics et confort
- Restart de services sur liste autorisée, logs bornés, heure, liste blanche `calaos_config`, téléchargement de la sauvegarde.
- Réseau, avec « appliquer puis confirmer » et retour automatique sous 60 s.
- Mises à jour depuis le mobile (CM1).
- CC5 : apt dans une unit transitoire, seulement si CC4 se révèle insuffisant à la recette.
- Code affiché dans l'app plutôt que dans la notification.
- Digests `@sha256` et JSON de releases signé (minisign).
- Inventaire des devices utilisés, pour pouvoir sortir calaos-server de `--privileged`.

## Ce qu'on ne fait pas
- Rôles ni `users.json`.
- Cookies, sessions navigateur, JWT.
- Confiance fondée sur l'IP.
- Signatures ECDSA d'appareil, WebAuthn, TOTP, number matching. Le code push ne s'applique qu'aux actions listées en phase 2 ; tout le reste de l'admin n'exige que le mot de passe admin.
- Reset par clé USB.
- Rollback de snapshot à distance, installation disque à distance.
- Mot de passe root SSH, calibration tactile.
- Admin depuis le mobile en phase 1 et 2, sauf l'enrôlement de l'approbateur.

## Risques résiduels acceptés
- Une RCE dans calaos_server donne root (conteneur privilégié).
- Un accès physique donne root.
- Avec la console, un attaquant venu d'Internet doit disposer du mot de passe partagé, du mot de passe admin et du téléphone.
- Un relais push compromis voit les codes, mais pas le mot de passe admin.
- Un MITM sur le LAN reste possible côté mobile jusqu'à CM2.
- Le code est lisible sur l'écran verrouillé jusqu'à la phase 3.
- Un rollback via GRUB, qui suppose un accès physique, réinstalle d'anciennes versions sans gating.
- Avant le bootstrap, quiconque a le mot de passe partagé peut définir l'admin le premier. On le détecte par le push et la bannière, et on répare par `admin reset`.
- Webapp sur certificat auto-signé : l'utilisateur accepte un avertissement. Un MITM actif sur le LAN peut servir le port 80 en clair, et HSTS n'est pas possible. Un certificat Let's Encrypt via le DDNS supprime ce risque. Via un VPN sans certificat, comme MLSH, l'avertissement reste, mais le MITM n'est plus possible à l'intérieur du tunnel.
- Le limiteur argon2 global (environ 5 par minute) permet à un tiers de retarder les déverrouillages légitimes.

## Vérification
- **calaos-container** (tests Go) :
  - token vide refusé ;
  - `?token=` refusé hors `/events` ;
  - hash et limitation de débit ;
  - session et expiration ;
  - upgrade asynchrone et résultat persisté ;
  - reset via CLI.
- **calaos_base** (tests JsonApi) :
  - `admin_not_set` ;
  - gating de `config` ; `change_cred` toujours accepté au niveau foyer ;
  - refus en HTTP clair et hors haproxy ;
  - Origin étranger, `null` ou rebindé refusé ;
  - scope MCP refusé ;
  - expiration à 15 min.
- **Webapp** : e2e sur le mock avec la coupure et la reprise pendant l'upgrade.
- **Installer** : enregistrement en ligne avec et sans mot de passe admin.
- **Phase 2** : brute force du code bloqué après 5 essais, ticket console non rejouable, pty tué à l'expiration.
- **Recette sur la box** : upgrade complet depuis la webapp, y compris l'auto-upgrade de calaos-server.
