#pragma once
#include <Arduino.h>

static const char INDEX_PAGE_BODY[] PROGMEM = R"=====(
<div class="container-fluid p-3">

  <!-- Conflict warning -->
  <div id="conflict-banner" style="display:none;background:#2a0a20;border:1px solid #f33abc"
       class="align-items-center gap-2 mb-3 px-3 py-2 rounded">
    <span style="color:#f33abc">&#9888;</span>
    <span class="small" style="color:#f33abc">
      Universe conflict: two or more sources are clashing on a universe.
      Enable a merge mode (HTP/LTP) on that output, or leave only one source.
    </span>
  </div>

  <!-- Merging indicator (positive: multiple sources combined on purpose) -->
  <div id="merge-banner" style="display:none;background:#0c2417;border:1px solid #45d85c"
       class="align-items-center gap-2 mb-3 px-3 py-2 rounded">
    <span style="color:#45d85c">&#10227;</span>
    <span class="small" style="color:#45d85c">
      Merging multiple sources on a universe (HTP/LTP).
    </span>
  </div>

  <!-- Update banner -->
  <div id="update-banner" class="mb-3 px-3 py-2 rounded"
       style="display:none;background:#2a2400;border:1px solid #ffaa1c">
    <div class="d-flex align-items-center justify-content-between">
      <span style="color:#ffaa1c;font-size:.85rem">&#8635;&nbsp; Firmware update available: <strong id="update-ver"></strong></span>
      <div class="d-flex gap-2">
        <button type="button" id="update-go" class="btn btn-sm fw-semibold" style="background:#ffaa1c;color:#06141a;font-size:.78rem">Update</button>
        <button type="button" id="update-dismiss" class="btn btn-sm" style="border:1px solid #ffaa1c;color:#ffaa1c;font-size:.78rem">Dismiss</button>
      </div>
    </div>
    <pre id="update-notes" style="display:none;margin:.5rem 0 0;font-size:.72rem;color:#e7c46b;white-space:pre-wrap;word-break:break-word;background:rgba(0,0,0,.25);border-radius:4px;padding:.4rem .6rem;max-height:160px;overflow-y:auto"></pre>
    <!-- Hidden form: the Update button installs the newest release directly (no detour via /config) -->
    <form method="POST" action="/ota/github" id="ota-form" class="d-none">
      <input type="hidden" name="version" id="ota-version">
    </form>
  </div>


  <!-- Controls -->
  <div class="card mb-3">
    <div class="card-body py-2 d-flex align-items-center flex-wrap gap-3">
      <div class="align-items-center gap-2 mb-0" id="out-sel-wrap" style="display:none">
        <span class="mb-0 small text-secondary">View</span>
        <div class="btn-group btn-group-sm" id="out-sel" role="group" aria-label="Output view"></div>
      </div>
      <div class="form-check form-switch mb-0">
        <input class="form-check-input" type="checkbox" id="modeSwitch" role="switch"
               onchange="setMode(this.checked)">
        <label class="form-check-label" for="modeSwitch">Manual override</label>
      </div>
      <button class="btn btn-danger btn-sm" onclick="sendBlackout()">Blackout</button>
      <span class="badge ms-auto" id="ws-badge" style="background:#30363d;padding:.35em .65em">Connecting…</span>
    </div>
  </div>

  <!-- DMX Grid -->
  <div class="card mb-3">
    <div class="card-header d-flex align-items-center justify-content-between">
      <span>DMX Channels 1-512</span>
      <small class="text-secondary">click to edit</small>
    </div>
    <div class="card-body p-2">
      <div id="grid"></div>
    </div>
  </div>

  <!-- Active Senders -->
  <div class="card mb-3">
    <div class="card-header">Active Senders</div>
    <div class="card-body p-0">
      <table class="table table-sm mb-0 snd-tbl" style="--bs-table-bg:transparent">
        <thead>
          <tr>
            <th class="ps-3">Source IP</th>
            <th>Protocol</th>
            <th>FPS</th>
            <th>Last seen</th>
          </tr>
        </thead>
        <tbody id="senders-body">
          <tr><td colspan="4" class="text-secondary text-center small py-2">No senders detected</td></tr>
        </tbody>
      </table>
    </div>
  </div>

  <!-- RDM fixtures live on their own tab now (/rdm) -->

  <!-- Change Log -->
  <div class="card mb-3">
    <div class="card-header">Change Log</div>
    <div id="log-body" style="max-height:280px;overflow-y:auto">
      <div class="text-secondary small text-center py-3">No changes yet</div>
    </div>
  </div>

</div>

<!-- Channel modal -->
<div class="ch-overlay" id="modal" onclick="if(event.target===this)closeModal()">
  <div class="modal-card">
    <div class="d-flex align-items-center justify-content-between mb-2">
      <span class="fw-semibold" id="modal-title">Channel</span>
      <button class="btn btn-sm btn-outline-secondary px-2 py-0" onclick="closeModal()">&times;</button>
    </div>
    <div class="d-flex gap-2 mb-2">
      <input type="text" class="form-control form-control-sm" id="ch-label" maxlength="24"
             placeholder="Label (e.g. Front Wash L)"
             style="background:#0d1117;border-color:#30363d;color:#c9d1d9"
             onchange="saveLabel()">
      <button class="btn btn-outline-warning btn-sm text-nowrap" onclick="identify()" title="Flash this channel to full for ~1.5 s to locate the fixture">Identify</button>
    </div>
    <svg id="spark-svg" viewBox="0 0 60 40" preserveAspectRatio="none"
         width="100%" height="36"
         style="display:block;border-radius:4px;background:#1c2128;margin-bottom:.5rem">
      <polyline id="spark-line" points="" fill="none" stroke="#23e6f7" stroke-width="1.5"
                vector-effect="non-scaling-stroke"/>
    </svg>
    <input type="range" class="form-range mb-1" id="ch-slider" min="0" max="255"
           oninput="sliderMove(+this.value)">
    <div class="d-flex justify-content-between align-items-center mb-3">
      <span class="text-secondary small" id="modal-pct">0%</span>
      <span class="fw-bold" style="font-size:1.3rem" id="modal-val">0</span>
    </div>
    <div class="d-flex gap-2">
      <button class="btn btn-outline-secondary btn-sm flex-fill" onclick="setQuick(0)">Off</button>
      <button class="btn btn-outline-secondary btn-sm flex-fill" onclick="setQuick(128)">50%</button>
      <button class="btn btn-outline-secondary btn-sm flex-fill" onclick="setQuick(255)">Full</button>
      <button class="btn btn-primary btn-sm flex-fill" onclick="closeModal()">Done</button>
    </div>
  </div>
)=====";
