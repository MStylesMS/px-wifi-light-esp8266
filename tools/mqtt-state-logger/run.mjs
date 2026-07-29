#!/usr/bin/env node
/**
 * MQTT state logger for px-wifi-light-esp8266 diagnostics.
 * Subscribes to state/warnings/events and writes JSONL with gap detection.
 */

import mqtt from 'mqtt';
import { createWriteStream, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';

function printHelp() {
    console.log(`px-wifi-light MQTT state logger

Usage:
  node run.mjs [options]

Options:
  --broker <url>           MQTT broker URL (default: mqtt://127.0.0.1:1883)
  --username <value>       MQTT username
  --password <value>       MQTT password
  --state-topic <topic>    State topic (default: paradox/agent22/lights/state)
  --base-topic <topic>     Base topic for warnings/events (default: paradox/agent22/lights)
  --log-dir <path>         Log directory (default: ./logs)
  --gap-ms <ms>            Gap threshold for state heartbeats (default: 16000)
  --help                   Show this help
`);
}

function parseArgs(argv) {
    const options = {
        broker: 'mqtt://127.0.0.1:1883',
        username: undefined,
        password: undefined,
        stateTopic: 'paradox/agent22/lights/state',
        baseTopic: 'paradox/agent22/lights',
        logDir: './logs',
        gapMs: 16000,
    };

    for (let i = 0; i < argv.length; i += 1) {
        const arg = argv[i];
        switch (arg) {
            case '--broker': options.broker = argv[++i]; break;
            case '--username': options.username = argv[++i]; break;
            case '--password': options.password = argv[++i]; break;
            case '--state-topic': options.stateTopic = argv[++i]; break;
            case '--base-topic': options.baseTopic = argv[++i]; break;
            case '--log-dir': options.logDir = argv[++i]; break;
            case '--gap-ms': options.gapMs = Number(argv[++i]); break;
            case '--help': printHelp(); process.exit(0);
            default: throw new Error(`unknown argument: ${arg}`);
        }
    }
    return options;
}

function isoNow() {
    return new Date().toISOString();
}

function summarizeState(topic, payload) {
    const row = {
        recv_ts: isoNow(),
        topic,
        kind: 'state',
    };

    if (payload && typeof payload === 'object') {
        if (payload.status === 'offline') row.status = 'offline';
        row.uptime_s = payload.uptime_s ?? null;
        row.free_heap = payload.free_heap ?? payload.health?.free_heap_bytes ?? null;
        row.min_free_heap = payload.health?.min_free_heap_bytes ?? null;
        row.max_block_bytes = payload.health?.max_block_bytes ?? null;
        row.fading = payload.fading ?? null;
        row.on = payload.on ?? null;
        row.scene = payload.scene ?? null;
        row.fw_version = payload.fw_version ?? null;
        row.wifi_sta = payload.wifi?.sta_connected ?? null;
        row.rssi = payload.wifi?.rssi ?? null;
        row.mqtt_connected = payload.mqtt?.connected ?? null;
        row.mqtt_subscribed = payload.mqtt?.subscribed_commands ?? null;
        row.mqtt_reconnect_count = payload.mqtt?.reconnect_count ?? null;
        row.mqtt_publish_fail_count = payload.mqtt?.publish_fail_count ?? null;
        row.mqtt_last_inbound_cmd_ms = payload.mqtt?.last_inbound_cmd_ms ?? null;
    }

    return row;
}

function summarizeGeneric(topic, payload, kind) {
    return {
        recv_ts: isoNow(),
        topic,
        kind,
        payload,
    };
}

function main() {
    const options = parseArgs(process.argv.slice(2));
    mkdirSync(options.logDir, { recursive: true });

    const stamp = isoNow().replace(/[:.]/g, '-');
    const logPath = join(options.logDir, `lights-${stamp}.jsonl`);
    const gapPath = join(options.logDir, `lights-${stamp}.gaps.log`);
    const out = createWriteStream(logPath, { flags: 'a' });
    const gapOut = createWriteStream(gapPath, { flags: 'a' });

    const topics = [
        options.stateTopic,
        `${options.baseTopic}/warnings`,
        `${options.baseTopic}/events`,
    ];

    console.log(`[${isoNow()}] logging to ${logPath}`);
    console.log(`[${isoNow()}] gap log: ${gapPath}`);
    console.log(`[${isoNow()}] broker=${options.broker} topics=${topics.join(', ')}`);

    let lastStateMs = Date.now();
    let gapTimer = null;

    function scheduleGapCheck() {
        if (gapTimer) clearTimeout(gapTimer);
        gapTimer = setTimeout(() => {
            const nowMs = Date.now();
            const gapMs = nowMs - lastStateMs;
            if (gapMs >= options.gapMs) {
                const line = JSON.stringify({
                    recv_ts: isoNow(),
                    kind: 'gap',
                    topic: options.stateTopic,
                    gap_ms: gapMs,
                    message: `no state received for ${gapMs}ms`,
                });
                gapOut.write(`${line}\n`);
                console.error(`[${isoNow()}] GAP: no state for ${gapMs}ms`);
            }
            scheduleGapCheck();
        }, options.gapMs);
    }

    const client = mqtt.connect(options.broker, {
        username: options.username,
        password: options.password,
        reconnectPeriod: 5000,
        clientId: `px-light-logger-${process.pid}`,
    });

    client.on('connect', () => {
        console.log(`[${isoNow()}] connected`);
        client.subscribe(topics, { qos: 0 }, (err) => {
            if (err) {
                console.error(`[${isoNow()}] subscribe failed: ${err.message}`);
                process.exit(1);
            }
            scheduleGapCheck();
        });
    });

    client.on('message', (topic, buf) => {
        let payload;
        try {
            payload = JSON.parse(buf.toString('utf8'));
        } catch {
            payload = { raw: buf.toString('utf8') };
        }

        let row;
        if (topic === options.stateTopic) {
            lastStateMs = Date.now();
            row = summarizeState(topic, payload);
        } else if (topic.endsWith('/warnings')) {
            row = summarizeGeneric(topic, payload, 'warning');
        } else {
            row = summarizeGeneric(topic, payload, 'event');
        }

        out.write(`${JSON.stringify(row)}\n`);
    });

    client.on('error', (err) => {
        console.error(`[${isoNow()}] mqtt error: ${err.message}`);
    });

    client.on('close', () => {
        console.log(`[${isoNow()}] disconnected`);
    });

    process.on('SIGINT', () => {
        console.log(`[${isoNow()}] stopping`);
        client.end(true, () => process.exit(0));
    });
    process.on('SIGTERM', () => {
        client.end(true, () => process.exit(0));
    });
}

main();
