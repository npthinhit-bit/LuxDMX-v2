# Web Routes — Technical Reference

| Attribute | Value |
|---|---|
| **Layer** | `net` |
| **Module** | `web_routes` |
| **Source Files** | `src/net/web_routes.h`, `src/net/web_routes.cpp` |
| **Route Registration** | `src/net/web_server.cpp:29` |
| **Related** | [Web Server](net-web-server.md), [Rate Limiter](net-rate-limiter.md), [Web Frontend](net-web-frontend.md) |

## 1. File Inventory

| Source File | Purpose |
|---|---|
| `src/net/web_routes.h` | Forward declarations for all 26 dynamic handlers + `parseUidParam` helper |
| `src/net/web_routes.cpp` | Full handler implementations (JSON APIs, config, OTA, RDM, setup, reset, LED) |

## 2. Handler Inventory

### 2.1 JSON Snapshot Handlers

| Handler | Route | Source |
|---|---|---|
| `handleDmxJson` | `GET /dmx.json` | `src/net/web_routes.cpp:33` |
| `handleSendersJson` | `GET /senders.json` | `src/net/web_routes.cpp:58` |
| `handleLogJson` | `GET /log.json` | `src/net/web_routes.cpp:77` |
| `handleInfoJson` | `GET /info.json` | `src/net/web_routes.cpp:81` |
| `handleVersionJson` | `GET /version.json` | `src/net/web_routes.cpp:106` |
| `handleRdmJson` | `GET /rdm.json` | `src/net/web_routes.cpp:118` |

### 2.2 Config Handlers

| Handler | Route | Source |
|---|---|---|
| `handleConfigExport` | `GET /config/export` | `src/net/web_routes.cpp:166` |
| `handleConfigImport` | `POST /config/import` | `src/net/web_routes.cpp:173` |
| `handleConfigPost` | `POST /config` | `src/net/web_routes.cpp:222` |

### 2.3 Health Handler

| Handler | Route | Source |
|---|---|---|
| `handleHealth` | `GET /health` | `src/net/web_routes.cpp:188` |

### 2.4 Setup Handlers

| Handler | Route | Source |
|---|---|---|
| `handleSetupScan` | `GET /setup/scan` | `src/net/web_routes.cpp:306` |
| `handleSetupPost` | `POST /setup` | `src/net/web_routes.cpp:320` |

### 2.5 Reset / Reboot Handlers

| Handler | Route | Source |
|---|---|---|
| `handleResetPost` | `POST /reset` | `src/net/web_routes.cpp:334` |
| `handleRebootPost` | `POST /reboot` | `src/net/web_routes.cpp:346` |

### 2.6 OTA Handlers

| Handler | Route | Source |
|---|---|---|
| `handleOtaGithub` | `POST /ota/github` | `src/net/web_routes.cpp:352` |
| `handleOtaUrl` | `POST /ota/url` | `src/net/web_routes.cpp:374` |
| `handleOtaStatusJson` | `GET /ota/status` | `src/net/web_routes.cpp:389` |
| `otaUploadChunk` | `POST /ota/upload` | declared `src/net/web_server.h:20`, implemented in `src/net/ota.cpp` |

### 2.7 RDM Handlers

| Handler | Route | Source |
|---|---|---|
| `handleRdmTrigger` | `GET /rdm/discover`, `/rdm/setaddr`, `/rdm/identify`, `/rdm/setpers`, `/rdm/setlabel` | `src/net/web_routes.cpp:397` |
| `handleRdmTod` | `GET /rdm/tod` | `src/net/web_routes.cpp:469` |
| `handleRdmBqp` | `GET /rdm/bqp` | `src/net/web_routes.cpp:479` |
| `handleRdmMerge` | `GET /rdm/merge` | `src/net/web_routes.cpp:494` |
| `parseUidParam` | helper | `src/net/web_routes.cpp:510` |

### 2.8 LED Handler

| Handler | Route | Source |
|---|---|---|
| `handleLedBright` | `GET /led/bright` | `src/net/web_routes.cpp:522` |

### 2.9 Label Handlers

| Handler | Route | Source |
|---|---|---|
| `handleLabelsGet` | `GET /labels.json` | `src/net/web_routes.cpp:291` |
| `handleLabelsBody` | `POST /labels` (upload) | `src/net/web_routes.cpp:301` |

### 2.10 Auto-Update Handler

| Handler | Route | Source |
|---|---|---|
| `handleAutoUpdatePost` | `POST /autoupdate` | `src/net/web_routes.cpp:295` |

## 3. Helper Functions

