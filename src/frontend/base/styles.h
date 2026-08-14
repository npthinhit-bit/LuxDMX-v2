#pragma once
#include <Arduino.h>

static const char FRONTEND_STYLES[] PROGMEM = R"=====(:root {
  --gh-bg:      #0d1117;
  --gh-surface: #161b22;
  --gh-border:  #30363d;
  --gh-text:    #c9d1d9;
  --gh-muted:   #8b949e;
  --lux-cyan:    #23e6f7;
  --lux-magenta: #f33abc;
  --lux-amber:   #ffaa1c;
  --lux-green:   #45d85c;
  --lux-ink:     #06141a;
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
html { background: var(--gh-bg); scrollbar-gutter: stable; }
body { background: var(--gh-bg); animation:lxfade .16s ease-out; }
@keyframes lxfade { from { opacity:0 } to { opacity:1 } }
.card { background: var(--gh-surface); border-color: var(--gh-border); }
.card-header { background: #1c2128; border-color: var(--gh-border); }
.form-control,.form-select { background:var(--gh-bg); border-color:var(--gh-border); color:var(--gh-text); }
.form-control:focus { background:var(--gh-bg); border-color:var(--gh-accent); color:var(--gh-text); box-shadow:0 0 0 .25rem rgba(35,230,247,.25); }
.input-group-text { background:#1c2128; border-color:var(--gh-border); color:var(--gh-muted); }
footer { border-color:var(--gh-border)!important; color:var(--gh-muted); font-size:.77rem; }
footer a { color:var(--gh-muted); text-decoration:none; }
footer a:hover { color:var(--gh-text); }
.app-modal { display:none; position:fixed; inset:0; background:rgba(0,0,0,.7); z-index:1080; align-items:center; justify-content:center; padding:1rem; }
.app-modal.show { display:flex; }
.app-modal .card { width:min(440px,96vw); }
#ws-badge { font-size: 0.72rem; }

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
  overflow: hidden;
}
.ch:hover { transform: scale(1.15); border-color: var(--gh-accent); z-index: 1; }
.ch .gauge {
  position: absolute; left: 0; right: 0; bottom: 0;
  height: 0%;
  background: rgba(255,255,255,.20);
  border-top: 1px solid rgba(255,255,255,.55);
  z-index: 0; pointer-events: none;
  transition: height .12s linear;
}
.ch .ch-n, .ch .ch-v, .ch .ch-l { position: relative; z-index: 1; }
.ch .ch-n { font-size: 0.74rem; font-weight: 700; color: #fff; line-height: 1.05;
  text-shadow: 1px 0 0 #000, -1px 0 0 #000, 0 1px 0 #000, 0 -1px 0 #000; }
.ch .ch-v { font-size: 0.5rem; color: #fff; line-height: 1.1;
  text-shadow: 1px 0 0 #000, -1px 0 0 #000, 0 1px 0 #000, 0 -1px 0 #000; }
.ch .ch-l {
  font-size: 0.4rem; color: #bfeff6; font-weight: 600; line-height: 1;
  max-width: 40px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  text-shadow: 1px 0 0 #000, -1px 0 0 #000, 0 1px 0 #000, 0 -1px 0 #000;
}
.ch.labeled { border-color: #1f7d8a; }
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

.snd-tbl { font-size: .8rem; }
.snd-tbl th { color: var(--gh-muted); font-weight: 500; border-color: var(--gh-border) !important; }
.snd-tbl td { border-color: var(--gh-border) !important; }

.pin-grp .pin-pick { flex:0 0 auto; padding:.15rem .45rem; display:flex; align-items:center; }
.pin-grp .pin-pick svg { width:15px; height:15px; }
.is-warning { border-color:#ffaa1c !important; }
.is-warning:focus { box-shadow:0 0 0 .25rem rgba(255,170,28,.3)!important; }
#pin-warnings .alert { font-size:.8rem; padding:.5rem .75rem; }
#pin-warnings .alert:last-child { margin-bottom:0; }
.app-modal .card.board-card { width:min(700px,96vw); max-height:92vh; overflow:auto; }
.board-wrap { overflow:auto; text-align:center; padding:10px 0; }
.board-svg { max-width:100%; height:auto; font-family:ui-monospace,Menlo,Consolas,monospace; }
.board-svg .pad { cursor:pointer; }
.board-svg .pad:hover .pin-pad { stroke:#23e6f7; stroke-width:2.6; }
.board-svg .ppin { cursor:default; }
.board-svg .ppin.power { pointer-events:none; }
.board-svg .ppin.power text { font-style:italic; }
.board-svg .board-body { pointer-events:none; }
.pin-dot { fill:#0d1117; pointer-events:none; }
.board-svg .pad text, .board-svg .ppin text { pointer-events:none; }
.board-legend { font-size:.74rem; color:#8b949e; display:flex; gap:1rem; flex-wrap:wrap; justify-content:center; margin-top:.5rem; }
.board-legend span::before { content:''; display:inline-block; width:10px; height:10px; border-radius:50%; margin-right:.3rem; vertical-align:middle; }
.lg-free::before{ background:#45d85c; } .lg-warn::before{ background:#ffaa1c; } .lg-bad::before{ background:#f33abc; } .lg-used::before{ background:#0d1117; box-shadow:0 0 0 2px #23e6f7 inset; }
.lg-pwr::before{ background:#d98c1f; border-radius:2px; } .lg-gnd::before{ background:#7d8590; border-radius:2px; }
.hdr-strips { margin-top:.9rem; display:flex; flex-direction:column; align-items:center; gap:.5rem; }
.hdr-strips .hdr-strip { max-width:100%; }
.hdr-strips-cap { font-size:.74rem; color:#8b949e; }
.pin-grp .hdr-hint { font-size:.7rem; padding:.15rem .4rem; color:#23e6f7; background:#161b22; cursor:help; }
.fixed-field { background:#161b22 !important; color:var(--gh-muted) !important; cursor:not-allowed; opacity:.9; }
.hw-fixed, .hw-headers { font-size:.8rem; margin-top:.55rem; }
.hw-fixed .hwb { display:inline-block; background:#161b22; border:1px solid var(--gh-border); border-radius:4px; padding:.05rem .4rem; margin:.12rem .25rem .12rem 0; color:var(--gh-text); white-space:nowrap; }
.hw-fixed .g, .hdr-tbl .g { color:#23e6f7; }
.hdr-tbl { display:inline-block; vertical-align:top; border-collapse:collapse; margin:.35rem .8rem .2rem 0; }
.hdr-tbl caption { caption-side:top; text-align:left; font-weight:600; color:var(--gh-text); padding-bottom:.15rem; }
.hdr-tbl td { border:1px solid var(--gh-border); padding:.08rem .45rem; line-height:1.5; }
.hdr-tbl .pn { color:var(--gh-muted); text-align:right; }
.hdr-tbl .pw { color:#ffaa1c; }
.card-header.sec-head { display:flex; align-items:center; gap:.5rem; cursor:pointer; user-select:none; }
.card-header.sec-head:hover { filter:brightness(1.35); }
.card-header.sec-head:focus-visible { outline:2px solid var(--lux-cyan); outline-offset:-2px; }
.sec-caret { flex:0 0 auto; width:11px; height:11px; color:var(--gh-muted); transition:transform .15s ease; }
.sec-caret svg { display:block; width:100%; height:100%; }
.card.sec-closed > .card-header .sec-caret { transform:rotate(-90deg); }
.card.sec-closed > .card-body { display:none; }
.sec-sum { margin-left:auto; min-width:0; font-weight:400; font-size:.78rem; color:var(--gh-muted);
  white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.sec-head .form-check, .sec-head #board-detected { cursor:default; }
#save-bar { position:fixed; left:0; right:0; bottom:0; z-index:1030; padding:.55rem 1rem;
  background:rgba(13,17,23,.93); backdrop-filter:blur(6px); -webkit-backdrop-filter:blur(6px);
  border-top:1px solid var(--gh-border); box-shadow:0 -3px 14px rgba(0,0,0,.45); }
#save-bar .save-inner { max-width:960px; margin:0 auto; }
body { padding-bottom:5rem; }

.mono { font-family:monospace; }
.pill { font-size:.68rem; padding:.12em .5em; border-radius:10px; background:#21262d; color:var(--gh-muted); }
.dash-title { font-size:.8rem; font-weight:600; margin:.1rem 0 .45rem; }
.s-grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(290px,1fr)); gap:.6rem; margin-bottom:1rem; }
.s-card { border:1px solid var(--gh-border); border-radius:8px; background:#0d1117; padding:.5rem .6rem .45rem; }
.s-title { font-size:.78rem; font-weight:600; margin-bottom:.1rem; }
.s-title .c-unit { color:var(--gh-muted); font-weight:400; }
.chart-svg { width:100%; height:auto; display:block; }
.c-grid { stroke:#20262e; stroke-width:.5; }
.c-line { fill:none; stroke-width:1.5; vector-effect:non-scaling-stroke; }
.c-dot  { stroke:#0d1117; stroke-width:.5; }
.c-ax   { fill:var(--gh-muted); font-size:8px; }
.c-empty{ fill:var(--gh-muted); font-size:9px; }
.legend { display:flex; flex-wrap:wrap; gap:.05rem .7rem; margin-top:.3rem; }
.legend .lg { font-size:.66rem; color:var(--gh-muted); display:flex; align-items:center; gap:.3rem; }
.legend .lg i { width:10px; height:3px; border-radius:2px; display:inline-block; }
.fxt { width:100%; border-collapse:collapse; font-size:.82rem; }
.fxt thead th { text-align:left; font-weight:600; color:var(--gh-muted); font-size:.64rem;
  text-transform:uppercase; letter-spacing:.04em; padding:.3rem .5rem; border-bottom:1px solid var(--gh-border); white-space:nowrap; }
.fxt th.sortable { cursor:pointer; user-select:none; }
.fxt th.sortable:hover { color:#c9d1d9; }
.fxt th .sort-ind { color:#58a6ff; margin-left:.2em; }
.fxt td { padding:.4rem .5rem; border-bottom:1px solid var(--gh-border); vertical-align:middle; }
.fxrow { cursor:pointer; }
.fxrow:hover > td { background:#1c2128; }
.fxrow .fx-name { font-weight:600; }
.fxrow .fx-uid { font-family:monospace; font-size:.72rem; color:var(--gh-muted); }
.uni-badge { font-size:.68rem; padding:.08em .45em; border-radius:6px; background:rgba(88,166,255,.18); color:#79c0ff; font-weight:600; }
.fxrow .caret { display:inline-block; width:.85em; transition:transform .15s; color:var(--gh-muted); }
.fxrow.open .caret { transform:rotate(90deg); }
.fxedit { display:none; }
.fxedit.open { display:table-row; }
.fxedit > td { background:#0d1117; }
.pers-sel { display:inline-block; width:auto; font-size:.72rem; line-height:1.1; min-height:auto;
  padding:.1rem 1.15rem .1rem .35rem; background-position:right .25rem center; background-size:9px; }
.s-cell { min-width:170px; }
.s-item { display:flex; align-items:center; gap:.45rem; padding:.05rem 0; }
.s-item .form-check { min-height:auto; margin:0; padding-left:2.1em; }
.s-item .form-check-input { cursor:pointer; }
.s-item .s-nm  { color:var(--gh-muted); font-size:.72rem; }
.s-item .s-val { font-variant-numeric:tabular-nums; font-weight:600; margin-left:auto; white-space:nowrap; }
.s-all { border-bottom:1px solid var(--gh-border); padding-bottom:.2rem; margin-bottom:.15rem; }
.s-all .s-all-lbl { color:#c9d1d9; font-weight:600; }
.s-none { color:var(--gh-muted); }
.lx-modal { position:fixed; inset:0; background:rgba(1,4,9,.7); display:none; align-items:center; justify-content:center; z-index:1050; }
.lx-modal.show { display:flex; animation:lxfade .12s ease-out; }
.lx-modal-box { width:min(440px,92vw); box-shadow:0 12px 40px rgba(0,0,0,.55); }

@view-transition { navigation: auto; }
::view-transition-group(site-nav) { animation-duration:.18s; }
::view-transition-old(root), ::view-transition-new(root) { animation-duration:.18s; }
)=====";
