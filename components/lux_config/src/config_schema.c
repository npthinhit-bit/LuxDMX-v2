#include "config_engine.h"
#include "common.h"
#include "logger.h"
#include "boards.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "config_schema";

/* ---- Live config singleton (spec 45 §2) ---- */
Config cfg;

/* ---- Enum label arrays ---- */
static const char* proto_labels[] = {"Off", "ArtNet", "sACN", "Both", NULL};
static const char* wifi_mode_labels[] = {"Station", "AP", "AP+STA", NULL};
static const char* led_type_labels[] = {"off", "GPIO", "WS2812", "Panel", NULL};
static const char* merge_mode_labels[] = {"Off", "HTP", "LTP", "LTP-Takover", "Priority", NULL};
static const char* loss_mode_labels[] = {"Hold", "Zero", "Stop", "Preset", "Home", NULL};
static const char* input_mode_labels[] = {"Off", "To-Network", "Monitor", NULL};
static const char* tx_style_labels[] = {"Continuous", "Delta", NULL};
static const char* tx_src_labels[] = {"Local", "ArtNet", NULL};
static const char* btn_action_labels[] = {"Off", "Next", "Prev", "Enter", "Back", NULL};
static const char* eth_phy_labels[] = {"LAN8720", "IP101", "RTL8201", "DP83848", "KSZ8081", "JL1101", NULL};

/* ---- Board template text (embedded from templates/) ---- */
const char _BOARD_TEMPLATE_BASE[] =
    "# Base template\n"
    "hostname=dmx-gateway\n"
    "otapw=dmxota\n"
    "protocol=2\n"
    "ledpin=2\n"
    "ledtype=1\n"
    "ledbrr=255\n"
    "ledbrg=255\n"
    "ledbry=255\n"
    "ledbrb=255\n"
    "ledbrw=255\n"
    "dispsda=21\n"
    "dispscl=22\n"
    "ethcs=5\n"
    "ethsck=18\n"
    "ethmosi=23\n"
    "ethmiso=19\n"
    "ethint=4\n"
    "ethrst=25\n"
    "ethfreq=20\n"
    "rmiiaddr=1\n"
    "rmiimdc=23\n"
    "rmiimdio=18\n"
    "rmiipwr=16\n"
    "subnet=255\n"
    "autoip=1\n"
    "dscp=1\n"
    "dscpdmx=46\n"
    "tcSend=0\n"
    "tcType=1\n"
    "tcFps=25\n"
    "artrdm=1\n"
    "encsteps=4\n"
    "ctlunimax=15\n"
    "btn1act=3\n"
    "btn2act=4\n"
    "btn3act=1\n"
    "btn4act=2\n"
    "a_en=1\n"
    "a_tx=17\n"
    "a_rx=16\n"
    "a_brk=176\n"
    "a_mab=12\n"
    "a_inv=0\n"
    "b_uni=1\n"
    "b_port=2\n"
    "b_brk=176\n"
    "b_mab=12\n"
    "b_inv=0\n";

static const char _BOARD_TEMPLATE_ESP32S3_N16R8[] =
    "extends=_base\n"
    "wifimode=0\n"
    "wifissid=MSI\n"
    "wifipsk=12345678\n"
    "ledpin=48\n"
    "ledtype=2\n";

static const char _BOARD_TEMPLATE_WT32ETH01[] =
    "extends=_base\n";

static const char _BOARD_TEMPLATE_ESP32DEV[] =
    "extends=_base\n";

const char* config_get_board_template_text(board_type_t board) {
    switch (board) {
        case BOARD_ESP32S3_N16R8: return _BOARD_TEMPLATE_ESP32S3_N16R8;
        case BOARD_WT32ETH01:     return _BOARD_TEMPLATE_WT32ETH01;
        case BOARD_ESP32DEV:      return _BOARD_TEMPLATE_ESP32DEV;
        default:                  return _BOARD_TEMPLATE_ESP32DEV;
    }
}

