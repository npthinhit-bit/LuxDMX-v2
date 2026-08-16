# WebSocket Protocol

This document describes the LuxDMX V2 WebSocket protocol: the binary DMX frame
pushed by the firmware at ~10 Hz, the periodic text meta frame, the text/JSON
command set accepted from browser clients, and the REST API endpoints exposed
by the HTTP server.

**WebSocket endpoint:** `ws://<device-ip>/ws`

---

## Binary Frame (DMX push, ~10 Hz)

The firmware pushes a fixed-size binary frame to every subscribed WebSocket
client at approximately 10 Hz (every 100 ms). The frame is **2095 bytes**
total and is assembled by `wsBuildFrame()` (`src/net/ws_frame.cpp`).

### Layout Summary

| Offset | Size | Field                   | Type      |
|--------|------|-------------------------|-----------|
| 0      | 16   | Header                  | struct    |
| 16     | 2048 | DMX data                | 4 x 512   |
| 2064   | 20   | Per-output stats        | 4 outputs |
| 2084   | 1    | Changed-universe bitmap | uint8     |
| 2085   | 10   | RDM tail                | struct    |

### Header (bytes 0-15)

All multi-byte fields are **big-endian**.

| Offset | Size | Field      | Encoding            | Description                                            |
|--------|------|------------|---------------------|--------------------------------------------------------|
| 0      | 2    | fps        | uint16 (big-endian) | Overall output FPS x 10 (divide by 10 for actual FPS) |
| 2      | 2    | rssi       | int16 (big-endian)  | WiFi RSSI dBm, or Ethernet link speed (Mbps)           |
| 4      | 4    | heap       | uint32 (big-endian) | Free heap in bytes (`ESP.getFreeHeap()`)               |
| 8      | 4    | uptime     | uint32 (big-endian) | Uptime in seconds                                     |
| 12     | 1    | senders    | uint8               | Count of active network senders                       |
| 13     | 1    | srcStatus  | uint8               | Source status: 0=normal, 1=conflict, 2=merging         |
| 14     | 2    | jitter     | uint16 (big-endian) | Inter-frame arrival jitter (ms x 10)                  |

### DMX Data (bytes 16-2063)

4 outputs x 512 channels = 2048 bytes. The DMX **start code** (slot 0) is
**not** included -- each 512-byte block contains data slots 1-512 only.

| Output | Byte offset | Size |
|--------|-------------|------|
| 0      | 16          | 512  |
| 1      | 528         | 512  |
| 2      | 1040        | 512  |
| 3      | 1552        | 512  |

### Per-Output Stats (bytes 2064-2083)

20 bytes total, organized as three parallel arrays (not interleaved per output):

**Output FPS** (bytes 2064-2071): 4 x uint16 BE -- `outFps[i] = raw / 10.0`
**Input FPS** (bytes 2072-2079): 4 x uint16 BE -- `inFps[i] = raw / 10.0`
**TX Style** (bytes 2080-2083): 4 x uint8, one byte per output

TX style flag bits:

| Bit | Value | Meaning                                          |
|-----|-------|--------------------------------------------------|
| 0   | 0x01  | Delta mode (one DMX frame per input packet)      |
| 1   | 0x02  | TX source is Art-Net (vs sACN or local)          |
| 2   | 0x04  | Reserved                                         |
| 3   | 0x08  | Reserved                                         |

### Changed-Universe Bitmap (byte 2084)

| Bit | Universe |
|-----|----------|
| 0   | Output 0  |
| 1   | Output 1  |
| 2   | Output 2  |
| 3   | Output 3  |

Bit `i` is set when output `i`'s DMX data changed since the last
`wsBuildFrame()` call. Used by `wsPush()` for per-client delta filtering.
The byte is included in the binary frame payload but is not consumed by the
frontend.

### RDM Tail (bytes 2085-2094)

| Offset | Size | Field      | Type       | Description                              |
|--------|------|------------|------------|------------------------------------------|
| 2085   | 2    | rdmCount   | uint16 BE  | Number of RDM devices discovered         |
| 2087   | 4    | rdmSent    | uint32 BE  | Total RDM request messages sent          |
| 2091   | 4    | rdmRecv    | uint32 BE  | Total RDM response messages received     |

