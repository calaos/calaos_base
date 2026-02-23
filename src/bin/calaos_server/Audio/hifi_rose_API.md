# Rose RS520 — Full API Reference

Reverse-engineered from Rose Connect 5.9.16 (Electron) + Wireshark capture.

---

## Communication Architecture

```
┌──────────────────────────────────┐                ┌──────────────────────────────────┐
│        Rose Connect (PC)         │                │          Rose RS520              │
│                                  │                │        (Android/Linux)           │
│                                  │                │                                  │
│  ┌────────────────────────────┐  │  HTTPS :9283   │  ┌────────────────────────────┐  │
│  │  axios + rootCA.crt        │──┼───────────────→│  │  REST API (113 endpoints)  │  │
│  │  (TLS 1.2, self-signed)    │  │  JSON POST/GET │  │  Cert: HiFiRose Root CA    │  │
│  └────────────────────────────┘  │                │  └────────────────────────────┘  │
│                                  │                │                                  │
│  ┌────────────────────────────┐  │  HTTP :9284    │  ┌────────────────────────────┐  │
│  │  Express server            │←─┼────────────────│  │  okhttp/3.12.0 client      │  │
│  │  (notifications callback)  │  │  JSON POST     │  │  (push notifications)      │  │
│  └────────────────────────────┘  │                │  └────────────────────────────┘  │
│                                  │                │                                  │
│  ┌────────────────────────────┐  │  UDP :51054    │  ┌────────────────────────────┐  │
│  │  dgram socket (send)       │──┼───────────────→│  │  UDP listener              │  │
│  │  Direct media commands     │  │  text/plain    │  │  SMB/CIFS file playback    │  │
│  └────────────────────────────┘  │                │  └────────────────────────────┘  │
│  ┌────────────────────────────┐  │  UDP :51053    │  ┌────────────────────────────┐  │
│  │  dgram socket (receive)    │←─┼────────────────│  │  UDP sender                │  │
│  └────────────────────────────┘  │                │  └────────────────────────────┘  │
│                                  │                │                                  │
│  ┌────────────────────────────┐  │  HTTP :8000    │  ┌────────────────────────────┐  │
│  │  Media library client      │──┼───────────────→│  │  Media library REST API    │  │
│  └────────────────────────────┘  │                │  └────────────────────────────┘  │
│                                  │                │                                  │
│  ┌────────────────────────────┐  │  HTTP :9285    │  ┌────────────────────────────┐  │
│  │  Queue file client         │──┼───────────────→│  │  Queue file server         │  │
│  └────────────────────────────┘  │                │  └────────────────────────────┘  │
└──────────────────────────────────┘                └──────────────────────────────────┘
```

### Ports Summary

| Port | Proto | Direction | Role |
|------|-------|-----------|------|
| **9283** | HTTPS (TLS 1.2) | PC → RS520 | Main API (113 endpoints) |
| **9284** | HTTP | RS520 → PC | Push notifications (callbacks) |
| **9285** | HTTP | PC → RS520 | Queue file download |
| **8000** | HTTP | PC → RS520 | Local media library |
| **51054** | UDP | PC → RS520 | Direct media commands |
| **51053** | UDP | RS520 → PC | Incoming UDP messages |
| **7000** | TCP | — | AirPlay (Shairport Sync) |
| **9999** | TCP | — | Spotify Connect |
| **9080** | TCP | — | Qobuz Connect |

---

## HTTPS Authentication

The RS520 uses a **self-signed** certificate (`HiFiRose Root CA`, RSA-4096). The app bundles the `rootCA.crt` file and disables hostname verification:

```javascript
const caCert = fs.readFileSync('./rootCA.crt');
const httpsAgent = new https.Agent({
    ca: caCert,
    checkServerIdentity: () => undefined  // bypass hostname check
});
```

The app first attempts plain HTTP — if the RS520 rejects (RST), it falls back to HTTPS. An `isHttps` flag is stored in the Redux state.

All API calls go through a wrapper that:
1. Tries `fetch()` if `isHttps === false`
2. Otherwise goes through Electron IPC → `AuthHttps()` (Node-side with the certificate)

### Standard Headers

```
Content-Type: application/json;charset=utf-8
```

Some requests also include a `roseToken` in the body (Rose user authentication).

---

## Network Discovery (mDNS)

