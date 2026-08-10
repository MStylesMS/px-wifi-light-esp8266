#!/usr/bin/env bash
# Start the MQTT state logger detached from the current shell/SSH session.
# Safe to run over SSH — exits immediately after starting.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${LOG_DIR:-/opt/paradox/logs/lights-monitor}"
PID_FILE="$LOG_DIR/logger.pid"

mkdir -p "$LOG_DIR"

if [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "Logger already running (pid $(cat "$PID_FILE"))"
    exit 0
fi

setsid node "$DIR/run.mjs" \
    --broker "${BROKER:-mqtt://127.0.0.1}" \
    --state-topic "${STATE_TOPIC:-paradox/agent22/lights/state}" \
    --base-topic "${BASE_TOPIC:-paradox/agent22/lights}" \
    --log-dir "$LOG_DIR" \
    --gap-ms "${GAP_MS:-16000}" \
    >> "$LOG_DIR/runner.log" 2>&1 < /dev/null &

echo $! > "$PID_FILE"
echo "Started logger pid $(cat "$PID_FILE") (PPID will become 1)"
