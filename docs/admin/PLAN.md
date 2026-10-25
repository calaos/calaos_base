# Administration de Calaos depuis la webapp

État : plan validé en conception, **non lancé**. On ne l'implémente que sur décision de Raoul.
Historique : trois rounds d'analyse Fable (rôles, mot de passe admin léger, inventaire), un arbitre, une relecture et un audit de sécurité avec PoC (hors dépôt), les décisions de Raoul du 2026-10-17/18, puis une red team, une revue d'ingénierie et une revue finale de cohérence le 2026-10-22, arbitrées par Raoul le même jour.

## Contexte
Beaucoup d'utilisateurs n'ont pas Calaos Home desktop, le kiosque qui tourne sur la box. Pour mettre à jour ou entretenir la box, ils n'ont que SSH et PuTTY, ce qui est souvent hors de leur portée.

Objectif :
- faire depuis la webapp les mises à jour, le reboot, puis la maintenance (console, logs, services, réseau) ;
- plus tard, une partie depuis Calaos mobile.

Contraintes : simple pour l'utilisateur, suffisamment sûr, pas d'usine à gaz.

## Constats qui fondent le design
Les constats calaos-container portent sur `origin/main`.
- **Les mises à jour** passent par calaos-container, un daemon Go root qui écoute sur `127.0.0.1:8000`, protégé par un token dans `/run/calaos/calaos-ct.token`.
  - Déjà corrigé sur `origin/main` : token vide refusé, comparaison en temps constant (CC-011) ; adresse d'écoute `127.0.0.1` par défaut, adresse vide refusée (CC-014). Ces correctifs n'atteignent les box qu'une fois le deb publié et le daemon redémarré.
  - Reste vrai : token à faible entropie (`date+$RANDOM`, mode 0644, `init_calaosfs`) et `?token=` accepté sur toutes les routes. Le daemon stable `1.7.0` n'accepte que l'en-tête `Authorization` : `?token=` est apparu en `1.7.1-dev.2`, pour le WS `/events`.
  - Le dépôt a un backlog (`docs/backlog/CC-0xx`, `docs/BOARD.md`) avec propriété de fichiers : CC-040 possède `app/`, `cmd/` et `docs/API.md`, CC-042 possède l'unit systemd.
  - Le CLI `calaos-os` envoie déjà `Authorization: Bearer`. calaos_server n'appelle pas le daemon aujourd'hui : ni client HTTP, ni client WebSocket. Le cas « ancien calaos_server, nouveau daemon » est donc trivial.
  - calaos_home (le kiosque, `github.com/calaos/calaos_home`) appelle aussi le daemon pour les mises à jour ; son dépôt n'a pas été vérifié.
- **calaos-server a déjà l'équivalent de root.** Il tourne avec `--privileged --network=host` et monte `/run/calaos` en lecture-écriture. Une RCE dans calaos_server donne root sur la box, quel que soit le design.
- **Il n'y a qu'un compte partagé** (`cn_user/cn_pass`, `user/pass` par défaut), stocké en clair. Il donne `config get/put` (et donc les secrets), `get_mcp_info` et `change_cred`. `checkCredentials` retombe sur les clés legacy `calaos_user/calaos_password` si `cn_*` est vide, et accepte le login vide si les quatre le sont.
- **Le WS n'a ni contrôle d'Origin ni session.** Le mobile ignore les erreurs TLS. haproxy présente un certificat auto-signé sur le port 443, et le port 5454 est en HTTP clair.
- **haproxy pose `X-Forwarded-Proto`** sur ses deux frontends depuis le premier `haproxy_pre`, et le template DDNS aussi. Mais `haproxy.cfg` n'est généré qu'une fois et l'utilisateur peut l'éditer.
- **Le loopback n'est pas que haproxy.** Tous les conteneurs partagent le netns hôte : mosquitto en anonyme sur `0.0.0.0:1883`, frontend zigbee2mqtt sans `auth_token` sur `:8080`, grafana 8 en root sur `:3000`. Un conteneur compromis est un pair loopback qui peut forger XFP et XFF.
- **Lua `requestUrl` sort de 127.0.0.1**, sans en-têtes. Toute confiance fondée sur l'IP est donc contournable.
- **L'installer** n'utilise que des POST sur `/api.php`, sans Origin. Le mobile, remote_ui et MCP n'envoient pas d'Origin non plus. Seul le proxy Vite de dev en envoie un.
- **Auto-upgrade de calaos-container** : la postinst ne redémarre pas le daemon (`--no-stop-on-upgrade`). L'ancien processus tourne jusqu'à un `systemctl restart calaos-container` ou un reboot. apt tourne dans le processus du daemon (libapt-pkg).
- **Trois outils de mise à jour** existent sur les box : `calaos-os upgrade` et le bouton du kiosque passent par le daemon, qui exécute apt lui-même ; `apt upgrade` passe à côté.
- **Upgrade synchrone** : le résultat est perdu par `resetStatus()`, rien ne lance `dpkg --configure -a` au boot, et le CLI comme le kiosque reposent sur le POST bloquant et la forme de réponse actuelle.
- **`/events` du daemon** : un hub unique, non bloquant, sans rejeu. Un événement publié pendant que calaos_server est arrêté est perdu. Le CLI ignore les types d'événements inconnus.
- **calaos_server ignore en silence** un message WS inconnu : un ancien serveur ne répond jamais à `admin_unlock`. Il pinge déjà ses WS toutes les 15 s, et haproxy les place sous `timeout tunnel 3600s`.
- **`init_calaosfs`** tourne en `ExecStartPre` de dix units, en parallèle au boot, avec `set -euo pipefail` : chaque étape doit être idempotente.
- **`/mnt/calaos` est dans le snapshot racine** : un rollback snapper restaure aussi la config. Ce n'est pas un point de montage.
- **`docs/refactoring/DECISIONS.md:694-699`** (« Pas de forçage des identifiants par défaut ») interdit de bloquer sur `user/pass` ; le bootstrap admin y déroge.

## Le design
### Un mot de passe admin distinct, vérifié par calaos-container, fermé par défaut
- **Daemon**
  - Hash argon2id (`golang.org/x/crypto`, m = 32 à 64 MiB, t = 2 à 3, p = 1) dans `/mnt/calaos/admin/`, un répertoire ordinaire en 0700 créé par `mkdir -p` et `chmod 700` dans la boucle existante d'`init_calaosfs`.
    - Pas de sous-volume btrfs : un rollback restaure le mot de passe admin de l'époque, comme le mot de passe foyer. `calaos-os admin set` couvre le cas contraire.
  - Routes `GET admin/status`, `POST admin/verify`, `POST admin/set`, `POST admin/reset`, `POST admin/alerts/ack`, nées dans l'enveloppe de CC-040. Les chemins sont relatifs au préfixe choisi par CC-040 (par exemple `/api/v2/admin/status`).
  - Mot de passe admin de 10 caractères au moins, refusé sinon par `admin/set`.
  - Le débit est limité **avant** le hachage : une seule vérification à la fois, et une vérification concurrente est refusée tout de suite (429) au lieu d'être mise en file. Le budget (environ 5 par minute) ne compte que les **échecs**.
  - `verify` renvoie une session opaque émise par le daemon :
    - 32 octets CSPRNG, 15 min **absolues** (non glissantes) ;
    - invalidée par `admin/set`, `admin/reset` et, en phase 2, 5 échecs de code push (décision de Raoul, 2026-10-22) ;
    - gardée dans `/run/calaos-container/` (0700, créé par le daemon avec `MkdirAll`), qui n'est monté dans **aucun** conteneur. Pas de `RuntimeDirectory=` dans l'unit : systemd le viderait à chaque arrêt, et les sessions ne survivraient pas au redémarrage du daemon après sa propre mise à jour.
  - calaos_server ne fait que relayer la session, donc le déverrouillage survit aussi au redémarrage de calaos_server pendant une mise à jour.
  - CLI `calaos-os admin set|reset`. Le mot de passe est lu sur stdin (deux fois), jamais en argument : `RunCommand` journalise les argv et `ps` les expose.
  - `admin/reset` reste une route du daemon, appelée par le CLI : un seul mécanisme à tester.
  - `admin/set` crée le hash en `O_EXCL`, pour que deux clients ne puissent pas faire le bootstrap en même temps : un second appel reçoit `admin_already_set`.
  - `admin/set` a trois modes, fixés dans `docs/API.md` :
    - bootstrap : pas de hash, pas de session, `O_EXCL` ;
    - changement : session valide (et code en phase 2) ;
    - récupération : `{"recovery": true}` avec le seul bearer, envoyé uniquement par `calaos-os admin set`, jamais relayé par calaos_server (hors table).

    Les trois invalident toutes les sessions et publient l'alerte `admin_set`. Le mode récupération est une limite assumée : quiconque tient le bearer peut l'utiliser, comme une RCE dans calaos_server.
  - **Alertes** : le daemon n'a pas de client push. Il persiste `admin_set`, `admin_reset`, `approver_set`, `approver_removed` et `console_opened` (et, en phase 3, `network_rolled_back`, amendement (b)) dans `/mnt/calaos/admin/` et les rejoue à chaque nouvel abonné `/events` jusqu'à l'ack de calaos_server, qui l'envoie après avoir enregistré l'événement. Une alerte émise pendant l'arrêt de calaos_server arrive donc au redémarrage, et une seule fois. Elle ne porte ni token ni code.
  - Tout appel porteur d'une session est refusé sur `install/*`, `rollback_snapshot` et `network/*` (middleware), sauf, depuis la phase 3, `network/state`, `confirm`, `revert` et `apply` (amendement (b)). `system/reboot` répond 409 pendant un job.
  - Audit dans `/mnt/calaos/admin/audit.log` et sur stdout (donc journald, hors snapshot) : adresse du client, action, résultat, id de session.
    - L'id de session est formé des 8 premiers caractères hexadécimaux du SHA-256 de la session, jamais de la session elle-même.
    - L'adresse du client vient de l'en-tête `X-Calaos-Client-IP` posé par calaos_server (défini dans `API.md`).
