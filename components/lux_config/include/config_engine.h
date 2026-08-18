#pragma once

#include "esp_err.h"
#include "cJSON.h"

// Configuration field types
typedef enum {
    CFG_TYPE_INT,
    CFG_TYPE_BOOL,
    CFG_TYPE_STRING,
    CFG_TYPE_ENUM
} cfg_type_t;

// Configuration field flags
typedef enum {
    CFG_FLAG_NONE = 0,
    CFG_FLAG_LIVE = (1 << 0),      // Applies immediately
    CFG_FLAG_REBOOT = (1 << 1),    // Requires reboot
    CFG_FLAG_SECRET = (1 << 2),    // Mask in dumps
    CFG_FLAG_NOWEB = (1 << 3),     // Hide from web form
    CFG_FLAG_KEEPNE = (1 << 4)     // Keep if not empty
} cfg_flag_t;

// Configuration field descriptor
typedef struct {
    const char* key;
    const char* json_key;
    cfg_type_t type;
    void* value_ptr;
    int min;
    int max;
    const char* label;
    const char* group;
    uint32_t flags;
} cfg_field_t;

// Configuration engine interface
esp_err_t config_engine_init(void);
esp_err_t config_load(void);
esp_err_t config_save(void);
esp_err_t config_set_value(const char* key, const char* value);
char* config_get_value(const char* key);
esp_err_t config_export_json(cJSON** root);
esp_err_t config_import_json(cJSON* root);
const cfg_field_t* config_get_fields(size_t* count);