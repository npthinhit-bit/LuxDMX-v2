#pragma once
#include <Arduino.h>

static const char INDEX_PAGE_JS[] PROGMEM = R"=====(

const dmx = new Uint8Array(512);
let activeCh = 0, sock;
let labels = {};      // labels for the currently-viewed output: { "1": "Front L", ... }
let allLabels = {};   // per-output: { "0": {"1":"Front L"}, "1": {...} }, each line has its own fixtures
let devInfo = {};  // filled from /info.json

// the navbar strip (incl. its value cache) is the shared _nav.html fragment; this page only
// feeds it the /ws frame via LuxNav.stats(...) below.

// Outputs (enabled universes) and which one the monitor is currently viewing.
let outputs = [];     // [{idx, uni}] enabled outputs, from /info.json
let viewOut = 0;      // device-side output index being streamed

// Tell the device which output's DMX buffer to stream + control, then relabel.
function setViewOut(idx){
  viewOut = idx;
  if (sock && sock.readyState === 1) sock.send(JSON.stringify({type:'viewout', out: idx}));
  labels = allLabels[viewOut] || {};   // each output has its own fixture names
  applyLabels();
  markActiveOut();
}
function markActiveOut(){
  document.querySelectorAll('#out-sel button').forEach(function(b){
    var on = +b.dataset.out === viewOut;
    b.classList.toggle('btn-primary', on);
    b.classList.toggle('btn-outline-secondary', !on);
  });
}
function buildOutSelector(){
  var wrap = document.getElementById('out-sel-wrap');
  var grp  = document.getElementById('out-sel');
  if (outputs.length < 2) { wrap.style.display = 'none'; return; }
  grp.innerHTML = outputs.map(function(o){
    return '<button type="button" class="btn btn-outline-secondary" data-out="' + o.idx +
           '">Output ' + String.fromCharCode(65 + o.idx) + ' · U' + o.uni +
           ' · <span class="out-fps">&middot;</span> fps</button>';
  }).join('');
  grp.querySelectorAll('button').forEach(function(b){
    b.addEventListener('click', function(){ setViewOut(+b.dataset.out); });
  });
  markActiveOut();
  wrap.style.display = 'flex';
}

// Device info for the nav subtitle (served statically now; values via JSON)
fetch('/info.json').then(function(r){ return r.json(); }).then(function(d){
  devInfo = d;
  var outs = d.outputs || [{en:true, uni:d.universe||0}];
  outputs = [];
  for (var i = 0; i < outs.length; i++) if (outs[i].en) outputs.push({idx:i, uni:outs[i].uni});
  if (outputs.length) viewOut = outputs[0].idx;
  buildOutSelector();
}).catch(function(){});

// Sparkline: 60 samples at 0.5 s = 30 s of history
const SPARK_LEN = 60;
const spark = new Uint8Array(SPARK_LEN);
let sparkIdx = 0, sparkFull = false, sparkTimer = null;

// Build 512-cell grid
const grid = document.getElementById('grid');
for (let i = 1; i <= 512; i++) {
  const el = document.createElement('div');
  el.className = 'ch';
  el.id = 'ch' + i;
  el.innerHTML = '<div class="gauge" id="g' + i + '"></div>' +
                 '<span class="ch-n">' + i + '</span>' +
                 '<span class="ch-v" id="v' + i + '">0</span>' +
                 '<span class="ch-l" id="l' + i + '"></span>';
  el.onclick = () => openModal(i);
  grid.appendChild(el);
}

// Custom tooltip: a 4x clone of whatever cell is hovered, refreshed live.
const cellTip = document.createElement('div');
cellTip.id = 'cell-tip';
document.body.appendChild(cellTip);
let hoverCh = 0;
function refreshTip() {
  const cell = document.getElementById('ch' + hoverCh);
  if (!cell) return;
  const clone = cell.cloneNode(true);
  clone.querySelectorAll('[id]').forEach((n) => n.removeAttribute('id'));
  clone.removeAttribute('id');
  cellTip.replaceChildren(clone);
  const r = cell.getBoundingClientRect();
  const W = 105, H = 105, gap = 8;
  let left = r.left + r.width / 2 - W / 2;
  let top = r.top - H - gap;
  if (top < 4) top = r.bottom + gap;
  left = Math.max(4, Math.min(left, window.innerWidth - W - 4));
  cellTip.style.left = left + 'px';
  cellTip.style.top = top + 'px';
}
grid.addEventListener('mouseover', (e) => {
  const cell = e.target.closest('.ch');
  if (!cell) return;
  hoverCh = +cell.id.slice(2);
  refreshTip();
  cellTip.style.display = 'block';
});
grid.addEventListener('mouseout', (e) => {
  const cell = e.target.closest('.ch');
  if (cell && !cell.contains(e.relatedTarget)) { hoverCh = 0; cellTip.style.display = 'none'; }
});

