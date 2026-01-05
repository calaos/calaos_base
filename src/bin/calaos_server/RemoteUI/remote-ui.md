# RemoteUI - Embedded Touch Screen System

## Overview

The Calaos RemoteUI system enables connection of embedded touch screens (ESP32, embedded Linux, etc.) to the main home automation server. It provides a complete architecture for provisioning, secure authentication, and bidirectional communication to create distributed user interfaces throughout the home automation installation.

## System Architecture

### Main Components

```
RemoteUI/
├── RemoteUI.h/.cpp                    # Class representing a touch screen
├── RemoteUIManager.h/.cpp             # Singleton manager for RemoteUIs
├── RemoteUIProvisioningHandler.h/.cpp # HTTP handler for provisioning
├── RemoteUIWebSocketHandler.h/.cpp    # WebSocket handler for communication
├── HMACAuthenticator.h/.cpp           # Secure HMAC authentication
└── RemoteUIConfig.h/.cpp              # XML parser/writer for configuration
```

### Data Flow

```
[RemoteUI Device]
    ↓ HTTP Provisioning
[RemoteUIProvisioningHandler]
    ↓ Configuration
[RemoteUIManager]
    ↓ WebSocket Auth
[RemoteUIWebSocketHandler]
    ↔ Bidirectional Communication
[EventManager] ← [IOBase Objects]
```

## Provisioning Process

### 1. Provisioning Request

A new touch screen initiates a provisioning request via HTTP POST.

**Endpoint**: `POST /api/v3/provision/request`

**Request Structure**:
```json
{
  "provisioning_code": "ABC123",
  "device_info": {
    "type": "esp32_touch_panel",
    "manufacturer": "Calaos",
    "model": "TouchPanel v1.0",
    "firmware": "1.2.3",
    "mac_address": "AA:BB:CC:DD:EE:FF",
    "capabilities": {
      "screen_resolution": "480x320",
      "touch_support": true,
      "wifi": true,
      "bluetooth": false
    }
  }
}
```

### 2. Provisioning Processing

The server validates the provisioning code and creates a new RemoteUI object:

1. **Code validation**: Verify that the code exists and is not already used
2. **RemoteUI creation**: Generate unique ID and authentication token
3. **Secret generation**: Create HMAC secret for future authentication
4. **Persistence**: Save configuration to `io.xml`

**Provisioning Response**:
```json
{
  "success": true,
  "remote_ui": {
    "id": "remote_ui_ABC123",
    "auth_token": "a7f3b5d2e9c1b4a6f8e3d5c2a9b7e4d6c1f8a5e2d9b6c3f0a7d4b1e8c5a2f9d6",
    "device_secret": "7f3b5a2d9e1c4b6a8c3f5e2d9a7b4c6e1f8d5a2c9b6e3f0d7a4b1c8e5f2a9",
    "websocket_endpoint": "ws://192.168.1.100:5454/api/v3/remote_ui/ws",
    "config": {
      "name": "Living Room Screen",
      "room": "living_room",
      "theme": "dark",
      "brightness": 80,
      "timeout": 30,
      "pages": []
    }
  }
}
```

### 3. Provisioning Security

The provisioning endpoint is protected against brute force attacks through multiple security mechanisms:

#### Rate Limiting
- **Per-IP rate limiting**: Maximum 1 request per 10 seconds per IP address
- **Client retry interval**: Legitimate devices retry every 10 seconds, matching the server's rate limit
- **Error response**: HTTP 429 "Too many requests or blacklisted" when limit exceeded

#### Code Switching Detection
The server tracks provisioning codes attempted by each IP address:
- **Tracking window**: 1 hour sliding window
- **Detection threshold**: Maximum 10 different codes per IP per hour
- **Legitimate behavior**: A real device always uses the same provisioning code
- **Attack pattern**: An attacker trying multiple codes is detected and blocked

#### Automatic Blacklisting
When an IP exceeds the code switching threshold:
- **Blacklist duration**: 1 hour
- **Security alert**: Logged with severity WARNING
- **Automatic cleanup**: Tracking data and blacklist entries are automatically cleaned after expiration

#### Implementation Details
```cpp
// Security constants
RATE_LIMIT_SECONDS = 10          // Min 10s between requests per IP
MAX_CODES_PER_IP = 10            // Max different codes per IP per hour
TRACKING_WINDOW_SECONDS = 3600   // 1 hour tracking window
BLACKLIST_DURATION_SECONDS = 3600 // 1 hour blacklist
```

