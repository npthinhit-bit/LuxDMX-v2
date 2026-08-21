#include "config_engine.h"
#include "common.h"
#include "logger.h"
#include "boards.h"
#include "hw.h"
#include "config_schema.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

static const char* TAG = "config_engine";
static const char* NVS_NAMESPACE = "dmxgw";
static bool config_initialized = false;

static void* cfg_field_ptr(const CfgField* f) {
    return (char*)&cfg + f->offset;
}

static void* cfg_output_ptr(const CfgOutputField* f, int idx) {
    return (char*)&cfg.outputs[idx] + f->offset;
}

static void apply_int_value(const CfgField* f, const char* val) {
    char* end;
    long v = strtol(val, &end, 0);
    if (*end == '\0' && v >= f->min && v <= f->max)
        *(int*)cfg_field_ptr(f) = (int)v;
}

static void apply_str_value(const CfgField* f, const char* val) {
    char* dst = (char*)cfg_field_ptr(f);
    strncpy(dst, val, f->max_len - 1);
    dst[f->max_len - 1] = '\0';
}

static void apply_bool_value(const CfgField* f, const char* val) {
    bool b = (strcmp(val,"true")==0 || strcmp(val,"1")==0);
    *(bool*)cfg_field_ptr(f) = b;
}

static void apply_output_int(const CfgOutputField* f, int idx, const char* val) {
    char* end;
    long v = strtol(val, &end, 0);
    if (*end == '\0' && v >= f->min && v <= f->max)
        *(int*)cfg_output_ptr(f, idx) = (int)v;
}

static void apply_output_str(const CfgOutputField* f, int idx, const char* val) {
    char* dst = (char*)cfg_output_ptr(f, idx);
    strncpy(dst, val, f->max_len - 1);
    dst[f->max_len - 1] = '\0';
}

static void apply_output_bool(const CfgOutputField* f, int idx, const char* val) {
    bool b = (strcmp(val,"true")==0 || strcmp(val,"1")==0);
    *(bool*)cfg_output_ptr(f, idx) = b;
}