The RS520 advertises via mDNS `_http._tcp.local` with a name starting with `roseHifi-`.

```javascript
// App side (Network.js)
const browser = dnssd.Browser(dnssd.tcp('http'));
browser.on('serviceUp', service => {
    if (service.name.includes("roseHifi")) {
        // → service.addresses[0] = RS520 IP
    }
});
```

The device name follows the pattern: `roseHifi-{DeviceName}` (e.g., `roseHifi-RoseSalon`).

---

## Main API — Port 9283 (HTTPS)

Base URL: `https://{device_ip}:9283`

Unless otherwise noted, all requests are **POST** with `Content-Type: application/json`.

---

### 1. Device — Identification & State

#### `POST /device_name`
Ping the device / retrieve its name. No body required.
```json
// Response (observed)
{
  "data": {
    "dbFileSize": 0,
    "deviceGateway": "192.168.30.222",
    "deviceID": "FF:AB:DE:0F:27:98",
    "deviceIP": "192.168.30.135",
    "deviceName": "RoseSalon",
    "deviceNetmask": "255.255.255.0",
    "deviceType": "RS520",
    "deviceVersion": "5.9.09",
    "isDbScanning": false,
    "isDev": false,
    "isSyncMode": false,
    "musicPlaytype": 0,
    "stableState": "Stable"
  },
  "code": "G0000",
  "status": { "errorCd": "", "errorMsg": "", "outs": "OK" },
  "version": "1.0.9"
}
```

#### `POST /device_connected`
Signal app connection. Returns the device menu and a `deviceRoseToken` for authentication.
```json
// Request
{ "connectIP": "192.168.30.68" }
// Response (observed)
{
  "data": {
    "dbFileSize": 172032,
    "deviceID": "FF:AB:DE:0F:27:98",
    "deviceIP": "192.168.30.135",
    "deviceName": "RoseSalon",
    "deviceRoseToken": "ROSE0D1746ABCD",
    "deviceType": "RS520",
    "deviceVersion": "5.9.09",
    "isDbScanning": false,
    "isDev": false,
    "isSyncMode": true,
    "musicPlaytype": 17,
    "regionCode": "FR"
  },
  "menuArr": [
    { "img": 0, "isSelected": false, "menu": "ROSE_HOME", "status": 1 },
    { "img": 0, "isSelected": false, "menu": "MUSIC", "status": 1 },
    { "img": 0, "isSelected": false, "menu": "VIDEO", "status": 1 },
    { "img": 0, "isSelected": false, "menu": "CD_PLAY", "status": 1 },
    { "img": 0, "isSelected": false, "menu": "ROSE_RADIO", "packageNm": "com.citech.roseradio", "status": 1 },
    { "img": 0, "isSelected": false, "menu": "TIDAL", "packageNm": "com.citech.rosetidal", "status": 1 },
    { "img": 0, "isSelected": false, "menu": "ROSETUBE", "packageNm": "com.citech.rosetube", "status": 1 },
    { "img": 0, "isSelected": false, "menu": "SETTING", "status": 1 }
  ],
  "code": "G0000",
  "status": { "errorCd": "", "errorMsg": "", "outs": "OK" },
  "version": "1.0.9"
}
```

#### `POST /device_get_info`
Retrieve detailed device information.
**Note:** Returns an empty body in practice.

#### `POST /get_current_state`
Full current playback state (title, artist, position, source, etc.).

All responses include a common structure: `{"code": "G0000"|"SLEEP", "status": {"errorCd": "", "errorMsg": "", "outs": "OK"}, "version": "1.0.9"}`

**When device is in sleep mode**, all endpoints return:
```json
{ "code": "SLEEP", "status": { "errorCd": "", "errorMsg": "", "outs": "OK" }, "version": "1.0.9" }
```

**When device is awake (observed):**
```json
{
  "data": {
    "albumName": "GONE FISHIN'",
    "artistName": "GLU, Phantogram",
    "buf": 0,
    "buffer": "0",
    "curPosition": "197000",
    "duration": "236000",
    "favCnt": 0,
    "isFavorite": false,
    "isFile": false,
    "isHdmiOn": false,
    "isPlaying": true,
    "isServer": false,
    "path": "",
    "playState": "",
    "playType": "AIRPLAY",
    "repeatMode": 0,
    "shuffleMode": 0,
    "subAppCurrentData": "",
    "tempArr": [],
    "thumbnail": ["/storage/emulated/0/Pictures/airplay1.jpg"],
    "titleName": "GONE FISHIN'",
    "trackInfo": "iPhone de Laetitia",
    "ui_state": 0,
    "volume": 0
  },
  "code": "G0000",
  "status": { "errorCd": "", "errorMsg": "", "outs": "OK" },
  "version": "1.0.9"
}
```

