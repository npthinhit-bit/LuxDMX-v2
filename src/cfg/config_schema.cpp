// THE field table. Single source of truth for every persisted setting's
// structure: name, type, constraint, label, group. NO defaults live here, they
// come from board TEMPLATES (templates/*.ini). The only constants referenced are
// STRUCTURAL (how many merge modes / RMII PHY families the code supports).
#include "config_types.h"
#include "config_schema.h"
#include "config_enums.h"

#define ARRSZ(a) (sizeof(a) / sizeof((a)[0]))

static const char* const ENUM_PROTOCOL[] = {"Art-Net", "sACN", "Art-Net + sACN"};
static const char* const ENUM_LEDTYPE[]  = {"off", "plain GPIO", "WS2812 RGB", "5-LED panel"};
static const char* const ENUM_DISPTYPE[] = {"off", "SSD1306 128x64", "SSD1306 128x32", "SH1106", "SSD1351 colour"};
static const char* const ENUM_WIREDPHY[] = {"W5500 (SPI)", "LAN8720 (RMII)"};
static const char* const ENUM_ETHSPIPHY[] = {"W5500", "DM9051"};
static const char* const ENUM_WIFIMODE[] = {"STA (client)", "AP (standalone)"};
static const char* const ENUM_FBMODE[]   = {"keep retrying", "open WPA2 AP", "reboot", "join WiFi"};
static const char* const ENUM_BTNROLE[]  = {"off", "Enter / Select", "Back", "Next (+)", "Prev (-)"};
static const char* const ENUM_TXRATE[]  = {"40 fps (25 ms)", "41.7 fps (24 ms)", "33.3 fps (30 ms)",
                                           "25 fps (40 ms)", "20 fps (50 ms)"};
static const char* const ENUM_TXSTYLE[] = {"Continuous (free-run)", "Delta (follow the input)"};
static const char* const ENUM_TXSRC[]   = {"set here", "set over Art-Net"};
static const char* const ENUM_OUTPUT_MODE[] = {"DMX only", "RDM full (DE/RE)"};

#define IFIELD(key, json, member, mn, mx, label, group) \
    { key, json, CfgKind::Int,  offsetof(Config, member), mn, mx, label, group, CFG_REBOOT, nullptr, 0 }
#define BFIELD(key, json, member, label, group, flags) \
    { key, json, CfgKind::Bool, offsetof(Config, member), 0, 1, label, group, (CFG_REBOOT | (flags)), nullptr, 0 }
#define SFIELD(key, json, member, label, group, flags) \
    { key, json, CfgKind::Str,  offsetof(Config, member), 0, 0, label, group, (CFG_REBOOT | (flags)), nullptr, 0 }
#define EFIELD(key, json, member, label, group, labels) \
    { key, json, CfgKind::Enum, offsetof(Config, member), 0, (int32_t)ARRSZ(labels) - 1, label, group, CFG_REBOOT, labels, (uint8_t)ARRSZ(labels) }
#define IFIELD_L(key, json, member, mn, mx, label, group) \
    { key, json, CfgKind::Int,  offsetof(Config, member), mn, mx, label, group, CFG_LIVE, nullptr, 0 }
#define BFIELD_L(key, json, member, label, group, flags) \
    { key, json, CfgKind::Bool, offsetof(Config, member), 0, 1, label, group, (uint16_t)(CFG_LIVE | (flags)), nullptr, 0 }
#define SFIELD_L(key, json, member, label, group, flags) \
    { key, json, CfgKind::Str,  offsetof(Config, member), 0, 0, label, group, (uint16_t)(CFG_LIVE | (flags)), nullptr, 0 }
#define EFIELD_L(key, json, member, label, group, labels) \
    { key, json, CfgKind::Enum, offsetof(Config, member), 0, (int32_t)ARRSZ(labels) - 1, label, group, CFG_LIVE, labels, (uint8_t)ARRSZ(labels) }

