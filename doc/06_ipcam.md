# IP Cameras — IPCam

## Vue d'ensemble

Le sous-système de caméras IP gère les flux vidéo et les PTZ (Pan-Tilt-Zoom). Les caméras héritent de `IOBase` (type `TSTRING`) et apparaissent dans le cache `cameraCache` de `ListeRoom`.

---

## IPCam (classe de base)

**Fichier :** [src/bin/calaos_server/IPCam/IPCam.h](../src/bin/calaos_server/IPCam/IPCam.h)

```cpp
class IPCam : public IOBase {
public:
    virtual string getVideoUrl();     // URL du flux vidéo
    virtual string getSnapshotUrl();  // URL de capture d'image
    virtual string getMjpegUrl();     // URL du flux MJPEG
    
    virtual void MoveUp();
    virtual void MoveDown();
    virtual void MoveLeft();
    virtual void MoveRight();
    virtual void MoveStop();
    virtual void MoveTo(int pan, int tilt);
    virtual void ZoomIn();
    virtual void ZoomOut();
};
```

### Paramètres communs

| Paramètre | Description |
|---|---|
| `host` | IP ou hostname de la caméra |
| `port` | Port HTTP (défaut 80) |
| `user` | Nom d'utilisateur |
| `password` | Mot de passe |
| `path` | Chemin du flux (optionnel) |

---

## Implémentations

| Classe | Marque/Modèle | Type XML |
|---|---|---|
| `Axis` | Caméras Axis | `IPCamAxis` |
| `Foscam` | Foscam IP cameras | `IPCamFoscam` |
| `Gadspot` | Gadspot | `IPCamGadspot` |
| `Planet` | Planet cameras | `IPCamPlanet` |
| `StandardMjpeg` | Tout flux MJPEG standard | `IPCamMJPEG` |
| `SynoSurveillanceStation` | Synology Surveillance Station | `IPCamSyno` |

---

## Accès depuis l'API JSON

Les caméras sont exposées via `JsonApi::buildJsonCameras()` qui retourne la liste de toutes les caméras avec leurs URLs.

```json
{
  "cameras": [
    {
      "id": "id-cam-001",
      "name": "Entrée",
      "type": "IPCamFoscam",
      "video_url": "rtsp://...",
      "mjpeg_url": "http://..."
    }
  ]
}
```

---

## Intégration dans les règles

Les caméras peuvent être :
- Cibles d'une `ActionTouchscreen` (afficher la caméra sur un écran)
- Attachées à une `ActionPush` via `notifPicUuid` (capture d'image + notification push)
