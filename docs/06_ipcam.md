# IP Cameras — IPCam

> **Écrit contre le code et les goldens** (E4.5c). Chaque affirmation porte sa provenance :
> `(dérivé, Fichier.cpp:L-L)` pour une lecture du source, `(capturé, <golden>)` pour un extrait
> d'un fichier de référence de `tests/core/golden/`. Les chemins sont relatifs à `src/`.

## Vue d'ensemble

Les caméras IP héritent de `IPCam`, qui hérite de `IOBase` et rend `TSTRING`
(dérivé, [IPCam/IPCam.h:30,67](../src/bin/calaos_server/IPCam/IPCam.h)). Le constructeur pose
`gui_type = "camera"` et `visible = "false"`, puis **réinscrit** l'IO dans les caches de
`ListeRoom` — c'est ce qui la fait entrer dans `cameraCache` :

```cpp
// (dérivé, IPCam/IPCam.cpp:32-43, intégral)
    set_param("gui_type", "camera");
    set_param("visible", "false");

    ioDoc->paramAdd("width", _("Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size."), IODoc::TYPE_INT, false, "");
    ioDoc->paramAdd("rotate", _("Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW."), IODoc::TYPE_INT, false, "");
    //T2.19: insecure by default (user decision) — most cameras serve
    //self-signed HTTPS, existing configs have no insecure param
    ioDoc->paramAdd("insecure", _("Skip TLS certificate verification when connecting to the camera. Default to true (most cameras use self-signed certificates). Set to false to only allow a verified HTTPS connection."), IODoc::TYPE_BOOL, false, "true");

    //Add again to cache, because gui_type has changed
    //Special case for Camera
    ListeRoom::Instance().addIOHash(this);
```

⚠️ Cet `addIOHash()` est bien dans le **constructeur**. `ListeRoom::addIOHash()` ne pousse dans
`cameraCache` que si `gui_type == "camera"`
(dérivé, [ListeRoom.cpp:81-83](../src/bin/calaos_server/ListeRoom.cpp)) ; c'est ce cache que
`buildJsonCameras()` parcourt.

---

## IPCam (classe de base)

**Fichier :** [src/bin/calaos_server/IPCam/IPCam.h](../src/bin/calaos_server/IPCam/IPCam.h)

Surface publique **complète** de la classe :

```cpp
// (dérivé, IPCam/IPCam.h:39-46,64-97, extrait — corps inline et commentaires retirés)
    IPCam(Params &p);
    virtual ~IPCam();

    virtual std::string getVideoUrl() { return ""; }    //flux mjpeg
    virtual std::string getPictureUrl() { return ""; }  //image unique

    virtual Params getCapabilities() { return caps; }
    virtual void activateCapabilities(std::string capability, std::string cmd, std::string value) { }

    virtual DATA_TYPE get_type() { return TSTRING; }
    virtual bool set_value(std::string val);
    virtual void downloadSnapshot(std::function<void(const string &)> dataCb);

    bool tlsInsecure();                                 //T2.19
    void camGet(const string &url);                     //T2.19
    static std::string maskUrlCredentials(const std::string &url);  //T3.3

    virtual bool SaveToXml(pugi::xml_node node) override;
```