- **calaos_server**
  - Action `admin_unlock {admin_pass}` en WS **et** en HTTP, avec `LoginThrottle` en amont. Un échec ne compte **pas** comme un échec de login foyer : il a sa propre clé `LoginThrottle`, `admin:<ip>`, avec un backoff jusqu'à 60 s et la réponse `rate_limited`, en plus du limiteur du daemon (décision de Raoul, 2026-10-23).
    - Raison : avec une clé partagée, chaque login foyer réussi remettrait à zéro le compte d'échecs admin.
    - L'échec ne ferme pas le WS foyer.
  - `admin_unlock` renvoie `{admin_session, expires_in}`.
    - En WS, chaque message `admin` porte l'`admin_session`.
    - En HTTP, `config` et `get_mcp_info` portent l'`admin_session` dans le corps JSON, jamais en query (haproxy journalise les URL). `admin_pass` n'est plus envoyé à chaque requête : l'installer déverrouille une fois par dialogue.
    - calaos_server ne garde aucune session. La webapp garde la session en mémoire et la renvoie après reconnexion et re-login. Sur `bad_session`, elle redemande le mot de passe admin sans message d'erreur.
  - Une fois l'admin défini, ces actions sont réservées à l'admin :
    - `config get/put` ;
    - `get_mcp_info` ;
    - le message `admin`, sauf `admin {action: update_status}`.
  - `change_cred` (le mot de passe du foyer) reste au niveau foyer, car il exige déjà le mot de passe actuel. L'écran de Calaos mobile continue donc de fonctionner (décision de Raoul, 2026-10-18).
  - Tant que l'admin n'est pas défini (daemon joignable), `admin` répond `admin_not_set` et le reste fonctionne comme aujourd'hui.
  - **État admin dans calaos_server** (module `AdminState`, livré par CB2, consommé par CB1 et CB3), règle « collante » (décision de Raoul, 2026-10-22) :
    - `admin/status` est lu au démarrage, relu toutes les 60 s (tant que l'état est inconnu : toutes les 5 s pendant les 2 premières minutes, puis 60 s ; l'échec n'est journalisé qu'une fois) et à chaque `admin_set` ou `admin_reset` reçu sur `/events` (CB3) ;
    - drapeau persistant `admin_seen_set` dans le répertoire de cache de calaos_server (`Utils::getCachePath()`, fichier `admin_seen_set`). `config put` ne peut pas l'écrire : sa liste blanche se limite aux trois fichiers de config ;
    - **daemon joignable** : sa réponse fait foi et met le drapeau à jour. « Défini » le pose ; `admin_not_set`, ou un 404 d'un ancien daemon, l'efface ;
    - **daemon injoignable ou réponse invalide, drapeau présent** : fermé. `config`, `get_mcp_info` et `admin*` répondent `daemon_unreachable`, le contrôle d'Origin s'applique et ACAO n'est plus reflété ;
    - **daemon injoignable, drapeau absent** (installations sans daemon : Docker, dev, autres distributions ; box qui n'ont jamais eu d'admin) : tout se comporte exactement comme aujourd'hui, et `admin*` répond `admin_unavailable`.
  - Le scope service MCP refuse `admin_unlock`, `admin_set` et `admin` (sans `admin_set`, un client MCP pourrait faire le bootstrap ; décision de Raoul, 2026-10-23), avec mise à jour de `JsonApiServiceScope_test.cpp`. La console n'a pas de login : seul le ticket authentifie.
  - Le jeton MCP n'est **pas** régénéré au bootstrap (cela casserait les clients MCP configurés). L'action `mcp_regenerate` de la table régénère `mcp_token` et redémarre le sidecar ; la vue Admin a un bouton « Régénérer ».
  - Clients du daemon, à écrire :
    - HTTP (CB2) : sur `UrlDownloader`, bearer lu dans `/run/calaos/calaos-ct.token` au démarrage et à chaque 401 ;
    - WebSocket `/events` (CB3) : sur `WebSocketFrame`, reconnexion avec backoff ; un 404 ou un 426 signale un daemon trop ancien, et on réessaie plus tard.
  - URL du daemon codée en dur (`http://127.0.0.1:8000`). Pour les tests, les variables d'environnement `CALAOS_CONTAINER_URL` et `CALAOS_CONTAINER_TOKEN_FILE` la surchargent. Elles ne sont lues qu'au démarrage, et l'URL est refusée si l'hôte n'est pas `127.0.0.1` ou `::1`. Ce n'est pas une option de `local_config.xml` : `config put` permettrait sinon d'envoyer le bearer et le mot de passe admin à un hôte tiers.
  - `LoginThrottle` plafonne les **identités XFF venues du loopback**, par portée (foyer, `admin:`, `service:`). Au plus 16 noms jamais connectés ont leur propre compteur, jusqu'à 300 s sans essai. Les nouveaux noms au-delà, et tout nom qui n'est pas une adresse IP, partagent le compteur `loopback-overflow`. Un nom qui a réussi un login reste hors plafond 7 jours après son dernier succès, en mémoire seulement (perdu au redémarrage). Pas de budget global d'échecs : un seul attaquant bloquerait tout le foyer.
- **Contrat et relais `admin`**
  - Le protocole client de calaos_server est décrit dans la section Administration de `docs/08_http_api.md` (calaos_base), écrite par CB0 avant tout autre ticket calaos_base, webapp ou installer :
    - `admin_unlock {admin_pass}` → `{admin_session, expires_in}` ;
    - `admin_set {new_pass, admin_session?}`, sans session pour le bootstrap, avec session pour le changement (et le code en phase 2) ;
    - `admin {admin_session, action, params}` ;
    - `admin_event {type, ts, data}` ;
    - erreurs `{success:"false", error:<code du vocabulaire>, msg:<texte lisible>}`, en WS comme en HTTP.

    Le mock de la webapp et le faux daemon en dérivent.
  - Le contrat du daemon est la section admin de `docs/API.md` de calaos-container, écrite dans CC-040. Les tests calaos_base et le mock de la webapp en dérivent.
  - Il fixe les règles de session : 32 octets, 15 min absolues, invalidation par `set`, `reset` et 5 échecs de code push (la session invalidée répond `bad_session`).
  - Table fermée : chaque valeur d'enum donne une méthode et un chemin codés en dur. Aucun chemin ne vient du client.
    - `update_check`, `update_available`, `update_upgrade_all`, `update_upgrade(package)`, `update_status` ;
    - `system_reboot`, `system_restart` (redémarre le kiosque calaos-home), `system_info` ;
    - `mcp_regenerate`, traitée dans calaos_server ;
    - en phase 2, `approver_candidates`, `approver_list`, `approver_enroll`, `approver_confirm`, `approver_remove`, `code_request`, `code_verify`, `console_open`.
  - `package` validé par la règle de `docs/API.md` de calaos-container : regex `^[A-Za-z0-9.+-]+$`, premier caractère alphanumérique, pas de `..`, 128 caractères au plus.
  - `install/*`, `rollback_snapshot` et `network/*` ne sont jamais relayés (phase 3 : `network/state`, `apply`, `confirm` et `revert` le sont, amendement (b)).
  - `admin {action: update_status}` est accepté sans `admin_session` une fois l'admin défini, sous `secureTransport()`. `admin_event` est diffusé à toute connexion WS loguée, port 5454 compris : c'est la seule exception à « rien d'admin en clair ».
  - calaos_server est abonné en permanence à `/events` (CB3) et ne retransmet aux clients foyer, sous `admin_event`, que :
    - `upgrade_progress` ;
    - `admin_set` et `admin_reset`, avec l'horodatage seul.
    - En phase 2, `approver_set` et `approver_removed` (levées par AP1a) et `console_opened` (levée par CO1) : mêmes règles de persistance, poussées à tous les téléphones par AP1b/CO2.

    Tout autre type est consommé ou ignoré, jamais retransmis. En phase 3, `network_pending`, `network_applied` et `network_rolled_back` le sont aussi (amendement (b)).
  - Vocabulaire d'erreurs, défini une fois :

    | Code | Cas | Message webapp |
    |---|---|---|
    | `admin_not_set` | admin non défini | proposer le bootstrap |
    | `admin_already_set` | bootstrap concurrent | « Quelqu'un vient de définir le mot de passe admin » et bannière |
    | `bad_password` | mot de passe admin faux | « Mot de passe admin incorrect » |
    | `rate_limited` (429) | limiteur du daemon ou `LoginThrottle`, vérification déjà en cours | « Trop d'essais, réessayez dans une minute » |
    | `bad_session` (401) | session absente, expirée ou invalidée (`set`, `reset`, 5 échecs de code) | nouvelle demande du mot de passe, silencieuse |
    | `password_too_short` | moins de 10 caractères | longueur minimale |
    | `https_required` | transport non sûr | « utilisez https:// » |
    | `daemon_too_old` (404) | ancien daemon en mémoire | « Activation en cours », « Redémarrez la box » en repli |
    | `daemon_unreachable` | daemon injoignable alors que l'admin a déjà été vu défini, erreur de bearer | réessayer |
    | `admin_unavailable` | daemon injoignable et admin jamais vu défini (Docker, dev, autre distribution) | « Administration indisponible (calaos-container injoignable ; disponible seulement sur Calaos OS) », nouvel essai automatique |
    | `upgrade_in_progress` (409) | job en cours | « Une mise à jour est déjà en cours » |
    | `not_allowed` | hors table, scope MCP, bootstrap refusé | message explicite |

    Le daemon refuse tout de suite (429) une vérification concurrente au lieu de la mettre en file, pour que `admin_unlock` réponde toujours en moins de 5 s. Les codes sont portés par le corps (`code`) ; un 401 sans `code = bad_session` est une erreur de bearer, que calaos_server traduit en `daemon_unreachable` après relecture du token.

