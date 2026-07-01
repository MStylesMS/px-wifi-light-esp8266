# px-wifi-light-esp8266 — Pin Mapping

**Status:** Draft (wiring diagram lost; assignments below are best-guess from original install)
**Version:** 0.1.0

Authoritative GPIO table. These assignments must not change without a hardware revision and a corresponding update to `src/config.h`.

> **Wiring note.** The original wiring diagram was not preserved.
> The assignments below are inferred from the installation layout.
> Verify with a multimeter before deploying to production.
> Update this file and `src/config.h` if corrections are needed.

## Hardware

- **MCU board:** LoLin NodeMCU V3 (30-pin, ESP12F, CH340 USB-Serial)
  - PlatformIO board target: `nodemcuv2` (identical GPIO map; only physical
    width and USB chip differ from V2)

## GPIO assignments

| NodeMCU label | GPIO | Direction | Role | Notes |
|---------------|------|-----------|------|-------|
| D4 | GPIO2 | OUT digital | White channel (on/off) | Boot-strap: must be HIGH at reset. On-board LED active-LOW also wired here on some carriers — LED will blink inverted to the white output. Active-HIGH drive to transistor/MOSFET assumed. |
| D5 | GPIO14 | OUT PWM | Green channel (0–255) | Software PWM at 1 kHz |
| D6 | GPIO12 | OUT PWM | Red channel (0–255) | Software PWM at 1 kHz |
| D7 | GPIO13 | OUT PWM | Blue channel (0–255) | Software PWM at 1 kHz |
| D3 | GPIO0 | IN pull-up | Factory-reset button | FLASH button on NodeMCU; hold at boot for 3 s |

### Unused / reserved

| NodeMCU label | GPIO | Notes |
|---------------|------|-------|
| D1 | GPIO5 | Free — I2C SCL candidate for future expansion |
| D2 | GPIO4 | Free — I2C SDA candidate for future expansion |
| D8 | GPIO15 | Boot-strap LOW; usable as output after boot |
| D0 | GPIO16 | No internal pull-up; deep-sleep wake (unused) |
| A0 | ADC0 | Free — voltage sense / ambient light sensor candidate |

## Boot-strap reminder

At reset the ESP8266 boot ROM samples three pins to determine boot mode:

| Pin | Required state | How guaranteed |
|-----|---------------|----------------|
| GPIO0 (D3) | HIGH (normal boot) | External pull-up or button released |
| GPIO2 (D4) | HIGH | White channel transistor pulls LOW only after boot; not a concern if transistor is OFF by default |
| GPIO15 (D8) | LOW | On-board pull-down on NodeMCU |

## Compile-time constants

Defined in `src/config.h`:

```cpp
namespace pins {
    inline constexpr uint8_t WHITE     = 2;   // D4 / GPIO2
    inline constexpr uint8_t GREEN     = 14;  // D5 / GPIO14
    inline constexpr uint8_t RED       = 12;  // D6 / GPIO12
    inline constexpr uint8_t BLUE      = 13;  // D7 / GPIO13
    inline constexpr uint8_t FLASH_BTN = 0;   // D3 / GPIO0
}
```

## PWM parameters

| Parameter | Value |
|-----------|-------|
| API range | 0–255 (set via `analogWriteRange(255)`) |
| Frequency | 1 000 Hz (set via `analogWriteFreq(1000)`) |
| Type | Software PWM (ESP8266 SDK) |
| Resolution | ~10-bit internally, exposed as 8-bit |