**Note:** `curPosition` and `duration` are strings (milliseconds). `volume` in this response is always 0 — use `GET /get_control_info` for the real volume.

#### `GET /get_control_info`
Control information (playback state, volume, active source, etc.).
```json
// Response (observed)
{
  "airplayInfo": true,
  "displayInfo": "Off",
  "dlnaInfo": true,
  "isUsbEnable": false,
  "qobuzConnectInfo": true,
  "spotifyInfo": true,
  "volumeNotSupported": 0,
  "volumeValue": 10,
  "code": "G0000",
  "status": { "errorCd": "", "errorMsg": "", "outs": "OK" },
  "version": "1.0.9"
}
```
**This is the only reliable source for reading the current volume** (`volumeValue` field, integer).

#### `POST /check_server`
Check that the device server is active.

#### `POST /get_rose_user_info`
Retrieve Rose user info registered on the device.
```json
// Response
{ "email": "...", "accesskey": "...", "userName": "..." }
```

#### `POST /set_rose_user_info`
Register Rose user info on the device.

#### `POST /app_package_info_get`
Retrieve installed package information.

#### `POST /app_package_update_set`
Trigger a package update.

---

### 2. Playback Control

#### `POST /current_play_state`
Main playback control command.
```json
// Play/Pause toggle
{ "currentPlayState": 17 }

// Next track
{ "currentPlayState": 18 }

// Previous track
{ "currentPlayState": 19 }

// Seek (jump to position)
{ "currentPlayState": 22, "currentPlaySeekto": 45000 }

// Repeat mode toggle
{ "currentPlayState": 24 }

// Shuffle mode toggle
{ "currentPlayState": 25 }

// Favorite (star)
{ "currentPlayState": 23, "roseToken": "..." }
```

**`currentPlayState` Constants:**

| Value | Constant | Action |
|-------|----------|--------|
| 17 | `REMOTE_MUSIC_STATE_PLAY_PAUSE_TOGGLE` | Play/Pause |
| 18 | `REMOTE_MUSIC_STATE_NEXT` | Next track |
| 19 | `REMOTE_MUSIC_STATE_PREV` | Previous track |
| 22 | `REMOTE_SEEKTO` | Seek (+ `currentPlaySeekto` in ms) |
| 23 | — | Toggle favorite |
| 24 | `REMOTE_MUSIC_STATE_REPEAT` | Toggle repeat |
| 25 | `REMOTE_MUSIC_STATE_SHUFFLE` | Toggle shuffle |

#### `POST /total_queue_play_relate_state`
Queue-relative playback state.

---

### 3. Volume & Mute

#### `POST /volume`
Set the volume.
```json
// Request
{ "volume": 20 }
```
**Note:** This endpoint returns an **empty body** on success. To read the current volume, use `GET /get_control_info` instead. When the device is in sleep mode, this endpoint also returns an empty body and has no effect.

#### `POST /mute.state.get`
Retrieve mute state.
```json
// Response (observed)
{ "mute": 0, "code": "G0000", "status": { "errorCd": "", "errorMsg": "", "outs": "OK" }, "version": "1.0.9" }
// After mute toggle:
{ "mute": 1, "code": "G0000", "status": { "errorCd": "", "errorMsg": "", "outs": "OK" }, "version": "1.0.9" }
```
**Note:** `mute` is an integer (0 = not muted, 1 = muted), not a boolean.

---

### 4. Virtual Remote Control (`/remote_bar_order`)

#### `POST /remote_bar_order`
Remote control command — main entry point for system actions.
```json
{
  "barControl": "remote_bar_order.mute",
  "value": -1,
  "roseToken": "..."
}
```

**Available `barControl` commands:**

