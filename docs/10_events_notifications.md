# Events & Notifications — EventManager, Push, E-mail

## Vue d'ensemble

Deux mécanismes de communication sortante, sans rapport l'un avec l'autre :

1. **EventManager** — diffusion interne des changements d'état vers les clients
   connectés (WebSocket, et HTTP via `poll_listen`).
2. **NotifManager** — notifications externes vers l'utilisateur (push mobile,
   e-mail).

Le premier est un protocole ; il est décrit ici jusqu'à la forme exacte des
octets. Pour le reste de l'API JSON (requêtes/réponses), voir
[08_http_api.md](08_http_api.md).

---

## L'enveloppe WebSocket réelle

**Fichiers :** [JsonApiHandlerWS.cpp:53-73](../src/bin/calaos_server/JsonApiHandlerWS.cpp),
[EventManager.cpp:185-196](../src/bin/calaos_server/EventManager.cpp)

Un événement poussé est un message **non sollicité** : il n'a **pas** de
`msg_id`, puisqu'il ne répond à aucune requête (`sendJson()` n'ajoute `msg_id`
que si un `client_id` non vide lui est passé, et `handleEvents()` n'en passe
aucun).

```json
{
  "msg": "event",
  "data": {
    "event_raw": "io_changed id:io_0 state:true",
    "type": "3",
    "type_str": "io_changed",
    "data": {"id": "io_0", "state": "true"}
  }
}
```
*(dérivé de [JsonApiHandlerWS.cpp:53-73](../src/bin/calaos_server/JsonApiHandlerWS.cpp)
et [EventManager.cpp:185-196](../src/bin/calaos_server/EventManager.cpp) —
aucun golden ne capture d'événement.)*

Forme, champ par champ :

| Champ | Type sur le fil | Contenu |
|---|---|---|
| `msg` | chaîne | toujours `"event"` |
| `msg_id` | — | **jamais présent** |
| `data.event_raw` | chaîne | représentation **plate url-encodée**, voir plus bas — ce n'est **pas** du JSON |
| `data.type` | **chaîne** | l'entier de l'énumération, stringifié (`Utils::to_string(getType())`) |
| `data.type_str` | chaîne | le nom court, voir la table des types |
| `data.data` | objet | les paramètres de l'événement, **toutes valeurs chaînes** (`Params::toJson()`) |

Le double niveau de `data` n'est pas une erreur de lecture : `data` (enveloppe
WS) contient un objet qui contient lui-même un `data` (les paramètres de
l'événement).

### Le format `event_raw`

`CalaosEvent::toString()`
([EventManager.cpp:198-214](../src/bin/calaos_server/EventManager.cpp))
concatène le `type_str` puis, pour chaque paramètre, `" " + url_encode(clé) +
":" + url_encode(valeur)`. C'est un format **plat**, hérité, à parser
soi-même ; il porte exactement la même information que `data.data`, mais
url-encodée et sans structure. Les paramètres suivent l'ordre alphabétique des
clés (`Params` est une `map`).

```
io_changed id:io_0 state:true
audio_volume_changed player_id:e40_player volume:42
room_changed new_room_name:Salon%20Sud old_room_name:Salon room_type:salon
```

Un client moderne doit lire `data.data` et ignorer `event_raw`.

### Filtrage : rien n'est envoyé avant le login

```cpp
void JsonApiHandlerWS::handleEvents(const CalaosEvent &event)
{
    if (!loggedin)
        return;
    ...
}
```
([JsonApiHandlerWS.cpp:53-61](../src/bin/calaos_server/JsonApiHandlerWS.cpp))

Tant que la session n'est pas authentifiée (`login` ou `login_service`), **aucun
événement n'est poussé** et rien n'est mis en attente : les événements survenus
avant le login sont perdus pour ce client. Il n'y a **pas** d'abonnement à
souscrire ni de filtre par type : dès que la session est authentifiée, elle
reçoit **tous** les événements. Une session `serviceScope` (sidecar MCP) les
reçoit aussi — le filtrage `scopeDenied` ne porte que sur les requêtes
entrantes.

---

## L'équivalent HTTP : `poll_listen`

**Fichiers :** [PollListenner.cpp](../src/bin/calaos_server/PollListenner.cpp),
[JsonApiHandlerHttp.cpp:388-427](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)

Un client HTTP ne peut rien recevoir spontanément. Il ouvre donc une file
d'événements côté serveur, et vient la vider périodiquement. Trois sous-actions
(`action: "poll_listen"`, clé `type`) :

```json
// 1. Enregistrement
{"action": "poll_listen", "type": "register", "cn_user": "…", "cn_pass": "…"}
// → {"uuid":"<uuid v4>"}

// 2. Récupération (à répéter)
{"action": "poll_listen", "type": "get", "uuid": "<uuid>", "cn_user": "…", "cn_pass": "…"}

// 3. Libération
{"action": "poll_listen", "type": "unregister", "uuid": "<uuid>", "cn_user": "…", "cn_pass": "…"}
// → {"success":"true"} ou {"success":"false"} si l'uuid est inconnu
```
*(dérivé de [JsonApiHandlerHttp.cpp:388-427](../src/bin/calaos_server/JsonApiHandlerHttp.cpp).)*

La réponse à `get` est **plate** : ni `msg`, ni enveloppe `data`, juste un
tableau des objets événements accumulés depuis le dernier appel.

```json
{
  "success": "true",
  "events": [
    {"event_raw": "io_changed id:io_0 state:true",
     "type": "3", "type_str": "io_changed",
     "data": {"id": "io_0", "state": "true"}},
    {"event_raw": "audio_volume_changed player_id:e40_player volume:42",
     "type": "20", "type_str": "audio_volume_changed",
     "data": {"player_id": "e40_player", "volume": "42"}}
  ]
}
```
*(dérivé de [JsonApiHandlerHttp.cpp:403-424](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) ;
les éléments du tableau sont produits par le même `CalaosEvent::toJson()` que la
voie WebSocket, donc identiques champ pour champ.)*

Points de comportement, tous vérifiés dans `PollListenner.cpp` :

- Un `uuid` inconnu (ou expiré) donne `{"success":"false"}` **sans** clé
  `events` ([PollListenner.cpp:108-114](../src/bin/calaos_server/PollListenner.cpp)).
- Chaque `get` **vide** la file et **réarme** le minuteur
  ([PollListenner.cpp:116-122](../src/bin/calaos_server/PollListenner.cpp)).
- Sans `get` pendant `TIMEOUT_POLLLISTENNER` = **300 s**, l'enregistrement est
  détruit ([PollListenner.h:28](../src/bin/calaos_server/PollListenner.h),
  [PollListenner.cpp:53-64](../src/bin/calaos_server/PollListenner.cpp)). Le
  `uuid` devient alors invalide et il faut se réenregistrer.
- La file n'est **pas bornée** : un client qui s'enregistre puis ne consomme pas
  fait croître la liste jusqu'au timeout.
- Un `type` absent ou inconnu renvoie `{}`.
- Ce n'est **pas** du long polling : `get` répond immédiatement, avec un tableau
  éventuellement vide.

---

## Table complète des types d'événements

Dérivée de l'énumération
([EventManager.h:37-71](../src/bin/calaos_server/EventManager.h)), de
`typeToString()`
([EventManager.cpp:141-183](../src/bin/calaos_server/EventManager.cpp)) et de
**tous** les appels à `EventManager::create()` du dépôt.

`type` sur le fil est la valeur entière de la colonne « n° », sous forme de
chaîne. `EventUnkown` (0) n'est jamais diffusé : `appendEvent()` le jette avec
un avertissement ([EventManager.cpp:38-42](../src/bin/calaos_server/EventManager.cpp)).

| n° | Constante | `type_str` | Clés de `data` | État |
|---:|---|---|---|---|
| 1 | `EventIOAdded` | `io_added` | `id`, `room_name`, `room_type` | ✅ [ListeRoom.cpp:466](../src/bin/calaos_server/ListeRoom.cpp) |
| 2 | `EventIODeleted` | `io_deleted` | `id`, `room_name`, `room_type` | ✅ [Room.cpp:77](../src/bin/calaos_server/Room.cpp) |
| 3 | `EventIOChanged` | `io_changed` | `id` + `state`, ou `id` + la propriété modifiée | ✅ ~40 sites (tous les IO, `set_param`) |
| 4 | `EventIOPropertyDelete` | `io_prop_deleted` | `id`, `param` | ✅ [JsonApi.cpp:726](../src/bin/calaos_server/JsonApi.cpp) |
| 5 | `EventRoomAdded` | `room_added` | — | ❌ **MORT** |
| 6 | `EventRoomDeleted` | `room_deleted` | — | ❌ **MORT** |
| 7 | `EventRoomChanged` | `room_changed` | variable, voir ci-dessous | ✅ [Room.cpp:114,123,133,143](../src/bin/calaos_server/Room.cpp), [JsonApi.cpp:1918](../src/bin/calaos_server/JsonApi.cpp) |
| 8 | `EventRoomPropertyDelete` | `room_prop_deleted` | — | ❌ **MORT** |
| 9 | `EventTimeRangeChanged` | `timerange_changed` | `id` | ✅ [JsonApi.cpp:1670](../src/bin/calaos_server/JsonApi.cpp) |
| 10 | `EventScenarioAdded` | `scenario_added` | `id` | ✅ [JsonApi.cpp:1797](../src/bin/calaos_server/JsonApi.cpp) |
| 11 | `EventScenarioDeleted` | `scenario_deleted` | `id` | ✅ [JsonApi.cpp:1823](../src/bin/calaos_server/JsonApi.cpp) |
| 12 | `EventScenarioChanged` | `scenario_changed` | `id` | ✅ [JsonApi.cpp:1675,1950,1973,1996](../src/bin/calaos_server/JsonApi.cpp) |
| 13 | `EventAudioSongChanged` | `audio_song_changed` | `player_id` | ✅ [Squeezebox.cpp:268](../src/bin/calaos_server/Audio/Squeezebox.cpp), [RoonPlayer.cpp:276](../src/bin/calaos_server/Audio/RoonPlayer.cpp) |
| 14 | `EventAudioPlaylistAdd` | `playlist_tracks_added` | `player_id` | ✅ [Squeezebox.cpp:303](../src/bin/calaos_server/Audio/Squeezebox.cpp) |
| 15 | `EventAudioPlaylistDelete` | `playlist_tracks_deleted` | `player_id`, `position` | ✅ [Squeezebox.cpp:286](../src/bin/calaos_server/Audio/Squeezebox.cpp) |
| 16 | `EventAudioPlaylistMove` | `playlist_tracks_moved` | `player_id`, `from`, `to` | ✅ [Squeezebox.cpp:276](../src/bin/calaos_server/Audio/Squeezebox.cpp) |
| 17 | `EventAudioPlaylistReload` | `playlist_reload` | `player_id` | ✅ [Squeezebox.cpp:295](../src/bin/calaos_server/Audio/Squeezebox.cpp) |
| 18 | `EventAudioPlaylistCleared` | `playlist_cleared` | `player_id` | ❌ **INATTEIGNABLE**, voir ci-dessous |
| 19 | `EventAudioStatusChanged` | `audio_status_changed` | `player_id`, `state` (`play`/`pause`/`stop`) | ✅ [Squeezebox.cpp:328,337](../src/bin/calaos_server/Audio/Squeezebox.cpp), [RoonPlayer.cpp:300](../src/bin/calaos_server/Audio/RoonPlayer.cpp) |
| 20 | `EventAudioVolumeChanged` | `audio_volume_changed` | `player_id`, `volume` | ✅ [Squeezebox.cpp:349](../src/bin/calaos_server/Audio/Squeezebox.cpp), [RoonPlayer.cpp:310](../src/bin/calaos_server/Audio/RoonPlayer.cpp) |
| 21 | `EventTouchScreenCamera` | `touchscreen_camera_request` | `id` (la caméra) | ✅ [ActionTouchscreen.cpp:49](../src/bin/calaos_server/Rules/ActionTouchscreen.cpp) |
| 22 | `EventPushNotification` | `push_notif` | — | ❌ **MORT** comme événement (mais utilisé en base, voir ci-dessous) |
| 23 | `EventIOStatusChanged` | `io_status_changed` | `id` + les champs de `getStatusInfo()` | ✅ [MqttCtrl.cpp:625…789](../src/bin/calaos_server/IO/Mqtt/MqttCtrl.cpp) (10 sites) |

**23 types déclarés, 18 réellement émis, 5 inatteignables.**

Un `type_str` inconnu ne peut pas apparaître sur le fil : `typeToString()`
renvoie `"unkown"` en dernier recours, mais seul `EventUnkown` y mènerait, et
celui-là est jeté avant diffusion.

### Les 4 types morts

`EventRoomAdded` (5), `EventRoomDeleted` (6), `EventRoomPropertyDelete` (8) et
`EventPushNotification` (22) sont déclarés dans l'énumération et traduits par
`typeToString()`, mais **aucun `EventManager::create()` du dépôt ne les
utilise**. Aucun client ne les recevra jamais. En particulier : créer ou
supprimer une pièce ne notifie personne ; seules les modifications de pièce
existante (`room_changed`) le font.

Nuance importante pour `EventPushNotification` : il est mort **comme événement
diffusé**, mais sa valeur `22` est écrite comme `event_type` dans le journal
d'historique par `ActionPush::sendNotif()`
([ActionPush.cpp:103-119](../src/bin/calaos_server/Rules/ActionPush.cpp)). Un
client qui lit `eventlog` verra donc des lignes `event_type: "22"` — dont le
`event_raw` a une forme **différente** de celle des événements IO :
`{"message": "…", "pic_uid": "…"}`.

### Le 5ᵉ : `playlist_cleared`, masqué par une branche antérieure

`EventAudioPlaylistCleared` (18) **a** un site d'émission
([Squeezebox.cpp:306-313](../src/bin/calaos_server/Audio/Squeezebox.cpp)), mais
il est inatteignable. Le mécanisme exact :

```cpp
else if (p["2"] == "loadtracks" || p["2"] == "clear" ||
         p["2"] == "play" || p["2"] == "load")           // ligne 290
{
    ...
    EventManager::create(CalaosEvent::EventAudioPlaylistReload, ...);
}
else if (p["2"] == "addtracks" || p["2"] == "add")       // ligne 298
{ ... }
else if (p["2"] == "clear")                              // ligne 306 — jamais atteinte
{
    ...
    EventManager::create(CalaosEvent::EventAudioPlaylistCleared, ...);
}
```

La notification `playlist clear` du serveur Squeezebox correspond au test
`p["2"] == "clear"` de la **ligne 290**, qui fait partie d'une chaîne `else if`
antérieure : elle est consommée là, et émet `playlist_reload` (17). Le
`else if (p["2"] == "clear")` de la ligne 306 ne peut donc jamais s'exécuter.
**Un vidage de playlist arrive au client sous le type `playlist_reload`, jamais
sous `playlist_cleared`.** Un client ne doit pas attendre le type 18.

### `room_changed` : des clés différentes selon la cause

Contrairement aux autres, `room_changed` ne porte pas un jeu de clés stable —
chaque site d'émission a le sien :

| Cause | Clés de `data` |
|---|---|
| IO retiré d'une pièce ([Room.cpp:114](../src/bin/calaos_server/Room.cpp)) | `input_id_deleted`, `room_name`, `room_type` |
| Renommage ([Room.cpp:123](../src/bin/calaos_server/Room.cpp)) | `old_room_name`, `new_room_name`, `room_type` |
| Changement de type ([Room.cpp:133](../src/bin/calaos_server/Room.cpp)) | `old_room_type`, `new_room_type`, `room_name` |
| Changement de `hits` ([Room.cpp:143](../src/bin/calaos_server/Room.cpp)) | `old_room_hits`, `new_room_hits`, `room_name`, `room_type` |
| IO déplacé vers une pièce ([JsonApi.cpp:1918](../src/bin/calaos_server/JsonApi.cpp)) | `io_id_added`, `room_name`, `room_type` |

Noter l'asymétrie de nommage entre `input_id_deleted` et `io_id_added` : ce sont
bien deux clés distinctes, pas une faute de frappe de ce document.

### `io_status_changed` : les clés dépendent de ce que l'IO sait

Les paramètres sont ceux de `IOBase::getStatusInfo()`
([IOBase.cpp:349-363](../src/bin/calaos_server/IOBase.cpp)), qui n'ajoute une
clé que si l'information est disponible : `battery_level`, `connected`
(`"true"`/`"false"`), `wireless_signal`, `uptime`, `ip_address`, `wifi_ssid` —
plus `id`, ajouté par la surcharge à trois arguments de
`EventManager::create()`
([EventManager.cpp:124-135](../src/bin/calaos_server/EventManager.cpp)). Le même
jeu de clés se retrouve dans l'objet `status_info` de `get_io` / `get_home`.

---

## EventManager

**Fichier :** [src/bin/calaos_server/EventManager.h](../src/bin/calaos_server/EventManager.h)

Singleton. Point central de dispatch de tous les événements Calaos vers les
clients connectés.

### Création d'un événement

```cpp
// Événement sans paramètre
EventManager::create(CalaosEvent::EventIOChanged);

// Avec paramètres
EventManager::create(CalaosEvent::EventIOChanged, { {"id", ioId},
                                                    {"state", "true"} });

// Avec un id d'IO ajouté d'office sous la clé "id"
EventManager::create(CalaosEvent::EventIOStatusChanged, ioId, io->getStatusInfo());

// Sans log dans l'historique (4e/3e argument logHistory)
EventManager::create(CalaosEvent::EventIOChanged, p, false);
```

La diffusion n'est **pas** synchrone : `appendEvent()` empile l'événement et
arme un `Idler` qui vide la file au tour de boucle suivant
([EventManager.cpp:36-59](../src/bin/calaos_server/EventManager.cpp)). L'ordre
des événements est préservé, mais un client ne peut pas supposer qu'un événement
est arrivé avant la réponse à la requête qui l'a provoqué.

### Réception

```cpp
EventManager::Instance().newEvent.connect(
    [](const CalaosEvent &ev) { /* … */ }
);
```

Deux abonnés dans le serveur : `JsonApiHandlerWS` (un par connexion WebSocket,
[JsonApiHandlerWS.cpp:37](../src/bin/calaos_server/JsonApiHandlerWS.cpp)) et
`PollObject` (un par enregistrement `poll_listen`,
[PollListenner.cpp:32](../src/bin/calaos_server/PollListenner.cpp)).

### CalaosEvent

```cpp
class CalaosEvent {
    int evType;         // valeur de l'énumération
    Params evParams;    // paramètres, toujours chaîne → chaîne
    bool logHistory;    // si true, candidat au HistLogger

    json_t *toJson() const;   // {event_raw, type, type_str, data}
    string toString() const;  // la forme plate url-encodée
};
```

---

## HistLogger

**Fichier :** [src/bin/calaos_server/HistLogger.h](../src/bin/calaos_server/HistLogger.h)

Journalise les événements dans une base SQLite locale (`events.db` dans le
répertoire de cache).

Le filtre d'écriture est **beaucoup plus étroit** que la diffusion
([EventManager.cpp:61-98](../src/bin/calaos_server/EventManager.cpp)) : un
événement n'est journalisé que si

1. son type est `EventIOChanged`, **et**
2. ses paramètres contiennent la clé `state`, **et**
3. `logHistory` est vrai, **et**
4. l'IO existe encore et porte le paramètre `log_history == "true"`.

Autrement dit : les 17 autres types d'événements ne sont **jamais** journalisés.
La seule autre écriture en base vient de `ActionPush`, qui insère directement un
`HistEvent` de type 22 (voir plus haut).

```cpp
HistLogger::Instance().appendEvent(e);
```

Consultable via l'API JSON : action/message `eventlog` → `buildJsonEventLog()`.
Voir [08_http_api.md](08_http_api.md) pour la forme de la réponse — c'est la
seule de l'API à contenir de vrais entiers JSON.

---

## DataLogger

**Fichier :** [src/bin/calaos_server/DataLogger.h](../src/bin/calaos_server/DataLogger.h)

Enregistre les valeurs des IOs dans le temps (pour graphes et statistiques).
Indépendant du HistLogger et de l'EventManager.

---

## NotifManager

**Fichier :** [src/bin/calaos_server/NotifManager.h](../src/bin/calaos_server/NotifManager.h)

Singleton pour l'envoi de notifications externes. **Il n'émet aucun
`CalaosEvent`** : une notification partie ne se voit pas sur le fil WebSocket.

### E-mail

```cpp
NotifManager::Instance().sendMailNotification(
    "Alarme détectée",              // sujet
    "Mouvement détecté en entrée",  // corps
    "user@example.com",             // destinataire (optionnel)
    "calaos@maison.local",          // expéditeur (optionnel)
    "/tmp/snapshot.jpg"             // pièce jointe (optionnelle)
);
```

L'envoi n'est **pas** fait en process : le corps est écrit dans un fichier
temporaire et le binaire **`calaos_mail`** est lancé avec `--from`, `--to`,
`--subject`, `--body` (et `--attach` le cas échéant)
([NotifManager.cpp:36-142](../src/bin/calaos_server/NotifManager.cpp)). C'est ce
binaire qui parle SMTP, via `libquickmail`
([src/bin/tools/calaos_mail.cpp](../src/bin/tools/calaos_mail.cpp),
[src/lib/libquickmail/](../src/lib/libquickmail/)).

Options de `local_config.xml` réellement lues :

| Clé | Lue par | Rôle |
|---|---|---|
| `notif/mail_sender` | NotifManager | expéditeur par défaut (repli : `calaos@localhost`) |
| `notif/mail_recipients` | NotifManager | destinataires par défaut — **sans elle, aucun mail n'est envoyé** |
| `smtp_debug` | NotifManager | passe `--verbose` à `calaos_mail` |
| `smtp_server`, `smtp_port` | calaos_mail | serveur SMTP |
| `smtp_auth`, `smtp_tls` | calaos_mail | authentification / TLS |
| `smtp_username`, `smtp_password` | calaos_mail | identifiants SMTP |

### Push (notifications mobiles)

```cpp
NotifManager::Instance().sendPushNotification(
    "Lumière allumée",   // message (défaut : "Calaos Notification" si vide)
    "uuid-de-l-evenement", // uuid d'un HistEvent portant l'image (optionnel)
    []() { /* callback envoi terminé */ }
);
```

Les tokens des appareils sont enregistrés via `JsonApi::registerPushToken()`
(action/message `register_push`), qui exige `token` non vide et `hardware`
valant exactement `android` ou `ios` — sinon il renvoie `false` et l'API répond
`{"success":"false"}`
([JsonApi.cpp:2062-2074](../src/bin/calaos_server/JsonApi.cpp)). Les tokens sont
stockés dans la même base SQLite que l'historique.

L'option `notif_development` de `local_config.xml` bascule les notifications iOS
en environnement de développement
([NotifManager.cpp:194-196](../src/bin/calaos_server/NotifManager.cpp)).

---

## Intégration dans les règles

Les notifications sont déclenchées par les actions de règles :

- `ActionMail` → `NotifManager::sendMailNotification()`
  ([ActionMail.cpp:89](../src/bin/calaos_server/Rules/ActionMail.cpp))
- `ActionPush` → `NotifManager::sendPushNotification()`
  ([ActionPush.cpp:124](../src/bin/calaos_server/Rules/ActionPush.cpp))
- `ActionTouchscreen` → émet l'événement `touchscreen_camera_request`
  ([ActionTouchscreen.cpp:49](../src/bin/calaos_server/Rules/ActionTouchscreen.cpp))

Forme XML réelle d'une action push, dérivée de `SaveToXml()`
([ActionPush.cpp:150-158](../src/bin/calaos_server/Rules/ActionPush.cpp)) — le
message est le **texte** du nœud, et l'image est un **attribut** `attachment` :

```xml
<calaos:action type="push">
  <calaos:push attachment="id-cam-entree"><![CDATA[Mouvement détecté !]]></calaos:push>
</calaos:action>
```

Quand `attachment` est renseigné, une image est capturée avant l'envoi, stockée
sous un `pic_uid`, et le push transporte l'uuid de l'événement d'historique
correspondant ; l'image se récupère ensuite en HTTP par
`action: "event_picture"` avec ce `pic_uid` (voir
[08_http_api.md](08_http_api.md)).

Enfin, `IOBase` envoie lui-même des notifications de batterie faible et de perte
de connexion, sans passer par une règle
([IOBase.cpp:243-321](../src/bin/calaos_server/IOBase.cpp)).
