#pragma once
#include <ETH.h>

// RMII PHY family name table — index matches the RMII PHY enum in config_enums.h
// (RMII_PHY_LAN8720 = 0 .. RMII_PHY_JL1101 = 5).
#if defined(HAS_ETH_RMII)
static const char* const RMII_PHY_NAMES[] = {"LAN8720", "IP101", "RTL8201", "DP83848", "KSZ8081", "JL1101"};

// Map a config_rmiiPhy enum index to the eth_phy_type_t the ETH.begin() call expects.
static inline eth_phy_type_t rmiiPhyType(int idx)
{
    switch (idx)
    {
    case 0:
        return ETH_PHY_LAN8720;
    case 1:
        return ETH_PHY_TLK110;  // IP101 is pin-compatible, TLK110 driver works
    case 2:
        return ETH_PHY_RTL8201;
    case 3:
        return ETH_PHY_DP83848;
    case 4:
        return ETH_PHY_KSZ8081;
    case 5:
        return ETH_PHY_JL1101;  // falls through to GENERIC on some SDKs
    default:
        return ETH_PHY_LAN8720;
    }
}
#endif
