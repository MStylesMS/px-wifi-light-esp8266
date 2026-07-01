/* app.js — px-wifi-light-esp8266 status page */
(function () {
  'use strict';

  function setText(id, val) {
    var el = document.getElementById(id);
    if (el) el.textContent = val != null ? val : '—';
  }

  function showMsg(text, isError) {
    var el = document.getElementById('msg');
    if (!el) return;
    el.textContent = text;
    el.classList.remove('hidden', 'error');
    if (isError) el.classList.add('error');
  }

  function fmtUptime(s) {
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = s % 60;
    return (h ? h + 'h ' : '') + (m ? m + 'm ' : '') + sec + 's';
  }

  function loadStatus() {
    fetch('/api/status')
      .then(function (r) { return r.json(); })
      .then(function (d) {
        setText('prop-name',  d.prop_name);
        setText('fw-version', d.fw_version);
        setText('ap-ip',      d.ap_ip);
        setText('sta-ip',     d.sta_connected ? d.sta_ip : 'not connected');
        setText('sta-ssid',   d.sta_connected ? d.sta_ssid : '—');
        setText('sta-rssi',   d.sta_connected ? d.sta_rssi + ' dBm' : '—');
        setText('mdns',       d.mdns);
        setText('uptime',     fmtUptime(d.uptime_s));
        setText('free-heap',  (d.free_heap / 1024).toFixed(1) + ' KB');
      })
      .catch(function (e) { showMsg('Failed to load status: ' + e, true); });
  }

  document.getElementById('refresh').addEventListener('click', loadStatus);

  document.getElementById('restart').addEventListener('click', function () {
    if (!confirm('Restart device?')) return;
    fetch('/api/restart', { method: 'POST' })
      .then(function () { showMsg('Restarting…'); })
      .catch(function (e) { showMsg('Error: ' + e, true); });
  });

  document.getElementById('reset').addEventListener('click', function () {
    if (!confirm('Factory reset — this will erase your config. Continue?')) return;
    fetch('/api/reset', { method: 'POST' })
      .then(function () { showMsg('Factory reset done. Rebooting…'); })
      .catch(function (e) { showMsg('Error: ' + e, true); });
  });

  loadStatus();
}());
