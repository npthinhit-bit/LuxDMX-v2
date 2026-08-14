#pragma once
#include <Arduino.h>

static const char SHARED_JS[] PROGMEM = R"=====(<script>
function esc(s){ return String(s==null?'':s).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]; }); }
function vNum(s){ var m = String(s).match(/(\d+)\.(\d+)\.(\d+)/); return m ? (+m[1]*1e6 + +m[2]*1e3 + +m[3]) : 0; }
function extractChanges(body) {
  if (!body) return [];
  var b = body, cut = b.search(/##?\s*Flash files/i);
  if (cut >= 0) b = b.slice(0, cut);
  return b.split('\n').map(function(l){ return l.trim(); })
    .filter(function(l){ return /^[-•]/.test(l); })
    .map(function(l){ return l.replace(/^[-•]\s*/, ''); });
}
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
</script>
)=====";