---

## Text Frame: Meta Push (~2 Hz)

Every 2 seconds, the firmware pushes a JSON text frame to all connected
clients:

```json
{"meta":1,"senders":[...],"log":[...]}
```

| Field    | Description                                                  |
|----------|--------------------------------------------------------------|
| meta     | Always `1` (frame type identifier)                           |
| senders  | Array of active senders (ip, proto, universe, priority, fps) |
| log      | Array of recent log entries (up to 32 entries)               |

---

## Text Frame Commands (Browser to Firmware)

Commands are JSON objects sent as WebSocket text frames. The handler
(`handleWsText` in `src/net/ws_handler.cpp`) matches substrings, so field
ordering is flexible but key names must match exactly.

### subscribe

Select which universes (outputs) this client receives. Replaces the default
(all 4 subscribed on connect).

```json
{"cmd":"subscribe","universes":[0,1]}
```

| Field      | Type    | Range | Description                     |
|------------|---------|-------|---------------------------------|
| universes  | array[] | 0-3   | Output indices to subscribe to  |

### viewout

Select the monitor output targeted by subsequent `set`, `blackout`, and
`saveScene` commands.

```json
{"cmd":"viewout","out":0}
```

| Field | Type | Range | Description                     |
|-------|------|-------|---------------------------------|
| out   | int  | 0-3   | Output index (must be enabled)  |

### blackout

Zero all 512 data channels (slots 1-512) on the current monitor output.
The start code (slot 0) is preserved.

```json
{"cmd":"blackout"}
```

### mode

Toggle manual mode. When enabled, the gateway holds current DMX values
instead of clearing them when all sources go silent.

```json
{"cmd":"mode","val":true}
```

| Field | Type  | Description                          |
|-------|-------|--------------------------------------|
| val   | bool  | `true` to enable manual mode         |

### identify (LED)

Trigger LED identification on a specific channel of the monitor output
for `IDENTIFY_MS` (approximately 5 seconds).

```json
{"cmd":"identify","ch":42}
```

| Field | Type | Range   | Description                  |
|-------|------|---------|------------------------------|
| ch    | int  | 1-512   | Channel number to identify   |

### set

Set a single DMX channel value on the current monitor output.

```json
{"cmd":"set","ch":42,"val":128}
```

| Field | Type | Range  | Description     |
|-------|------|--------|-----------------|
| ch    | int  | 1-512  | Channel number  |
| val   | int  | 0-255  | Channel value   |

### scene

Trigger a stored scene with an optional fade time.

```json
{"cmd":"scene","play":0,"fade":1000}
```

| Field | Type | Description                          |
|-------|------|--------------------------------------|
| play  | int  | Scene index to trigger               |
| fade  | int  | Fade time in ms (optional, default 0) |

### saveScene

Snapshot the current DMX data of the monitor output into a scene slot and
persist to NVS.

```json
{"cmd":"saveScene","idx":0,"name":"My Scene"}
```

| Field | Type   | Description                      |
|-------|--------|----------------------------------|
| idx   | int    | Scene index (0 - MAX_SCENES-1)   |
| name  | string | Optional scene label (max 31)    |

### clearScene

Erase a stored scene from NVS.

```json
{"cmd":"clearScene","idx":0}
```

| Field | Type | Description              |
|-------|------|--------------------------|
| idx   | int  | Scene index to erase     |

### RDM Commands

All RDM commands must include the `"rdm"` key. Actions are queued by
`handleWsTextRdm()` and executed in `loop()` via `rdmWsProcessQueued()` to
avoid blocking the WebSocket handler during RMT operations.

#### discover

Trigger RDM discovery on the selected RDM output.

```json
{"cmd":"rdm","discover":true}
```

#### setaddr

Set the DMX address of a discovered RDM device.

```json
{"cmd":"rdm","setaddr":true,"uid":"434100000001","addr":1}
```

| Field | Type   | Format          | Description               |
|-------|--------|-----------------|---------------------------|
| uid   | string | 12 hex chars    | 4-char mfr ID + 8-char dev |
| addr  | int    | 0-512           | New DMX start address     |