esp_err_t config_apply_template_text(const char* text) {
    if (!text) return ESP_ERR_INVALID_ARG;
    char buf[512];
    const char* p = text;
    while (*p) {
        /* Parse one line */
        char* bp = buf;
        while (*p && *p != '\n' && *p != '\r' && bp < buf + sizeof(buf) - 1)
            *bp++ = *p++;
        *bp = '\0';
        if (*p) p++; /* skip newline */
        /* Skip comments and blanks */
        while (*buf == ' ' || *buf == '\t') memmove(buf, buf+1, strlen(buf));
        if (*buf == '#' || *buf == '\0') continue;
        /* Handle extends= */
        if (strncmp(buf, "extends=", 8) == 0) {
            char* name = buf + 8;
            while (*name == ' ' || *name == '\t') name++;
            if (strcmp(name, "_base") == 0) {
                config_apply_template_text(_BOARD_TEMPLATE_BASE);
            }
            continue;
        }
        /* Parse key=value */
        char* eq = strchr(buf, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = buf;
        char* val = eq + 1;
        /* Trim whitespace from key */
        while (*key == ' ' || *key == '\t') key++;
        char* ke = key + strlen(key);
        while (ke > key && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = '\0';
        /* Trim whitespace from val */
        while (*val == ' ' || *val == '\t') val++;
        char* ve = val + strlen(val);
        while (ve > val && (ve[-1] == ' ' || *ve == '\t')) *--ve = '\0';

        /* Check if per-output key (a_/b_/c_/d_ prefix) */
        if (strlen(key) > 2 && key[1] == '_') {
            char letter = key[0];
            char* suffix = key + 2;
            int out_idx = -1;
            switch (letter) {
                case 'a': out_idx = 0; break;
                case 'b': out_idx = 1; break;
                case 'c': out_idx = 2; break;
                case 'd': out_idx = 3; break;
                default: out_idx = -1; break;
            }
            if (out_idx >= 0 && out_idx < MAX_OUTPUTS) {
                int fc;
                const CfgOutputField* of = config_get_output_fields(&fc);
                for (int i = 0; i < fc; i++) {
                    if (strcmp(of[i].key_suffix, suffix) == 0) {
                        switch (of[i].type) {
                            case CFG_TYPE_INT:   apply_output_int(&of[i], out_idx, val); break;
                            case CFG_TYPE_BOOL:  apply_output_bool(&of[i], out_idx, val); break;
                            case CFG_TYPE_STRING: apply_output_str(&of[i], out_idx, val); break;
                            default: break;
                        }
                        break;
                    }
                }
            }
            continue;
        }
        /* Global key */
        int fc;
        const CfgField* f = config_get_fields(&fc);
        for (int i = 0; i < fc; i++) {
            if (strcmp(f[i].key, key) == 0) {
                switch (f[i].type) {
                    case CFG_TYPE_INT:    apply_int_value(&f[i], val); break;
                    case CFG_TYPE_BOOL:   apply_bool_value(&f[i], val); break;
                    case CFG_TYPE_STRING: apply_str_value(&f[i], val); break;
                    default: break;
                }
                break;
            }
        }
    }
    return ESP_OK;
}

void migrateNvsKeys(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    /* Check legacy o0_* and o1_* keys */
    int outfc;
    const CfgOutputField* of = config_get_output_fields(&outfc);
    for (int i = 0; i < outfc; i++) {
        char old_key[32], new_key[32];
        snprintf(old_key, sizeof(old_key), "o0_%s", of[i].key_suffix);
        snprintf(new_key, sizeof(new_key), "a_%s", of[i].key_suffix);
        size_t len = 0;
        if (nvs_get_str(h, old_key, NULL, &len) == ESP_OK) {
            char* val = malloc(len);
            if (val && nvs_get_str(h, old_key, val, &len) == ESP_OK)
                nvs_set_str(h, new_key, val);
            free(val);
            nvs_erase_key(h, old_key);
        } else {
            int32_t ival;
            if (nvs_get_i32(h, old_key, &ival) == ESP_OK) {
                nvs_set_i32(h, new_key, ival);
                nvs_erase_key(h, old_key);
            }
        }
        /* Same for o1_* -> b_* */
        snprintf(old_key, sizeof(old_key), "o1_%s", of[i].key_suffix);
        snprintf(new_key, sizeof(new_key), "b_%s", of[i].key_suffix);
        if (nvs_get_str(h, old_key, NULL, &len) == ESP_OK) {
            char* val = malloc(len);
            if (val && nvs_get_str(h, old_key, val, &len) == ESP_OK)
                nvs_set_str(h, new_key, val);
            free(val);
            nvs_erase_key(h, old_key);
        } else {
            int32_t ival;
            if (nvs_get_i32(h, old_key, &ival) == ESP_OK) {
                nvs_set_i32(h, new_key, ival);
                nvs_erase_key(h, old_key);
            }
        }
    }
    /* Migrate apfb -> fbmode */
    int32_t apfb_val;
    if (nvs_get_i32(h, "apfb", &apfb_val) == ESP_OK) {
        nvs_set_i32(h, "fbmode", apfb_val ? 1 : 0); /* simple mapping */
        nvs_erase_key(h, "apfb");
    }
    nvs_commit(h);
    nvs_close(h);
}

esp_err_t config_reset_to_template(void) {
    memset(&cfg, 0, sizeof(cfg));
    board_type_t board = hw_get_board_type();
    const char* tmpl = config_get_board_template_text(board);
    config_apply_template_text(tmpl);
    /* Hardware fields from canonical board table */
    const board_def_t* tbl = board_get_table();
    size_t count = board_get_count();
    if (tbl && board < count) {
        cfg.ledpin = tbl[board].led_pin;
        cfg.ledtype = (int)tbl[board].led_type;
    }
    return ESP_OK;
}

void config_load_outputs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    int outfc;
    const CfgOutputField* of = config_get_output_fields(&outfc);
    const char* prefixes[] = {"a_", "b_", "c_", "d_"};
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        for (int i = 0; i < outfc; i++) {
            char key[32];
            snprintf(key, sizeof(key), "%s%s", prefixes[o], of[i].key_suffix);
            switch (of[i].type) {
                case CFG_TYPE_INT: {
                    int32_t v;
                    if (nvs_get_i32(h, key, &v) == ESP_OK)
                        *(int*)cfg_output_ptr(&of[i], o) = (int)v;
                    break;
                }
                case CFG_TYPE_BOOL: {
                    uint8_t v;
                    if (nvs_get_u8(h, key, &v) == ESP_OK)
                        *(bool*)cfg_output_ptr(&of[i], o) = v ? true : false;
                    break;
                }
                case CFG_TYPE_STRING: {
                    char buf[256]; size_t len = sizeof(buf);
                    if (nvs_get_str(h, key, buf, &len) == ESP_OK) {
                        char* dst = (char*)cfg_output_ptr(&of[i], o);
                        strncpy(dst, buf, of[i].max_len - 1);
                        dst[of[i].max_len - 1] = '\0';
                    }
                    break;
                }
                default: break;
            }
        }
    }
    nvs_close(h);
}

