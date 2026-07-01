/* app.js — px-wifi-light-esp8266 web UI */
(function () {
  'use strict';

  // ---- helpers ----
  function $(id) { return document.getElementById(id); }
  function setText(id, val) { var el = $(id); if (el) el.textContent = (val != null) ? val : '—'; }

  // ---- white channel state ----
  var s_white = false;
  function updateWhiteBtn() {
    var b = $('btn-white');
    if (b) b.textContent = 'White: ' + (s_white ? 'ON' : 'OFF');
  }

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

    // Light preview — three swatches
    var on  = d.on;
    var br  = (d.brightness != null ? d.brightness : 100) / 100;
    var whiteOn = !!(on && d.white);
    var sr = Math.round((d.r || 0) * br);
    var sg = Math.round((d.g || 0) * br);
    var sb = Math.round((d.b || 0) * br);
    var WHITE_FLOOR = 230;

    var swW = $('swatch-white');
    if (swW) swW.style.background = whiteOn ? '#fff' : '#111';

    var swR = $('swatch-rgb');
    if (swR) swR.style.background = on ? rgbToHex(sr, sg, sb) : '#111';

    var floor = whiteOn ? WHITE_FLOOR : 0;
    var blendR = on ? Math.min(255, floor + sr) : 0;
    var blendG = on ? Math.min(255, floor + sg) : 0;
    var blendB = on ? Math.min(255, floor + sb) : 0;
    var swB = $('swatch-blend');
    if (swB) swB.style.background = rgbToHex(blendR, blendG, blendB);
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
    s_white = !!d.white;
    updateWhiteBtn();
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

  // ---- White toggle ----
  $('btn-white').addEventListener('click', function () {
    sendCommand({ command: 'setWhite', white: !s_white });
  });

  // ---- Settings section ----
  $('settings-toggle').addEventListener('click', function () {
    var body = $('settings-body');
    var chev = $('settings-chevron');
    if (body.classList.contains('hidden')) {
      body.classList.remove('hidden');
      if (chev) chev.textContent = '▼';
      loadCfg();
    } else {
      body.classList.add('hidden');
      if (chev) chev.textContent = '▶';
    }
  });

  function showCfgMsg(text, isError) {
    var el = $('cfg-msg');
    if (!el) return;
    el.textContent = text;
    el.classList.remove('hidden', 'error');
    if (isError) el.classList.add('error');
    if (!isError) setTimeout(function () { el.classList.add('hidden'); }, 4000);
  }

  function updateHostnamePreview() {
    var val = $('cfg-hostname').value.trim().toLowerCase().replace(/\s+/g, '-');
    var prev = $('hostname-preview');
    if (prev) prev.textContent = val || '—';
  }
  $('cfg-hostname').addEventListener('input', updateHostnamePreview);

  function validateHostname(v) {
    if (!v) return 'Hostname cannot be empty';
    if (v.length > 63) return 'Hostname too long (max 63 chars)';
    if (!/^[a-z0-9]([a-z0-9-]*[a-z0-9])?$/.test(v))
      return 'Use only lowercase letters, numbers and hyphens; no leading/trailing hyphens';
    return null;
  }

  function loadCfg() {
    fetch('/api/config')
      .then(function (r) { return r.json(); })
      .then(function (d) {
        var primary = (d.wifi && d.wifi.primary) || {};
        $('cfg-ssid').value      = primary.ssid || '';
        $('cfg-wifi-pass').value = '';
        var hn = (d.device && d.device.prop_name) || '';
        $('cfg-hostname').value  = hn;
        updateHostnamePreview();
        var mqtt = d.mqtt || {};
        $('cfg-mqtt-host').value  = mqtt.host      || '';
        $('cfg-mqtt-port').value  = mqtt.port      || 1883;
        $('cfg-mqtt-user').value  = mqtt.username  || '';
        $('cfg-mqtt-pass').value  = '';
        $('cfg-base-topic').value = mqtt.base_topic || '';
      })
      .catch(function (e) { showCfgMsg('Failed to load: ' + e, true); });
  }

  $('btn-scan').addEventListener('click', function () {
    var btn  = $('btn-scan');
    var list = $('scan-list');
    btn.disabled = true;
    btn.textContent = 'Scanning…';
    list.innerHTML = '';
    list.classList.add('hidden');
    fetch('/api/scan')
      .then(function (r) { return r.json(); })
      .then(function (d) {
        btn.disabled = false;
        btn.textContent = '⟳ Scan';
        var nets = d.networks || [];
        if (!nets.length) {
          list.innerHTML = '<li class="scan-item dim-text">No networks found</li>';
        } else {
          nets.sort(function (a, b) { return b.rssi - a.rssi; });
          nets.forEach(function (n) {
            var strength = n.rssi > -55 ? 'Strong' : n.rssi > -65 ? 'Good' : n.rssi > -75 ? 'Fair' : 'Weak';
            var li = document.createElement('li');
            li.className = 'scan-item';
            li.textContent = n.ssid + '  ' + strength + '  (' + n.rssi + ' dBm)' + (n.secure ? '  [secured]' : '');
            li.addEventListener('click', function () {
              $('cfg-ssid').value = n.ssid;
              list.classList.add('hidden');
            });
            list.appendChild(li);
          });
        }
        list.classList.remove('hidden');
      })
      .catch(function (e) {
        btn.disabled = false;
        btn.textContent = '⟳ Scan';
        showCfgMsg('Scan failed: ' + e, true);
      });
  });

  $('btn-save-cfg').addEventListener('click', function () {
    var hn = $('cfg-hostname').value.trim().toLowerCase().replace(/\s+/g, '-');
    var hnErr = validateHostname(hn);
    if (hnErr) { showCfgMsg(hnErr, true); return; }
    var port = parseInt($('cfg-mqtt-port').value, 10);
    if (isNaN(port) || port < 1 || port > 65535) { showCfgMsg('MQTT port must be 1–65535', true); return; }
    var payload = {
      device: { prop_name: hn },
      wifi:   { primary: { ssid: $('cfg-ssid').value.trim() } },
      mqtt:   {
        host:       $('cfg-mqtt-host').value.trim(),
        port:       port,
        username:   $('cfg-mqtt-user').value.trim(),
        base_topic: $('cfg-base-topic').value.trim()
      }
    };
    var wp = $('cfg-wifi-pass').value;
    if (wp) payload.wifi.primary.password = wp;
    var mp = $('cfg-mqtt-pass').value;
    if (mp) payload.mqtt.password = mp;
    fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d.ok) { showCfgMsg('Save failed: ' + (d.error || '?'), true); return; }
        showCfgMsg(d.reboot_required ? 'Saved — device will reboot.' : 'Settings saved.', false);
      })
      .catch(function (e) { showCfgMsg('Error: ' + e, true); });
  });

  // Auto-refresh every 10 s.
  loadState();
  setInterval(loadState, 10000);
}());