#### Device Provisioning Flow
1. **Device generates code**: The RemoteUI device generates and displays its provisioning code
2. **Admin adds code**: Administrator adds the code to the server's `io.xml` configuration
3. **Device retries**: Device retries provisioning request every 10 seconds
4. **Success**: Once the code is added to config, the next retry succeeds
5. **Security**: Only the legitimate code will succeed, brute force attempts are blocked

This approach ensures that:
- Legitimate devices are never blocked (they use the same code)
- Brute force attacks are detected and prevented
- No manual intervention required to unblock legitimate devices
- Attack attempts are logged for security monitoring

### Authentication Token Security

Authentication tokens (`auth_token`) are generated using cryptographically secure random number generation (OpenSSL `RAND_bytes()`). Each token contains 256 bits of entropy, making them cryptographically secure and impossible to predict or guess.

**Token Format**:
- **Length**: 64 characters (32 bytes hex-encoded)
- **Character set**: Hexadecimal (0-9, a-f)
- **Entropy**: 256 bits (cryptographic strength)
- **Example**: `a7f3b5d2e9c1b4a6f8e3d5c2a9b7e4d6c1f8a5e2d9b6c3f0a7d4b1e8c5a2f9d6`

**Security Properties**:
- Tokens are generated independently of provisioning codes using cryptographically secure random generation
- Collision probability is negligible (< 2^-256)
- Tokens cannot be derived, predicted, or enumerated from any user-controlled input
- Each device receives a unique, unpredictable token during provisioning

**Backward Compatibility**:
Devices provisioned before version 4.5 may have tokens in the older format (`remote_ui_<code>`). These devices continue to function normally without requiring updates. To upgrade old devices to cryptographically secure tokens, administrators can delete the `device_secret` and `auth_token` parameters from `io.xml` and re-provision the device.

### Time Synchronization Requirements

**Critical:** RemoteUI clients MUST maintain accurate time synchronization for authentication to work correctly.

#### Server Configuration
- **Timestamp tolerance**: ±30 seconds
- Requests with timestamps outside this window will be rejected
- Error messages: `"Timestamp too old/new"` or `"Timestamp rejected - check client time synchronization"`

#### Client Requirements

RemoteUI clients (ESP32, embedded Linux, etc.) must implement NTP/SNTP time synchronization to ensure authentication works reliably.

**ESP32 Example (NTP/SNTP Setup):**
```cpp
#include <time.h>
#include "esp_sntp.h"

void setupTimeSync() {
    // Configure SNTP
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    // Wait for time sync (max 10 seconds)
    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < 100) {
        delay(100);
    }

    // Set timezone (example: Europe/Paris)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void setup() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);

    setupTimeSync();  // MUST be called after WiFi connection

    // Now safe to authenticate with Calaos server
}
```

**Linux Embedded Devices:**
```bash
# Install NTP client
apt-get install ntpdate

# Sync with NTP server
ntpdate -s pool.ntp.org

# Or use systemd-timesyncd
timedatectl set-ntp true
```

#### Troubleshooting Time Sync Issues

**Symptom:** Authentication fails with `"Timestamp too old/new"` or `"Timestamp rejected"`

**Solutions:**
1. Verify NTP sync is working: `sntp_get_sync_status()` on ESP32
2. Check system time: `time(NULL)` should return current Unix timestamp
3. Verify network connectivity to NTP servers (pool.ntp.org, time.google.com, etc.)
4. Check timezone configuration (use UTC or configure correctly)
5. Monitor server logs for `"Large time difference"` warnings (indicates approaching the tolerance limit)

**Clock Drift:**
- **Acceptable drift**: < 15 seconds (server logs info message)
- **Warning threshold**: > 15 seconds (server logs detailed warning)
- **Rejection threshold**: > 30 seconds (authentication fails)
- **Recommendation**: Re-sync NTP every 1-24 hours depending on device clock accuracy
- **ESP32 typical drift**: 50-100 ppm (8-10 seconds per day)

### Device Boot Sequence (After Initial Provisioning)

After a device has been provisioned, the following boot sequence should be implemented:

#### Boot Flow
```
┌─────────────────────────────────────────────────────────────────┐
│                       DEVICE BOOT                               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌─────────────────┐
                    │  Connect WiFi   │
                    └─────────────────┘
                              │
                              ▼
                    ┌─────────────────┐
                    │  NTP Time Sync  │
                    └─────────────────┘
                              │
                              ▼
                    ┌───────────────────┐
                    │ Has credentials?  │───No───▶ Initial Provisioning
                    │(auth_token+secret)│        (POST /api/v3/provision/request)
                    └───────────────────┘
                              │
                             Yes
                              │
                              ▼
                    ┌──────────────────┐
                    │ Connect WebSocket│
                    │   with HMAC      │
                    └──────────────────┘
                              │
              ┌───────────────┴───────────────┐
              │                               │
           Success                         Error
              │                               │
              ▼                               ▼
    ┌──────────────────┐            ┌─────────────────┐
    │ Normal operation │            │ Check HTTP code │
    │  (receive states,│            └─────────────────┘
    │   send actions)  │                   │
    └──────────────────┘         ┌─────────┴─────────┐
                                 │                   │
                          401/403               Other (429, 500...)
                       (Invalid token          (Temporary error)
                        or HMAC)                     │
                                 │                   │
                                 ▼                   ▼
                    ┌────────────────────┐    ┌─────────────────┐
                    │ Clear credentials  │    │  Retry with     │
                    │ (delete auth_token │    │  exponential    │
                    │  and device_secret)│    │  backoff        │
                    └────────────────────┘    └─────────────────┘
                                 │
                                 ▼
                    ┌──────────────────┐
                    │ Re-provision     │
                    │ (show new code,  │
                    │  POST /provision)│
                    └──────────────────┘
```

#### WebSocket Authentication Error Responses

When WebSocket connection fails due to authentication issues, the server returns HTTP error responses (not WebSocket) with JSON body:

| HTTP Code | Error String | Meaning | Device Action |
|-----------|--------------|---------|---------------|
| 400 | `missing_headers` | Required auth headers not provided | Check code, retry |
| 401 | `invalid_token` | Token unknown/deleted on server | **Clear credentials & re-provision** |
| 401 | `invalid_timestamp` | Clock out of sync (>30s drift) | Sync NTP, retry |
| 401 | `invalid_nonce` | Nonce reused or wrong format | Generate new nonce, retry |
| 403 | `invalid_hmac` | HMAC mismatch (wrong secret) | **Clear credentials & re-provision** |
| 429 | `rate_limited` | Too many attempts | Wait 1 minute, retry |

**Response Format:**
```json
{
  "error": "invalid_token",
  "status": "authentication_failed"
}
```

#### Critical Decision: When to Re-provision

The device MUST clear its stored credentials and initiate re-provisioning when:
- HTTP 401 with `invalid_token` error (token was deleted/changed on server)
- HTTP 403 with `invalid_hmac` error (secret was changed on server)

The device SHOULD NOT re-provision for temporary errors:
- HTTP 401 with `invalid_timestamp` → Sync NTP and retry
- HTTP 401 with `invalid_nonce` → Generate new nonce and retry
- HTTP 429 → Wait and retry
- Network errors → Retry with backoff

#### ESP32 Implementation Example

```cpp
void connectToServer() {
    // Build HMAC authentication headers
    String timestamp = String(time(nullptr));
    String nonce = generateNonce();  // 64 hex chars
    String hmac = computeHMAC(auth_token + timestamp + nonce, device_secret);

    // Connect WebSocket with headers
    webSocket.setExtraHeaders(
        "Authorization: Bearer " + auth_token + "\r\n"
        "X-Auth-Timestamp: " + timestamp + "\r\n"
        "X-Auth-Nonce: " + nonce + "\r\n"
        "X-Auth-HMAC: " + hmac
    );

    webSocket.begin(server_host, 5454, "/api/v3/remote_ui/ws");
}

void handleConnectionError(int httpCode, String response) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, response);
    String error = doc["error"];

    if (httpCode == 401 && error == "invalid_token") {
        // Server doesn't recognize our token - must re-provision
        clearCredentials();
        startProvisioning();
    }
    else if (httpCode == 403 && error == "invalid_hmac") {
        // Secret mismatch - must re-provision
        clearCredentials();
        startProvisioning();
    }
    else if (httpCode == 401 && error == "invalid_timestamp") {
        // Clock drift - sync NTP and retry
        syncNTP();
        delay(1000);
        connectToServer();
    }
    else {
        // Temporary error - retry with backoff
        delay(retryDelay);
        retryDelay = min(retryDelay * 2, 60000);  // Max 1 minute
        connectToServer();
    }
}
```

## HMAC Authentication

### Security Principles