#### identify (RDM)

Toggle RDM identify mode on a device (its LEDs flash).

```json
{"cmd":"rdm","identify":true,"uid":"434100000001"}
```

> **Note:** Both LED identify and RDM identify match the `"identify"` keyword.
> The LED identify handler runs first and will intercept any message containing
> `"identify"` without a `"ch"` field. RDM identify is best sent via the
> REST endpoint `GET /rdm/identify` instead of WebSocket.

| Field    | Type  | Description                            |
|----------|-------|----------------------------------------|
| identify | bool  | `true` to start, `false` to stop       |
| uid      | string| 12 hex chars                           |

#### setpers

Set the active personality (operating mode) of an RDM device.

```json
{"cmd":"rdm","setpers":true,"uid":"434100000001","pers":1}
```

| Field | Type   | Description       |
|-------|--------|-------------------|
| uid   | string | 12 hex chars      |
| pers  | int    | Personality index |

#### setlabel

Set the device label of an RDM responders.

```json
{"cmd":"rdm","setlabel":true,"uid":"434100000001","label":"Spot 1"}
```

| Field | Type   | Description                      |
|-------|--------|----------------------------------|
| uid   | string | 12 hex chars                     |
| label | string | Device label (max 32 chars)      |

#### UID Format

The UID is a 12-character hex string: 4 characters for the manufacturer ID
followed by 8 characters for the device ID. Example: `434100000001`.

---

## Subscription and Delta Semantics

- On connect, a client is subscribed to **all 4 universes** (subscription
  mask `0x000F`).
- The `subscribe` command **replaces** the entire subscription mask.
- Per-client delta filtering: if no subscribed universe changed AND the client
  already received the current `wsFrameSeq`, the frame is skipped for that
  client (see `wsPush()` in `src/net/websocket.cpp`).
- The changed-universe bitmap (byte 2084) drives this filtering. It is computed
  in `wsBuildFrame()` and read by `wsPush()`.

### Frame Sequence Counter

`wsFrameSeq` (uint32, incremented per `wsBuildFrame()` call) is stored per
client as `wsClientFrameSeq[slot]`. This lets the push loop skip clients that
are already up-to-date for the current frame sequence.

### Client Slot Management

Clients are assigned to slots via `client->id() % WS_MAX_CLIENTS` (max 12).
Stale slots (disconnected clients) are cleared on disconnect.

---

## REST API

The HTTP server runs on port 80. All routes are registered in
`webRegisterRoutes()` (`src/net/web_server.cpp`). JSON responses include
`Cache-Control: no-store`.

### Static Assets

| Method | Path                 | Handler               | Content-Type        |
|--------|----------------------|-----------------------|---------------------|
| GET    | `/logo.webp`         | `handleLogo`          | `image/webp`        |
| GET    | `/favicon.png`       | `handleFavicon`       | `image/png`         |
| GET    | `/favicon.ico`       | `handleFavicon`       | `image/png`         |
| GET    | `/bootstrap.min.css` | `handleBootstrapCss`  | `text/css` (gzip)   |

### Web App Pages

| Method | Path      | Handler              |
|--------|-----------|----------------------|
| GET    | `/`       | `handleRoot`         |
| GET    | `/config` | `handleConfigGet`    |
| GET    | `/rdm`    | `handleRdmPage`      |
| GET    | `/setup`  | `handleSetupGet`     |
| GET    | `/reset`  | `handleResetGet`     |
| GET    | `/ota`    | `handleOtaStatus`    |

### JSON Data Endpoints

| Method | Path                | Handler              | Description                               |
|--------|---------------------|----------------------|-------------------------------------------|
| GET    | `/dmx.json`         | `handleDmxJson`      | Per-output DMX data + status              |
| GET    | `/senders.json`     | `handleSendersJson`  | Active senders list                       |
| GET    | `/log.json`         | `handleLogJson`      | Recent log entries                        |
| GET    | `/info.json`        | `handleInfoJson`     | System info (version, heap, network)      |
| GET    | `/version.json`     | `handleVersionJson`  | Firmware version + update status          |
| GET    | `/health`           | `handleHealth`       | Health check + per-output status          |
| GET    | `/rdm.json`         | `handleRdmJson`      | RDM subsystem status                      |
| GET    | `/rdm/tod`          | `handleRdmTod`       | RDM Table of Devices                      |
| GET    | `/diag/soak-stats`  | inline lambda        | Soak test DRAM/PSRAM stats                |
| GET    | `/labels.json`      | `handleLabelsGet`    | Label data                                |

