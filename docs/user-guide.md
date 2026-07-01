# px-wifi-light-esp8266 — User Guide

**Status:** Draft
**Version:** 0.1.0

---

## 1. What It Does

The px-wifi-light is a WiFi-connected RGBW light controller for Paradox Productions escape rooms. It controls four output channels:

- **White** — on/off (D4/GPIO2)
- **Red** — PWM 0–100% (D6/GPIO12)
- **Green** — PWM 0–100% (D5/GPIO14)
- **Blue** — PWM 0–100% (D7/GPIO13)

It is controlled via MQTT commands and provides a local web UI for setup and manual control.

---

## 2. First Boot

1. Power on the device via USB.
2. The device boots, mounts its filesystem, and immediately brings up a WiFi access point named `px-light-XXXX` (last 4 hex digits of MAC).
3. Connect your phone or laptop to that AP (password: **Paradox1**).
4. Open `http://192.168.4.1` in a browser.
5. The Web UI loads showing light controls and device status.

---

## 3. Configuration

All settings are stored in `/config.json` on the device's internal flash (LittleFS). You can edit settings via the Web UI or by pre-loading a `data/config.json` before flashing with `pio run -t uploadfs`.

### 3.1 Key settings

| Setting | Default | Description |
|---------|---------|-------------|
| `device.prop_name` | `px-light-XXXX` | Human name; sets AP SSID and mDNS hostname |
| `wifi.primary.ssid` | _(empty)_ | LAN WiFi SSID to join |
| `wifi.primary.password` | _(empty)_ | LAN WiFi password |
| `wifi.ap_password` | `Paradox1` | AP and OTA update password |
| `mqtt.host` | _(empty)_ | MQTT broker hostname or IP |
| `mqtt.port` | `1883` | MQTT broker port |
| `mqtt.base_topic` | `paradox/lights/px-light-XXXX` | Root topic for this device |
| `mqtt.heartbeat_interval_ms` | `10000` | How often to publish state (ms) |

### 3.2 Changing settings via the Web UI

Use `POST /api/config` with a partial JSON body:

```bash
curl -X POST http://192.168.4.1/api/config \
  -H 'Content-Type: application/json' \
  -d '{"mqtt":{"host":"192.168.1.10","base_topic":"paradox/lights/stage-left"}}'
```

Changes to WiFi credentials or MQTT host/port trigger an automatic reboot.

---

## 4. Controlling Lights

### 4.1 Via MQTT

Publish to `{base_topic}/commands` (e.g. `paradox/lights/px-light-aabb/commands`):

```json
{ "command": "on" }
{ "command": "setColorScene", "scene": "cyan" }
{ "command": "setColor", "color": "#ff8000", "brightness": 75 }
{ "command": "off" }
```

See `docs/api.md` for the full command reference and scene table.

### 4.2 Via the Web UI

The web UI at `http://192.168.4.1/` provides:

- **On / Off / Identify** buttons
- **Scene** quick-select grid (White, Warm, Red, Green, Blue, etc.)
- **RGB sliders** for custom colours
- **Brightness** slider
- Live state display (colour swatch, current scene, MQTT status)

---

## 5. OTA Updates

### ArduinoOTA (PlatformIO)

Connect to the same network as the device, then:

```sh
pio run -e ota -t upload
```

The `[env:ota]` environment in `platformio.ini` targets `192.168.4.1` by default. Change `upload_port` to the device's LAN IP after it joins your network.

### HTTP update page

Navigate to `http://<device-ip>/update` (username: `admin`, password: AP password). Upload the `.bin` file from `.pio/build/nodemcuv2/firmware.bin`.

---

## 6. Factory Reset

**Method 1 — Hardware:** Hold the **FLASH** button (D3 / GPIO0) during power-on for ≥ 3 seconds.

**Method 2 — Web:** `POST http://<host>/api/reset`

**Method 3 — Web UI:** Click **Factory Reset** on the status page.

Factory reset erases `/config.json`; the device reboots into built-in defaults with the open AP.

---

## 7. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| AP not appearing | Device crashed at boot | Serial monitor for errors; factory reset |
| White channel flickers at boot | GPIO2 bootstrap | Expected on some boards (on-board LED also on GPIO2); safe to ignore |
| MQTT not connecting | `mqtt.host` not set or unreachable | Check config via Web UI; verify broker is running |
| Colour wrong | Red/green/blue channels may be swapped on your wiring | Swap `pins::RED` / `pins::GREEN` / `pins::BLUE` in `src/config.h` and re-flash |
| OTA times out | PC not on same network as device | Use AP IP (192.168.4.1) or switch PC to same LAN |
