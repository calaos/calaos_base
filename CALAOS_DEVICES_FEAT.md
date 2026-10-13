# Calaos — Devices (climate first)

Status: design validated on 2026-10-12, not yet implemented. Decisions and their rationale are
recorded in `docs/refactoring/DECISIONS.md` (section "2026-10-12 — devices et climate").
Read `AGENTS.md` first for the codebase layout.

---

## 1. Goals and constraints

1. **Structure.** Today every IO is a flat entry in its room. With dozens of MQTT IOs per
   appliance, `calaos_installer` becomes unmanageable. A **device** groups the IOs of one physical
   or logical appliance, and that hierarchy is visible in `io.xml` and in the installer.
2. **Automation of bookkeeping.** Creating a device creates its IOs; deleting it deletes them.
   Nothing is left behind for the user to forget.
3. **Climate first.** Heating, AC and heat pumps are the first device family. The mechanism is
   generic so other families (multisensor, RGBW light, cover) can follow without redesign.
4. **No discovery.** Calaos has no ESPHome auto-discovery; an ESPHome climate today means creating
   N MQTT IOs by hand. Device templates in the installer replace that repetition.

Hard constraints:

- **The rules engine is unchanged.** It only ever sees scalar IOs. A device *is* an IO with a
  scalar value (§5), and its sub-IOs remain ordinary IOs.
- **Existing IOs, drivers and APIs are unchanged.** Everything here is additive.
- **No silent breakage.** A tool that does not understand devices must fail loudly, never drop
  them (§3.3).

---

## 2. Concepts

