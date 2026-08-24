# Audio — Lecteurs et Amplis AV

> **Écrit contre le code et les goldens** (E4.5c). Chaque affirmation porte sa provenance :
> `(dérivé, Fichier.cpp:L-L)` pour une lecture du source, `(capturé, <golden>)` pour un extrait
> d'un fichier de référence de `tests/core/golden/`. Les chemins sont relatifs à `src/`.

## Vue d'ensemble

Le sous-système audio couvre **deux familles qui n'ont pas la même nature** :

| famille | classe IO | `gui_type` | apparaît dans `get_home` |
|---|---|---|---|
| Lecteurs réseau (Squeezebox/LMS, Roon) | `AudioPlayer` | `audio_player` | tableau **`audio`** |
| Amplis AV (Denon, Marantz, Onkyo, Pioneer, Yamaha, HiFi Rose) | `IOAVReceiver` | `avreceiver` | ni `audio` ni `cameras` |

(dérivé, [Audio/AudioPlayer.cpp:49](../src/bin/calaos_server/Audio/AudioPlayer.cpp),
[Audio/AVReceiver.cpp:265](../src/bin/calaos_server/Audio/AVReceiver.cpp).)

⚠️ **Un ampli AV n'est pas dans le cache audio.** `ListeRoom::addIOHash()` ne pousse dans
`audioCache` que les IOs de `gui_type == "audio_player"`
(dérivé, [ListeRoom.cpp:84-86](../src/bin/calaos_server/ListeRoom.cpp)), et c'est ce cache que
`buildJsonAudio()` parcourt
(dérivé, [JsonApi.cpp:393](../src/bin/calaos_server/JsonApi.cpp)). Un ampli est un IO ordinaire
de la pièce ; il n'est **rattaché** à un lecteur que par le paramètre `amp` de celui-ci
(voir plus bas).

⚠️ **`AVReceiver` n'hérite pas d'`IOBase`.** C'est un objet de transport partagé, sans identité
d'IO. L'IO, c'est `IOAVReceiver`
(dérivé, [Audio/AVReceiver.h:39,141](../src/bin/calaos_server/Audio/AVReceiver.h) :
`class AVReceiver` sans base, `class IOAVReceiver: public IOBase, public sigc::trackable`).

---

## AudioPlayer

**Fichier :** [src/bin/calaos_server/Audio/AudioPlayer.h](../src/bin/calaos_server/Audio/AudioPlayer.h)

Classe de base des lecteurs. `get_type()` rend `TSTRING`
(dérivé, [Audio/AudioPlayer.h:107](../src/bin/calaos_server/Audio/AudioPlayer.h)).

Le constructeur pose `gui_type = "audio_player"` et `visible = "false"`, puis **réinscrit l'IO**
dans les caches de `ListeRoom` parce que le `gui_type` a changé après la construction d'`IOBase` :

```cpp
// (dérivé, Audio/AudioPlayer.cpp:49-54, intégral)
    get_params().Add("gui_type", "audio_player");
    get_params().Add("visible", "false");

    //Add again to cache, because gui_type has changed
    //Special case for AudioPlayer
    ListeRoom::Instance().addIOHash(this);
```

⚠️ Cet `addIOHash()` est bien dans le **constructeur** — c'est lui qui fait entrer le lecteur dans
`audioCache`.

### Méthodes virtuelles (extrait)

```cpp
// (dérivé, Audio/AudioPlayer.h:46-105, extrait)
virtual void Play() { }
virtual void Pause() { }
virtual void Stop() { }
virtual void Next() { }
virtual void Previous() { }
virtual void Power(bool on) { }
virtual void Sleep(int seconds) { }
virtual void Synchronize(string playerid, bool sync) { }
virtual void getSynchronizeList(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }

virtual void get_volume(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
virtual void set_volume(int vol) { }
virtual Params getOptions() { Params p; return p; }

virtual void get_title(...);   virtual void get_artist(...);  virtual void get_album(...);
virtual void get_album_cover(...); virtual void get_genre(...); virtual void get_songinfo(...);
virtual void get_current_time(...); virtual void set_current_time(double seconds) { }
virtual void get_duration(...); virtual void get_sleep(...);
virtual void get_status(...);  virtual void get_sync_status(...);

// playlist : moveup, movedown, delete(int), play(int), play_artist, play_album, play_title,
// add_artist, add_album, add_title, add_items, play_items, clear, save, delete(string),
// get_playlist_current, get_playlist_size, get_playlist_item, get_playlist_basic_info,
// get_playlist_album_cover, get_album_cover_id

virtual bool canPlaylist() { return false; }
virtual bool canDatabase() { return false; }
virtual Params getDatabaseCapabilities() { Params p; return p; }
AudioDB *get_database() { return database; }
```

