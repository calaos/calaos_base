# Configuration options

This page is generated from the option registry of `src/lib/ConfigOptions.cpp`, do not edit it by hand.

`local_config.xml` is a shared file: calaos_server is not its only reader. Calaos Home, the touch screen interface, reads and writes the same file, and the MCP sidecar reads a few keys of its own. The "Used by" column says who actually consumes each key.

Annotations: **⟳** restart required, **🔒** secret, **⚙** generated automatically, **⚠** deprecated, **◦** advanced.

## Network / API

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `port_api` ⟳ | TCP port | `5454` | calaos_server | API port — TCP port the JSON HTTP/WebSocket API listens on. Changing it requires reconfiguring every client: Calaos Home, Calaos Installer, the mobile application and any reverse proxy in front of the server. The MCP sidecar reads the same key to know where to reach the API. Range: 1…65535. |
| `listen_address` ⟳ | host name or IP address | `0.0.0.0` | calaos_server | Listen address — Local address the HTTP/WebSocket API and the UDP discovery server bind to. 0.0.0.0 accepts connections on every interface; set a single address to confine the server to one network. An address that does not exist on the machine prevents the server from starting. |
| `max_http_body_size` ⟳ ◦ | whole number | `4194304` | calaos_server | Maximum HTTP body size — Biggest HTTP request body accepted on the API port, in bytes. A request announcing or sending more is refused with a 413 answer. The biggest legitimate payload a Calaos client sends is around 215 KiB (calaos_installer pushing io.xml and rules.xml), the default keeps a large margin above it. A value that is not a plain number between 4096 and 1073741824 falls back to the default. Range: 4096…1073741824. See also: `max_websocket_message_size`. |
| `max_websocket_message_size` ⟳ ◦ | whole number | `4194304` | calaos_server | Maximum websocket message size — Biggest websocket message accepted on the API port, fragments included, in bytes. A bigger message is refused with a 1009 close frame. A single websocket frame stays capped at 4 MiB whatever this value, so raising it above that only takes effect on fragmented messages. A value that is not a plain number between 4096 and 1073741824 falls back to the default. Range: 4096…1073741824. See also: `max_http_body_size`. |
| `max_connections` ⟳ ◦ | whole number | `100` | calaos_server | Maximum connections — Simultaneous connections accepted on the API port, all clients together. Above it a new connection is answered 503 and closed right away, nothing already opened is evicted. A value that is not a plain number between 1 and 10000 falls back to the default. Range: 1…10000. See also: `max_connections_per_ip`. |
| `max_connections_per_ip` ⟳ ◦ | whole number | `20` | calaos_server | Maximum connections per client — Simultaneous connections accepted from one client address, so that a single client cannot occupy every max_connections slot and evict everybody else. Above it a request is answered 429 and its connection closed. The client address is the last entry of the last X-Forwarded-For header line (the address the haproxy in front of calaos_server saw), or the TCP peer address on a direct connection. A value that is not a plain number between 1 and 10000 falls back to the default. Range: 1…10000. See also: `max_connections`. |
| `request_read_timeout` ⟳ ◦ | whole number | `30` | calaos_server | Request read timeout — Delay, in seconds, a new connection is given to send one complete HTTP request before being closed. It only covers the time before the first request is parsed, so it never applies to an opened websocket, a long poll or a camera stream, only to a client that connects and then sends nothing or dribbles its headers. A value that is not a plain number between 1 and 600 falls back to the default. Range: 1…600. |
| `wwwroot` | filesystem path | `<data dir>/app` | calaos_server | Web interface directory — Directory served under /app/ for the main web interface. Leave it empty to serve the files installed with the server; a path that is not a directory is ignored and the installed one is used instead. |
| `debug_enabled` | boolean | `false` | calaos_server | Debug web interface — Serves the debug web interface under /debug/. It exposes the internal state of the server and is meant for development, so leave it off on a box in production. When off, /debug/ answers as if it did not exist. See also: `debug_wwwroot`. |
| `debug_wwwroot` ◦ | filesystem path | `<data dir>/debug` | calaos_server | Debug interface directory — Directory served under /debug/ when the debug interface is enabled. Leave it empty to serve the files installed with the server. See also: `debug_enabled`. |

