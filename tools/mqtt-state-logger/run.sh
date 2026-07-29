#!/usr/bin/env bash
# shellcheck shell=bash
# Read-only MQTT logger for px-wifi-light-esp8266 state/warnings/events.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BROKER="mqtt://127.0.0.1:1883"
STATE_TOPIC="paradox/agent22/lights/state"
BASE_TOPIC="paradox/agent22/lights"
LOG_DIR="/opt/paradox/logs/lights-monitor"
GAP_MS=16000

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --broker <url>         MQTT broker URL (default: mqtt://127.0.0.1:1883)
  --state-topic <topic>  State topic (default: paradox/agent22/lights/state)
  --base-topic <topic>   Base topic (default: paradox/agent22/lights)
  --log-dir <path>       Log directory (default: /opt/paradox/logs/lights-monitor)
  --gap-ms <ms>          Gap threshold (default: 16000)
  --help                 Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --broker) BROKER="$2"; shift 2 ;;
        --state-topic) STATE_TOPIC="$2"; shift 2 ;;
        --base-topic) BASE_TOPIC="$2"; shift 2 ;;
        --log-dir) LOG_DIR="$2"; shift 2 ;;
        --gap-ms) GAP_MS="$2"; shift 2 ;;
        --help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

mkdir -p "$LOG_DIR"

if command -v node >/dev/null 2>&1; then
    if [[ ! -d "$SCRIPT_DIR/node_modules/mqtt" ]]; then
        echo "Installing mqtt dependency in $SCRIPT_DIR ..."
        (cd "$SCRIPT_DIR" && npm install --omit=dev)
    fi
    exec node "$SCRIPT_DIR/run.mjs" \
        --broker "$BROKER" \
        --state-topic "$STATE_TOPIC" \
        --base-topic "$BASE_TOPIC" \
        --log-dir "$LOG_DIR" \
        --gap-ms "$GAP_MS"
fi

if command -v mosquitto_sub >/dev/null 2>&1 && command -v jq >/dev/null 2>&1; then
    STAMP="$(date -u +%Y-%m-%dT%H-%M-%SZ)"
    LOG_FILE="$LOG_DIR/lights-${STAMP}.jsonl"
    GAP_FILE="$LOG_DIR/lights-${STAMP}.gaps.log"
    LAST_STATE_MS="$(date +%s%3N)"

    echo "Logging to $LOG_FILE (mosquitto_sub fallback)"

    mosquitto_sub -h "${BROKER#mqtt://}" -t "$STATE_TOPIC" \
        -t "${BASE_TOPIC}/warnings" -t "${BASE_TOPIC}/events" -v |
    while read -r topic payload; do
        NOW_ISO="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        if [[ "$topic" == "$STATE_TOPIC" ]]; then
            NOW_MS="$(date +%s%3N)"
            GAP=$((NOW_MS - LAST_STATE_MS))
            if (( GAP >= GAP_MS )); then
                printf '{"recv_ts":"%s","kind":"gap","topic":"%s","gap_ms":%s}\n' \
                    "$NOW_ISO" "$STATE_TOPIC" "$GAP" >> "$GAP_FILE"
                echo "[$NOW_ISO] GAP: ${GAP}ms" >&2
            fi
            LAST_STATE_MS="$NOW_MS"
            jq -c --arg recv_ts "$NOW_ISO" --arg topic "$topic" \
                '{recv_ts:$recv_ts, topic:$topic, kind:"state",
                  uptime_s:(.uptime_s // null), free_heap:(.free_heap // .health.free_heap_bytes // null),
                  min_free_heap:(.health.min_free_heap_bytes // null),
                  fading:(.fading // null), on:(.on // null), scene:(.scene // null),
                  fw_version:(.fw_version // null),
                  wifi_sta:(.wifi.sta_connected // null), rssi:(.wifi.rssi // null),
                  mqtt_connected:(.mqtt.connected // null),
                  mqtt_subscribed:(.mqtt.subscribed_commands // null)}' \
                <<< "$payload" >> "$LOG_FILE"
        else
            KIND="event"
            [[ "$topic" == *"/warnings" ]] && KIND="warning"
            jq -c --arg recv_ts "$NOW_ISO" --arg topic "$topic" --arg kind "$KIND" \
                --argjson payload "$payload" \
                '{recv_ts:$recv_ts, topic:$topic, kind:$kind, payload:$payload}' \
                >> "$LOG_FILE" 2>/dev/null || \
                printf '{"recv_ts":"%s","topic":"%s","kind":"%s","raw":%s}\n' \
                    "$NOW_ISO" "$topic" "$KIND" "$(jq -Rs . <<< "$payload")" >> "$LOG_FILE"
        fi
    done
    exit 0
fi

echo "ERROR: need node (preferred) or mosquitto_sub+jq" >&2
exit 1
