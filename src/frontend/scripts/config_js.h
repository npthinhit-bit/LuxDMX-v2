#pragma once
#include <Arduino.h>

static const char CONFIG_PAGE_JS[] PROGMEM = R"=====(

var CURRENT = '';
// INFO_BOARD = what the firmware DETECTED (compile-time BOARD_ID); SEL_BOARD = what the user
// PICKED and saved (cfg.boardSel). They differ on a released LuxDMX v6: it runs the generic
// esp32s3dev build, so it detects as "esp32s3-devkitc-1" and only the saved pick knows better.
var INFO_BOARD = '', INFO_MCU = '', SEL_BOARD = '';

// Letter prefix per output index: 0->a, 1->b, 2->c, 3->d
var PFX = ['a','b','c','d'];
function $(id){ return document.getElementById(id); }

// Per-output universe hint (depends on the global protocol + that output's universe)
function updCardHint(card){
  var u = +card.querySelector('.out-uni').value, p = +$('proto-sel').value, parts = [];
  if (p !== 1) parts.push('Art-Net: universe ' + u);
  if (p !== 0) parts.push('sACN: universe ' + (u + 1) + '  →  239.255.0.' + (u + 1));
  card.querySelector('.out-uni-hint').textContent = parts.join('  ·  ');
}
function updAllUniHints(){
  document.querySelectorAll('.out-card').forEach(updCardHint);
}
function syncOutBody(card){
  var on = card.querySelector('.out-en').checked;
  card.querySelector('.out-body').style.opacity = on ? '' : '.45';
}

// Clone the template once per output and bind its fields/handlers
function buildOutputs(outs){
  var tpl = $('out-tpl'), cont = $('outputs-container');
  cont.innerHTML = '';
  for (var i = 0; i < outs.length; i++) wireCard(cont, tpl, i, outs[i] || {});
}
function wireCard(cont, tpl, i, o){
  var node = tpl.content.firstElementChild.cloneNode(true);
  node.setAttribute('data-sec', 'out' + i);   // its own fold state, remembered per output
  node.querySelector('.out-title').textContent = 'DMX Output ' + String.fromCharCode(65 + i);
  var en  = node.querySelector('.out-en');
  var uni = node.querySelector('.out-uni');
  var port= node.querySelector('.out-port');
  var tx  = node.querySelector('.out-tx');
  var rx  = node.querySelector('.out-rx');
  var rts = node.querySelector('.out-rts');
  var merge = node.querySelector('.out-merge');
  var loss = node.querySelector('.out-loss');
  // Letter prefix per output index: 0->a, 1->b, 2->c, 3->d
  en.name  = PFX[i]+'_en';   en.id = PFX[i]+'_en';   en.checked = !!o.en;
  uni.name = PFX[i]+'_uni';  uni.value  = (o.uni  != null ? o.uni  : i);
  port.name= PFX[i]+'_port'; port.value = (o.port != null ? o.port : i + 1);
  tx.name  = PFX[i]+'_tx';   tx.value   = (o.tx   != null ? o.tx   : -1);
  rx.name  = PFX[i]+'_rx';   rx.value   = (o.rx   != null ? o.rx   : -1);
  rts.name = PFX[i]+'_rts';  rts.value  = (o.rts  != null ? o.rts  : -1);
  merge.name = PFX[i]+'_merge'; merge.value = (o.merge != null ? o.merge : 0);
  loss.name = PFX[i]+'_loss'; loss.value = (o.loss != null ? o.loss : 0);
  var style = node.querySelector('.out-style');
  var rate  = node.querySelector('.out-rate');
  style.name = PFX[i]+'_style'; style.value = (o.style != null ? o.style : 0);
  rate.name  = PFX[i]+'_rate';  rate.value  = (o.rate  != null ? o.rate  : 0);
  // Say where the transmit style came from. A console can set it over Art-Net (ArtAddress
  // AcStyleDelta / AcStyleConst), and then the dropdown shows a value the user never picked --
  // which looks like a bug unless we label it.
  markStyleSource(node, o.styleSrc);
  style.addEventListener('change', function(){ markStyleSource(node, 0); });
  en.addEventListener('change', function(){ syncOutBody(node); });
  uni.addEventListener('input', function(){ updCardHint(node); });
  cont.appendChild(node);
  secInit(node);          // the output cards fold like every other section
  updCardHint(node);
  syncOutBody(node);
}
// Badge next to "Transmit style" saying who set it. 1 = a controller pushed it over Art-Net
// (ArtAddress AcStyleDelta / AcStyleConst); 0 = picked here. Without this a mode the console
// changed silently looks like the user's own setting.
function markStyleSource(node, src){
  var lab = node.querySelector('.out-style').closest('.col-sm-6').querySelector('.form-label');
  var old = lab.querySelector('.style-src');
  if (old) old.remove();
  var b = document.createElement('span');
  b.className = 'style-src badge ms-2 ' + (src == 1 ? 'text-bg-info' : 'text-bg-secondary');
  b.textContent = src == 1 ? 'set over Art-Net' : 'set here';
  b.title = src == 1
    ? 'A controller selected this over Art-Net (ArtAddress). Changing it here takes it back.'
    : 'Chosen in this page (or the serial console).';
  lab.appendChild(b);
}
function updStatic(){ $('static-fields').style.display = $('static-sw').checked ? '' : 'none'; }
// Wired-Ethernet capability flags from the firmware build (set in populate()).
var ETH_SPI = false, ETH_RMII = false, HAS_ETH = false;
// RMII PHY family labels (index = cfg.rmiiPhy / the firmware enum order).
var RMII_PHY_LABELS = ['LAN8720 / LAN8742', 'IP101', 'RTL8201', 'DP83848', 'KSZ8081', 'JL1101'];
// Build the single Wired-Ethernet selector from what the build supports: None, W5500
// (if compiled), then each RMII PHY (only on a classic ESP32). One list replaces the
// old enable switch + the separate PHY/RMII dropdowns.
function buildWiredSelect(){
  var sel = $('wired-sel'); if (!sel) return;
  var html = '<option value="none">None (WiFi only)</option>';
  if (ETH_SPI)  html += '<option value="w5500">W5500 (SPI module)</option>'
                      + '<option value="dm9051">DM9051 (SPI module)</option>';
  if (ETH_RMII) for (var i = 0; i < RMII_PHY_LABELS.length; i++)
    html += '<option value="rmii' + i + '">' + RMII_PHY_LABELS[i] + ' (RMII)</option>';
  sel.innerHTML = html;
}
function wiredSel(){ var el = $('wired-sel'); return el ? el.value : 'none'; }
// 0 = W5500/none, 1 = RMII. Used by the pin-conflict overlay (rmiiActive).
function currentPhy(){ return wiredSel().indexOf('rmii') === 0 ? 1 : 0; }
// Drive the card + the hidden firmware fields from the one selector. None = no wired
// (forces WiFi); W5500 or an RMII PHY shows its pins + the "Use wired Ethernet" switch.
function updWired(){
  var v = wiredSel(), rmii = v.indexOf('rmii') === 0, dm9051 = v === 'dm9051',
      spi = v === 'w5500' || dm9051, none = v === 'none';
  $('ethon-sw').checked = spi;                   // SPI module (W5500 or DM9051) selected
  $('wired-phy').value  = rmii ? '1' : '0';      // wiredPhy 0=SPI, 1=RMII
  $('eth-spi-phy').value = dm9051 ? '1' : '0';   // ethSpiPhy 0=W5500, 1=DM9051
  if (rmii) $('rmii-phy').value = v.slice(4);    // rmiiPhy index from "rmiiN"
  $('w5500-pins').style.display = spi ? '' : 'none';
  $('rmii-pins').style.display  = rmii  ? '' : 'none';
  $('net-mode-row').style.display = none ? 'none' : '';   // "Use wired Ethernet" only once a PHY is picked
  if (none) $('useeth-sw').checked = false;               // None forces WiFi
  updNetMode();
}
// Show only the network fields that apply to the chosen interface/mode (issue #14):
// wired Ethernet -> AP-fallback switch; WiFi -> mode selector (+ AP password in AP
// mode). A standalone AP uses a fixed subnet, so static-IP config is hidden there.
function updNetMode(){
  var hasEth = $('net-mode-row').style.display !== 'none';
  var eth = hasEth && $('useeth-sw').checked;
  var ap  = !eth && $('wifi-mode').value === '1';
  var fbAp = eth && $('fb-mode').value === '1';   // the standalone-AP fallback uses the AP password too
  var fbWifi = eth && $('fb-mode').value === '3'; // the "join WiFi" fallback needs the saved STA credentials
  $('wifi-mode-row').style.display = eth ? 'none' : '';
  $('sta-creds-row').style.display = ((!eth && !ap) || fbWifi) ? '' : 'none';   // SSID/password in client mode, or the join-WiFi fallback
  $('ap-pw-row').style.display     = (ap || fbAp) ? '' : 'none';
  $('ap-fb-row').style.display     = eth ? '' : 'none';
  $('static-row').style.display    = ap  ? 'none' : '';
  if (ap) $('static-fields').style.display = 'none';
  else    updStatic();
}
function updLedPin(){
  var t = $('led-type').value;
  $('led-pin-row').style.display = (t === '0' || t === '3') ? 'none' : '';
  $('led5-row').style.display    = (t === '3') ? '' : 'none';
}
function updDispPins(){
  var t = $('disp-type').value;
  $('disp-common').style.display = t === '0' ? 'none' : '';
  $('disp-i2c').style.display = (t === '1' || t === '2' || t === '3') ? '' : 'none';
  $('disp-spi').style.display = (t === '4') ? '' : 'none';
}
function updHostUrl(){ $('dev-host-url').textContent = 'http://' + $('dev-host').value + '.local'; }

