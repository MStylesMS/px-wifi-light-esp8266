# px-wifi-light-esp8266

WiFi-connected light controller for the Paradox Productions escape-room ecosystem.

- **MCU:** ESP12F Devkit V3 (NodeMCU-compatible, CH340 USB)
- **Toolchain:** PlatformIO + Arduino (ESP8266 core)
- **Comms:** WiFi (AP always-on + optional STA)
- **Web UI:** Single-page status page at `http://192.168.4.1/`

## First-time OTA flash (device previously programmed with Arduino IDE)

If the device already has OTA-capable firmware:

### Step 1 — connect your PC to the device AP

The old firmware creates a WiFi access point.  Connect your PC to it.
The device will be reachable at **192.168.4.1** (Arduino IDE default AP IP).

> If you don't know the AP name, scan for ESP_xxxxxx or similar networks
> created by the chip.  You can also check the Arduino serial monitor output
> from the original flash if you have it.

### Step 2 — (optional) verify OTA is listening

```sh
# Ping the device
ping 192.168.4.1

# Check that the ArduinoOTA UDP port is open (port 8266)
# On Windows:
pio device list  # just to confirm PlatformIO sees things
```

### Step 3 — build and OTA-upload the new firmware

```sh
# Flash the filesystem (web UI assets + config.json) via serial first if possible,
# OR skip this step — the device will fall back to built-in defaults until you
# can do a serial uploadfs later.

# OTA upload of the firmware binary:
pio run -e ota -t upload
```

If the original firmware had an OTA password, add it to `platformio.ini` under
`[env:ota]`:
```ini
upload_flags = --auth=YourOldPassword
```

If you get "No route to host" or it times out, the old firmware may not have
had OTA enabled.  In that case you'll need a USB cable for the first flash
(Step 4 below).

### Step 4 — first serial flash (fallback if OTA is not available)

```sh
# Connect USB cable, then:
pio run -e nodemcuv2 -t uploadfs   # flash LittleFS (web UI + config)
pio run -e nodemcuv2 -t upload     # flash firmware
pio device monitor                  # watch the boot log
```

---

## Normal workflow after first flash

Once the new firmware is running, all future updates can be done OTA:

```sh
pio run -e ota -t upload
```

Or over the AP web interface at `http://192.168.4.1/update`
(username `admin`, password = `ap_password` from config, default `Paradox1`).

---

## Repository layout

```
src/       firmware sources
data/      LittleFS assets (index.html, app.js, style.css, config.json)
test/      native host smoke tests
```

## Config

Copy `config.json.example` to `data/config.json` and fill in credentials:

```sh
cp config.json.example data/config.json
```

`data/config.json` is gitignored — your credentials stay local.

## After boot

- AP `px-light-XXXX` at **192.168.4.1** (password: `Paradox1`)
- Joins `Paradox` WiFi and shows its LAN IP on the status page
- mDNS hostname: `px-light-XXXX.local`
- Status page: `http://192.168.4.1/`
- HTTP OTA: `http://192.168.4.1/update`

## Factory reset

Hold the **FLASH** button during boot for 3 seconds.  Wipes `config.json` and
reboots into defaults.
