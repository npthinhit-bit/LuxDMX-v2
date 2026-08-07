#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>

// sACN (E1.31) receive constants — byte offsets into the 638-byte minimum packet.
static constexpr int SACN_ACN_ID_OFF    = 4;
static constexpr int SACN_ROOT_VEC_OFF  = 18;
static constexpr int SACN_FRAME_VEC_OFF = 40;
static constexpr int SACN_PRIORITY_OFF  = 108;
static constexpr int SACN_UNIVERSE_OFF  = 113;
static constexpr int SACN_STARTCODE_OFF = 125;
static constexpr int SACN_DATA_OFF      = 126;
static constexpr int SACN_MIN_LEN       = 638;

extern const uint8_t ACN_PACKET_ID[12];

// One multicast socket per enabled output, joined to its sACN universe
// (sACN universe = Art-Net universe + 1). Shared UDP port 5568.
extern WiFiUDP sacnUdp[];
extern uint8_t sacnBuf[];

// Join the multicast group for each enabled output's universe. Call after WiFi/Ethernet is up.
void startSacn();

// Drain + validate all pending packets on one output's sACN socket, dispatching
// each to routeFrame() by the packet's own universe (not the socket's).
void readSacnSocket(int outIdx);

// Drain all outputs' sACN sockets. Called from the netRxTask on core 0.
void readSacn();
