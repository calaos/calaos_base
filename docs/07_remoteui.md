# RemoteUI — Appareils embarqués connectés

## Vue d'ensemble

Le sous-système RemoteUI permet à des appareils embarqués (tableaux de bord, panneaux muraux,
ESP32-S3, Waveshare/Luckfox 86-panel, etc.) de se connecter au serveur Calaos via WebSocket avec
authentification **HMAC-SHA256**.

L'appareil peut :
- recevoir l'état des IOs référencés par ses pages ;
- remonter l'état de ses relais physiques ;
- recevoir des mises à jour firmware OTA.

> **Provenance.** Aucun des 145 fichiers de référence de `tests/core/golden/` ne couvre RemoteUI
> (vérifié : `grep -rl remote_ui tests/core/golden/` ne renvoie rien). Toutes les affirmations
> ci-dessous sont adossées au **code** (`Fichier.cpp:ligne` vérifié) ou aux trois suites de test
> RemoteUI listées en fin de document. Le fichier
> [src/bin/calaos_server/RemoteUI/remote-ui.md](../src/bin/calaos_server/RemoteUI/remote-ui.md)
> est une **spécification**, pas une description du code : sur plusieurs points elle diverge de ce
> qui est implémenté (voir « Écarts spec / code » plus bas).

---

## Architecture

```
[Appareil embarqué]
       ↕ WebSocket sur /api/v3/remote_ui/ws
[RemoteUIWebSocketHandler]  (par connexion, dérive de JsonApiHandlerWS)
       ↕
[RemoteUIManager]           (singleton, registre global)
       ↕
[RemoteUI IO]               (dans ListeRoom, + RemoteUIOutputRelay pour les relais)
```

⚠️ Rien dans le chemin RemoteUI n'impose TLS : la réponse de provisioning distribue une URL
**`ws://`** en clair (dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:463`).

---

## RemoteUI (IO)

**Dossier :** [src/bin/calaos_server/IO/RemoteUI/](../src/bin/calaos_server/IO/RemoteUI/)

Un IO de type `RemoteUI` représente un appareil embarqué dans le modèle de données. Il vit dans
**`io.xml`**, sous un nœud de pièce, comme les autres IOs (dérivé,
`src/bin/calaos_server/Room.cpp:171` ; le provisioning persiste via
`Config::Instance().SaveConfigIO()`, `src/bin/calaos_server/RemoteUI/RemoteUIProvisioningHandler.cpp:207`).

### Paramètres déclarés

(dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:99-108`)

| Paramètre | Type | Obligatoire | Défaut | Description |
|---|---|---|---|---|
| `device_type` | liste | oui | `waveshare-86-panel` | Modèle : `waveshare-86-panel`, `luckfox-86-panel`, `custom` |
| `provisioning_code` | string | oui | — | Code de provisioning pour la mise en service initiale |
| `auth_token` | string | non | — | Token d'authentification (généré au provisioning) |
| `device_manufacturer` | string | non | — | Fabricant remonté par l'appareil |
| `device_platform` | string | non | — | Plateforme remontée par l'appareil |
| `device_secret` | string | non | — | Secret partagé servant de **clé HMAC** |
| `device_version` | string | non | — | Version remontée par l'appareil |
| `grid_h` | int | oui | `3` | Taille horizontale de la grille |
| `grid_w` | int | oui | `3` | Taille verticale de la grille |
| `mac_address` | string | non | — | Adresse MAC de l'appareil |

⚠️ **Le paramètre de token s'appelle `auth_token`, pas `token`.** C'est ce nom que
`RemoteUIManager::getRemoteUIByToken()` interroge (dérivé,
`src/bin/calaos_server/RemoteUI/RemoteUIManager.cpp:96`) et que le provisioning écrit
(`RemoteUIProvisioningHandler.cpp:199`).

Le constructeur pose aussi `set_param("gui_type", "remote_ui")` et, quand `visible` est absent,
`set_param("visible", "false")` (dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:114`,
`:128`).

⚠️ Plusieurs paramètres sont **lus sans être déclarés dans `ioDoc`** : `name`, `brightness`,
`timeout`, `theme`, `room`, et les huit `screensaver_*`
(dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:494-497`, `:511`, `:525` et
`src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.cpp:272-279`). Ils n'apparaissent donc
pas dans `io_doc.json`.

### Actions

`set_brightness X`, `set_page page_id`, `show_notif message`
(dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:110-112`).

### `device_info` — aller-retour rétabli (T3.15)

Jusqu'à T3.15, `SaveToXml()` attachait `<calaos:device_info>` au nœud qu'il recevait —
c'est-à-dire le nœud **PIÈCE**, puisque `Room::SaveToXml()` remet son propre élément à chacun de
ses IOs — et le faisait **avant** d'ajouter `<calaos:remote_ui>`. L'élément sortait donc comme
**frère précédent immédiat** de l'appareil qu'il décrit, là où `LoadFromXml()` — qui a toujours
regardé **à l'intérieur** de `<calaos:remote_ui>`, comme le format le documente — ne le trouvait
jamais. Les informations étaient **écrites mais jamais relues**.
(dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:39-63`.)

