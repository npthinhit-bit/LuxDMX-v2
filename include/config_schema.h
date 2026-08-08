#pragma once
#include <Arduino.h>

// The persisted config structs, moved here out of main.cpp. config_schema.cpp
// describes every field below in one table (CONFIG_FIELDS / OUTPUT_FIELDS).
static constexpr int MAX_OUTPUTS = 4;

struct DmxOutput {
    bool enabled;
    int  universe;   // Art-Net universe (0-15), 15-bit with net/subnet (0-32767)
    int  net;        // Art-Net net switch (0-127)
    int  subnet;     // Art-Net subnet (0-15)
    int  sacnUniverse; // sACN streaming universe (0 = auto, derive from universe+1)
    int  sacnSync;   // sACN sync universe (0 = none, stream-sync staging active)
    int  port;       // UART number for RDM RX (1=UART1, 2=UART2); ignored for DMX-only outputs
    int  txPin;
    int  rxPin;      // -1 = output only (no RDM)
    int  rtsPin;     // -1 = auto-direction module / no RDM
    int  mergeMode;  // how to combine multiple sources on this universe
    int  lossMode;   // what to send when every source on this universe goes silent
    int  txRate;     // index into DMX_RATE_MS: the free-running period for this port
    int  txStyle;    // 0 = continuous (free-run at txRate), 1 = delta (one frame per input packet)
    int  txStyleSrc; // 0 = set locally (web UI / serial console), 1 = set by a controller via Art-Net
    int  mode;       // Output mode (output_mode_t): 0 = DMX only, 1 = RDM full
};

struct Config {
    String    hostname;
    String    otaPassword;
    String    boardSel;
    int       protocol;
    int       ledPin;
    int       ledType;
    int       ledR, ledG, ledY, ledB, ledW;
    int       ledBrR, ledBrG, ledBrY, ledBrB, ledBrW;
    DmxOutput outputs[MAX_OUTPUTS];
    int       dispType;
    int       dispSda, dispScl, dispRot, dispCs, dispDc, dispRst, dispSck, dispMosi;
    int       encA, encB, encSw;
    int       encSteps;
    bool      encReverse;
    int       btn1Pin, btn2Pin, btn3Pin, btn4Pin;
    int       btn1Act, btn2Act, btn3Act, btn4Act;
    bool      btnActiveHigh;
    int       ctlUniMax;
    int       ethCs, ethSck, ethMosi, ethMiso, ethInt, ethRst, ethFreqMhz;
    bool      ethW5500;
    int       ethSpiPhy;
    int       wiredPhy;
    int       rmiiPhy, rmiiAddr, rmiiMdc, rmiiMdio, rmiiPwr, rmiiClk;
    bool      useEthernet;
    int       wifiMode;
    String    wifiSsid;
    String    wifiPsk;
    bool      apFallback;
    int       linkLossMode;
    String    apPassword;
    bool      staticIp;
    String    ip, gateway, subnet, dns;
    bool      ipProg;
    bool      autoUpdate;
    bool      artnetRdm;
    int       rdmMaxDev;
};

extern Config cfg;

#if MAX_OUTPUTS > 4
#error "ESP32-S3 hardware ceiling: 4 RMT TX channels"
#endif