### 3.1 `sendJson` (internal)
```cpp
static void sendJson(AsyncWebServerRequest* req, const char* j);
```
- Source: `src/net/web_routes.cpp:27`.
- Creates a 200 response with `application/json` content type.
- Adds `Cache-Control: no-store` header (`src/net/web_routes.cpp:29`).
- Called by: `handleDmxJson`, `handleSendersJson`, `handleLogJson`, `handleInfoJson`, `handleVersionJson`, `handleRdmJson`, `handleConfigExport`, `handleHealth`, `handleRdmTod`, `handleOtaStatusJson`.

### 3.2 `parseUidParam`
```cpp
bool parseUidParam(AsyncWebServerRequest* req, const char* name, rdm_uid_t& uid);
```
- Source: `src/net/web_routes.cpp:510`.
- Extracts a 12-character hex UID string from a query parameter.
- Uses `sscanf` with `%04x` for manufacturer ID and `%08x` for device ID (`src/net/web_routes.cpp:515-516`).
- Returns `false` if the parameter is missing or the hex string is < 12 chars (`src/net/web_routes.cpp:513`).
- Used by: `handleRdmTrigger` for setaddr, identify, setpers, setlabel (`src/net/web_routes.cpp:414`).

## 4. `handleConfigPost` — Config Save Flow

Source: `src/net/web_routes.cpp:222`

```
handleConfigPost(req):
    if hasParam("import"):
        → cfgcore::importJson(body, err) → saveConfig() → {"reboot":true}     // src/net/web_routes.cpp:224-234
    for each CONFIG_FIELDS[i]:
        if hasParam(f.key):
            → cfgcore::setValue(f.key, val, err)
            → check f.flags & CFG_REBOOT → needsReboot = true                 // src/net/web_routes.cpp:248
    for each OUTPUT_FIELDS[i] per output:
        if hasParam(key):
            → cfgcore::setValue(fullKey, val, err)
            → check f.flags & CFG_REBOOT / CFG_LIVE                          // src/net/web_routes.cpp:264-272
    if changed:
        → saveConfig()
        → if outputChangedLive: updateOutputRuntime(o) for all outputs       // src/net/web_routes.cpp:277-278
        → {"reboot": needsReboot, "fields": "..."}
```

- Iterates `CONFIG_FIELDS` (count: `CONFIG_FIELD_COUNT`) at `src/net/web_routes.cpp:241`.
- Iterates `OUTPUT_FIELDS` (count: `OUTPUT_FIELD_COUNT`) at `src/net/web_routes.cpp:257`.
- Both field tables defined in `src/cfg/config_schema.cpp:133,179`.
- Output keys use letter prefix: `a_` for output 0, `b_` for output 1, `c_` for output 2, `d_` for output 3 (`src/net/web_routes.cpp:259,263`).

## 5. `handleRdmJson` — RDM Status Snapshot

Source: `src/net/web_routes.cpp:118`

Produces JSON with:
- `rdmEnabled`: from `cfg.artnetRdm` (`src/net/web_routes.cpp:120`)
- `lineCount`: from `rdmLineCount()` (`src/net/web_routes.cpp:121`)
- `sent`/`recv`: from `rdmSent()`/`rdmRecv()` (`src/net/web_routes.cpp:122-123`)
- `fixturesA`: from `stats().rdmCount` (`src/net/web_routes.cpp:124`)
- `outputs[]`: per-output universe, mergeMode, index (`src/net/web_routes.cpp:125-133`)
- `rdmLines[]`: line-to-output mapping (`src/net/web_routes.cpp:134-144`)
- `bqPolicy`: from `artNet().bqPolicy` (`src/net/web_routes.cpp:150`)
- `artTodReqs`, `artRdmReqs`, `artFlushes`, `artPolls`: from `artNet()` counters (`src/net/web_routes.cpp:158-161`)

## 6. `handleRdmTrigger` — RDM Action Handler

Source: `src/net/web_routes.cpp:397`

| Action Param | Function Called | Source |
|---|---|---|
| `"discover"` | Sets `rdmPollDirty = true` | `src/net/web_routes.cpp:406` |
| `"setaddr"` | `rdmOutSelect(rdmOut)`, `rdmOpSetAddr(uid, addr, &ack)` | `src/net/web_routes.cpp:425-427` |
| `"identify"` | `rdmOutSelect(rdmOut)`, `rdmOpSetIdentify(uid, on, &ack)` | `src/net/web_routes.cpp:435-437` |
| `"setpers"` | `rdmOutSelect(rdmOut)`, `rdmOpSetPersonality(uid, pers, &ack)` | `src/net/web_routes.cpp:447-449` |
| `"setlabel"` | `rdmOutSelect(rdmOut)`, `rdmOpSetString(uid, RDM_PID_DEVICE_LABEL, label, &ack)` | `src/net/web_routes.cpp:459-461` |

