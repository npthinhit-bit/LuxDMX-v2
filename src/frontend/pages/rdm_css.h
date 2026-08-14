#pragma once
#include <Arduino.h>

static const char RDM_PAGE_CSS[] PROGMEM = R"=====(
:root { --gh-border:#30363d; --gh-muted:#8b949e; }
/* Dark from the very first paint so a page navigation never flashes white before the CSS/content
   loads, and a quick fade-in so switching tabs reads as a smooth transition in every browser. */
html { background:#0d1117; }
body { background:#0d1117; animation:lxfade .16s ease-out; }
@keyframes lxfade { from { opacity:0 } to { opacity:1 } }
/* the navbar (markup + its CSS + behaviour) is the shared src/pages/_nav.html fragment */
.card-header { background:#1c2128; border-color:var(--gh-border); font-size:.85rem; }
.mono { font-family:monospace; }
.pill { font-size:.68rem; padding:.12em .5em; border-radius:10px; background:#21262d; color:var(--gh-muted); }

/* grouped sensor charts (one per RDM sensor type, a line per fixture-series) */
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

/* fixtures table (all info visible; expand a row only to edit it) */
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
/* mini personality dropdown */
.pers-sel { display:inline-block; width:auto; font-size:.72rem; line-height:1.1; min-height:auto;
  padding:.1rem 1.15rem .1rem .35rem; background-position:right .25rem center; background-size:9px; }
/* per-sensor switch + live value inside a table row */
.s-cell { min-width:170px; }
.s-item { display:flex; align-items:center; gap:.45rem; padding:.05rem 0; }
.s-item .form-check { min-height:auto; margin:0; padding-left:2.1em; }
.s-item .form-check-input { cursor:pointer; }
.s-item .s-nm  { color:var(--gh-muted); font-size:.72rem; }
.s-item .s-val { font-variant-numeric:tabular-nums; font-weight:600; margin-left:auto; white-space:nowrap; }
.s-all { border-bottom:1px solid var(--gh-border); padding-bottom:.2rem; margin-bottom:.15rem; }
.s-all .s-all-lbl { color:#c9d1d9; font-weight:600; }
.s-none { color:var(--gh-muted); }
/* discovery confirmation modal (Bootstrap-styled, no browser popup) */
.lx-modal { position:fixed; inset:0; background:rgba(1,4,9,.7); display:none; align-items:center; justify-content:center; z-index:1050; }
.lx-modal.show { display:flex; animation:lxfade .12s ease-out; }
.lx-modal-box { width:min(440px,92vw); box-shadow:0 12px 40px rgba(0,0,0,.55); }
)=====";
