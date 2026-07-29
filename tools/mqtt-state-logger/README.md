# MQTT state logger

Read-only diagnostic logger for `px-wifi-light-esp8266`. Subscribes to `{base}/state`, `{base}/warnings`, and `{base}/events`, writes JSONL, and flags heartbeat gaps.

## Agent 22 (manual start)

```bash
mkdir -p /opt/paradox/logs/lights-monitor
nohup /path/to/run.sh \
  --broker mqtt://127.0.0.1 \
  --state-topic paradox/agent22/lights/state \
  --log-dir /opt/paradox/logs/lights-monitor \
  >> /opt/paradox/logs/lights-monitor/runner.log 2>&1 &
```

Stop with `kill <pid>` or reboot the host.

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