Authentication uses HMAC-SHA256 to guarantee:
- **Integrity**: Messages cannot be modified
- **Authentication**: Only devices with the correct secret can connect
- **Anti-replay protection**: Using nonces and timestamps
- **Rate limiting**: Limiting authentication attempts

### WebSocket Authentication Process

When connecting via WebSocket, the touch screen must provide the following headers:

```
Authorization: Bearer a7f3b5d2e9c1b4a6f8e3d5c2a9b7e4d6c1f8a5e2d9b6c3f0a7d4b1e8c5a2f9d6
X-Auth-Timestamp: 1640995200
X-Auth-Nonce: 1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3
X-Auth-HMAC: computed_hmac_signature
```

### HMAC Calculation

The HMAC is calculated on the concatenation of:
`auth_token + timestamp + nonce`

```cpp
string data_to_sign = auth_token + timestamp + nonce;
string hmac = compute_hmac_sha256(device_secret, data_to_sign);
```

### Server-Side Validation

1. **Token extraction**: From `Authorization` header
2. **Timestamp validation**: ±30 seconds tolerance
3. **Nonce verification**:
   - **Format**: Exactly 64 hexadecimal characters (32 bytes of random data)
   - **Anti-replay protection**: 5-minute cache prevents nonce reuse
   - **Security**: 256-bit entropy prevents birthday paradox collision attacks
4. **Rate limiting**: Maximum 3 attempts per minute per IP
5. **HMAC calculation and comparison**: Integrity validation

## WebSocket Communication

### Connection and Authentication

**Endpoint**: `ws://server:5454/api/v3/remote_ui/ws`

Once authenticated, the touch screen automatically receives:
- Initial states of all IOs referenced in its configuration
- Real-time updates of state changes

### Server → RemoteUI Messages

#### 1. Initial IO States
```json
{
  "msg": "remote_ui_io_states",
  "data": {
    "living_room_light": {
      "id": "living_room_light",
      "type": "light",
      "state": "true",
      "gui_type": "light",
      "name": "Living Room Light"
    },
    "living_room_temp": {
      "id": "living_room_temp",
      "type": "temp",
      "state": "22.5",
      "gui_type": "temp",
      "name": "Living Room Temperature"
    }
  }
}
```

#### 2. Individual State Update
```json
{
  "msg": "io_state",
  "data": {
    "io_id": "living_room_light",
    "state": "false"
  }
}
```

#### 3. Configuration Update
```json
{
  "msg": "remote_ui_config_update",
  "data": {
    "config": {
      "name": "Living Room Screen",
      "room": "living_room",
      "theme": "light",
      "brightness": 90,
      "timeout": 60,
      "pages": [
        {
          "id": "1",
          "name": "Home",
          "widgets": [
            {
              "type": "button",
              "io": "living_room_light",
              "x": 10,
              "y": 10,
              "w": 100,
              "h": 50
            }
          ]
        }
      ]
    }
  }
}
```

### RemoteUI → Server Messages

#### 1. IO State Change
```json
{
  "msg": "set_state",
  "data": {
    "io": "living_room_light",
    "state": "true"
  }
}
```

#### 2. Configuration Request
```json
{
  "msg": "remote_ui_get_config"
}
```

## XML Configuration

### Structure in io.xml

```xml
<calaos:config>
  <calaos:home>
    <!-- RemoteUI section (optional, backward compatible) -->
    <calaos:remote_uis>
      <calaos:remote_ui id="remote_ui_ABC123">
        <calaos:provisioning_code>ABC123</calaos:provisioning_code>
        <calaos:auth_token>a7f3b5d2e9c1b4a6f8e3d5c2a9b7e4d6c1f8a5e2d9b6c3f0a7d4b1e8c5a2f9d6</calaos:auth_token>
        <calaos:device_secret>7f3b5a2d9e1c4b6a...</calaos:device_secret>
        <calaos:mac_address>AA:BB:CC:DD:EE:FF</calaos:mac_address>

        <!-- Device information -->
        <calaos:device_info>
          <calaos:param name="type" value="esp32_touch_panel"/>
          <calaos:param name="manufacturer" value="Calaos"/>
          <calaos:param name="model" value="TouchPanel v1.0"/>
          <calaos:param name="firmware" value="1.2.3"/>
          <calaos:param name="capabilities" value="{'screen_resolution':'480x320'}"/>
        </calaos:device_info>

        <!-- Interface configuration -->
        <calaos:param name="name" value="Living Room Screen"/>
        <calaos:param name="room" value="living_room"/>
        <calaos:param name="theme" value="dark"/>
        <calaos:param name="brightness" value="80"/>
        <calaos:param name="timeout" value="30"/>

        <!-- Pages and widgets -->
        <calaos:pages>
          <calaos:page id="1" name="Home">
            <calaos:widget type="button" io="living_room_light" x="10" y="10" w="100" h="50"/>
            <calaos:widget type="temp_display" io="living_room_temp" x="120" y="10" w="80" h="40"/>
          </calaos:page>
          <calaos:page id="2" name="Climate">
            <calaos:widget type="thermostat" io="living_room_thermostat" x="10" y="10" w="200" h="150"/>
          </calaos:page>
        </calaos:pages>
      </calaos:remote_ui>
    </calaos:remote_uis>
  </calaos:home>
</calaos:config>
```