## Authentication

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `cn_user` | text | `user` | calaos_server, Calaos Home | API user name — User name every client must present to the JSON API: Calaos Home, Calaos Installer, the mobile application and the MCP sidecar. It is read on every request, so a change takes effect at once on the server, but every client has to be updated too. A freshly created local_config.xml is seeded with "user". See also: `calaos_user`. |
| `cn_pass` 🔒 | password | `pass` | calaos_server, Calaos Home | API password — Password every API client must present. Changing it locks out Calaos Home, Calaos Installer and the mobile application until they are updated too. It is stored in clear text, which is why local_config.xml is kept readable by its owner only. A freshly created file is seeded with "pass" — change it. See also: `calaos_password`. |
| `calaos_user` ⚠ ◦ | text | empty | calaos_server | API user name (legacy) — Older name of the API user, still honoured when cn_user is empty. The server deletes it as soon as the credentials are changed through the API. Keep it only while an old client is still around. ⚠ Deprecated. Use `cn_user` instead. See also: `cn_user`. |
| `calaos_password` 🔒 ⚠ ◦ | password | empty | calaos_server | API password (legacy) — Older name of the API password, still honoured when cn_pass is empty. The server deletes it as soon as the credentials are changed through the API. ⚠ Deprecated. Use `cn_pass` instead. See also: `cn_pass`. |

## Logging

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `debug_level` | whole number | `4` | calaos_server | Log level — Default verbosity of every log domain: 0 unknown, 1 critical, 2 error, 3 warning, 4 info, 5 debug. A value outside that range falls back to 4. The level is also handed to the external drivers and to the MCP sidecar through the CALAOS_LOG_LEVEL environment variable. Range: 0…5. Accepted values: `0`, `1`, `2`, `3`, `4`, `5`. See also: `debug_domains`. |
| `debug_domains` ◦ | comma separated list | empty | calaos_server | Per-domain log levels — Verbosity of individual log domains, overriding the global level, as a comma separated list of domain:level pairs. Domains that are not listed keep the global level. The list is also handed to the external drivers and to the MCP sidecar through the CALAOS_LOG_DOMAINS environment variable. Example: `hifirose:5,network:0`. See also: `debug_level`. |

## E-mail (SMTP)

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `smtp_server` | host name or IP address | empty | calaos_server | SMTP server — Host name or IP address of the SMTP server used to send the notification e-mails. An old style smtp:// or smtps:// URI is still accepted and reduced to its host part. While it is empty every mail fails. Example: `smtp.example.org`. |
| `smtp_port` | TCP port | empty | calaos_server | SMTP port — TCP port of the SMTP server: usually 25 for plain SMTP, 587 for submission with STARTTLS and 465 for implicit TLS. There is no fallback, mail sending needs this key set. Range: 1…65535. Example: `587`. |
| `smtp_auth` | boolean | `false` | calaos_server | SMTP authentication — Authenticates on the SMTP server with smtp_username and smtp_password. When off the mail is sent anonymously and both credentials are ignored, which only works with a relay that trusts the local network. See also: `smtp_username`, `smtp_password`. |
| `smtp_tls` | boolean | `false` | calaos_server | SMTP over TLS — Opens the connection to the SMTP server over TLS instead of plain text. Required by nearly every provider, and mandatory whenever credentials travel outside the local network. |
| `smtp_username` | text | empty | calaos_server | SMTP user name — User name presented to the SMTP server. Only used when smtp_auth is on. Many providers expect the full e-mail address here. See also: `smtp_auth`. |
| `smtp_password` 🔒 | password | empty | calaos_server | SMTP password — Password presented to the SMTP server. Only used when smtp_auth is on. It is stored in clear text in local_config.xml; prefer a dedicated application password when the provider offers one. See also: `smtp_auth`. |
| `smtp_debug` ◦ | boolean | `false` | calaos_server | SMTP debug log — Runs the mail helper in verbose mode so the whole SMTP dialogue is written to the log. Useful to diagnose a delivery failure, but it also prints the authentication exchange, so turn it back off afterwards. |

## Notifications

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `notif/mail_sender` | e-mail address | `calaos@localhost` | calaos_server | Notification sender — Address used as the sender of the notification e-mails when a rule does not give one. Most providers reject a sender that is not one of their own mailboxes, so the built-in fallback calaos@localhost rarely gets delivered. |
| `notif/mail_recipients` | list of e-mail addresses | empty | calaos_server | Notification recipients — Destination of the notification e-mails when a rule does not give one. Mail sending is aborted when this is empty. Beware that the mail helper currently hands the whole value to the server as a single recipient, so only one address really gets delivered. See also: `user_emails`. |
| `notif/battery_mail_enabled` | boolean | `true` | calaos_server | Low battery e-mail — Sends an e-mail when a battery powered device reports a low battery. Applies to every IO exposing a battery level. Enabled unless it is explicitly set to false. See also: `notif/battery_push_enabled`. |
| `notif/battery_push_enabled` | boolean | `true` | calaos_server | Low battery push — Sends a push notification to the registered mobile applications when a battery powered device reports a low battery. See also: `notif/battery_mail_enabled`. |
| `notif/io_connected_mail_enabled` | boolean | `true` | calaos_server | IO connection e-mail — Sends an e-mail when an IO goes offline or comes back online. This can be very noisy on an unstable radio network, where a device may flap several times an hour. See also: `notif/io_connected_push_enabled`. |
| `notif/io_connected_push_enabled` | boolean | `true` | calaos_server | IO connection push — Sends a push notification to the registered mobile applications when an IO goes offline or comes back online. See also: `notif/io_connected_mail_enabled`. |
| `notif_development` ◦ | boolean | `false` | calaos_server | Apple development push — Marks the Apple push notifications as targeting the APNs development gateway instead of the production one. Only useful with a mobile application built and signed for development; a production application stops receiving anything. Note that this key has no notif/ prefix, unlike the other ones here. |