// Channel labels
function applyLabels() {
  for (let i = 1; i <= 512; i++) {
    const lbl = labels[i] || '';
    document.getElementById('l' + i).textContent = lbl;
    const el = document.getElementById('ch' + i);
    el.setAttribute('aria-label', lbl ? (i + ': ' + lbl) : ('Channel ' + i));
    el.classList.toggle('labeled', !!lbl);
  }
}
// Labels are stored per output. A legacy flat {ch:name} blob (string values) is
// migrated to {"0":{...}} so existing single-universe labels stay on Output A.
function isNestedLabels(o){
  for (var k in o) return typeof o[k] === 'object' && o[k] !== null;
  return false;
}
function loadLabels() {
  fetch('/labels.json').then(function(r){ return r.json(); })
    .then(function(d){
      d = d || {};
      allLabels = isNestedLabels(d) ? d : { '0': d };
      labels = allLabels[viewOut] || {};
      applyLabels();
    }).catch(function(){});
}
function saveLabel() {
  const v = document.getElementById('ch-label').value.trim();
  if (v) labels[activeCh] = v; else delete labels[activeCh];
  allLabels[viewOut] = labels;
  applyLabels();
  document.getElementById('modal-title').textContent =
    'Channel ' + activeCh + (labels[activeCh] ? ': ' + labels[activeCh] : '');
  fetch('/labels', { method: 'POST', headers: { 'Content-Type': 'application/json' },
                     body: JSON.stringify(allLabels) }).catch(function(){});
}
function identify() { send({type: 'identify', ch: activeCh}); }
loadLabels();

function cellBg(v) {
  if (v === 0) return '';
  const t = v / 255;                 // dark slate -> brand cyan as the level rises
  const r = Math.round(28  + t * (35  - 28));
  const g = Math.round(33  + t * (230 - 33));
  const b = Math.round(40  + t * (247 - 40));
  return 'rgb(' + r + ',' + g + ',' + b + ')';
}

function applyDmx(data) {
  for (let i = 0; i < 512; i++) {
    const v = data[i];
    if (dmx[i] === v) continue;
    dmx[i] = v;
    const el = document.getElementById('ch' + (i + 1));
    el.style.background = cellBg(v);
    document.getElementById('g' + (i + 1)).style.height = (v / 255 * 100) + '%';
    document.getElementById('v' + (i + 1)).textContent = v;
    if (activeCh === i + 1) syncModal(v);
  }
  if (hoverCh) refreshTip();
}

// Modal
function openModal(ch) {
  activeCh = ch;
  spark.fill(0); sparkIdx = 0; sparkFull = false;
  document.getElementById('spark-line').setAttribute('points', '');
  document.getElementById('modal-title').textContent =
    'Channel ' + ch + (labels[ch] ? ': ' + labels[ch] : '');
  document.getElementById('ch-label').value = labels[ch] || '';
  syncModal(dmx[ch - 1]);
  document.getElementById('modal').classList.add('show');
  if (sparkTimer) clearInterval(sparkTimer);
  sparkTimer = setInterval(sampleSpark, 500);
}
function closeModal() {
  document.getElementById('modal').classList.remove('show');
  activeCh = 0;
  if (sparkTimer) { clearInterval(sparkTimer); sparkTimer = null; }
}
function syncModal(v) {
  document.getElementById('ch-slider').value = v;
  document.getElementById('modal-val').textContent = v;
  document.getElementById('modal-pct').textContent = Math.round(v / 255 * 100) + '%';
}
function sliderMove(v) {
  syncModal(v);
  send({type: 'set', ch: activeCh, val: v});
}
function setQuick(v) { sliderMove(v); document.getElementById('ch-slider').value = v; }
function setMode(m)  { send({type: 'mode', manual: m}); }
function sendBlackout() { send({type: 'blackout'}); }
function send(obj) { if (sock && sock.readyState === 1) sock.send(JSON.stringify(obj)); }

// Sparkline
function sampleSpark() {
  if (!activeCh) return;
  spark[sparkIdx] = dmx[activeCh - 1];
  sparkIdx = (sparkIdx + 1) % SPARK_LEN;
  if (sparkIdx === 0) sparkFull = true;
  drawSpark();
}
function drawSpark() {
  var count = sparkFull ? SPARK_LEN : sparkIdx;
  if (count < 2) return;
  var pts = '';
  for (var i = 0; i < count; i++) {
    var si = sparkFull ? (sparkIdx + i) % SPARK_LEN : i;
    var x = (i / (count - 1) * 60).toFixed(2);
    var y = (38 - spark[si] / 255 * 37).toFixed(2);
    pts += (i ? ' ' : '') + x + ',' + y;
  }
  document.getElementById('spark-line').setAttribute('points', pts);
}