## REST Management API

### RemoteUI List

**Endpoint**: `GET /api/v3/remote_ui/list`

**Response**:
```json
{
  "remote_uis": [
    {
      "id": "remote_ui_ABC123",
      "name": "Living Room Screen",
      "room": "living_room",
      "is_online": true,
      "is_provisioned": true,
      "last_seen": "2023-12-31T23:59:59Z",
      "device_info": {
        "type": "esp32_touch_panel",
        "manufacturer": "Calaos",
        "model": "TouchPanel v1.0"
      }
    }
  ]
}
```

### RemoteUI Status

**Endpoint**: `GET /api/v3/remote_ui/status/{id}`

**Response**:
```json
{
  "id": "remote_ui_ABC123",
  "name": "Living Room Screen",
  "room": "living_room",
  "is_online": true,
  "is_provisioned": true,
  "provisioned_at": "2023-12-01T10:00:00Z",
  "last_seen": "2023-12-31T23:59:59Z",
  "config": {
    "brightness": 80,
    "timeout": 30,
    "theme": "dark"
  },
  "referenced_ios": [
    "living_room_light",
    "living_room_temp",
    "living_room_thermostat"
  ]
}
```

## Referenced IO Management

### Automatic Extraction

The system automatically analyzes configured pages and widgets to extract the list of referenced IOs. This list is used for:

1. **Notification filtering**: Only state changes of referenced IOs are sent
2. **Initial states**: On connection, only states of referenced IOs are transmitted
3. **Optimization**: Reduction of network traffic and CPU load

### Extraction Algorithm

```cpp
void RemoteUI::extractReferencedIOs()
{
    referenced_ios.clear();

    // Traverse all pages
    for (const auto &page : config.pages)
    {
        if (page.contains("widgets"))
        {
            // Traverse all widgets in the page
            for (const auto &widget : page["widgets"])
            {
                if (widget.contains("io") && widget["io"].is_string())
                {
                    referenced_ios.insert(widget["io"]);
                }
            }
        }
    }
}
```

## Security and Performance

### Security Measures

1. **HMAC Authentication**: Impossible to forge without the secret
2. **Rate limiting**: Protection against denial of service attacks
3. **Nonce cache**: Anti-replay protection with automatic expiration
   - **Required format**: 64 hexadecimal characters (32 bytes of cryptographically secure random data)
   - **Entropy**: 256 bits minimum to prevent birthday paradox collision attacks
4. **Timestamp validation**: Prevention of replay attacks
5. **Session isolation**: Each RemoteUI has its own WebSocket session

### Performance Optimizations

1. **Smart filtering**: Only referenced IOs generate notifications
2. **Nonce cache**: Automatic cleanup of expired entries (5 minutes)
3. **Rate limit cleanup**: Periodic cleanup of expired limitations
4. **Optimized JSON serialization**: Reuse of JSON objects
5. **Connection pooling**: Reuse of WebSocket connections

### Configuration Parameters

```cpp
// Security
static const int MAX_ATTEMPTS_PER_MINUTE = 3;
static const int NONCE_EXPIRY_SECONDS = 300;    // 5 minutes
static const int TIMESTAMP_TOLERANCE_SECONDS = 30;  // ±30 seconds
static const int NONCE_LENGTH = 64;             // 64 hex characters (32 bytes)

// Performance
static const size_t SECRET_LENGTH = 32;          // 256 bits
static const int CLEANUP_INTERVAL_SECONDS = 60; // Cache cleanup
```

## Integration in Calaos Architecture

### Startup Initialization