### Config Endpoints

| Method | Path               | Handler               | Description                              |
|--------|--------------------|-----------------------|------------------------------------------|
| GET    | `/config/export`   | `handleConfigExport`  | Export full config as JSON               |
| POST   | `/config`          | `handleConfigPost`    | Update config fields (form parameters)   |
| POST   | `/config/import`   | `handleConfigImport`  | Import config (POST param: `config`)     |

### Setup Portal Endpoints

| Method | Path            | Handler              | Description                                   |
|--------|-----------------|----------------------|-----------------------------------------------|
| GET    | `/setup/scan`   | `handleSetupScan`    | Scan for visible WiFi networks                |
| POST   | `/setup`        | `handleSetupPost`    | Save WiFi credentials (params: `ssid`, `psk`) |

### Reset and Reboot Endpoints

| Method | Path      | Handler            | Description                              |
|--------|-----------|--------------------|------------------------------------------|
| POST   | `/reset`  | `handleResetPost`  | Factory reset (param: `confirm=1`)      |
| POST   | `/reboot` | `handleRebootPost` | Reboot the device                        |

### OTA Endpoints

| Method | Path               | Handler               | Description                              |
|--------|--------------------|-----------------------|------------------------------------------|
| POST   | `/ota/github`      | `handleOtaGithub`     | OTA from GitHub release (param: version) |
| POST   | `/ota/url`         | `handleOtaUrl`        | OTA from URL (param: url)                |
| POST   | `/ota/upload`      | `otaUploadChunk`      | Upload firmware binary (multipart)       |
| GET    | `/ota/status`      | `handleOtaStatusJson` | OTA progress JSON                        |

### RDM Control Endpoints

| Method | Path             | Handler              | Description                                  |
|--------|------------------|----------------------|----------------------------------------------|
| GET    | `/rdm/discover`  | `handleRdmTrigger`   | Start RDM discovery                          |
| GET    | `/rdm/setaddr`   | `handleRdmTrigger`   | Set RDM device address (params: uid, addr)   |
| GET    | `/rdm/identify`  | `handleRdmTrigger`   | Toggle RDM identify (params: uid, on)        |
| GET    | `/rdm/setpers`   | `handleRdmTrigger`   | Set RDM personality (params: uid, pers)      |
| GET    | `/rdm/setlabel`  | `handleRdmTrigger`   | Set RDM label (params: uid, label)           |
| GET    | `/rdm/bqp`       | `handleRdmBqp`       | Set background queue policy (param: p)       |
| GET    | `/rdm/merge`     | `handleRdmMerge`     | Set output merge mode (params: out, mode)    |

### LED and Misc Endpoints

| Method | Path             | Handler                | Description                                  |
|--------|------------------|------------------------|----------------------------------------------|
| GET    | `/led/bright`    | `handleLedBright`      | Set LED brightness (param: v, 0-100)         |
| POST   | `/labels`        | `handleLabelsBody`     | Update labels (body upload)                  |
| POST   | `/autoupdate`    | `handleAutoUpdatePost` | Toggle auto-update flag                      |

---

## Frame Delivery Notes

- **Binary DMX push** uses `c->binary(wsBuf, WS_FRAME_LEN)` to send to each
  connected client. The frame buffer is static (allocated once, reused every
  push) to avoid heap fragmentation under Art-Net flood (issue #64).
- **Meta push** uses `ws.textAll(m)` to broadcast the JSON meta frame.
- A per-client health check (`c->status() == WS_CONNECTED`, `c->canSend()`)
  guards against sending to stale or unreachable clients.
- If free heap drops below 40 KB or max-alloc heap below 24 KB, the meta
  push is suppressed (but binary DMX push continues).