**Écriture, désormais au bon endroit** (dérivé,
`src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:255-264`) :

```cpp
if (!device_info.empty())
{
    pugi::xml_node device_info_elem = cnode.append_child("calaos:device_info");

    for (auto it = device_info.begin(); it != device_info.end(); ++it)
    {
        if (it.value().is_string())
            XmlUtils::setAttribute(device_info_elem, it.key(), it.value().get<string>());
    }
}
```

`cnode` est le `<calaos:remote_ui>` créé juste avant (`:238`).

**Lecture, avec récupération des orphelins** (dérivé,
`src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:140-158`) : si `node.child("calaos:device_info")`
est vide, `legacyRoomDeviceInfo(node)` remonte les frères **précédents** du nœud `remote_ui`,
saute les nœuds non-éléments, et adopte le premier élément rencontré **s'il** s'appelle
`calaos:device_info` — sinon il n'adopte rien (dérivé, `:64-80`). Un log le signale :
`migrating a <calaos:device_info> found under the room node, it will be saved inside
<calaos:remote_ui>` (`:149-150`).

C'est une **migration unique** : la valeur est resauvegardée au bon endroit, donc l'orphelin
disparaît à la sauvegarde suivante — `Room::SaveToXml()` reconstruit l'élément de pièce depuis le
modèle, il ne rapièce pas l'ancien document (dérivé, `:60-62`). **Aucune action utilisateur
nécessaire.**

**Il n'y a pas de liste fixe de champs** : chaque attribut XML devient une clé JSON de type
chaîne (`:157-158`), et seules les valeurs chaînes sont réécrites (`:261`) — l'aller-retour est
donc sans perte par construction.

⚠️ **Rien ne peuple `device_info` à l'exécution aujourd'hui.** La seule affectation du membre est
`device_info = Json::object();` dans `LoadFromXml` (`:155`) ; le provisioning, lui, écrit des
paramètres d'IO ordinaires (`device_type`, `device_manufacturer`, `device_platform`,
`device_version`, `mac_address` — `RemoteUIProvisioningHandler.cpp:173-185`). `device_info` ne
fait donc aujourd'hui que restituer ce qu'un Calaos Installer ou un fichier hérité y a mis.

---

## RemoteUIManager

**Fichier :** [src/bin/calaos_server/RemoteUI/RemoteUIManager.h](../src/bin/calaos_server/RemoteUI/RemoteUIManager.h)

Singleton gérant toutes les connexions RemoteUI actives, instancié depuis `main()`
(dérivé, `src/bin/calaos_server/main.cpp:145`).

### API publique

(dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIManager.h:86-131`)

```cpp
static RemoteUIManager &Instance();

RemoteUI *getRemoteUI(const string &id);
RemoteUI *getRemoteUIByToken(const string &token);
std::vector<RemoteUI*> getAllRemoteUIs();

bool validateAuthentication(const string &token, const string &timestamp,
                            const string &nonce, const string &hmac,
                            const string &ip_address);
AuthFailureReason validateAuthenticationWithReason(const string &token, const string &timestamp,
                                                   const string &nonce, const string &hmac,
                                                   const string &ip_address);

void notifyAllIOStates();
void notifyOtaUpdates();

bool checkRateLimit(const string &ip_address);
void addNonce(const string &nonce, const string &ip_address);
bool isNonceUsed(const string &nonce) const;

void addWebSocketHandler(const string &remote_ui_id, RemoteUIWebSocketHandler *handler);
void removeWebSocketHandler(const string &remote_ui_id, RemoteUIWebSocketHandler *handler);

void sendCommand(const string &remote_ui_id, const string &msg_type, const Json &data);

size_t getOnlineCount() const;
size_t getTotalCount() const;
size_t getConnectedHandlerCount() const;
```

### Authentification

Le message signé est la concaténation `token:timestamp:nonce`
(capturé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:407`, intégral) :

```cpp
string message = token + ":" + timestamp + ":" + nonce;
```

La clé HMAC est le paramètre **`device_secret`** de l'IO (`:403`), et le condensé est calculé par
`HMAC(EVP_sha256(), …)` d'OpenSSL (`:412-415`).

**En-têtes HTTP du handshake** (dérivé,
`src/bin/calaos_server/RemoteUI/HMACAuthenticator.cpp:36-43`) : `Authorization` (forme
`Bearer <token>`), `X-Auth-Timestamp`, `X-Auth-Nonce`, `X-Auth-HMAC`, plus les facultatifs
`User-Agent`, `Origin`, `X-Device-Version`, `X-Device-Hardware-Id`. **Les quatre premiers sont
obligatoires** (`:48-54`).