void config_save_outputs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    int outfc;
    const CfgOutputField* of = config_get_output_fields(&outfc);
    const char* prefixes[] = {"a_", "b_", "c_", "d_"};
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        for (int i = 0; i < outfc; i++) {
            char key[32];
            snprintf(key, sizeof(key), "%s%s", prefixes[o], of[i].key_suffix);
            switch (of[i].type) {
                case CFG_TYPE_INT:
                    nvs_set_i32(h, key, *(int*)cfg_output_ptr(&of[i], o));
                    break;
                case CFG_TYPE_BOOL:
                    nvs_set_u8(h, key, *(bool*)cfg_output_ptr(&of[i], o) ? 1 : 0);
                    break;
                case CFG_TYPE_STRING:
                    nvs_set_str(h, key, (char*)cfg_output_ptr(&of[i], o));
                    break;
                default: break;
            }
        }
    }
    nvs_commit(h);
    nvs_close(h);
}

esp_err_t config_load(void) {
    config_reset_to_template();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "No config in NVS, using template defaults");
        config_load_outputs();
        return ESP_OK;
    }
    int fc;
    const CfgField* f = config_get_fields(&fc);
    for (int i = 0; i < fc; i++) {
        switch (f[i].type) {
            case CFG_TYPE_INT: { int32_t v; if (nvs_get_i32(h, f[i].key, &v)==ESP_OK && v>=f[i].min && v<=f[i].max) *(int*)cfg_field_ptr(&f[i]) = (int)v; break; }
            case CFG_TYPE_BOOL: { uint8_t v; if (nvs_get_u8(h, f[i].key, &v)==ESP_OK) *(bool*)cfg_field_ptr(&f[i]) = v ? true : false; break; }
            case CFG_TYPE_STRING: { char buf[256]; size_t len=sizeof(buf); if (nvs_get_str(h, f[i].key, buf, &len)==ESP_OK) { char* d=(char*)cfg_field_ptr(&f[i]); strncpy(d, buf, f[i].max_len-1); d[f[i].max_len-1]='\0'; } break; }
            case CFG_TYPE_ENUM: { int32_t v; if (nvs_get_i32(h, f[i].key, &v)==ESP_OK && v>=f[i].min && v<=f[i].max) *(int*)cfg_field_ptr(&f[i]) = (int)v; break; }
        }
    }
    nvs_close(h);
    config_load_outputs();
    LOG_INFO(TAG, "Configuration loaded from NVS");
    return ESP_OK;
}

esp_err_t config_save(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    int fc;
    const CfgField* f = config_get_fields(&fc);
    for (int i = 0; i < fc; i++) {
        switch (f[i].type) {
            case CFG_TYPE_INT:    nvs_set_i32(h, f[i].key, *(int*)cfg_field_ptr(&f[i])); break;
            case CFG_TYPE_BOOL:   nvs_set_u8(h, f[i].key, *(bool*)cfg_field_ptr(&f[i]) ? 1 : 0); break;
            case CFG_TYPE_STRING: nvs_set_str(h, f[i].key, (char*)cfg_field_ptr(&f[i])); break;
            case CFG_TYPE_ENUM:   nvs_set_i32(h, f[i].key, *(int*)cfg_field_ptr(&f[i])); break;
        }
    }
    nvs_commit(h);
    nvs_close(h);
    config_save_outputs();
    LOG_INFO(TAG, "Configuration saved to NVS");
    return ESP_OK;
}