// WebSocket
function connect() {
  sock = new WebSocket('ws://' + location.host + '/ws');
  sock.binaryType = 'arraybuffer';
  var badge = document.getElementById('ws-badge');
  sock.onopen  = function() { badge.textContent = 'Live';    badge.style.background = '#45d85c'; badge.style.color = '#06141a';
                              if (viewOut) sock.send(JSON.stringify({type:'viewout', out: viewOut})); };
  sock.onclose = function() { badge.textContent = 'Offline'; badge.style.background = '#f33abc'; badge.style.color = '#06141a'; setTimeout(connect, 2000); };
  sock.onmessage = function(e) {
    if (typeof e.data === 'string') {
      // Text frame: senders + log metadata (replaces HTTP polling)
      try {
        var meta = JSON.parse(e.data);
        if (meta.meta) { updateSenders(meta.senders); updateLog(meta.log); }
      } catch (err) {}
      return;
    }
    if (!(e.data instanceof ArrayBuffer)) return;
    if (window.LuxNav) LuxNav.stats(e.data);   // the shared navbar reads its stats off this frame
    // frame: 16-byte header, dmx(512 * nOut), per-output stats(5 * nOut), 10-byte RDM tail
    var v = new DataView(e.data);
    var srcStatus = v.getUint8(13);   // 0 = normal, 1 = conflict, 2 = merging (drives the banners)

    // Output frame rates label each output-selector button.
    // Frame layout: 16-byte header, DMX(CHANS * nOut), per-output stats(5 * nOut), 10-byte tail.
    // 5 bytes per output: out fps(2), in fps(2), transmit style(1).
    var CHANS = 512;
    var nOut = Math.max(0, Math.floor((v.byteLength - 16 - 10) / (CHANS + 5)));
    var statsOff = 16 + nOut * CHANS;   // start of the per-output stats blocks
    var perOut = [];
    for (var oi = 0; oi < nOut; oi++) perOut[oi] = v.getUint16(statsOff + 2 * oi) / 10;
    document.querySelectorAll('#out-sel button').forEach(function(btn){
      var sp = btn.querySelector('.out-fps'), idx = +btn.dataset.out;
      if (sp && perOut[idx] != null) sp.textContent = perOut[idx].toFixed(1);
    });

    var cb = document.getElementById('conflict-banner');
    var mb = document.getElementById('merge-banner');
    cb.style.display = srcStatus === 1 ? 'flex' : 'none';   // conflict (unmanaged clash)
    mb.style.display = srcStatus === 2 ? 'flex' : 'none';   // merging (intended)

    // Extract the currently-viewed output's 512 DMX channels from the multi-universe DMX block.
    applyDmx(new Uint8Array(e.data, 16 + viewOut * CHANS, CHANS));
  };
}
connect();

// Senders & log polling
function fmtAgo(ago) { return ago === 0 ? 'now' : ago + 's ago'; }

function updateSenders(data) {
  var tbody = document.getElementById('senders-body');
  if (!data || !data.length) {
    tbody.innerHTML = '<tr><td colspan="4" class="text-secondary text-center small py-2">No senders detected</td></tr>';
    return;
  }
  tbody.innerHTML = data.map(function(s) {
    var proto = s.p === 0 ? 'Art-Net' : 'sACN';
    return '<tr>' +
      '<td class="ps-3" style="font-family:monospace">' + s.ip + '</td>' +
      '<td>' + proto + '</td>' +
      '<td>' + s.fps.toFixed(1) + '</td>' +
      '<td>' + fmtAgo(s.ago) + '</td>' +
      '</tr>';
  }).join('');
}

function updateLog(data) {
  var el = document.getElementById('log-body');
  if (!data || !data.length) {
    el.innerHTML = '<div class="text-secondary small text-center py-3">No changes yet</div>';
    return;
  }
  var newest = data[0].ms;
  el.innerHTML = data.map(function(entry) {
    var ageSec  = Math.round((newest - entry.ms) / 1000);
    var timeStr = ageSec === 0 ? 'now' : ageSec + 's ago';
    var proto   = entry.p === 0 ? 'Art-Net' : 'sACN';
    if (entry.u != null && outputs.length > 1) proto += ' U' + entry.u;
    var chs     = entry.ch.map(function(c) {
      return (labels[c[0]] ? labels[c[0]] : 'ch' + c[0]) + '=' + c[1];
    }).join('&nbsp; ');
    return '<div class="px-3 py-2" style="border-bottom:1px solid #30363d">' +
      '<div class="d-flex justify-content-between align-items-baseline">' +
      '<span style="font-size:.72rem;color:#8b949e">' + timeStr + ' &middot; ' + proto + ' &middot; ' + entry.ip + '</span>' +
      '<span class="fw-semibold" style="font-size:.75rem">' + entry.n + ' ch</span>' +
      '</div>' +
      (chs ? '<div style="font-size:.7rem;color:#8b949e;font-family:monospace;margin-top:1px">' + chs + '</div>' : '') +
      '</div>';
  }).join('');
}

