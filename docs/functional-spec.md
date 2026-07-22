# px-wifi-light-esp8266 — Functional Specification

**Status:** Draft
**Version:** 0.1.0

This document specifies **what the device does**. Update this document before changing behaviour.

---

## 1. Roles and Responsibilities

The device has three cooperating concerns running in a single non-blocking cooperative loop:

1. **Light output engine** — owns the five hardware channels (white on/off, UV PWM, RGB PWM), applies brightness scaling to RGB, and processes scene lookups.
2. **MQTT command / state surface** — receives Paradox light commands, publishes retained state and heartbeats.
3. **Network / Web UI surface** — WiFi (STA + always-on AP), HTTP settings / control page, OTA updates.

The device is **permanently powered**. "Off" means all channels at zero; the network surface, MQTT client, and Web UI remain fully active.

---

## 2. Hardware Output Model

```
              ┌─────────────┐
  D1 GPIO5  ──┤ White (dig) ├──► white LED driver (on/off)
  D2 GPIO4  ──┤ UV    (PWM) ├──► UV    LED driver (0–255) [independent]
  D5 GPIO14 ──┤ Green (PWM) ├──► green LED driver (0–255)
  D6 GPIO12 ──┤ Red   (PWM) ├──► red   LED driver (0–255)
  D7 GPIO13 ──┤ Blue  (PWM) ├──► blue  LED driver (0–255)
              └─────────────┘
```

All five channels are **active-HIGH** (HIGH = on). PWM channels use ESP8266 software PWM at 1 kHz. A global `brightness` scaler (0–100 %) is applied to RGB channels before writing to hardware.

The white channel is digital (full-on or full-off) and is **not** affected by the brightness scaler.

The UV channel is **fully independent**: it is not gated by the master `on` flag, not affected by `brightness`, and not modified by scenes. It must be controlled explicitly via `setUV`.

---

## 3. Light State

The canonical light state has six fields:

| Field | Type | Description |
|-------|------|-------------|
| `on` | bool | Master output enable. `false` forces white and RGB channels to zero. Does **not** affect UV. |
| `white` | bool | White channel (D1). Ignored when `on=false`. |
| `r`, `g`, `b` | uint8 0–255 | RGB channel targets. Scaled by `brightness` at output. |
| `brightness` | uint8 0–100 | Global PWM scaler (%). Does not affect white or UV channels. |
| `uv` | uint8 0–255 | UV channel level (D4). Fully independent — not affected by `on`, `brightness`, or scenes. |
| `scene` | string | Last named scene applied, or `""`. UV is never modified by scene changes. |

All state transitions take effect immediately and are reflected in the next MQTT state publish.

---

## 4. Commands

All commands follow the Paradox command envelope:

```json
{ "command": "<name>", ... }
```

### Supported commands

| Command | Params | Effect |
|---------|--------|--------|
| `on` / `allOn` | — | Set `on=true`. If no channels were set, defaults to white on. |
| `off` / `allOff` | — | Set `on=false`. Channel targets are preserved for next `on`. |
| `setColor` | `color: "#rrggbb"` or `{r,g,b}`, optional `brightness` | Set RGB, turn white off, set `on=true`. |
| `setWhite` | `white: true\|false` | Turn white channel on or off. `false` + no RGB active turns `on=false`. |
| `setBrightness` | `brightness: 0–100` | Set brightness scaler. Sets `on=true` if currently off. |
| `setUV` | `level: 0–255` | Set UV channel level. Independent of on/off and brightness. |
| `setColorScene` / `scene` | `scene: "<name>"` | Apply a named scene (see §5). |
| `getState` / `getStatus` | — | Re-publish retained state immediately. |
| `identify` | — | Flash all channels full-white for 2 s, then restore prior state. |
| `restart` | — | Schedule a firmware reboot (~500 ms). |

Unknown commands produce a `LIGHT_CMD_UNKNOWN` warning on the `/warnings` topic.

---

## 5. Named Scenes

Scenes are resolved by `light_ctrl::apply_scene()`. Name matching is case-insensitive.

| Scene name | White | R | G | B | Brightness |
|------------|:-----:|---|---|---|:----------:|
| `off` | off | 0 | 0 | 0 | 100 |
| `white` / `normal` / `brightWhite` | on | 0 | 0 | 0 | 100 |
| `softWhite` | off | 255 | 223 | 223 | 50 |
| `warmWhite` | on | 32 | 8 | 0 | 100 |
| `dim` | off | 255 | 255 | 255 | 30 |
| `coolWhite` | off | 80 | 80 | 255 | 100 |
| `red` | off | 255 | 0 | 0 | 100 |
| `green` | off | 0 | 255 | 0 | 100 |
| `blue` | off | 0 | 0 | 255 | 100 |
| `yellow` | off | 255 | 255 | 0 | 100 |
| `orange` | off | 255 | 128 | 0 | 100 |
| `cyan` | off | 0 | 255 | 255 | 100 |
| `magenta` | off | 255 | 0 | 255 | 100 |
| `purple` | off | 128 | 0 | 255 | 100 |
| `pink` | off | 255 | 64 | 128 | 100 |

