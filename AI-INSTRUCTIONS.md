# px-wifi-light-esp8266 — AI Instructions

ESP8266 Wi-Fi light prop firmware for Paradox.

## MQTT topic contract

| Topic | When | Default | Notes |
|-------|------|---------|-------|
| `{baseTopic}/state` | Connect, on change, every heartbeat (~10s) | under `mqtt.base_topic` | Retained state / heartbeat. Prefer `paradox/<room>/<device>/state`. |
| `{baseTopic}/commands` | Inbound | `…/commands` | |
| `{baseTopic}/events` | Outbound | `…/events` | |
| `{baseTopic}/warnings` | Outbound | `…/warnings` | Plural |
| Announce (`mqtt.announce_topic`) | **Once** per MQTT connect/reconnect | `paradox/props` | Discovery bus. May be `<company>/props` for third-party installs. |

Do not publish frequent state to the announce topic.

## Other conventions

- Document non-trivial MQTT/config changes before coding when practical.
- **Prop admin reverse proxy:** HTTP UI honours `X-Forwarded-Prefix` (injects
  `<base href>` into HTML via `src/http_proxy.*`). Static UI uses path-relative
  URLs. See PxD `docs/PROP_ADMIN_REVERSE_PROXY.md`.

## Suite standards

Suite-wide contracts live in [../../../apps/PxH/docs/standards/](../../../apps/PxH/docs/standards/) (folder, not a single file). Read those before changing MQTT topics or shared conventions. If you change a standard, update the file under PxH `docs/standards/` first and propagate to other repos' docs in the same work.
