#!/usr/bin/env bash
set -euo pipefail

LOG_DIR=/opt/paradox/logs/lights-monitor
PID_FILE="$LOG_DIR/logger.pid"

if [[ ! -f "$PID_FILE" ]]; then
    echo "No pid file at $PID_FILE"
    exit 1
fi

PID="$(cat "$PID_FILE")"
if kill -0 "$PID" 2>/dev/null; then
    kill "$PID"
    echo "Stopped logger pid $PID"
else
    echo "Logger pid $PID not running"
fi

rm -f "$PID_FILE"
