# px-wifi-light-esp8266 — API Reference

All interfaces are available once the device is on the network (STA mode) or via its own access point (AP mode). The device exposes two integration surfaces: an **HTTP REST API** and **MQTT topics**.

---

## 1. Addressing

| Mode | Address |
|------|---------|
| AP IP | `192.168.4.1` (always) |
| STA IP | shown on the Web UI status page / `GET /api/state` → `wifi.sta_ip` |
| mDNS | `<prop_name>.local` (e.g. `px-light-aabb.local`) |

Replace `<host>` in every example below with any of the above.

---

## 2. HTTP REST API

All endpoints run on **port 80**. Requests and responses use `application/json`. No authentication on the REST API (the AP password applies only to the HTTP OTA endpoint and ArduinoOTA).

### 2.1 Static assets

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/` | Web UI (`index.html`) |
| `GET` | `/style.css` | Stylesheet |
| `GET` | `/app.js` | Frontend script |

### 2.2 Device state

#### `GET /api/state`

Returns the full device state (same schema as the MQTT `/state` topic).

```bash
curl http://<host>/api/state
```

```json
{
  "timestamp": "uptime+1234s",
  "application": "px-wifi-light-esp8266",
  "fw_version": "0.5.1",
  "instance": "px-light-AABB",
  "uptime_s": 1234,
  "free_heap": 38192,
  "health": {
    "free_heap_bytes": 38192,
    "min_free_heap_bytes": 35000,
    "max_block_bytes": 16384
  },
  "mqtt": {
    "connected": true,
    "subscribed_commands": true,
    "reconnect_count": 0,
    "publish_fail_count": 0,
    "last_inbound_cmd_ms": 120000
  },
  "on": true,
  "white": false,
  "r": 255, "g": 0, "b": 128,
  "brightness": 100,
  "uv": 0,
  "fading": false,
  "default_fade_time_s": 1.0,
  "scene": "magenta",
  "wifi": {
    "sta_connected": true,
    "ap_ip": "192.168.4.1",
    "ap_ssid": "px-light-aabb",
    "ap_clients": 1,
    "sta_ip": "192.168.1.42",
    "sta_ssid": "Paradox",
    "rssi": -58,
    "mac": "AA:BB:CC:DD:EE:FF",
    "mdns": "px-light-aabb.local"
  }
}
```

`free_heap` is retained for backward compatibility; prefer `health.free_heap_bytes`.
`health.min_free_heap_bytes` is the lowest free heap seen since boot.
`health.max_block_bytes` helps detect heap fragmentation.
`mqtt.last_inbound_cmd_ms` is device uptime (ms) when a command was last received.

### 2.3 Light control

#### `POST /api/light`

Send any Paradox light command (same format as the MQTT `/commands` topic).

```bash
curl -X POST http://<host>/api/light \
  -H 'Content-Type: application/json' \
  -d '{"command":"setColorScene","scene":"cyan"}'
```

Returns `{"ok":true}` or `{"ok":false,"error":"..."}`.

**Example commands:**

```bash
# Turn on
curl -X POST http://<host>/api/light -d '{"command":"on"}'

# Set explicit colour
curl -X POST http://<host>/api/light \
  -d '{"command":"setColor","color":"#ff8000","brightness":80}'

# Fade to a colour over 2.5 seconds
curl -X POST http://<host>/api/light \
  -d '{"command":"setColor","color":"#ff8000","brightness":80,"fadeTime":2.5}'

# Toggle white channel
curl -X POST http://<host>/api/light -d '{"command":"setWhite","white":true}'

# Named scene
curl -X POST http://<host>/api/light \
  -d '{"command":"setColorScene","scene":"softWhite"}'

# Turn off
curl -X POST http://<host>/api/light -d '{"command":"off"}'
```

### 2.4 Configuration

#### `GET /api/config`

Returns the current persisted configuration, including `light.default_fade_time_s`
(default `1.0`) — the fallback fade duration (seconds) used by `on`/`off`/
`setBrightness`/`setColor`/`fade` when a command omits `fadeTime`.

#### `POST /api/config`

Partial or full config update. Only fields in the request body are changed.

```bash
curl -X POST http://<host>/api/config \
  -H 'Content-Type: application/json' \
  -d '{"mqtt":{"host":"192.168.1.10"}}'