const CfgField CONFIG_FIELDS[] = {
    SFIELD("hostname", "hostname", hostname,    "Hostname",       "Identity", CFG_KEEPNE),
    SFIELD_L("board",    "boardSel", boardSel,    "Board",          "Identity", CFG_NONE),
    SFIELD("otapw",    "otapw",    otaPassword, "OTA password",   "Identity", CFG_SECRET | CFG_KEEPNE),
    EFIELD_L("protocol", "protocol", protocol,    "Input protocol", "Identity", ENUM_PROTOCOL),
    IFIELD("ledpin",  "ledPin",  ledPin,  -1, 48, "LED pin",           "Status LED"),
    EFIELD("ledtype", "ledType", ledType,        "LED type",          "Status LED", ENUM_LEDTYPE),
    IFIELD("ledr",    "ledR",    ledR,    -1, 48, "5-LED panel R pin", "Status LED"),
    IFIELD("ledg",    "ledG",    ledG,    -1, 48, "5-LED panel G pin", "Status LED"),
    IFIELD("ledy",    "ledY",    ledY,    -1, 48, "5-LED panel Y pin", "Status LED"),
    IFIELD("ledb",    "ledB",    ledB,    -1, 48, "5-LED panel B pin", "Status LED"),
    IFIELD("ledw",    "ledW",    ledW,    -1, 48, "5-LED panel W pin", "Status LED"),
    IFIELD_L("ledbrr",  "ledBrR",  ledBrR,   0, 255, "5-LED panel R brightness", "Status LED"),
    IFIELD_L("ledbrg",  "ledBrG",  ledBrG,   0, 255, "5-LED panel G brightness", "Status LED"),
    IFIELD_L("ledbry",  "ledBrY",  ledBrY,   0, 255, "5-LED panel Y brightness", "Status LED"),
    IFIELD_L("ledbrb",  "ledBrB",  ledBrB,   0, 255, "5-LED panel B brightness", "Status LED"),
    IFIELD_L("ledbrw",  "ledBrW",  ledBrW,   0, 255, "5-LED panel W brightness", "Status LED"),
    EFIELD("disptype", "dispType", dispType,        "Display type", "Display", ENUM_DISPTYPE),
    IFIELD("dispsda",  "dispSda",  dispSda,  -1, 48, "I2C SDA",     "Display"),
    IFIELD("dispscl",  "dispScl",  dispScl,  -1, 48, "I2C SCL",     "Display"),
    IFIELD_L("disprot",  "dispRot",  dispRot,   0,  1, "Rotate 180",  "Display"),
    IFIELD("dispcs",   "dispCs",   dispCs,   -1, 48, "SPI CS",      "Display"),
    IFIELD("dispdc",   "dispDc",   dispDc,   -1, 48, "SPI DC",      "Display"),
    IFIELD("disprst",  "dispRst",  dispRst,  -1, 48, "SPI RST",     "Display"),
    IFIELD("dispsck",  "dispSck",  dispSck,  -1, 48, "SPI SCK",     "Display"),
    IFIELD("dispmosi", "dispMosi", dispMosi, -1, 48, "SPI MOSI",    "Display"),
    IFIELD("enca",     "encA",     encA,      -1, 48, "Encoder A pin",        "Controls"),
    IFIELD("encb",     "encB",     encB,      -1, 48, "Encoder B pin",        "Controls"),
    IFIELD("encsw",    "encSw",    encSw,     -1, 48, "Encoder push pin",     "Controls"),
    IFIELD_L("encsteps", "encSteps", encSteps,   1,  4, "Encoder steps/detent", "Controls"),
    BFIELD_L("encrev",   "encReverse", encReverse,     "Reverse encoder dir",  "Controls", CFG_NONE),
    IFIELD("btn1pin",  "btn1Pin",  btn1Pin,   -1, 48, "Button 1 pin",         "Controls"),
    EFIELD_L("btn1act",  "btn1Act",  btn1Act,           "Button 1 action",      "Controls", ENUM_BTNROLE),
    IFIELD("btn2pin",  "btn2Pin",  btn2Pin,   -1, 48, "Button 2 pin",         "Controls"),
    EFIELD_L("btn2act",  "btn2Act",  btn2Act,           "Button 2 action",      "Controls", ENUM_BTNROLE),
    IFIELD("btn3pin",  "btn3Pin",  btn3Pin,   -1, 48, "Button 3 pin",         "Controls"),
    EFIELD_L("btn3act",  "btn3Act",  btn3Act,           "Button 3 action",      "Controls", ENUM_BTNROLE),
    IFIELD("btn4pin",  "btn4Pin",  btn4Pin,   -1, 48, "Button 4 pin",         "Controls"),
    EFIELD_L("btn4act",  "btn4Act",  btn4Act,           "Button 4 action",      "Controls", ENUM_BTNROLE),
    BFIELD_L("btnah",    "btnActiveHigh", btnActiveHigh, "Buttons active-high", "Controls", CFG_NONE),
    IFIELD_L("ctlunimax","ctlUniMax", ctlUniMax,  1, 511, "Menu max universe",  "Controls"),
    BFIELD("ethon",   "ethW5500", ethW5500,           "W5500 module enabled", "Ethernet (W5500)", CFG_NONE),
    IFIELD("ethcs",   "ethCs",    ethCs,      -1, 48, "W5500 CS",   "Ethernet (W5500)"),
    IFIELD("ethsck",  "ethSck",   ethSck,     -1, 48, "W5500 SCK",  "Ethernet (W5500)"),
    IFIELD("ethmosi", "ethMosi",  ethMosi,    -1, 48, "W5500 MOSI", "Ethernet (W5500)"),
    IFIELD("ethmiso", "ethMiso",  ethMiso,    -1, 48, "W5500 MISO", "Ethernet (W5500)"),
    IFIELD("ethint",  "ethInt",   ethInt,     -1, 48, "W5500 INT",  "Ethernet (W5500)"),
    IFIELD("ethrst",  "ethRst",   ethRst,     -1, 48, "W5500 RST",  "Ethernet (W5500)"),
    IFIELD("ethfreq", "ethFreq",  ethFreqMhz,  1, 80, "W5500 SPI MHz", "Ethernet (W5500)"),
    EFIELD("ethspiphy", "ethSpiPhy", ethSpiPhy, "SPI Ethernet chip", "Ethernet (W5500)", ENUM_ETHSPIPHY),
    EFIELD("wiredphy", "wiredPhy", wiredPhy,                       "Wired PHY",       "Ethernet (RMII)", ENUM_WIREDPHY),
    IFIELD("rmiiphy",  "rmiiPhy",  rmiiPhy,  0, RMII_PHY_COUNT - 1, "RMII PHY family", "Ethernet (RMII)"),
    IFIELD("rmiiaddr", "rmiiAddr", rmiiAddr, 0, 31, "RMII SMI addr",  "Ethernet (RMII)"),
    IFIELD("rmiimdc",  "rmiiMdc",  rmiiMdc,  0, 48, "RMII MDC",       "Ethernet (RMII)"),
    IFIELD("rmiimdio", "rmiiMdio", rmiiMdio, 0, 48, "RMII MDIO",      "Ethernet (RMII)"),
    IFIELD("rmiipwr",  "rmiiPwr",  rmiiPwr, -1, 48, "RMII PHY power", "Ethernet (RMII)"),
    IFIELD("rmiiclk",  "rmiiClk",  rmiiClk,  0,  3, "RMII REF_CLK",   "Ethernet (RMII)"),
    BFIELD("useeth",   "useEthernet",  useEthernet,  "Use wired Ethernet", "Network", CFG_NONE),
    EFIELD("wifimode", "wifiMode",     wifiMode,     "WiFi mode",          "Network", ENUM_WIFIMODE),
    SFIELD("wifissid", "wifiSsid",     wifiSsid,     "WiFi SSID",          "Network", CFG_NONE),
    SFIELD("wifipsk",  "wifiPsk",      wifiPsk,      "WiFi password",      "Network", CFG_SECRET | CFG_KEEPNE),
    EFIELD("fbmode",   "linkLossMode", linkLossMode, "Link-loss policy",   "Network", ENUM_FBMODE),
    SFIELD("appw",     "apPassword",   apPassword,   "AP password",        "Network", CFG_SECRET),
    BFIELD("staticip", "staticIp",     staticIp,     "Static IP",          "Network", CFG_NONE),
    SFIELD("ip",       "ip",           ip,           "IP address",         "Network", CFG_NONE),
    SFIELD("gateway",  "gateway",      gateway,      "Gateway",            "Network", CFG_NONE),
    SFIELD("subnet",   "subnet",       subnet,       "Subnet mask",        "Network", CFG_NONE),
    SFIELD("dns",      "dns",          dns,          "DNS server",         "Network", CFG_NONE),
    BFIELD_L("ipprog", "ipProg",       ipProg,       "Art-Net remote IP config (ArtIpProg)", "Network", CFG_NONE),
    BFIELD_L("artrdm", "artnetRdm", artnetRdm, "RDM over Art-Net", "RDM", CFG_NONE),
    IFIELD("rdmmaxdev", "rdmMaxDev", rdmMaxDev, 0, 64, "RDM device limit (0 = auto)", "RDM"),
    BFIELD("autoupd", "autoUpdate", autoUpdate, "Auto-update firmware", "Updates", CFG_NOWEB),
};
const size_t CONFIG_FIELD_COUNT = ARRSZ(CONFIG_FIELDS);

