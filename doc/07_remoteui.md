# RemoteUI — Appareils embarqués connectés

## Vue d'ensemble

Le sous-système RemoteUI permet à des appareils embarqués (tableaux de bord, panneaux muraux, ESP32-S3, etc.) de se connecter au serveur Calaos via WebSocket avec authentification HMAC-SHA256.

L'appareil peut :
- Recevoir les états de tous les IOs en temps réel
- Envoyer des commandes (appui bouton, relais, etc.)
- Recevoir des mises à jour firmware OTA

---

## Architecture

```
[Appareil embarqué]
       ↕ WebSocket TLS
[RemoteUIWebSocketHandler]  (par connexion)
       ↕
[RemoteUIManager]           (singleton, registre global)
       ↕
[RemoteUI IO]               (dans ListeRoom, ex: RemoteUIOutputRelay)
```

---

## RemoteUI (IO)

**Dossier :** [src/bin/calaos_server/IO/RemoteUI/](../src/bin/calaos_server/IO/RemoteUI/)

Un IO de type `RemoteUI` représente un appareil embarqué dans le modèle de données. Il est configuré comme n'importe quel IO dans `local_config.xml`.

### Paramètres

| Paramètre | Description |
|---|---|
| `token` | Token unique d'authentification de l'appareil |
| `name` | Nom de l'appareil |
| `type` | `"RemoteUI"` |

---

## RemoteUIManager

**Fichier :** [src/bin/calaos_server/RemoteUI/RemoteUIManager.h](../src/bin/calaos_server/RemoteUI/RemoteUIManager.h)

Singleton gérant toutes les connexions RemoteUI actives.

### Authentification

Le protocole d'authentification utilise **HMAC-SHA256** avec protection contre les attaques replay :

```
Client → Server : {token, timestamp, nonce, hmac}
  hmac = HMAC-SHA256(secret_key, token + ":" + timestamp + ":" + nonce)
```

Limites de sécurité :
- `MAX_ATTEMPTS_PER_MINUTE = 20` — rate limiting par IP
- `NONCE_EXPIRY_SECONDS = 300` — nonce valide 5 min
- `TIMESTAMP_TOLERANCE_SECONDS = 30` — tolérance ±30s sur l'horloge

```cpp
AuthFailureReason reason = RemoteUIManager::Instance().validateAuthenticationWithReason(
    token, timestamp, nonce, hmac, ip_address
);
```

### Envoi de commandes vers un appareil

```cpp
RemoteUIManager::Instance().sendCommand(
    "remote-ui-id",
    "relay_set",
    { {"relay_num": 1}, {"value": true} }
);
```

### Notifications d'état

```cpp
RemoteUIManager::Instance().notifyAllIOStates();  // envoi à tous les appareils connectés
RemoteUIManager::Instance().notifyOtaUpdates();   // notification de dispo firmware
```

---

## RemoteUIWebSocketHandler

**Fichier :** [src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.h](../src/bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.h)

Gestionnaire de connexion WebSocket pour un appareil RemoteUI. Traite les messages entrants et sortants.

### Messages entrants (appareil → serveur)

| Type | Description |
|---|---|
| `auth` | Authentification initiale |
| `relay_update` | Mise à jour d'état d'un relais depuis l'appareil |
| `config_request` | Demande de configuration |
| `ota_request` | Demande de mise à jour firmware |

### Messages sortants (serveur → appareil)

| Type | Description |
|---|---|
| `auth_ok` / `auth_fail` | Résultat authentification |
| `io_state` | État d'un IO |
| `relay_set` | Commande de relais |
| `config_update` | Push de configuration |
| `ota_update` | Notification de firmware disponible |

---

## RemoteUIOutputRelay

**Fichier :** [src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.h](../src/bin/calaos_server/IO/RemoteUI/RemoteUIOutputRelay.h)

IO de type `OutputLight` (TBOOL) représentant un relais physique sur l'appareil RemoteUI.

```cpp
class RemoteUIOutputRelay : public OutputLight {
    string remote_ui_id;   // ID de l'appareil RemoteUI
    int relay_num;         // numéro du relais sur l'appareil

    // Commande → appareil
    virtual bool set_value_real(bool val) override;
    
    // Mise à jour depuis l'appareil (sans reboucler)
    void updateStateFromDevice(bool val);
};
```

### Paramètres

| Paramètre | Description |
|---|---|
| `remote_ui_id` | ID de l'IO RemoteUI parent |
| `relay_num` | Numéro du relais (0-based) |
| `name` | Nom du relais |

---

## OTA Firmware

**Fichiers :** [src/bin/calaos_server/RemoteUI/OtaFirmwareManager.h](../src/bin/calaos_server/RemoteUI/OtaFirmwareManager.h), [OtaHttpHandler.h](../src/bin/calaos_server/RemoteUI/OtaHttpHandler.h), [FirmwareManifest.h](../src/bin/calaos_server/RemoteUI/FirmwareManifest.h)

Gestion des mises à jour firmware over-the-air pour les appareils RemoteUI.

- `FirmwareManifest` — décrit un firmware (version, URL, checksum)
- `OtaFirmwareManager` — vérifie les firmwares disponibles, compare les versions
- `OtaHttpHandler` — endpoint HTTP pour télécharger le firmware

---

## Provisioning

**Fichier :** [src/bin/calaos_server/RemoteUI/RemoteUIProvisioningHandler.h](../src/bin/calaos_server/RemoteUI/RemoteUIProvisioningHandler.h)

Endpoint HTTP de provisioning. Permet à un nouvel appareil de recevoir sa configuration initiale (token, identifiants Wi-Fi, etc.).

---

## HMACAuthenticator

**Fichier :** [src/bin/calaos_server/RemoteUI/HMACAuthenticator.h](../src/bin/calaos_server/RemoteUI/HMACAuthenticator.h)

Classe utilitaire pour calculer et vérifier les HMAC-SHA256. Utilisée par `RemoteUIManager`.

---

## RemoteUISecurityLimits

**Fichier :** [src/bin/calaos_server/RemoteUI/RemoteUISecurityLimits.h](../src/bin/calaos_server/RemoteUI/RemoteUISecurityLimits.h)

Constantes de sécurité centralisées (rate limiting, expiration de nonce, tolérance horloge).

---

## AuthFailureReason

**Fichier :** [src/bin/calaos_server/RemoteUI/AuthFailureReason.h](../src/bin/calaos_server/RemoteUI/AuthFailureReason.h)

Enum décrivant la raison d'échec d'authentification (token inconnu, HMAC invalide, nonce expiré, rate limit, etc.). Retourné à l'appareil dans le message `auth_fail`.
