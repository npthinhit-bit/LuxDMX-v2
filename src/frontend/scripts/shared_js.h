#pragma once
#include <Arduino.h>

<<<<<<< ours
<<<<<<< ours
static const char SHARED_JS[] PROGMEM = R"=====(<script>
function esc(s){ return String(s==null?'':s).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]; }); }
function vNum(s){ var m = String(s).match(/(\d+)\.(\d+)\.(\d+)/); return m ? (+m[1]*1e6 + +m[2]*1e3 + +m[3]) : 0; }
function extractChanges(body) {
=======
=======
>>>>>>> theirs
static const char SHARED_JS[] PROGMEM = R"=====(

// Lightrope confirm/info dialog (no Bootstrap JS dependency).
function vNum(s){ var m = String(s).match(/(\d+)\.(\d+)\.(\d+)/); return m ? (+m[1]*1e6 + +m[2]*1e3 + +m[3]) : 0; }
function extractChanges(body){
<<<<<<< ours
>>>>>>> theirs
=======
>>>>>>> theirs
  if (!body) return [];
  var b = body, cut = b.search(/##?\s*Flash files/i);
  if (cut >= 0) b = b.slice(0, cut);
  return b.split('\n').map(function(l){ return l.trim(); })
    .filter(function(l){ return /^[-•]/.test(l); })
    .map(function(l){ return l.replace(/^[-•]\s*/, ''); });
}
<<<<<<< ours
<<<<<<< ours
function showModal(opts) {
  opts = opts || {};
  return new Promise(function(resolve) {
=======
function showModal(opts){
  opts = opts || {};
  return new Promise(function(resolve){
>>>>>>> theirs
=======
function showModal(opts){
  opts = opts || {};
  return new Promise(function(resolve){
>>>>>>> theirs
    var ov = document.getElementById('app-modal');
    document.getElementById('app-modal-title').textContent = opts.title || 'Confirm';
    document.getElementById('app-modal-body').textContent  = opts.body  || '';
    var ok = document.getElementById('app-modal-ok'), cancel = document.getElementById('app-modal-cancel');
    ok.textContent = opts.okText || 'OK';
    ok.className   = 'btn btn-sm ' + (opts.okClass || 'btn-primary');
    cancel.style.display = (opts.cancel === false) ? 'none' : '';
    ov.classList.add('show');
<<<<<<< ours
<<<<<<< ours
    function close(val) {
=======
    function close(val){
>>>>>>> theirs
=======
    function close(val){
>>>>>>> theirs
      ov.classList.remove('show');
      ok.onclick = cancel.onclick = ov.onclick = null;
      document.removeEventListener('keydown', onKey);
      resolve(val);
    }
<<<<<<< ours
<<<<<<< ours
    function onKey(ev) { if (ev.key === 'Escape') close(false); else if (ev.key === 'Enter') close(true); }
    ok.onclick     = function() { close(true); };
    cancel.onclick = function() { close(false); };
    ov.onclick     = function(ev) { if (ev.target === ov) close(false); };
    document.addEventListener('keydown', onKey);
  });
}
</script>
=======
=======
>>>>>>> theirs
    function onKey(ev){ if (ev.key === 'Escape') close(false); else if (ev.key === 'Enter') close(true); }
    ok.onclick     = function(){ close(true); };
    cancel.onclick = function(){ close(false); };
    ov.onclick     = function(ev){ if (ev.target === ov) close(false); };
    document.addEventListener('keydown', onKey);
  });
}
)=====";

static const char APP_MODAL_HTML[] PROGMEM = R"=====(
<!-- Confirm modal (shared: firmware update, pin conflicts, etc.) -->
<div class="app-modal" id="app-modal">
  <div class="card shadow">
    <div class="card-header" id="app-modal-title">Confirm</div>
    <div class="card-body">
      <p class="mb-3" id="app-modal-body"></p>
      <div class="d-flex justify-content-end gap-2">
        <button type="button" class="btn btn-outline-secondary btn-sm" id="app-modal-cancel">Cancel</button>
        <button type="button" class="btn btn-primary btn-sm" id="app-modal-ok">OK</button>
      </div>
    </div>
  </div>
</div>
<<<<<<< ours
>>>>>>> theirs
=======
>>>>>>> theirs
)=====";
