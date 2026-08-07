#pragma once
// Structural constants the schema references. These are NOT board defaults — they
// describe what the compiled code supports (how many merge modes / fallback modes
// / RMII PHY families exist). They mirror the values in main.cpp.
enum { MERGE_OFF = 0, MERGE_HTP = 1, MERGE_LTP = 2 };
enum { LOSS_HOLD = 0, LOSS_ZERO = 1, LOSS_STOP = 2 };
enum { NET_WIFI_STA = 0, NET_WIFI_AP = 1 };
enum { WIRED_FB_RETRY = 0, WIRED_FB_AP = 1, WIRED_FB_REBOOT = 2, WIRED_FB_WIFI = 3 };

#ifndef RMII_PHY_COUNT
#define RMII_PHY_COUNT 6
#endif

// RMII PHY family indices (matches the index into RMII_PHY_NAMES in wifi/ethernet)
enum { RMII_PHY_LAN8720 = 0, RMII_PHY_IP101 = 1, RMII_PHY_RTL8201 = 2,
       RMII_PHY_DP83848 = 3, RMII_PHY_KSZ8081 = 4, RMII_PHY_JL1101 = 5 };

// Ref-clk mode options for RMII (matches ETH_CLOCK_*)
enum { RMII_CLK_GPIO0_IN = 0, RMII_CLK_GPIO0_OUT = 1,
       RMII_CLK_GPIO16_OUT = 2, RMII_CLK_GPIO17_OUT = 3 };

// DMX transmit style
enum { TXSTYLE_CONTINUOUS = 0, TXSTYLE_DELTA = 1 };

// Who set the current TX style on an output
enum { TXSRC_LOCAL = 0, TXSRC_ARTNET = 1 };
