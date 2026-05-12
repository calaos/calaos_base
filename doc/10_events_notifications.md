# Events & Notifications — EventManager, Push, E-mail

## Vue d'ensemble

Deux mécanismes de communication sortante :
1. **EventManager** — diffusion interne des changements d'état aux clients connectés (WebSocket)
2. **NotifManager** — notifications externes (push mobile, e-mail)

---

## EventManager

**Fichier :** [src/bin/calaos_server/EventManager.h](../src/bin/calaos_server/EventManager.h)

Singleton. Point central de dispatch de tous les événements Calaos vers les clients connectés.

### Création d'un événement

```cpp
// Événement simple
EventManager::create(CalaosEvent::EventIOChanged);

// Avec paramètres
EventManager::create(CalaosEvent::EventIOChanged, ioId, {
    {"value", "true"},
    {"type", "bool"}
});

// Sans log dans l'historique
EventManager::create(CalaosEvent::EventIOChanged, p, false);
```

### Réception

```cpp
EventManager::Instance().newEvent.connect(
    [](const CalaosEvent &ev) {
        // traitement...
    }
);
```

Le `JsonApiHandlerWS` se connecte à ce signal pour pousser les événements à tous les clients WebSocket.

### Types d'événements (CalaosEvent)

| Constante | Description |
|---|---|
| `EventIOAdded` | Nouvel IO créé |
| `EventIODeleted` | IO supprimé |
| `EventIOChanged` | Valeur d'un IO modifiée |
| `EventIOPropertyDelete` | Propriété IO supprimée |
| `EventRoomAdded` / `Deleted` / `Changed` | Modification d'une pièce |
| `EventTimeRangeChanged` | Plage horaire modifiée |
| `EventScenarioAdded` / `Deleted` / `Changed` | Scénario modifié |
| `EventAudioSongChanged` | Chanson courante changée |
| `EventAudioPlaylistAdd` / `Delete` / `Move` / `Reload` / `Cleared` | Playlist modifiée |
| `EventAudioStatusChanged` | État du lecteur (play/pause/stop) |
| `EventAudioVolumeChanged` | Volume modifié |
| `EventTouchScreenCamera` | Afficher une caméra sur un écran |
| `EventPushNotification` | Notification push mobile |
| `EventIOStatusChanged` | Statut IO changé (batterie, connexion…) |

### CalaosEvent

```cpp
class CalaosEvent {
    int evType;
    Params evParams;
    bool logHistory;   // si true, loggé dans HistLogger
    
    json_t *toJson() const;
    string toString() const;
};
```

---

## HistLogger

**Fichier :** [src/bin/calaos_server/HistLogger.h](../src/bin/calaos_server/HistLogger.h)

Journalise les événements dans une base de données SQLite locale. Permet de consulter l'historique des changements d'état.

```cpp
HistLogger::Instance().appendEvent(ev);
```

Accessible via l'API JSON : `buildJsonEventLog()`.

---

## DataLogger

**Fichier :** [src/bin/calaos_server/DataLogger.h](../src/bin/calaos_server/DataLogger.h)

Enregistre les valeurs des IOs dans le temps (pour graphes et statistiques).

---

## NotifManager

**Fichier :** [src/bin/calaos_server/NotifManager.h](../src/bin/calaos_server/NotifManager.h)

Singleton pour l'envoi de notifications externes.

### E-mail

```cpp
NotifManager::Instance().sendMailNotification(
    "Alarme détectée",           // sujet
    "Mouvement détecté en entrée", // corps
    "user@example.com",          // destinataire (optionnel)
    "calaos@maison.local",       // expéditeur (optionnel)
    "/tmp/snapshot.jpg"          // pièce jointe (optionnel)
);
```

Configuration e-mail dans `local_config.xml` :
- `smtp_server` — serveur SMTP
- `smtp_port` — port (défaut 25)
- `smtp_user` / `smtp_password`
- `mail_to` / `mail_from` — adresses par défaut

Implémenté via `libquickmail` ([src/lib/libquickmail/](../src/lib/libquickmail/)).

### Push (notifications mobiles)

```cpp
NotifManager::Instance().sendPushNotification(
    "Lumière allumée",         // message
    "uuid-de-la-photo",        // uuid image dans HistLogger (optionnel)
    []() { /* callback envoi terminé */ }
);
```

Les tokens de push des appareils mobiles sont enregistrés via `JsonApi::registerPushToken()`.

---

## Intégration dans les règles

Les notifications sont déclenchées via les actions :
- `ActionMail` → `NotifManager::sendMailNotification()`
- `ActionPush` → `NotifManager::sendPushNotification()`

Exemple de règle avec notification push + photo :
```xml
<action type="ActionPush">
  <message>Mouvement détecté !</message>
  <camera_id>id-cam-entree</camera_id>
</action>
```