**Ordre des contrôles**, défini en un seul endroit — `validateAuthentication()` délègue à
`validateAuthenticationWithReason()` pour que les vérifications ne divergent pas
(dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIManager.cpp:134-141`) :

> rate-limit → rejeu de nonce → longueur de nonce → fenêtre temporelle → HMAC

Le nonce doit faire **exactement 64 caractères hexadécimaux** (32 octets)
(dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIManager.cpp:161-167`).

### Comparaisons en temps constant (T1.6, T2.15)

Deux secrets sont comparés sur ce chemin, et **les deux** le sont en temps constant :

- **Le MAC** — `HMACAuthenticator::constantTimeHexEquals(hmac, result, result_len)`
  (dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:422`). L'implémentation garde d'abord
  la longueur, puis compare les octets bruts avec `CRYPTO_memcmp`
  (dérivé, `src/bin/calaos_server/RemoteUI/HMACAuthenticator.h:126-135`). L'ancien
  `std::string operator==` court-circuitait au premier octet différent, ce qui divulguait par le
  temps de réponse combien de caractères de tête du MAC fourni étaient corrects (`RemoteUI.cpp:417-421`).
- **Le token** (T2.15) — la comparaison est faite par `JsonApi::secureCompare()`
  (capturé, `src/bin/calaos_server/RemoteUI/RemoteUIManager.cpp:96`, intégral) :

  ```cpp
  if (JsonApi::secureCompare(io->get_param("auth_token"), token))
  ```

  Plus aucun `==` sur le token dans ce chemin.

### Limites de sécurité

⚠️ **Elles ne sont pas dans `RemoteUISecurityLimits.h`.** Les trois constantes d'authentification
sont publiques dans `RemoteUIManager.h` (extrait — les commentaires intercalaires sont omis,
`src/bin/calaos_server/RemoteUI/RemoteUIManager.h:134`, `:135`, `:141`) :

```cpp
static constexpr int MAX_ATTEMPTS_PER_MINUTE = 20;
static constexpr int NONCE_EXPIRY_SECONDS = 300; // 5 minutes
static constexpr int TIMESTAMP_TOLERANCE_SECONDS = 30;
```

`RemoteUISecurityLimits.h` porte, lui, **quatre** plafonds anti-épuisement mémoire, et rien
d'autre (dérivé, `src/bin/calaos_server/RemoteUI/RemoteUISecurityLimits.h:27-44`) :

| Constante | Valeur |
|---|---|
| `MAX_REQUEST_BODY_SIZE` | `1024 * 1024` (1 Mio) |
| `MAX_PAGES_PER_REMOTEUI` | `50` |
| `MAX_WIDGETS_PER_PAGE` | `100` |
| `MAX_STRING_LENGTH` | `255` |

Deux minuteries de ménage tournent en continu : purge des nonces toutes les 300 s, purge du
rate-limit toutes les 60 s (dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIManager.cpp:61`,
`:69`).

### Envoi de commandes vers un appareil

```cpp
RemoteUIManager::Instance().sendCommand(remote_ui_id, "remote_ui_set_relay",
    { { "relay", relay_num }, { "state", val } });
```

(capturé, `src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.cpp:49-50`, intégral.)

### Notifications d'état

```cpp
RemoteUIManager::Instance().notifyAllIOStates();  //états IO à tous les appareils connectés
RemoteUIManager::Instance().notifyOtaUpdates();   //firmwares disponibles
```

---

## RemoteUIWebSocketHandler

**Fichier :** [src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.h](../src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.h)

Gestionnaire de connexion WebSocket pour un appareil RemoteUI. **Il dérive de
`JsonApiHandlerWS`** : tout message qu'il ne reconnaît pas est délégué à l'API JSON standard
(dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.cpp:157`). Un appareil
authentifié atteint donc aussi `login`, `get_home`, `get_state`, `set_state`…

### Routage et authentification du handshake

L'URL est **`/api/v3/remote_ui/ws`** (dérivé, `src/bin/calaos_server/WebSocket.cpp:273-274`).
L'authentification a lieu **pendant le handshake HTTP**, pas dans un message WebSocket : en cas
d'échec, le serveur répond une erreur HTTP avec le statut mappé et un corps JSON, puis ferme
(dérivé, `src/bin/calaos_server/WebSocket.cpp:286-333`) :

```json
{"error": "invalid_hmac", "status": "authentication_failed"}
```

(dérivé, `src/bin/calaos_server/WebSocket.cpp:300-302` — la valeur d'`error` est celle
d'`authFailureToString()`.)

### Enveloppe des messages

Les messages sont typés par une clé **`msg`** au premier niveau, avec la charge utile sous
`data` — c'est l'enveloppe de `JsonApiHandlerWS::sendJson()`
(dérivé, `src/bin/calaos_server/JsonApiHandlerWS.cpp:75-84`) :

```json
{"msg": "<type>", "data": { … }}
```

⚠️ Ce n'est **pas** une clé `type` : le dispatch entrant lit `message["msg"]`
(dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.cpp:130-132`).