/* ---- Schema initialization (no-op with offsetof, kept for API compat) ---- */
esp_err_t config_schema_init(void) {
    return ESP_OK;
}

/* ---- Field descriptor table: 47 global fields ---- */
static const char* _empty_labels[] = {NULL};

const CfgField CONFIG_FIELDS[] = {
    /* Identity */
    { "hostname",  "hostname",    CFG_TYPE_STRING, offsetof(Config, hostname),  sizeof(cfg.hostname),  0, 32, "Hostname",     "Identity",    CFG_FLAG_REBOOT|CFG_FLAG_KEEPNE, NULL },
    { "otapw",     "otaPassword", CFG_TYPE_STRING, offsetof(Config, otapw),      sizeof(cfg.otapw),      0, 64, "OTA Password", "Identity",    CFG_FLAG_SECRET|CFG_FLAG_REBOOT|CFG_FLAG_KEEPNE, NULL },
    /* Protocol */
    { "protocol",  "protocol",     CFG_TYPE_INT, offsetof(Config, protocol), 0, 0, 3, "Protocol",   "Protocol",   CFG_FLAG_LIVE, proto_labels },
    /* WiFi */
    { "wifimode",  "wifiMode",    CFG_TYPE_INT, offsetof(Config, wifimode), 0, 0, 2, "WiFi Mode",   "WiFi",      CFG_FLAG_LIVE, wifi_mode_labels },
    { "wifissid",  "wifiSsid",    CFG_TYPE_STRING, offsetof(Config, wifissid), sizeof(cfg.wifissid), 0, 32, "WiFi SSID",   "WiFi",      CFG_FLAG_REBOOT, NULL },
    { "wifipsk",   "wifiPassword", CFG_TYPE_STRING, offsetof(Config, wifipsk), sizeof(cfg.wifipsk), 0, 64, "WiFi Password", "WiFi", CFG_FLAG_SECRET|CFG_FLAG_REBOOT|CFG_FLAG_KEEPNE, NULL },
    /* LED */
    { "ledpin",    "ledPin",      CFG_TYPE_INT, offsetof(Config, ledpin), 0, -1, 48, "LED Pin",   "LED",  CFG_FLAG_REBOOT, NULL },
    { "ledtype",   "ledType",     CFG_TYPE_INT, offsetof(Config, ledtype), 0, 0, 3, "LED Type",   "LED",  CFG_FLAG_REBOOT, led_type_labels },
    { "ledbr",     "ledBrightness", CFG_TYPE_INT, offsetof(Config, ledbr), 0, 0, 100, "LED Brightness", "LED", CFG_FLAG_LIVE, NULL },
    { "ledbrr",    "panelBrR",    CFG_TYPE_INT, offsetof(Config, ledbrr), 0, 0, 255, "Panel Red",  "LED",  CFG_FLAG_LIVE, NULL },
    { "ledbrg",    "panelBrG",    CFG_TYPE_INT, offsetof(Config, ledbrg), 0, 0, 255, "Panel Green", "LED", CFG_FLAG_LIVE, NULL },
    { "ledbry",    "panelBrY",    CFG_TYPE_INT, offsetof(Config, ledbry), 0, 0, 255, "Panel Yellow", "LED", CFG_FLAG_LIVE, NULL },
    { "ledbrb",    "panelBrB",    CFG_TYPE_INT, offsetof(Config, ledbrb), 0, 0, 255, "Panel Blue", "LED", CFG_FLAG_LIVE, NULL },
    { "ledbrw",    "panelBrW",    CFG_TYPE_INT, offsetof(Config, ledbrw), 0, 0, 255, "Panel White", "LED", CFG_FLAG_LIVE, NULL },
    { "ledr",      "ledRedPin",   CFG_TYPE_INT, offsetof(Config, ledr), 0, -1, 48, "LED Red Pin",  "LED",  CFG_FLAG_REBOOT, NULL },
    { "ledg",      "ledGreenPin", CFG_TYPE_INT, offsetof(Config, ledg), 0, -1, 48, "LED Green Pin", "LED", CFG_FLAG_REBOOT, NULL },
    { "ledy",      "ledYellowPin", CFG_TYPE_INT, offsetof(Config, ledy), 0, -1, 48, "LED Yellow Pin", "LED", CFG_FLAG_REBOOT, NULL },
    { "ledb",      "ledBluePin",  CFG_TYPE_INT, offsetof(Config, ledb), 0, -1, 48, "LED Blue Pin",  "LED",  CFG_FLAG_REBOOT, NULL },
    { "ledw",      "ledWhitePin", CFG_TYPE_INT, offsetof(Config, ledw), 0, -1, 48, "LED White Pin", "LED", CFG_FLAG_REBOOT, NULL },
    /* Display */
    { "dispsda",   "displaySda",  CFG_TYPE_INT, offsetof(Config, dispsda), 0, -1, 48, "Display SDA", "Display", CFG_FLAG_REBOOT, NULL },
    { "dispscl",   "displayScl",  CFG_TYPE_INT, offsetof(Config, dispscl), 0, -1, 48, "Display SCL", "Display", CFG_FLAG_REBOOT, NULL },
    /* Ethernet W5500 SPI */
    { "ethw5500",   "ethW5500",   CFG_TYPE_INT, offsetof(Config, ethw5500), 0, 0, 1, "W5500 Enable", "Ethernet", CFG_FLAG_REBOOT, NULL },
    { "ethcs",     "ethCs",       CFG_TYPE_INT, offsetof(Config, ethcs), 0, -1, 48, "W5500 CS Pin",   "Ethernet", CFG_FLAG_REBOOT, NULL },
    { "ethsck",    "ethSck",      CFG_TYPE_INT, offsetof(Config, ethsck), 0, -1, 48, "W5500 SCK Pin",  "Ethernet", CFG_FLAG_REBOOT, NULL },
    { "ethmosi",   "ethMosi",     CFG_TYPE_INT, offsetof(Config, ethmosi), 0, -1, 48, "W5500 MOSI Pin", "Ethernet", CFG_FLAG_REBOOT, NULL },
    { "ethmiso",   "ethMiso",     CFG_TYPE_INT, offsetof(Config, ethmiso), 0, -1, 48, "W5500 MISO Pin", "Ethernet", CFG_FLAG_REBOOT, NULL },
    { "ethint",    "ethInt",      CFG_TYPE_INT, offsetof(Config, ethint), 0, -1, 48, "W5500 INT Pin",  "Ethernet", CFG_FLAG_REBOOT, NULL },
    { "ethrst",    "ethRst",      CFG_TYPE_INT, offsetof(Config, ethrst), 0, -1, 48, "W5500 RST Pin",  "Ethernet", CFG_FLAG_REBOOT, NULL },
    { "ethfreq",   "ethFreq",     CFG_TYPE_INT, offsetof(Config, ethfreq), 0, 0, 40, "SPI Frequency",  "Ethernet", CFG_FLAG_LIVE, NULL },
    { "ethspiphy", "ethSpiPhy",   CFG_TYPE_INT, offsetof(Config, ethspiphy), 0, 0, 1, "SPI PHY Type",  "Ethernet", CFG_FLAG_LIVE, NULL },
    /* RMII */
    { "rmiiaddr",  "rmiiPhyAddr", CFG_TYPE_INT, offsetof(Config, rmiiaddr), 0, 1, 8, "RMII PHY Addr", "RMII",  CFG_FLAG_REBOOT, NULL },
    { "rmiimdc",   "rmiiMdc",     CFG_TYPE_INT, offsetof(Config, rmiimdc), 0, -1, 48, "RMII MDC Pin",  "RMII",  CFG_FLAG_REBOOT, NULL },
    { "rmiimdio",  "rmiiMdio",    CFG_TYPE_INT, offsetof(Config, rmiimdio), 0, -1, 48, "RMII MDIO Pin", "RMII",  CFG_FLAG_REBOOT, NULL },
    { "rmiipwr",   "rmiiPower",   CFG_TYPE_INT, offsetof(Config, rmiipwr), 0, -1, 48, "RMII PWR Pin",  "RMII",  CFG_FLAG_REBOOT, NULL },
    /* Network */
    { "subnet",    "subnetMask",  CFG_TYPE_INT, offsetof(Config, subnet), 0, 0, 255, "Subnet Mask",   "Network", CFG_FLAG_REBOOT, NULL },
    { "autoip",    "autoIp",      CFG_TYPE_BOOL, offsetof(Config, autoip), 0, 0, 1, "AutoIP",        "Network", CFG_FLAG_LIVE, NULL },
    { "dscp",      "dscp",        CFG_TYPE_INT, offsetof(Config, dscp), 0, 0, 63, "QoS DSCP",      "Network", CFG_FLAG_LIVE, NULL },
    { "dscpdmx",   "dscpDmx",     CFG_TYPE_INT, offsetof(Config, dscpdmx), 0, 0, 63, "DMX DSCP",    "Network", CFG_FLAG_LIVE, NULL },
    /* Timecode */
    { "tcSend",    "timecodeSend", CFG_TYPE_BOOL, offsetof(Config, tcSend), 0, 0, 1, "Send Timecode", "Timecode", CFG_FLAG_LIVE, NULL },
    { "tcType",    "timecodeType", CFG_TYPE_INT, offsetof(Config, tcType), 0, 0, 1, "Timecode Type", "Timecode", CFG_FLAG_LIVE, NULL },
    { "tcFps",     "timecodeFps", CFG_TYPE_INT, offsetof(Config, tcFps), 0, 0, 30, "Timecode FPS",  "Timecode", CFG_FLAG_LIVE, NULL },
    /* Art-Net */
    { "artrdm",    "artnetRdm",   CFG_TYPE_BOOL, offsetof(Config, artrdm), 0, 0, 1, "ArtNet RDM",  "ArtNet", CFG_FLAG_LIVE, NULL },
    /* Controls */
    { "encsteps",  "encoderSteps", CFG_TYPE_INT, offsetof(Config, encsteps), 0, 1, 32, "Encoder Steps", "Controls", CFG_FLAG_LIVE, NULL },
    { "ctlunimax", "ctrlUniMax",  CFG_TYPE_INT, offsetof(Config, ctlunimax), 0, 0, 15, "Control Uni Max", "Controls", CFG_FLAG_LIVE, NULL },
    { "btn1act",   "button1Action", CFG_TYPE_INT, offsetof(Config, btn1act), 0, 0, 4, "Button 1 Action", "Controls", CFG_FLAG_LIVE, btn_action_labels },
    { "btn2act",   "button2Action", CFG_TYPE_INT, offsetof(Config, btn2act), 0, 0, 4, "Button 2 Action", "Controls", CFG_FLAG_LIVE, btn_action_labels },
    { "btn3act",   "button3Action", CFG_TYPE_INT, offsetof(Config, btn3act), 0, 0, 4, "Button 3 Action", "Controls", CFG_FLAG_LIVE, btn_action_labels },
    { "btn4act",   "button4Action", CFG_TYPE_INT, offsetof(Config, btn4act), 0, 0, 4, "Button 4 Action", "Controls", CFG_FLAG_LIVE, btn_action_labels },
};
const int CONFIG_FIELD_COUNT = sizeof(CONFIG_FIELDS) / sizeof(CONFIG_FIELDS[0]);