## InfluxDB

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `influxdb_enabled` ⟳ | boolean | `false` | calaos_server | Enable InfluxDB logging — Logs the state of every IO to an InfluxDB time series database, for graphing and long term analysis. Everything else in this section is ignored while it is off. |
| `influxdb_version` ⟳ | choice | `1` | calaos_server | InfluxDB API version — InfluxDB 1.x is addressed with a database name; 2.x uses an organisation, a bucket and an API token. Choosing the wrong one makes every write fail with an authentication or a not-found error. Accepted values: `1`, `2`. See also: `influxdb_database`, `influxdb_org`, `influxdb_bucket`, `influxdb_token`. |
| `influxdb_host` ⟳ | host name or IP address | `127.0.0.1` | calaos_server | InfluxDB host — Host name or IP address of the InfluxDB server. The default points at an instance running on the box itself. |
| `influxdb_port` ⟳ | TCP port | `8086` | calaos_server | InfluxDB port — TCP port of the InfluxDB HTTP API. 8086 is the default of both 1.x and 2.x. Range: 1…65535. |
| `influxdb_log_timeout` ⟳ ◦ | whole number | `300` | calaos_server | InfluxDB snapshot interval — Delay in seconds between two full dumps of every IO state to the database. States are also written as they change; this periodic snapshot is what keeps a flat curve alive between two changes. A value of 0 is treated as 300. Range: 0…86400. |
| `influxdb_database` ⟳ | text | `calaos` | calaos_server | InfluxDB database (1.x) — Name of the InfluxDB 1.x database the server writes to. The server creates it at start-up if it does not exist yet. Ignored when the API version is 2. See also: `influxdb_version`. |
| `influxdb_org` ⟳ | text | `calaos` | calaos_server | InfluxDB organisation (2.x) — InfluxDB 2.x organisation owning the bucket. Ignored when the API version is 1. See also: `influxdb_version`. |
| `influxdb_bucket` ⟳ | text | `calaos-data` | calaos_server | InfluxDB bucket (2.x) — InfluxDB 2.x bucket the measurements are written to. It has to exist already, the server does not create it. Ignored when the API version is 1. See also: `influxdb_version`. |
| `influxdb_token` ⟳ 🔒 | token | empty | calaos_server | InfluxDB token (2.x) — InfluxDB 2.x API token with write access to the bucket. Data logging stays disabled while it is empty, and the server reports an unauthorized error when it is wrong. Ignored when the API version is 1. See also: `influxdb_version`. |

## History

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `history_keep_days` | whole number | `30` | calaos_server | History retention — Number of days of event history kept in the local database. Older events are deleted every time a new one is recorded, together with the pictures the push notifications attached to them — lowering this value destroys data at the next event. Range: 1…3650. |

## Location

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `latitude` | decimal number | `46.422713` | calaos_server, Calaos Home | Latitude — Latitude in decimal degrees, used to compute sunrise and sunset for the time ranges of the rules, and by Calaos Home for the weather. The default shown here is the one the sunrise computation really falls back to when the key is missing (centre of France); a freshly created local_config.xml is instead seeded with 48.864715, Paris. Range: -90…90. See also: `longitude`. |
| `longitude` | decimal number | `2.548828` | calaos_server, Calaos Home | Longitude — Longitude in decimal degrees, used to compute sunrise and sunset for the time ranges of the rules, and by Calaos Home for the weather. The default shown here is the one the sunrise computation really falls back to when the key is missing (centre of France); a freshly created local_config.xml is instead seeded with 2.322235, Paris. Range: -180…180. See also: `latitude`. |

