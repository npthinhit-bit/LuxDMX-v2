#pragma once
#include <Arduino.h>

static const char RDM_PAGE_BODY[] PROGMEM = R"=====(
<div class="container-fluid p-3">
  <div class="card mb-3">
    <div class="card-header d-flex align-items-center justify-content-between flex-wrap gap-2">
      <span>Fixtures (RDM)</span>
      <div class="d-flex align-items-center gap-3">
        <div class="form-check form-switch mb-0" title="Toggle all currently-selected sensors off, then back on. Never enables everything.">
          <input class="form-check-input" type="checkbox" id="live-sw" onclick="aggClick(event,'master')">
          <label class="form-check-label small" for="live-sw">Live sensors</label>
        </div>
        <div class="d-flex align-items-center gap-1" title="Art-Net BackgroundQueuePolicy: harvest RDM status messages from fixtures in the background at this severity. Also settable from a console (DMX-Workshop).">
          <label class="form-label small mb-0 text-secondary" for="bqp-sel">Health poll</label>
          <select class="form-select form-select-sm py-0" style="width:auto" id="bqp-sel" onchange="setBqp(this.value)">
            <option value="4">Off</option>
            <option value="1">Advisory</option>
            <option value="2">Warning</option>
            <option value="3">Error</option>
          </select>
        </div>
        <span class="pill" id="art-port">&nbsp;</span>
        <span id="disc-ctl" class="d-flex gap-1"></span>
      </div>
    </div>
    <div class="card-body">
      <div id="merge-ctl" class="d-flex flex-wrap align-items-center gap-2 mb-2 small"></div>
      <div id="rdm-status" class="text-secondary small mb-2">Loading…</div>
      <div id="sensor-dash"></div>
      <div id="fx-list"></div>
    </div>
  </div>
  <div class="text-secondary small" id="art-stats"></div>
</div>

<div id="disc-modal" class="lx-modal" onclick="if(event.target===this)hideDiscModal()">
  <div class="lx-modal-box card">
    <div class="card-header">Run RDM discovery?</div>
    <div class="card-body">
      <p class="small text-secondary mb-3"><span id="disc-modal-txt" class="text-info"></span>
        Discovery shares the DMX wire to scan for fixtures, so the output frame rate can dip for a
        few seconds while it runs. Best not to do this on a live show cue.</p>
      <div class="d-flex justify-content-end gap-2">
        <button class="btn btn-outline-secondary btn-sm" onclick="hideDiscModal()">Cancel</button>
        <button class="btn btn-primary btn-sm" onclick="startDiscovery()">Run discovery</button>
      </div>
)=====";
