#include "config_engine.h"
#include "common.h"
#include "logger.h"
#include "cJSON.h"
#include <string.h>
#include <stdbool.h>

static const char* TAG = "config_json";

static void* json_field_ptr(const CfgField* f) {
    return (char*)&cfg + f->offset;
}

esp_err_t config_export_json(cJSON** root) {
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    *root = cJSON_CreateObject();
    if (!*root) {
        return ESP_ERR_NO_MEM;
    }

    int field_count;
    const CfgField* fields = config_get_fields(&field_count);

    for (int i = 0; i < field_count; i++) {
        const CfgField* field = &fields[i];

        if (field->flags & CFG_FLAG_NOWEB) {
            continue;
        }

        if (field->flags & CFG_FLAG_SECRET && strlen((char*)json_field_ptr(field)) > 0) {
            cJSON_AddStringToObject(*root, field->json_key, "********");
            continue;
        }

        switch (field->type) {
            case CFG_TYPE_INT:
                cJSON_AddNumberToObject(*root, field->json_key, *(int*)json_field_ptr(field));
                break;
            case CFG_TYPE_BOOL:
                cJSON_AddBoolToObject(*root, field->json_key, *(bool*)json_field_ptr(field));
                break;
            case CFG_TYPE_STRING:
                cJSON_AddStringToObject(*root, field->json_key, (char*)json_field_ptr(field));
                break;
            case CFG_TYPE_ENUM:
                cJSON_AddNumberToObject(*root, field->json_key, *(int*)json_field_ptr(field));
                break;
        }
    }

    cJSON_AddStringToObject(*root, "version", "1.0");
    cJSON_AddNumberToObject(*root, "fieldCount", field_count);

    return ESP_OK;
}

esp_err_t config_import_json(cJSON* root) {
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    int field_count;
    const CfgField* fields = config_get_fields(&field_count);

    for (int i = 0; i < field_count; i++) {
        const CfgField* field = &fields[i];
        cJSON* item = cJSON_GetObjectItem(root, field->json_key);

        if (!item) {
            continue;
        }

        if (field->flags & CFG_FLAG_SECRET) {
            continue;
        }

        switch (field->type) {
            case CFG_TYPE_INT:
                if (cJSON_IsNumber(item)) {
                    int value = item->valueint;
                    if (value >= field->min && value <= field->max) {
                        *(int*)json_field_ptr(field) = value;
                    }
                }
                break;
            case CFG_TYPE_BOOL:
                if (cJSON_IsBool(item)) {
                    *(bool*)json_field_ptr(field) = cJSON_IsTrue(item);
                }
                break;
            case CFG_TYPE_STRING:
                if (cJSON_IsString(item)) {
                    char* dst = (char*)json_field_ptr(field);
                    strncpy(dst, item->valuestring, field->max_len - 1);
                    dst[field->max_len - 1] = '\0';
                }
                break;
            case CFG_TYPE_ENUM:
                if (cJSON_IsNumber(item)) {
                    int value = item->valueint;
                    if (value >= field->min && value <= field->max) {
                        *(int*)json_field_ptr(field) = value;
                    }
                }
                break;
        }
    }

    return ESP_OK;
}

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
