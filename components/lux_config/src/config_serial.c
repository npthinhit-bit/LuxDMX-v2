#include "config_engine.h"
#include "common.h"
#include "logger.h"
#include "wifi_manager.h"
#include "wifi_config.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "config_serial";

// Forward declaration
static esp_err_t config_serial_dump(char* response, size_t response_len);
static esp_err_t factory_reset(void);

// Flag to signal reboot requested via serial
static volatile bool serial_reboot_requested = false;

// Check and clear the reboot flag (called by main loop)
bool config_serial_check_reboot(void) {
    if (serial_reboot_requested) {
        serial_reboot_requested = false;
        return true;
    }
    return false;
}

// Serial console command handler
esp_err_t config_serial_handle_command(const char* command, char* response, size_t response_len) {
    if (!command || !response || response_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copy command for parsing
    char cmd[256];
    strncpy(cmd, command, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';

    // Trim whitespace
    char* trimmed = cmd;
    while (isspace((unsigned char)*trimmed)) {
        trimmed++;
    }
    size_t len = strlen(trimmed);
    while (len > 0 && isspace((unsigned char)trimmed[len - 1])) {
        trimmed[--len] = '\0';
    }

    if (len == 0) {
        snprintf(response, response_len, "OK");
        return ESP_OK;
    }

    // Handle commands
    if (strcmp(trimmed, "dump") == 0) {
        return config_serial_dump(response, response_len);
    }
    else if (strncmp(trimmed, "save", 4) == 0 && (trimmed[4] == '\0' || isspace((unsigned char)trimmed[4]))) {
        esp_err_t err = config_save();
        if (err != ESP_OK) {
            snprintf(response, response_len, "ERROR: %s", esp_err_to_name(err));
            return err;
        }
        char* arg = trimmed + 4;
        while (isspace((unsigned char)*arg)) {
            arg++;
        }
        if (strcmp(arg, "reboot") == 0) {
            serial_reboot_requested = true;
            snprintf(response, response_len, "OK rebooting");
        } else {
            snprintf(response, response_len, "OK");
        }
        return err;
    }
    else if (strcmp(trimmed, "load") == 0) {
        esp_err_t err = config_load();
        snprintf(response, response_len, err == ESP_OK ? "OK" : "ERROR: %s", esp_err_to_name(err));
        return err;
    }
    else if (strcmp(trimmed, "reset") == 0) {
        esp_err_t err = config_reset_to_template();
        snprintf(response, response_len, err == ESP_OK ? "OK" : "ERROR: %s", esp_err_to_name(err));
        return err;
    }
    else if (strcmp(trimmed, "wifi") == 0) {
        // Report WiFi status
        net_state_t state = wifi_get_net_state();
        const char* state_str;
        switch (state) {
            case NET_STATE_INIT:        state_str = "INIT"; break;
            case NET_STATE_CONNECTING:  state_str = "CONNECTING"; break;
            case NET_STATE_STATION:     state_str = "STATION"; break;
            case NET_STATE_AP_ONLY:     state_str = "AP_ONLY"; break;
            case NET_STATE_AP_AND_STA:  state_str = "AP_AND_STA"; break;
            case NET_STATE_DISCONNECTED:state_str = "DISCONNECTED"; break;
            default:                    state_str = "UNKNOWN"; break;
        }
        char ip[16] = {0}, netmask[16] = {0}, gateway[16] = {0};
        bool connected = wifi_is_connected();
        esp_err_t ip_err = wifi_get_ip_info(ip, netmask, gateway);
        if (ip_err == ESP_OK) {
            snprintf(response, response_len, "OK state=%s connected=%s ip=%s gw=%s",
                     state_str, connected ? "yes" : "no", ip, gateway);
        } else {
            snprintf(response, response_len, "OK state=%s connected=%s",
                     state_str, connected ? "yes" : "no");
        }
        return ESP_OK;
    }
    else if (strcmp(trimmed, "reboot") == 0) {
        serial_reboot_requested = true;
        snprintf(response, response_len, "OK rebooting");
        return ESP_OK;
    }
    else if (strcmp(trimmed, "help") == 0 || strcmp(trimmed, "?") == 0) {
        snprintf(response, response_len,
            "Available commands:\n"
            "  dump              Print all config fields as key=value\n"
            "  get <key>         Print a single field as key=value\n"
            "  set <key> <value> Set one field\n"
            "  save [reboot]     Persist settings to NVS; reboot if \"reboot\" given\n"
            "  reset             Reset config to board template defaults (in RAM)\n"
            "  load              Reload config from NVS\n"
            "  list [filter]     List all config field keys (optionally filter by substring)\n"
            "  wifi              Show WiFi status (state, connected, ip, gw)\n"
            "  reboot            Trigger system reboot\n"
            "  factory           Factory reset: erase all NVS keys and reset to template\n"
            "  help              Show this help text\n"
            "  key=value         Set one or more fields inline (space/comma separated)\n");
        return ESP_OK;
    }
    else if (strcmp(trimmed, "factory") == 0) {
        esp_err_t err = factory_reset();
        snprintf(response, response_len, err == ESP_OK ? "OK factory reset" : "ERROR: %s", esp_err_to_name(err));
        return err;
    }
    else if (strcmp(trimmed, "list") == 0 || strncmp(trimmed, "list ", 5) == 0) {
        char* filter = (trimmed[4] == '\0') ? NULL : trimmed + 5;
        if (filter) {
            while (isspace((unsigned char)*filter)) filter++;
            size_t flen = strlen(filter);
            while (flen > 0 && isspace((unsigned char)filter[flen - 1])) {
                filter[--flen] = '\0';
            }
            if (*filter == '\0') filter = NULL;
        }
        int field_count;
        const CfgField* fields = config_get_fields(&field_count);
        size_t pos = 0;
        for (size_t i = 0; i < field_count; i++) {
            const CfgField* field = &fields[i];
            if (filter && strstr(field->key, filter) == NULL) {
                continue;
            }
            int written = snprintf(response + pos, response_len - pos, "%s\n", field->key);
            if (written < 0 || (size_t)written >= response_len - pos) {
                break;
            }
            pos += written;
        }
        return ESP_OK;
    }
    else if (strncmp(trimmed, "get ", 4) == 0) {
        const char* key = trimmed + 4;
        char* value = config_get_value(key);
        if (value) {
            snprintf(response, response_len, "%s", value);
            free(value);
            return ESP_OK;
        } else {
            snprintf(response, response_len, "ERROR: Key not found");
            return ESP_ERR_NOT_FOUND;
        }
    }
    else if (strncmp(trimmed, "set ", 4) == 0) {
        char* key = trimmed + 4;
        char* value = strchr(key, '=');
        if (value) {
            *value = '\0';
            value++;

            // Trim key
            char* key_trimmed = key;
            while (isspace((unsigned char)*key_trimmed)) {
                key_trimmed++;
            }
            len = strlen(key_trimmed);
            while (len > 0 && isspace((unsigned char)key_trimmed[len - 1])) {
                key_trimmed[--len] = '\0';
            }

            // Trim value
            char* value_trimmed = value;
            while (isspace((unsigned char)*value_trimmed)) {
                value_trimmed++;
            }
            len = strlen(value_trimmed);
            while (len > 0 && isspace((unsigned char)value_trimmed[len - 1])) {
                value_trimmed[--len] = '\0';
            }

            esp_err_t err = config_set_value(key_trimmed, value_trimmed);
            snprintf(response, response_len, err == ESP_OK ? "OK" : "ERROR: %s", esp_err_to_name(err));
            return err;
        } else {
            snprintf(response, response_len, "ERROR: Invalid set command format");
            return ESP_ERR_INVALID_ARG;
        }
    }
    else {
        snprintf(response, response_len, "ERROR: Unknown command (try help)");
        return ESP_ERR_NOT_FOUND;
    }
}

// Dump configuration to serial console
esp_err_t config_serial_dump(char* response, size_t response_len) {
    if (!response || response_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int field_count;
    const CfgField* fields = config_get_fields(&field_count);

    size_t pos = 0;
    for (size_t i = 0; i < field_count; i++) {
        const CfgField* field = &fields[i];

        // Format: key=value (type) [flags]
        int written = snprintf(response + pos, response_len - pos,
                              "%s=", field->key);
        if (written < 0 || written >= (int)(response_len - pos)) {
            break;
        }
        pos += written;

        // Add value based on type
        switch (field->type) {
            case CFG_TYPE_INT:
                written = snprintf(response + pos, response_len - pos,
                                  "%d", *(int*)((char*)&cfg + field->offset));
                break;
            case CFG_TYPE_BOOL:
                written = snprintf(response + pos, response_len - pos,
                                  "%s", *(bool*)((char*)&cfg + field->offset) ? "true" : "false");
                break;
            case CFG_TYPE_STRING:
                if (field->flags & CFG_FLAG_SECRET && strlen((char*)&cfg + field->offset) > 0) {
                    written = snprintf(response + pos, response_len - pos, "********");
                } else {
                    written = snprintf(response + pos, response_len - pos,
                                      "%s", (char*)&cfg + field->offset);
                }
                break;
            case CFG_TYPE_ENUM:
                written = snprintf(response + pos, response_len - pos,
                                  "%d", *(int*)((char*)&cfg + field->offset));
                break;
        }

        if (written < 0 || written >= (int)(response_len - pos)) {
            break;
        }
        pos += written;

        // Add type
        written = snprintf(response + pos, response_len - pos, " (%s)",
                          field->type == CFG_TYPE_INT ? "int" :
                          field->type == CFG_TYPE_BOOL ? "bool" :
                          field->type == CFG_TYPE_STRING ? "string" : "enum");
        if (written < 0 || written >= (int)(response_len - pos)) {
            break;
        }
        pos += written;

        // Add flags
        if (field->flags & CFG_FLAG_LIVE) {
            written = snprintf(response + pos, response_len - pos, " [LIVE]");
            if (written < 0 || written >= (int)(response_len - pos)) {
                break;
            }
            pos += written;
        }
        if (field->flags & CFG_FLAG_REBOOT) {
            written = snprintf(response + pos, response_len - pos, " [REBOOT]");
            if (written < 0 || written >= (int)(response_len - pos)) {
                break;
            }
            pos += written;
        }
        if (field->flags & CFG_FLAG_SECRET) {
            written = snprintf(response + pos, response_len - pos, " [SECRET]");
            if (written < 0 || written >= (int)(response_len - pos)) {
                break;
            }
            pos += written;
        }

        // Add newline
        written = snprintf(response + pos, response_len - pos, "\n");
        if (written < 0 || written >= (int)(response_len - pos)) {
            break;
        }
        pos += written;
    }

    return ESP_OK;
}
// Factory reset: reset config to template and erase NVS namespace
static esp_err_t factory_reset(void) {
    esp_err_t err = config_reset_to_template();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t nvs_handle;
    err = nvs_open("dmxgw", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    int field_count;
    const CfgField* fields = config_get_fields(&field_count);
    for (size_t i = 0; i < field_count; i++) {
        nvs_erase_key(nvs_handle, fields[i].key);
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    LOG_INFO(TAG, "Factory reset: config restored and NVS cleared");
    return ESP_OK;
}