| barControl | Action |
|-----------|--------|
| `remote_bar_order.home` | Go to home screen |
| `remote_bar_order.back` | Go back |
| `remote_bar_order.screen_onoff` | Toggle LCD screen on/off |
| `remote_bar_order.clock` | Display clock |
| `remote_bar_order_sleep_on_off` | Sleep mode on/off |
| `remote_bar_order.reboot` | Reboot device |
| `remote_bar_order.power_onoff` | Power on/off device |
| `remote_bar_order.mute` | Toggle mute (value: -1) |
| `remote_bar_order.timer` | Timer setting |
| `remote_bar_order_airplay_restart` | Restart AirPlay service |
| `remote_bar_order_dlna_restart` | Restart DLNA service |
| `remote_bar_order.hdmi` | HDMI selection (+ `strValue`) |

For HDMI, add the `strValue` field to the body:
```json
{ "barControl": "remote_bar_order.hdmi", "value": 0, "strValue": "HDMI_ARC" }
```

#### `POST /remote_bar_order_spotify_restart`
Restart Spotify Connect service.

#### `POST /remote_bar_order_qobuz_connect_restart`
Restart Qobuz Connect service.

---

### 5. Playback Queue

#### `POST /total_queue_get`
Retrieve the current playback queue.

#### `POST /total_queue_get_checksum`
Retrieve the queue checksum (for synchronization).
```json
// Response
{ "checkSum": "..." }
```

#### `POST /total_queue_set`
Set/replace the playback queue.
```json
{
  "data": [ /* track array */ ],
  "queueMode": "...",
  "currentPlayer": "...",
  "remotePositionPlay": 0,
  "checkSum": "..."
}
```

#### `POST /total_queue_modify`
Modify the existing queue.

#### `POST /total_queue_delete`
Delete items from the queue.
```json
{
  "indexArr": [0, 1, 2],
  "playType": "...",
  "checkSum": "..."
}
```

#### `POST /total_recommand_get`
Retrieve recommendations.

#### `POST /total_recommand_play`
Play recommendations.

---

### 6. Source-Specific Playback

#### Network / Local File Playback
```
POST /music_network_play
```
```json
{
  "music": { /* track object */ },
  "musicPlayType": "...",
  "playType": "...",
  "shuffle": 0
}
```

```
POST /music.folder.all.play   — Play entire folder
POST /music_song               — Play a specific song
POST /music_image_upload       — Upload album artwork
POST /music.file.info.get      — Get file info
```

#### Tidal
```
POST /tidalPlay_set_session_info   — Configure Tidal session
POST /tidalPlay_get_session_info   — Retrieve Tidal session
POST /tidalPlay_set_queue          — Add to queue (tracks)
POST /tidalPlay_set_queue.video    — Add Tidal videos
```
Body for set_queue:
```json
{
  "currentPosition": 0,
  "roseToken": "...",
  "shuffle": 0,
  "playType": "...",
  "tidalModeItem": [ /* tracks */ ]
}
```

#### Qobuz
```
POST /qobuz_set_session_info
POST /qobuz_get_session_info
POST /qobuz_set_queue           — (dynamic endpoint via setPlay)
```

#### Spotify
```
POST /spotify.name.get          — Device Spotify name
```

#### Apple Music
```
POST /apple_music_set_session_info
POST /apple_music_get_session_info
POST /apple_music_set_queue     — (dynamic endpoint via setPlay)
```

#### Amazon Music
```
POST /amazon_music_set_session_info
POST /amazon_music_get_session_info
POST /amazon_music_set_direct_play
POST /amazon_music_set_queue_explicit
```
```json
{ "isExplicit": true }
```

#### Bugs (Korean streaming service)
```
POST /bugs_set_session_info
POST /bugs_get_session_info
POST /bugs_set_queue            — (dynamic endpoint via setPlay)
```

#### YouTube / RoseTube
```
POST /youtubePlay               — (dynamic endpoint via setPlay)
POST /youtube.current.play.relate.list
```
Body:
```json
{
  "currentPosition": 0,
  "roseToken": "...",
  "shuffle": 0,
  "youtubePlayType": "...",
  "youtube": [ /* items */ ]
}
```

#### Radio
```
POST /radioPlay
POST /rose_radio_play
POST /get.radio.user.channels.item
POST /set.radio.channel.item.add
POST /set.radio.channel.item.delete
POST /set.radio.user.channels.item
POST /set.radio.fav.url
```

