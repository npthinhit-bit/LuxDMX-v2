# Golioth Service Patterns

API patterns for all six Golioth services. Always verify the user's SDK version (from `west.yml`) before providing code — key APIs changed in v0.18, v0.21, and v0.22.

## Table of Contents
1. [Client Initialization](#client-initialization)
2. [Stream (Telemetry)](#stream-telemetry)
3. [LightDB State](#lightdb-state)
4. [Device Settings](#device-settings)
5. [RPC](#rpc)
6. [OTA Firmware Updates](#ota-firmware-updates)
7. [Backend Logging](#backend-logging)

---

## Client Initialization

The `golioth_client` is the central object. Everything else depends on it being initialized and connected.

```c
#include <golioth/client.h>

static struct golioth_client *client;

static void on_client_event(struct golioth_client *client,
                             enum golioth_client_event event,
                             void *arg)
{
    bool connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);
    LOG_INF("Golioth client %s", connected ? "connected" : "disconnected");
}

void main(void)
{
    // Credentials are loaded from settings subsystem (set via shell or provisioning)
    // Never hardcode credentials (removed in v0.18.0)
    const struct golioth_client_config *config = golioth_sample_credentials_get();

    client = golioth_client_create(config);
    golioth_client_register_event_callback(client, on_client_event, NULL);

    // Wait for connection before using services
    golioth_client_wait_for_connect(client, -1);
}
```

**Key Kconfig options** (add to `prj.conf`):
```kconfig
CONFIG_GOLIOTH_SYSTEM_CLIENT=y
CONFIG_GOLIOTH_SAMPLE_COMMON=y   # provides golioth_sample_credentials_get()
CONFIG_GOLIOTH_SAMPLE_WIFI=y     # if using WiFi
```

---

## Stream (Telemetry)

Use for time-series sensor data. Data is CBOR-encoded and routed through Golioth Pipelines.

```c
#include <golioth/stream.h>
#include <zcbor_encode.h>

static void stream_temperature(struct golioth_client *client, float temp)
{
    uint8_t buf[64];
    struct zcbor_state_t zs[1];

    zcbor_new_state(zs, ARRAY_SIZE(zs), buf, sizeof(buf), 1);
    zcbor_map_start_encode(zs, 1);
    zcbor_tstr_put_lit(zs, "temperature");
    zcbor_float32_put(zs, temp);
    zcbor_map_end_encode(zs, 1);

    int err = golioth_stream_set_async(client,
                                       "sensor",
                                       GOLIOTH_CONTENT_TYPE_CBOR,
                                       buf,
                                       zs->payload - buf,
                                       NULL, NULL);
    if (err) {
        LOG_ERR("Stream failed: %d", err);
    }
}
```

**Key Kconfig:**
```kconfig
CONFIG_GOLIOTH_STREAM=y
CONFIG_ZCBOR=y
```

---

## LightDB State

Use for device digital twin — desired state from cloud, reported state from device.

```c
#include <golioth/lightdb_state.h>

// Write device state to cloud
static void report_state(struct golioth_client *client, int value)
{
    char buf[32];
    snprintk(buf, sizeof(buf), "%d", value);

    golioth_lightdb_set_async(client,
                               "counter",
                               GOLIOTH_CONTENT_TYPE_JSON,
                               buf, strlen(buf),
                               NULL, NULL);
}

// Observe cloud-set desired state
static void on_desired(struct golioth_client *client,
                        const struct golioth_response *response,
                        const char *path,
                        const uint8_t *payload,
                        size_t payload_size,
                        void *arg)
{
    if (response->status != GOLIOTH_OK) return;
    LOG_INF("Desired state: %.*s", payload_size, payload);
}

golioth_lightdb_observe_async(client, "desired", GOLIOTH_CONTENT_TYPE_JSON,
                               on_desired, NULL);
```

**Key Kconfig:**
```kconfig
CONFIG_GOLIOTH_LIGHTDB_STATE=y
```

---

## Device Settings

Use for remotely-managed configuration (polling intervals, thresholds, feature flags). Settings sync automatically when the device connects.

```c
#include <golioth/settings.h>

#define DEFAULT_POLL_INTERVAL_MS 5000
static int32_t poll_interval_ms = DEFAULT_POLL_INTERVAL_MS;

static enum golioth_settings_status on_poll_interval(const struct golioth_setting *setting,
                                                       void *arg)
{
    poll_interval_ms = setting->i32;
    LOG_INF("Poll interval updated: %d ms", poll_interval_ms);
    return GOLIOTH_SETTINGS_SUCCESS;
}

// Register after client creation
golioth_settings_register_int(client, "POLL_INTERVAL_MS", on_poll_interval, NULL);
```

**Key Kconfig:**
```kconfig
CONFIG_GOLIOTH_SETTINGS=y
```

> **Thread-safety**: The settings callback fires from the Golioth client thread. Any variable written in the callback and read from the application thread (e.g., `sampling_interval_ms`) must be protected. Use `atomic_set`/`atomic_get` for simple integers, or a mutex for structs. Failing to do this causes a data race that is silent until it isn't.

---

## RPC

Use for cloud-triggered device actions (reboot, get network info, adjust log level, run diagnostics).

```c
#include <golioth/rpc.h>

static enum golioth_rpc_status on_reboot(zcbor_state_t *request_params_array,
                                          zcbor_state_t *response_detail_map,
                                          void *arg)
{
    LOG_WRN("Reboot requested via RPC");
    // Schedule reboot after response is sent
    k_work_schedule(&reboot_work, K_MSEC(500));
    return GOLIOTH_RPC_OK;
}

// Register after client creation
golioth_rpc_register(client, "reboot", on_reboot, NULL);
```

**Key Kconfig:**
```kconfig
CONFIG_GOLIOTH_RPC=y
```

---

## OTA Firmware Updates

**Always include OTA from day one.** On Zephyr/NCS this uses MCUboot. On ESP-IDF it uses the native ESP-IDF OTA mechanism. On ModusToolbox it uses the ModusToolbox DFU mechanism.

### Zephyr/NCS with MCUboot (v0.22.x pattern)

```c
#include <golioth/ota.h>

static void on_ota_manifest(struct golioth_client *client,
                              const struct golioth_ota_manifest *manifest,
                              void *arg)
{
    // Check if there's a newer firmware version
    const struct golioth_ota_component *main_component =
        golioth_ota_find_component(manifest, "main");

    if (!main_component) return;

    if (golioth_ota_component_is_newer(main_component)) {
        LOG_INF("New firmware available: %s", main_component->version);
        // Trigger download and flash
        golioth_ota_download_and_observe(client, main_component, NULL, NULL);
    }
}

// Subscribe to OTA manifest updates (v0.21+ name)
golioth_ota_manifest_subscribe(client, on_ota_manifest, NULL);
```

> **Version note**: Before v0.21.0, this function was called `golioth_ota_observe_manifest_async()`.
> Before v0.18.0, the download callback had a different (blocking) signature.

**Required files for Zephyr OTA:**
- `sysbuild.conf`: enables MCUboot in sysbuild
- `VERSION`: firmware version (used by OTA comparison)
- Board overlay with correct flash partitions for MCUboot

**sysbuild.conf:**
```kconfig
SB_CONFIG_BOOTLOADER_MCUBOOT=y
```

**prj.conf additions:**
```kconfig
CONFIG_GOLIOTH_OTA=y
CONFIG_MCUBOOT_IMG_MANAGER=y
CONFIG_IMG_MANAGER=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_STREAM_FLASH=y
```

**Build with sysbuild:**
```bash
west build -b <board> --sysbuild
```

---

## Backend Logging

Golioth remote logging is a drop-in Zephyr logging backend — no code changes needed, just Kconfig.

```kconfig
CONFIG_GOLIOTH_LOG_BACKEND=y
CONFIG_LOG=y
CONFIG_LOG_BACKEND_GOLIOTH=y
```

> **v0.22 note**: Logs now route through Golioth Pipelines by default. If your project uses a Pipeline configuration, ensure it includes a log routing rule. The `hello` sample's default pipeline works for most cases.

Use standard Zephyr logging macros (`LOG_INF`, `LOG_WRN`, `LOG_ERR`, etc.) — they automatically forward to Golioth when connected.

---

## Composing Multiple Services

When combining services, initialization order matters:

1. Create client (`golioth_client_create`)
2. Register callbacks (settings, RPC, OTA manifest) — before waiting for connect
3. Wait for connection (`golioth_client_wait_for_connect`)
4. Start application work (stream, lightdb reads/writes)

Settings and RPC callbacks registered before connect will fire immediately once the device syncs with the cloud after connecting — this is the intended pattern, not a race condition.