Toutes les méthodes de lecture sont **asynchrones** : elles ne rendent rien, elles rappellent un
`AudioRequest_cb`.

### `set_value(string)` — le vocabulaire de commande

C'est ce que reçoit une action de règle visant un lecteur. Les valeurs reconnues, **exactement**
(dérivé, [Audio/AudioPlayer.cpp:115-177](../src/bin/calaos_server/Audio/AudioPlayer.cpp)) :

| Valeur | Action |
|---|---|
| `play` | Lecture |
| `pause` | Pause |
| `stop` | Stop |
| `next` | Suivant |
| `previous` | Précédent |
| `power on` / `power off` | Mise sous/hors tension |
| `sleep <secondes>` | Extinction différée |
| `sync <playerid>` | Synchroniser avec un autre lecteur |
| `unsync <playerid>` | Désynchroniser |
| `play <argument>` | Vider la playlist et jouer `<argument>` (ex. `album_id:XX`, `artist_id:XX`, `playlist_id:XX`) |
| `add <argument>` | Ajouter à la playlist |
| **`volume set <n>`** | Volume absolu |
| **`volume up <n>`** | Augmenter de `n` |
| **`volume down <n>`** | Diminuer de `n` |

⚠️ **Les formes `volume+`, `volume-` et `volume 75` n'existent pas.** Le préfixe est
`volume set ` / `volume up ` / `volume down `, espace compris — c'est aussi ce que publie
l'`ioDoc` du lecteur, donc ce que propose calaos_installer
(dérivé, [Audio/AudioPlayer.cpp:43-45](../src/bin/calaos_server/Audio/AudioPlayer.cpp) :
`actionAdd("volume set 50" | "volume up 1" | "volume down 1")`).

