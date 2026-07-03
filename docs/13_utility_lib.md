# Utility Library — Bibliothèque utilitaire

## Vue d'ensemble

Le dossier `src/lib/` contient les utilitaires partagés entre le serveur et les sous-processus. Tout est dans le namespace `Utils` sauf indication contraire.

---

## Utils

**Fichier :** [src/lib/Utils.h](../src/lib/Utils.h)

Header principal qui inclut toutes les dépendances standard. Inclure `<Utils.h>` suffit pour accéder à std, sigc++, TinyXML, JSON, Logger.

### Fonctions utilitaires

```cpp
// Conversions
string Utils::to_string(double d);
string Utils::to_string(int i);
double Utils::to_double(const string &s);
bool Utils::to_bool(const string &s);  // "true"/"1"/"yes" → true

// Chaînes
string Utils::url_encode(const string &s);
string Utils::replace_all(string str, const string &from, const string &to);
vector<string> Utils::split(const string &str, const string &delim);
string Utils::trim(const string &str);

// Formatage
string Utils::sprintf(const char *fmt, ...);

// Types
typedef unsigned short UWord;  // 16 bits non signé (Modbus)
```

---

## Params

**Fichier :** [src/lib/Params.h](../src/lib/Params.h)

Map string→string avec accès simplifié.

```cpp
Params p;
p.Add("key", "value");     // ajoute ou écrase
p["key"]                   // retourne "" si absent
p.Exists("key")            // teste la présence
p.Delete("key")            // supprime
p.size()                   // nombre d'entrées

// Itération
for (auto &pair : p)
    cDebugDom("params") << pair.first << " = " << pair.second;
```

---

## Logger

**Fichier :** [src/lib/Logger.h](../src/lib/Logger.h)

Système de log multi-domaine avec niveaux.

### Macros

```cpp
cDebug()          << "message debug global";
cInfo()           << "message info";
cWarning()        << "attention";
cError()          << "erreur";
cCritical()       << "critique";

// Avec domaine (filtrage par domaine)
cDebugDom("mqtt") << "message debug du domaine mqtt";
cInfoDom("wago")  << "connexion établie";
```

### Configuration

Les domaines de log peuvent être activés/désactivés via variables d'environnement ou fichier de config. Niveau de log global configurable.

---

## Timer

**Fichier :** [src/lib/Timer.h](../src/lib/Timer.h)

Timer basé sur libuv, intégré dans la boucle événementielle principale.

```cpp
// Timer one-shot
auto t = std::make_shared<Timer>(5.0, []() {
    // callback après 5 secondes
});

// Timer répétitif
auto t = std::make_shared<Timer>(1.0, []() {
    // callback toutes les secondes
}, true /*repeat*/);

// Annulation
t->stop();
```

---

## NTPClock

**Fichier :** [src/lib/NTPClock.h](../src/lib/NTPClock.h)

Synchronisation NTP. Maintient l'heure système précise pour les règles temporelles.

---

## ColorUtils

**Fichier :** [src/lib/ColorUtils.h](../src/lib/ColorUtils.h)

Conversions de couleurs pour les IOs RGB.

```cpp
ColorValue cv;
cv.r = 255; cv.g = 128; cv.b = 0;  // RGB
cv.h = 30; cv.s = 100; cv.v = 100; // HSV

// Conversions
ColorValue::fromRGB(r, g, b);
ColorValue::fromHSV(h, s, v);
ColorValue::fromString("255,128,0");  // format "R,G,B"
string cv.toString();
```

---

## FileUtils

**Fichier :** [src/lib/FileUtils.h](../src/lib/FileUtils.h)

Utilitaires fichiers.

```cpp
bool FileUtils::fileExists(const string &path);
bool FileUtils::dirExists(const string &path);
bool FileUtils::copyFile(const string &src, const string &dst);
string FileUtils::readFile(const string &path);
bool FileUtils::writeFile(const string &path, const string &content);
```

---

## WebSocketFrame

**Fichier :** [src/lib/WebSocketFrame.h](../src/lib/WebSocketFrame.h)

Parsing et construction de frames WebSocket (RFC 6455). Utilisé par `WebSocket`.

---

## SHA1

**Fichier :** [src/lib/SHA1.h](../src/lib/SHA1.h)

Implémentation SHA-1 pour le handshake WebSocket.

---

## base64

**Fichier :** [src/lib/base64.h](../src/lib/base64.h)

Encodage/décodage base64, utilisé pour le handshake WebSocket.

---

## UrlDownloader

**Fichier :** [src/lib/UrlDownloader.h](../src/lib/UrlDownloader.h)

Client HTTP asynchrone basé sur libcurl + libuv. Utilisé par les drivers qui font des requêtes HTTP (Hue, Web, Reolink, etc.).

```cpp
UrlDownloader *dl = new UrlDownloader("http://api.example.com/data", true /*async*/);
dl->setHeader("Authorization", "Bearer token");
dl->setBody("POST", R"({"cmd":"on"})");
dl->resultData.connect([](int status, const string &data) {
    // traite la réponse
});
dl->start();
```

---

## tcpsocket

**Fichier :** [src/lib/tcpsocket.h](../src/lib/tcpsocket.h)

Socket TCP synchrone simple. Utilisé par les drivers qui nécessitent une connexion TCP persistante (LMS Squeezebox CLI, amplis AV réseau).

---

## ExpressionEvaluator

**Fichier :** [src/lib/ExpressionEvaluator.h](../src/lib/ExpressionEvaluator.h)

Évaluation d'expressions mathématiques (basé sur `exprtk`). Utilisé dans `ActionStd` pour les valeurs dynamiques.

```cpp
ExpressionEvaluator eval;
eval.setVariable("temp", 21.5);
eval.setVariable("setpoint", 20.0);
double result = eval.evaluate("(temp - setpoint) * 2.5");
```

---

## Calendar

**Fichier :** [src/lib/Calendar.h](../src/lib/Calendar.h)

Utilitaires calendrier (jours fériés, calculs de dates) utilisés par `TimeRange`.

---

## sunset.h

**Fichier :** [src/lib/sunset.h](../src/lib/sunset.h)

Calcul des heures de lever/coucher du soleil selon GPS (latitude, longitude). Utilisé par `InPlageHoraire`.

---

## ThreadedQueue

**Fichier :** [src/lib/ThreadedQueue.h](../src/lib/ThreadedQueue.h)

File d'attente thread-safe. Utilisée pour passer des données entre threads si nécessaire.

---

## Bibliothèques tierces embarquées

| Bibliothèque | Emplacement | Usage |
|---|---|---|
| `TinyXML` + TinyXPath | `src/lib/TinyXML/` | Parsing/écriture XML |
| `exprtk` | `src/lib/exprtk/` | Évaluation expressions mathématiques |
| `libquickmail` | `src/lib/libquickmail/` | Envoi d'e-mails SMTP |
| `llhttp` | `src/lib/llhttp/` | Parsing HTTP (headers) |
| `sole` | `src/lib/sole/` | Génération d'UUID v4 |
| `sqlite_modern_cpp` | `src/lib/sqlite_modern_cpp/` | Interface C++ moderne pour SQLite |
| `uri_parser` | `src/lib/uri_parser/` | Parsing d'URIs |
| `uvw` | `src/lib/uvw/` | Wrapper C++ de libuv |
| `nlohmann/json` | inclus via `json.hpp` | Parsing JSON (alternative à jansson) |
