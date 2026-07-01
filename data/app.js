/* app.js — px-wifi-light-esp8266 web UI */
(function () {
  'use strict';

  // ---- helpers ----
  function $(id) { return document.getElementById(id); }
  function setText(id, val) { var el = $(id); if (el) el.textContent = (val != null) ? val : '—'; }

  function showMsg(text, isError) {
    var el = $('msg');
    if (!el) return;
    el.textContent = text;
    el.classList.remove('hidden', 'error');
    if (isError) el.classList.add('error');
    clearTimeout(showMsg._t);
    if (!isError) showMsg._t = setTimeout(function () { el.classList.add('hidden'); }, 3000);
  }

  function fmtUptime(s) {
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
    return (h ? h + 'h ' : '') + (m ? m + 'm ' : '') + sec + 's';
  }

  function toHex2(n) { return ('0' + (n & 0xff).toString(16)).slice(-2); }
  function rgbToHex(r, g, b) { return '#' + toHex2(r) + toHex2(g) + toHex2(b); }

  // ---- light command ----
  function sendCommand(payload, cb) {
    fetch('/api/light', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d.ok) showMsg('Command failed: ' + (d.error || '?'), true);
        else { if (cb) cb(); loadState(); }
      })
      .catch(function (e) { showMsg('Error: ' + e, true); });
  }

  // ---- state load ----
  function applyState(d) {
    // MQTT badge
    var badge = $('mqtt-badge');
    if (badge) {
      badge.textContent = 'MQTT';
      badge.className = 'badge ' + (d.mqtt_connected ? 'badge-ok' : 'badge-off');
    }

    // Light preview
    var on  = d.on;
    var hex = on ? rgbToHex(d.r || 0, d.g || 0, d.b || 0) : '#000';
    // Show white tint if white is on
    if (on && d.white) {
      hex = d.r || d.g || d.b ? hex : '#fff';
    }
    var swatch = $('color-swatch');
    if (swatch) swatch.style.background = hex;
    setText('light-state-label', on ? 'ON' : 'OFF');
    setText('light-scene-label', d.scene || '');

    // Sliders
    $('sl-r').value      = d.r || 0;
    $('sl-g').value      = d.g || 0;
    $('sl-b').value      = d.b || 0;
    $('sl-bright').value = d.brightness != null ? d.brightness : 100;
    updateSliderLabels();

    // WiFi status
    var wifi = d.wifi || {};
    setText('prop-name',  d.instance);
    setText('fw-version', d.fw_version);
    setText('ap-ip',      wifi.ap_ip);
    setText('sta-ip',     wifi.sta_connected ? wifi.sta_ip : 'not connected');
    setText('sta-ssid',   wifi.sta_connected ? wifi.sta_ssid : '—');
    setText('sta-rssi',   wifi.sta_connected ? wifi.rssi + ' dBm' : '—');
    setText('mdns',       wifi.mdns);
    setText('uptime',     fmtUptime(d.uptime_s || 0));
    setText('free-heap',  ((d.free_heap || 0) / 1024).toFixed(1) + ' KB');
  }

  function loadState() {
    fetch('/api/state')
      .then(function (r) { return r.json(); })
      .then(applyState)
      .catch(function (e) { showMsg('Failed to load state: ' + e, true); });
  }

  // ---- slider labels ----
  function updateSliderLabels() {
    ['r','g','b','bright'].forEach(function (id) {
      var el = $('sl-' + id);
      if (!el) return;
      var lbl = el.parentElement.querySelector('span');
      if (lbl) lbl.textContent = el.value;
    });
  }

  ['sl-r','sl-g','sl-b','sl-bright'].forEach(function (id) {
    var el = $(id);
    if (el) el.addEventListener('input', updateSliderLabels);
  });

  // ---- wire up labels inside <label> elements ----
  // Insert value spans after each range input
  document.querySelectorAll('.slider-group input[type=range]').forEach(function (inp) {
    var span = document.createElement('span');
    span.textContent = inp.value;
    inp.parentElement.appendChild(span);
    inp.addEventListener('input', function () { span.textContent = inp.value; });
  });

  // ---- button handlers ----
  $('btn-on').addEventListener('click', function () {
    sendCommand({ command: 'on' });
  });

  $('btn-off').addEventListener('click', function () {
    sendCommand({ command: 'off' });
  });

  $('btn-identify').addEventListener('click', function () {
    sendCommand({ command: 'identify' });
  });

  $('btn-apply-rgb').addEventListener('click', function () {
    sendCommand({
      command: 'setColor',
      color: {
        r: parseInt($('sl-r').value, 10),
        g: parseInt($('sl-g').value, 10),
        b: parseInt($('sl-b').value, 10)
      },
      brightness: parseInt($('sl-bright').value, 10)
    });
  });

  // Scene buttons
  document.querySelectorAll('.scene-grid button[data-scene]').forEach(function (btn) {
    btn.addEventListener('click', function () {
      sendCommand({ command: 'setColorScene', scene: btn.dataset.scene });
    });
  });

  $('refresh').addEventListener('click', loadState);

  $('restart').addEventListener('click', function () {
    if (!confirm('Restart device?')) return;
    fetch('/api/restart', { method: 'POST' })
      .then(function () { showMsg('Restarting…'); })
      .catch(function (e) { showMsg('Error: ' + e, true); });
  });

  $('reset').addEventListener('click', function () {
    if (!confirm('Factory reset — this will erase your config. Continue?')) return;
    fetch('/api/reset', { method: 'POST' })
      .then(function () { showMsg('Factory reset done. Rebooting…'); })
      .catch(function (e) { showMsg('Error: ' + e, true); });
  });

  // Auto-refresh every 10 s.
  loadState();
  setInterval(loadState, 10000);
}());