### Messages entrants (appareil → serveur) — deux, et deux seulement

| `msg` | Charge utile | Effet | Site |
|---|---|---|---|
| `remote_ui_get_config` | — | Répond `remote_ui_config` | `RemoteUIWebSocketHandler.cpp:135-140` |
| `remote_ui_relay_state` | `data.relay` (int), `data.state` (bool) | Met à jour l'IO relais correspondant sans reboucler | `RemoteUIWebSocketHandler.cpp:142-148`, `:169-221` |

⚠️ Il n'existe **ni `auth`, ni `relay_update`, ni `config_request`, ni `ota_request`** — ces quatre
types figuraient dans ce document avant la revue E4.5e et **aucun n'est dispatché**.
L'authentification se fait au handshake (voir plus haut), et le rescan OTA est un endpoint HTTP.

Les deux champs de `remote_ui_relay_state` sont obligatoires, et une valeur du mauvais type est
attrapée et ignorée avec un avertissement, sans réponse d'erreur
(dérivé, `RemoteUIWebSocketHandler.cpp:174-192`).

### Messages sortants (serveur → appareil)

| `msg` | Contenu | Site |
|---|---|---|
| `remote_ui_config` | Réponse à `remote_ui_get_config` (`getRemoteUIConfigMessage()`) | `RemoteUIWebSocketHandler.cpp:165-166` |
| `remote_ui_io_states` | États initiaux des IOs référencés par les pages | `RemoteUIWebSocketHandler.cpp:252` |
| `remote_ui_config_update` | Configuration complète poussée (voir ci-dessous) | `RemoteUIWebSocketHandler.cpp:314` |
| `remote_ui_fw_update_available` | Firmware disponible | `OtaFirmwareManager.cpp:273` |
| `remote_ui_set_relay` | `{ "relay": <int>, "state": <bool> }` | `IO/RemoteUI/RemoteUIOutputRelay.cpp:49-50` |
| `remote_ui_set_brightness` | `{ "brightness": <int> }` | `IO/RemoteUI/RemoteUI.cpp:513-514` |
| `remote_ui_set_page` | `{ "page_id": … }` | `IO/RemoteUI/RemoteUI.cpp:531-532` |
| `remote_ui_notification` | `{ "message": … }` | `IO/RemoteUI/RemoteUI.cpp:542-543` |
| `event` | Événements Calaos, hérités de `JsonApiHandlerWS` | `JsonApiHandlerWS.cpp:60` |

⚠️ Il n'existe **ni `auth_ok`/`auth_fail`, ni `io_state`, ni `relay_set`, ni `config_update`, ni
`ota_update`** — ces six noms figuraient dans ce document avant la revue E4.5e ; aucun n'est émis.