## MCP

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `mcp_token` ⟳ 🔒 ⚙ | token | generated | calaos_server, MCP sidecar | MCP client token — Token an MCP client has to present to reach the Calaos MCP server. 64 hexadecimal characters generated at the first start and written back to local_config.xml. Emptying it makes the server generate a new one at the next start, which invalidates every client already configured. See also: `mcp_service_token`. |
| `mcp_service_token` ⟳ 🔒 ⚙ | token | generated | calaos_server, MCP sidecar | MCP service token — Token the MCP sidecar uses to authenticate itself against the JSON API of the server. Generated the same way as mcp_token at the first start. It is purely internal, no external client ever needs it. See also: `mcp_token`. |
| `mcp_rate_limit` ⟳ ◦ | whole number | `300` | MCP sidecar | MCP rate limit — Maximum number of authentication attempts the MCP sidecar accepts from one IP address before it starts refusing them. 0 disables the limit. Read by the Python sidecar only, never by calaos_server. Range: 0…100000. |
| `mcp_ban_failures` ⟳ ◦ | whole number | `20` | MCP sidecar | MCP ban threshold — Number of failed authentications from one IP address before the MCP sidecar bans it for mcp_ban_seconds. 0 disables banning entirely. Read by the Python sidecar only, never by calaos_server. Range: 0…10000. See also: `mcp_ban_seconds`. |
| `mcp_ban_seconds` ⟳ ◦ | whole number | `120` | MCP sidecar | MCP ban duration — How long, in seconds, an IP address stays banned once it reached mcp_ban_failures failed authentications. Read by the Python sidecar only, never by calaos_server. Range: 0…86400. See also: `mcp_ban_failures`. |

## RemoteUI / OTA

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `ota_enabled` ⟳ | boolean | `true` | calaos_server | Firmware updates over the air — Offers firmware updates to the RemoteUI touch screens: the server scans the firmware directory and proposes the newest build to every screen that connects. For historical reasons the value 1 is accepted as well as true. Accepted values: `true`, `1`, `false`, `0`. |
| `ota_firmware_path` ⟳ | filesystem path | `<data dir>/firmwares` | calaos_server | Firmware directory — Directory scanned for the RemoteUI firmware images offered over the air. Leave it empty to use the directory created by the installation. |
| `ota_rescan_interval` ⟳ | whole number | `60` | calaos_server | Firmware rescan interval — Delay in minutes between two scans of the firmware directory. Anything below 1 is treated as 60. Lower it while publishing new firmwares, raise it to spare a slow storage. Range: 1…10080. |

## Calaos Home

| Key | Type | Default | Used by | Description |
|---|---|---|---|---|
| `show_cursor` | boolean | `true` | Calaos Home | Show the mouse pointer — Shows the real X11 pointer in Calaos Home. When off it is replaced by a 1x1 transparent pixmap, which is what a touch screen wants. calaos_server only seeds this key, Calaos Home is the one reading it. |
| `dpms_enable` | boolean | `false` | Calaos Home | Automatic screen blanking — Lets Calaos Home blank the touch screen after a period without any touch. calaos_server only seeds this key, Calaos Home is the one reading it. See also: `dpms_standby`. |
| `dpms_standby` | whole number | empty | Calaos Home | Blanking delay — Idle delay in minutes before Calaos Home blanks the screen, when automatic blanking is on. Any value below 1 is treated as 1 minute. Read by Calaos Home only. Range: 0…1440. See also: `dpms_enable`. |
| `calaos_server_host` | host name or IP address | empty | Calaos Home | Server address — Forces the address Calaos Home connects to. Setting it disables the UDP auto-discovery on port 4545, which is what you want when the screen and the server sit on different subnets or when several servers answer. Leave it empty to keep auto-discovery. Read by Calaos Home only. See also: `calaos/host`. |
| `lang` | choice | empty | Calaos Home | Interface language — Language of the Calaos Home interface. Leave it empty to follow the system locale. Read by Calaos Home only, it has no effect on the language of the server logs. Accepted values: `de`, `en`, `es`, `fr`, `hi`, `nb`, `pl`, `ru`. |
| `user_emails` | list of e-mail addresses | empty | Calaos Home | User e-mail addresses — Addresses managed from the "user info" page of Calaos Home. It overlaps notif/mail_recipients without being the same key: the server never reads this one and Calaos Home never reads the other, so both have to be kept in step by hand. See also: `notif/mail_recipients`. |
| `calaos/host` ⚠ ◦ | host name or IP address | empty | Calaos Home | Server address (stray key) — Known pollution rather than a real option. Calaos Home saves its settings through QSettings on every platform and, on the desktop build, the group path of the setting leaks verbatim into local_config.xml. Nothing ever reads it back; the address really used is calaos_server_host. It is deliberately left alone by the automatic purge, so deleting it is safe but manual. ⚠ Deprecated. Use `calaos_server_host` instead. See also: `calaos_server_host`. |

## Obsolete keys

These keys are no longer read by anything. `calaos_config purge` removes them, and calaos_server removes them at start-up when the configuration is writable.

- `hwid`
- `use_ntp`
- `fw_target`
- `fw_version`
- `device_type`