- `rdmOut` is the current RDM-capable output index (`src/core/output_init.h:10`).
- `rdmOutSelect` calls `rdmRmtSelect(line)` internally (`src/core/output_init.cpp:29-33`).
- `rdmPollDirty` is checked in `loop()` at `src/main.cpp:141`: `if (rdmPollDirty) { rdmPollDirty = false; rdmSavePoll(); }`.

## 7. `handleOtaGithub` / `handleOtaUrl` — OTA Initiation

Source: `src/net/web_routes.cpp:352,374`

### 7.1 GitHub OTA
```cpp
void handleOtaGithub(AsyncWebServerRequest* req) {
    String ver = req->getParam("version", true)->value();  // src/net/web_routes.cpp:354
    // OR
    String url = req->getParam("url", true)->value();       // src/net/web_routes.cpp:356
    otaTarget = "https://github.com/thinhh0321/LuxDMX/releases/download/v" + ver + "/firmware-esp32-s3.bin";  // src/net/web_routes.cpp:363
    // 302 redirect to /ota
    // xTaskCreatePinnedToCore(ota_gh_task, ..., 0)  // src/net/web_routes.cpp:368
}
```

### 7.7.2 URL OTA
```cpp
void handleOtaUrl(AsyncWebServerRequest* req) {
    otaTarget = req->getParam("url", true)->value();  // src/net/web_routes.cpp:379
    // 302 redirect to /ota
    // xTaskCreatePinnedToCore(ota_url_task, ..., 0)   // src/net/web_routes.cpp:383
}
```

- Both create a FreeRTOS task on **core 0** with 8 KB stack (`src/net/web_routes.cpp:371,386`).
- Both redirect (HTTP 302) to `/ota` which serves `OTA_PROGRESS_PAGE`.
- `otaTarget` is a global `String` declared in `src/sys/sys_platform.h:35`.
- `otaFromGitHub()` and `otaFromUrl()` defined in `src/net/ota.cpp`.

## 8. `handleSetupPost` — Setup Portal Config

Source: `src/net/web_routes.cpp:320`

- Requires `ssid` and `psk` params (`src/net/web_routes.cpp:321`).
- Writes to `cfg.wifiSsid`, `cfg.wifiPsk`, sets `cfg.wifiMode = NET_WIFI_STA` (`src/net/web_routes.cpp:322-324`).
- Calls `saveConfig()` then `ESP.restart()` after 100 ms delay (`src/net/web_routes.cpp:325-328`).
- `NET_WIFI_STA` enum defined in `include/config_enums.h`.

## 9. `handleResetPost` — Factory Reset

Source: `src/net/web_routes.cpp:334`

- Requires `confirm=1` param (`src/net/web_routes.cpp:335`).
- Clears NVS namespace `"dmxgw"` via `Preferences` (`src/net/web_routes.cpp:336-337`).
- `ESP.restart()` after 100 ms (`src/net/web_routes.cpp:338-340`).

## 10. `handleRebootPost` — Soft Reboot

Source: `src/net/web_routes.cpp:346`

- No confirmation required.
- Sends "Rebooting..." then `ESP.restart()` after 100 ms (`src/net/web_routes.cpp:347-349`).

## 11. `handleHealth` — Health Check

Source: `src/net/web_routes.cpp:188`

Returns JSON with:
- `status`: "ok" (`src/net/web_routes.cpp:190`)
- `uptime_s`: `uptimeSec()` (`src/net/web_routes.cpp:191`)
- `heap_dram_free`: `ESP.getFreeHeap()` (`src/net/web_routes.cpp:192`)
- `outputs[]`: per-output id (A/B/C/D), universe, fps, source, signal, rx_frames, rx_loss (`src/net/web_routes.cpp:193-205`)
- `network`: interface name, IP, link speed or RSSI (`src/net/web_routes.cpp:207-215`)
- `alerts`: empty array (placeholder) (`src/net/web_routes.cpp:217`)

## 12. `handleInfoJson` — Device Info

Source: `src/net/web_routes.cpp:81`

Merges config export JSON with firmware metadata:
- Calls `cfgcore::exportJson(j, true)` — strips secrets (`src/net/web_routes.cpp:83`).
- Appends: `version`, `build`, `variant` from `firmware_version.h` (`src/net/web_routes.cpp:88-90`).
- Appends: `uptime_s`, `heap_free`, `rssi`, `eth` flag, `board`, `mcu` (`src/net/web_routes.cpp:91-101`).
- `BOARD_ID` and `MCU_ID` from `src/sys/sys_platform.h:11-12`.
- `firmware_version.h` is at `src/sys/firmware_version.h` (auto-generated by `extra_scripts.py`).