Clés de `remote_ui_config_update` (dérivé,
`src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.cpp:304-353`) : `name`, `brightness`
(⚠️ **seulement s'il est réglé**, T3.74), `grid_height`, `grid_width`, les huit `screensaver_*`,
`pages`, `room`
(`name`/`type`/`hits`, seulement si la pièce est trouvée), et `io_items` — un tableau projeté par
`JsonApi::buildJsonIO(io, jio, IoProjection::DeviceConfig)` (`:346`).

Les noms de params sont ceux de l'API 5454, déclarés une seule fois par
`JsonApi::ioProjectionParams()` (`JsonApi.cpp:576`) : `id`, `name`, `type`, `hits`, `var_type`,
`visible`, `chauffage_id`, `rw`, `unit`, `gui_type`, `state`, `autoscenario_uid`, `step`, `io_type`,
`io_style`, `value_warning`. ⚠️ **La politique de valeur, elle, n'est pas la même que sur 5454** —
trois différences, et l'appareil ne négocie aucune version de protocole, donc aucune ne peut être
nivelée par le serveur seul ([T3.69](refactoring/T3.69.md)) :

| | 5454 | `io_items` |
|---|---|---|
| `state`, `var_type` | calculés depuis la valeur et le type de l'IO | lus comme des params — et aucun IO construit par le serveur n'en porte, donc l'écran n'en reçoit pas |
| param qui existe et est vide | émis (`"unit": ""`) | omis |
| `status_info` | objet imbriqué ajouté | jamais |

L'écran tient ses états de `remote_ui_io_states`, pas de sa configuration.

Clés de `remote_ui_fw_update_available` (dérivé,
`src/bin/calaos_server/RemoteUI/OtaFirmwareManager.cpp:260-270`) : `hardware_id`, `version`,
`checksum_sha256`, `download_url`, plus `release_notes` et `name` s'ils sont renseignés.

### Cycle de vie (T1.10)

- Jeton de vie : `std::shared_ptr<bool> handlerAlive` (dérivé,
  `src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.h:48`), invalidé en tête de destructeur
  (`…cpp:46`) avant le désenregistrement auprès du manager (`:53`).
- `buildJsonState()` peut différer son callback à cause de requêtes audio asynchrones : la
  complétion est gardée par le jeton, et la branche d'abandon **libère** le résultat dont elle
  détient la propriété — `json_decref(jret); //we own the result, avoid leaking it`
  (dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.cpp:236-247`).
- Parse de grille **non lançante** : `parseGridDimension()` borne à `[1, 1000]` et retombe sur
  3×3, pour qu'une valeur malformée venue de l'appareil ne remonte pas une exception dans la
  boucle d'événements (dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.h:79-87`,
  appelée `…cpp:270-271`).
- Une nouvelle connexion pour un appareil déjà connecté **ferme proprement l'ancienne**, avec le
  code de fermeture `1000` et la raison `replaced by new connection`
  (dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIManager.cpp:401-402`) ; le retrait vérifie
  l'identité du pointeur pour ne pas déréférencer un handler déjà remplacé (`:416`).

✅ **Corrigé par [T3.68](refactoring/T3.68.md)** : `getRemoteUIConfigMessage()` lisait
`brightness` et `timeout` par un `std::stoi` nu sur des paramètres **ni déclarés ni défaultés**, et
l'exception, avalée par le `catch` de `processApi()`, était journalisée en « JSON parse error » —
l'appareil ne recevait **aucune** réponse `remote_ui_config`. Les deux passent maintenant par
`getTimeout()` (défaut **30**, corroboré par le micrologiciel, `main/calaos_protocol.h:137`).
⚠️ **`getBrightness()` (défaut 100) a été SUPPRIMÉ par [T3.74](refactoring/T3.74.md)** : ce défaut
contredisait le 80 de l'appareil et de la spécification de wire. `brightness` n'est plus posé que
lorsque le param est réglé et lisible (`RemoteUI::putBrightnessIfSet()`), et l'écran retombe sinon
sur sa propre valeur (dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:583-590`). Le `try` de `processApi()` est coupé en
deux : le parse garde son nom, le service du message est nommé
`unhandled failure while serving <msg>` (dérivé,
`src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.cpp:130-178`).

✅ **Corrigé par [T3.70](refactoring/T3.70.md)** : `LoadFromXml()` convertissait les attributs
`x`/`y` d'un `<calaos:widget>` par un `std::stoi` nu, et **aucun maillon du chemin de chargement
n'a de `try`** (`IOFactory::CreateIO()`, `Room::LoadFromXml()`, `Config::LoadConfigIO()`,
`main()`) — un `x=""` ou un `x="haut"` faisait **terminer le serveur avant sa boucle
d'événements**, emportant toute la configuration déclarée après. La lecture passe par
`Utils::from_string_or_keep()` : si la valeur n'est pas un entier entier, l'attribut reste
**absent** et le widget tombe dans le contrôle voisin qui écarte déjà tout widget sans
`type`/`x`/`y`. Chaque widget écarté est signalé sur le canal différé mail/push du chargement de
configuration (`Config::reportConfigAlert()`), avec l'écran, la page et le widget concernés
(dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:238-289`).

---

## RemoteUIOutputRelay

**Fichier :** [src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.h](../src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.h)

IO dérivant d'`OutputLight` (dérivé,
`src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.h:29`), représentant un relais physique sur
l'appareil RemoteUI. Il se pilote donc comme n'importe quelle sortie lumière.

### Paramètres

(dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.cpp:37-38`)

| Paramètre | Type | Obligatoire | Description |
|---|---|---|---|
| `remote_ui_id` | string | oui | ID de l'IO RemoteUI parent |
| `relay_num` | int **1…99** | oui | Numéro du relais sur l'appareil (`1, 2, ...`) |

⚠️ **`relay_num` est indexé à partir de 1**, pas de 0 : le défaut du constructeur est `relay_num(1)`
(`:31-33`) et `paramAddInt` borne à `[1, 99]` (`:38`). Ce document indiquait « 0-based » avant la
revue E4.5e.

### Sens des flux

- **Commande → appareil** : `set_value_real(bool val)` émet
  `sendCommand(remote_ui_id, "remote_ui_set_relay", { { "relay", relay_num }, { "state", val } })`
  (dérivé, `…RemoteUIOutputRelay.cpp:47-52`).
