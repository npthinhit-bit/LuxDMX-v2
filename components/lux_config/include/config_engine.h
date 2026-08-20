#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type kinds for config fields */
typedef enum {
    CFG_TYPE_INT,
    CFG_TYPE_BOOL,
    CFG_TYPE_STRING,
    CFG_TYPE_ENUM
} cfg_type_t;

/* Config field flags (spec 45 §5) */
#define CFG_FLAG_NONE      0
#define CFG_FLAG_LIVE      (1 << 0)   /* Applies instantly */
#define CFG_FLAG_REBOOT    (1 << 1)   /* Requires reboot */
#define CFG_FLAG_SECRET    (1 << 2)   /* Masked in dumps */
#define CFG_FLAG_NOWEB     (1 << 3)   /* Hidden from web form */
#define CFG_FLAG_KEEPNE    (1 << 4)   /* Keep if web field blank */
#define CFG_FLAG_READONLY  (1 << 5)   /* Not writable */

typedef uint32_t cfg_flag_t;

/* Per-output config struct — 24 fields (spec 45 §5) */
typedef struct {
    int en;              /* 0-1: output enabled */
    int uni;             /* 0-15: DMX universe */
    int net;             /* 0-127: ArtNet network */
    int sub;             /* 0-15: ArtNet subnet */
    int sacn;            /* 1-63999: sACN universe */
    int port;            /* 0-3: UART port */
    int tx;              /* TX pin (-1 = none) */
    int rx;              /* RX pin (-1 = none) */
    int rts;             /* RTS/DE/RE pin (-1 = none) */
    int mode;            /* 0=DMX, 1=RDM */
    int brk;             /* break time us (88-300) */
    int mab;             /* MAB time us (0-300) */
    int inv;             /* 0-1: invert polarity */
    int mergemode;       /* 0=Off, 1=HTP, 2=LTP, 3=LTP-Takover, 4=Priority */
    int losemode;        /* 0=Hold, 1=Zero, 2=Stop, 3=Preset, 4=Home */
    int lospreset;       /* 0-511: loss preset value */
    int failsafeto;      /* 0-600: failsafe timeout seconds */
    int txrate;          /* 1-44: transmit rate Hz */
    int txstyle;         /* 0=Continuous, 1=Delta */
    int txsrc;           /* 0=Local, 1=ArtNet */
    int inputmode;       /* 0=Off, 1=To-Network, 2=Monitor */
    int splitmask;       /* 0-255: split mask */
    int loopback;        /* 0-1: loopback */
    int priority;        /* 0-255: priority value */
    int sacnsync;        /* 0=off, 1-63999: sACN Stream-Sync universe */
} DmxOutput;

#define MAX_OUTPUTS 4

/* Full Config struct — 47 global fields + 4 outputs (spec 45 §5) */
typedef struct {
    /* Identity */
    char hostname[32];
    char otapw[64];
    /* Protocol */
    int protocol;        /* 0=Off, 1=ArtNet, 2=sACN, 3=Both */
    /* WiFi */
    int wifimode;        /* 0=Station, 1=AP, 2=AP+STA */
    char wifissid[32];
    char wifipsk[64];
    /* LED */
    int ledpin;          /* LED pin (reboot) */
    int ledtype;         /* 0=off, 1=GPIO, 2=WS2812, 3=panel */
    int ledbr;           /* 0-100: brightness percentage */
    int ledbrr;          /* 0-255: panel red brightness */
    int ledbrg;
    int ledbry;
    int ledbrb;
    int ledbrw;
    int ledr; int ledg; int ledy; int ledb; int ledw; /* panel indicator pins */
    /* Display */
    int dispsda; int dispscl;
    /* Ethernet W5500 SPI */
    int ethw5500;        /* enable W5500 */
    int ethcs; int ethsck; int ethmosi; int ethmiso;
    int ethint; int ethrst;
    int ethfreq;          /* SPI frequency (live) */
    int ethspiphy;
    /* RMII */
    int rmiiaddr; int rmiimdc; int rmiimdio; int rmiipwr;
    /* Network */
    int subnet; int autoip; int dscp; int dscpdmx;
    /* Timecode */
    int tcSend; int tcType; int tcFps;
    /* Art-Net */
    int artrdm;
    /* Controls */
    int encsteps; int ctlunimax;
    int btn1act; int btn2act; int btn3act; int btn4act;
    /* Outputs */
    DmxOutput outputs[MAX_OUTPUTS];
} Config;

/* Field descriptor using offsetof — no value_ptr, no accessor methods (spec 45 §5, §11) */
typedef struct {
    const char* key;          /* NVS key */
    const char* json_key;     /* JSON export key */
    cfg_type_t type;
    size_t offset;            /* offsetof(Config, field) */
    size_t max_len;           /* buffer size for strings, 0 for ints */
    int min; int max;
    const char* label;
    const char* group;
    uint32_t flags;
    const char** enum_labels; /* NULL for non-enum */
} CfgField;

/* Per-output field descriptor */
typedef struct {
    const char* key_suffix;   /* e.g. "uni" (prefixed a/b/c/d_ at NVS time) */
    const char* json_key;
    cfg_type_t type;
    size_t offset;            /* offsetof(DmxOutput, field) */
    size_t max_len;
    int min; int max;
    const char* label;
    const char* group;
    uint32_t flags;
    const char** enum_labels;
} CfgOutputField;

/* Live config singleton */
extern Config cfg;

/* Descriptor tables */
extern const CfgField CONFIG_FIELDS[];
extern const int CONFIG_FIELD_COUNT;
extern const CfgOutputField OUTPUT_FIELDS[];
extern const int OUTPUT_FIELD_COUNT;

/* Core config API */
esp_err_t config_engine_init(void);
esp_err_t config_load(void);
esp_err_t config_save(void);
esp_err_t config_reset_to_template(void);
esp_err_t config_set_value(const char* key, const char* value);
char* config_get_value(const char* key);
const CfgField* config_get_fields(int* count);
const CfgOutputField* config_get_output_fields(int* count);

/* Per-output accessors */
esp_err_t config_set_output_value(int output_idx, const char* key, const char* value);
char* config_get_output_value(int output_idx, const char* key);
void config_load_outputs(void);
extern const char _BOARD_TEMPLATE_BASE[];
void config_save_outputs(void);

/* NVS migration (spec 02 §6.1) */
void migrateNvsKeys(void);

/* Template text parser (spec 02 §6.2, spec 46) */
esp_err_t config_apply_template_text(const char* text);

/* JSON export/import */
esp_err_t config_export_json(cJSON** root);
esp_err_t config_import_json(cJSON* root);
esp_err_t config_export_json_string(char** json_string);
esp_err_t config_import_json_string(const char* json_string);

/* Validation */
esp_err_t config_validate_value(const CfgField* field, const char* value);
esp_err_t config_validate_output_value(const CfgOutputField* field, const char* value);

#ifdef __cplusplus
}
#endif
