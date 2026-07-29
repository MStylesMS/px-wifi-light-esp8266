#!/usr/bin/env bash
# OTA deploy helper — run on Agent 22 AFTER customer sessions end.
set -euo pipefail

FIRMWARE="${1:-/opt/paradox/tools/px-wifi-light-firmware.bin}"
HOST="${2:-10.0.4.155}"
USER="${OTA_USER:-admin}"
PASS="${OTA_PASS:-MCEscher}"

if [[ ! -f "$FIRMWARE" ]]; then
    echo "Firmware not found: $FIRMWARE" >&2
    exit 1
fi

echo "Deploying $FIRMWARE to http://${HOST}/update ..."
curl -f -S --user "${USER}:${PASS}" -F "update=@${FIRMWARE}" "http://${HOST}/update"
echo
echo "Waiting for reboot..."
sleep 15
echo "Checking state via MQTT (requires mosquitto_sub)..."
timeout 30 mosquitto_sub -h 127.0.0.1 -t paradox/agent22/lights/state -C 1 -W 20 | head -1