- **Retour appareil → serveur** : le handler retrouve l'IO par le triplet
  `type == "RemoteUIOutputRelay"` + `remote_ui_id` + `relay_num`, puis appelle
  `updateStateFromDevice(state)` — qui met l'état à jour **sans reboucler** vers l'appareil
  (dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.cpp:203-210`). Si aucun IO ne
  correspond, un avertissement est journalisé (`:219-220`).

---

## OTA Firmware

**Fichiers :** [OtaFirmwareManager.h](../src/bin/calaos_server/RemoteUI/OtaFirmwareManager.h),
[OtaHttpHandler.h](../src/bin/calaos_server/RemoteUI/OtaHttpHandler.h),
[FirmwareManifest.h](../src/bin/calaos_server/RemoteUI/FirmwareManifest.h)

### Options de configuration

| Clé | Défaut | Rôle |
|---|---|---|
| `ota_enabled` | activé | Coupe tout le sous-système quand elle vaut autre chose que `true`/`1` |
| `ota_firmware_path` | `/usr/share/calaos/firmwares` | Racine des firmwares |
| `ota_rescan_interval` | 60 minutes | Période de re-scan |

(dérivé, `src/bin/calaos_server/RemoteUI/OtaFirmwareManager.cpp:36-37`, `:54-56`, `:65-72`,
`:76-77`.) Leur description de référence est dans
[16_config_options.md](16_config_options.md), générée depuis le registre.

### ⚠️ Intervalle de re-scan borné à 30 jours

(capturé, `src/bin/calaos_server/RemoteUI/OtaFirmwareManager.h:101-110`, intégral :)

```cpp
static uint64_t computeRescanIntervalMs(int minutes)
{
    static const uint64_t maxMs = 30ull * 24 * 60 * 60 * 1000; //30 days
    if (minutes < 1)
        minutes = 1;
    uint64_t ms = static_cast<uint64_t>(minutes) * 60ull * 1000ull;
    if (ms > maxMs)
        ms = maxMs;
    return ms;
}
```

Le calcul est fait en 64 bits et borné à **`[1 minute, 30 jours]`**. En `int`,
`minutes * 60 * 1000` débordait (UB) au-delà d'environ 35 791 minutes et pouvait produire une
période de minuterie **minuscule** (dérivé, `…OtaFirmwareManager.h:95-99`). La fonction est
inline dans l'en-tête exprès, pour être testable sans lier les objets du serveur.

### Découverte des firmwares

Un sous-répertoire par `hardware_id` sous la racine, chacun portant un `manifest.json` ; le nom du
répertoire doit être égal au `hardware_id` du manifeste, et le checksum est vérifié dès le scan
(dérivé, `src/bin/calaos_server/RemoteUI/OtaFirmwareManager.cpp:174`, `:195-201`, `:204-208`).

Champs de `manifest.json` (dérivé,
`src/bin/calaos_server/RemoteUI/FirmwareManifest.cpp:60-109` et `FirmwareManifest.h:69-85`) :

| Clé JSON | Obligatoire |
|---|---|
| `schema_version` | oui |
| `version` | oui |
| `hardware_id` | oui |
| `firmware_file` | oui |
| `checksum_sha256` | oui |
| `name`, `description`, `release_date`, `release_notes`, `metadata` | non |

⚠️ **Il n'y a pas de champ URL** dans le manifeste : l'URL de téléchargement est synthétisée —
`"/api/v3/ota/firmware/" + hardware_id + "/download"`
(dérivé, `src/bin/calaos_server/RemoteUI/OtaFirmwareManager.cpp:264`).

⚠️ **La décision de mise à jour est une simple inégalité de chaînes**, pas une comparaison
sémantique de versions : `if (firmware->getVersion() == currentVersion)` → pas de mise à jour
(dérivé, `src/bin/calaos_server/RemoteUI/OtaFirmwareManager.cpp:249-253`). Un « downgrade » est
donc proposé comme une mise à jour.

### Endpoints HTTP

Préfixe `/api/v3/ota/` (dérivé, `src/bin/calaos_server/RemoteUI/OtaHttpHandler.cpp:47-48`) :

| Méthode + chemin | Auth | Réponse |
|---|---|---|
| `POST /api/v3/ota/rescan` | **localhost seulement** | `{"success":…, "message":"Firmware rescan completed", "firmware_count":…}` |
| `GET /api/v3/ota/firmware/{hardware_id}/download` | HMAC | Le binaire, `Content-Type: application/octet-stream`, en-tête `X-Checksum-SHA256` |

(dérivé, `src/bin/calaos_server/RemoteUI/OtaHttpHandler.cpp:68-71`, `:75-105`, `:153-159`,
`:166-168`, `:224-227`.)

Autres réponses : OTA désactivé → `503 OTA_DISABLED` (`:61`) ; chemin inconnu sous le préfixe →
`404 NOT_FOUND` (`:108`) ; URL de téléchargement malformée → `400 INVALID_URL` (`:89`) ;
identifiant matériel refusé par la garde anti-traversée → `400 INVALID_HARDWARE_ID` (`:100`).
Corps d'erreur : `{"success": false, "error": {"code": …, "message": …}}` (`:202-205`).

Le firmware est **streamé** par blocs de 64 Kio, et non chargé en mémoire d'un bloc
(dérivé, `src/bin/calaos_server/RemoteUI/OtaHttpHandler.cpp:235-252`).

---

## Provisioning

**Fichier :** [src/bin/calaos_server/RemoteUI/RemoteUIProvisioningHandler.h](../src/bin/calaos_server/RemoteUI/RemoteUIProvisioningHandler.h)

Préfixe `/api/v3/provision/`, **une seule route** : `POST /api/v3/provision/request` — tout le
reste répond `404 Endpoint not found` (dérivé,
`src/bin/calaos_server/RemoteUI/RemoteUIProvisioningHandler.cpp:56-57`, `:69-76`).

### Requête

Clés obligatoires : `code` (chaîne) et `device_info` (objet), lequel doit porter `type` et
`mac_address` (dérivé, `…RemoteUIProvisioningHandler.cpp:246-259`).

⚠️ **La spec dit `provisioning_code`, le code lit `code`**
(`remote-ui.md:43-46` contre `RemoteUIProvisioningHandler.cpp:117` et `:246`).

Les clés de `device_info` réellement consommées, et le paramètre d'IO où chacune atterrit
(dérivé, `…RemoteUIProvisioningHandler.cpp:173-185`) : `hardware_id` → `device_type`,
`manufacturer` → `device_manufacturer`, `platform` → `device_platform`, `version` →
`device_version`, `mac_address` → `mac_address`.

### Réponse

(dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:454-473`)

