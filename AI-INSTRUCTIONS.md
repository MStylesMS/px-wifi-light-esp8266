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
- **Prop admin reverse proxy:** HTTP UI honours `X-Forwarded-Prefix` / Host /
  Proto via `src/http_proxy.*` (injects `<base href>`, `join_path`, `build_url`,
  `build_ws_url`). Static UI uses path-relative URLs. See PxD
  `docs/PROP_ADMIN_REVERSE_PROXY.md`.

## Suite standards

Public suite brief + contracts live in [../../../apps/PxH/docs/standards/](../../../apps/PxH/docs/standards/) (folder, not a single file) — especially `AI-INSTRUCTIONS.md` and `MQTT-CONTRACT.md`. Read those before changing MQTT topics or shared conventions. If you change a standard, update the file under PxH `docs/standards/` first and propagate to other repos' docs in the same work.

If the workspace has `Px-Suite/` (or `/opt/paradox/Px-Suite`), use it for internal notes, cross-cutting pending plans, and business overview — do not put those into distributed PxH standards.