/* ---- Per-output field descriptor table: 25 fields ---- */
const CfgOutputField OUTPUT_FIELDS[] = {
    { "en",          "enabled",     CFG_TYPE_INT,   offsetof(DmxOutput, en),          0, 0, 1, "Enabled",       "Output", CFG_FLAG_REBOOT, NULL },
    { "uni",         "universe",   CFG_TYPE_INT,   offsetof(DmxOutput, uni),         0, 0, 15, "Universe",      "Output", CFG_FLAG_LIVE, NULL },
    { "net",         "network",    CFG_TYPE_INT,   offsetof(DmxOutput, net),         0, 0, 127, "Network",      "Output", CFG_FLAG_REBOOT, NULL },
    { "sub",         "subnet",     CFG_TYPE_INT,   offsetof(DmxOutput, sub),         0, 0, 15, "Subnet",        "Output", CFG_FLAG_REBOOT, NULL },
    { "sacn",        "sacnUniverse", CFG_TYPE_INT, offsetof(DmxOutput, sacn),        0, 1, 63999, "sACN Universe", "Output", CFG_FLAG_LIVE, NULL },
    { "port",        "uartPort",   CFG_TYPE_INT,   offsetof(DmxOutput, port),        0, 0, 3, "UART Port",     "Output", CFG_FLAG_REBOOT, NULL },
    { "tx",          "txPin",      CFG_TYPE_INT,   offsetof(DmxOutput, tx),          0, -1, 48, "TX Pin",        "Output", CFG_FLAG_REBOOT, NULL },
    { "rx",          "rxPin",      CFG_TYPE_INT,   offsetof(DmxOutput, rx),          0, -1, 48, "RX Pin",        "Output", CFG_FLAG_REBOOT, NULL },
    { "rts",         "rtsPin",     CFG_TYPE_INT,   offsetof(DmxOutput, rts),         0, -1, 48, "RTS Pin",       "Output", CFG_FLAG_REBOOT, NULL },
    { "mode",        "mode",       CFG_TYPE_INT,   offsetof(DmxOutput, mode),        0, 0, 1, "Mode",          "Output", CFG_FLAG_REBOOT, NULL },
    { "brk",         "breakTime",  CFG_TYPE_INT,   offsetof(DmxOutput, brk),         0, 88, 300, "Break Time",  "Output", CFG_FLAG_REBOOT, NULL },
    { "mab",         "mabTime",    CFG_TYPE_INT,   offsetof(DmxOutput, mab),         0, 0, 300, "MAB Time",    "Output", CFG_FLAG_REBOOT, NULL },
    { "inv",         "invert",     CFG_TYPE_INT,   offsetof(DmxOutput, inv),         0, 0, 1, "Invert",        "Output", CFG_FLAG_LIVE, NULL },
    { "mergemode",   "mergeMode",  CFG_TYPE_INT,   offsetof(DmxOutput, mergemode),   0, 0, 4, "Merge Mode",    "Output", CFG_FLAG_LIVE, merge_mode_labels },
    { "losemode",    "lossMode",   CFG_TYPE_INT,   offsetof(DmxOutput, losemode),    0, 0, 4, "Loss Mode",     "Output", CFG_FLAG_LIVE, loss_mode_labels },
    { "lospreset",   "lossPreset", CFG_TYPE_INT,   offsetof(DmxOutput, lospreset),   0, 0, 511, "Loss Preset",  "Output", CFG_FLAG_LIVE, NULL },
    { "failsafeto",  "failsafeTO", CFG_TYPE_INT,   offsetof(DmxOutput, failsafeto),  0, 0, 600, "Failsafe TO",  "Output", CFG_FLAG_LIVE, NULL },
    { "txrate",      "txRate",     CFG_TYPE_INT,   offsetof(DmxOutput, txrate),      0, 1, 44, "TX Rate",       "Output", CFG_FLAG_LIVE, NULL },
    { "txstyle",     "txStyle",    CFG_TYPE_INT,   offsetof(DmxOutput, txstyle),     0, 0, 1, "TX Style",      "Output", CFG_FLAG_LIVE, tx_style_labels },
    { "txsrc",       "txSource",   CFG_TYPE_INT,   offsetof(DmxOutput, txsrc),       0, 0, 1, "TX Source",     "Output", CFG_FLAG_LIVE, tx_src_labels },
    { "inputmode",   "inputMode",  CFG_TYPE_INT,   offsetof(DmxOutput, inputmode),   0, 0, 2, "Input Mode",    "Output", CFG_FLAG_LIVE, input_mode_labels },
    { "splitmask",   "splitMask",  CFG_TYPE_INT,   offsetof(DmxOutput, splitmask),   0, 0, 255, "Split Mask",   "Output", CFG_FLAG_LIVE, NULL },
    { "loopback",    "loopback",   CFG_TYPE_BOOL,  offsetof(DmxOutput, loopback),    0, 0, 1, "Loopback",      "Output", CFG_FLAG_LIVE, NULL },
    { "priority",    "priority",   CFG_TYPE_INT,   offsetof(DmxOutput, priority),    0, 0, 255, "Priority",     "Output", CFG_FLAG_LIVE, NULL },
    { "sacnsync",    "sacnSync",   CFG_TYPE_INT,   offsetof(DmxOutput, sacnsync),    0, 0, 63999, "sACN Sync",   "Output", CFG_FLAG_LIVE, NULL },
};
const int OUTPUT_FIELD_COUNT = sizeof(OUTPUT_FIELDS) / sizeof(OUTPUT_FIELDS[0]);