#define OINT(suffix, json, member, legacy, mn, mx, label) \
    { suffix, json, CfgKind::Int,  offsetof(DmxOutput, member), legacy, mn, mx, label, CFG_REBOOT, nullptr, 0 }
#define OINT_L(suffix, json, member, legacy, mn, mx, label) \
    { suffix, json, CfgKind::Int,  offsetof(DmxOutput, member), legacy, mn, mx, label, CFG_LIVE, nullptr, 0 }
#define OBOOL(suffix, json, member, legacy, label) \
    { suffix, json, CfgKind::Bool, offsetof(DmxOutput, member), legacy, 0, 1, label, CFG_REBOOT, nullptr, 0 }
#define OENUM(suffix, json, member, label, labels) \
    { suffix, json, CfgKind::Enum, offsetof(DmxOutput, member), nullptr, 0, (int32_t)ARRSZ(labels) - 1, \
      label, CFG_LIVE, labels, (uint8_t)ARRSZ(labels) }
#define OENUM_RO(suffix, json, member, label, labels) \
    { suffix, json, CfgKind::Enum, offsetof(DmxOutput, member), nullptr, 0, (int32_t)ARRSZ(labels) - 1, \
      label, (uint16_t)(CFG_LIVE | CFG_NOWEB), labels, (uint8_t)ARRSZ(labels) }
