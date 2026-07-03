# Audio — Lecteurs et Amplis AV

## Vue d'ensemble

Le sous-système audio gère deux types de périphériques :
1. **AudioPlayer** — lecteurs audio réseau (Squeezebox/LMS, Roon)
2. **AVReceiver** — amplis AV (Denon, Marantz, Onkyo, Pioneer, Rose, Yamaha)

Les deux héritent de `IOBase` (type `TSTRING`). Ils apparaissent dans le cache audio de `ListeRoom`.

---

## AudioPlayer

**Fichier :** [src/bin/calaos_server/Audio/AudioPlayer.h](../src/bin/calaos_server/Audio/AudioPlayer.h)

Classe de base abstraite pour les lecteurs audio.

### Méthodes virtuelles

```cpp
virtual void Play();
virtual void Pause();
virtual void Stop();
virtual void Next();
virtual void Previous();
virtual void Power(bool on);
virtual void Sleep(int seconds);
virtual void Synchronize(string playerid, bool sync);

virtual void get_volume(AudioRequest_cb cb, AudioPlayerData user_data);
virtual void set_volume(int vol);

virtual void get_title(AudioRequest_cb cb, ...);
virtual void get_artist(AudioRequest_cb cb, ...);
virtual void get_album(AudioRequest_cb cb, ...);
virtual void get_album_cover(AudioRequest_cb cb, ...);
virtual void get_current_time(AudioRequest_cb cb, ...);
virtual void set_current_time(double seconds);
virtual void get_duration(AudioRequest_cb cb, ...);
virtual void get_status(AudioRequest_cb cb, ...);

// Playlist
virtual void playlist_play(int item);
virtual void playlist_add_artist(string item);
virtual void playlist_play_album(string item);
virtual void playlist_clear();
virtual void playlist_save(string name);
virtual void get_playlist_size(AudioRequest_cb cb, ...);
virtual void get_playlist_item(int index, AudioRequest_cb cb, ...);

virtual bool canPlaylist();
virtual bool canDatabase();
virtual Params getDatabaseCapabilities();
```

### set_value(string)

Les commandes sont envoyées via `set_value(string)`. Valeurs reconnues :

| Valeur | Action |
|---|---|
| `"play"` | Lecture |
| `"pause"` | Pause |
| `"stop"` | Stop |
| `"next"` | Suivant |
| `"previous"` | Précédent |
| `"power on"` / `"power off"` | Mise sous/hors tension |
| `"volume+"` / `"volume-"` | Incrément volume |
| `"volume 75"` | Volume absolu |

### AudioPlayerData

**Fichier :** [src/bin/calaos_server/Audio/AudioPlayerData.h](../src/bin/calaos_server/Audio/AudioPlayerData.h)

Conteneur de résultat pour les callbacks asynchrones :

```cpp
struct AudioPlayerData {
    bool isSuccess;
    string svalue;            // résultat chaîne
    double dvalue;            // résultat numérique
    int ivalue;
    vector<string> vectData;  // liste de résultats
    // ... données DB (albums, artistes, etc.)
};
using AudioRequest_cb = sigc::slot<void, AudioPlayerData>;
```

---

## Squeezebox / Logitech Media Server

**Fichiers :** [src/bin/calaos_server/Audio/Squeezebox.h](../src/bin/calaos_server/Audio/Squeezebox.h), [SqueezeboxDB.h](../src/bin/calaos_server/Audio/SqueezeboxDB.h)

Interface avec le serveur LMS (Logitech Media Server) via son API CLI (port TCP 9090).

### Paramètres

| Paramètre | Description |
|---|---|
| `host` | IP du serveur LMS |
| `port` | Port CLI LMS (défaut 9090) |
| `playerid` | MAC address du player Squeezebox |

### SqueezeboxDB

Sous-classe pour l'accès à la base de données musicale LMS (albums, artistes, genres, playlists, radios).

---

## RoonPlayer

**Fichier :** [src/bin/calaos_server/Audio/RoonPlayer.h](../src/bin/calaos_server/Audio/RoonPlayer.h)

Intégration avec le logiciel Roon via son API réseau.

---

## AVReceiver (Amplis AV)

**Fichier :** [src/bin/calaos_server/Audio/AVReceiver.h](../src/bin/calaos_server/Audio/AVReceiver.h)

Classe de base pour les amplis AV. Hérite aussi de `IOBase` (TSTRING).

### Commandes communes

```cpp
virtual void Power(bool on);
virtual void VolumeUp();
virtual void VolumeDown();
virtual void Mute(bool mute);
virtual void SelectInput(string input);
virtual void SelectSurround(string mode);
```

### Implémentations

| Classe | Protocole |
|---|---|
| `AVRDenon` | RS232/Telnet Denon |
| `AVRMarantz` | RS232/Telnet Marantz |
| `AVROnkyo` | EISCP réseau Onkyo |
| `AVRPioneer` | IP Control Pioneer |
| `AVRYamaha` | YNCA Yamaha |
| `AVRRose` | API Rose HiFi |

### AVRManager

**Fichier :** [src/bin/calaos_server/Audio/AVRManager.h](../src/bin/calaos_server/Audio/AVRManager.h)

Registre singleton de tous les amplis configurés.

---

## AudioDB

**Fichier :** [src/bin/calaos_server/Audio/AudioDB.h](../src/bin/calaos_server/Audio/AudioDB.h)

Classe de base pour l'accès à la base de données musicale d'un lecteur (si `canDatabase() == true`). Méthodes asynchrones pour :
- Albums, artistes, années, genres
- Playlists, titres
- Radios (internet)
- Recherche full-text
- Pochettes d'album

---

## AVRRoseNotifServer

**Fichier :** [src/bin/calaos_server/Audio/AVRRoseNotifServer.h](../src/bin/calaos_server/Audio/AVRRoseNotifServer.h)

Serveur de notifications push pour les amplis Rose HiFi. Écoute les événements entrants de l'ampli (changement de source, volume, etc.).

---

## Accès depuis l'API JSON

Les endpoints audio de `JsonApi` :

```
audio_get_playlist_size
audio_get_playlist_item
audio_get_cover_info
audio_get_current_time
audio_get_db_stats
audio_db_get_albums
audio_db_get_artists
audio_db_get_playlists
audio_db_get_radios
audio_db_get_search
```

Voir [08_http_api.md](08_http_api.md) pour le protocole JSON.