// Senders + log now arrive over the WebSocket (see sock.onmessage), no HTTP
// polling, which previously exhausted the device's TCP sockets.

// RDM fixtures moved to their own tab (/rdm).

// Firmware update check
function vNum(s) { var m = String(s).match(/(\d+)\.(\d+)\.(\d+)/); return m ? (+m[1]*1e6 + +m[2]*1e3 + +m[3]) : 0; }
// Lightweight confirm popup (same look as the one on the settings page)
function showModal(opts) {
  opts = opts || {};
  return new Promise(function(resolve) {
    var ov = document.getElementById('app-modal');
    document.getElementById('app-modal-title').textContent = opts.title || 'Confirm';
    document.getElementById('app-modal-body').textContent  = opts.body  || '';
    var ok = document.getElementById('app-modal-ok'), cancel = document.getElementById('app-modal-cancel');
    ok.textContent = opts.okText || 'OK';
    ok.className   = 'btn btn-sm ' + (opts.okClass || 'btn-primary');
    cancel.style.display = (opts.cancel === false) ? 'none' : '';
    ov.classList.add('show');
    function close(val) {
      ov.classList.remove('show');
      ok.onclick = cancel.onclick = ov.onclick = null;
      document.removeEventListener('keydown', onKey);
      resolve(val);
    }
    function onKey(ev) { if (ev.key === 'Escape') close(false); else if (ev.key === 'Enter') close(true); }
    ok.onclick     = function() { close(true); };
    cancel.onclick = function() { close(false); };
    ov.onclick     = function(ev) { if (ev.target === ov) close(false); };
    document.addEventListener('keydown', onKey);
  });
}
function extractChanges(body) {
  if (!body) return [];
  var b = body, cut = b.search(/##?\s*Flash files/i);
  if (cut >= 0) b = b.slice(0, cut);
  return b.split('\n').map(function(l){ return l.trim(); })
    .filter(function(l){ return /^[-•]/.test(l); })
    .map(function(l){ return l.replace(/^[-•]\s*/, ''); });
}
// Update check is driven entirely by the live LuxDMX.org release list so the banner
// always reflects the true newest version (the device only polls once at boot).
fetch('/version.json').then(function(r) { return r.json(); }).then(function(d) {
  var curNum = vNum(d.current);
  fetch('https://luxdmx.org/firmware/releases')
    .then(function(r) { return r.json(); })
    .then(function(list) {
      var rels = (list || [])
        .filter(function(rel){ return /^v\d+\.\d+\.\d+$/.test(rel.tag_name); })
        .sort(function(a,b){ return vNum(b.tag_name) - vNum(a.tag_name); });
      if (!rels.length || vNum(rels[0].tag_name) <= curNum) return;  // up to date
      var newest = rels[0].tag_name;
      // Skip the banner if the user already dismissed this exact version
      try { if (localStorage.getItem('dismissedUpdate') === newest) return; } catch (e) {}
      document.getElementById('update-ver').textContent = newest.replace(/^v/, '');
      var banner = document.getElementById('update-banner');
      banner.style.display = 'block';
      document.getElementById('update-dismiss').onclick = function() {
        try { localStorage.setItem('dismissedUpdate', newest); } catch (e) {}
        banner.style.display = 'none';
      };
      // Update -> confirm popup -> install the newest release straight away
      document.getElementById('update-go').onclick = function() {
        var ver = newest.replace(/^v/, '');
        showModal({
          title: 'Update firmware',
          body: 'Install v' + ver + '? The device will reboot and be offline for about a minute.',
          okText: 'Update & reboot', okClass: 'btn-warning'
        }).then(function(ok) {
          if (!ok) return;
          document.getElementById('ota-version').value = ver;
          document.getElementById('ota-form').submit();
        });
      };
      // Aggregate changes across every release newer than the installed version
      var lines = [];
      rels.filter(function(rel){ return vNum(rel.tag_name) > curNum; }).forEach(function(rel) {
        var ch = extractChanges(rel.body);
        if (ch.length) {
          lines.push(rel.tag_name + ':');
          ch.forEach(function(c){ lines.push('  • ' + c); });
        }
      });
      if (!lines.length) return;
      var pre = document.getElementById('update-notes');
      pre.textContent = lines.join('\n');
      pre.style.display = 'block';
    }).catch(function() {});
}).catch(function() {});

)=====";
