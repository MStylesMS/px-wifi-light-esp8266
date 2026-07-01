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
  "fw_version": "0.1.0",
  "instance": "px-light-AABB",
  "uptime_s": 1234,
  "free_heap": 38192,
  "on": true,
  "white": false,
  "r": 255, "g": 0, "b": 128,
  "brightness": 100,
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

# Toggle white channel
curl -X POST http://<host>/api/light -d '{"command":"setWhite","white":true}'

# Named scene
curl -X POST http://<host>/api/light \
  -d '{"command":"setColorScene","scene":"warmWhite"}'

# Turn off
curl -X POST http://<host>/api/light -d '{"command":"off"}'
```

### 2.4 Configuration

#### `GET /api/config`

Returns the current persisted configuration.

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
| `{base_topic}/events` | OUT | no | Command outcomes and device events |
| `{base_topic}/warnings` | OUT | no | Validation failures, unknown commands |

### 3.2 Commands (`{base_topic}/commands`)

All commands follow the Paradox envelope `{"command":"<name>", ...params}`.

| Command | Required params | Optional params | Effect |
|---------|-----------------|-----------------|--------|
| `on` / `allOn` | — | — | Turn on; default to white if nothing set |
| `off` / `allOff` | — | — | All channels off |
| `setColor` | `color` | `brightness` | Set RGB (`"#rrggbb"` or `{r,g,b}`); white off |
| `setWhite` | `white` (bool) | — | Toggle white channel; `false` + no RGB turns device off |
| `setBrightness` | `brightness` (0–100) | — | Set PWM scaler |
| `setColorScene` / `scene` | `scene` (string) | — | Apply named scene |
| `getState` / `getStatus` | — | — | Force-publish state |
| `identify` | — | — | Flash 2 s then restore |
| `restart` | — | — | Schedule reboot |

### 3.3 State payload (`{base_topic}/state`)

Retained. See §2.2 for the full schema.

### 3.4 Warning codes

| Code | Meaning |
|------|---------|
| `LIGHT_CMD_UNKNOWN` | `command` name not recognised |
| `LIGHT_CMD_INVALID` | Required parameter missing or malformed |

### 3.5 Announce

On each MQTT connection the device publishes to `mqtt.announce_topic` (default `paradox/props`):

```json
{
  "timestamp": "uptime+Ns",
  "application": "px-wifi-light-esp8266",
  "fw_version": "0.1.0",
  "instance": "px-light-AABB",
  "base_topic": "paradox/lights/px-light-aabb",
  "ip": "192.168.1.42",
  "mac": "AA:BB:CC:DD:EE:FF",
  "mdns": "px-light-aabb.local"
}
```