- **Transport**
  - Invariant : haproxy est le seul terminateur TLS et le seul chemin d'accès admin. Tout accès distant passe par le port 443, que ce soit par le DDNS aujourd'hui ou par un VPN demain (MLSH, par exemple, arrive sur l'IP overlay : `https://<ip-overlay>`).
  - On ne fait jamais confiance à une interface ni à une plage VPN comme transport sûr. Trois raisons :
    - MLSH n'a pas d'ACL : tout nœud du cluster est un pair complet ;
    - un serveur signal compromis peut injecter un pair ;
    - la plage 100.64/10 est aussi celle du CGNAT des opérateurs.
  - Le test tient dans un seul prédicat `secureTransport()` (CB2). Une option `trusted_secure_peers` pourra l'étendre plus tard sans toucher aux appelants.
  - Ces actions ne sont acceptées que si le pair est en loopback **et** que `X-Forwarded-Proto` vaut `https` :
    - `admin_unlock`, `admin_set`, `admin` et `/api/console` ;
    - une fois l'admin défini, `config` et `get_mcp_info`.
  - Ce prédicat n'est pas infalsifiable : un conteneur du netns hôte peut forger XFP. Le remède (socket Unix entre haproxy et calaos_server) est en phase 3.
  - XFP absent : refus (fail closed). Pas de patch de `haproxy.cfg` à l'exécution : pas de `haproxy -c` sur l'hôte, et un patch raté couperait toute la webapp.
  - Les templates (`haproxy_pre` et `calaos_ddns`) gagnent, pour les **nouveaux** fichiers, dans le backend Calaos : `http-request set-header X-Forwarded-Proto %[ssl_fc,iif(https,http)]`. La doc dit qu'un `haproxy.cfg` modifié doit garder `set-header X-Forwarded-Proto` sur chaque frontend.
  - L'installer passe déjà par `https://IP/api.php` pour un hôte nu. Un hôte mémorisé en `http://…:5454` reçoit `https_required` ; CI1 le réécrit en https et réessaie une fois.
  - Sinon, la webapp affiche « utilisez https:// », quel que soit le chemin (LAN, DDNS, VPN).
  - Rien d'admin ne passe en clair sur le port 5454, sauf la diffusion d'`admin_event`.
- **Origin (CB1)**
  - Un Origin n'est accepté que dans trois cas :
    - pair en loopback, `X-Forwarded-Proto` à `https`, et hôte de l'Origin égal à `Host`. Sur ce chemin, TLS empêche le rebinding : le certificat ne correspond pas à l'hôte rebindé et le navigateur abandonne. C'est ce qui fait marcher le DDNS sans configuration ;
    - une IP portée par une interface (`getifaddrs`, le conteneur `--network=host` voit celles de l'hôte, IP overlay comprise). Elle sert la webapp foyer en `http://IP:5454` sur le LAN ; un nom rebindé n'est pas une IP de la box, donc le rebinding reste refusé ;
    - un hôte de `api_allowed_hosts` (dev, noms VPN, DNS maison, `.local`).
  - Pas de hostname ni de `.local` automatiques.
  - Hors du premier cas, comparer à `Host` ne suffit pas : le DNS rebinding passe, comme l'a montré le PoC.
  - Le contrôle porte sur le handshake WS **et** sur les requêtes HTTP `/api.php`. `Access-Control-Allow-Origin` n'est renvoyé que pour un Origin accepté (`HttpClient.cpp:337` reflète tout aujourd'hui).
  - `Origin: null` est refusé.
  - Casse, point final et port par défaut sont normalisés, et le port n'est comparé que si `Host` en contient un.
  - L'absence d'Origin est acceptée (clients natifs).
  - Tant que l'admin n'est pas défini (selon `AdminState`), rien ne change (décision de Raoul, 2026-10-22) : tout Origin est accepté et ACAO reflète comme aujourd'hui. Les Origins qui seraient refusés sont journalisés, pour qu'on puisse les ajouter à `api_allowed_hosts` avant le bootstrap. Le contrôle s'applique dès que l'admin est défini, et aussi quand le daemon est injoignable alors que le drapeau `admin_seen_set` est présent.
  - En dev, le proxy Vite réécrit l'Origin vers la cible (WA2) : rien à configurer sur la box.

### Montée de version sans rupture
Exigence de Raoul : une installation existante monte vers les versions admin par les outils actuels (`calaos-os upgrade`, `apt upgrade`, bouton du kiosque), sans étape manuelle, sans SSH et sans reboot. Chaque ticket concerné porte ces garanties.
- **Compatibilité entre versions**, pendant et après la montée :
  - ancien kiosque et ancien CLI `calaos-os` avec le nouveau daemon ;
  - ancien calaos_server avec le nouveau daemon ;
  - nouveau calaos_server avec l'ancien daemon encore en mémoire.

  Pour cela, les routes et les formes de réponse actuelles du daemon restent servies telles quelles (upgrade synchrone compris) pendant au moins une release. L'enveloppe CC-040, le job 202 de CC3 et les routes admin vivent sous un préfixe versionné choisi dans CC-040 (par exemple `/api/v2/`). CH1b n'est pas un prérequis de la montée : il doit seulement être livré avant le retrait des routes legacy.
- **Token (CC2)** : même chemin, `/run/calaos/calaos-ct.token`. Le fichier n'est écrit que s'il est absent, donc au boot (`/run` est un tmpfs) : un calaos_server ou un kiosque déjà lancé garde un token valide, et le nouveau format prend effet au boot suivant ; l'ancien token faible reste donc valable jusque-là (décision de Raoul, 2026-10-22).
  - Création atomique, car dix units lancent `init_calaosfs` en parallèle : `umask 077`, écriture dans un fichier temporaire du même répertoire, puis `ln` (qui échoue si le token existe), puis suppression du temporaire.
  - `?token=` est retiré si CH1a constate qu'aucun kiosque publié ne s'en sert. Sinon il est gardé sur `/events` seulement, et retiré par un ticket CC2b une release après CH1b (décision de Raoul, 2026-10-22).
- **Activation du nouveau daemon, sans reboot.** `--no-stop-on-upgrade` reste : un redémarrage immédiat tuerait le daemon s'il exécute lui-même le run apt (CLI, kiosque, webapp). La postinst lance à la place un redémarrage différé (unit transitoire `systemd-run`) qui :
  - attend la libération du verrou dpkg, puis la fin du job en cours : `GET /api/update/status` renvoie l'état repos (`""` ou `"idle"`, voir Daemons 1.5.0 à 1.7.0) (en `1.7.0` comme sur `origin/main`) ;
  - réessaie toutes les 5 s si le daemon est injoignable, et abandonne en le journalisant au bout de 2 h ;
  - ne redémarre que si le processus en mémoire tourne encore sur l'ancien binaire (`/proc/<pid>/exe` marqué supprimé).

  Ce chemin couvre les trois outils, y compris quand c'est l'ancien daemon qui exécute la montée. Un nouveau daemon sort de lui-même avec le code 0 après un job qui l'a mis à jour, et le redémarrage différé n'a alors plus rien à faire.
- **Job asynchrone (CC3)** : l'upgrade versionné répond 202 et crée un job. Le dernier résultat est persisté, `reboot_required` est calculé sans se fier aux snapshots. Le nouveau CLI suit le job par `/events`. La sortie 0 n'a lieu qu'après la réponse au POST legacy et la publication de l'événement terminal. `Restart=always` relance alors le nouveau binaire, et les sessions survivent dans `/run/calaos-container/`.
- **Unit (CC-042)** : `ExecStartPre=-/usr/bin/dpkg --configure -a` (avec le `-` : un dpkg cassé n'empêche pas le daemon de démarrer), `KillMode=process`, `TimeoutStopSec`, `Restart=always` conservé.
  - `TimeoutStartSec=30min` : reprendre la configuration de calaos-server tire l'image et dépasserait les 90 s par défaut, et systemd tuerait dpkg à chaque essai.
  - La postinst ne bloque jamais sur le démarrage de calaos-container : `--no-block`, ou rien si `$CALAOS_CT_STARTPRE` est posé par l'ExecStartPre. Sinon une reprise lancée depuis l'ExecStartPre attendrait le démarrage de sa propre unit.
  - Un run interrompu est repris au démarrage suivant du daemon ; si le verrou est encore tenu, la commande échoue sans effet. Le 409 sur `system/reboot` évite de couper dpkg.
- **Ordre des paquets** dans un même run : indifférent, puisque toutes les combinaisons de versions fonctionnent. `calaos-server` dépend de `calaos-container (>= <version admin>)` pour que les deux montent ensemble. Pas de `Breaks`, sauf si la recette en montre le besoin.
- **calaos-server** est redémarré par son paquet pendant la montée : la webapp se reconnecte, se relogue, renvoie `update_status` et garde sa session.
- **Daemons 1.5.0 à 1.7.0** : `GET /api/update/status` renvoie `status: ""` à l'état repos, pas `"idle"`. Les clients acceptent les deux (décision de Raoul, 2026-10-23).
- **Défauts inchangés** : sans admin défini, tout se comporte comme aujourd'hui (Origin et ACAO compris, voir CB1). Aucune modification de `haproxy.cfg` sur une installation existante. Les installations sans daemon (Docker, dev, autres distributions) gardent aussi le comportement actuel. Seule exception : la fenêtre où un admin est défini alors que calaos_server n'a encore jamais joint le daemon (drapeau absent). Si le daemon est injoignable pendant cette fenêtre, la box se comporte comme sans admin (voir les risques résiduels). Hors de cette fenêtre, le repli fermé (`config`, `get_mcp_info`, `admin*` en `daemon_unreachable`, contrôle d'Origin, ACAO non reflété) ne touche que les box où l'admin a déjà été vu défini.
- **Versions mélangées**
  - Webapp neuve, calaos_server ancien : `admin_unlock` reste sans réponse. WA1 abandonne au bout de 5 s : « calaos-server trop ancien, mettez à jour la box ». Le 429 immédiat du daemon garantit qu'une vérification légitime répond avant ce délai.
  - Ancien daemon encore en mémoire (`daemon_too_old`) : « Activation de l'administration en cours », avec nouvel essai automatique. « Redémarrez la box » n'apparaît qu'en repli, si l'état persiste au-delà de 2 min.
  - Ancien installer, serveur neuf : le refus de `config get` est affiché tel quel, donc la chaîne d'erreur doit être lisible.
  - CB3 n'est jamais livré avant CC3 : un POST synchrone derrière le relais bloquerait plusieurs minutes.

### Bootstrap et récupération
- **Bootstrap** : avec le mot de passe partagé, on peut définir l'admin une seule fois (`admin_set` sans session). Refusé si les identifiants effectifs (après le repli de `checkCredentials`) sont vides, valent `user/pass`, ou viennent des clés legacy `calaos_user/calaos_password`. Une entrée datée de `docs/refactoring/DECISIONS.md` remplace « Pas de forçage des identifiants par défaut » sur ce point. L'écran de bootstrap affiche la liste `api_allowed_hosts` en vigueur. Deux alertes, persistées par le daemon jusqu'à l'ack :
  - un push « Mot de passe admin défini » envoyé à tous les tokens ;
  - un événement persistant (`EventPushNotification`), affiché à chaque client au login suivant.

  Il n'y a aucune condition d'IP.
- **Changer le mot de passe admin** : en phase 1, `admin_set` avec une session admin valide. Dès qu'un approbateur existe (phase 2), il faut en plus le code push.
- **Récupération** : `calaos-os admin set` en SSH, sur le LAN ou via le VPN, mot de passe lu sur stdin (mode récupération). Aucune fenêtre de bootstrap ne s'ouvre.
- **Retour à « non défini »** : `calaos-os admin reset`, cas rare.
  - La commande efface le hash, les approbateurs, les sessions, et tue les consoles ouvertes.
  - Elle envoie un push et affiche une bannière. On repasse ensuite par le bootstrap, après avoir changé le mot de passe foyer : sinon un détenteur de l'ancien peut redéfinir l'admin le premier.
- Pas de reset par clé USB : décision de Raoul, jugé compliqué pour rien.

### Second facteur : code par push vers un téléphone approbateur (phase 2)
- **Enrôlement**
  - Un admin déverrouillé choisit son téléphone parmi les tokens `register_push`, qui gagne un nom d'appareil (`device_name`, stocké dans `push_tokens`).
  - Depuis iOS 16, sans droit spécial, tout iPhone se nomme « iPhone » (décision de Raoul, 2026-10-24) : Calaos mobile propose le réglage « Nom de cet appareil » (AP2b). `approver_candidates` renvoie aussi la plateforme, la date d'enregistrement et `last_seen`, que WA3 affiche.
  - Le daemon envoie un code **à ce seul token**, et l'utilisateur le tape dans la webapp : c'est la preuve qu'il possède le téléphone.
  - Le daemon stocke le token comme approbateur. Pas de secret d'appareil qui authentifie le téléphone : rien n'est remis au téléphone par la box. Depuis la phase 3, le téléphone a sa propre clé push, qui ne sert qu'à lui cacher le code (amendement (a)).
  - Les routes daemon de l'enrôlement et du ticket console sont nommées dans `docs/API.md` par AP1a et CO1.
- **Nombre d'approbateurs** : un seul suffit (décision de Raoul). Un second est possible en option.
- **Retrait** : le dernier approbateur ne se retire pas (`approver_last`). Pour changer de téléphone, on enrôle le nouveau puis on retire l'ancien ; un téléphone perdu se règle par `admin reset` en SSH.
- **Rotation du token push** (réinstallation, restauration, purge à 30 jours) : elle oblige à se ré-enrôler. Le changement d'approbateur exige un code ; si l'ancien token est mort, on passe par le second approbateur s'il existe, sinon par `calaos-os admin reset` puis `admin set` en SSH (décision de Raoul, 2026-10-22). En attendant, les mises à jour et le reboot restent possibles avec le seul mot de passe admin : seuls la console, le réseau et le changement d'approbateur sont bloqués.
- **Code**
  - 6 chiffres, valable 60 s, à usage unique.
  - Lié à la session daemon, à l'action et au hash des paramètres.
  - Invalidé après 5 essais, qui invalident aussi la session daemon : il faut redonner le mot de passe admin. Un seul code en attente par session, au moins 10 s entre deux générations, environ 10 par heure au total.
  - Comparaison en temps constant.
- **Envoi**
  - Le daemon publie `push_code {approver_id, code_id}` sur `/events`, sans token ni code.
  - calaos_server récupère `{token, message}` (en phase 3 `{token, message, action, code, expires_in}`) par une route du daemon protégée par le bearer (`GET admin/push/<code_id>`), puis envoie par `NotifManager` : un seul client push.
  - Le token de l'approbateur ne sort jamais du daemon et de calaos_server, et `push_code` n'est jamais retransmis aux clients.
- **Le téléphone approbateur n'a besoin que du push sortant** : il n'a pas à joindre la box. Seul l'enrôlement l'exige, sur le LAN ou via le VPN.
- **Protège** : chaque ouverture de la console, le changement du mot de passe admin, le changement d'approbateur, la config réseau (phase 3).
- **Sans approbateur enrôlé**, ces actions sont indisponibles. Il reste la console SSH.

### Console web (phase 2)
- **Principe**
  - Shell root sur l'hôte, servi par le paquet Debian **ttyd** (décision de Raoul, 2026-10-24) : le terminal n'est pas réimplémenté. Le daemon lance un ttyd par console, sur une socket Unix (`--once`, `--max-clients 1`). `ttyd` rejoint les dépendances du paquet calaos-container.
  - Le daemon garde la barrière (code push, ticket à usage unique lié à la session), les limites et l'audit.
  - Endpoint dédié `/api/console` : un tunnel brut dans calaos_server, sur le modèle de `McpProxyHandler`, qui injecte le bearer du daemon dans la première requête puis relaie vers la socket de ttyd.
  - Ce chemin est détecté avant le parseur HTTP et échappe à `checkHandshakeRequest` : le tunnel applique lui-même `secureTransport()` et le contrôle d'Origin sur les en-têtes détectés.
  - Côté webapp, `@xterm/xterm` (thème Calaos) est chargé seulement sur la route console et parle le protocole WS de ttyd.
- **Ouverture**
  - Admin déverrouillé, puis un code push **par ouverture**, qui délivre un ticket à usage unique de 30 s. Il n'y a pas de flag « console activée » : c'est le code qui active la console.
  - Le ticket est envoyé dans la **première trame** WS, jamais dans l'URL (haproxy journalise les URL). Rien n'atteint ttyd ni le shell avant validation.
- **Limites**
  - Le ttyd et son shell sont tués après 15 min d'inactivité, au bout d'1 h, à l'invalidation de la session ou au reset. CO2 signale l'inactivité au daemon ; les pings ne la réarment pas.
  - 2 consoles au maximum.
- **Traçabilité**
  - Alerte persistée `console_opened` levée par CO1 à chaque ouverture ; AP1b/CO2 la poussent à tous les téléphones.
  - Audit dans `audit.log` et dans journald. Derrière un agent qui redirige vers loopback, l'identité se réduit à 127.0.0.1 : c'est la même limite que pour le MCP.
  - Le tunnel brut ne passe pas par la classe `WebSocket`, qui pinge toutes les 15 s : le côté daemon pingue donc lui-même le tunnel (toutes les 30 s), et le ping propre de ttyd (`-P`) reste à sa valeur par défaut. Les pings ne réarment jamais le délai d'inactivité : seule la saisie console du client compte.

## Parcours utilisateur
- **Installation existante (3 étapes).** La première montée vers les versions admin se fait avec l'outil habituel (`calaos-os upgrade`, `apt upgrade` ou le bouton du kiosque), sans reboot ni étape de plus : les nouvelles versions sont actives à la fin du run. Tant qu'on ne définit pas l'admin, rien ne change. Ensuite : onglet Admin, puis « Définir le mot de passe admin » (un push part), puis « Mettre à jour ». Par la suite : Admin, mot de passe, Mettre à jour.
- **Installation neuve (4 étapes).** Login `user/pass`, changement du mot de passe foyer, définition du mot de passe admin, mise à jour.
- **Ouvrir la console (phase 2).** Admin, puis Console, puis saisie du code reçu sur le téléphone, et le terminal s'ouvre.
- **Mot de passe admin perdu.** `ssh root@<ip>` puis `calaos-os admin set`. Rien à faire dans la webapp.

## Tickets
Branche d'intégration : les tickets calaos-container sont fusionnés dans `admin/p6`, qui ne publie pas. Elle est fusionnée une seule fois dans main, après la recette RC1 (décision de Raoul, 2026-10-23).

Contrats de référence : `docs/API.md` de calaos-container pour les routes du daemon, `docs/08_http_api.md` de calaos_base (section Administration) pour les messages de calaos_server.

### Prérequis calaos-container (backlog existant)

| Repo | Ticket | Contenu | Durée |
|---|---|---|---|
| calaos-container | Board | ménage du board : P0 à P3 en ✅ d'après `git log origin/main` (CC-001 à CC-035, CC-034 fermé) ; CC-050 et CC-055 notés « absorbés par CC3 », CC-052 « absorbé par CC2 » ; `CC-040-*.md` amendé (préfixe versionné, routes legacy inchangées, sanitisation et 409 seulement sous le préfixe, section admin de `API.md`) ; `CC-042-*.md` amendé (`Restart=always` gardé, `ExecStartPre=-dpkg --configure -a`, `KillMode=process`, `TimeoutStopSec`, `TimeoutStartSec`, propriété de `debian/calaos-container.postinst`) ; nouvelle phase « P6 — Admin » : CC2 (deps CC-040, CC-042), CC1 (deps CC2), CC3 (deps CC1) | 0,25 j |
| calaos-container | CC-040 | contrat d'API unifié et CLI en phase, sous un préfixe versionné ; routes et formes legacy servies telles quelles au moins une release ; `docs/API.md` reçoit la section admin (table de relais, trois modes d'`admin/set`, vocabulaire d'erreurs, règles de session, `X-Calaos-Client-IP`, forme du job 202, noms d'événements), dans un premier commit fusionné seul | 3 j |
| calaos-container | CC-041 | build et release (flag prerelease, usrmerge, une seule source de version), canal dev/experimental utilisé par RC1 ; se ferme sur son propre test de montée dev.x vers dev.y ; en parallèle, fichiers disjoints | 1 j |
| calaos-container | CC-042 ⇐ CC4 | unit systemd : `dpkg --configure -a`, `KillMode=process`, `TimeoutStopSec`, `TimeoutStartSec=30min` ; `Restart=always` conservé, le `Restart=on-failure` du ticket est abandonné ; postinst (nouveau fichier) avec redémarrage différé (verrou dpkg libre, job fini, binaire périmé) et jamais bloquante sur le démarrage du daemon ; en parallèle de CC-040, fichiers disjoints | 1 j |

### Phase 1 (≈ 26 j, plus 0,1 j si CC2b ; chemin critique ≈ 13,5 j) : mises à jour et reboot depuis la webapp

| Repo | Ticket | Contenu | Durée |
|---|---|---|---|
| calaos-build | OS1 | retirer le stub `policy-rc.d` de Docker des images SD (`create_sdimg.sh`), et le retirer une fois sur les box SD existantes par une postinst. Raison : le stub bloque le redémarrage des services après une montée (décision de Raoul, 2026-10-23) | 0,5 j |
| calaos_home | CH1a | constater dans calaos_home (`github.com/calaos/calaos_home`), toutes versions publiées, l'usage de `?token=`, des routes legacy et de `/events`, et si les types d'événements inconnus sont ignorés ; fixe l'estimation de CH1b | 0,1 j |
| calaos-container | CC2 ⇐ CC-052 | token 32 octets urandom en 0600 dans `init_calaosfs`, même chemin, écrit seulement s'il est absent, création atomique ; `?token=` retiré ou gardé sur `/events` seul selon CH1a ; tests dans `app/context_test.go` ; remplace au passage `mountpoint -q "$fs"` par `[ -d "$fs" ]`, qui empêche aujourd'hui la création de `/etc/ola`. Depends on: CC-040, CC-042, CH1a | 0,5 j |
| calaos-container | CC2b | conditionnel (si CH1a trouve un usage de `?token=`) : retrait de `?token=` sur `/events`, une release après CH1b. Depends on: CH1b | 0,1 j |
| calaos-container | CC1 | mot de passe admin : répertoire 0700, argon2id, routes, trois modes d'`admin/set`, sessions dans `/run/calaos-container/`, limiteur sur les échecs et 429 immédiat, longueur minimale, `O_EXCL`, alertes persistées et ack, CLI `set/reset` sur stdin, audit. Depends on: CC-040, CC2 | 1,75 j |
| calaos-container | CC3 ⇐ CC-050, CC-055 | upgrade asynchrone versionné (202 + job), upgrade synchrone legacy conservé, résultat persisté, `reboot_required`, sortie 0 après auto-mise à jour (après la réponse legacy et l'événement terminal), 409 sur reboot pendant un job, middleware des sessions, CLI sur 202 et `/events`, tests. Depends on: CC1 | 3,25 j |
| calaos_home | CH1b | client de mise à jour : enveloppe CC-040, job asynchrone, `/events`, bearer en en-tête. Estimation fixée par CH1a. Pas un prérequis de la montée, à livrer avant le retrait des routes legacy | 0,75 j |
| pkgdebs, calaos_ddns | PK1 | `calaos-server` : `Depends: calaos-container (>= <version admin>)`, pas de `Breaks` ; ligne XFP du backend dans les deux templates, nouveaux fichiers seulement | 0,2 j |
| calaos_base | CB0 | section Administration de `docs/08_http_api.md` : messages, enveloppe, `admin_set` (bootstrap et changement), `admin_event`, format d'erreur ; dérivé de la section admin de `docs/API.md` | 0,25 j |
| calaos_base | CB1 | Origin sur WS et HTTP : règle haproxy-https, IP d'interfaces, `api_allowed_hosts`, `null` refusé, normalisation, ACAO conditionnel (`HttpClient.cpp:337` et `WebSocket.cpp:401`) ; sans admin défini, journalisation seule. Depends on: CB2 (`AdminState`, `secureTransport()`) | 0,75 j |
| calaos_base | CB2 | client HTTP du daemon (`UrlDownloader`, bearer relu au démarrage et à chaque 401), module `AdminState` (règle collante, drapeau `admin_seen_set`, 404 et injoignable, `admin_unavailable`), `admin_unlock` et `admin_set` en WS et HTTP, `admin_session`, répartition et gating du message `admin`, `admin_not_set`, `secureTransport()`, condition de bootstrap et entrée `DECISIONS.md`, refus pour le scope MCP, chaînes d'erreur lisibles, faux daemon de test (`CALAOS_CONTAINER_URL`, loopback seulement) ; les tests d'`AdminState` utilisent un répertoire de cache temporaire (`--cache`) et ne touchent jamais `~/.cache/calaos`. Depends on: CB0 | 3 j |
| calaos_base | CB3 | client WebSocket `/events` (sur `WebSocketFrame`, reconnexion avec backoff, 404/426 ⇒ daemon trop ancien), relais en table fermée, filtre des événements, ack des alertes vers push et `EventPushNotification`, rafraîchissement d'`AdminState`, `mcp_regenerate`. Depends on: CB2, CC3 | 3 j |
| calaos_base | CB4 | plafond des nouvelles identités XFF venues du loopback dans `LoginThrottle`. Après CB2 | 0,5 j |
| calaos-web-app | WA1 | vue Admin : définir / déverrouiller, changer le mot de passe admin (`admin_set` avec session), mises à jour, progression, reprise après redémarrage serveur, reboot, « utilisez https:// », écran du mot de passe foyer, liste `api_allowed_hosts` affichée au bootstrap, délai de 5 s sur `admin_unlock`, « Activation en cours » avec nouvel essai puis repli sur « Redémarrez la box », nouvel essai automatique sur `admin_unavailable`, messages d'erreur, bouton « Régénérer » du jeton MCP, scénarios du mock. Depends on: CB0 | 3,5 j |
| calaos-web-app | WA2 | réécriture de l'Origin par le proxy de dev | 0,1 j |
| calaos_installer | CI1 | champ mot de passe admin optionnel, `admin_unlock` une fois par dialogue, `admin_session` dans `config`, réécriture https et une retentative sur `https_required`, refus affiché. Depends on: CB0 | 0,5 j |
| doc-website | DOC1 | pages update et services : la montée se fait avec l'outil habituel, la webapp remplace ensuite SSH ; récupération par `admin set` ; `reset` après changement du mot de passe foyer ; rollback ; `api_allowed_hosts` avant le bootstrap ; `haproxy.cfg` modifié ; Docker et autres distributions : pas d'administration web | 0,5 j |
| box | RC1 | recette en VM depuis l'image stable 4.5.0, paquets du canal dev/experimental publié par CC-041 (la préparation de la VM peut passer par SSH) : `calaos-os upgrade`, `apt upgrade`, bouton du kiosque ; les deux ordres d'activation ; runs interrompus ; sessions après redémarrage du daemon ; puis dev.x vers dev.y depuis la webapp, qui refait le test de CC-041 sur la VM | 1,75 j |

Propriété des fichiers calaos_base :
- CB2 : `JsonApiHandlerWS.cpp`, `JsonApiHandlerHttp.cpp`, nouveaux `AdminState` et `DaemonClient`, `docs/refactoring/DECISIONS.md` ;
- CB1 : `WebSocket.cpp` (`checkHandshakeRequest`), `HttpClient.cpp` (`processHeaders`), nouveau `OriginPolicy` ;
- CB4 : `LoginThrottle` dans `JsonApi.{h,cpp}` et `JsonApiHardening_test.cpp` ;
- CB3 : la table du message `admin`, dont CB2 ne livre que la répartition et le gating.

Toute nouvelle option est déclarée dans `src/lib/ConfigOptions.cpp`, et `docs/16_config_options.md` est régénéré (`calaos_config options --markdown`).

### Ordre d'exécution

| Étape | Tickets | Remarque |
|---|---|---|
| 0 | Board ∥ CH1a | CH1a décide du sort de `?token=` avant CC2 |
| 1 | CC-040 ∥ CC-041 ∥ CC-042 | rien d'autre dans `app/` et `cmd/` pendant CC-040 ; CC-041 et CC-042 sur des fichiers disjoints |
| 1 (parallèle) | OS1 | en parallèle de l'étape « dès 1a » |
| 1a | section admin de `docs/API.md` (premier commit de CC-040, fusionné seul), puis CB0 | |
| dès 1a | CB2, WA1, WA2, CI1 | le contrat suffit : faux daemon et mock |
| après CB2 | CB1 ∥ CB4 | `AdminState`, `secureTransport()`, `LoginThrottle` |
| 2 | CC2 | après CC-040 et CC-042 |
| 3 | CC1 | |
| 4 | CC3 | |
| après l'étape 4 | CB3, PK1, DOC1, CH1b | CB3 jamais livré avant CC3 ; CH1b avant le retrait des routes legacy |
| release suivante | CC2b, si CH1a l'exige | une release après CH1b, hors chemin critique |
| fin | RC1 | |

Chemin critique ≈ 13,5 j : Board, CC-040, CC2, CC1, CC3, CB3, RC1. CB2, CB1, CB4, WA1 et CI1 ont plusieurs jours de marge.

### Phase 2 (≈ 9,75 j) : console web et téléphone approbateur

| Repo | Ticket | Contenu | Durée |
|---|---|---|---|
| calaos-container | AP1a | approbateurs et codes push : routes d'enrôlement, codes, limites, liaison à l'action, `push_code {approver_id, code_id}`, route bearer de récupération, invalidation de la session après 5 échecs de code | 1,75 j |
| calaos_base | AP1b | consommation de `push_code` dans le client `/events` de CB3, `device_name` dans `register_push` et `push_tokens`, valeurs `approver_candidates`, `approver_list`, `approver_enroll` (sélection par `token_id`), `approver_confirm`, `approver_remove`, `code_request`, `code_verify`, `console_open` de la table, champs `code_id`/`code` d'`admin_set`, alertes `approver_set`/`approver_removed`, plateforme, date d'enregistrement et `last_seen` des approbateurs et des candidats | 1,25 j |
| calaos_mobile | AP2 | déjà fusionné : `device_name` dans `register_push` ; `register_push` en mode HTTP envoyé avec les identifiants (aujourd'hui un échec de login par session) | 0,5 j |
| calaos_mobile | AP2b | réglage dans l'app « Nom de cet appareil » (iOS 16 renvoie « iPhone »), avec renvoi de `register_push` au changement | 0,5 j |
| calaos-container | CO1 | console via ttyd (socket Unix par console, `--once`, `--max-clients 1`), dépendance `ttyd`, route du ticket, limites, kill, ping, alerte `console_opened`, audit sur stdout | 1 j |
| calaos_base | CO2 | tunnel `/api/console` vers la socket ttyd, `secureTransport()` et Origin sur les en-têtes détectés, bearer injecté, inactivité signalée au daemon, push `console_opened` | 1,25 j |
| calaos-web-app | CO3 | vue console : xterm.js au thème Calaos, protocole WS de ttyd | 1 j |
| calaos-web-app | WA3 | enrôlement de l'approbateur, saisie du code, affichage plateforme / date d'enregistrement / `last_seen` des candidats | 1 j |
| calaos_mobile | CM2 | épinglage TOFU du certificat à la place de `ignoreSslErrors`, par hôte (IP LAN, nom DDNS, IP overlay), avec nouvelle confirmation en cas de changement de clé (renouvellement Let's Encrypt) | 1 j |
| doc-website | DOC2 | console, approbateur, rotation | 0,5 j |

Ordre phase 2 : AP1a ∥ AP2, puis AP2b, puis AP1b, puis WA3 ; CO1 (après AP1a), puis CO2, puis CO3 ; CM2 indépendant ; DOC2 à la fin. La phase 2 démarre après RC1. `admin/p7` n'atteint main/master qu'avec AP1a, AP1b, CO1 et WA3 (décision de Raoul, 2026-10-24) : un calaos_server plus ancien renvoie `internal` au changement de mot de passe dès qu'un code est exigé.

### Phase 3 : diagnostics et confort
Périmètre approuvé par Raoul le 2026-10-25, après une revue de sécurité dont toutes les recommandations sont retenues. Le design et ses contrats sont dans `docs/08_http_api.md` (calaos_server) et `docs/API.md` de calaos-container (daemon), branche d'intégration `admin/p8` dans calaos-container, calaos_base et calaos-web-app.

**Dans le périmètre**
- **P3-CODE** : le code push n'est plus dans le texte de la notification. Il est chiffré avec la clé push du téléphone, qui le déchiffre et l'affiche dans l'app (code, action, compte à rebours, copie). Le téléphone n'a toujours pas à joindre la box (amendement (a)).
- **P3-SVC** : état de quinze unités et redémarrage de onze d'entre elles sur liste fermée ; `calaos-container`, `haproxy`, `systemd-networkd` et `ssh` en lecture seule. Session admin seulement.
- **P3-LOGS**, avec l'heure de la box : logs du journal systemd bornés, filtrés par source, priorité, période et texte, avec masquage au mieux des secrets et suivi par interrogation d'un curseur toutes les 2 s ; lecture et réglage de l'heure et du fuseau, `systemd-timesyncd` (amendement (e)).
- **P3-CFG** : liste blanche d'options de `local_config.xml` modifiables depuis la webapp (`config_list`, `config_set`), et téléchargement de la sauvegarde avec les secrets (`config_backup`, amendements (c) et (d)).
- **P3-NET** : configuration réseau avec « appliquer puis confirmer » et retour automatique sous 120 s (amendement (b)) ; le ticket CC-051 (tests de validation réseau) est absorbé par le travail du daemon.
- **CM1** : mises à jour, progression, reboot et informations système depuis Calaos mobile (amendement (f)).
- Compléments décidés avec la revue : **P3-NET-CLI** (`calaos-os network state|apply|confirm|revert`, pour tester et réparer par SSH), **P3-RC** (recette en VM), **P3-DOC** (doc-website).

**Hors périmètre, reporté**
- Socket Unix entre haproxy et calaos_server : le TCP 5454 reste cru pour XFF et XFP.
- `auth_token` zigbee2mqtt et mot de passe mosquitto générés par les scripts `_pre`.
- CC5 : apt dans une unit transitoire, seulement si CC-042 se révèle insuffisant à la recette.
- Digests `@sha256` et JSON de releases signé (minisign) : P3-SIGN.
- Inventaire des devices utilisés, pour pouvoir sortir calaos-server de `--privileged` : P3-PRIV.
- Wi-Fi (la box n'a pas de pile Wi-Fi).
- CC-053 et CC-054 : quand on veut, sans lien avec l'admin.

**Ordre**
- Étape 1 : contrat du daemon (`docs/API.md`), puis contrat de calaos_server et ces amendements.
- Étape 2, en parallèle selon les dépôts : daemon (code, puis réseau et services en fichiers disjoints, puis logs et heure), calaos_base (code push, relais), webapp (socle), mobile (CM1 d'abord, puis le code push, qui a besoin des vecteurs de test de `docs/08_http_api.md`).
- Étape 3 : configuration côté serveur, CLI réseau, vues de la webapp (services, logs et heure, configuration, réseau, indicateur `secure`).
- Étape 4 : recette en VM ; étape 5 : doc-website, puis `admin/p8` rejoint main/master d'un bloc (une webapp de phase 3 a besoin des lignes de relais de phase 3 ; un calaos_server de phase 3 tolère un daemon plus ancien, `daemon_too_old` par action).
- Estimation : chaîne daemon 6 à 7 j, calaos_server environ 4 j, webapp environ 5 j, mobile environ 3 j, contrats 1 j, recette 1 j ; chemin critique 9 à 10 j.

### Amendements du 2026-10-25 (phase 3)
Décisions de Raoul du 2026-10-25, issues de la revue de sécurité de la phase 3. Chaque amendement corrige le texte plus haut qu'il nomme.

**(a) La clé d'appareil remplace « pas de secret d'appareil ».** Ce que le plan refusait (« Ce qu'on ne fait pas », enrôlement), c'était d'*authentifier* le téléphone par un secret (ECDSA, signatures, rotation). Cela reste refusé. En revanche, chaque téléphone a une **clé push** : 32 octets tirés par l'app, envoyés à chaque `register_push` (`push_key`), stockés dans `push_tokens`, **gelés une fois posés tant que le téléphone est approbateur** (même règle que son nom et son format) ; la première clé d'un approbateur de la phase 2, qui n'en a pas, est acceptée (il ne peut pas se ré-enrôler avec le même jeton), et une clé perdue se résout par un jeton neuf, un second approbateur ou `calaos-os admin reset`. Elle sert uniquement à chiffrer le code (HMAC-SHA256, construction fixée dans `docs/08_http_api.md`, avec des vecteurs de test) pour qu'il ne figure ni dans le texte de la notification, ni chez le relais push, FCM ou APNs, ni chez qui ne détient que le jeton. Le second facteur est inchangé : le code tapé dans la webapp. Le principe de « Envoi » est conservé : le téléphone n'a pas à joindre la box, le code arrive chiffré dans le push. Les apps sans clé (stores) reçoivent le texte actuel pendant une release, puis il est retiré.

**(b) Le réseau est relayé.** `network/state`, `network/apply` (protégé par un code push de l'action `network_config`), `network/confirm` et `network/revert` sont relayés par calaos_server ; `network/list`, `network/trial` (alias du CLI) et l'ancienne route `network/{intf}` restent réservées au bearer et ne sont jamais relayées. Le plan disait « sous 60 s » : la fenêtre « appliquer puis confirmer » est de **120 s** (changer d'adresse, accepter de nouveau l'avertissement du certificat auto-signé, se reconnecter et déverrouiller prend en pratique plus d'une minute). Le changement s'applique en essai dans `/run/systemd/network` et n'entre dans `/etc` qu'à la confirmation : un redémarrage ou une coupure de courant revient donc en arrière par construction. N'importe quelle session admin vivante peut confirmer, depuis la nouvelle adresse. Un retour arrière lève l'alerte persistée `network_rolled_back`, sans push vers les téléphones.

**(c) La liste blanche n'est pas une frontière ; risque résiduel du `config put`.** La liste blanche de P3-CFG est un confort et un garde-fou, pas une limite de sécurité : le `config put` HTTP existant, protégé par la session admin, réécrit tout `local_config.xml` (`listen_address`, `port_api`, `cn_pass`, `mcp_token`, `api_allowed_hosts`, entre autres). Le plan ne prétend donc pas qu'une session admin ne peut pas déplacer calaos_server hors de haproxy ou l'ouvrir. Durcir `config put` par une liste d'interdits casserait l'éditeur de configuration de l'installer : non retenu.

**(d) La sauvegarde contient les secrets.** Le fichier de `config_backup` (JSON, `io.xml`, `rules.xml`, `local_config.xml`, `widgets.xml`) porte les secrets que `config get` donne déjà à l'admin : `cn_pass`, `mcp_token`, mots de passe de caméras. C'est aussi ce dont une restauration a besoin. Il n'est servi qu'en https, à une session admin, avec `Cache-Control: no-store` et `X-Content-Type-Options: nosniff`, et n'est jamais journalisé. Il est enregistré en clair dans les téléchargements, ce que l'interface dit. La restauration reste `config put` ou l'installer.

**(e) `systemd-timesyncd`, pas chrony.** Le client NTP de Calaos OS est `systemd-timesyncd` (l'image n'installe pas chrony). Le réglage de l'heure passe par `timedatectl`, dont `set-ntp` pilote timesyncd, et l'unité `systemd-timesyncd.service` figure dans la liste des services. Régler l'heure à la main n'est proposé que si l'horloge n'est pas synchronisée. Un changement de fuseau demande de redémarrer calaos-server (et calaos-home) : rien ne relit `/etc/timezone`, les processus lisent `/etc/localtime` à leur démarrage ; le daemon ne l'écrit pas et le signale par `restart_required`.

**(f) Mises à jour depuis Calaos mobile.** La ligne « Admin depuis le mobile en phase 1 et 2 » ne vaut plus pour la phase 3 : Calaos mobile (CM1) peut vérifier et installer les mises à jour, suivre leur progression, redémarrer la box et lire les informations système, par le relais calaos_server existant, en https, avec le mot de passe admin. La session admin ne reste qu'en mémoire (15 min, effacée à la déconnexion et au retour de l'arrière-plan après expiration) ; aucun déverrouillage pendant qu'une confiance TLS est en attente, et la session est abandonnée si la clé épinglée change. Aucune action protégée par un code ; ni bootstrap, ni changement du mot de passe admin, ni approbateurs, ni console.

## Ce qu'on ne fait pas
- Rôles ni `users.json`.
- Cookies, sessions navigateur, JWT.
- Confiance fondée sur l'IP.
- Signatures ECDSA d'appareil, WebAuthn, TOTP, number matching. Le code push ne s'applique qu'aux actions listées en phase 2 ; tout le reste de l'admin n'exige que le mot de passe admin.
- Secret d'appareil qui authentifie le téléphone, et rotation silencieuse du token approbateur. Depuis le 2026-10-25, une **clé push** par appareil existe, mais elle ne sert qu'à chiffrer le code pour le cacher au relais push (amendement (a)).
- Sous-volume btrfs pour `/mnt/calaos/admin`.
- Patch de `haproxy.cfg` à l'exécution, et toute modification de `haproxy.cfg` sur une installation existante.
- Étape manuelle pour la montée vers les versions admin (SSH, reboot, commande dédiée).
- Rupture des clients actuels du daemon pendant la release de transition.
- Hostname et `.local` automatiques dans la liste d'Origin.
- URL du daemon configurable dans `local_config.xml`.
- Budget global d'échecs de login.
- Régénération automatique du jeton MCP.
- Reset par clé USB, ou par effacement de fichier et signal.
- Rollback de snapshot à distance, installation disque à distance.
- Mot de passe root SSH, calibration tactile.
- Admin depuis le mobile en phase 1 et 2, sauf l'enrôlement de l'approbateur. Depuis la phase 3 : seulement les mises à jour, leur progression, le reboot et les informations système (amendement (f)) ; ni bootstrap, ni changement du mot de passe admin, ni approbateurs, ni console.

## Risques résiduels acceptés
- Une RCE dans calaos_server donne root (conteneur privilégié).
- Un accès physique donne root.
- Conteneurs du netns hôte (mosquitto anonyme, frontend zigbee2mqtt sans `auth_token`, grafana 8 en root) : en compromettre un donne un pair loopback qui forge XFP et XFF, passe `secureTransport()` et force le mot de passe foyer (freiné par le plafond d'identités : environ 17 essais par minute en régime établi, une centaine la première minute) puis le mot de passe admin au rythme du daemon. Tant qu'il entretient le compteur partagé, et jusqu'à 300 s après, un client inconnu passant par haproxy (nouvel appareil, nouvelle adresse, ou tout client après un redémarrage de calaos_server) est refusé même avec le bon mot de passe. Il peut également bloquer le compteur `127.0.0.1` des clients locaux du loopback, qui n'est pas plafonné. Il n'obtient ni session ni code. Il peut aussi, pendant un redémarrage du daemon, usurper le port 8000 : effacer `admin_seen_set` et capter le bearer. Corrigé en phase 3.
- Quiconque tient le bearer du daemon peut réécrire le mot de passe admin par le mode récupération, comme une RCE dans calaos_server.
- Un rollback restaure le mot de passe admin et le mot de passe foyer de l'époque.
- Avec la console, un attaquant venu d'Internet doit disposer du mot de passe partagé, du mot de passe admin et du téléphone.
- Un relais push compromis voit les codes des téléphones qui n'ont pas de clé push (apps des stores, pendant une release), mais pas le mot de passe admin ; avec une clé push (phase 3), il ne voit que le texte générique et la charge chiffrée. Le relais n'authentifie pas l'émetteur : qui connaît un token peut pousser un faux message vers ce téléphone.
- Un MITM sur le LAN reste possible côté mobile jusqu'à CM2, et capte alors le token push du téléphone.
- Une `admin_session` copiée (XSS, extension, journal tiers) est rejouable 15 min par toute connexion loguée au foyer : elle n'est pas liée à la connexion.
- Le code est lisible sur l'écran verrouillé tant que l'app du téléphone n'envoie pas de clé push (phase 3 : l'écran verrouillé ne montre plus aucun code).
- Une session admin réécrit toutes les options de `local_config.xml` par le `config put` HTTP existant, `listen_address`, `port_api`, `cn_pass`, `mcp_token` et `api_allowed_hosts` compris. La liste blanche de la phase 3 n'est pas une frontière (amendement (c)).
- La sauvegarde téléchargée par `config_backup` contient les secrets que `config get` remet déjà à l'admin ; elle est enregistrée en clair sur le poste de l'admin (amendement (d)).
- Un rollback via GRUB, qui suppose un accès physique, réinstalle d'anciennes versions sans gating.
- Avant le bootstrap, quiconque a le mot de passe partagé peut définir l'admin le premier. On le détecte par le push et la bannière, même si calaos_server était arrêté, et on répare par `calaos-os admin set`.
- Un `api_allowed_hosts` posé avant le bootstrap par un détenteur du mot de passe foyer (via `config put`) reste en place ensuite ; l'écran de bootstrap l'affiche pour qu'on le vérifie.
- La progression des mises à jour (`admin_event`) est diffusée à toute connexion loguée, en clair sur le port 5454 compris.
- Un `haproxy.cfg` modifié avec un frontend sans `set-header X-Forwarded-Proto` laisse passer la valeur du client.
- Les correctifs CC-011 et CC-014 ne protègent une box qu'après la mise à jour du daemon et son redémarrage.
- Après une montée sans reboot, l'ancien token faible reste en service jusqu'au boot suivant (décision de Raoul, 2026-10-22).
- Tant que l'admin n'est pas défini, la box garde l'exposition actuelle : pas de contrôle d'Origin, ACAO reflété, DNS rebinding possible sur le port 5454 (décision de Raoul, 2026-10-22).
- Les routes legacy du daemon restent ouvertes pendant au moins une release, avec leur forme actuelle.
- Selon CH1a, `?token=` reste accepté sur `/events` jusqu'à CC2b.
- Si un admin est défini (par exemple par `calaos-os admin set`) avant que calaos_server ait jamais joint le daemon, et que le daemon reste injoignable, calaos_server se comporte comme sans admin jusqu'au premier contact : `config`, `get_mcp_info` et Origin restent au niveau foyer (décision de Raoul, 2026-10-22).
- Webapp sur certificat auto-signé : l'utilisateur accepte un avertissement. Un MITM actif sur le LAN peut servir le port 80 en clair, et HSTS n'est pas possible. Un certificat Let's Encrypt via le DDNS supprime ce risque. Via un VPN sans certificat, comme MLSH, l'avertissement reste, mais le MITM n'est plus possible à l'intérieur du tunnel.
- Le limiteur argon2 du daemon (environ 5 échecs par minute) permet à un tiers qui échoue en boucle de retarder les déverrouillages légitimes.
- Sans second approbateur, une rotation du token push oblige à passer par SSH pour la console et le réseau (décision de Raoul, 2026-10-22).
- Enrôlement au premier arrivé pendant une fenêtre sans approbateur (après la montée vers la phase 2, un reset ou un rollback) : détecté par l'alerte persistée `approver_set`, poussée à tous les téléphones, et corrigé par `calaos-os admin reset` (décision de Raoul, 2026-10-24).

## Vérification
- **calaos-container** (tests Go) :
  - token de 32 octets en 0600 ; `?token=` refusé sur toutes les routes, ou sur toutes sauf `/events` selon CH1a ;
  - hash, longueur minimale, limiteur qui ne compte que les échecs, vérification concurrente refusée en 429 sans attente ;
  - session de 32 octets, expiration absolue à 15 min, invalidation par `set`, `reset` et 5 échecs de code (phase 2), survie à l'auto-redémarrage ;
  - deux bootstraps concurrents : un seul gagne, l'autre reçoit `admin_already_set` ;
  - trois modes d'`admin/set` : bootstrap sans session, changement avec session, récupération avec le seul bearer ; tous invalident les sessions et publient `admin_set` ;
  - 401 de bearer distinct de `bad_session` (champ `code`) ;
  - alerte publiée sans abonné, rejouée au premier abonné, plus jamais après l'ack ;
  - upgrade asynchrone, résultat persisté, sortie 0 après auto-mise à jour et seulement après l'événement terminal, 409 sur reboot pendant un job ;
  - porteur de session refusé sur `install/*`, `rollback_snapshot`, `network/*` ;
  - audit : id de session tronqué au SHA-256, jamais la session ;
  - CLI `admin set` sur stdin, `admin reset`, `upgrade` sur 202 et `/events` ;
  - routes legacy : réponses identiques à l'ancien daemon (golden), upgrade synchrone compris, pour l'ancien CLI et l'ancien kiosque ; un abonné legacy reçoit des types d'événements nouveaux sans erreur.
- **Paquet calaos-container** : la postinst ne redémarre jamais le daemon pendant un run et ne bloque jamais sur son démarrage ; le redémarrage différé attend le verrou dpkg et `status: ""` ou `"idle"`, réessaie si le daemon est injoignable, abandonne au bout de 2 h, et ne fait rien si le binaire en mémoire est à jour.
- **init_calaosfs** : dix units démarrées en parallèle, script idempotent, `/mnt/calaos/admin` en 0700, token existant conservé, un seul token écrit en cas de course, `/etc/ola` créé.
- **calaos_base** (tests JsonApi, faux daemon via `CALAOS_CONTAINER_URL`) :
  - `admin_not_set` ;
  - sans admin défini, comportement identique à aujourd'hui : Origin étranger accepté et journalisé, ACAO reflété, `config` et `get_mcp_info` au niveau foyer ;
  - `AdminState` (répertoire de cache temporaire via `--cache`, jamais `~/.cache/calaos`) :
    - sans daemon, comportement identique à aujourd'hui (`config get/put`, `get_mcp_info`, Origin, ACAO), et `admin*` répond `admin_unavailable` ;
    - drapeau `admin_seen_set` présent et daemon arrêté : `config`, `get_mcp_info` et `admin*` en `daemon_unreachable`, Origin contrôlé, ACAO non reflété ;
    - daemon joignable : « défini » pose le drapeau, `admin_not_set` ou un 404 l'efface ;
    - rafraîchi par `admin_set` et `admin_reset` ;
  - `CALAOS_CONTAINER_URL` vers un hôte autre que loopback refusé au démarrage ;
  - faux daemon ancien (routes legacy seules) : `daemon_too_old`, le reste fonctionne ;
  - gating de `config` et `get_mcp_info` par `admin_session` dans le corps ; session en query ignorée ; `change_cred` toujours accepté au niveau foyer ;
  - `admin {action: update_status}` accepté sans session sous `secureTransport()` ;
  - échec d'`admin_unlock` sans fermeture du WS ; `bad_password` et `rate_limited` distincts ;
  - refus en HTTP clair, hors haproxy et sans XFP ;
  - admin défini : Origin étranger, `null` ou rebindé refusé ; IP d'interface acceptée sur 5454 ; Origin égal à `Host` accepté sous haproxy-https ;
  - bootstrap refusé avec des identifiants vides, `user/pass` ou legacy ;
  - scope MCP refusé (`admin_unlock`, `admin_set` et `admin`) ;
  - table fermée : valeur inconnue refusée, `package` contenant `/`, `..`, `?` ou `%2e` refusé ;
  - filtre `/events` : un client foyer reçoit `upgrade_progress`, `admin_set` et `admin_reset` avec l'horodatage seul, et rien d'autre ;
  - client `/events` : reconnexion après coupure, 404/426 traité comme daemon trop ancien ;
  - alerte rejouée après un redémarrage de calaos_server : un seul push, un seul événement ;
  - plafond des identités XFF loopback ;
  - `mcp_regenerate` change le jeton et redémarre le sidecar.
- **Webapp** : e2e sur le mock avec la coupure et la reprise pendant l'upgrade, calaos_server ancien (message au bout de 5 s, pas d'attente infinie), daemon ancien (« Activation en cours » puis reprise), `bad_session` suivi d'une nouvelle demande, changement du mot de passe admin.
- **Installer** : enregistrement en ligne avec et sans mot de passe admin, un seul déverrouillage par dialogue, hôte `http://…:5454` réécrit en https.
- **Phase 2** :
  - `push_code` par id : le bus ne porte ni token ni code, calaos_server récupère `{token, message}` par la route bearer et pousse vers ce seul token ;
  - un client foyer ne reçoit jamais `push_code` ;
  - `register_push` d'un token approbateur par un client foyer ne change rien à l'approbateur ; `device_name` stocké et proposé à l'admin ;
  - brute force du code bloqué après 5 essais, et session daemon invalidée (`bad_session` à l'appel suivant) ;
  - `approver_set` et `console_opened` : alerte persistée, rejouée jusqu'à l'ack, poussée à tous les téléphones ;
  - retrait du dernier approbateur refusé (`approver_last`) ;
  - ticket console non rejouable, ttyd tué à l'expiration, pings sans effet sur l'inactivité ;
  - `/api/console` refusé hors haproxy et avec un Origin étranger ;
  - entrée d'audit présente dans journald.
- **Recette en VM (RC1)**, depuis l'image stable 4.5.0, sans aucune admin définie au départ :
  - chacun des trois chemins (`calaos-os upgrade`, `apt upgrade`, bouton du kiosque) aboutit sans reboot ni SSH : nouveau daemon actif, ancien CLI et ancien kiosque fonctionnels pendant et après, webapp foyer inchangée, `haproxy.cfg` intact ;
  - les deux ordres d'activation : calaos-server redémarré avant le daemon (ancien daemon en mémoire, `daemon_too_old`) et après ;
  - run tué pendant la configuration de calaos-container, puis pendant celle de calaos-server : le daemon redémarre et termine la configuration sans dépasser le délai ni se bloquer ;
  - sessions intactes après `systemctl restart calaos-container` ;
  - ensuite, montée dev.x vers dev.y depuis la webapp, y compris l'auto-upgrade de calaos-server et la sortie 0 du daemon ;
  - au boot suivant, token au nouveau format et clients toujours connectés.
