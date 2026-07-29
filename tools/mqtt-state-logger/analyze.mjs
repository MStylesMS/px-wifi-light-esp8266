#!/usr/bin/env node
/**
 * Summarize px-wifi-light MQTT state logger JSONL for failure diagnosis.
 */

import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

function printHelp() {
    console.log(`Usage: node analyze.mjs [--log-dir <path>] [--file <path>]

Reads JSONL from the mqtt-state-logger and prints summary + anomaly hints.
`);
}

function parseArgs(argv) {
    const options = { logDir: './logs', file: undefined };
    for (let i = 0; i < argv.length; i += 1) {
        const arg = argv[i];
        if (arg === '--log-dir') options.logDir = argv[++i];
        else if (arg === '--file') options.file = argv[++i];
        else if (arg === '--help') { printHelp(); process.exit(0); }
        else throw new Error(`unknown argument: ${arg}`);
    }
    return options;
}

function loadLines(options) {
    if (options.file) {
        return readFileSync(options.file, 'utf8').split('\n').filter(Boolean);
    }
    const files = readdirSync(options.logDir)
        .filter((name) => name.endsWith('.jsonl'))
        .sort();
    if (!files.length) throw new Error(`no .jsonl files in ${options.logDir}`);
    const latest = files[files.length - 1];
    console.log(`Analyzing ${join(options.logDir, latest)}`);
    return readFileSync(join(options.logDir, latest), 'utf8').split('\n').filter(Boolean);
}

function main() {
    const options = parseArgs(process.argv.slice(2));
    const rows = loadLines(options).map((line) => JSON.parse(line));

    const states = rows.filter((r) => r.kind === 'state');
    const gaps = rows.filter((r) => r.kind === 'gap');
    const warnings = rows.filter((r) => r.kind === 'warning');

    if (!states.length) {
        console.log('No state rows found.');
        return;
    }

    const first = states[0];
    const last = states[states.length - 1];
    const heaps = states.map((r) => r.free_heap).filter((v) => typeof v === 'number');
    const minHeap = Math.min(...heaps);
    const maxHeap = Math.max(...heaps);
    const minFreeReported = states
        .map((r) => r.min_free_heap)
        .filter((v) => typeof v === 'number');
    const reboots = states.filter((r, i) => i > 0 && r.uptime_s < states[i - 1].uptime_s);

    console.log('\n=== Summary ===');
    console.log(`State messages: ${states.length}`);
    console.log(`Gap events: ${gaps.length}`);
    console.log(`Warnings: ${warnings.length}`);
    console.log(`Time span: ${first.recv_ts} -> ${last.recv_ts}`);
    console.log(`Uptime span: ${first.uptime_s}s -> ${last.uptime_s}s`);
    console.log(`Free heap range: ${minHeap} .. ${maxHeap}`);
    if (minFreeReported.length) {
        console.log(`Min free heap reported: ${Math.min(...minFreeReported)}`);
    }
    console.log(`Detected reboots (uptime drop): ${reboots.length}`);

    console.log('\n=== Signature checks ===');
    if (gaps.length) console.log('- Heartbeat gaps detected (device offline or MQTT publish stopped)');
    if (minHeap < 12000) console.log('- Free heap dropped below 12KB (possible exhaustion)');
    if (reboots.length) console.log('- Uptime decreased without manual note (silent reboot)');
    if (last.mqtt_connected === false) console.log('- Last state shows mqtt.connected=false');
    if (last.mqtt_subscribed === false) console.log('- Last state shows mqtt.subscribed_commands=false');
    const stuckFade = states.filter((r) => r.fading === true);
    if (stuckFade.length > 10) console.log(`- fading=true seen ${stuckFade.length} times (check if stuck)`);

    if (gaps.length) {
        console.log('\n=== Recent gaps ===');
        gaps.slice(-5).forEach((g) => console.log(JSON.stringify(g)));
    }
    if (warnings.length) {
        console.log('\n=== Recent warnings ===');
        warnings.slice(-5).forEach((w) => console.log(JSON.stringify(w.payload ?? w)));
    }
}

main();