```json
{
  "status": "accepted",
  "device_id": "…",
  "auth_token": "…",
  "device_secret": "…",
  "server_config": { "websocket_url": "…", "sync_interval": 1000 },
  "remote_ui_config": { "name": "…", "pages": [ … ] }
}
```

En cas d'échec : `{"status": "error", "error": "…"}`
(dérivé, `…RemoteUIProvisioningHandler.cpp:233-234`).

⚠️ **Aucun identifiant Wi-Fi n'est jamais renvoyé** — ce document l'affirmait avant la revue
E4.5e ; les six clés ci-dessus sont la réponse complète.

⚠️ **`websocket_url` est codée en dur sur localhost** (dérivé,
`src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:463`) :

```cpp
server_config["websocket_url"] = "ws://localhost:5454/api/v3/remote_ui/ws";
```

Un appareil provisionné se retrouve donc pointé sur `localhost`, ce qui est faux en déploiement
réel. Défaut connu, consigné dans `docs/refactoring/FINDINGS.md`, **non corrigé**.

Token et secret font 32 octets aléatoires encodés en 64 caractères hexadécimaux
(dérivé, `…RemoteUIProvisioningHandler.cpp:386`, `:396`, `:402-409` et
`src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:393-399`). La configuration est persistée par
`Config::Instance().SaveConfigIO()` (`…ProvisioningHandler.cpp:207`).

### Anti-abus

(dérivé, `src/bin/calaos_server/RemoteUI/RemoteUIProvisioningHandler.cpp:39-42`)

| Constante | Valeur |
|---|---|
| `RATE_LIMIT_SECONDS` | 10 |
| `MAX_CODES_PER_IP` | 10 |
| `TRACKING_WINDOW_SECONDS` | 3600 |
| `BLACKLIST_DURATION_SECONDS` | 3600 |

Réponses associées : `429 Too many requests or blacklisted` (`:133`),
`413 Request body too large` (`:96`), `413 Device info fields too long` (`:124`),
`404 Invalid provisioning code` (`:165`).

---

## HMACAuthenticator

**Fichier :** [src/bin/calaos_server/RemoteUI/HMACAuthenticator.h](../src/bin/calaos_server/RemoteUI/HMACAuthenticator.h)

Utilitaires d'authentification : extraction et normalisation des en-têtes
(`WebSocketHeaders::parse`, `findHeader`), génération de nonce (32 octets `RAND_bytes`),
décodage hexadécimal, et `constantTimeHexEquals()`.

⚠️ **Le sens de la dépendance est l'inverse de ce que ce document affirmait** : ce n'est pas
`RemoteUIManager` qui utilise `HMACAuthenticator`, c'est `HMACAuthenticator` qui appelle
`RemoteUIManager::Instance().validateAuthenticationWithReason(...)`
(dérivé, `src/bin/calaos_server/RemoteUI/HMACAuthenticator.cpp:84`). Le calcul du HMAC lui-même
vit dans `RemoteUI::validateHMAC()`
(dérivé, `src/bin/calaos_server/IO/RemoteUI/RemoteUI.cpp:401-423`).

---

## AuthFailureReason

**Fichier :** [src/bin/calaos_server/RemoteUI/AuthFailureReason.h](../src/bin/calaos_server/RemoteUI/AuthFailureReason.h)