// Populate every field from /info.json (page itself is served statically)
fetch('/info.json').then(function(r){ return r.json(); }).then(function(d){
  CURRENT = d.version || '';
  $('cur-ver').textContent = 'v' + CURRENT;
  $('proto-sel').value = d.protocol;
  $('artrdm-sw').checked = d.artnetRdm !== undefined ? !!d.artnetRdm : true;
  $('useeth-sw').checked = !!d.useEthernet;
  $('wifi-mode').value = d.wifiMode !== undefined ? d.wifiMode : 0;
  $('wifi-ssid').value = d.wifiSsid || '';
  $('ap-pw').value = d.apPassword || '';
  $('fb-mode').value = String(d.linkLossMode !== undefined ? d.linkLossMode : (d.apFallback ? 1 : 0));
  $('static-sw').checked = !!d.staticIp;
  $('ipprog-sw').checked = !!d.ipProg;
  $('net-ip').value  = d.sip || '';
  $('net-gw').value  = d.gateway || '';
  $('net-sn').value  = d.subnet || '';
  $('net-dns').value = d.dns || '';
  $('led-type').value = d.ledType;
  $('led-pin').value  = d.ledPin;
  $('led-r').value = d.ledR !== undefined ? d.ledR : -1;
  $('led-g').value = d.ledG !== undefined ? d.ledG : -1;
  $('led-y').value = d.ledY !== undefined ? d.ledY : -1;
  $('led-b').value = d.ledB !== undefined ? d.ledB : -1;
  $('led-w').value = d.ledW !== undefined ? d.ledW : -1;
   buildOutputs(d.outputs || [{en:true,uni:d.universe||0,port:1,tx:17,rx:16,rts:-1},
                              {en:false,uni:1,port:2,tx:-1,rx:-1,rts:-1},
                              {en:false,uni:2,port:0,tx:-1,rx:-1,rts:-1},
                              {en:false,uni:3,port:0,tx:-1,rx:-1,rts:-1}]);
  $('disp-type').value = d.dispType !== undefined ? d.dispType : 0;
  $('disp-sda').value  = d.dispSda  !== undefined ? d.dispSda  : 21;
  $('disp-scl').value  = d.dispScl  !== undefined ? d.dispScl  : 22;
  $('disp-rot').value  = d.dispRot  !== undefined ? d.dispRot  : 0;
  $('disp-cs').value   = d.dispCs   !== undefined ? d.dispCs   : -1;
  $('disp-dc').value   = d.dispDc   !== undefined ? d.dispDc   : -1;
  $('disp-rst').value  = d.dispRst  !== undefined ? d.dispRst  : -1;
  $('disp-sck').value  = d.dispSck  !== undefined ? d.dispSck  : -1;
  $('disp-mosi').value = d.dispMosi !== undefined ? d.dispMosi : -1;
  // On-unit controls (issue #24): rotary encoder + buttons -> display menu
  $('enc-a').value     = d.encA     !== undefined ? d.encA     : -1;
  $('enc-b').value     = d.encB     !== undefined ? d.encB     : -1;
  $('enc-sw').value    = d.encSw    !== undefined ? d.encSw    : -1;
  $('enc-steps').value = d.encSteps !== undefined ? d.encSteps : 4;
  $('enc-rev').checked = !!d.encReverse;
  $('btn1-pin').value  = d.btn1Pin  !== undefined ? d.btn1Pin  : -1;
  $('btn1-act').value  = d.btn1Act  !== undefined ? d.btn1Act  : 3;
  $('btn2-pin').value  = d.btn2Pin  !== undefined ? d.btn2Pin  : -1;
  $('btn2-act').value  = d.btn2Act  !== undefined ? d.btn2Act  : 4;
  $('btn3-pin').value  = d.btn3Pin  !== undefined ? d.btn3Pin  : -1;
  $('btn3-act').value  = d.btn3Act  !== undefined ? d.btn3Act  : 1;
  $('btn4-pin').value  = d.btn4Pin  !== undefined ? d.btn4Pin  : -1;
  $('btn4-act').value  = d.btn4Act  !== undefined ? d.btn4Act  : 2;
  $('btn-ah').checked  = !!d.btnActiveHigh;
  $('ctl-unimax').value = d.ctlUniMax !== undefined ? d.ctlUniMax : 15;
  $('eth-cs').value   = d.ethCs   !== undefined ? d.ethCs   : 5;
  $('eth-sck').value  = d.ethSck  !== undefined ? d.ethSck  : 18;
  $('eth-mosi').value = d.ethMosi !== undefined ? d.ethMosi : 23;
  $('eth-miso').value = d.ethMiso !== undefined ? d.ethMiso : 19;
  $('eth-int').value  = d.ethInt  !== undefined ? d.ethInt  : 4;
  $('eth-rst').value  = d.ethRst  !== undefined ? d.ethRst  : 25;
  $('eth-freq').value = d.ethFreq !== undefined ? d.ethFreq : 20;
  ETH_SPI = !!d.ethSpi; ETH_RMII = !!d.ethRmii; HAS_ETH = !!d.hasEth;
  $('rmii-clk').value  = (d.rmiiClk  !== undefined ? d.rmiiClk  : 0);
  $('rmii-addr').value = (d.rmiiAddr !== undefined ? d.rmiiAddr : 1);
  $('rmii-mdc').value  = (d.rmiiMdc  !== undefined ? d.rmiiMdc  : 23);
  $('rmii-mdio').value = (d.rmiiMdio !== undefined ? d.rmiiMdio : 18);
  $('rmii-pwr').value  = (d.rmiiPwr  !== undefined ? d.rmiiPwr  : 16);
  // One Wired-Ethernet selector: build its options, then pick the current PHY from the
  // saved state (RMII -> rmiiN; else W5500 if the module is enabled; else None).
  buildWiredSelect();
  $('wired-sel').value = (d.wiredPhy === 1) ? ('rmii' + (d.rmiiPhy || 0))
                       : (d.ethW5500 ? (d.ethSpiPhy === 1 ? 'dm9051' : 'w5500') : 'none');
  if (ETH_SPI || ETH_RMII)  $('w5500-card').style.display = '';    // build has a wired PHY -> show the card
  updWired();                                                      // map selection -> hidden fields + visibility
  $('dev-host').value = d.hostname || '';
  $('dev-otapw').value = d.otapw || '';
  $('auto-update-sw').checked = !!d.autoUpdate;
  updNetMode(); updLedPin(); updDispPins(); updHostUrl(); updSummaries();
  $('save-btn').disabled = false;
  INFO_BOARD = d.board || ''; INFO_MCU = d.mcu || ''; SEL_BOARD = d.boardSel || '';
  initBoards();
  loadVersions();
}).catch(function(){
  // Could not read the device's current settings. Render the skeleton so the page isn't blank,
  // but do NOT let it be saved: these are hardcoded placeholders, not this device's config, and
  // saving them would overwrite a perfectly good setup with defaults. Seen for real when the page
  // was opened while the device was still rebooting -- it silently offered "output A on GPIO17,
  // output B off" and a save wrote exactly that.
  buildOutputs([{en:true,uni:0,port:1,tx:17,rx:16,rts:-1},{en:false,uni:1,port:2,tx:-1,rx:-1,rts:-1}]);
  INFO_BOARD=''; INFO_MCU=''; SEL_BOARD=''; initBoards(); loadVersions();
  var sb = $('save-btn');
  sb.disabled = true;
  sb.textContent = 'Cannot read settings';
  sb.title = 'The device did not answer /info.json, so this page is showing placeholders rather '
           + 'than your settings. Reload once it is back; saving now would overwrite your config.';
  showModal({
    title: 'Could not load the current settings',
    body: 'The device did not answer, so this page is showing placeholder values instead of your '
        + 'actual configuration. Saving has been disabled so it cannot overwrite anything. '
        + 'Reload the page once the device is reachable again (it may still be restarting).',
    okText: 'OK', cancel: false
  });
});

// Bootstrap-styled confirm/info dialog (no Bootstrap JS dependency).
// opts: {title, body, okText, okClass, cancel:false}  ->  Promise<bool>
function showModal(opts){
  opts = opts || {};
  return new Promise(function(resolve){
    var ov = $('app-modal');
    $('app-modal-title').textContent = opts.title || 'Confirm';
    $('app-modal-body').textContent  = opts.body  || '';
    var ok = $('app-modal-ok'), cancel = $('app-modal-cancel');
    ok.textContent = opts.okText || 'OK';
    ok.className    = 'btn btn-sm ' + (opts.okClass || 'btn-primary');
    cancel.style.display = (opts.cancel === false) ? 'none' : '';
    ov.classList.add('show');
    function close(val){
      ov.classList.remove('show');
      ok.onclick = cancel.onclick = ov.onclick = null;
      document.removeEventListener('keydown', onKey);
      resolve(val);
    }
    function onKey(ev){ if (ev.key === 'Escape') close(false); else if (ev.key === 'Enter') close(true); }
    ok.onclick     = function(){ close(true); };
    cancel.onclick = function(){ close(false); };
    ov.onclick     = function(ev){ if (ev.target === ov) close(false); };
    document.addEventListener('keydown', onKey);
  });
}

// Guard: never submit an enabled output with no TX pin, it can't drive a line
// and the device refuses it anyway. Catch it here with a clear message.
document.querySelector('form[action="/config"]').addEventListener('submit', function(e){
  var bad = [];
  document.querySelectorAll('.out-card').forEach(function(card, i){
    if (card.querySelector('.out-en').checked && +card.querySelector('.out-tx').value < 0){
      bad.push('Output ' + String.fromCharCode(65 + i));
      secReveal(card);          // unfold it, the missing pin has to be visible
    }
  });
  if (bad.length){
    e.preventDefault();
    showModal({
      title: 'Cannot save settings',
      body: bad.join(' and ') + ': enabled but no TX pin set. Set a TX GPIO (0 or higher), '
        + 'or turn the output off. TX = -1 means "no data line".',
      okText: 'Got it', cancel: false
    });
    return;
  }
  // Submit over fetch so we can act on the device's answer. Most settings apply live now, and
  // the firmware tells us whether this particular save actually needs a restart and which
  // settings forced it -- so we can say why instead of rebooting on the user unannounced.
  e.preventDefault();
  var btn = $('save-btn'), was = btn.textContent;
  btn.disabled = true; btn.textContent = 'Saving...';
  fetch('/config', { method: 'POST', body: new FormData(this) })
    .then(function(r){ return r.json(); })
    .then(function(j){
      if (j.reboot){
        showModal({
          title: 'Restarting to apply this',
          body: 'Saved. Most settings take effect immediately, but these need a restart because '
              + 'they are bound to a pin or a driver that is set up at boot:\n\n' + j.fields
              + '\n\nThe device is restarting now and will be back in a few seconds. '
              + 'If its address changed you will have to reconnect.',
          okText: 'OK', cancel: false
        });
      } else {
        btn.textContent = 'Saved';
        setTimeout(function(){ btn.textContent = was; btn.disabled = false; }, 1500);
      }
    })
    .catch(function(){
      // Older firmware answered with an HTML page and rebooted regardless. Don't leave the
      // button stuck in that case.
      btn.textContent = was; btn.disabled = false;
      showModal({ title: 'Saved', body: 'The device did not report back. It may be restarting.',
                  okText: 'OK', cancel: false });
    });
});

$('proto-sel').addEventListener('change', updAllUniHints);
$('static-sw').addEventListener('change', updStatic);
$('useeth-sw').addEventListener('change', updNetMode);
$('wired-sel').addEventListener('change', function(){ updWired(); validate(); });
// RMII MDC/MDIO/power/clock changes move the reserved pin set, so re-validate.
['rmii-mdc', 'rmii-mdio', 'rmii-pwr', 'rmii-clk'].forEach(function(id){
  $(id).addEventListener(id === 'rmii-clk' ? 'change' : 'input', function(){ validate(); });
});
$('wifi-mode').addEventListener('change', updNetMode);
$('fb-mode').addEventListener('change', updNetMode);
{ var _pu = $('pin-unlock'); if (_pu) _pu.addEventListener('change', function(){ applyHardwiredLocks(); validate(); }); }
$('led-type').addEventListener('change', updLedPin);
$('disp-type').addEventListener('change', updDispPins);
$('dev-host').addEventListener('input', updHostUrl);

// Restart the device (POST /reboot). Confirm first: this drops DMX output for ~10 s, and
// the button is one click away from a live rig. Then wait for the device to answer again
// and reload, so you land on a page showing the state it came back in.
$('reboot-btn').addEventListener('click', function(){
  showModal({
    title: 'Restart the device?',
    body: 'The device restarts and DMX output stops for about ten seconds. Nothing is changed '
        + 'or erased. If its address is assigned by DHCP it will normally come back on the same one.',
    okText: 'Restart', okClass: 'btn-warning'
  }).then(function(ok){
    if (!ok) return;
    var btn = $('reboot-btn');
    btn.disabled = true; btn.textContent = 'Restarting…';
    // The device answers, then reboots ~600 ms later, so a dropped connection here is
    // normal and not worth reporting as a failure.
    fetch('/reboot', { method: 'POST' }).catch(function(){}).then(function(){
      var deadline = Date.now() + 60000;
      (function poll(){
        setTimeout(function(){
          fetch('/info.json?t=' + Date.now(), { cache: 'no-store' })
            .then(function(r){ if (!r.ok) throw 0; location.reload(); })
            .catch(function(){
              if (Date.now() < deadline) return poll();
              btn.disabled = false; btn.textContent = 'Restart device';
              showModal({ title: 'Still not back',
                body: 'The device did not answer within a minute. It may have come back on a '
                    + 'different address, or it needs a power cycle.',
                okText: 'OK', cancel: false });
            });
        }, 1000);
      })();
    });
  });
});

// Auto-update toggle (persisted on device, no reboot)
$('auto-update-sw').addEventListener('change', function(){
  fetch('/autoupdate', { method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body: 'enabled=' + ($('auto-update-sw').checked ? '1' : '0') }).catch(function(){});
});