---

## 6. MQTT Behaviour

### Topic roles (Paradox contract)

| Role | Topic | Cadence | Default |
|------|-------|---------|---------|
| **Announce** | `mqtt.announce_topic` | Once per MQTT connect/reconnect | `paradox/props` (or `<company>/props` for third-party installs) |
| **State / heartbeat** | `{base_topic}/state` | Connect, on change, every `heartbeat_interval_ms` (~10s) | Prefer `paradox/<room>/<device>/state` |
| Commands / events / warnings | `{base_topic}/…` | As needed | Plural `/warnings` |

Announce is for discovery (PxH props panel, catalog). Periodic heartbeats must use `{base_topic}/state`, not the announce topic.

### Topics

| Topic | Direction | Retained | Description |
|-------|-----------|:--------:|-------------|
| `{base_topic}/commands` | IN | no | Receives Paradox command payloads |
| `{base_topic}/state` | OUT | **yes** | Full device state; published on connect, on change, and every `heartbeat_interval_ms` |
| `{base_topic}/scenes` | OUT | **yes** | Available colour scenes; published once per MQTT connection |
| `{base_topic}/events` | OUT | no | Command outcomes and device events |
| `{base_topic}/warnings` | OUT | no | Validation failures, unknown commands |
| `mqtt.announce_topic` | OUT | no | One-shot online announce (see above) |

### Scenes publish

On each MQTT connection, immediately after the state publish, the device publishes a retained payload to `{base_topic}/scenes` listing the scenes available for the `setColorScene` command. The list is fixed at compile time; each entry carries an `id` (passed to `setColorScene`), a human-readable `label`, and a `swatch` hex colour for UI rendering. A newly connecting subscriber receives this payload immediately from the broker without waiting for a heartbeat.

### Heartbeat

State is published:
1. On first MQTT connection (and each reconnect).
2. Immediately after any command that changes output state.
3. Every `heartbeat_interval_ms` (default 10 000 ms).

### Last Will

On unclean disconnect, the broker publishes a tombstone to `{base_topic}/state`:

```json
{ "timestamp": "uptime+Ns", "application": "px-wifi-light-esp8266",
  "instance": "<prop_name>", "status": "offline" }
```

---

## 7. WiFi Behaviour

- **AP + STA always on simultaneously** (`WIFI_AP_STA` mode).
- AP IP fixed at `192.168.4.1`. SSID derived from `prop_name` (lower-case, spaces → hyphens).
- STA connects to `wifi.primary`; falls back to `wifi.backup` if primary is unavailable (via `ESP8266WiFiMulti`).
- MQTT reconnects automatically with 5-second back-off whenever STA is connected and `mqtt.host` is set.

---

## 8. Web UI

- Always reachable at `http://192.168.4.1/` (AP) and at the STA IP when joined.
- Also reachable via mDNS at `http://<prop_name>.local/`.
- No login required.
- Provides a live status + light control page that polls `/api/state` every 10 s.
- HTTP OTA update endpoint at `/update` (credentials: `admin` / `ap_password`).

---

## 9. Configuration Persistence

- Config stored in LittleFS at `/config.json`.
- On parse error the bad file is renamed to `/config.bad.json` and defaults are restored.
- Factory reset: hold FLASH button (GPIO0) during boot for ≥ 3 s, or `POST /api/reset`.

---

## 10. Startup Sequence

```
setup()
  pxlog::begin()
  LittleFS.begin()         (format if mount fails)
  factory_reset_check()
  cfg::load()
  light_ctrl::begin()      (all channels off)
  wifi_mgr::begin()        (AP up immediately, STA starts connecting)
  web_ui::begin()          (HTTP server on port 80)
  ota_mgr::begin()         (ArduinoOTA + mDNS)
  commands::begin()
  mqtt_mgr::begin()        (connect deferred until STA available)

loop()
  wifi_mgr::loop()
  web_ui::loop()
  ota_mgr::loop()
  mqtt_mgr::loop()         (heartbeat, reconnect, message dispatch)
  light_ctrl::tick()       (identify timer)
  commands::tick()         (deferred restart)
  serial heartbeat every 30 s
```
