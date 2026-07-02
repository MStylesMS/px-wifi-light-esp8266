# px-wifi-light-esp8266 — Hardware Specification

**Status:** Draft
**Version:** 0.1.0

---

## MCU Board

| Item | Value |
|------|-------|
| Board | LoLin NodeMCU V3 (30-pin) |
| Module | ESP-12F (ESP8266MOD) |
| Flash | 4 MB (accessible via LittleFS for config + web assets) |
| USB-Serial | CH340G |
| Operating voltage | 3.3 V logic / 5 V USB input |
| Power draw (typ) | ~80 mA @ 3.3 V during WiFi TX bursts |

### NodeMCU V3 vs V2 note

The LoLin V3 is physically wider (30 pins including extra GND/3V3/VIN power rails) than some V2 designs (22 pins). The ESP-12F GPIO mapping and ESP8266 core are identical. PlatformIO `board = nodemcuv2` is the correct target for both.

---

## Output Channels

| Channel | NodeMCU | GPIO | Type | Load |
|---------|---------|------|------|------|
| White | D1 | 5 | Digital on/off | White LED strip or bulb via transistor/MOSFET |
| UV | D4 | 2 | PWM 0–255 | UV LED strip via transistor/MOSFET |
| Red | D6 | 12 | PWM 0–255 | Red LED strip via transistor/MOSFET |
| Green | D5 | 14 | PWM 0–255 | Green LED strip via transistor/MOSFET |
| Blue | D7 | 13 | PWM 0–255 | Blue LED strip via transistor/MOSFET |

All outputs are **active-HIGH**, 3.3 V logic.  A transistor or MOSFET driver stage is required between the ESP8266 GPIO and any LED load.

The UV channel (D4/GPIO2) is **fully independent** — it is not gated by the master on/off state and must be controlled explicitly via the `setUV` command.

> **Wiring lost.** The original wiring diagram was not preserved. Verify actual driver polarity before power-on.

---

## Power

The NodeMCU V3 is powered from the USB port (`VIN` / 5 V rail) or via the `3V3` pin. The ESP8266 draws up to ~350 mA during WiFi bursts; ensure the supply can source at least 500 mA for headroom.

LED load current is separate and depends on the driver stage and strip specification.

---

## Factory Reset

Hold the on-board **FLASH** button (GPIO0 / D3) for ≥ 3 seconds during power-on. The firmware will wipe `/config.json` and reboot into defaults.

---

## Antenna

The ESP-12F has a PCB antenna. Ensure it is not obstructed inside the enclosure.