⚠️ **Une valeur non reconnue ne produit aucune erreur** : elle ne déclenche rien, mais
l'événement `EventIOChanged` est émis **quand même**, avec la valeur brute
(dérivé, [Audio/AudioPlayer.cpp:179-183](../src/bin/calaos_server/Audio/AudioPlayer.cpp) —
l'émission est hors de la chaîne de `else if`). Une action mal orthographiée est donc
silencieuse côté serveur et visible côté client.

`volume up` / `volume down` sont **relatifs** : ils demandent d'abord le volume courant au
lecteur, puis appliquent le delta dans le callback
(dérivé, [Audio/AudioPlayer.cpp:162-177,186-191](../src/bin/calaos_server/Audio/AudioPlayer.cpp)).

### Conditions et actions publiées à l'installeur

Conditions : `onplay`, `onpause`, `onstop`, `onsongchange`, `onplaylistchange`, `onvolumechange`
(dérivé, [Audio/AudioPlayer.cpp:31-36](../src/bin/calaos_server/Audio/AudioPlayer.cpp)).
`hasChanged()` traduit le statut interne en l'une de ces chaînes, plus `onerror` par défaut, et
émet `EventIOChanged` avec `{id, state}`
(dérivé, [Audio/AudioPlayer.cpp:75-98](../src/bin/calaos_server/Audio/AudioPlayer.cpp)).

### AudioPlayerData

**Fichier :** [src/bin/calaos_server/Audio/AudioPlayerData.h](../src/bin/calaos_server/Audio/AudioPlayerData.h)

Conteneur de résultat des callbacks asynchrones. Ses champs publics, **au complet** :

```cpp
// (dérivé, Audio/AudioPlayerData.h:84-88, intégral)
    Params params;
    vector<Params> vparams;
    int ivalue = 0, ivalue2 = 0;
    string svalue;
    double dvalue = 0.0;
```

```cpp
// (dérivé, Audio/AudioPlayerData.h:31-32, intégral)
typedef sigc::slot<void, AudioPlayerData> AudioRequest_cb;
typedef sigc::signal<void, AudioPlayerData> AudioRequest_signal;
```

⚠️ Il n'y a **ni `isSuccess` ni `vectData`** : les listes de résultats passent par `vparams`
(un `Params` par ligne) et le succès se lit dans le `status` remis séparément par la couche de
transport. `AudioPlayerData` porte en outre un **chaînage privé** (`chain_data`), copié en
profondeur par le constructeur de copie et l'affectation, et détruit avec l'objet
(dérivé, [Audio/AudioPlayerData.h:37,55-58,71-74,79-82](../src/bin/calaos_server/Audio/AudioPlayerData.h)).

---

## Squeezebox / Logitech Media Server

**Fichiers :** [Audio/Squeezebox.h](../src/bin/calaos_server/Audio/Squeezebox.h),
[Audio/SqueezeboxDB.h](../src/bin/calaos_server/Audio/SqueezeboxDB.h)

Interface avec LMS par son API CLI (TCP). **Deux types XML** :
`Squeezebox` et l'alias historique `slim`
(dérivé, [Audio/Squeezebox.cpp:36-37](../src/bin/calaos_server/Audio/Squeezebox.cpp)).

### Paramètres

| Paramètre | Obligatoire | Description |
|---|---|---|
| `host` | oui | IP du serveur LMS |
| **`id`** | oui | Identifiant unique du player dans LMS |
| `port_cli` | non, défaut **9090** | Port CLI de LMS |
| `port_web` | non, défaut **9000** | Port de l'interface web de LMS |

(dérivé, [Audio/Squeezebox.cpp:48-51](../src/bin/calaos_server/Audio/Squeezebox.cpp).)

⚠️ **Il n'existe pas de paramètre `playerid`** : la clé est `id`.

⚠️ **`port` est une clé héritée, et elle disparaît en silence.** Si `port_cli` est absent, le
constructeur lit `port` comme port CLI, force `port_web` à 9000 et **écrit** `port_cli` et
`port_web` dans les paramètres
(dérivé, [Audio/Squeezebox.cpp:62-73](../src/bin/calaos_server/Audio/Squeezebox.cpp)).
La suppression de `port`, elle, est **inconditionnelle** : elle est hors de la branche de
migration et s'applique donc **aussi** à une configuration déjà pourvue de `port_cli`
(dérivé, [Audio/Squeezebox.cpp:74-75](../src/bin/calaos_server/Audio/Squeezebox.cpp) :
`if (param.Exists("port")) param.Delete("port");`). La prochaine sauvegarde de la configuration
ne contient donc plus `port`, dans tous les cas.

### Capacités

`Squeezebox` est **le seul lecteur qui fournit une base musicale** :
`canPlaylist()` et `canDatabase()` rendent `true`
(dérivé, [Audio/Squeezebox.h:203-204](../src/bin/calaos_server/Audio/Squeezebox.h)), et le
constructeur instancie `SqueezeboxDB`
(dérivé, [Audio/Squeezebox.cpp:81](../src/bin/calaos_server/Audio/Squeezebox.cpp)).

### Fiabilité de la file de commandes

Les commandes CLI sont **sérialisées dans une file**, une seule en vol à la fois, avec un
minuteur de garde de **40 s** (`SQ_TIMEOUT`) et une reconnexion à **3 s** (`SQ_RECONNECT`)
(dérivé, [Audio/Squeezebox.cpp:30-31,459-460](../src/bin/calaos_server/Audio/Squeezebox.cpp)).

Trois défauts corrigés, tous observables :

- **Un timeout ne bloque plus la file.** L'ancien code sortait tôt du traitement d'échec, ce qui
  laissait `inProgress` posé pour toujours sur la commande de tête : **toute la file était
  gelée après un seul dépassement**. Le traitement retombe désormais dans le tronc commun, la
  commande est échouée, dépilée, et la file est relancée
  (dérivé, [Audio/Squeezebox.cpp:386-416](../src/bin/calaos_server/Audio/Squeezebox.cpp)).
- **Le callback en échec est désormais appelé.** Il ne l'était **jamais** : l'appelant restait en
  attente indéfiniment. Il est maintenant émis avec `status = false`
  (dérivé, [Audio/Squeezebox.cpp:398-410](../src/bin/calaos_server/Audio/Squeezebox.cpp)).
- **La reconnexion est effective.** `close()` seul ne déclenchait aucune reconnexion (la logique
  était accrochée aux seuls événements `Error`/`End`) : les deux connexions sont maintenant
  fermées et deux reconnexions sont planifiées explicitement
  (dérivé, [Audio/Squeezebox.cpp:365-384](../src/bin/calaos_server/Audio/Squeezebox.cpp)). À la
  reconnexion, une commande restée marquée en cours est **redéclenchée** au lieu d'attendre son
  timeout (dérivé, [Audio/Squeezebox.cpp:175-183](../src/bin/calaos_server/Audio/Squeezebox.cpp)).

### SqueezeboxDB

Sous-classe d'`AudioDB` pour la base musicale LMS : albums, artistes, genres, années, playlists,
radios, dossiers, recherche.

⚠️ **Le marqueur `count` n'est pas à une position fixe** dans le flux de résultats.
`parseListAnswer()` le traite comme n'importe quel autre champ, sa position dépend donc de la
réponse du serveur ; `getRandoms()` le place **en dernier**
(dérivé, [Audio/SqueezeboxDB.cpp:66-73,774-775](../src/bin/calaos_server/Audio/SqueezeboxDB.cpp)).
Un client **ne doit pas supposer** qu'il arrive en premier. Le comportement observable côté API
est décrit dans [08_http_api.md](08_http_api.md).

---

## RoonPlayer

**Fichier :** [src/bin/calaos_server/Audio/RoonPlayer.h](../src/bin/calaos_server/Audio/RoonPlayer.h)

Intégration Roon via le sous-processus `calaos_roon`
(dérivé, [Audio/RoonPlayer.cpp:34-35](../src/bin/calaos_server/Audio/RoonPlayer.cpp), source
Python `ExternProcRoon_main.py`).

**Type XML : `Roon`** — l'alias, pas le nom de classe
(dérivé, [Audio/RoonPlayer.cpp:29](../src/bin/calaos_server/Audio/RoonPlayer.cpp) :
`REGISTER_IO_USERTYPE(Roon, RoonPlayer)`).

| Paramètre | Obligatoire | Description |
|---|---|---|
| `zone_id` | oui | Identifiant de zone Roon |
| `host` | non | IP du serveur Roon ; vide = autodétection réseau |
| `port` | **oui, sans défaut** — voir l'avertissement ci-dessous | Port du serveur Roon |

(dérivé, [Audio/RoonPlayer.cpp:172-174](../src/bin/calaos_server/Audio/RoonPlayer.cpp).)

⚠️ **`port` est déclaré obligatoire et sans défaut — le « 9330 » de la ligne de code est un
argument mal placé.** La signature est
`paramAdd(name, description, ParamType type, bool mandatory, string defaultval = "", bool readonly = false)`
(dérivé, [IO/IODoc.h:46](../src/bin/calaos_server/IO/IODoc.h)) : dans
`paramAdd("port", …, IODoc::TYPE_INT, 9330)` le `9330` occupe la place de **`mandatory`** et vaut
donc `true`, tandis que `defaultval` reste **vide**
(dérivé, [Audio/RoonPlayer.cpp:174](../src/bin/calaos_server/Audio/RoonPlayer.cpp)).

Conséquence à l'exécution : `Utils::from_string("")` laisse `port` à **0**
(dérivé, [Audio/RoonPlayer.cpp:179](../src/bin/calaos_server/Audio/RoonPlayer.cpp)), et si `host`
est renseigné c'est **`--port 0`** qui part au sidecar
(dérivé, [Audio/RoonPlayer.cpp:46-48](../src/bin/calaos_server/Audio/RoonPlayer.cpp)). Le défaut
9330 n'existe que **côté Python, quand le drapeau est absent** — ce qui n'arrive jamais dès lors
que `host` est posé. **Renseigner explicitement `port` si l'on renseigne `host`.** Consigné dans
`docs/refactoring/FINDINGS.md`.

⚠️ **Roon ne fournit ni playlist ni base musicale** : `canPlaylist()` et `canDatabase()` rendent
`false` (dérivé, [Audio/RoonPlayer.h:176-177](../src/bin/calaos_server/Audio/RoonPlayer.h)).
C'est visible dans la maison de référence :

```json
// (capturé, tests/core/golden/ws_get_home.json, extrait — élément du tableau "audio")
      {
        "avr": "e40_avr",
        "database": "false",
        "id": "e40_player",
        "name": "Player",
        "playlist": "false",
        "type": "Roon"
      }
```

---

## Consulter la base musicale d'un lecteur qui n'en a pas : refusé, plus de plantage (T3.19)

C'était un **arrêt net de `calaos_server`, atteignable par tout client authentifié**. Les seize
commandes `audio_db` déréférençaient `AudioPlayer::database` **sans le tester**, alors que ce
pointeur vaut `nullptr` par défaut
(dérivé, [Audio/AudioPlayer.cpp:28](../src/bin/calaos_server/Audio/AudioPlayer.cpp)) et que
`Squeezebox` est le **seul** à l'affecter
(dérivé, [Audio/Squeezebox.cpp:81](../src/bin/calaos_server/Audio/Squeezebox.cpp)). Ouvrir
l'écran Médiathèque sur un lecteur Roon suffisait à faire tomber le serveur.

La garde, appelée **juste avant le déréférencement** dans les seize builders :

```cpp
// (dérivé, JsonApi.cpp:976-985, intégral)
bool JsonApi::audioDbUnavailable(AudioPlayer *player,
                                 const std::function<void(json_t *)> &result_lambda)
{
    if (player->get_database())
        return false;

    Params p = {{"error", "no music database" }};
    result_lambda(p.toJson());
    return true;
}
```

⚠️ **Le test est le pointeur, pas `canDatabase()`.** La capacité est une constante de classe,
seulement **publiée** dans `get_home` ; la précondition du déréférencement est
`database != nullptr`. Les deux ne coïncident qu'accidentellement aujourd'hui, et les deux sens
de l'écart sont épinglés par les tests
(dérivé, [JsonApi.cpp:948-956](../src/bin/calaos_server/JsonApi.cpp),
[tests/core/JsonApiInputGuards_test.cpp](../tests/core/JsonApiInputGuards_test.cpp)).

La réponse **réutilise le vocabulaire d'erreur existant**, aucune forme nouvelle n'est
introduite :

```json
// (capturé, tests/core/golden/t319_ws_audio_db_no_database.json, intégral)
{
  "data": {
    "error": "no music database"
  },
  "msg": "audio_db",
  "msg_id": "t319"
}
```

```json
// (capturé, tests/core/golden/t319_http_audio_db_no_database.json, intégral)
{
  "error": "no music database"
}
```

**Ce qui ne change pas** : un lecteur qui **possède** une base répond exactement comme avant,
mêmes champs et même pagination. **Une base vide reste une base vide** — elle rend une liste
vide, pas une erreur ; seule l'**absence** de base est refusée.

---

## AVReceiver (amplis AV)

**Fichier :** [src/bin/calaos_server/Audio/AVReceiver.h](../src/bin/calaos_server/Audio/AVReceiver.h)

Deux classes distinctes :

- **`AVReceiver`** — le transport vers l'ampli. **N'hérite pas d'`IOBase`.** Il est
  **partagé et compté en références** : `AVRManager::Create()` retrouve l'instance par `host` et
  incrémente `ref_count` au lieu d'en créer une seconde
  (dérivé, [Audio/AVRManager.cpp:50-78](../src/bin/calaos_server/Audio/AVRManager.cpp)). Trois
  zones d'un même ampli partagent donc **une** connexion.
- **`IOAVReceiver`** — l'IO, `TSTRING`, une instance **par zone**
  (dérivé, [Audio/AVReceiver.h:141-166](../src/bin/calaos_server/Audio/AVReceiver.h)).

**Type XML : `AVReceiver`**, servi par `IOAVReceiver`
(dérivé, [Audio/AVReceiver.cpp:29](../src/bin/calaos_server/Audio/AVReceiver.cpp) :
`REGISTER_IO_USERTYPE(AVReceiver, IOAVReceiver)`).

### Paramètres

| Paramètre | Obligatoire | Description |
|---|---|---|
| `host` | oui | IP de l'ampli |
| `model` | oui | Modèle, voir table ci-dessous |
| `port` | non, 0…65535 | Port de connexion (chaque driver a son défaut) |
| `zone` | non, 0…10, défaut 1 | Zone pilotée par cet IO |

(dérivé, [Audio/AVReceiver.cpp:254-257,267-268](../src/bin/calaos_server/Audio/AVReceiver.cpp).)

### Valeurs de `model`

| `model` | Classe | Protocole |
|---|---|---|
| `pioneer` | `AVRPioneer` | IP Control Pioneer |
| `denon` | `AVRDenon` | Telnet Denon |
| `onkyo` | `AVROnkyo` | eISCP Onkyo |
| `marantz` | `AVRMarantz` | Telnet Marantz |
| `yamaha` | `AVRYamaha` | YNCA Yamaha |
| **`hifirose`** | `AVRRose` | API HTTP HiFi Rose |

(dérivé, [Audio/AVRManager.cpp:55-71](../src/bin/calaos_server/Audio/AVRManager.cpp).)

⚠️ La valeur pour un ampli Rose est **`hifirose`**. La description du paramètre `model` publiée à
l'installeur ne liste que cinq modèles et **omet `hifirose`**
(dérivé, [Audio/AVReceiver.cpp:257](../src/bin/calaos_server/Audio/AVReceiver.cpp)) — c'est un
écart de la documentation générée, pas du code : la valeur fonctionne.

Un `model` inconnu ne crée **pas** l'ampli : `AVRManager::Create()` journalise
`AVRManager(): Unknown A/V Receiver model <model>` et rend `NULL`
(dérivé, [Audio/AVRManager.cpp:67-71](../src/bin/calaos_server/Audio/AVRManager.cpp)). L'IO
existe alors sans transport et ses commandes sont sans effet
(dérivé, [Audio/AVReceiver.cpp:328](../src/bin/calaos_server/Audio/AVReceiver.cpp) :
`if (!isEnabled() || !receiver) return false;`).

### `set_value(string)` — le vocabulaire de commande

| Valeur | Action |
|---|---|
| `power on` / `power true` | Allumer la zone |
| `power off` / `power false` | Éteindre la zone |
| `volume <n>` | Volume absolu de la zone |
| `source <n>` | Sélectionner la source **par son numéro** (`AVR_INPUT_*`) |
| `custom <commande>` | Envoyer une trame brute du protocole constructeur |

(dérivé, [Audio/AVReceiver.cpp:326-357](../src/bin/calaos_server/Audio/AVReceiver.cpp),
[Audio/AVReceiver.cpp:259-263](../src/bin/calaos_server/Audio/AVReceiver.cpp) pour l'`ioDoc`.)

⚠️ `source` attend un **entier**, l'indice de l'énumération `AVR_INPUT_*`
(dérivé, [Audio/AVReceiver.h:93-118](../src/bin/calaos_server/Audio/AVReceiver.h)). Cette
énumération est **gelée** : son ordre est stocké dans les actions des règles, on ne peut
qu'**ajouter à la fin**, jamais réordonner
(dérivé, [Audio/AVReceiver.h:100-101](../src/bin/calaos_server/Audio/AVReceiver.h)).

### API C++ de l'ampli

```cpp
// (dérivé, Audio/AVReceiver.h:120-137, intégral)
    virtual void Power(bool on, int zone = 1) {}
    virtual bool getPower(int zone = 1);
    virtual void setVolume(int volume, int zone = 1) {}
    virtual int getVolume(int zone = 1);

    virtual AVRList getSources() { return source_names; }
    virtual void selectInputSource(int source, int zone = 1) {}
    virtual int getInputSource(int zone = 1);

    //return true if AVR can send his display status text
    virtual bool hasDisplay() { return false; }
    virtual string getDisplayText() { return display_text; }

    virtual void sendCustomCommand(string command) { sendRequest(command); }

    sigc::signal<void, string, string> state_changed_1; //zone 1
    sigc::signal<void, string, string> state_changed_2; //zone 2
    sigc::signal<void, string, string> state_changed_3; //zone 3
```

⚠️ Il n'existe **ni `VolumeUp()`, ni `VolumeDown()`, ni `Mute()`, ni `SelectInput(string)`, ni
`SelectSurround()`.** Le volume est absolu (`setVolume(int, zone)`) et la source se choisit par
numéro (`selectInputSource(int, zone)`).

Chaque `IOAVReceiver` s'abonne au signal de **sa** zone
(dérivé, [Audio/AVReceiver.cpp:271-276](../src/bin/calaos_server/Audio/AVReceiver.cpp)).

### Volume rempli à deux chiffres (Denon / Marantz / Onkyo)

Les protocoles constructeurs exigent un champ de volume de **largeur fixe**. Les commandes sont
désormais correctement remplies de zéros : **`MV05`**, et non plus `MV5`.

```cpp
// (dérivé, Audio/AVRDenon.cpp:266-270, intégral)
    //width()/fill() only apply to the next insertion: set them right before
    //the value so the volume is the thing that gets zero-padded
    ss.width(2);
    ss.fill('0');
    ss << v;
```

Le préfixe dépend de la zone : `MV` en zone 1, `Z2` en zone 2, `Z3` en zone 3
(dérivé, [Audio/AVRDenon.cpp:261-264](../src/bin/calaos_server/Audio/AVRDenon.cpp)).

### AVRManager

**Fichier :** [src/bin/calaos_server/Audio/AVRManager.h](../src/bin/calaos_server/Audio/AVRManager.h)

Registre singleton des amplis, **indexé par `host`**. `Create()` incrémente `ref_count`,
`Delete()` le décrémente ; l'objet n'est détruit qu'au dernier relâchement
(dérivé, [Audio/AVRManager.cpp:43-48,50-78,80…](../src/bin/calaos_server/Audio/AVRManager.cpp)).

### AVRRoseNotifServer

**Fichier :** [src/bin/calaos_server/Audio/AVRRoseNotifServer.h](../src/bin/calaos_server/Audio/AVRRoseNotifServer.h)

Serveur **HTTP** de notifications push pour les amplis HiFi Rose : il **écoute sur le port
9284** et reçoit les POST du RS520 sur `/device_state_noti` et `/test`. Il est **partagé entre
toutes les instances `AVRRose`**
(dérivé, [Audio/AVRRoseNotifServer.h:37-45](../src/bin/calaos_server/Audio/AVRRoseNotifServer.h)).

⚠️ Ce port doit être joignable **depuis l'ampli** pour que les changements d'état remontent.

⚠️ Les transferts sortants d'`AVRRose` sont **toujours non vérifiés en TLS** et n'exposent aucune
option `insecure` (dérivé, [Audio/AVRRose.cpp:414,440](../src/bin/calaos_server/Audio/AVRRose.cpp)).
Voir [02_io_drivers.md](02_io_drivers.md#option-insecure--tls-des-urls-configurées-par-lutilisateur).

---

## AudioDB

**Fichier :** [src/bin/calaos_server/Audio/AudioDB.h](../src/bin/calaos_server/Audio/AudioDB.h)

Classe de base de la base musicale d'un lecteur. Toutes ses méthodes sont **asynchrones** et
paginées par `(from, nb)` :

```cpp
// (dérivé, Audio/AudioDB.h:39-75, extrait)
virtual void getStats(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) {}
virtual void getAlbums(AudioRequest_cb callback, int from, int nb, ...);
virtual void getAlbumsTitles(AudioRequest_cb callback, int from, int nb, string album_id, ...);
virtual void getArtists(...);        virtual void getArtistsAlbums(..., string artist_id, ...);
virtual void getGenres(...);         virtual void getGenresArtists(..., string genre_id, ...);
virtual void getYears(...);          virtual void getYearsAlbums(..., string year, ...);
virtual void getPlaylists(...);      virtual void getPlaylistsTracks(..., string playlist_id, ...);
virtual void getRadios(...);         virtual void getRadiosItems(..., string radio, string item_id = "", string search = "", ...);
virtual void getRandoms(...);        virtual void setRandomsType(string type) {}
virtual void getSearch(..., string search, ...);
virtual void getMusicFolder(..., string folder_id = "", ...);
virtual void getTrackInfos(AudioRequest_cb callback, string track_id, ...);
```

Le corps par défaut de chacune est **vide** : une sous-classe qui n'implémente pas une méthode ne
répond simplement jamais.

---

## Accès depuis l'API JSON

⚠️ **Il n'existe aucune action nommée `audio_get_playlist_size`, `audio_get_cover_info`,
`audio_get_current_time`, `audio_get_db_stats`, `audio_db_get_albums`, `audio_db_get_artists`,
`audio_db_get_playlists`, `audio_db_get_radios` ni `audio_db_get_search`.** Le protocole n'a
que **deux** actions audio, chacune portant une clé **`audio_action`** qui nomme la sous-action.

| action | sous-actions | dispatch |
|---|---|---|
| `audio` | `get_playlist_size`, `get_time`, `get_playlist_item`, `get_cover_url` (+ `get_cover` **en HTTP seulement**) | [JsonApiHandlerWS.cpp:350-374](../src/bin/calaos_server/JsonApiHandlerWS.cpp), [JsonApiHandlerHttp.cpp:691-716](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) |
| `audio_db` | seize sous-actions | [JsonApiHandlerWS.cpp:376-461](../src/bin/calaos_server/JsonApiHandlerWS.cpp), [JsonApiHandlerHttp.cpp:781-862](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) |

⚠️ **Divergence de nom entre transports sur les albums** : la même sous-action s'appelle
`get_albums` en HTTP et `get_album` en WebSocket
(dérivé, [JsonApiHandlerHttp.cpp:781](../src/bin/calaos_server/JsonApiHandlerHttp.cpp),
[JsonApiHandlerWS.cpp:380](../src/bin/calaos_server/JsonApiHandlerWS.cpp)). Un client
multi-transport doit écrire les deux.

Une sous-action inconnue ou absente est refusée par
`{"error": "unkown audio_action"}` (la coquille est dans le code, elle est gelée)
(dérivé, [JsonApiHandlerWS.cpp:374,461](../src/bin/calaos_server/JsonApiHandlerWS.cpp)).

⚠️ **`audio_action` n'est pas repris dans toutes les réponses.** `get_stats` le renvoie,
`get_playlist_size` et `get_time` l'avalent. Un client ne peut donc pas s'en servir pour
corréler une réponse à sa requête — il doit utiliser `msg_id`.

```json
// (capturé, tests/core/golden/t317b_ws_audio_db_get_stats.json, intégral)
{
  "data": {
    "albums": "12",
    "artists": "7",
    "audio_action": "get_stats",
    "songs": "134"
  },
  "msg": "audio_db",
  "msg_id": "1"
}
```

```json
// (capturé, tests/core/golden/t317b_ws_audio_get_playlist_size.json, intégral)
{
  "data": {
    "playlist_size": "12"
  },
  "msg": "audio",
  "msg_id": "1"
}
```

Messages d'erreur des deux familles, tels qu'épinglés :
`unkown player_id` pour un id inconnu et `empty player id` pour un id vide — deux messages
différents, tous deux gelés
(capturé, `tests/core/golden/t317b_ws_audio_unknown_player.json`,
`tests/core/golden/t317b_ws_audio_empty_player_id.json`).

Le protocole complet — arguments, formes de réponse, correspondance HTTP/WS, pagination — est
décrit dans [08_http_api.md](08_http_api.md).

### `get_home` : le tableau `audio`

`buildJsonAudio()` rend, pour chaque lecteur du cache audio : `id`, `name`, `type`,
`playlist` (`canPlaylist()`), `database` (`canDatabase()`), et `avr` **si et seulement si** le
lecteur porte un paramètre `amp`
(dérivé, [JsonApi.cpp:389-419](../src/bin/calaos_server/JsonApi.cpp)). Les informations
détaillées (titre, volume, position) ne sont **délibérément pas** demandées ici, pour ne pas
retarder `get_home` par un aller-retour vers chaque lecteur : elles s'obtiennent par `get_state`
(dérivé, [JsonApi.cpp:412-414](../src/bin/calaos_server/JsonApi.cpp)).

### Plus de plantage sur déconnexion en cours de requête (T3.17a/b/c)

Toutes les commandes audio qui interrogent le lecteur ou sa base font un **aller-retour réseau**.
Si le client se déconnectait pendant cet aller-retour, la réponse revenait sur une connexion
détruite et **faisait planter le serveur**. C'est corrigé sur les cinq commandes d'état du
lecteur, les quinze commandes de médiathèque et `get_playlist`, **sur les deux transports**.

**Ce qui ne change pas** : un client encore connecté reçoit sa réponse **complète et inchangée**
— mêmes champs, même ordre, même pagination, mêmes messages d'erreur. Aucune liste n'est
tronquée. Seul le client **déjà parti** ne reçoit plus rien.

**Nouveau comportement observable, un seul** : un lecteur **supprimé** pendant la consultation de
sa playlist fait répondre `{"success":"false"}` au lieu de planter — exactement ce que
`get_playlist` répond déjà pour un id inconnu.