| Term | Meaning |
|---|---|
| **Device** | An `IOBase` subclass declared by a `<calaos:device>` element in a room. Has a `type` (family, e.g. `climate`) and an `impl` (behaviour, e.g. `proxy`, `thermostat`). |
| **Owned IO** | An ordinary IO declared *inside* the `<calaos:device>` element. Created and deleted with the device. |
| **Link** | `<calaos:link name="…" io="…" [command_io="…"]/>`: gives a role (`mode`, `current_temp`, …) to an IO. The target is either an owned IO or an **external** IO declared elsewhere (e.g. the room's temperature sensor). The device never creates or deletes an external IO. |
| **Param** | `<calaos:param name="…" value="…"/>`: a device setting (hysteresis, keep-alive, …). |
| **Capabilities** | What a device instance can do (modes, setpoint bounds, fan…). Derived from which links are present plus params (§6.3). |
| **Template** | Installer-side description of how to generate a device, its owned IOs and links for a known integration (§10). |

The family selects the client widget; the `impl` selects the server class. Two climate impls:

| | Heat only | Heat + cool | Full HVAC |
|---|---|---|---|
| `proxy` (the appliance regulates) | rare | ESPHome `heat_cool` | ESPHome split AC |
| `thermostat` (Calaos regulates) | heating relay | reversible floor heat pump | — |

---

## 3. `io.xml` format

### 3.1 Example

```xml
<calaos:home format="2">
<calaos:room name="Salon" type="livingroom">

  <calaos:input id="salon_temp" type="MqttInputTemp" name="Température salon" .../>

  <!-- proxy: Mitsubishi CN105 via ESPHome, regulating against the room sensor -->
  <calaos:device id="salon_ac" type="climate" impl="proxy" name="Clim salon">
    <calaos:input  id="salon_ac_mode"       type="MqttInputString"  visible="false" .../>
    <calaos:output id="salon_ac_mode_cmd"   type="MqttOutputString" visible="false" .../>
    <calaos:input  id="salon_ac_current"    type="MqttInputTemp"    visible="false" .../>
    <calaos:output id="salon_ac_target"     type="MqttOutputAnalog" visible="false" value_min="16" value_max="30" step="0.5" .../>
    <calaos:input  id="salon_ac_action"     type="MqttInputString"  visible="false" .../>
    <calaos:output id="salon_ac_remotetemp" type="MqttOutputAnalog" visible="false" .../>

    <calaos:link name="mode"         io="salon_ac_mode" command_io="salon_ac_mode_cmd"/>
    <calaos:link name="current_temp" io="salon_ac_current"/>
    <calaos:link name="target_temp"  io="salon_ac_target"/>
    <calaos:link name="action"       io="salon_ac_action"/>
    <calaos:link name="remote_temp"  io="salon_ac_remotetemp"/>
    <calaos:link name="remote_temp_source" io="salon_temp"/>   <!-- external -->
    <calaos:param name="remote_temp_keepalive" value="120"/>
  </calaos:device>

  <!-- thermostat: reversible floor heat pump on two relays -->
  <calaos:device id="salon_floor" type="climate" impl="thermostat" name="Plancher salon">
    <calaos:internal id="salon_floor_target"      type="InternalInt"    save="true" visible="false"/>
    <calaos:internal id="salon_floor_target_low"  type="InternalInt"    save="true" visible="false"/>
    <calaos:internal id="salon_floor_target_high" type="InternalInt"    save="true" visible="false"/>
    <calaos:internal id="salon_floor_action"      type="InternalString" visible="false"/>
    <calaos:output   id="salon_floor_heat" type="WODigital" visible="false" .../>
    <calaos:output   id="salon_floor_cool" type="WODigital" visible="false" .../>

    <calaos:link name="current_temp"     io="salon_temp"/>      <!-- external -->
    <calaos:link name="humidity"         io="salon_humidity"/>  <!-- external -->
    <calaos:link name="target_temp"      io="salon_floor_target"/>
    <calaos:link name="target_temp_low"  io="salon_floor_target_low"/>
    <calaos:link name="target_temp_high" io="salon_floor_target_high"/>
    <calaos:link name="action"           io="salon_floor_action"/>
    <calaos:link name="output_heat"      io="salon_floor_heat"/>
    <calaos:link name="output_cool"      io="salon_floor_cool"/>
    <calaos:param name="emitter"        value="floor"/>
    <calaos:param name="hysteresis"     value="0.5"/>
    <calaos:param name="min_on_time"    value="900"/>
    <calaos:param name="min_off_time"   value="900"/>
    <calaos:param name="min_range"      value="2"/>
    <calaos:param name="sensor_timeout" value="1800"/>
    <calaos:param name="humidity_max"   value="65"/>
  </calaos:device>

</calaos:room>
</calaos:home>
```

### 3.2 Rules

- `<calaos:device>` is a room child, like `<calaos:remote_ui>`. Devices do not nest.
- Owned IOs use the existing `input`/`output`/`internal` elements with their usual attributes.
- IO ids stay globally unique. Templates name owned IOs `<device_id>_<role>`; nothing parses that
  convention, it is only for readability.
- An external IO may be linked by several devices. An owned IO belongs to exactly one device.
- Link targets are resolved after **all** rooms are loaded (an external IO may live in another
  room). An unresolved link is logged with `cErrorDom("device")`, and the capability it would
  provide is absent; the device itself still loads.

### 3.3 Versioning and loud failure

`<calaos:home format="2">` marks a file that may contain devices. A missing attribute means
format 1. This protects against tools that would otherwise drop devices silently, since both the
server and the installer rebuild `io.xml` from their model on save:

- **Server, `config put`:** the request declares the client's support with `"io_format": 2` next to
  `config_files`. If the current `io.xml` contains at least one device (opaque ones included) and
  the request does not declare `io_format` ≥ 2, the upload is refused before any backup, write or
  restart, with an explicit `error` ("update calaos_installer: this io.xml would drop N
  device(s)"). A client that declares format 2 is trusted to delete devices, the last one included
  (a file without devices is written as format 1, so the file alone cannot carry that intent). An
  unparsable `io.xml` is always refused.
- **Installer:** reads `io_format` in the `config get` response (absent = 1), refuses to save
  devices to a server that does not announce format 2, and declares `"io_format": 2` in every
  `config put`. When loading a file whose format is newer than it knows,
  it opens read-only.

---

## 4. Server architecture

```
IOBase
 └─ Device                  generic: links, params, owned IOs, XML children, command parsing
     └─ ClimateBase         climate model: modes, setpoints, capabilities, state object
         ├─ ProxyClimate        the appliance regulates; Calaos relays
         └─ InternalThermostat  Calaos regulates (a "virtual driver", §8)
```

### 4.1 Loading and saving

- `Room::LoadFromXml` adds `calaos:device` to its whitelist and builds it through `IOFactory`,
  keyed by `impl` (`REGISTER_IO` as for any IO), exactly like `remote_ui`.
- `Device::LoadFromXml` creates its owned IOs through `IOFactory` and registers them in the
  **room's flat list**. Rules, the state cache, `get_home`, calaos_mobile and MCP therefore see
  them as they see any IO today.
- `Device::SaveToXml` writes the `<calaos:device>` element, then its owned IOs, links and params as
  children. `Room::SaveToXml` skips owned IOs at room level, since their device writes them.
- `Config::SaveConfigIO` writes `format="2"` as soon as one device exists.

### 4.2 Ownership and lifetime

- The room keeps owning every IO, owned ones included. A device stores **ids**, never raw
  pointers, and resolves them through `ListeRoom::get_io()` when it needs them.
- Deleting a device (API or installer) deletes its owned IOs in the same operation. External IOs
  are untouched.
- Deleting an external IO that a link still targets is allowed but logged at load. The installer
  warns before doing it.
- A thermostat whose mandatory owned IOs are missing (hand-edited file) recreates them at load with
  default values. The next save persists them.

### 4.3 Change notification hook

There is no per-IO subscription today: `EventManager::newEvent` is global, deferred by one loop
turn and carries the state as a string. A device needs to react to its linked IOs.

`IOChangeHub` (a dedicated singleton) maps an IO id to callbacks. They are dispatched from
`ListeRule::executeTrigger()`, right after the rules of the same trigger, so they see what those rules
wrote and their own writes are queued like rule actions. A subscription is an RAII handle, keyed by
id: it may precede the IO and survives its deletion. A callback reads the IO's current state, and
deleting an IO while a cascade is running is refused (`ListeRoom::deleteIO()` returns false).

Re-entrancy: a callback that writes another IO goes through the same `ListeRule` serialization
(`execInProgress` / `pendingTriggers`) as a rule action, so a setpoint → relay → rule chain is
serialized, never recursive.

### 4.4 Timers

Follow the existing patterns (`src/lib/Timer.h`): a member `std::unique_ptr<Timer>` for
anything cancellable or re-armable (lock-outs, keep-alive, debounce), and `LifetimeTag::singleShot`
for one-shots. A raw `Timer::singleShot` fails the `IoLifetimeTimer` census test.

---

## 5. The device as an IO: value and commands

### 5.1 Scalar value (what rules see)

`get_value_string()` of a climate device is its **mode** (`off`, `heat`, `cool`, `auto`, `dry`,
`fan_only`). A rule condition such as "`salon_ac` == `off`" works for any climate device whatever
its impl. Setpoints, current temperature and action are read by rules from the linked IOs, which are
ordinary IOs.

For a proxy, the mode mirrors the `mode` link. A thermostat stores its mode itself and persists it
through the state cache (`iostates.cache`).

### 5.2 Commands

There is still **one command path**: `set_value(string)`, reached by rule actions, Lua
`calaos:set_io`, the WebSocket/HTTP `set_state` and MCP. For a device, the string is either:

- **a plain mode**: `off` is equivalent to `{"mode":"off"}`;
- **a JSON object** (starts with `{`): a partial update, e.g.
  `{"mode":"heat","target_temp":21.5}`.

Semantics:

- Only the fields present are applied.
- Each field is validated against the instance's capabilities (§6.3): the field must exist,
  enums must contain the value, and numbers are clamped to min/max and rounded to the step.
- **Best effort**: an invalid field is rejected and logged with the device id and the reason, and
  the valid fields are still applied.
- `val_var` (taking the value from another IO) is not supported inside the JSON. To copy another
  IO's value into a setpoint, the rule writes the setpoint IO directly.

Existing IOs are unaffected: their command strings do not change.

### 5.3 State publication

- `get_home`: a device item carries the usual fields (`state` = mode, `gui_type` = the family,
  e.g. `climate`) plus a `device` object:

  ```json
  "device": {
    "family": "climate", "impl": "proxy",
    "capabilities": {
      "modes": ["off","heat","cool","auto"],
      "target_temp": {"min": 16, "max": 30, "step": 0.5, "unit": "°C"},
      "target_temp_range": true,
      "fan_modes": ["auto","low","high"],
      "reports_action": true,
      "remote_temp": true
    },
    "state": {"mode": "cool", "current_temp": 23.4, "target_temp": 21.0, "action": "cooling",
              "fan_mode": "auto"},
    "ios": {"mode": "salon_ac_mode", "target_temp": "salon_ac_target", "...": "..."}
  }
  ```

- `io_changed`: when any linked IO changes, the device emits one `io_changed` for its own id,
  debounced (100 ms, member timer), carrying `state` and the same `device` object. Clients that do
  not know devices keep reading `state`.
- The `device` object is always sent **whole** (in `get_home` and in every `io_changed`): clients
  **replace** their copy, never merge it, so a capability or `ios` entry the server dropped cannot
  linger. Numbers and booleans are real JSON numbers and booleans, not strings (unlike the legacy
  `state` string).
- Owned IOs are still listed in the room's `items` with `visible="false"`, as today.

---

## 6. Climate model

### 6.1 Modes and action

```cpp
enum class ClimateMode   { Off, Heat, Cool, Auto, Dry, FanOnly };
enum class ClimateAction { Off, Idle, Heating, Cooling, Drying, Fan };
```

Serialized as lower-case strings (`fan_only`), which are the ESPHome strings: no value mapping is
needed for ESPHome. A value map for other integrations (Zigbee2MQTT) is deferred.

### 6.2 Setpoints

- `target_temp`: the single setpoint, used in `heat` and `cool`.
- `target_temp_low` / `target_temp_high`: the range, used in `auto`. Present when the device
  supports a range (ESPHome `heat_cool`, thermostat with `output_cool`).
- `min_range`: the minimum gap enforced between low and high. It replaces the earlier "dead zone"
  and protects a heat pump against back-to-back heat/cool changeovers.

### 6.3 Link roles and capability derivation

A capability exists **if and only if** its link exists. The server and the installer share this
rule: neither has to duplicate the other's logic.

| Link role | Proxy | Thermostat | Capability / field |
|---|---|---|---|
| `mode` (+ `command_io`) | required | — (the device stores it) | `modes` |
| `current_temp` | optional | **required** (regulation input) | `current_temp` |
| `target_temp` | optional | **required**, owned | `target_temp` (bounds from the IO's `value_min`/`value_max`/`step`) |
| `target_temp_low` / `_high` | optional | owned, when `output_cool` exists | `target_temp_range` |
| `action` | optional | owned, written by the thermostat | `reports_action` |
| `fan_mode` / `swing_mode` (+ `command_io`) | optional | — | `fan_modes` / `swing_modes` (list from a param) |
| `remote_temp` + `remote_temp_source` | optional | — | `remote_temp` |
| `output_heat` | — | **required** | enables `heat` |
| `output_cool` | — | optional | enables `cool`, `auto` |
| `humidity` | — | required to allow `cool` when `emitter=floor` | dew-point cut |

Thermostat modes: `{off, heat}` without `output_cool`, `{off, heat, cool, auto}` with it.

---

## 7. ProxyClimate

- Reads come from the `io` of each link, and writes go to its `command_io`, or to the `io` itself
  when it is an output that is both readable and writable (`MqttOutputAnalog`).
- No regulation in Calaos.
- **Remote temperature** (2-way serial units such as Mitsubishi CN105): the source sensor's value
  is relayed to `remote_temp` on every change **and** every `remote_temp_keepalive` seconds. The
  unit reverts to its internal sensor if updates stop, so the keep-alive is mandatory. This keeps
  the unit's own inverter modulation while fixing its sensor input.
- IR-only units (`climate_ir`) cannot take a remote temperature. To regulate them on a real sensor,
  use ESPHome's `generic_thermostat` on the device, or a Calaos thermostat that gates the unit's
  mode (later).

---

## 8. InternalThermostat

A thermostat is a **virtual driver**, not an automation. Like an MQTT IO parsing its messages, it
carries its own C++ behaviour; the "logic lives in user rules" invariant applies to policy, not to
drivers.

- **Regulation** (in the class, fixed): `evaluate()`, triggered by the change hook (§4.3) on
  `current_temp`, the setpoints, the mode and `humidity`, plus the timers.
- **Policy** (user rules, unchanged engine): night setback, away mode, window open. Rules write the
  setpoint IOs or the device mode. No scheduler inside the thermostat.

### 8.1 `evaluate()`

```
off  : both outputs OFF
heat : cool OFF; heat ON below target − hysteresis, OFF above target
cool : heat OFF; cool ON above target + hysteresis, OFF below target
auto : heat as in "heat" against target_low; cool as in "cool" against target_high
```

Constraints applied on top:

- `min_on_time` / `min_off_time` per output: a switch inside a lock-out is deferred with a
  re-armable timer, then `evaluate()` runs again.
- In `auto`, never energise both outputs. A changeover additionally waits for `min_off_time` of
  the other output.
- `action` is written after each evaluation (`off`, `idle`, `heating`, `cooling`).

### 8.2 Safety

- **Startup:** no evaluation, outputs OFF, until `current_temp` has delivered a value after boot.
  Sensors such as MQTT or Wago have not read anything when the device is built.
- **Stale sensor:** if `current_temp` has not changed for `sensor_timeout` seconds, outputs OFF
  and an error is logged. Regulation resumes on the next value.
- **Dew point (floor cooling):** with `emitter=floor`, `cool` requires a `humidity` link, and
  `output_cool` is cut while humidity ≥ `humidity_max`. Without a humidity link, `cool` and
  `auto` are refused at load (logged) and absent from the capabilities.
- **Thermal inertia:** for floors, templates default to a wide hysteresis and long min on/off
  times. PID is out of scope.

### 8.3 Protected outputs

`output_heat` and `output_cool` are driven by the thermostat; a second writer causes a conflict.

- MCP hides them from `list_ios` and refuses to write them.
- A write from a rule, Lua or `set_state` is **logged as a warning**, never blocked: the rules
  engine must never break.

---

## 9. Clients

### 9.1 JSON API and calaos_mobile

There are no new endpoints. `set_state` with the JSON command (§5.2), plus the `device` object in
`get_home` and `io_changed` (§5.3). calaos_mobile ignores unknown keys and hides `visible=false`
IOs. The tolerance of its event decoder to the extra `device` key must be checked during
implementation.

### 9.2 MCP

A device is an IO: `set_io_state` with the JSON command works once the tool description documents
the format (from IODoc, §11). The protected outputs are hidden (§8.3).

### 9.3 RemoteUI

- Widget: `<calaos:widget type="Climate" io_id="salon_ac" .../>` (widget types are PascalCase in
  the firmware), referencing the device through the existing `io_id`. Registered with
  `registerWidgetAnySize` so it adapts to every cell size.
- The panel reads the `device` object from `remote_ui_io_states` and `io_changed`, and commands
  through `set_state` with the JSON command. No new message.
- `RemoteUI::extractReferencedIOs()` expands a device to its owned and linked IOs.
- One capability-driven widget for the whole climate family:
  - mode buttons from `modes`;
  - setpoint dial bounded by min/max/step, two handles when `target_temp_range` is present and the
    mode is `auto`;
  - optional fan/swing selectors and an action badge;
  - mode-dependent gating (`off` greys the setpoint, `fan_only` hides it);
  - a responsive layout so a heat-only thermostat does not look empty in a large cell.
- Restricting a panel to the IOs it references (`hasReferencedIO()` is never called today) is a
  pre-existing security gap, tracked separately.

---

## 10. calaos_installer

- **Tree view:** room → device → owned IOs. Free-standing IOs are shown as today.
- **Device wizard** from templates: generates the `<calaos:device>`, its owned IOs, links and
  default params in one step. Initial templates:
  - `esphome_climate` (with an optional `remote_temp` feed);
  - `esphome_climate_ir`;
  - `thermostat_heat`;
  - `thermostat_heat_cool`;
  - `thermostat_floor_heat_cool` (dew-point fields required).
- **Cascade delete** of a device and its owned IOs. A warning before deleting an external IO that a
  link still targets.
- **Format gate** (§3.3).
- **Rule action editor:** when IODoc declares a JSON command format for the IO's type, show a form
  generated from it instead of the free text field. Only the fields whose link exists on this
  instance are offered (§6.3). The stored action value is the JSON string.
- **Templates are generated by calaos_base**, like `io_doc.json`, imported at the installer build,
  and wired into CI so they stay in sync.

---

## 11. IODoc

Each IO type documents its command format:

- existing types: unchanged (string commands as today);
- device types: `"command_format": "json"` plus one entry per field: name, type
  (`enum`/`number`), allowed values or min/max/step, unit, and the link role it requires.

The installer form (§10) and the MCP tool description (§9.2) are built from it.

---

## 12. Supporting changes

- `MqttOutputString` (missing today, needed for `mode/command`, `fan_mode/command`, …).
- `value_min` / `value_max` params on analog outputs (and `InternalInt`), used to bound setpoints.
  Not `min`/`max`: calaos_installer has always written `min="-10000" max="10000"` on analog IOs
  without the server reading them, so reusing those names would silently clamp deployed outputs.
  Those legacy params are dropped by the server at load and no longer written by the installer.
- `ListeRoom::get_chauffage_var()` on the server: dead code (no production caller), removed in a
  separate ticket. The `chauffage_id` param and its JSON key stay: calaos_installer uses them for
  its heating-rule assistant.
- RemoteUI panel restriction: separate security ticket (§9.3).

---

## 13. Implementation phases

1. **Core (calaos_base):**
   - work:
     - `Device` with load/save of children;
     - the change hook;
     - `format="2"` and the `config put` guard;
     - JSON command parsing;
     - `device` object in `get_home`/`io_changed`;
     - `MqttOutputString` and analog `min`/`max`.
   - tests:
     - XML round-trip (load → save → load is identical);
     - a format 1 upload is refused when devices exist;
     - command parsing (plain mode, partial JSON, invalid field).
2. **Climate (calaos_base):**
   - work: `ClimateBase`, `ProxyClimate`, `InternalThermostat`, the MCP filtering and warnings.
   - tests:
     - the hysteresis, min on/off timer expiry and auto changeover;
     - startup without a sensor value, and a stale sensor;
     - the dew-point cut;
     - proxy forwarding and the keep-alive cadence.
3. **IODoc and template generation (calaos_base + CI).**
4. **Installer:** tree view, wizard, cascade delete, format gate, generated action form.
5. **RemoteUI:** server-side expansion of referenced IOs; the firmware climate widget.

---

## 14. Open points

- Debounce value (100 ms) and whether `io_changed` should carry the full `device` object or only
  `state`.
- Whether MCP's service scope may send JSON commands to devices, or only read them.
- Default values per template (hysteresis, min on/off, `sensor_timeout`, `humidity_max`).