/* ---- Functions ---- */

const CfgField* config_get_fields(int* count) {
    if (count) *count = CONFIG_FIELD_COUNT;
    return CONFIG_FIELDS;
}

const CfgOutputField* config_get_output_fields(int* count) {
    if (count) *count = OUTPUT_FIELD_COUNT;
    return OUTPUT_FIELDS;
}

esp_err_t config_validate_value(const CfgField* field, const char* value) {
    if (!field || !value) return ESP_ERR_INVALID_ARG;
    switch (field->type) {
        case CFG_TYPE_INT: {
            char* end;
            long v = strtol(value, &end, 0);
            if (*end != '\0') return ESP_ERR_INVALID_ARG;
            if (v < field->min || v > field->max) return ESP_ERR_INVALID_ARG;
            break;
        }
        case CFG_TYPE_BOOL:
            if (strcmp(value,"true")!=0 && strcmp(value,"false")!=0 &&
                strcmp(value,"1")!=0 && strcmp(value,"0")!=0)
                return ESP_ERR_INVALID_ARG;
            break;
        case CFG_TYPE_STRING:
            if (strlen(value) > field->max_len - 1) return ESP_ERR_INVALID_ARG;
            break;
        case CFG_TYPE_ENUM:
            if (field->enum_labels) {
                int i;
                for (i = field->min; i <= field->max && field->enum_labels[i - field->min]; i++) {
                    if (strcmp(value, field->enum_labels[i - field->min]) == 0) {
                        return ESP_OK;
                    }
                }
                if (i > field->max) return ESP_ERR_INVALID_ARG;
            }
            break;
    }
    return ESP_OK;
}

esp_err_t config_validate_output_value(const CfgOutputField* field, const char* value) {
    if (!field || !value) return ESP_ERR_INVALID_ARG;
    switch (field->type) {
        case CFG_TYPE_INT: {
            char* end;
            long v = strtol(value, &end, 0);
            if (*end != '\0') return ESP_ERR_INVALID_ARG;
            if (v < field->min || v > field->max) return ESP_ERR_INVALID_ARG;
            break;
        }
        case CFG_TYPE_BOOL:
            if (strcmp(value,"true")!=0 && strcmp(value,"false")!=0 &&
                strcmp(value,"1")!=0 && strcmp(value,"0")!=0)
                return ESP_ERR_INVALID_ARG;
            break;
        case CFG_TYPE_STRING:
            if (strlen(value) > field->max_len - 1) return ESP_ERR_INVALID_ARG;
            break;
        case CFG_TYPE_ENUM:
            break;
    }
    return ESP_OK;
}
