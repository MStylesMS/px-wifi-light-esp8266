# MQTT state logger

Read-only diagnostic logger for `px-wifi-light-esp8266`. Subscribes to `{base}/state`, `{base}/warnings`, and `{base}/events`, writes JSONL, and flags heartbeat gaps.

## Agent 22 (manual start)

```bash
# Over SSH — use start-detached.sh or ssh -f so the process survives disconnect:
/opt/paradox/tools/mqtt-state-logger/start-detached.sh

# Or from your workstation:
ssh -f paradox@agent22.story-geological.ts.net \
  '/opt/paradox/tools/mqtt-state-logger/start-detached.sh'
```

Stop:

```bash
/opt/paradox/tools/mqtt-state-logger/stop.sh
```

PID file: `/opt/paradox/logs/lights-monitor/logger.pid`

Logs: `/opt/paradox/logs/lights-monitor/lights-*.jsonl`

The process runs under init (PPID 1) after start — safe to close SSH.

## Analyze logs after a failure

```bash
node analyze.mjs --log-dir /opt/paradox/logs/lights-monitor
```

## OTA deploy (after customer sessions)

Firmware is staged at `/opt/paradox/tools/px-wifi-light-firmware-v0.4.1.bin` on Agent 22.

```bash
/opt/paradox/tools/deploy-agent22-ota.sh /opt/paradox/tools/px-wifi-light-firmware-v0.4.1.bin
```

## Local test

```bash
cd tools/mqtt-state-logger
npm install
node run.mjs --broker mqtt://127.0.0.1 --log-dir ./logs
```