Sept valeurs, avec leur chaîne et leur statut HTTP (dérivé,
`src/bin/calaos_server/RemoteUI/AuthFailureReason.h:31-40`, `:43-62`, `:65-86`) :

| Énumérateur | Chaîne | HTTP |
|---|---|---|
| `Success` | `success` | 200 |
| `MissingHeaders` | `missing_headers` | 400 |
| `InvalidToken` | `invalid_token` | 401 |
| `InvalidTimestamp` | `invalid_timestamp` | 401 |
| `InvalidNonce` | `invalid_nonce` | 401 |
| `InvalidHMAC` | `invalid_hmac` | 403 |
| `RateLimited` | `rate_limited` | 429 |
| *(défaut)* | `unknown_error` | 401 |

⚠️ **Ce n'est pas renvoyé dans un message `auth_fail`** — ce message n'existe pas. La valeur part
dans la **réponse HTTP d'échec du handshake WebSocket**, sous la clé `error` du corps JSON
(dérivé, `src/bin/calaos_server/WebSocket.cpp:293-333`).

---

## Écarts spec / code

Le document de spécification en arbre
[src/bin/calaos_server/RemoteUI/remote-ui.md](../src/bin/calaos_server/RemoteUI/remote-ui.md)
diverge du code sur trois points, vérifiés au source :

1. **`device_info` en XML** : la spec décrit des enfants
   `<calaos:param name=… value=…/>` (`remote-ui.md:534-540`) ; le code écrit et lit des
   **attributs** (`IO/RemoteUI/RemoteUI.cpp:157-158`, `:261-262`).
2. **Emplacement des appareils** : la spec les place dans une section `<calaos:remote_uis>` sous
   `<calaos:home>` (`remote-ui.md:524-526`) ; le chargeur lit `<calaos:remote_ui>` comme enfant d'une
   **pièce** (`Room.cpp:171`).
3. **Clé du code de provisioning** : `provisioning_code` dans la spec (`remote-ui.md:43-46`), `code`
   dans le code (`RemoteUIProvisioningHandler.cpp:117`, `:246`).

Une quatrième incohérence est **interne au code** : la chaîne de type d'IO cherchée n'est pas la
même partout — `RemoteUIManager` accepte `"RemoteUI"` ou `"remote_ui_output"`
(`RemoteUIManager.cpp:90-91`, `:117-118`) tandis que le handler de provisioning accepte
`"RemoteUI"` ou `"remote_ui"` (`RemoteUIProvisioningHandler.cpp:151`). `REGISTER_IO(RemoteUI)`
(`IO/RemoteUI/RemoteUI.cpp:84`) fixe la valeur réelle à **`"RemoteUI"`**, donc les branches
alternatives sont mortes.

Ces quatre écarts sont consignés dans `docs/refactoring/FINDINGS.md` ; ils sont **hors périmètre**
de la mise à jour documentaire.

---

## Tests

| Suite | Couvre |
|---|---|
| [tests/core/RemoteUIDeviceInfo_test.cpp](../tests/core/RemoteUIDeviceInfo_test.cpp) | T3.15, 4 tests : `DeviceInfoSurvivesSaveAndReload` (`:167`), `LegacyDeviceInfoUnderRoomIsAdoptedAndRewritten` (`:203`), `LegacyOrphanGoesToItsOwnDeviceOnly` (`:234`), `NoDeviceInfoWritesNoElement` (`:264`). Le fixture vérifie **le placement** : il cherche `calaos:device_info` et exige que son parent soit `calaos:remote_ui` (`:129`, `:257`) |
| [tests/RemoteUIHmac_test.cpp](../tests/RemoteUIHmac_test.cpp) | 8 tests sur les helpers d'auth inline : MAC correct accepté, hexadécimal majuscule accepté, MAC divergent rejeté, **inversion d'un seul bit** rejetée, longueur incorrecte rejetée, hexadécimal malformé rejeté (`:83-137`), aller-retour `hexDecode` (`:151`), `findHeader` exact/minuscule/absent (`:173`) |
| [tests/RemoteUIOtaLifecycle_test.cpp](../tests/RemoteUIOtaLifecycle_test.cpp) | 9 tests T1.10 : `parseGridDimension` sur valeurs valides, malformées (sans lever), à ordures en fin de chaîne, hors bornes (`:54-79`) ; `computeRescanIntervalMs` nominal, borne basse, **absence de débordement signé au-delà de la limite int32**, borne haute à 30 jours, jamais zéro (`:90-120`) |

⚠️ Les corrections d'usage-après-libération de T1.10 **ne sont pas testables unitairement** sans
boucle d'événements ; elles sont couvertes par revue de code et scénarios AddressSanitizer
(dérivé, `tests/RemoteUIOtaLifecycle_test.cpp:33-36`).