#### Video
```
POST /video_queue_data
POST /video_queue_play
POST /get_video_setting_value
POST /set_video_setting_value
```

#### CD
```
POST /cd.list.get       — List CD tracks
POST /cd.play            — Play CD
POST /cd.play.stop       — Stop CD
POST /cd.out             — Eject CD
POST /cd.riping.check    — Check ripping status
```

#### FM Tuner
```
POST /get.fmtuner.data              — FM tuner state
POST /set.fmtuner.play_power_on_off — Tuner on/off
POST /set.fmtuner.play_hz           — Set frequency
POST /set.fmtuner.play              — Play a station
POST /set.fmtuner.fav               — Add to favorites
POST /set.fmtuner.country           — Set country
POST /set.fmtuner.region            — Set region
```

#### Symphony (Rose Symphony)
```
POST /sympony_music_show_view
```

---

### 7. Audio Configuration (DAC / Inputs-Outputs)

#### `POST /in.out.mode.get`
Retrieve current input/output configuration.
```json
// Response (observed)
{
  "clockInfo": -1,
  "clockMode": 0,
  "funcMode": 0,
  "internalMode": "0,0,1,1",
  "isDacReset": false,
  "javsMode": 0,
  "outputMode": 4,
  "xmosInfo": -1,
  "version": "1.0.9"
}
```
**Note:** This endpoint does not include `code` or `status` fields in its response.

#### `POST /input.mode.set`
Change input mode.

#### `POST /input.setting.get` / `POST /input.setting.set`
Read/write input settings.

#### `POST /output.setting.get` / `POST /output.setting.set`
Read/write output settings.

#### `POST /usb.dac.get` / `POST /usb.dac.set`
USB DAC configuration.
```json
// For set:
{ "usbDacItem": "..." }
```

#### `POST /sfp.dac.get`
Retrieve SFP DAC info.

#### `POST /signal.path.get`
Retrieve the full audio signal path.
```json
// Response (observed)
{
  "item": {
    "model": "High Performance Network Streamer",
    "output": "Analog(AMP + PREAMP)",
    "outputMode": 4,
    "playType": "AIRPLAY",
    "source": "Tim's iPad",
    "tempArr": []
  },
  "code": "G0000",
  "status": { "errorCd": "", "errorMsg": "", "outs": "OK" },
  "version": "1.0.9"
}
```

#### `POST /xlr.hp.get`
XLR / headphone output info.

#### `POST /xlr.setting.get` / `POST /xlr.setting.set`
XLR configuration.

#### `POST /i2s.mode.set`
I2S mode configuration.

#### `POST /clock.mode.set`
Set clock mode.

#### `POST /hdmi_on_off`
Enable/disable HDMI output.

#### `POST /vu.mode.type.get` / `POST /vu.mode.type.set`
VU meter mode (display type).

---

### 8. Equalizer

#### `POST /remote_equalizer_get`
Retrieve equalizer settings.

#### `POST /remote_equalizer_set`
Apply equalizer settings.

---

### 9. Media Library

#### `POST /media_library`
Media library management.

#### `POST /media_library_add_delete`
Add or remove sources from the library.

#### `POST /media_library_db_init`
Reset the media library database.

#### `POST /remote_set_media_library_autoscan_list`
Configure media folder auto-scan.

#### `POST /remote_cache_search_get`
Search in local cache.

#### `POST /media.album.folder.fav.add` / `.delete` / `.favs.get`
Favorites management (folders/albums).

#### `POST /meta_info_get` / `POST /meta_info_set`
Read/write metadata.

#### `POST /external_usb`
USB drive management.

#### `POST /external_usb_get_mount`
Check USB mounts.

#### `POST /remote_network_share`
Network share configuration.

---

### 10. Display & Menus

#### `POST /display_value_get`
Retrieve display settings.
```json
{ "aodValue": false, "roseToken": "..." }
```

#### `POST /display_value_set`
Modify display settings.

#### `POST /menu_edit_get`
Retrieve menu configuration.

#### `POST /menu_edit_set`
Modify menu configuration.
```json
{ "data": [ /* menu items */ ], "roseToken": "..." }
```

---

### 11. Sleep & Timer

#### `GET /sleep.time.get`
Retrieve sleep timer.
```json
// Response (observed)
{ "timer": 0, "code": "G0000", "status": { "errorCd": "", "errorMsg": "", "outs": "OK" }, "version": "1.0.9" }
```