#define OENUM_R(suffix, json, member, label, labels) \
    { suffix, json, CfgKind::Enum, offsetof(DmxOutput, member), nullptr, 0, (int32_t)ARRSZ(labels) - 1, \
      label, CFG_REBOOT, labels, (uint8_t)ARRSZ(labels) }

const CfgOutputField OUTPUT_FIELDS[] = {
    OBOOL("en",    "en",    enabled,   nullptr,             "Enabled"),
    OINT_L("uni",   "uni",   universe,  "universe", 0, 32767, "Art-Net universe"),
    OINT_L("net",   "net",   net,       nullptr, 0, 127, "Art-Net net (0-127)"),
    OINT_L("sub",   "sub",   subnet,    nullptr, 0, 15, "Art-Net subnet (0-15)"),
    OINT_L("sacn",  "sacn",  sacnUniverse, nullptr, 0, 32767, "sACN universe (0=auto=universe+1)"),
    OINT_L("sasync", "sacnSync", sacnSync, nullptr, 0, 32767, "sACN sync universe (0=none)"),
    OINT ("port",  "port",  port,      "dmxport",  0,  2,   "UART port"),
    OINT ("tx",    "tx",    txPin,     "dmxtx",   -1, 48,   "TX pin"),
    OINT ("rx",    "rx",    rxPin,     "dmxrx",   -1, 48,   "RX pin"),
    OINT ("rts",   "rts",   rtsPin,    "dmxrts",  -1, 48,   "RTS / DE-RE pin"),
    OINT_L("merge", "merge", mergeMode, nullptr, MERGE_OFF, MERGE_LTP, "Merge mode"),
    OINT_L("loss",  "loss",  lossMode,  nullptr, LOSS_HOLD, LOSS_STOP, "Signal-loss policy"),
    OENUM("rate",  "rate",  txRate,     "DMX output rate",  ENUM_TXRATE),
    OENUM("style", "style", txStyle,    "Transmit style",   ENUM_TXSTYLE),
    OENUM_RO("stylesrc", "styleSrc", txStyleSrc, "Transmit style set by", ENUM_TXSRC),
    OENUM_R("mode", "mode", mode, "Output mode", ENUM_OUTPUT_MODE),
};
const size_t OUTPUT_FIELD_COUNT = ARRSZ(OUTPUT_FIELDS);