// Version table from LuxDMX.org
function vNum(s){ var m = String(s).match(/(\d+)\.(\d+)\.(\d+)/); return m ? (+m[1]*1e6 + +m[2]*1e3 + +m[3]) : 0; }
function extractChanges(body){
  if (!body) return [];
  var b = body, cut = b.search(/##?\s*Flash files/i);
  if (cut >= 0) b = b.slice(0, cut);
  return b.split('\n').map(function(l){ return l.trim(); })
    .filter(function(l){ return /^[-•]/.test(l); })
    .map(function(l){ return l.replace(/^[-•]\s*/, ''); });
}
function installVersion(ver){
  showModal({
    title: 'Install firmware',
    body: 'Install v' + ver + '? The device will reboot and be offline for about a minute.',
    okText: 'Install & reboot', okClass: 'btn-warning'
  }).then(function(ok){
    if (!ok) return;
    $('ota-version').value = ver;
    $('ota-form').submit();
  });
}
function loadVersions(){
  fetch('https://luxdmx.org/firmware/releases')
    .then(function(r){ return r.json(); })
    .then(function(list){
      var rows = (list||[])
        .filter(function(rel){ return /^v\d+\.\d+\.\d+$/.test(rel.tag_name); })
        .sort(function(a,b){ return vNum(b.tag_name) - vNum(a.tag_name); });
      var tb = $('ver-rows');
      if (!rows.length){ tb.innerHTML = '<tr><td colspan="4" class="text-secondary text-center small py-2">No releases found</td></tr>'; return; }
      var LIMIT = 5;   // show the newest 5; the rest go behind a "Show more" button
      var html = rows.map(function(rel, i){
        var ver = rel.tag_name.replace(/^v/, '');
        var date = (rel.published_at||'').slice(0,10);
        var changes = extractChanges(rel.body);
        var chHtml = changes.length
          ? '<ul class="mb-0 ps-3">' + changes.slice(0,8).map(function(c){
              return '<li>' + c.replace(/&/g,'&amp;').replace(/</g,'&lt;') + '</li>'; }).join('') + '</ul>'
          : '<span class="text-secondary">&middot;</span>';
        var isCurrent = (ver === CURRENT);
        var badge = (i === 0 ? ' <span class="badge bg-primary">latest</span>' : '')
                  + (isCurrent ? ' <span class="badge bg-success">installed</span>' : '');
        var btn = isCurrent
          ? '<button class="btn btn-outline-secondary btn-sm" disabled>Installed</button>'
          : '<button class="btn btn-outline-primary btn-sm" onclick="installVersion(\'' + ver + '\')">Install</button>';
        var extra = i >= LIMIT ? ' class="ver-extra" style="display:none"' : '';
        return '<tr' + extra + '>' +
          '<td class="text-nowrap">v' + ver + badge + '</td>' +
          '<td class="text-nowrap text-secondary">' + date + '</td>' +
          '<td class="small">' + chHtml + '</td>' +
          '<td class="text-end">' + btn + '</td>' +
          '</tr>';
      }).join('');
      if (rows.length > LIMIT){
        html += '<tr id="ver-more-row"><td colspan="4" class="text-center py-2">' +
          '<button class="btn btn-outline-secondary btn-sm" onclick="showAllVersions()">' +
          'Show ' + (rows.length - LIMIT) + ' older version' + (rows.length - LIMIT === 1 ? '' : 's') +
          '</button></td></tr>';
      }
      tb.innerHTML = html;
    })
    .catch(function(){
      $('ver-rows').innerHTML =
        '<tr><td colspan="4" class="text-secondary text-center small py-2">Could not reach LuxDMX.org</td></tr>';
    });
}
function showAllVersions(){
  var tb = $('ver-rows'); if (!tb) return;
  tb.querySelectorAll('.ver-extra').forEach(function(r){ r.style.display = ''; });
  var more = $('ver-more-row'); if (more) more.remove();
}

/* ===================== Collapsible sections =====================
   Every card on this page folds away from its header. The open/closed state is
   remembered in this browser, a folded header shows a one-line summary of what's
   inside, and a section holding a rejected pin always pops back open so an error
   can never hide behind a fold. Folding only hides fields visually, so a collapsed
   section still submits its values with the form. */
var SEC_KEY = 'lux_cfg_sections';
var SEC_CARET = '<svg viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><path d="M2.5 5.5h11L8 12z"/></svg>';
var secState = (function(){ try { return JSON.parse(localStorage.getItem(SEC_KEY) || '{}'); } catch(e){ return {}; } })();
function secSave(){ try { localStorage.setItem(SEC_KEY, JSON.stringify(secState)); } catch(e){} }
function secCards(){ return Array.prototype.slice.call(document.querySelectorAll('.container .card[data-sec]')); }
function secKey(card){ return card.getAttribute('data-sec'); }
// Everything starts folded: the page opens as a one-screen overview of what is set,
// and you unfold only the section you came for. Once you open (or close) one, that
// choice is what you get next time.
// `_all` is what Collapse/Expand all last did. It has to be remembered separately,
// because the DMX output cards are built later (once /info.json lands) -- without it,
// hitting "Expand all" while the page is still loading would leave the outputs folded.
function secIsOpen(card){
  var k = secKey(card);
  if (secState[k] !== undefined) return !!secState[k];
  return secState._all !== undefined ? !!secState._all : false;
}
function secSet(card, open, quiet){
  card.classList.toggle('sec-closed', !open);
  var head = card.querySelector('.card-header');
  if (head) head.setAttribute('aria-expanded', open ? 'true' : 'false');
  if (!quiet){ secState[secKey(card)] = open ? 1 : 0; secSave(); }
  updSummaries();
}
// "all of them" beats every earlier per-section choice, so drop those and keep just the
// bulk one. That is also what makes it stick for the output cards, which are built later.
function secSetAll(open){
  secState = { _all: open ? 1 : 0 };
  secCards().forEach(function(c){ secSet(c, open, true); });
  secSave();
  updSummaries();
}
function secAnyOpen(){ return secCards().some(function(c){ return !c.classList.contains('sec-closed'); }); }
// Open the section a field lives in (used when validation rejects a pin).
function secReveal(el){
  var card = el && el.closest ? el.closest('.card[data-sec]') : null;
  if (card && card.classList.contains('sec-closed')) secSet(card, true);
  return card;
}
function secInit(card){
  if (card.getAttribute('data-sec-on')) return;
  var head = card.querySelector('.card-header'), body = card.querySelector('.card-body');
  if (!head || !body) return;
  card.setAttribute('data-sec-on', '1');
  head.classList.add('sec-head');
  head.setAttribute('role', 'button');
  head.setAttribute('tabindex', '0');
  var caret = document.createElement('span');
  caret.className = 'sec-caret'; caret.innerHTML = SEC_CARET;
  head.insertBefore(caret, head.firstChild);
  var sum = document.createElement('span');
  sum.className = 'sec-sum';
  head.appendChild(sum);
  function toggle(){ secSet(card, card.classList.contains('sec-closed')); }
  head.addEventListener('click', function(ev){
    // controls that live in a header (the per-output Enabled switch) keep working
    if (ev.target.closest('input,label,select,button,a')) return;
    toggle();
  });
  head.addEventListener('keydown', function(ev){
    if (ev.key === 'Enter' || ev.key === ' '){ ev.preventDefault(); toggle(); }
  });
  secSet(card, secIsOpen(card), true);
}
function secInitAll(){ secCards().forEach(secInit); }

// One-line summary per section, shown only while the section is folded.
function selText(id){ var s = $(id); return (s && s.selectedIndex >= 0) ? s.options[s.selectedIndex].text : ''; }
var PIN_ERR = 0, PIN_WARN = 0;   // pin-validation counts, set by renderWarnings()
function plural(n, w){ return n + ' ' + w + (n === 1 ? '' : 's'); }
var SEC_SUM = {
  board: function(){
    var p = [selText('board-sel')];
    if (PIN_ERR)  p.push(plural(PIN_ERR, 'pin error'));
    if (PIN_WARN) p.push(plural(PIN_WARN, 'warning'));
    return p.filter(Boolean).join(' · ');
  },
  protocol: function(){ return selText('proto-sel') + ($('artrdm-sw').checked ? ' · RDM over Art-Net' : ''); },
  network: function(){
    var eth = $('net-mode-row').style.display !== 'none' && $('useeth-sw').checked;
    var ap  = !eth && $('wifi-mode').value === '1';
    var p = [eth ? 'wired Ethernet' : ap ? 'WiFi access point'
                 : ('WiFi client' + ($('wifi-ssid').value ? ' · ' + $('wifi-ssid').value : ''))];
    if (!ap) p.push($('static-sw').checked ? 'static IP' : 'DHCP');
    return p.join(' · ');
  },
  wired:   function(){ return selText('wired-sel'); },
  led:     function(){ return selText('led-type'); },
  display: function(){ return selText('disp-type'); },
  controls: function(){
    var enc = +$('enc-a').value >= 0 && +$('enc-b').value >= 0;
    var btns = 0;
    for (var i = 1; i <= 4; i++) if (+$('btn' + i + '-pin').value >= 0) btns++;
    if (!enc && !btns) return 'none';
    return [enc ? 'encoder' : '', btns ? plural(btns, 'button') : ''].filter(Boolean).join(' · ');
  },
  'outputs-help': function(){ return 'pins and wiring'; },
  device:  function(){ return ($('dev-host').value || '') + '.local'; },
  firmware:function(){ return CURRENT ? 'installed v' + CURRENT : ''; },
  danger:  function(){ return 'erase WiFi credentials'; },
  out:     function(card){
    if (!card.querySelector('.out-en').checked) return 'off';
    var rts = +card.querySelector('.out-rts').value;
    var style = card.querySelector('.out-style'), rate = card.querySelector('.out-rate');
    // In Delta the rate is only the fall-back while the source is quiet, so name the style
    // instead; in Continuous the fps is the interesting number.
    var clock = style.value === '1' ? 'delta' : rate.options[rate.selectedIndex].text.split(' ')[0] + ' fps';
    return 'universe ' + card.querySelector('.out-uni').value
         + ' · UART' + card.querySelector('.out-port').value
         + ' · TX ' + card.querySelector('.out-tx').value
         + (rts >= 0 ? ' · RDM' : '')
         + ' · ' + clock;
  }
};
function updSummaries(){
  secCards().forEach(function(card){
    var el = card.querySelector('.card-header > .sec-sum'); if (!el) return;
    var k = secKey(card), fn = SEC_SUM[/^out\d+$/.test(k) ? 'out' : k];
    var txt = '';
    if (card.classList.contains('sec-closed') && fn){ try { txt = fn(card) || ''; } catch(e){} }
    el.textContent = txt;
  });
  var all = $('sec-all'); if (all) all.textContent = secAnyOpen() ? 'Collapse all' : 'Expand all';
}
$('sec-all').addEventListener('click', function(){ secSetAll(!secAnyOpen()); });
// Any field edit can change a folded section's summary line.
document.addEventListener('input',  updSummaries);
document.addEventListener('change', updSummaries);
secInitAll();


var BOARDS = {};
var PICK_TARGET = null;
var REMOTE_INDEX = {};
var BUILTINS = ['luxdmx_v6','luxdmx_4uni','esp32s3-devkitc-1','esp32-devkitc','esp32-devkit-v1','xiao-esp32s3'];
var CATALOG_URL = 'https://luxdmx.org/web/boards/';

// Chip families: used when no full board descriptor is selected (Custom).
var FAMILIES = {
  esp32:   { flash:[6,7,8,9,10,11], serial:[1,3], inputOnly:[34,35,36,39], strapping:[0,2,5,12,15], usbjtag:[], max:39 },
  esp32s3: { flash:[26,27,28,29,30,31,32], serial:[43,44], inputOnly:[], strapping:[0,3,45,46], usbjtag:[19,20], max:48 }
};

function esc(s){ return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
function byRole(role){ return document.getElementsByName(role)[0]; }
function setVal(role, v){ var el = byRole(role); if (el) el.value = v; }

// ---- Board descriptors -----------------------------------------------------
function P(gpio, silk, flags){ return { gpio:gpio, silk:silk || ('IO'+gpio), flags:flags || [] }; }
function finalizeDesc(d){
  d.byGpio = {};
  (d.cols || []).forEach(function(col){ col.forEach(function(p){ d.byGpio[p.gpio] = p; }); });
  return d;
}
// ESP32-S3 WROOM-1 castellation order (DevKitC-1 headers / LuxDMX v6 module).
var S3L = [4,5,6,7,15,16,17,18,8,19,20,3,46,9,10,11,12,13,14];
var S3R = [21,47,48,45,0,35,36,37,38,39,40,41,42,44,43,2,1];
function s3silk(g){ return g===43 ? 'TX0' : g===44 ? 'RX0' : 'IO'+g; }
function s3flags(g, eth){
  var f = [];
  if ([0,3,45,46].indexOf(g) >= 0) f.push('strapping');
  if ([43,44].indexOf(g) >= 0) f.push('serial');
  if ([19,20].indexOf(g) >= 0) f.push('usb-jtag');
  if (eth && [9,10,11,12,13,14].indexOf(g) >= 0) f.push('reserved:eth-spi');
  return f;
}
function s3cols(eth){
  return [ S3L.map(function(g){ return P(g, s3silk(g), s3flags(g, eth)); }),
           S3R.map(function(g){ return P(g, s3silk(g), s3flags(g, eth)); }) ];
}
// ESP32 DevKitC (WROOM-32, 38-pin).
var E32L = [36,39,34,35,32,33,25,26,27,14,12,13,9,10,11];
var E32R = [23,22,1,3,21,19,18,5,17,16,4,0,2,15,8,7,6];
function e32silk(g){ return g===36 ? 'VP' : g===39 ? 'VN' : g===1 ? 'TX0' : g===3 ? 'RX0' : 'IO'+g; }
function e32flags(g){
  var f = [];
  if ([6,7,8,9,10,11].indexOf(g) >= 0) f.push('flash');
  if ([1,3].indexOf(g) >= 0) f.push('serial');
  if ([34,35,36,39].indexOf(g) >= 0) f.push('input-only');
  if ([0,2,5,12,15].indexOf(g) >= 0) f.push('strapping');
  return f;
}
function e32cols(){
  return [ E32L.map(function(g){ return P(g, e32silk(g), e32flags(g)); }),
           E32R.map(function(g){ return P(g, e32silk(g), e32flags(g)); }) ];
}
// ESP32 DevKit v1 (DOIT, 30-pin), narrower; flash pins (6-11) NOT broken out.
var E32_30L = [36,39,34,35,32,33,25,26,27,14,12,13];
var E32_30R = [23,22,1,3,21,19,18,5,17,16,4,0,2,15];
function e32cols30(){
  return [ E32_30L.map(function(g){ return P(g, e32silk(g), e32flags(g)); }),
           E32_30R.map(function(g){ return P(g, e32silk(g), e32flags(g)); }) ];
}
// Seeed XIAO ESP32-S3: tiny board, D0..D10 silk labels ([gpio, silk]).
var XIAO_L = [[1,'D0'],[2,'D1'],[3,'D2'],[4,'D3'],[5,'D4'],[6,'D5'],[43,'D6/TX']];
var XIAO_R = [[44,'D7/RX'],[7,'D8'],[8,'D9'],[9,'D10']];
function xiaocols(){
  function m(p){ return P(p[0], p[1], s3flags(p[0], false)); }
  return [ XIAO_L.map(m), XIAO_R.map(m) ];
}
// ---- Curated physical headers (issue #17) ----------------------------------
// Real header order for the offline core boards, so the diagram is wire-by-it:
// every pin in its true row, power/GND/EN included (and non-assignable). Each
// side is a list of [silk, gpioOrNull]; null = a power/GND/EN/NC pin. These
// mirror web/boards/<id>.json exactly (kept here too so they work fully offline).
function phType(silk, gpio){
  var s = String(silk).toUpperCase();
  if (s === '3V3' || s === '5V' || s === 'VIN' || s === 'VBUS' || s === 'VCC') return 'power';
  if (s === 'GND' || s === 'G' || s === '0V') return 'gnd';
  if (s === 'EN' || s === 'RST' || s === 'RESET') return 'en';
  if (s === 'NC') return 'nc';
  return gpio == null ? 'nc' : 'gpio';
}
function phSide(label, rows){
  return rows.map(function(r, i){
    var silk = r[0], gpio = (r.length > 1 ? r[1] : null), t = phType(silk, gpio);
    var e = { pos:i + 1, side:label, silk:silk, type:t };
    if (gpio != null && t === 'gpio') e.gpio = gpio;
    return e;
  });
}
function phys(usb, leftRows, rightRows){ return { usb:usb, pins: phSide('L', leftRows).concat(phSide('R', rightRows)) }; }
// ESP32 DevKitC V4 (WROOM-32, 38-pin). Espressif J2 (left) + J3 (right). Flash silk D0..D3/CMD/CLK.
function physDevKitC(){ return phys('bottom',
  [['3V3'],['EN'],['VP',36],['VN',39],['IO34',34],['IO35',35],['IO32',32],['IO33',33],['IO25',25],['IO26',26],['IO27',27],['IO14',14],['IO12',12],['GND'],['IO13',13],['D2',9],['D3',10],['CMD',11],['5V']],
  [['GND'],['IO23',23],['IO22',22],['TX0',1],['RX0',3],['IO21',21],['GND'],['IO19',19],['IO18',18],['IO5',5],['IO17',17],['IO16',16],['IO4',4],['IO0',0],['IO2',2],['IO15',15],['D1',8],['D0',7],['CLK',6]]); }
// ESP32 DevKit v1 (DOIT, 30-pin). USB at the bottom; flash pins not broken out; Dxx silk.
function physDoit(){ return phys('bottom',
  [['EN'],['VP',36],['VN',39],['D34',34],['D35',35],['D32',32],['D33',33],['D25',25],['D26',26],['D27',27],['D14',14],['D12',12],['D13',13],['GND'],['VIN']],
  [['3V3'],['D15',15],['D2',2],['D4',4],['RX2',16],['TX2',17],['D5',5],['D18',18],['D19',19],['D21',21],['RX0',3],['TX0',1],['D22',22],['D23',23],['GND']]); }
// ESP32-S3-DevKitC-1 v1.1 (44-pin). Espressif J1 (left) + J3 (right); GND silk "G", reset "RST".
function physS3DevKitC(){ return phys('bottom',
  [['3V3'],['3V3'],['RST'],['IO4',4],['IO5',5],['IO6',6],['IO7',7],['IO15',15],['IO16',16],['IO17',17],['IO18',18],['IO8',8],['IO3',3],['IO46',46],['IO9',9],['IO10',10],['IO11',11],['IO12',12],['IO13',13],['IO14',14],['5V'],['GND']],
  [['GND'],['TX0',43],['RX0',44],['IO1',1],['IO2',2],['IO42',42],['IO41',41],['IO40',40],['IO39',39],['IO38',38],['IO37',37],['IO36',36],['IO35',35],['IO0',0],['IO45',45],['IO48',48],['IO47',47],['IO21',21],['IO20',20],['IO19',19],['GND'],['GND']]); }
function buildBoards(){
  BOARDS = {};
  // The LuxDMX board. J4 (display) and J6 (expansion) deliberately share one power layout
  // (1=+3V3, 2=GND) so swapping a cable between them cannot destroy hardware -- enforced on
  // the board itself by hardware/scripts/validate_header_parity.py, see docs/display.md
  // "Header safety". J6 pin 9 is a second ground rather than a 7th GPIO.
  BOARDS['luxdmx_v6'] = finalizeDesc({ id:'luxdmx_v6', name:'LuxDMX v6 (ESP32-S3 + W5500)', mcu:'esp32s3',
    cols:s3cols(true),
    // Everything on this board is fixed in copper except the J4 display header and the
    // J6 expansion header. `field` = the form field it locks; `gpio` is the pin it sits on.
    hardwired:[
      {field:'a_tx', gpio:17, label:'DMX A · TX → DI'},
      {field:'a_rx', gpio:18, label:'DMX A · RX → RO'},
      {field:'a_rts',gpio:8,  label:'DMX A · DE/RE → EN'},
      {field:'a_port',val:1,  label:'DMX A · UART port'},
      {field:'b_tx', gpio:16, label:'DMX B · TX → DI'},
      {field:'b_rx', gpio:21, label:'DMX B · RX → RO'},
      {field:'b_rts',gpio:47, label:'DMX B · DE/RE → EN'},
       {field:'b_port',val:2,  label:'DMX B · UART port'},
       {field:'ethsck', gpio:12, label:'W5500 · SCLK'},
      {field:'ethmosi',gpio:11, label:'W5500 · MOSI'},
      {field:'ethmiso',gpio:13, label:'W5500 · MISO'},
      {field:'ethcs',  gpio:10, label:'W5500 · CS'},
      {field:'ethint', gpio:14, label:'W5500 · INT'},
      {field:'ethrst', gpio:9,  label:'W5500 · RST'},
      {field:'ledtype',val:3,   label:'5-LED status panel'},
      {field:'ledr', gpio:1, label:'LED · Red'},
      {field:'ledg', gpio:2, label:'LED · Green'},
      {field:'ledy', gpio:6, label:'LED · Yellow'},
      {field:'ledb', gpio:7, label:'LED · Blue'},
      {field:'ledw', gpio:15,label:'LED · White'}
      // NB: the display pins (dispsda/scl/sck/mosi/cs/dc/rst) are deliberately NOT locked.
      // They land on the J4 header (see `headers` below), so the user can pick them freely
      // and even wire a display to J6 instead. They default to the J4 wiring via `preset`.
    ],
    headers:[
      {ref:'J4', name:'Display header', pins:[
        {pin:1,silk:'3V3'},{pin:2,silk:'GND'},{pin:3,silk:'SDA',gpio:4},{pin:4,silk:'SCL',gpio:5},
        {pin:5,silk:'SCK',gpio:39},{pin:6,silk:'MOSI',gpio:40},{pin:7,silk:'CS',gpio:41},
        {pin:8,silk:'DC',gpio:42},{pin:9,silk:'RST',gpio:38} ]},
      {ref:'J6', name:'Expansion header', pins:[
        {pin:1,silk:'3V3'},{pin:2,silk:'GND'},{pin:3,silk:'IO35',gpio:35},{pin:4,silk:'IO36',gpio:36},
        {pin:5,silk:'IO37',gpio:37},{pin:6,silk:'IO48',gpio:48},{pin:7,silk:'TX0',gpio:43},
        {pin:8,silk:'RX0',gpio:44},{pin:9,silk:'GND'} ]}
    ],
    preset:{ ledType:3, ledr:1, ledg:2, ledy:6, ledb:7, ledw:15,
             ledbright:{r:255, g:8, y:255, b:255, w:17},
             ethcs:10, ethsck:12, ethmosi:11, ethmiso:13, ethint:14, ethrst:9,
             dispType:1, dispsda:4, dispscl:5,
             dispsck:39, dispmosi:40, dispcs:41, dispdc:42, disprst:38,
             outputs:[{en:true, uni:0, port:1, tx:17, rx:18, rts:8},
                      {en:true, uni:1, port:2, tx:16, rx:21, rts:47}] } });
   BOARDS['luxdmx_4uni'] = finalizeDesc({ id:'luxdmx_4uni', name:'LuxDMX 4-Universe (ESP32-S3 + W5500)', mcu:'esp32s3',
     cols:s3cols(true),
     hardwired:[
       {field:'a_tx', gpio:17, label:'DMX A · TX → DI'},
       {field:'a_rx', gpio:18, label:'DMX A · RX → RO'},
       {field:'a_rts',gpio:8,  label:'DMX A · DE/RE → EN'},
       {field:'a_port',val:1,  label:'DMX A · UART port'},
       {field:'b_tx', gpio:16, label:'DMX B · TX → DI'},
       {field:'b_rx', gpio:15, label:'DMX B · RX → RO'},
       {field:'b_rts',gpio:7,  label:'DMX B · DE/RE → EN'},
       {field:'b_port',val:2,  label:'DMX B · UART port'},
       {field:'c_tx', gpio:5,  label:'DMX C · TX → DI'},
       {field:'d_tx', gpio:6,  label:'DMX D · TX → DI'},
       {field:'ethsck', gpio:12, label:'W5500 · SCLK'},
       {field:'ethmosi',gpio:11, label:'W5500 · MOSI'},
       {field:'ethmiso',gpio:13, label:'W5500 · MISO'},
       {field:'ethcs', gpio:10, label:'W5500 · CS'},
       {field:'ethint', gpio:9,  label:'W5500 · INT'},
       {field:'ethrst', gpio:3,  label:'W5500 · RST'},
       {field:'ledtype',val:3,  label:'5-LED status panel'},
       {field:'ledr', gpio:1, label:'LED · Red'},
       {field:'ledg', gpio:2, label:'LED · Green'},
       {field:'ledy', gpio:6, label:'LED · Yellow'},
       {field:'ledb', gpio:7, label:'LED · Blue'},
       {field:'ledw', gpio:15,label:'LED · White'}
     ],
     preset:{ ledType:3, ledr:1, ledg:2, ledy:6, ledb:7, ledw:15,
              ledbright:{r:255, g:8, y:255, b:255, w:17},
              ethcs:10, ethsck:12, ethmosi:11, ethmiso:13, ethint:9, ethrst:3,
              dispType:1, dispsda:4, dispscl:5,
              dispsck:39, dispmosi:40, dispcs:41, dispdc:42, disprst:38,
              outputs:[
                {en:true, uni:0, port:1, tx:17, rx:18, rts:8, mode:1},
                {en:true, uni:1, port:2, tx:16, rx:15, rts:7, mode:1},
                {en:true, uni:2, port:0, tx:5,  rx:-1, rts:-1, mode:0},
                {en:true, uni:3, port:0, tx:6,  rx:-1, rts:-1, mode:0}
              ] } });
  BOARDS['esp32s3-devkitc-1'] = finalizeDesc({ id:'esp32s3-devkitc-1', name:'ESP32-S3 DevKitC-1', mcu:'esp32s3',
    cols:s3cols(false), phys:physS3DevKitC(),
    preset:{ ledType:2, ledPin:48, dispType:1, dispsda:8, dispscl:9,
             outputs:[{en:true, uni:0, port:1, tx:17, rx:18, rts:-1}] } });
  BOARDS['esp32-devkitc'] = finalizeDesc({ id:'esp32-devkitc', name:'ESP32 DevKitC (WROOM-32, 38-pin)', mcu:'esp32',
    cols:e32cols(), phys:physDevKitC(),
    preset:{ ledType:1, ledPin:2, dispType:1, dispsda:21, dispscl:22,
             outputs:[{en:true, uni:0, port:1, tx:17, rx:16, rts:-1}] } });
  BOARDS['esp32-devkit-v1'] = finalizeDesc({ id:'esp32-devkit-v1', name:'ESP32 DevKit v1 (DOIT, 30-pin)', mcu:'esp32',
    cols:e32cols30(), phys:physDoit(),
    preset:{ ledType:1, ledPin:2, dispType:1, dispsda:21, dispscl:22,
             outputs:[{en:true, uni:0, port:1, tx:17, rx:16, rts:-1}] } });
  BOARDS['xiao-esp32s3'] = finalizeDesc({ id:'xiao-esp32s3', name:'Seeed XIAO ESP32-S3', mcu:'esp32s3',
    cols:xiaocols(),
    preset:{ ledType:0, dispType:1, dispsda:5, dispscl:6,
             outputs:[{en:true, uni:0, port:1, tx:1, rx:2, rts:-1}] } });
}

// ---- Which board / family is in effect -------------------------------------
function currentBoardDesc(){ var id = $('board-sel').value; return (id && id !== 'custom' && BOARDS[id]) ? BOARDS[id] : null; }
function genericForMcu(){
  // Explicit per-family fallback. IMPORTANT: never *guess* a family; the old code
  // returned the S3 board for anything that wasn't exactly 'esp32', so a device that
  // reported an empty/unknown mcu showed an ESP32-S3 board on a plain ESP32.
  if (INFO_MCU === 'esp32')   return BOARDS['esp32-devkitc']     || null;
  if (INFO_MCU === 'esp32s3') return BOARDS['esp32s3-devkitc-1'] || null;
  return null;   // unknown / not reported -> show no board rather than the wrong chip
}
function diagramDesc(){ return currentBoardDesc() || genericForMcu(); }
function currentBoardName(){ var b = diagramDesc(); return b ? b.name : 'Board'; }
function familyOf(){ var b = currentBoardDesc(); var m = (b && b.mcu) || INFO_MCU || 'esp32s3'; return FAMILIES[m] || FAMILIES.esp32s3; }

// ---- Roles (one per configurable GPIO field) -------------------------------
var ROLE_META = {
  ledpin:{l:'Status LED',t:'out'},
  ledr:{l:'LED Red',t:'out'}, ledg:{l:'LED Green',t:'out'}, ledy:{l:'LED Yellow',t:'out'},
  ledb:{l:'LED Blue',t:'out'}, ledw:{l:'LED White',t:'out'},
  dispsda:{l:'Display SDA',t:'out'}, dispscl:{l:'Display SCL',t:'out'},
  dispcs:{l:'Display CS',t:'out'}, dispdc:{l:'Display DC',t:'out'}, disprst:{l:'Display RST',t:'out'},
  dispsck:{l:'Display SCK',t:'out'}, dispmosi:{l:'Display MOSI',t:'out'},
  ethcs:{l:'W5500 Ethernet CS',t:'out'},   ethsck:{l:'W5500 Ethernet SCK',t:'out'},
  ethmosi:{l:'W5500 Ethernet MOSI',t:'out'}, ethmiso:{l:'W5500 Ethernet MISO',t:'in'},
  ethint:{l:'W5500 Ethernet Interrupt',t:'in'}, ethrst:{l:'W5500 Ethernet Reset',t:'out'},
  // On-unit controls (issue #24) — all inputs, so they're happy on input-only pins too
  enca:{l:'Encoder A',t:'in'}, encb:{l:'Encoder B',t:'in'}, encsw:{l:'Encoder push',t:'in'},
  btn1pin:{l:'Button 1',t:'in'}, btn2pin:{l:'Button 2',t:'in'},
  btn3pin:{l:'Button 3',t:'in'}, btn4pin:{l:'Button 4',t:'in'}
};
function roleLabel(role){
  if (ROLE_META[role]) return ROLE_META[role].l;
  var m = role.match(/^o(\d+)_(tx|rx|rts)$/);
  if (m) return 'Output ' + String.fromCharCode(65 + (+m[1])) + ' ' + m[2].toUpperCase();
  return role;
}
function roleType(role){
  if (ROLE_META[role]) return ROLE_META[role].t;
  var m = role.match(/^o\d+_(tx|rx|rts)$/);
  if (m) return m[1] === 'rx' ? 'in' : 'out';
  return 'out';
}
// Roles that are currently relevant given LED type / display type / outputs.
function activeRoles(){
  var r = [], lt = $('led-type').value, dt = $('disp-type').value;
  if (lt === '1' || lt === '2') r.push('ledpin');
  if (lt === '3') r = r.concat(['ledr','ledg','ledy','ledb','ledw']);
  if (dt === '1' || dt === '2' || dt === '3') r = r.concat(['dispsda','dispscl']);
  if (dt === '4') r = r.concat(['dispcs','dispdc','disprst','dispsck','dispmosi']);
   document.querySelectorAll('.out-card').forEach(function(card, i){ if (i < PFX.length) { r.push(PFX[i]+'_tx',PFX[i]+'_rx',PFX[i]+'_rts'); } });
  if ($('ethon-sw') && $('ethon-sw').checked) r = r.concat(['ethcs','ethsck','ethmosi','ethmiso','ethint','ethrst']);
  // On-unit controls: a pin counts as claimed only once it's actually set (>= 0),
  // which the conflict map already filters on, so listing them here is enough.
  r = r.concat(['enca','encb','encsw','btn1pin','btn2pin','btn3pin','btn4pin']);
  return r;
}

// ---- Add a "pick on board" button next to every GPIO field -----------------
var PICK_ICON = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5">'
  + '<circle cx="8" cy="8" r="3"/><path d="M8 1v3M8 12v3M1 8h3M12 8h3"/></svg>';
function decoratePin(input){
  if (!input || input.dataset.picker) return;
  input.dataset.picker = '1';
  var role = input.name;
  var grp = document.createElement('div');
  grp.className = 'input-group input-group-sm pin-grp';
  input.parentNode.insertBefore(grp, input);
  grp.appendChild(input);
  var btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'btn btn-outline-secondary pin-pick';
  btn.title = 'Pick this pin on the board diagram';
  btn.innerHTML = PICK_ICON;
  btn.addEventListener('click', function(){ openPicker(role); });
  grp.appendChild(btn);
  input.addEventListener('input', validate);
  input.addEventListener('change', validate);
}
function decorateAllPins(){
  var names = ['ledpin','ledr','ledg','ledy','ledb','ledw',
               'dispsda','dispscl','dispcs','dispdc','disprst','dispsck','dispmosi',
               'ethcs','ethsck','ethmosi','ethmiso','ethint','ethrst',
               'enca','encb','encsw','btn1pin','btn2pin','btn3pin','btn4pin'];
  document.querySelectorAll('.out-card').forEach(function(card, i){ names.push('o'+i+'_tx','o'+i+'_rx','o'+i+'_rts'); });
  names.forEach(function(n){ decoratePin(byRole(n)); });
  document.querySelectorAll('.out-en').forEach(function(en){
    if (!en.dataset.vbound){ en.dataset.vbound = '1'; en.addEventListener('change', validate); }
  });
  applyHardwiredLocks();   // re-lock the board's fixed fields after any (re)decoration
}

// ---- Validation ------------------------------------------------------------
// ESP32 internal-EMAC RMII GPIOs. The data lines are FIXED by the EMAC hardware
// (TXD0=19 TXD1=22 TX_EN=21 RXD0=25 RXD1=26 RX_DV=27); the MDC/MDIO/power pins and the
// REF_CLK GPIO are configurable in the RMII settings, so the reserved set is dynamic.
var RMII_DATA_PINS = [19, 21, 22, 25, 26, 27];
// RMII reserves its pins only while it is the selected wired PHY (classic ESP32).
function rmiiActive(){ return ETH_RMII && currentPhy() === 1; }
function rmiiReservedPins(){
  var pins = RMII_DATA_PINS.slice();
  ['rmii-mdc', 'rmii-mdio', 'rmii-pwr'].forEach(function(id){
    var el = $(id); if (!el) return;
    var v = parseInt(el.value, 10); if (!isNaN(v) && v >= 0) pins.push(v);
  });
  var clkEl = $('rmii-clk'); var clk = clkEl ? parseInt(clkEl.value, 10) : 0;
  pins.push(clk === 2 ? 16 : clk === 3 ? 17 : 0);   // REF_CLK on GPIO16/17 out, else GPIO0
  return pins;
}
function pinFlags(g){
  if (isNaN(g)) return [];
  var fl;
  var b = currentBoardDesc();
  if (b && b.byGpio){ var p = b.byGpio[g]; fl = p ? (p.flags || []).slice() : ['absent']; }
  else {
    var fam = familyOf(); fl = [];
    if (fam.flash.indexOf(g) >= 0) fl.push('flash');
    if (fam.serial.indexOf(g) >= 0) fl.push('serial');
    if (fam.inputOnly.indexOf(g) >= 0) fl.push('input-only');
    if (fam.strapping.indexOf(g) >= 0) fl.push('strapping');
    if (fam.usbjtag.indexOf(g) >= 0) fl.push('usb-jtag');
    if (g > fam.max || g < 0) fl.push('range');
  }
  // Runtime overlay: while RMII is the selected wired PHY, its fixed GPIOs are
  // reserved, so a DMX / LED / display pin landing on one is flagged as a conflict.
  if (rmiiActive() && rmiiReservedPins().indexOf(g) >= 0 && fl.indexOf('reserved:eth-rmii') < 0)
    fl.push('reserved:eth-rmii');
  return fl;
}
function flagSeverity(flags, t){
  function r(s, m){ return { sev:s, msg:m }; }
  for (var i = 0; i < flags.length; i++){
    var f = flags[i];
    if (f === 'flash')  return r('err', 'is a SPI-flash pin (do not use)');
    if (f === 'serial') return r('err', 'is the USB-serial console pin');
    if (f === 'range')  return r('err', 'is out of range for this chip');
    if (f.indexOf('reserved:') === 0)
      return r('err', 'is reserved for ' + f.slice(9).replace('eth-spi', 'the W5500 Ethernet').replace('eth-rmii', 'the RMII (internal-MAC) Ethernet'));
  }
  if (flags.indexOf('input-only') >= 0 && t === 'out') return r('err', 'is input-only and cannot drive an output');
  if (flags.indexOf('absent') >= 0)    return r('warn', 'is not broken out on this board');
  if (flags.indexOf('strapping') >= 0) return r('warn', 'is a strapping/boot pin (use with care)');
  if (flags.indexOf('usb-jtag') >= 0)  return r('warn', 'is a USB-JTAG pin (D+/D-)');
  return r('', '');
}
function setState(el, cls){ el.classList.remove('is-invalid','is-warning'); if (cls) el.classList.add(cls); }
function renderWarnings(msgs){
  var c = $('pin-warnings'); if (!c) return;
  PIN_ERR = PIN_WARN = 0;
  msgs.forEach(function(m){ if (m.sev === 'err') PIN_ERR++; else PIN_WARN++; });
  updSummaries();                       // the folded header carries the count
  if (!msgs.length){ c.innerHTML = ''; return; }
  var errs = [], warns = [];
  msgs.forEach(function(m){ (m.sev === 'err' ? errs : warns).push(m); });
  var html = '';
  if (errs.length) html += '<div class="alert alert-danger"><strong>Fix before saving:</strong><ul class="mb-0 ps-3">'
    + errs.map(function(m){ return '<li>' + esc(m.text) + '</li>'; }).join('') + '</ul></div>';
  if (warns.length) html += '<div class="alert alert-warning"><strong>Warnings:</strong><ul class="mb-0 ps-3">'
    + warns.map(function(m){ return '<li>' + esc(m.text) + '</li>'; }).join('') + '</ul></div>';
  c.innerHTML = html;
}
function fixedUnlocked(){ var el = $('pin-unlock'); return !!(el && el.checked); }
// An Ethernet role field legitimately owns its own bus pins, so the "reserved for the
// Ethernet" flag must never be raised against the very field that defines that pin
// (GPIO10 is the W5500 CS -> the CS field can't be "reserved for the W5500 Ethernet").
function ownReservedFlag(role){
  if (['ethcs','ethsck','ethmosi','ethmiso','ethint','ethrst'].indexOf(role) >= 0) return 'reserved:eth-spi';
  if (['rmiimdc','rmiimdio','rmiipwr','rmiiclk'].indexOf(role) >= 0) return 'reserved:eth-rmii';
  return null;
}
function validate(){
  var roles = activeRoles(), used = {}, fields = [];
  roles.forEach(function(role){
    var el = byRole(role); if (!el) return;
    var v = parseInt(el.value, 10);
    fields.push({ role:role, el:el, v:v });
    if (!isNaN(v) && v >= 0) (used[v] = used[v] || []).push(role);
  });
  var msgs = [], hard = false;
  fields.forEach(function(f){
    setState(f.el, '');
    if (isNaN(f.v) || f.v < 0) return;
    var probs = [], sev = '';
    if (used[f.v].length > 1){
      var others = used[f.v].filter(function(r){ return r !== f.role; }).map(roleLabel);
      probs.push('GPIO' + f.v + ' also assigned to ' + others.join(', ')); sev = 'err';
    }
    var fl = pinFlags(f.v), own = ownReservedFlag(f.role);
    if (own) fl = fl.filter(function(x){ return x !== own; });   // a bus role field owns its own pins
    var s = flagSeverity(fl, roleType(f.role));
    if (s.sev){
      probs.push('GPIO' + f.v + ' ' + s.msg);
      if (s.sev === 'err') sev = 'err'; else if (sev !== 'err') sev = 'warn';
    }
    if (probs.length){
      setState(f.el, sev === 'err' ? 'is-invalid' : 'is-warning');
      msgs.push({ sev:sev, text:roleLabel(f.role) + ': ' + probs.join('; ') });
      // A rejected pin blocks Save, so never let it hide behind a folded section:
      // pop open both the field's own section and the one holding the warning list.
      if (sev === 'err'){ hard = true; secReveal(f.el); secReveal($('pin-warnings')); }
    }
  });
  renderWarnings(msgs);
  updPinHeaderHints();
  var sb = $('save-btn'); if (sb) sb.disabled = hard;
  if ($('board-modal').classList.contains('show')) renderCurrent();
  return !hard;
}

// ---- SVG board diagram + picker --------------------------------------------
function padClass(flags){
  for (var i = 0; i < flags.length; i++){ var f = flags[i];
    if (f === 'flash' || f === 'serial' || f === 'range' || f.indexOf('reserved:') === 0) return 'bad'; }
  for (i = 0; i < flags.length; i++){ var f2 = flags[i];
    if (f2 === 'strapping' || f2 === 'usb-jtag' || f2 === 'input-only') return 'warn'; }
  return 'free';
}
function statusColor(cls){ return cls === 'bad' ? '#f33abc' : cls === 'warn' ? '#ffaa1c' : '#45d85c'; }
// compact tag shown on the board for an assigned pin (full label is in the tooltip)
function shortRole(role){
  var m = role.match(/^([a-d])_(tx|rx|rts)$/);
  if (m) return String.fromCharCode(65 + PFX.indexOf(m[1])) + ':' + (m[2] === 'rts' ? 'DE' : m[2].toUpperCase());
  var map = { ledpin:'LED', ledr:'LEDr', ledg:'LEDg', ledy:'LEDy', ledb:'LEDb', ledw:'LEDw',
    dispsda:'SDA', dispscl:'SCL', dispcs:'CS', dispdc:'DC', disprst:'RST', dispsck:'SCK', dispmosi:'MOSI' };
  return map[role] || role;
}
// Dispatcher: boards that ship a curated physical header (`phys`) get the faithful
// vertical layout (every pin in its real row, power/GND/EN shown but greyed); every
// other board falls back to the original horizontal two-column diagram.
function renderBoard(){
  var b = diagramDesc(), wrap = $('board-svg-wrap');
  if (!b){ wrap.innerHTML = '<p class="text-secondary small">No diagram for this board. Use the GPIO fields directly.</p>'; return; }
  if (b.phys && b.phys.pins && b.phys.pins.length) renderBoardPhysical(b, wrap);
  else renderBoardCols(b, wrap);
  renderHeaderStrips(b, wrap);   // J4 / J6 connectors, drawn below the board
}
// Horizontal board diagram: the two pin columns become the top and bottom rows
// (pins along the horizon), silk + GPIO labels stand vertically just inside each
// row, and assigned-function callouts stack outward above/below. Geometry is in
// fixed board units at a fixed scale, so a pin is the SAME size on every board.
// gpio -> { ref, pin, silk } from the board's `headers`, so a pad on a user header can
// be tagged with its physical header + pin (e.g. GPIO4 = J4.3 SDA). Only signal pins
// (those with a gpio) are indexed; power/GND pins are skipped.
function headerIndex(b){
  var idx = {};
  ((b && b.headers) || []).forEach(function(h){
    (h.pins || []).forEach(function(pp){ if (pp.gpio != null) idx[pp.gpio] = { ref:h.ref, pin:pp.pin, silk:pp.silk }; });
  });
  return idx;
}
// gpio -> [roles currently assigned to it], from the live form fields.
function assignMap(){
  var assign = {};
  activeRoles().forEach(function(role){ var v = parseInt(byRole(role).value, 10); if (!isNaN(v) && v >= 0) (assign[v] = assign[v] || []).push(role); });
  return assign;
}
// A header pin with no `gpio` is a rail. Classify it by silk so it gets the same
// colours (and the same "not assignable" treatment) as a rail on the board diagram.
function headerPinType(p){
  if (p.gpio != null) return 'gpio';
  var s = String(p.silk || '').toUpperCase();
  if (s === 'GND' || s === 'AGND' || s === 'VSS') return 'gnd';
  if (s === 'NC' || s === '') return 'nc';
  if (s === 'EN' || s === 'RST' || s === 'RESET') return 'en';
  return 'power';
}
// ---- Wirable connectors (J4 display / J6 expansion) ------------------------
// The board diagram answers "which GPIO is that pad?"; a header answers "which
// hole in the plug do I crimp this wire into?". Those are different questions, so
// each header is drawn as its own connector strip: pins left->right in their REAL
// order with pin 1 marked, the rails greyed and inert, and every signal pin a
// click target that assigns straight into the field being picked. Assigned pins
// get the same cyan callout as the board, so the strip doubles as the wiring plan.
function renderHeaderStrips(b, wrap){
  var hs = (b && b.headers) || [];
  if (!hs.length) return;
  var assign = assignMap();
  wrap.insertAdjacentHTML('beforeend',
    '<div class="hdr-strips"><div class="hdr-strips-cap">Wirable connectors. Click a pin to assign it, same as on the board.</div>'
    + hs.map(function(h){ return headerStripSvg(h, assign); }).join('') + '</div>');
  wrap.querySelectorAll('.hdr-strips g.pad').forEach(function(node){
    node.addEventListener('click', function(){ onPinClick(+node.getAttribute('data-gpio')); });
  });
}
function headerStripSvg(h, assign){
  var pins = (h.pins || []).slice().sort(function(a, c){ return a.pin - c.pin; });
  var n = Math.max(pins.length, 1);
  // Board units, scaled once at the end so a header pin is the same size everywhere.
  var PITCH = 32, LM = 24, RM = 18, PAD = 15, PH = PAD / 2, SCALE = 1.5;
  var CAPY = 10, HY0 = 17, NUMY = 30, PADY = 47, SILKY = 71, GPIOY = 80.5, CALLY = 89;
  var HY1 = PADY + PH + 5, CFONT = 8, CADV = CFONT * 0.58;
  var W = LM + (n - 1) * PITCH + RM;
  var xOf = function(i){ return LM + i * PITCH; };
  var maxCall = 0;
  pins.forEach(function(p){ var u = (p.gpio != null) && assign[p.gpio];
    if (u) maxCall = Math.max(maxCall, u.map(calloutLabel).join(', ').length); });
  var H = maxCall ? (CALLY + maxCall * CADV + 5) : (GPIOY + 6);
  var s = '<svg class="board-svg hdr-strip" width="' + Math.round(W * SCALE) + '" viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="xMidYMid meet">';
  s += '<text x="2" y="' + CAPY + '" font-size="9.5" font-weight="600" fill="#c9d1d9">' + esc(h.ref + ' · ' + h.name) + '</text>';
  // connector housing + its keying slot; decorative, never a click target
  s += '<g pointer-events="none">'
    + '<rect x="6" y="' + HY0 + '" width="' + (W - 12) + '" height="' + (HY1 - HY0) + '" rx="3" fill="#1c222a" stroke="#39414b"/>'
    + '<rect x="10" y="' + (HY0 + 3) + '" width="' + (W - 20) + '" height="' + (HY1 - HY0 - 6) + '" rx="2" fill="none" stroke="#2a313a"/>'
    // pin-1 marker: the usual triangle pointing at the first contact
    + '<path d="M' + (LM - 15) + ',' + (PADY - 4) + ' L' + (LM - 15) + ',' + (PADY + 4) + ' L' + (LM - 9) + ',' + PADY + ' Z" fill="#c9d1d9"/>'
    + '</g>';
  pins.forEach(function(p, i){
    var x = xOf(i), type = headerPinType(p), gpio = (p.gpio != null) ? p.gpio : null;
    var power = type !== 'gpio', fl = power ? [] : currentFlags(gpio);
    var u = power ? null : assign[gpio];
    var col = power ? powerColor(type) : (u ? '#23e6f7' : statusColor(padClass(fl)));
    var tip = power
      ? (p.silk + ' · ' + (type === 'gnd' ? 'ground' : type === 'nc' ? 'not connected' : type === 'en' ? 'enable / reset' : 'power rail') + ' (not assignable)')
      : ('GPIO' + gpio + (p.silk ? ' (' + p.silk + ')' : '') + (fl.length ? ' · ' + fl.join(', ') : ' · free')
         + ' · ' + h.ref + ' pin ' + p.pin + (u ? ' · ' + u.map(roleLabel).join(', ') : ''));
    s += '<g class="' + (power ? 'ppin power' : 'pad') + '"' + (power ? '' : ' data-gpio="' + gpio + '"')
      + '><title>' + esc(tip) + '</title>'
      + '<text x="' + x + '" y="' + NUMY + '" text-anchor="middle" font-size="8" font-weight="600" fill="#8b949e">' + p.pin + '</text>'
      + '<rect class="pin-pad" x="' + (x - PH) + '" y="' + (PADY - PH) + '" width="' + PAD + '" height="' + PAD + '" rx="' + (p.pin === 1 ? 0 : 2) + '" fill="' + col + '" fill-opacity="' + (power ? '0.30' : '0.22') + '" stroke="' + col + '" stroke-width="' + (u ? 2.4 : 1.3) + '"/>'
      + '<circle class="pin-dot" cx="' + x + '" cy="' + PADY + '" r="2.2"/>'
      + '<text x="' + x + '" y="' + SILKY + '" text-anchor="middle" font-size="8" fill="' + (power ? '#8b949e' : '#c9d1d9') + '">' + esc(p.silk || '') + '</text>';
    if (gpio != null && p.silk !== ('IO' + gpio))
      s += '<text x="' + x + '" y="' + GPIOY + '" text-anchor="middle" font-size="7.5" fill="#8b949e">G' + gpio + '</text>';
    s += '</g>';
    if (u){
      var ct = u.map(calloutLabel).join(', ');
      s += '<text x="' + x + '" y="' + CALLY + '" text-anchor="start" font-size="' + CFONT + '" font-weight="600" fill="#23e6f7"'
        + ' transform="rotate(90 ' + x + ' ' + CALLY + ')" pointer-events="none">' + esc(ct) + '</text>';
    }
  });
  return s + '</svg>';
}
function renderBoardCols(b, wrap){
  if (!b || !b.cols){ wrap.innerHTML = '<p class="text-secondary small">No diagram for this board. Use the GPIO fields directly.</p>'; return; }
  var assign = assignMap();
  var hIdx = headerIndex(b);
  var topPins = b.cols[0] || [], botPins = b.cols[1] || [];
  var n = Math.max(topPins.length, botPins.length, 1);
  var PITCH = 22, LM = 28, RM = 16, PAD = 13, PH = PAD / 2, SILK = 40, MOD = 42, GAPM = 8, SFONT = 7, SCALE = 1.5;
  var BW = LM + (n - 1) * PITCH + RM;
  var topPadY = SILK + PH + 2, botPadY = topPadY + PH + GAPM + MOD + GAPM + PH, BH = botPadY + PH + 2 + SILK;
  var modY1 = topPadY + PH + GAPM, midY = BH / 2;
  var xOf = function(i){ return LM + i * PITCH; };
  // assigned-function callouts, stacked into rows so neighbours never overlap
  var CFONT = 9, CGAP = 13, estW = function(t){ return t.length * CFONT * 0.55 + 5; };
  var calls = [];
  function addCalls(pins, padY){ for (var i = 0; i < pins.length; i++){ var p = pins[i], u = assign[p.gpio];
    if (u) calls.push({ x: xOf(i), y: padY, gpio: p.gpio, text: u.map(calloutLabel).join(', ') + ' ·G' + p.gpio }); } }
  addCalls(topPins, topPadY); addCalls(botPins, botPadY);
  var TOPM = 6, BOTM = 6;
  ['T','B'].forEach(function(side){
    var list = calls.filter(function(p){ return side === 'T' ? p.y < midY : p.y >= midY; }).sort(function(a,c){ return a.x - c.x; });
    var ends = [];
    list.forEach(function(p){ var w = estW(p.text), L = p.x - w / 2, r = 0; while (r < ends.length && ends[r] > L - 2) r++; ends[r] = p.x + w / 2; p.row = r; });
    var need = ends.length ? ends.length * CGAP + 8 : 6;
    if (side === 'T') TOPM = need; else BOTM = need;
  });
  var DX = 4, DY = TOPM, OW = BW + 8, OH = BH + TOPM + BOTM;
  var s = '<svg class="board-svg" width="' + Math.round(OW * SCALE) + '" viewBox="0 0 ' + OW + ' ' + OH + '" preserveAspectRatio="xMidYMid meet">';
  s += '<g transform="translate(' + DX + ',' + DY + ')">';
  s += '<rect x="0" y="0" width="' + BW + '" height="' + BH + '" rx="9" fill="#0e3b2c" stroke="#1f8a63" stroke-width="1.2"/>';
  s += '<rect x="3" y="3" width="' + (BW - 6) + '" height="' + (BH - 6) + '" rx="7" fill="none" stroke="#0a2a20"/>';
  s += '<rect x="-4" y="' + (midY - 9) + '" width="12" height="18" rx="2" fill="#aab2bd" stroke="#6b7480"/>';   // USB on the left edge
  var mcx = BW / 2;
  s += '<rect x="' + (LM - 6) + '" y="' + modY1 + '" width="' + (BW - LM - RM + 12) + '" height="' + MOD + '" rx="3" fill="#2b3138" stroke="#11151a"/>';
  s += '<text x="' + mcx + '" y="' + (modY1 + MOD / 2 + 4) + '" fill="#9aa6b0" font-size="11" font-weight="600" text-anchor="middle">' + esc(b.name) + '</text>';
  [[8,8],[BW-8,8],[8,BH-8],[BW-8,BH-8]].forEach(function(c){ s += '<circle cx="' + c[0] + '" cy="' + c[1] + '" r="3" fill="#0d1117" stroke="#1f8a63" stroke-width="0.6"/>'; });
  function drawRow(pins, padY, top){
    for (var i = 0; i < pins.length; i++){ var p = pins[i], x = xOf(i), fl = p.flags || [], u = assign[p.gpio], col = u ? '#23e6f7' : statusColor(padClass(fl));
      var hr = hIdx[p.gpio], hrTag = hr ? (hr.ref + '.' + hr.pin) : '';
      var tip = 'GPIO' + p.gpio + (p.silk ? ' (' + p.silk + ')' : '') + (fl.length ? ' · ' + fl.join(', ') : ' · free')
        + (hr ? ' · ' + hr.ref + ' pin ' + hr.pin + (hr.silk ? ' (' + hr.silk + ')' : '') : '')
        + (u ? ' · ' + u.map(roleLabel).join(', ') : '');
      var lbl = 'G' + p.gpio + (p.silk && p.silk !== ('IO' + p.gpio) ? ' ' + p.silk : '') + (hrTag ? ' · ' + hrTag : '');
      var ly = top ? (padY - PH - 2) : (padY + PH + 2), rot = top ? -90 : 90;
      s += '<g class="pad' + (u ? ' used' : '') + '" data-gpio="' + p.gpio + '"><title>' + esc(tip) + '</title>'
        + '<rect class="pin-pad" x="' + (x - PH) + '" y="' + (padY - PH) + '" width="' + PAD + '" height="' + PAD + '" rx="2" fill="' + col + '" fill-opacity="0.22" stroke="' + col + '" stroke-width="' + (u ? 2.4 : 1.3) + '"/>'
        + '<circle class="pin-dot" cx="' + x + '" cy="' + padY + '" r="2.2"/>'
        + '<text x="' + x + '" y="' + ly + '" text-anchor="start" font-size="' + SFONT + '" fill="#c9d1d9" transform="rotate(' + rot + ' ' + x + ' ' + ly + ')">' + esc(lbl) + '</text></g>';
    }
  }
  drawRow(topPins, topPadY, true); drawRow(botPins, botPadY, false);
  s += '</g>';
  calls.forEach(function(p){
    var px = DX + p.x, py = DY + p.y, top = p.y < midY;
    var ly = top ? (DY - 6 - p.row * CGAP) : (DY + BH + 11 + p.row * CGAP), y2 = top ? ly + 1.5 : ly - 4;
    s += '<line x1="' + px + '" y1="' + py + '" x2="' + px + '" y2="' + y2 + '" stroke="#23e6f7" stroke-width="0.7"/>'
      + '<text x="' + px + '" y="' + ly + '" text-anchor="middle" font-size="' + CFONT + '" font-weight="600" fill="#23e6f7">' + esc(p.text) + '</text>';
  });
  s += '</svg>';
  wrap.innerHTML = s;
  wrap.querySelectorAll('.pad').forEach(function(node){ node.addEventListener('click', function(){ onPinClick(+node.getAttribute('data-gpio')); }); });
}
// Power / GND / EN / NC pins are shown so you can wire VCC/GND/EN by the diagram,
// but they are never an assignable signal target.
function isPower(type){ return type === 'power' || type === 'gnd' || type === 'en' || type === 'nc'; }
function powerColor(type){ return type === 'power' ? '#d98c1f' : type === 'gnd' ? '#7d8590' : type === 'en' ? '#a371f7' : '#545d68'; }
// Faithful physical diagram from a board's curated `phys` header. The module is
// drawn upright with its USB connector on the real edge; every pin sits in its
// true row, labelled with the board's own silk (plus the GPIO number where it has
// one). Power/GND/EN pins are greyed and inert; GPIO pins keep the exact same
// status colours, callouts and click-to-assign behaviour as the column diagram.
function renderBoardPhysical(b, wrap){
  var assign = assignMap();
  var hIdx = headerIndex(b);
  var pins = b.phys.pins, usb = b.phys.usb || 'bottom';
  var L = pins.filter(function(p){ return p.side === 'L'; }).sort(function(a,c){ return a.pos - c.pos; });
  var R = pins.filter(function(p){ return p.side === 'R'; }).sort(function(a,c){ return a.pos - c.pos; });
  var n = Math.max(L.length, R.length, 1);
  // Geometry (board units; scaled up once for the final SVG). Rows run top->bottom.
  // The pads sit on the two outer header edges; the module body is drawn strictly
  // BETWEEN the columns so it never overlaps (or intercepts clicks on) a pad.
  var PITCH = 19, TM = 18, PAD = 13, PH = PAD / 2, LBL = 50, CALL = 92, GUT = 8, SFONT = 7.5, SCALE = 1.5;
  var leftPadX = CALL + LBL, bodyX = leftPadX + PH + GUT, bodyW = 60;
  var rightPadX = bodyX + bodyW + GUT + PH;
  var BW = rightPadX + LBL + CALL;
  var BH = TM + (n - 1) * PITCH + TM;
  var bodyCx = bodyX + bodyW / 2;
  var usbBottom = usb !== 'top';
  var s = '<svg class="board-svg" width="' + Math.round(BW * SCALE) + '" viewBox="0 0 ' + BW + ' ' + (BH + 26) + '" preserveAspectRatio="xMidYMid meet">';
  s += '<g transform="translate(0,4)">';
  // module body + RF shield + USB connector are decorative only (no pointer events,
  // so they can never steal a click from a pad even if anti-aliasing overlaps).
  s += '<g class="board-body" pointer-events="none">';
  s += '<rect x="' + bodyX + '" y="' + (TM - 12) + '" width="' + bodyW + '" height="' + (BH - 2 * TM + 24) + '" rx="6" fill="#2b3138" stroke="#11151a"/>';
  var shieldY = usbBottom ? (TM - 8) : (BH - TM - 26);
  s += '<rect x="' + (bodyX + 4) + '" y="' + shieldY + '" width="' + (bodyW - 8) + '" height="32" rx="3" fill="#3a4048" stroke="#11151a" opacity="0.7"/>';
  var usbY = usbBottom ? (BH - 14) : (TM - 18), uw = 22;
  s += '<rect x="' + (bodyCx - uw / 2) + '" y="' + usbY + '" width="' + uw + '" height="15" rx="2" fill="#aab2bd" stroke="#6b7480"/>';
  s += '<text x="' + bodyCx + '" y="' + (BH / 2) + '" fill="#9aa6b0" font-size="8.5" font-weight="600" text-anchor="middle" transform="rotate(-90 ' + bodyCx + ' ' + (BH / 2) + ')">' + esc(b.name) + '</text>';
  s += '</g>';
  function rowY(i){ return TM + i * PITCH; }
  function drawSide(list, padX, isLeft){
    for (var i = 0; i < list.length; i++){
      var p = list[i], y = rowY(i), gpio = (p.gpio != null) ? p.gpio : null;
      var power = isPower(p.type), u = (gpio != null) ? assign[gpio] : null;
      var col = power ? powerColor(p.type) : (u ? '#23e6f7' : statusColor(padClass(currentFlags(gpio))));
      var fl = (gpio != null) ? currentFlags(gpio) : [];
      var hr = (gpio != null) ? hIdx[gpio] : null;
      var tip = power
        ? p.silk + (p.type === 'power' ? ' — power rail (not assignable)' : p.type === 'gnd' ? ' — ground (not assignable)' : p.type === 'en' ? ' — enable / reset (not assignable)' : ' — not connected')
        : ('GPIO' + gpio + (p.silk ? ' (' + p.silk + ')' : '') + (fl.length ? ' — ' + fl.join(', ') : ' — free')
           + (hr ? ' — ' + hr.ref + ' pin ' + hr.pin + (hr.silk ? ' (' + hr.silk + ')' : '') : '')
           + (u ? ' — ' + u.map(roleLabel).join(', ') : ''));
      // label text: silk, plus the GPIO number when the silk hides it, plus the header
      // pin it reaches (a board can have both a module header and a wirable connector)
      var lbl = p.silk + (gpio != null && p.silk !== ('IO' + gpio) && p.silk !== ('GPIO' + gpio) ? ' ·G' + gpio : '')
        + (hr ? ' · ' + hr.ref + '.' + hr.pin : '');
      var lblX = isLeft ? (padX - PH - 4) : (padX + PH + 4), anchor = isLeft ? 'end' : 'start';
      var cls = power ? 'ppin power' : 'pad';
      s += '<g class="' + cls + '"' + (power ? '' : ' data-gpio="' + gpio + '"') + '><title>' + esc(tip) + '</title>'
        + '<rect class="pin-pad" x="' + (padX - PH) + '" y="' + (y - PH) + '" width="' + PAD + '" height="' + PAD + '" rx="2" fill="' + col + '" fill-opacity="' + (power ? '0.30' : '0.22') + '" stroke="' + col + '" stroke-width="' + (u ? 2.4 : 1.3) + '"/>'
        + '<circle class="pin-dot" cx="' + padX + '" cy="' + y + '" r="2.1"/>'
        + '<text x="' + lblX + '" y="' + (y + 2.6) + '" text-anchor="' + anchor + '" font-size="' + SFONT + '" fill="' + (power ? '#8b949e' : '#c9d1d9') + '">' + esc(lbl) + '</text></g>';
      if (u){
        var cx = isLeft ? (padX - PH - LBL) : (padX + PH + LBL), ctext = u.map(calloutLabel).join(', ');
        s += '<line x1="' + lblX + '" y1="' + y + '" x2="' + cx + '" y2="' + y + '" stroke="#23e6f7" stroke-width="0.6" stroke-dasharray="2 2"/>'
          + '<text x="' + cx + '" y="' + (y + 2.6) + '" text-anchor="' + (isLeft ? 'end' : 'start') + '" font-size="8" font-weight="600" fill="#23e6f7">' + esc(ctext) + '</text>';
      }
    }
  }
  drawSide(L, leftPadX, true); drawSide(R, rightPadX, false);
  s += '</g></svg>';
  wrap.innerHTML = s;
  wrap.querySelectorAll('.pad').forEach(function(node){ node.addEventListener('click', function(){ onPinClick(+node.getAttribute('data-gpio')); }); });
}
// Flags for a GPIO from the active board descriptor (same source the validator uses).
function currentFlags(gpio){ if (gpio == null) return []; return pinFlags(gpio); }
function onPinClick(gpio){
  if (PICK_TARGET){
    var el = byRole(PICK_TARGET);
    if (el){ el.value = gpio; el.dispatchEvent(new Event('input', { bubbles:true })); }
    closeBoard();
  } else {
    chooseRole(gpio);
  }
}
function chooseRole(gpio){
  var roles = activeRoles(), h = $('board-pick-hint');
  h.classList.remove('d-none');
  h.innerHTML = 'Assign <strong>GPIO' + gpio + '</strong> to: '
    + roles.map(function(r){ return '<button type="button" class="btn btn-sm btn-outline-primary me-1 mb-1" data-r="' + r + '">' + esc(roleLabel(r)) + '</button>'; }).join('')
    + ' <button type="button" class="btn btn-sm btn-outline-secondary mb-1" data-r="">cancel</button>';
  h.querySelectorAll('button').forEach(function(btn){
    btn.addEventListener('click', function(){
      var r = btn.getAttribute('data-r');
      if (r){ var el = byRole(r); el.value = gpio; el.dispatchEvent(new Event('input', { bubbles:true })); }
      h.classList.add('d-none'); renderCurrent();
    });
  });
}
// fuller function label for the on-diagram callouts (e.g. "Status LED (WS2812)")
function calloutLabel(role){
  if (role === 'ledpin'){ var lt = $('led-type').value; return 'Status LED' + (lt === '2' ? ' (WS2812)' : lt === '1' ? ' (GPIO)' : ''); }
  return roleLabel(role);
}
function printBoard(){
  var c = $('board-svg-wrap').innerHTML;
  if (!c){ return; }
  var w = window.open('', '_blank', 'width=900,height=700'); if (!w) return;
  w.document.write('<!doctype html><html><head><title>' + esc(currentBoardName()) + ' · pin plan</title>'
    + '<style>body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;margin:0;padding:24px}'
    + 'h3{margin:0 0 14px;font-weight:600}svg{width:100%;max-width:780px;height:auto;display:block;margin:0 auto}'
    + '.hdr-strips{margin-top:18px}svg.hdr-strip{max-width:520px;margin-top:10px}</style></head>'
    + '<body><h3>' + esc(currentBoardName()) + ' · pin assignment</h3>' + c + '</body></html>');
  w.document.close(); w.focus(); setTimeout(function(){ try { w.print(); } catch(e){} }, 450);
}
function renderCurrent(){ renderBoard(); }
// Mirror the main board <select> into the modal so the board can be switched while
// picking pins; changes delegate to the real select (which handles catalog fetch).
function syncModalBoardSel(){
  var src = $('board-sel'), dst = $('board-sel-modal');
  if (!dst) return;
  dst.innerHTML = src.innerHTML;
  dst.value = src.value;
}
function openPicker(role){
  PICK_TARGET = role || null;
  var h = $('board-pick-hint');
  if (role){ h.classList.remove('d-none'); h.textContent = 'Click the pin to assign to ' + roleLabel(role) + '.'; }
  else { h.classList.add('d-none'); }
  $('board-modal-title').textContent = role ? ('Pick pin: ' + roleLabel(role)) : (currentBoardName() + ' · pin map');
  syncModalBoardSel();
  renderCurrent();
  $('board-modal').classList.add('show');
}
function closeBoard(){ $('board-modal').classList.remove('show'); PICK_TARGET = null; }

// ---- Templates -------------------------------------------------------------
function applyTemplate(){
  var b = BOARDS[$('board-sel').value];
  if (!b || !b.preset){ showModal({ title:'No template', body:'This board has no preset to apply.', cancel:false, okText:'OK' }); return; }
  var hasEth = ['ethcs','ethsck','ethmosi','ethmiso','ethint','ethrst'].some(function(k){ return b.preset[k] != null; });
  showModal({ title:'Apply ' + b.name + ' template?',
    body:'This overwrites the LED, display, DMX'
       + (hasEth ? ' and W5500 Ethernet' : '')
       + ' pin fields with the tested pin map for ' + b.name
       + (b.preset.ledbright ? ', and sets the 5-LED panel brightness' : '')
       + '. Your IP config, protocol and device settings are left unchanged.'
       + (hasEth ? ' Enable "Use wired Ethernet" yourself if you want to use the W5500.' : ''),
    okText:'Apply', okClass:'btn-primary' }).then(function(ok){
    if (!ok) return;
    var p = b.preset;
    if (p.ledType != null){ $('led-type').value = p.ledType; updLedPin(); }
    if (p.ledPin != null) setVal('ledpin', p.ledPin);
    ['ledr','ledg','ledy','ledb','ledw'].forEach(function(k){ if (p[k] != null) setVal(k, p[k]); });
    // W5500 pins are board wiring (which GPIOs the chip is soldered to), not network config —
    // so a template applies them, but leaves IP/gateway/mode alone. The user still opts into
    // wired Ethernet with the "Use wired Ethernet" switch.
    ['ethcs','ethsck','ethmosi','ethmiso','ethint','ethrst'].forEach(function(k){ if (p[k] != null) setVal(k, p[k]); });
    if (p.dispType != null){ $('disp-type').value = p.dispType; updDispPins(); }
    ['dispsda','dispscl','dispcs','dispdc','disprst','dispsck','dispmosi'].forEach(function(k){ if (p[k] != null) setVal(k, p[k]); });
    if (p.outputs){ buildOutputs(p.outputs); decorateAllPins(); }
    // Panel brightness isn't a config-form field (it's tuned live via /led/bright), so the tested
    // per-colour duty can't ride along on Save — push it straight to the endpoint, persisted.
    if (p.ledbright){
      var q = Object.keys(p.ledbright).map(function(k){ return k + '=' + p.ledbright[k]; }).join('&');
      fetch('/led/bright?' + q + '&save=1').catch(function(){});
    }
    updAllUniHints(); validate();
  });
}
function updBoardButtons(){ var b = BOARDS[$('board-sel').value]; $('board-apply').disabled = !(b && b.preset); updHardwired(); }
// The detected hardware (what the firmware reports), as opposed to whatever the board
// <select> is pointed at for browsing. Locks follow the real board, not the dropdown.
function detectedDesc(){ return (INFO_BOARD && BOARDS[INFO_BOARD]) || null; }
// Board whose copper-pin locks apply: the one the USER selected if any, else the one the
// firmware reported. So picking "LuxDMX v6" locks its W5500 / DMX / LED pins even on the
// generic esp32s3dev build (which reports esp32s3-devkitc-1); the Advanced unlock is the
// escape hatch for a reworked board.
function lockDesc(){ return currentBoardDesc() || detectedDesc(); }

// On a board that wires its pins in copper (LuxDMX v6), lock the matching form fields so
// they can't be changed: set the fixed value, disable the input, hide its pin-pick button,
// and drop in a hidden mirror so the value still POSTs (disabled inputs don't submit).
// Idempotent — safe to re-run after the output cards or LED/display fields rebuild.
function applyHardwiredLocks(){
  document.querySelectorAll('.fixed-field').forEach(function(el){ el.classList.remove('fixed-field'); el.disabled = false; });
  document.querySelectorAll('input.fixed-mirror').forEach(function(el){ el.remove(); });
  // ...and give every pick button back. Locking hides them; without this they stayed hidden
  // after switching to another board or ticking the Advanced unlock, so the field went
  // editable but you could no longer pick its pin off the diagram until a reload.
  document.querySelectorAll('.pin-grp .pin-pick').forEach(function(el){ el.style.display = ''; });
  var b = lockDesc(); var hw = b && b.hardwired; if (!hw) return;
  if (fixedUnlocked()) return;   // Advanced unlock: leave the fixed pins editable for a reworked board
  hw.forEach(function(e){
    if (!e.field) return;
    var val = (e.val != null ? e.val : e.gpio);
    var inp = byRole(e.field); if (!inp) return;
    inp.value = String(val);
    inp.disabled = true; inp.classList.add('fixed-field');
    inp.title = 'Fixed on this board' + (e.label ? ': ' + e.label : '');
    var m = document.createElement('input');
    m.type = 'hidden'; m.name = e.field; m.value = val; m.className = 'fixed-mirror';
    inp.parentNode.appendChild(m);
    var grp = inp.closest('.pin-grp'); if (grp){ var pk = grp.querySelector('.pin-pick'); if (pk) pk.style.display = 'none'; }
  });
}

// Tag each GPIO field with the physical header pin that GPIO comes out on ("J4.3"),
// so the number in the box tells you which hole to crimp without reopening the diagram.
// Follows the same board as the locks, and clears itself when the pin isn't on a header.
function updPinHeaderHints(){
  var idx = headerIndex(lockDesc());
  document.querySelectorAll('.pin-grp').forEach(function(grp){
    var inp = grp.querySelector('input[name]'); if (!inp) return;
    var v = parseInt(inp.value, 10);
    var hr = (!isNaN(v) && v >= 0) ? idx[v] : null;
    var tag = grp.querySelector('.hdr-hint');
    if (!hr){ if (tag) tag.remove(); return; }
    if (!tag){ tag = document.createElement('span'); tag.className = 'input-group-text hdr-hint'; grp.appendChild(tag); }
    tag.textContent = hr.ref + '.' + hr.pin;
    tag.title = hr.ref + ' pin ' + hr.pin + (hr.silk ? ' (' + hr.silk + ')' : '');
  });
}

// Show the detected board's fixed wiring + its wirable headers (J4/J6 on the LuxDMX board).
function renderHeaderTable(h){
  var rows = h.pins.map(function(p){
    var lab = (p.gpio != null) ? (esc(p.silk) + ' <span class="g">(' + p.gpio + ')</span>')
                               : ('<span class="pw">' + esc(p.silk) + '</span>');
    return '<tr><td class="pn">' + p.pin + '</td><td>' + lab + '</td></tr>';
  }).join('');
  return '<table class="hdr-tbl"><caption>' + esc(h.ref) + ' · ' + esc(h.name) + '</caption>' + rows + '</table>';
}
function updHardwired(){
  var el = $('board-hardwired'); if (!el) return;
  var b = lockDesc();
  var ur = $('pin-unlock-row');
  if (ur) ur.style.display = (b && b.hardwired && b.hardwired.length) ? '' : 'none';
  if (!b || (!b.hardwired && !b.headers)){ el.innerHTML = ''; return; }
  var html = '';
  if (b.hardwired && b.hardwired.length){
    html += '<div class="hw-fixed"><strong>Fixed wiring on ' + esc(b.name) + '</strong>'
          + ' — these are set in copper and can\'t be changed here:<div>'
          + b.hardwired.map(function(h){
              var pin = (h.gpio != null) ? ' <span class="g">IO' + h.gpio + '</span>' : '';
              return '<span class="hwb">' + esc(h.label || h.field) + pin + '</span>';
            }).join('') + '</div></div>';
  }
  if (b.headers && b.headers.length){
    html += '<div class="hw-headers"><strong>Wirable headers</strong> — the pins you can actually connect to:<div>'
          + b.headers.map(renderHeaderTable).join('') + '</div></div>';
  }
  el.innerHTML = html;
}

// ---- Board <select> + remote catalog (Phase 2) -----------------------------
function appendBoardOption(id, name){
  if (document.querySelector('#board-sel option[value="' + id + '"]')) return;
  var o = document.createElement('option'); o.value = id; o.textContent = name; $('board-sel').appendChild(o);
}
// Only offer boards that match the detected chip family; a classic-ESP32 pin map
// can't run on an ESP32-S3 and vice-versa. Unknown mcu on either side => show it.
function mcuMatch(m){ return !INFO_MCU || !m || m === INFO_MCU; }
function fillBoardSelect(){
  var html = '<option value="custom">Custom / manual</option>';
  BUILTINS.forEach(function(id){ if (BOARDS[id] && mcuMatch(BOARDS[id].mcu)) html += '<option value="' + id + '">' + esc(BOARDS[id].name) + '</option>'; });
  $('board-sel').innerHTML = html;
}
function ensureBoard(id, cb){
  // Network is authoritative when reachable; the localStorage copy (already in BOARDS[id])
  // is only an offline fallback, so always try a fresh fetch and overwrite the cache.
  fetch(CATALOG_URL + id + '.json', { cache:'no-store' })
    .then(function(r){ if (!r.ok) throw 0; return r.json(); })
    .then(function(d){
      BOARDS[id] = finalizeDesc(d);
      try { var c = JSON.parse(localStorage.getItem('lg_boards') || '{}'); c[id] = d; localStorage.setItem('lg_boards', JSON.stringify(c)); } catch(e){}
      cb(true);
    }).catch(function(){ cb(!!BOARDS[id]); });
}
function loadRemoteCatalog(){
  try {
    var cache = JSON.parse(localStorage.getItem('lg_boards') || '{}');
    Object.keys(cache).forEach(function(k){ if (!BOARDS[k] && mcuMatch(cache[k].mcu)){ BOARDS[k] = finalizeDesc(cache[k]); appendBoardOption(k, cache[k].name); } });
  } catch(e){}
  fetch(CATALOG_URL + 'index.json', { cache:'no-store' })
    .then(function(r){ if (!r.ok) throw 0; return r.json(); })
    .then(function(idx){
      (idx.boards || []).forEach(function(e){
        if (BUILTINS.indexOf(e.id) >= 0) return;
        if (!mcuMatch(e.mcu)) return;   // hide boards for a different chip family
        REMOTE_INDEX[e.id] = e;
        appendBoardOption(e.id, e.name);
      });
    }).catch(function(){ /* offline: built-ins still work */ });
}

// ---- Init (called from the /info.json loader) ------------------------------
// Point the board <select> at `id`, falling back to `alt` when that board isn't on offer.
// A saved catalog board has no <option> until its descriptor is fetched, so sit on the
// fallback and upgrade once it lands (offline: the fallback just stays).
function setBoardSel(id, alt){
  var sel = $('board-sel');
  id = id || alt || 'custom';
  if (BOARDS[id] && mcuMatch(BOARDS[id].mcu)) appendBoardOption(id, BOARDS[id].name);
  sel.value = id;
  if (sel.value === id) return;
  sel.value = alt || 'custom';
  if (id === 'custom' || BUILTINS.indexOf(id) >= 0) return;
  ensureBoard(id, function(ok){
    if (!ok || !mcuMatch(BOARDS[id].mcu)) return;
    appendBoardOption(id, BOARDS[id].name);
    sel.value = id;
    updBoardButtons(); applyHardwiredLocks(); validate();
  });
}
function initBoards(){
  buildBoards();
  fillBoardSelect();
  // The SAVED pick wins over detection. The compile-time board id is only the fallback for a
  // device that has never had a board picked, because it can't tell a v6 from a bare DevKitC.
  setBoardSel(SEL_BOARD || (BOARDS[INFO_BOARD] ? INFO_BOARD : 'custom'),
              BOARDS[INFO_BOARD] ? INFO_BOARD : 'custom');
  $('board-detected').textContent = INFO_BOARD ? ('detected: ' + INFO_BOARD + (INFO_MCU ? (' / ' + INFO_MCU) : '')) : '';
  decorateAllPins();
  // re-render the open picker (and re-sync the modal select) after a board change
  var afterBoardChange = function(){ updBoardButtons(); applyHardwiredLocks(); validate();
    if ($('board-modal').classList.contains('show')){ syncModalBoardSel(); renderCurrent(); } };
  $('board-sel').addEventListener('change', function(){
    var id = $('board-sel').value;
    // catalog (non-builtin) boards: always refresh the descriptor from the network so a
    // stale localStorage copy can never hide newer data.
    if (id && id !== 'custom' && BUILTINS.indexOf(id) < 0){
      ensureBoard(id, function(ok){
        if (!ok){ showModal({ title:'Board unavailable', body:'That board could not be loaded from the online catalog. Connect to the internet and retry, or use Custom.', cancel:false, okText:'OK' }); $('board-sel').value = 'custom'; }
        afterBoardChange();
      });
    } else { afterBoardChange(); }
  });
  // modal board <select> delegates to the main one (so catalog fetch + state stay in one place)
  $('board-sel-modal').addEventListener('change', function(){
    $('board-sel').value = this.value;
    $('board-sel').dispatchEvent(new Event('change'));
  });
  $('board-apply').addEventListener('click', applyTemplate);
  $('board-open').addEventListener('click', function(){ openPicker(null); });
  $('board-modal-close').addEventListener('click', closeBoard);
  $('board-modal-done').addEventListener('click', closeBoard);
  $('board-print').addEventListener('click', printBoard);
  $('board-modal').addEventListener('click', function(e){ if (e.target === $('board-modal')) closeBoard(); });
  $('led-type').addEventListener('change', function(){ decorateAllPins(); validate(); });
  $('disp-type').addEventListener('change', function(){ decorateAllPins(); validate(); });
  updBoardButtons();
  validate();
  loadRemoteCatalog();
}


// This page has no socket of its own, so open a lightweight one just to feed the shared navbar.
(function(){ var s; function c(){ try{ s=new WebSocket('ws://'+location.host+'/ws'); s.binaryType='arraybuffer';
  s.onclose=function(){ setTimeout(c,2000); };
  s.onmessage=function(e){ if(window.LuxNav) LuxNav.stats(e.data); }; }catch(_){ setTimeout(c,2000); } }
  c(); })();

)=====";