---

### 12. Shazam

#### `POST /set.shazm.serarch`
Start music recognition (Shazam).

#### `POST /set.shazm.serarch_cancel`
Cancel ongoing recognition.

---

### 13. Connected Amplifier (Rose AMP)

The RS520 can control an external Rose amplifier (e.g., RA180) over its own network:

```
POST /remote_get_connect_amp_item      — Retrieve connected amp
POST /remote_set_connect_amp_item      — Configure connected amp
POST /remote_get_connect_amp_vol_select — Retrieve volume control
POST /remote_set_connect_amp_vol_select — Configure volume control
```

The external amp is also directly accessible via:
```
GET  http://{amp_ip}/api/v1/info     — Amp info
GET  http://{amp_ip}/api/v1/all      — Full state
PUT  http://{amp_ip}/api/v1/volume   — Set volume
GET  http://{amp_ip}/api/v1/{param}  — Read a parameter
PUT  http://{amp_ip}/api/v1/{param}  — Modify a parameter
```

### 14. Play Type & Music Mode

```
GET  /get.music.play.type   — Current play type
POST /set.music.play.type   — Change play type
```

### 15. Device Name

```
POST /rose.name.set         — Rename device (Rose)
POST /spotify.name.set      — Rename device (Spotify)
```
```json
{ "deviceName": "MyRS520" }
```

---

## Media Library API — Port 8000 (HTTP)

Base URL: `http://{device_ip}:8000`

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/v1/albums` | GET | List albums |
| `/v1/artists` | GET | List artists |
| `/v1/videos` | GET | List videos |
| `/v1/video_albums` | GET | Video albums |
| `/v1/favfolders` | GET | Favorite folders |
| `/v1/random` | GET | Random playback |
| `/v1/scanner` | GET | Scanner status |
| `/v1/albumarts/{id}` | GET | Album artwork |

---

## Queue File API — Port 9285 (HTTP)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/total_queue_get_file` | POST | Download the full queue file |

---

## UDP Channel — Ports 51053/51054

### Sending Commands (PC → RS520, port 51054)

Format: `opn:0,{file_path}`

```
opn:0,/mnt/media_rw/cifs/{server_ip}/{share}/{path}/{file.flac}
opn:0,/mnt/media_rw/{internal_path}
```

Examples:
```
opn:0,/mnt/media_rw/cifs/192.168.1.123/music/Album/01.Track.flac
opn:0,/mnt/media_rw/usb/MyUSB/music/song.wav
```

### Receiving Messages (RS520 → PC, port 51053)

Text messages received and forwarded to the renderer via IPC `udp-message`.

---

## Push Notifications — Port 9284 (HTTP)

The RS520 sends POSTs to `http://{pc_ip}:9284` with `User-Agent: okhttp/3.12.0`.

### Local Server Endpoints

#### `POST /device_state_noti`
Device state notification.

#### `POST /test`
Health check (ping).

### Message Types (`messageType`)

| messageType | Description | Additional Data |
|-------------|-------------|-----------------|
| `music_start` | Playback started | — |
| `volume_change` | Volume changed | `volume: number` |
| `state_check` | Heartbeat / sync | — |
| `out_put_change` | Audio output changed | `dataObj: { outputMode, clockInfo, clockMode, funcMode, internalMode, isDacReset, javsMode, xmosInfo, version }` |
| `mute_state_change_noti` | Mute toggled | `position: number` |
| `abnormal_popup_close_noti` | Popup closed | `dataObj: boolean` |

### Observed `outputMode` Values

| Value | Likely Meaning |
|-------|---------------|
| 2 | Standard output (e.g., Speaker/Line Out) |
| 4 | Alternate output (e.g., Headphone/Balanced) |

---

## Usage Examples (curl)

### Get device name
```bash
curl -k https://192.168.30.135:9283/device_name \
  -X POST \
  -H "Content-Type: application/json;charset=utf-8"
```

### Set volume to 25
```bash
curl -k https://192.168.30.135:9283/volume \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"volume": 25}'
```

### Play/Pause toggle
```bash
curl -k https://192.168.30.135:9283/current_play_state \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"currentPlayState": 17}'
```

### Next track
```bash
curl -k https://192.168.30.135:9283/current_play_state \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"currentPlayState": 18}'
```