## 13. `handleVersionJson` — Firmware Version

Source: `src/net/web_routes.cpp:106`

Returns: `version`, `build`, `variant`, `latest`, `updateAvailable`, `otaProgress`.
- `latestVersion` and `updateAvailable` are `extern` globals from `src/sys/sys_platform.h:32,36`.
- `otaProgPct` from `src/sys/sys_platform.h:31`.

## 14. `handleLedBright` — LED Brightness

Source: `src/net/web_routes.cpp:522`

- Reads `v` param (0-100), calls `setLedBrightness(constrain(v, 0, 100))` (`src/net/web_routes.cpp:524-525`).
- `setLedBrightness` declared in `src/sys/led_status.h`.

## 15. `handleLabelsGet` / `handleLabelsBody` — Channel Labels

Source: `src/net/web_routes.cpp:291,301`

- `handleLabelsGet` currently returns `"{}"` (stub — labels stored in the frontend) (`src/net/web_routes.cpp:292`).
- `handleLabelsBody` currently returns "ok" (stub) (`src/net/web_routes.cpp:303`).
- The frontend persists labels locally in `localStorage` and sends them via POST (see [Web Frontend](net-web-frontend.md)).

## 16. `handleAutoUpdatePost`

Source: `src/net/web_routes.cpp:295`

- Toggles `cfg.autoUpdate` (`src/net/web_routes.cpp:296`).
- Calls `saveConfig()` (`src/net/web_routes.cpp:297`).
- Returns plain text `autoUpdate=0` or `autoUpdate=1` (`src/net/web_routes.cpp:298`).

## 17. `handleConfigExport` / `handleConfigImport`

### 17.1 Export (`src/net/web_routes.cpp:166`)
- `include_credentials` query param controls secret redaction (`src/net/web_routes.cpp:167-168`).
- Calls `cfgcore::exportJson(j, !includeCreds)` (`src/net/web_routes.cpp:169`).

### 17.2 Import (`src/net/web_routes.cpp:173`)
- Requires `config` body param (`src/net/web_routes.cpp:174`).
- Calls `cfgcore::importJson(body, err)` (`src/net/web_routes.cpp:180`).
- On success: `saveConfig()`, returns "Config imported. Reboot to apply." (`src/net/web_routes.cpp:181-182`).
- On failure: HTTP 400 with error message (`src/net/web_routes.cpp:184`).

## 18. `handleRdmTod` — RDM Table of Devices

Source: `src/net/web_routes.cpp:469`

- Returns count and array of UIDs from `stats().rdmTod[0..rdmCount-1]` (`src/net/web_routes.cpp:470-476`).
- UIDs formatted as hex: `man_id` + `dev_id` without colons (`src/net/web_routes.cpp:473`).

## 19. `handleRdmBqp` — Background Queue Policy

Source: `src/net/web_routes.cpp:479`

- Accepts `p` param (0-4) (`src/net/web_routes.cpp:480,485-487`).
- Sets `artNet().bqPolicy` and `artNet().bqDirty = true` (`src/net/web_routes.cpp:489-490`).
- `bqDirty` is checked in `loop()` at `src/main.cpp:143`: applies to NVS.

## 20. `handleRdmMerge` — Merge Mode

Source: `src/net/web_routes.cpp:494`

- Accepts `out` (0-3) and `mode` (0-4) params (`src/net/web_routes.cpp:495,501-503`).
- Sets `cfg.outputs[out].mergeMode` (`src/net/web_routes.cpp:505`).
- Calls `saveConfig()` — no reboot required (`src/net/web_routes.cpp:506-507`).

## 21. References

- Route registration: [`src/net/web_server.cpp:29-91`](src/net/web_server.cpp#L29)
- Handler declarations: [`src/net/web_routes.h`](src/net/web_routes.h)
- Config save (inline): [`src/cfg/config_core.h:26`](src/cfg/config_core.h#L26)
- Config field tables: [`src/cfg/config_schema.cpp:133,179`](src/cfg/config_schema.cpp#L133)
- RDM poll dirty check: [`src/main.cpp:141`](src/main.cpp#L141)
- BQ dirty check: [`src/main.cpp:143`](src/main.cpp#L143)
- Firmware version header: [`src/sys/firmware_version.h`](src/sys/firmware_version.h)
- OTA init: [`src/net/ota.cpp`](src/net/ota.cpp)
- Rate limiter globals: [`src/net/rate_limiter.cpp:87-88`](src/net/rate_limiter.cpp#L87)
- Output init: [`src/core/output_init.h`](src/core/output_init.h)
- AsyncTCP core pinning: [`platformio.ini:42`](platformio.ini#L42)