```

Returns `{"ok":true,"reboot_required":<bool>}`. Changes to WiFi credentials, hostname, MQTT host/port, or AP password trigger an automatic reboot (~1.5 s).

#### `GET /api/scan`

Trigger a WiFi network scan and return discovered access points. **Blocks ~2–4 seconds.**

```bash
curl http://<host>/api/scan
```

```json
{
  "networks": [
    { "ssid": "Paradox", "rssi": -52, "secure": true },
    { "ssid": "GuestWifi", "rssi": -71, "secure": false }
  ]
}
```

#### `POST /api/config/reset` (via `POST /api/reset`)

Factory-reset: wipes `/config.json` and reboots into defaults.

### 2.5 System

#### `GET /api/status`

Brief connectivity summary (subset of `/api/state`).

#### `POST /api/restart`

Schedules a device reboot (~500 ms). Returns immediately.

#### `POST /api/reset`

Factory reset and reboot.

### 2.6 OTA update

| Path | Credentials |
|------|-------------|
| `GET /update` | — (shows upload form) |
| `POST /update` | username `admin`, password = `ap_password` from config (default `Paradox1`) |

---

## 3. MQTT

Topics are relative to `{base_topic}` from the device config (default `paradox/lights/px-light-XXXX`).

### 3.1 Topic summary

| Topic | Direction | Retained | Description |
|-------|-----------|:--------:|-------------|
| `{base_topic}/commands` | IN | no | Light command payloads |
| `{base_topic}/state` | OUT | **yes** | Full device state (heartbeat + on-change) |
| `{base_topic}/scenes` | OUT | **yes** | Available colour scenes (published on connect) |
| `{base_topic}/events` | OUT | no | Command outcomes and device events |
| `{base_topic}/warnings` | OUT | no | Validation failures, unknown commands |

### 3.2 Commands (`{base_topic}/commands`)

All commands follow the Paradox envelope `{"command":"<name>", ...params}`.

| Command | Required params | Optional params | Effect |
|---------|-----------------|-----------------|--------|
| `on` / `allOn` | — | `brightness` (0–100), `fadeTime` (s) | Turn on; restore UV from last off; default white if nothing set |
| `off` / `allOff` | — | `fadeTime` (s) | Zero white, RGB outputs, and UV; preserve for next on; `scene=off` |
| `setColor` | `color` | `brightness`, `fadeTime` (s), `white` | Set RGB (`"#rrggbb"` or `{r,g,b}`); default white off; optional `white` preserves MOSFET; UV unchanged |
| `setWhite` | `white` (bool) | — | Toggle white channel |
| `setUV` | `level` (0–255) | `fadeTime` (s) | Set UV PWM 0–255 (not scaled by brightness) |
| `setBrightness` | `brightness` (0–100) | `fadeTime` (s) | Set RGB PWM scaler |
| `fade` | — | `brightness`, `color`, `level` (UV), `fadeTime` (s) | Ramp brightness/colour/UV to a target |
| `setDefaultFadeTime` | `fadeTime` (s, 0–60) | — | Persist the fallback fade duration used when a command omits `fadeTime` |
| `setColorScene` / `scene` | `scene` (string) | `fadeTime` (s) | Apply named scene; non-`uv` scenes force UV=0 |
| `getState` / `getStatus` | — | — | Force-publish state |
| `identify` | — | — | Flash 2 s then restore |
| `restart` | — | — | Schedule reboot |

#### Fading (`fadeTime`)

`on`, `off`, `setBrightness`, `setColor`, `fade`, and `setColorScene`/`scene` accept an optional `fadeTime` field — duration in **seconds** (float, e.g. `2.5`).

- If `fadeTime` is present (including explicitly `0`), it always wins — `0` means immediate, no fade.
- If `fadeTime` is **omitted**, the command falls back to the persisted `light.default_fade_time_s` config value (default `1.0`, i.e. a 1 second fade by default).

Use `setDefaultFadeTime` to change that persisted default:

```bash
curl -X POST http://<host>/api/light -d '{"command":"setDefaultFadeTime","fadeTime":2}'
```

This is also available over MQTT (`{base_topic}/commands`) and persists to `/config.json` immediately (no reboot required), emitting a `default-fade-time-updated` event.

Fade ramps brightness, RGB, and UV at ~30 Hz. The white channel is a digital on/off MOSFET and cannot be dimmed, so it switches instantly at the start of the fade. Sending any new command while a fade is in progress cancels it immediately and starts the new transition from the live in-progress values — it never finishes the original fade first. `off`/`allOff` zero UV as well as white/RGB outputs, and preserve prior channel values (including UV) for the next `on`/`allOn`. Successful light commands publish an event on `{base_topic}/events` with `type: "light"`.

```bash
curl -X POST http://<host>/api/light -d '{"command":"fade","brightness":40,"fadeTime":3}'
curl -X POST http://<host>/api/light -d '{"command":"off","fadeTime":2.5}'
curl -X POST http://<host>/api/light -d '{"command":"setColor","color":"#00dcff","brightness":100}'  # uses the 1s default
```

### 3.3 State payload (`{base_topic}/state`)

Retained. See §2.2 for the full schema.

### 3.4 Warning codes

| Code | Meaning |
|------|---------|
| `LIGHT_CMD_UNKNOWN` | `command` name not recognised |
| `LIGHT_CMD_INVALID` | Required parameter missing or malformed |

### 3.5 Scenes (`{base_topic}/scenes`)

Published with **retain=true** immediately after each MQTT connection. A newly connecting UI subscriber receives this payload without waiting for the next heartbeat.

```json
{
  "scenes": [
    { "id": "white",       "label": "White",        "swatch": "#F4F4F4" },
    { "id": "brightWhite", "label": "Bright White", "swatch": "#FFFFFF" },
    { "id": "softWhite",   "label": "Soft White",   "swatch": "#FFE8E0" },
    { "id": "moonlight",   "label": "Moonlight",    "swatch": "#B0B0C8" },
    { "id": "coolWhite",   "label": "Cool White",   "swatch": "#A0C8FF" },
    { "id": "nightLight",  "label": "Night Light",  "swatch": "#FF8000" },
    { "id": "red",         "label": "Red",          "swatch": "#FF0000" },
    { "id": "orange",      "label": "Orange",       "swatch": "#FF6E00" },
    { "id": "yellow",      "label": "Yellow",       "swatch": "#FFDC00" },
    { "id": "green",       "label": "Green",        "swatch": "#00FF5A" },
    { "id": "cyan",        "label": "Cyan",         "swatch": "#00DCFF" },
    { "id": "blue",        "label": "Blue",         "swatch": "#0046FF" },
    { "id": "magenta",     "label": "Magenta",      "swatch": "#FF00C8" },
    { "id": "purple",      "label": "Purple",       "swatch": "#AA3CFF" },
    { "id": "pink",        "label": "Pink",         "swatch": "#FF4080" },
    { "id": "uv",          "label": "UV",           "swatch": "#2A0038" },
    { "id": "off",         "label": "Off",          "swatch": "#000000" }
  ]
}
```

Each entry:

| Field | Description |
|-------|-------------|
| `id` | The scene identifier to pass in a `setColorScene` command |
| `label` | Human-readable display name |
| `swatch` | CSS hex colour representing the scene visually |

### 3.6 Announce

On each MQTT connection the device publishes **once** to `mqtt.announce_topic`
(default `paradox/props`; third-party installs may use `<company>/props`).
This is the discovery bus — not the periodic heartbeat. Live state continues
on `{base_topic}/state` (prefer `paradox/<room>/<device>/state`).

```json
{
  "timestamp": "uptime+Ns",
  "application": "px-wifi-light-esp8266",
  "fw_version": "0.5.1",
  "instance": "px-light-AABB",
  "base_topic": "paradox/lights/px-light-aabb",
  "ip": "192.168.1.42",
  "mac": "AA:BB:CC:DD:EE:FF",
  "mdns": "px-light-aabb.local"
}
```
