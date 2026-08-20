/*
 * Native test shim - implementation file
 * Provides minimal stub implementations of ESP-IDF APIs for host-side testing
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "shim.h"
#include "nvs.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "wifi_events.h"
#include "hw.h"

/* ---- NVS in-memory key-value store ---- */

typedef struct {
    char key[64];
    char value[256];
    size_t length;  /* for strings, includes null terminator */
    bool is_string;
    int32_t int_value;  /* for i32 values */
    uint8_t u8_value;   /* for u8 values */
    bool is_u8;
} nvs_entry_t;

static nvs_entry_t nvs_store[64];
static size_t nvs_store_size = 0;
static bool nvs_initialized = false;

static nvs_entry_t* nvs_find(const char* key, bool create) {
    for (size_t i = 0; i < nvs_store_size; i++) {
        if (strcmp(nvs_store[i].key, key) == 0) {
            return &nvs_store[i];
        }
    }
    if (create && nvs_store_size < 64) {
        nvs_entry_t* entry = &nvs_store[nvs_store_size++];
        memset(entry, 0, sizeof(nvs_entry_t));
        strncpy(entry->key, key, sizeof(entry->key) - 1);
        return entry;
    }
    return NULL;
}

esp_err_t nvs_open(const char* namespace, int open_mode, nvs_handle_t* out_handle) {
    (void)namespace;
    (void)open_mode;
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!nvs_initialized) {
        nvs_initialized = true;
    }
    *out_handle = (nvs_handle_t)0x1;  /* Dummy non-null handle */
    return ESP_OK;
}