esp_err_t config_set_value(const char* key, const char* value) {
    if (!key || !value) return ESP_ERR_INVALID_ARG;
    int fc;
    const CfgField* f = config_get_fields(&fc);
    for (int i = 0; i < fc; i++) {
        if (strcmp(f[i].key, key) == 0) {
            esp_err_t err = config_validate_value(&f[i], value);
            if (err != ESP_OK) { LOG_ERROR(TAG, "Invalid value for %s: %s", key, value); return err; }
            switch (f[i].type) {
                case CFG_TYPE_INT:    apply_int_value(&f[i], value); break;
                case CFG_TYPE_BOOL:   apply_bool_value(&f[i], value); break;
                case CFG_TYPE_STRING: apply_str_value(&f[i], value); break;
                case CFG_TYPE_ENUM:   apply_int_value(&f[i], value); break;
            }
            LOG_INFO(TAG, "Set %s = %s", key, value);
            return ESP_OK;
        }
    }
    /* Check output fields */
    int ofc;
    const CfgOutputField* of = config_get_output_fields(&ofc);
    for (int i = 0; i < ofc; i++) {
        if (strcmp(of[i].key_suffix, key) == 0) {
            /* Apply to output A by default */
            switch (of[i].type) {
                case CFG_TYPE_INT:    apply_output_int(&of[i], 0, value); break;
                case CFG_TYPE_BOOL:   apply_output_bool(&of[i], 0, value); break;
                case CFG_TYPE_STRING: apply_output_str(&of[i], 0, value); break;
                default: break;
            }
            LOG_INFO(TAG, "Set a_%s = %s", key, value);
            return ESP_OK;
        }
    }
    LOG_ERROR(TAG, "Key not found: %s", key);
    return ESP_ERR_NOT_FOUND;
}

char* config_get_value(const char* key) {
    if (!key) return NULL;
    int fc;
    const CfgField* f = config_get_fields(&fc);
    for (int i = 0; i < fc; i++) {
        if (strcmp(f[i].key, key) == 0) {
            char* val = malloc(256);
            if (!val) return NULL;
            switch (f[i].type) {
                case CFG_TYPE_INT:    case CFG_TYPE_ENUM: snprintf(val, 256, "%d", *(int*)cfg_field_ptr(&f[i])); break;
                case CFG_TYPE_BOOL:   snprintf(val, 256, "%s", *(bool*)cfg_field_ptr(&f[i]) ? "true" : "false"); break;
                case CFG_TYPE_STRING: snprintf(val, 256, "%s", (char*)cfg_field_ptr(&f[i])); break;
            }
            return val;
        }
    }
    LOG_ERROR(TAG, "Key not found: %s", key);
    return NULL;
}

esp_err_t config_set_output_value(int output_idx, const char* key, const char* value) {
    if (output_idx < 0 || output_idx >= MAX_OUTPUTS || !key || !value) return ESP_ERR_INVALID_ARG;
    int ofc;
    const CfgOutputField* of = config_get_output_fields(&ofc);
    for (int i = 0; i < ofc; i++) {
        if (strcmp(of[i].key_suffix, key) == 0) {
            esp_err_t err = config_validate_output_value(&of[i], value);
            if (err != ESP_OK) return err;
            switch (of[i].type) {
                case CFG_TYPE_INT:    apply_output_int(&of[i], output_idx, value); break;
                case CFG_TYPE_BOOL:   apply_output_bool(&of[i], output_idx, value); break;
                case CFG_TYPE_STRING: apply_output_str(&of[i], output_idx, value); break;
                default: break;
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

char* config_get_output_value(int output_idx, const char* key) {
    if (output_idx < 0 || output_idx >= MAX_OUTPUTS || !key) return NULL;
    int ofc;
    const CfgOutputField* of = config_get_output_fields(&ofc);
    for (int i = 0; i < ofc; i++) {
        if (strcmp(of[i].key_suffix, key) == 0) {
            char* val = malloc(256);
            if (!val) return NULL;
            switch (of[i].type) {
                case CFG_TYPE_INT:    case CFG_TYPE_ENUM: snprintf(val, 256, "%d", *(int*)cfg_output_ptr(&of[i], output_idx)); break;
                case CFG_TYPE_BOOL:   snprintf(val, 256, "%s", *(bool*)cfg_output_ptr(&of[i], output_idx) ? "true" : "false"); break;
                case CFG_TYPE_STRING: snprintf(val, 256, "%s", (char*)cfg_output_ptr(&of[i], output_idx)); break;
            }
            return val;
        }
    }
    return NULL;
}

esp_err_t config_engine_init(void) {
    if (config_initialized) return ESP_OK;
    ESP_ERROR_CHECK(config_schema_init());
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    migrateNvsKeys();
    config_load();
    LOG_INFO(TAG, "Configuration engine initialized");
    config_initialized = true;
    return ESP_OK;
}