### Mute toggle
```bash
curl -k https://192.168.30.135:9283/remote_bar_order \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"barControl": "remote_bar_order.mute", "value": -1}'
```

### Power off device
```bash
curl -k https://192.168.30.135:9283/remote_bar_order \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"barControl": "remote_bar_order.power_onoff", "value": -1}'
```

### Reboot
```bash
curl -k https://192.168.30.135:9283/remote_bar_order \
  -X POST \
  -H "Content-Type: application/json" \
  -d '{"barControl": "remote_bar_order.reboot", "value": -1}'
```

### Current playback state
```bash
curl -k https://192.168.30.135:9283/get_current_state \
  -X POST \
  -H "Content-Type: application/json"
```

### Control info
```bash
curl -k https://192.168.30.135:9283/get_control_info
```

### Mute state
```bash
curl -k https://192.168.30.135:9283/mute.state.get \
  -X POST \
  -H "Content-Type: application/json"
```

### Playback queue
```bash
curl -k https://192.168.30.135:9283/total_queue_get \
  -X POST \
  -H "Content-Type: application/json"
```

### Input/output configuration
```bash
curl -k https://192.168.30.135:9283/in.out.mode.get \
  -X POST \
  -H "Content-Type: application/json"
```

### Signal path
```bash
curl -k https://192.168.30.135:9283/signal.path.get \
  -X POST \
  -H "Content-Type: application/json"
```

### Sleep timer
```bash
curl -k https://192.168.30.135:9283/sleep.time.get
```

---

## Listening for Notifications (Node.js)

```javascript
const express = require('express');
const app = express();
app.use(express.json());

app.post('/device_state_noti', (req, res) => {
    const msg = req.body;
    const ts = new Date().toISOString();

    switch(msg.messageType) {
        case 'volume_change':
            console.log(`[${ts}] Volume → ${msg.volume}`);
            break;
        case 'music_start':
            console.log(`[${ts}] Playback started`);
            break;
        case 'out_put_change':
            console.log(`[${ts}] Output → mode ${msg.dataObj?.outputMode}`);
            break;
        case 'mute_state_change_noti':
            console.log(`[${ts}] Mute → position ${msg.position}`);
            break;
        case 'state_check':
            console.log(`[${ts}] Heartbeat`);
            break;
        default:
            console.log(`[${ts}] ${msg.messageType}:`, JSON.stringify(msg));
    }

    res.send('Data received');
});

app.post('/test', (req, res) => {
    console.log(`[${new Date().toISOString()}] Health check`);
    res.send('Data received');
});

app.listen(9284, '0.0.0.0', () => {
    console.log('Notification listener on :9284');
});
```

---

## Common Response Pattern

All API responses (when the device is awake) include:
```json
{
  "code": "G0000",
  "status": { "errorCd": "", "errorMsg": "", "outs": "OK" },
  "version": "1.0.9"
}
```

| `code` value | Meaning |
|-------------|---------|
| `G0000` | Device is awake and operational |
| `SLEEP` | Device is in sleep/standby mode |

### Device behavior in sleep mode

When the device is in sleep mode, most endpoints return only the common fields:
```json
{ "code": "SLEEP", "status": { "errorCd": "", "errorMsg": "", "outs": "OK" }, "version": "1.0.9" }
```

Some endpoints (notably `POST /volume` and `POST /device_get_info`) return an **empty body** regardless of the device state.

### Endpoints returning empty bodies

| Endpoint | Notes |
|----------|-------|
| `POST /volume` | Set volume works (side effect), but response body is always empty |
| `POST /device_get_info` | Always returns empty body |

---

## Device Information

| Property | Value |
|----------|-------|
| Configured name | RoseSalon |
| Model | RS520 |
| MAC | FF:AB:DE:0F:27:98 |
| mDNS hostname | Android-2.local / ROSE-0D1746.local |
| Base OS | Android (okhttp 3.12.0 + Android mDNS) |
| DAC firmware | 1.0.9 |
| AirPlay | Shairport Sync 366.0 (fw 4.3.4) |
| TLS Certificate | HiFiRose Root CA (RSA-4096, SHA-256, Seongnam KR) |
| Rose Connect | v5.9.16-prodRelease (Electron 31.0.2, Chrome 126) |