```cpp
// In main.cpp
RemoteUIManager::Instance().loadFromConfig();
```

### HttpServer Integration

```cpp
// Automatic routing of HTTP requests
if (remoteUIHandler->canHandleRequest(uri, method))
{
    remoteUIHandler->processRequest(uri, method, data, paramsGET);
}
```

### WebSocket Integration

```cpp
// RemoteUI WebSocket connection routing
if (uri == "/api/v3/remote_ui/ws")
{
    handler = new RemoteUIWebSocketHandler(httpClient);
}
```

### EventManager Integration

```cpp
// Automatic subscription to IO events
void RemoteUIWebSocketHandler::connectToEventManager()
{
    event_connection = EventManager::Instance().subscribeCalaosEvent(
        sigc::mem_fun(*this, &RemoteUIWebSocketHandler::handleIOEvent)
    );
}
```

## Deployment and Maintenance

### System Requirements

- **OpenSSL >= 1.1.0**: For HMAC calculations
- **libuv**: For asynchronous cleanup timers
- **nlohmann/json**: For JSON serialization

### Network Configuration

- **WebSocket Port**: 5454 (same as main Calaos API)
- **Connection timeout**: 30 seconds by default
- **Keep-alive**: Automatic ping/pong every 30 seconds

### Monitoring and Debug

- **Detailed logs**: All events logged with appropriate levels
- **Real-time statistics**: Number of connected screens, auth attempts, etc.
- **Debug interface**: Accessible via existing web debug interface

### Common Troubleshooting

1. **Authentication failure**: Check time synchronization
2. **Rate limiting**: Wait 1 minute between attempts
3. **Expired nonces**: Generate new nonce for each connection
4. **Corrupted configuration**: Validate XML structure of pages

## Client Implementation Example

### ESP32 (Arduino/C++)

```cpp
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>

class CalaosRemoteUI {
private:
    String auth_token;
    String device_secret;
    WebSocketsClient webSocket;

public:
    void connect() {
        // HMAC calculation for authentication
        String timestamp = String(WiFi.getTime());
        String nonce = generateNonce();
        String hmac = calculateHMAC(auth_token + timestamp + nonce);

        // Authentication headers
        String auth_header = "Authorization: Bearer " + auth_token;
        String timestamp_header = "X-Auth-Timestamp: " + timestamp;
        String nonce_header = "X-Auth-Nonce: " + nonce;
        String hmac_header = "X-Auth-HMAC: " + hmac;

        webSocket.setExtraHeaders(auth_header + "\r\n" +
                                 timestamp_header + "\r\n" +
                                 nonce_header + "\r\n" +
                                 hmac_header);

        webSocket.begin("192.168.1.100", 5454, "/api/v3/remote_ui/ws");
    }

    void sendIOState(const String& io_id, const String& state) {
        DynamicJsonDocument doc(200);
        doc["msg"] = "set_state";
        doc["data"]["io"] = io_id;
        doc["data"]["state"] = state;

        String json;
        serializeJson(doc, json);
        webSocket.sendTXT(json);
    }

    String generateNonce() {
        // Generate 32 bytes (256 bits) of cryptographically secure random data
        // Returns 64-character hexadecimal string
        unsigned char nonce[32];

        #ifdef ESP_PLATFORM
        // ESP32: Use hardware RNG
        esp_fill_random(nonce, sizeof(nonce));
        #else
        // Linux: Use std::random_device
        std::random_device rd;
        for (size_t i = 0; i < sizeof(nonce); i++)
            nonce[i] = static_cast<unsigned char>(rd() & 0xFF);
        #endif

        return bytesToHex(nonce, sizeof(nonce));
    }

    String bytesToHex(const unsigned char* data, size_t len) {
        String hex;
        hex.reserve(len * 2);
        for (size_t i = 0; i < len; i++) {
            char buf[3];
            sprintf(buf, "%02x", data[i]);
            hex += buf;
        }
        return hex;
    }

    String calculateHMAC(const String& message) {
        // HMAC-SHA256 implementation using mbedtls
        unsigned char hmac[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
        mbedtls_md_hmac_starts(&ctx, (const unsigned char*)device_secret.c_str(), device_secret.length());
        mbedtls_md_hmac_update(&ctx, (const unsigned char*)message.c_str(), message.length());
        mbedtls_md_hmac_finish(&ctx, hmac);
        mbedtls_md_free(&ctx);

        return bytesToHex(hmac, sizeof(hmac));
    }
};
```
