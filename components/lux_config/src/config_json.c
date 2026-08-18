#include "config_engine.h"
#include "common.h"
#include "logger.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "config_json";

// Export configuration to JSON
esp_err_t config_export_json(cJSON** root) {
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    *root = cJSON_CreateObject();
    if (!*root) {
        return ESP_ERR_NO_MEM;
    }

    size_t field_count;
    const cfg_field_t* fields = config_get_fields(&field_count);

    for (size_t i = 0; i < field_count; i++) {
        const cfg_field_t* field = &fields[i];

        // Skip fields marked as NOWEB
        if (field->flags & CFG_FLAG_NOWEB) {
            continue;
        }

        // Handle secret fields
        if (field->flags & CFG_FLAG_SECRET && strlen((char*)field->value_ptr) > 0) {
            cJSON_AddStringToObject(*root, field->json_key, "********");
            continue;
        }

        // Add value based on type
        switch (field->type) {
            case CFG_TYPE_INT:
                cJSON_AddNumberToObject(*root, field->json_key, *(int*)field->value_ptr);
                break;
            case CFG_TYPE_BOOL:
                cJSON_AddBoolToObject(*root, field->json_key, *(bool*)field->value_ptr);
                break;
            case CFG_TYPE_STRING:
                cJSON_AddStringToObject(*root, field->json_key, (char*)field->value_ptr);
                break;
            case CFG_TYPE_ENUM:
                cJSON_AddNumberToObject(*root, field->json_key, *(int*)field->value_ptr);
                break;
        }
    }

    // Add metadata
    cJSON_AddStringToObject(*root, "version", "1.0");
    cJSON_AddNumberToObject(*root, "fieldCount", field_count);

    return ESP_OK;
}

// Import configuration from JSON
esp_err_t config_import_json(cJSON* root) {
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t field_count;
    const cfg_field_t* fields = config_get_fields(&field_count);

    for (size_t i = 0; i < field_count; i++) {
        const cfg_field_t* field = &fields[i];
        cJSON* item = cJSON_GetObjectItem(root, field->json_key);

        if (!item) {
            continue;
        }

        // Skip secret fields during import
        if (field->flags & CFG_FLAG_SECRET) {
            continue;
        }

        // Set value based on type
        switch (field->type) {
            case CFG_TYPE_INT:
                if (cJSON_IsNumber(item)) {
                    int value = item->valueint;
                    if (value >= field->min && value <= field->max) {
                        *(int*)field->value_ptr = value;
                    }
                }
                break;
            case CFG_TYPE_BOOL:
                if (cJSON_IsBool(item)) {
                    *(bool*)field->value_ptr = cJSON_IsTrue(item);
                }
                break;
            case CFG_TYPE_STRING:
                if (cJSON_IsString(item)) {
                    strncpy((char*)field->value_ptr, item->valuestring, field->max - 1);
                    ((char*)field->value_ptr)[field->max - 1] = '\0';
                }
                break;
            case CFG_TYPE_ENUM:
                if (cJSON_IsNumber(item)) {
                    int value = item->valueint;
                    if (value >= field->min && value <= field->max) {
                        *(int*)field->value_ptr = value;
                    }
                }
                break;
        }
    }

    return ESP_OK;
}

// Export configuration to JSON string
esp_err_t config_export_json_string(char** json_string) {
    if (!json_string) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON* root;
    esp_err_t err = config_export_json(&root);
    if (err != ESP_OK) {
        return err;
    }

    *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!*json_string) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

// Import configuration from JSON string
esp_err_t config_import_json_string(const char* json_string) {
    if (!json_string) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON* root = cJSON_Parse(json_string);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = config_import_json(root);
    cJSON_Delete(root);

    return err;
}