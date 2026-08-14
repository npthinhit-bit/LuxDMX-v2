#pragma once
#include <Arduino.h>

static const char INDEX_PAGE_CSS[] PROGMEM = R"=====(
:root {
  --gh-bg:      #0d1117;
  --gh-surface: #161b22;
  --gh-border:  #30363d;
  --gh-text:    #c9d1d9;
  --gh-muted:   #8b949e;
  /* LuxDMX brand palette: cyan / magenta / amber / green */
  --lux-cyan:    #23e6f7;
  --lux-magenta: #f33abc;
  --lux-amber:   #ffaa1c;
  --lux-green:   #45d85c;
  --lux-ink:     #06141a;   /* dark text for legibility on the bright fills */
  --gh-accent:  var(--lux-cyan);
}
[data-bs-theme="dark"] {
  --bs-body-bg:            var(--gh-bg);
  --bs-body-color:         var(--gh-text);
  --bs-card-bg:            var(--gh-surface);
  --bs-card-border-color:  var(--gh-border);
  --bs-border-color:       var(--gh-border);
  --bs-secondary-color:    var(--gh-muted);
  --bs-primary: #23e6f7; --bs-primary-rgb: 35,230,247;
  --bs-success: #45d85c; --bs-success-rgb: 69,216,92;
  --bs-warning: #ffaa1c; --bs-warning-rgb: 255,170,28;
  --bs-danger:  #f33abc; --bs-danger-rgb:  243,58,188;
  --bs-link-color: #23e6f7; --bs-link-color-rgb: 35,230,247; --bs-link-hover-color: #5cedfb;
}
/* Contextual buttons recoloured to the brand palette. The fills are bright, so the
   label ink goes dark (var(--lux-ink)) for contrast instead of Bootstrap's white. */
