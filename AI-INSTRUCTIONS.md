# px-wifi-light-esp8266 — AI Instructions

ESP8266 Wi-Fi light prop firmware for Paradox.

## Conventions

- MQTT: `{baseTopic}/{commands|events|state|warnings}`.
- Announce on connect to `paradox/props`.
- Document non-trivial MQTT/config changes before coding when practical.
- **Prop admin reverse proxy:** HTTP UI honours `X-Forwarded-Prefix` (injects
  `<base href>` into HTML via `src/http_proxy.*`). Static UI uses path-relative
  URLs. See PxD `docs/PROP_ADMIN_REVERSE_PROXY.md`.