esp_err_t nvs_close(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value) {
    (void)handle;
    if (!key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_entry_t* entry = nvs_find(key, true);
    if (!entry) {
        return ESP_ERR_NO_MEM;
    }
    entry->is_string = true;
    strncpy(entry->value, value, sizeof(entry->value) - 1);
    entry->length = strlen(value) + 1;
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_str, size_t* length) {
    (void)handle;
    if (!key || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_entry_t* entry = nvs_find(key, false);
    if (!entry || !entry->is_string) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    size_t needed = entry->length;
    if (out_str) {
        if (*length < needed) {
            return ESP_ERR_INVALID_SIZE;
        }
        strncpy(out_str, entry->value, *length - 1);
        out_str[*length - 1] = '\0';
    }
    *length = needed;
    return ESP_OK;
}

esp_err_t nvs_set_i32(nvs_handle_t handle, const char* key, int32_t value) {
    (void)handle;
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_entry_t* entry = nvs_find(key, true);
    if (!entry) {
        return ESP_ERR_NO_MEM;
    }
    entry->is_string = false;
    entry->is_u8 = false;
    entry->int_value = value;
    return ESP_OK;
}

esp_err_t nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out_value) {
    (void)handle;
    if (!key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_entry_t* entry = nvs_find(key, false);
    if (!entry || entry->is_string || entry->is_u8) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out_value = entry->int_value;
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
    (void)handle;
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_entry_t* entry = nvs_find(key, true);
    if (!entry) {
        return ESP_ERR_NO_MEM;
    }
    entry->is_string = false;
    entry->is_u8 = true;
    entry->u8_value = value;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out_value) {
    (void)handle;
    if (!key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_entry_t* entry = nvs_find(key, false);
    if (!entry || entry->is_string || !entry->is_u8) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out_value = entry->u8_value;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) {
    (void)handle;
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < nvs_store_size; i++) {
        if (strcmp(nvs_store[i].key, key) == 0) {
            /* Shift remaining entries down */
            for (size_t j = i; j < nvs_store_size - 1; j++) {
                nvs_store[j] = nvs_store[j + 1];
            }
            nvs_store_size--;
            memset(&nvs_store[nvs_store_size], 0, sizeof(nvs_entry_t));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/* ---- NVS flash ---- */

esp_err_t nvs_flash_init(void) {
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void) {
    nvs_store_size = 0;
    memset(nvs_store, 0, sizeof(nvs_store));
    return ESP_OK;
}

/* ---- System ---- */

static uint32_t mock_free_heap = 4194304;  /* 4MB default */

uint32_t esp_get_free_heap_size(void) {
    return mock_free_heap;
}

void esp_restart(void) {
    /* In test mode, just log the restart */
    printf("[TEST] System restart requested\n");
}

/* ---- FreeRTOS ---- */

void vTaskDelay(unsigned long xTicksToDelay) {
    /* No-op in native tests */
    (void)xTicksToDelay;
}

/* ---- GPIO ---- */

static int gpio_levels[49] = {0};  /* 0-48 */
static bool gpio0_force_low = false;  /* For portal testing */

void gpio_config(const gpio_config_t* config) {
    (void)config;
    /* No-op in test mode */
}

void gpio_set_level(gpio_num_t gpio_num, int level) {
    if (gpio_num >= 0 && gpio_num < 49) {
        gpio_levels[gpio_num] = level;
    }
}

int gpio_get_level(gpio_num_t gpio_num) {
    if (gpio_num >= 0 && gpio_num < 49) {
        /* Simulate GPIO0 forced low for testing */
        if (gpio_num == 0) {
            return gpio0_force_low ? 0 : 1;
        }
        return gpio_levels[gpio_num];
    }
    return 0;
}

/* ---- Test control functions ---- */

/* Mock state: declared here, before first use (was defined-after-use; in C, file-scope statics are not visible before their declaration point). */
static esp_chip_info_t mock_chip_info = {
    .model = CHIP_ESP32,
    .revision = 1,
    .cores = 2,
};
static bool mock_psram_enabled = false;
static char mock_scan_ssids[10][32] = {0};
static int mock_scan_count = 0;

void test_shim_set_gpio0_low(bool low) {
    gpio0_force_low = low;
}

void test_shim_set_chip_model(int model) {
    mock_chip_info.model = model;
}

void test_shim_set_psram_enabled(bool enabled) {
    mock_psram_enabled = enabled;
}

void test_shim_set_scan_networks(const char** networks, int count) {
    mock_scan_count = 0;
    for (int i = 0; i < count && i < 10; i++) {
        strncpy(mock_scan_ssids[i], networks[i], 31);
        mock_scan_count++;
    }
}

void test_shim_reset_nvs(void) {
    nvs_store_size = 0;
    memset(nvs_store, 0, sizeof(nvs_store));
}

/* ---- ESP ROM ---- */

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

void esp_rom_delay_us(unsigned int us) {
#ifdef _WIN32
    Sleep(us ? (us + 999) / 1000 : 0);
#else
    usleep(us);
#endif
}

/* ---- ESP HTTP Server ---- */

esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* config) {
    (void)config;
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    *handle = (httpd_handle_t)0x1;
    return ESP_OK;
}

esp_err_t httpd_stop(httpd_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* uri) {
    (void)handle;
    (void)uri;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t* req, const char* buffer, int len) {
    (void)req;
    (void)buffer;
    (void)len;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) {
    (void)req;
    (void)type;
    return ESP_OK;
}

esp_err_t httpd_resp_send_500(httpd_req_t* req) {
    (void)req;
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t* req, int err_code, const char* msg) {
    (void)req;
    (void)err_code;
    (void)msg;
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t* req, char* buf, int len) {
    (void)req;
    (void)buf;
    (void)len;
    return 0;
}

/* ---- ESP Chip Info ---- */

void esp_chip_info(esp_chip_info_t* info) {
    if (info) {
        *info = mock_chip_info;
    }
}

/* ---- ESP PSRAM ---- */

bool esp_psram_is_initialized(void) {
    return mock_psram_enabled;
}

/* ---- Heap Caps ---- */

static size_t mock_spram_total = 0;

size_t heap_caps_get_total_size(int caps) {
    (void)caps;
    return mock_spram_total;
}

size_t heap_caps_get_free_size(int caps) {
    (void)caps;
    return mock_spram_total;
}

/* ---- ESP NetIF ---- */

esp_err_t esp_netif_init(void) {
    return ESP_OK;
}

esp_netif_t esp_netif_create_default_wifi_sta(void) {
    return (esp_netif_t)0x1;
}

esp_netif_t esp_netif_create_default_wifi_ap(void) {
    return (esp_netif_t)0x2;
}

esp_err_t esp_netif_get_ip_info(const esp_netif_t* netif, esp_netif_ip_info_t* ip_info) {
    (void)netif;
    if (!ip_info) {
        return ESP_ERR_INVALID_ARG;
    }
    ip_info->ip = 0;
    ip_info->gw = 0;
    ip_info->netmask = 0;
    return ESP_OK;
}

/* ---- ESP Event ---- */

esp_err_t esp_event_loop_create_default(void) {
    return ESP_OK;
}

esp_err_t esp_event_handler_register(int event_base, int32_t event_id, esp_event_handler_t handler) {
    (void)event_base;
    (void)event_id;
    (void)handler;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(int event_base, int32_t event_id,
                                               esp_event_handler_t handler, void* ctx) {
    (void)event_base;
    (void)event_id;
    (void)handler;
    (void)ctx;
    return ESP_OK;
}

/* ---- WiFi Events ---- */

esp_err_t wifi_events_init(wifi_event_cb_t handler) {
    (void)handler;
    return ESP_OK;
}

/* ---- ESP WiFi ---- */

static wifi_config_t mock_sta_config;
static wifi_config_t mock_ap_config;
static int mock_wifi_mode = WIFI_MODE_STA;
static bool wifi_connected = false;

esp_err_t esp_wifi_init(const void* config) {
    (void)config;
    memset(&mock_sta_config, 0, sizeof(mock_sta_config));
    memset(&mock_ap_config, 0, sizeof(mock_ap_config));
    return ESP_OK;
}

esp_err_t esp_wifi_start(void) {
    wifi_connected = false;
    return ESP_OK;
}

esp_err_t esp_wifi_set_mode(int mode) {
    mock_wifi_mode = mode;
    return ESP_OK;
}

esp_err_t esp_wifi_get_mode(int* mode) {
    if (!mode) {
        return ESP_ERR_INVALID_ARG;
    }
    *mode = mock_wifi_mode;
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(int ifx, const wifi_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ifx == WIFI_IF_STA) {
        mock_sta_config = *config;
    } else if (ifx == WIFI_IF_AP) {
        mock_ap_config = *config;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_get_config(int ifx, wifi_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ifx == WIFI_IF_STA) {
        *config = mock_sta_config;
    } else if (ifx == WIFI_IF_AP) {
        *config = mock_ap_config;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_connect(void) {
    wifi_connected = true;
    return ESP_OK;
}

esp_err_t esp_wifi_disconnect(void) {
    wifi_connected = false;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_start(const void* config, bool block) {
    (void)config;
    (void)block;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_num(uint16_t* num) {
    if (!num) {
        return ESP_ERR_INVALID_ARG;
    }
    *num = mock_scan_count;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t* num, wifi_ap_record_t* records) {
    if (!num || !records) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < *num && i < mock_scan_count; i++) {
        memset(records[i].ssid, 0, sizeof(records[i].ssid));
        strncpy((char*)records[i].ssid, mock_scan_ssids[i], 31);
        records[i].rssi = -50 - i * 10;
        records[i].authmode = 3;
        records[i].channel = 6;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t* ap_info) {
    if (!ap_info) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(ap_info, 0, sizeof(*ap_info));
    strncpy((char*)ap_info->ssid, "TestNetwork", 31);
    ap_info->rssi = -45;
    return ESP_OK;
}

/* ---- FreeRTOS Semaphore shim ---- */
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (SemaphoreHandle_t)0x1;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, unsigned long xTicksToWait) {
    (void)xSemaphore;
    (void)xTicksToWait;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    (void)xSemaphore;
    return pdTRUE;
}

unsigned long long esp_log_timestamp(void) {
    static unsigned long long ts = 0;
    return ts++ * 100;
}

void esp_log_level_set(const char* tag, esp_log_level_t level) {
    (void)tag;
    (void)level;
}

/* ---- Task creation shim ---- */
void xTaskCreate(TaskFunction_t pvTaskCode, const char* const pcName,
                 unsigned short usStackDepth, void* pvParameters,
                 unsigned char ucPriority, TaskHandle_t* pvCreatedTask) {
    (void)pvTaskCode;
    (void)pcName;
    (void)usStackDepth;
    (void)pvParameters;
    (void)ucPriority;
    if (pvCreatedTask) {
        *pvCreatedTask = (TaskHandle_t)0x1;
    }
}

void vTaskDelete(TaskHandle_t xTask) {
    (void)xTask;
}

void vTaskDelayUntil(unsigned long* pxPreviousWakeTime, unsigned long xTimeIncrement) {
    (void)pxPreviousWakeTime;
    (void)xTimeIncrement;
}

/* ---- Captive Portal ---- */

static bool captive_portal_active = false;

esp_err_t captive_portal_start(void) {
    captive_portal_active = true;
    return ESP_OK;
}

esp_err_t captive_portal_stop(void) {
    captive_portal_active = false;
    return ESP_OK;
}

bool captive_portal_is_active(void) {
    return captive_portal_active;
}

/* ---- Web WebSocket ---- */

esp_err_t web_websocket_init(httpd_handle_t server) {
    (void)server;
    return ESP_OK;
}

esp_err_t web_websocket_send_status(void) {
    return ESP_OK;
}

/* ---- Web Assets ---- */

const char* web_assets_get(const char* uri) {
    (void)uri;
    return "";
}

/* ---- Web Frontend ---- */

const char* web_frontend_get_index(void) { return ""; }
const char* web_frontend_get_config(void) { return ""; }
const char* web_frontend_get_setup(void) { return ""; }

/* ---- Web Routes ---- */

esp_err_t web_routes_register(httpd_handle_t server) {
    (void)server;
    return ESP_OK;
}

/* ---- Web Server ---- */

static httpd_handle_t mock_server = NULL;

esp_err_t web_server_init(void) {
    mock_server = (httpd_handle_t)0x1;
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    return ESP_OK;
}

esp_err_t web_server_stop(void) {
    mock_server = NULL;
    return ESP_OK;
}

httpd_handle_t web_server_get_handle(void) {
    return mock_server;
}

/* ---- ESP32 features that wifi_events.c may need ---- */

esp_err_t esp_wifi_set_ps(bool enable) {
    (void)enable;
    return ESP_OK;
}

void esp_wifi_set_max_tx_power(int8_t power) {
    (void)power;
}

/* ---- Hardware abstraction stubs (host-native only) ---- */

esp_err_t hw_init(void) {
    return ESP_OK;
}

board_type_t hw_get_board_type(void) {
    return BOARD_ESP32DEV;
}

const board_config_t* hw_get_board_config(void) {
    static const board_config_t cfg = {
        .type = BOARD_ESP32DEV,
        .name = "ESP32-DevKit",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_gpio = 2,
        .net_if = NET_IF_WIFI,
    };
    return &cfg;
}

/* ---- ESP Timer ---- */

int64_t esp_timer_get_time(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (int64_t)count.QuadPart * 1000000LL / (int64_t)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
#endif
}

uint32_t esp_timer_get_idle_loop_time_since(uint64_t last) {
    (void)last;
    return 0;
}

/* ---- Scene Engine stubs (spec 08; not yet implemented, referenced by merge_engine) ---- */

void sceneRecall(int idx, uint16_t fadeMs, int outIdx) {
    (void)idx;
    (void)fadeMs;
    (void)outIdx;
}

void sceneRecallHome(int outIdx) {
    (void)outIdx;
}

/* ---- GCC __sync_synchronize built-in (MSVC does not provide it) ---- */

#ifdef _WIN32
#include <windows.h>
void __sync_synchronize(void) {
    MemoryBarrier();
}
#endif