⚠️ **Il n'existe ni `getSnapshotUrl()` ni `getMjpegUrl()`.** Les deux seules URLs sont
`getPictureUrl()` (image unique) et `getVideoUrl()` (flux MJPEG). Une caméra qui ne fournit pas
de flux rend une chaîne vide, et l'API HTTP fabrique alors le flux à partir d'instantanés
successifs (dérivé, [JsonApiHandlerHttp.cpp:964-974](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

⚠️ **Il n'existe aucune méthode PTZ** (`MoveUp()`, `MoveDown()`, `MoveLeft()`, `MoveRight()`,
`MoveStop()`, `MoveTo()`, `ZoomIn()`, `ZoomOut()`). Le PTZ passe **entièrement** par
`set_value()` et `activateCapabilities()`, décrits ci-dessous.

### Capacités

`getCapabilities()` rend un `Params`. Les clés reconnues sont documentées dans l'en-tête
(dérivé, [IPCam/IPCam.h:48-63](../src/bin/calaos_server/IPCam/IPCam.h)) :
`ptz` (bool), `position` (int — nombre de positions mémoire, `0` = indisponible), `resolution`
(chaîne, liste séparée par des espaces), `led`, `buzzer`, `privacy` (bool), `quality`,
`brightness`, `contrast`, `color`, `saturation`, `sharpness`, `hue` (int, plage).

### `set_value(string)` — le vocabulaire de commande PTZ

C'est ce que reçoit une action de règle visant une caméra
(dérivé, [IPCam/IPCam.cpp:57-71](../src/bin/calaos_server/IPCam/IPCam.cpp)) :

| Valeur | Effet |
|---|---|
| `move <direction>` | `activateCapabilities("ptz", "move", <direction>)` |
| `save <n>` | `activateCapabilities("position", "save", <n>)` — mémoriser la position `n` |
| `recall <n>` | `activateCapabilities("position", "recall", <n>)` — rappeler la position `n` |

⚠️ Comme pour les lecteurs audio, **une valeur non reconnue ne produit aucune erreur** : elle ne
déclenche rien, mais `EventIOChanged` est émis quand même avec la valeur brute
(dérivé, [IPCam/IPCam.cpp:73-77](../src/bin/calaos_server/IPCam/IPCam.cpp) — l'émission est hors
de la chaîne de `else if`).

### Instantanés

`downloadSnapshot()` réutilise **un seul** `UrlDownloader` par caméra. Si un transfert est déjà en
cours, l'appelant reçoit **immédiatement le dernier instantané connu** (`lastSnapshot`), par un
`Timer::singleShot(0, …)`, au lieu de déclencher un second transfert
(dérivé, [IPCam/IPCam.cpp:124-128](../src/bin/calaos_server/IPCam/IPCam.cpp)). Un échec HTTP
(statut ≠ 200) rappelle le callback avec une **chaîne vide**, après avoir journalisé l'URL
**masquée** (dérivé, [IPCam/IPCam.cpp:116-120](../src/bin/calaos_server/IPCam/IPCam.cpp)).

---

## Sécurité : credentials et TLS

### Les identifiants ne fuient plus dans les logs (T3.3)

Toute URL de caméra remise à une instruction de journalisation doit passer par
`IPCam::maskUrlCredentials()`, qui masque le mot de passe d'un `userinfo`
(`http://user:secret@host/…`) **et** la valeur des paramètres de requête connus — `usr`/`pwd`
Foscam, `account`/`passwd`/`_sid` Synology, et les génériques
`user`/`username`/`password`/`loginuse`/`loginpas`
(dérivé, [IPCam/IPCam.h:90-95](../src/bin/calaos_server/IPCam/IPCam.h), implémentation déplacée
dans `Utils::maskUrlCredentials` — dérivé,
[IPCam/IPCam.cpp:94-99](../src/bin/calaos_server/IPCam/IPCam.cpp)).

Corrigé au passage dans `StandardMjpeg` : une boucle de débogage résiduelle **déversait tous les
paramètres au journal**, `url_jpeg` et `url_mjpeg` compris — qui peuvent contenir un
`http://user:pass@cam/` (dérivé,
[IPCam/StandardMjpeg.cpp:37-38](../src/bin/calaos_server/IPCam/StandardMjpeg.cpp)).

⚠️ **Le masquage ne rend pas le transport sûr** : les protocoles Foscam et Planet passent les
identifiants **en clair dans l'URL**. Sur un réseau non maîtrisé, il faut du HTTPS ou un proxy.

### TLS : non vérifié par défaut, option `insecure` par caméra (T2.19)

Décision utilisateur : la majorité des caméras servent du HTTPS **auto-signé** ; un défaut
« vérifié » casserait toutes les installations existantes. Chaque caméra accepte donc un
paramètre `insecure`, **`"true"` par défaut**.

⚠️ **Seule la chaîne exacte `false` active la vérification.** Absent, vide, `"True"`, `"False"`,
`"0"` — tout le reste vaut non vérifié, et la comparaison est **sensible à la casse**. Voir
[02_io_drivers.md](02_io_drivers.md#option-insecure--tls-des-urls-configurées-par-lutilisateur).

**Tous** les chemins de transfert vers une caméra honorent ce paramètre, et c'est le point
important — un seul oubli suffirait à rendre l'option mensongère :

| chemin | site |
|---|---|
| Instantané | [IPCam/IPCam.cpp:108](../src/bin/calaos_server/IPCam/IPCam.cpp) |
| Commandes PTZ des drivers (`camGet()`) | [IPCam/IPCam.h:82-88](../src/bin/calaos_server/IPCam/IPCam.h) |
| API Synology (login, info d'API, instantané) | [IPCam/SynoSurveillanceStation.cpp:176,211,244](../src/bin/calaos_server/IPCam/SynoSurveillanceStation.cpp) |
| Relais MJPEG de l'API HTTP | [JsonApiHandlerHttp.cpp:982-987](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) |
| Pièce jointe d'une règle (`ActionCameraDownload`) | [Rules/ActionCameraDownload.h:106-109](../src/bin/calaos_server/Rules/ActionCameraDownload.h) |

---

## Implémentations

| Type XML | Alias | Classe | Marque / modèle |
|---|---|---|---|
| `Axis` | — | `Axis` | Caméras Axis |
| `Foscam` | — | `Foscam` | Caméras Foscam |
| `Planet` | — | `Planet` | Caméras Planet (ICA-210/210W/300/302/500) |
| `StandardMjpeg` | `standard_mjpeg` | `StandardMjpeg` | Tout flux MJPEG/JPEG standard |
| `SynoSurveillanceStation` | — | `SynoSurveillanceStation` | Synology Surveillance Station |

(dérivé, [IPCam/Axis.cpp:26](../src/bin/calaos_server/IPCam/Axis.cpp),
[IPCam/Foscam.cpp:26](../src/bin/calaos_server/IPCam/Foscam.cpp),
[IPCam/Planet.cpp:26](../src/bin/calaos_server/IPCam/Planet.cpp),
[IPCam/StandardMjpeg.cpp:26-27](../src/bin/calaos_server/IPCam/StandardMjpeg.cpp),
[IPCam/SynoSurveillanceStation.cpp:27](../src/bin/calaos_server/IPCam/SynoSurveillanceStation.cpp).)

⚠️ **Les types XML sont les noms de classe.** Il n'existe **ni `IPCamAxis`, ni `IPCamFoscam`, ni
`IPCamPlanet`, ni `IPCamMJPEG`, ni `IPCamSyno`** : écrire l'un de ces cinq noms dans `io.xml`
produit `Unknown Input type !` et la caméra est ignorée. La recherche est en revanche
**insensible à la casse** (voir [02_io_drivers.md](02_io_drivers.md)), donc `foscam` fonctionne.

### Le driver **Gadspot a été supprimé** (T3.6)

Plus aucun utilisateur, matériel obsolète. Vérifié : `src/` et `tests/` n'en portent plus la
moindre trace. Une configuration qui référence encore `type="Gadspot"` **démarre normalement** —
la caméra est ignorée avec un avertissement au journal — mais la ligne est **effacée d'`io.xml`
dès la première réécriture, qu'un simple redémarrage déclenche 0,1 s après le boot**. Le contrat
complet, et pourquoi la perte n'est pas hypothétique, sont décrits dans
[02_io_drivers.md](02_io_drivers.md#drivers-retirés).

### Il n'y a pas de « paramètres communs » de caméra

Seuls `width`, `rotate` et `insecure` sont déclarés par la classe de base
(dérivé, [IPCam/IPCam.cpp:35-39](../src/bin/calaos_server/IPCam/IPCam.cpp)). Le constructeur
force par ailleurs `port = "80"` **s'il est absent**
(dérivé, [IPCam/IPCam.cpp:29-30](../src/bin/calaos_server/IPCam/IPCam.cpp)) — mais aucun `port`
n'est déclaré à ce niveau, et tous les drivers ne l'utilisent pas.

⚠️ **Il n'y a ni paramètre `user` ni paramètre `path`.** Les identifiants s'appellent
`username` / `password`, et chaque driver a ses propres clés :

| Type XML | Paramètres |
|---|---|
| `Axis` | `model` (obligatoire), `ptz`, `zoom_step`, `pan_framesize`, `tilt_framesize`, `resolution` |
| `Foscam` | `host`, `port` (défaut `88`), `username`, `password` (tous obligatoires), `ptz`, `zoom_step` (défaut `1`) |
| `Planet` | `model` (obligatoire : `ICA-210`, `ICA-210W`, `ICA-300`, `ICA-302`, `ICA-500`), `username`, `password` |
| `StandardMjpeg` | `url_jpeg` (obligatoire), `url_mjpeg`, `ptz` |
| `SynoSurveillanceStation` | `url` (obligatoire, URL complète du NAS, ex. `https://192.168.0.22:5000`), `username`, `password`, `camera_id` (obligatoires), `camera_profile` (0 = haute qualité, 1 = équilibré, 2 = bande passante réduite ; défaut 1) |

(dérivé, [IPCam/Axis.cpp:34-39](../src/bin/calaos_server/IPCam/Axis.cpp),
[IPCam/Foscam.cpp:40-45](../src/bin/calaos_server/IPCam/Foscam.cpp),
[IPCam/Planet.cpp:37-39](../src/bin/calaos_server/IPCam/Planet.cpp),
[IPCam/StandardMjpeg.cpp:33-35](../src/bin/calaos_server/IPCam/StandardMjpeg.cpp),
[IPCam/SynoSurveillanceStation.cpp:36-40](../src/bin/calaos_server/IPCam/SynoSurveillanceStation.cpp).)

⚠️ **Sur `StandardMjpeg`, la capacité PTZ dépend de la *présence* du paramètre `ptz`, pas de sa
valeur** : `ptz="false"` annonce quand même une caméra PTZ (et 8 positions mémoire), parce que le
test est `param.Exists("ptz")`
(dérivé, [IPCam/StandardMjpeg.cpp:41-49](../src/bin/calaos_server/IPCam/StandardMjpeg.cpp)). Pour
déclarer une caméra sans PTZ, **omettre le paramètre**.

### Synology : le callback d'instantané ne se déclenche plus deux fois (T3.3)

La branche « contenu non-JPEG » de `getSnapshot()` appelait `cb({})` puis **retombait** sur
`cb(data)` : le callback de fin de transfert partait **deux fois** sur une erreur ou un corps
JSON, ce qui rejouait le traitement et corrompait l'état interne (`isRunning`, `lastSnapshot`).
Il n'y a désormais **qu'un seul site d'appel**, et la décision « image ou rien » est isolée dans
une fonction pure :

```cpp
// (dérivé, IPCam/SynoSurveillanceStation.h:45-51, intégral)
    static string snapshotPayload(int status, const string &contentType,
                                  const string &data)
    {
        if (status != 200 || contentType != "image/jpeg")
            return {};
        return data;
    }
```

(dérivé, [IPCam/SynoSurveillanceStation.cpp:196-198](../src/bin/calaos_server/IPCam/SynoSurveillanceStation.cpp)
pour le site d'appel unique.)

---

## Accès depuis l'API JSON

### `get_home` : le tableau `cameras`

`buildJsonCameras()` rend **quatre clés par caméra**, et **aucune URL** :
`id`, `name`, `type` (le type XML), et `ptz` (`"true"` / `"false"`, d'après
`getCapabilities()["ptz"]`)
(dérivé, [JsonApi.cpp:359-387](../src/bin/calaos_server/JsonApi.cpp)).

```json
// (capturé, tests/core/golden/ws_get_home.json, extrait — le tableau "cameras")
    "cameras": [
      {
        "id": "e40_cam_ptz",
        "name": "Camera PTZ",
        "ptz": "true",
        "type": "StandardMjpeg"
      },
      {
        "id": "e40_cam_plain",
        "name": "Camera plain",
        "ptz": "false",
        "type": "StandardMjpeg"
      }
    ],
```

⚠️ **Il n'y a ni `video_url` ni `mjpeg_url` dans ce payload.** Un client n'obtient pas d'URL de
caméra : il demande l'image ou le flux **au serveur**, qui relaie
(voir [08_http_api.md](08_http_api.md)).

### Image et flux

Deux familles d'accès, **HTTP uniquement** pour la seconde :

| requête | réponse |
|---|---|
| `action: get_camera_pic` | JPEG **encodé en base64** dans un document JSON |
| `camera` / `type: get_picture` | octets `image/jpeg` **bruts** |
| `camera` / `type: get_video` | flux MJPEG relayé |

(dérivé, [JsonApiHandlerHttp.cpp:586-591,913,962-990](../src/bin/calaos_server/JsonApiHandlerHttp.cpp) ;
le détail des arguments `width` et `rotate` est dans [08_http_api.md](08_http_api.md).)

`get_camera_pic` répond `{"success":"true", "contenttype":"image/jpeg", "encoding":"base64",
"data": …}` (dérivé, [JsonApiHandlerHttp.cpp:586-591](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)).

⚠️ **Un instantané vide n'est pas une erreur sur `camera` / `get_picture`** : le serveur répond
`200` avec une **image de remplacement**, `camfail.jpg`, prise dans son répertoire de données
(dérivé, [JsonApiHandlerHttp.cpp:944-951](../src/bin/calaos_server/JsonApiHandlerHttp.cpp)). Un
client qui teste « ai-je reçu une image ? » recevra donc toujours une image.

Messages d'erreur, tels qu'épinglés :

```json
// (capturé, tests/core/golden/e40e_http_camera_unknown_id.json, intégral)
{
  "error": "unkown camera id"
}
```

```json
// (capturé, tests/core/golden/e40e_http_get_camera_pic_unknown_id.json, intégral)
{
  "error_str": "id not set",
  "success": "false"
}
```

```json
// (capturé, tests/core/golden/e40e_http_get_camera_pic_invalid_width.json, intégral)
{
  "error_str": "invalid width or rotate parameter",
  "success": "false"
}
```

⚠️ La coquille `unkown` est dans le code et **elle est gelée** : la corriger casserait les
clients qui la comparent.

### Plus de plantage sur déconnexion pendant un instantané (T3.17d)

Demander l'image d'une caméra déclenche un aller-retour vers celle-ci. Si le client se
déconnectait pendant ce transfert, l'image revenait sur une connexion détruite et **faisait
planter le serveur** — atteignable depuis l'API JSON sans manipulation particulière.

**Ce qui ne change pas, et c'est le point important** : un client **encore connecté** reçoit
toujours son image **complète**, même si la caméra est lente, et même si l'équipement caméra est
**supprimé pendant le transfert** — l'image déjà en vol est délivrée intégralement, jamais
tronquée. Seul le client **déjà parti** ne reçoit plus rien. Aucune forme de réponse nouvelle
n'est introduite.

---

## Intégration dans les règles

Les caméras peuvent être :

- **cibles d'une action** : `move <direction>`, `save <n>`, `recall <n>` (voir plus haut) ;
- **cibles d'une `ActionTouchscreen`** — l'affichage d'une caméra sur un écran, qui émet un
  événement `touchscreen_camera_request` :

```json
// (capturé, tests/core/golden/e40d_ws_touchscreen_camera.json, intégral)
{
  "data": {
    "data": {
      "id": "e40_cam_ptz"
    },
    "event_raw": "touchscreen_camera_request id:e40_cam_ptz",
    "type": "21",
    "type_str": "touchscreen_camera_request"
  },
  "msg": "event"
}
```

- **attachées à une `ActionPush`** via `notifPicUuid` : une capture est téléchargée et jointe à la
  notification, en honorant le paramètre `insecure` de la caméra
  (dérivé, [Rules/ActionCameraDownload.h:106-109](../src/bin/calaos_server/Rules/ActionCameraDownload.h)).
  Voir [10_events_notifications.md](10_events_notifications.md).