.btn-primary{--bs-btn-bg:#23e6f7;--bs-btn-border-color:#23e6f7;--bs-btn-color:#06141a;--bs-btn-hover-bg:#5cedfb;--bs-btn-hover-border-color:#5cedfb;--bs-btn-hover-color:#06141a;--bs-btn-active-bg:#15bccc;--bs-btn-active-border-color:#15bccc;--bs-btn-active-color:#06141a;--bs-btn-disabled-bg:#23e6f7;--bs-btn-disabled-border-color:#23e6f7;--bs-btn-disabled-color:#06141a;}
.btn-success{--bs-btn-bg:#45d85c;--bs-btn-border-color:#45d85c;--bs-btn-color:#06141a;--bs-btn-hover-bg:#6ee27f;--bs-btn-hover-border-color:#6ee27f;--bs-btn-hover-color:#06141a;--bs-btn-active-bg:#2fb846;--bs-btn-active-border-color:#2fb846;--bs-btn-active-color:#06141a;}
.btn-warning{--bs-btn-bg:#ffaa1c;--bs-btn-border-color:#ffaa1c;--bs-btn-color:#06141a;--bs-btn-hover-bg:#ffbe4d;--bs-btn-hover-border-color:#ffbe4d;--bs-btn-hover-color:#06141a;--bs-btn-active-bg:#e08e00;--bs-btn-active-border-color:#e08e00;--bs-btn-active-color:#06141a;}
.btn-danger{--bs-btn-bg:#f33abc;--bs-btn-border-color:#f33abc;--bs-btn-color:#06141a;--bs-btn-hover-bg:#ff5bcd;--bs-btn-hover-border-color:#ff5bcd;--bs-btn-hover-color:#06141a;--bs-btn-active-bg:#d11d9c;--bs-btn-active-border-color:#d11d9c;--bs-btn-active-color:#06141a;}
.btn-outline-primary{--bs-btn-color:#23e6f7;--bs-btn-border-color:#23e6f7;--bs-btn-hover-bg:#23e6f7;--bs-btn-hover-border-color:#23e6f7;--bs-btn-hover-color:#06141a;--bs-btn-active-bg:#23e6f7;--bs-btn-active-border-color:#23e6f7;--bs-btn-active-color:#06141a;}
.btn-outline-success{--bs-btn-color:#45d85c;--bs-btn-border-color:#45d85c;--bs-btn-hover-bg:#45d85c;--bs-btn-hover-border-color:#45d85c;--bs-btn-hover-color:#06141a;--bs-btn-active-bg:#45d85c;--bs-btn-active-border-color:#45d85c;--bs-btn-active-color:#06141a;}
.btn-outline-warning{--bs-btn-color:#ffaa1c;--bs-btn-border-color:#ffaa1c;--bs-btn-hover-bg:#ffaa1c;--bs-btn-hover-border-color:#ffaa1c;--bs-btn-hover-color:#06141a;--bs-btn-active-bg:#ffaa1c;--bs-btn-active-border-color:#ffaa1c;--bs-btn-active-color:#06141a;}
.btn-outline-danger{--bs-btn-color:#f33abc;--bs-btn-border-color:#f33abc;--bs-btn-hover-bg:#f33abc;--bs-btn-hover-border-color:#f33abc;--bs-btn-hover-color:#06141a;--bs-btn-active-bg:#f33abc;--bs-btn-active-border-color:#f33abc;--bs-btn-active-color:#06141a;}
.form-check-input:checked{background-color:#23e6f7;border-color:#23e6f7;}
.form-control:focus,.form-select:focus{border-color:#23e6f7;box-shadow:0 0 0 .25rem rgba(35,230,247,.25);}
.form-range::-webkit-slider-thumb{background:#23e6f7;}
.form-range::-moz-range-thumb{background:#23e6f7;}
html { background: var(--gh-bg); }   /* dark from first paint: no white flash on tab switch */
body { background: var(--gh-bg); animation:lxfade .16s ease-out; }
@keyframes lxfade { from { opacity:0 } to { opacity:1 } }

/* the navbar (markup + its CSS + behaviour) is the shared src/pages/_nav.html fragment */
.card { background: var(--gh-surface); border-color: var(--gh-border); }
.card-header { background: #1c2128; border-color: var(--gh-border); font-size: 0.85rem; }


#grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, 42px);
  gap: 3px;
}
.ch {
  width: 42px; height: 42px;
  border-radius: 5px;
  background: #1c2128;
  border: 1px solid var(--gh-border);
  display: flex; flex-direction: column;
  align-items: center; justify-content: center;
  cursor: pointer;
  user-select: none;
  transition: transform .08s, border-color .1s;
  position: relative;
  overflow: hidden;            /* clip the gauge fill to the rounded corners */
}
.ch:hover { transform: scale(1.15); border-color: var(--gh-accent); z-index: 1; }
/* Gauge: a frosted fill rising from the bottom; height ∝ channel value.
   The cell's own background still carries the value brightness underneath. */
.ch .gauge {
  position: absolute; left: 0; right: 0; bottom: 0;
  height: 0%;
  background: rgba(255,255,255,.20);
  border-top: 1px solid rgba(255,255,255,.55);
  z-index: 0; pointer-events: none;
  transition: height .12s linear;
}
.ch .ch-n, .ch .ch-v, .ch .ch-l { position: relative; z-index: 1; }
/* tiny hard 1px seam (0-blur), cheap to paint, keeps text crisp over the gauge */
.ch .ch-n { font-size: 0.74rem; font-weight: 700; color: #fff; line-height: 1.05;
  text-shadow: 1px 0 0 #000, -1px 0 0 #000, 0 1px 0 #000, 0 -1px 0 #000; }
.ch .ch-v { font-size: 0.5rem; color: #fff; line-height: 1.1;
  text-shadow: 1px 0 0 #000, -1px 0 0 #000, 0 1px 0 #000, 0 -1px 0 #000; }
.ch .ch-l {
  font-size: 0.4rem; color: #bfeff6; font-weight: 600; line-height: 1;
  max-width: 40px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  text-shadow: 1px 0 0 #000, -1px 0 0 #000, 0 1px 0 #000, 0 -1px 0 #000;
}
.ch.labeled { border-color: #1f7d8a; }   /* dimmed cyan ring on named channels */

/* Custom hover tooltip: a 2.5x-zoomed live clone of the hovered cell, shown instantly. */
#cell-tip {
  position: fixed; display: none;
  z-index: 2000; pointer-events: none;
  width: 105px; height: 105px;
  filter: drop-shadow(0 10px 24px rgba(0,0,0,.7));
}
#cell-tip .ch {
  transform: scale(2.5); transform-origin: top left;
  transition: none; cursor: default;
}

.form-check-input:checked { background-color: var(--gh-accent); border-color: var(--gh-accent); }

/* Channel modal */
.ch-overlay {
  display: none; position: fixed; inset: 0;
  background: rgba(0,0,0,.75); z-index: 1040;
  align-items: center; justify-content: center;
}
.ch-overlay.show { display: flex; }
.modal-card {
  background: #161b22;
  border: 1px solid var(--gh-border);
  border-radius: 10px;
  padding: 1.4rem;
  width: min(380px, 94vw);
  position: relative;
  z-index: 1;
}
.form-range::-webkit-slider-thumb { background: var(--gh-accent); }
.form-range::-webkit-slider-runnable-track { background: #30363d; }

/* Generic confirm modal (firmware update, etc.) */
.app-modal { display:none; position:fixed; inset:0; background:rgba(0,0,0,.7); z-index:1080; align-items:center; justify-content:center; padding:1rem; }
.app-modal.show { display:flex; }
.app-modal .card { width:min(440px,96vw); }

#ws-badge { font-size: 0.72rem; }
footer { border-color: var(--gh-border) !important; color: var(--gh-muted); font-size: 0.77rem; }
footer a { color: var(--gh-muted); text-decoration: none; }
footer a:hover { color: var(--gh-text); }

.snd-tbl { font-size: .8rem; }
.snd-tbl th { color: var(--gh-muted); font-weight: 500; border-color: var(--gh-border) !important; }
.snd-tbl td { border-color: var(--gh-border) !important; }
)=====";
