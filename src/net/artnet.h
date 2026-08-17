#pragma once
#include <stdint.h>

// Art-Net 4 RDM bridge constants — shared by basic receive and the RDM bridge.
static constexpr uint16_t ARTNET_OP_POLL        = 0x2000;
static constexpr uint16_t ARTNET_OP_POLLREPLY   = 0x2100;
static constexpr uint16_t ARTNET_OP_DMX         = 0x5000;
static constexpr uint16_t ARTNET_OP_NZS         = 0x5100;
static constexpr uint16_t ARTNET_OP_SYNC        = 0x5200;
static constexpr uint16_t ARTNET_OP_TODREQUEST  = 0x8000;
static constexpr uint16_t ARTNET_OP_TODDATA     = 0x8100;
static constexpr uint16_t ARTNET_OP_TODCONTROL  = 0x8200;
static constexpr uint16_t ARTNET_OP_RDM         = 0x8300;
static constexpr uint16_t ARTNET_OP_ADDRESS     = 0x6000;
static constexpr uint16_t ARTNET_OP_IPPROG      = 0xf800;
static constexpr uint16_t ARTNET_OP_IPPROGREPLY = 0xf900;
static constexpr uint16_t ARTNET_OP_TIMECODE    = 0x9700;
static constexpr uint16_t ARTNET_OP_TRIGGER     = 0x9900;
static constexpr int      ARTNET_PORT           = 6454;
static const uint8_t      ARTNET_ID[8]          = {'A', 'r', 't', '-', 'N', 'e', 't', 0};

static constexpr uint8_t ATC_FLUSH = 0x01;

// BackgroundQueuePolicy: severity for background status collection.
// 4 = disabled (default); 0..3 = collect NONE/ADVISORY/WARNING/ERROR.
static constexpr uint32_t BQ_POLL_MS = 5000;

// --- Art-Net TimeCode (E1.31 Annex C) ---
struct ArtTimeCode
{
    uint8_t type;  // 0=Film(24), 1=EFG(25), 2=DF(29.97), 3=SMPTE(30)
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t frame;
};

// Single struct holding all ArtNet module state.
// Defined as static in artnet.cpp; access via artNet() accessor.
struct ArtNetState
{
    int           artSock       = -1;
    bool          artRdmReady   = false;
    uint32_t      nodeIp        = 0;
    uint8_t       nodeMac[6]    = {0};
    bool          artRdmEnabled = true;
    uint16_t      artPolls      = 0;
    uint8_t       bqPolicy      = 4;
    volatile bool bqDirty       = false;
    volatile bool artCfgDirty   = false;
    uint8_t       syncMode      = 0;
    uint32_t      syncLastMs    = 0;
    ArtTimeCode   timecode      = {0, 0, 0, 0, 0};
    bool          timecodeValid = false;
    bool          timecodeSend  = false;
    uint8_t       timecodeType  = 0;
    uint8_t       timecodeFps   = 25;
    uint32_t      tcLastSendMs  = 0;
};

ArtNetState& artNet();

// Initialize the raw 6454 UDP socket + bridge state. Called from setup().
void artRdmInit();

// Drain the 6454 socket, parse Art-Net, dispatch. Bounded (8 packets/call -> ring).
void artRdmPollRx();

// Dispatch all enqueued Art-Net packets from the ring buffer (core 0, netRxTask).
void artPktDispatchAll();

// Send queued ArtRdm/ArtTod replies the DMX task produced (core 0 only).
void artRdmDrainResponses();

// ArtSync state — staging mode and last sync timestamp.
static constexpr uint32_t ARTSYNC_TIMEOUT_MS = 1000;

// Commit all staged ArtDMX frames to the live DMX buffers (called when ArtSync
// arrives, or by the DMX task on ArtSync timeout fallback). This is the core-0
// side of the ArtSync staging path — it writes dmxStaged[*] into dmxBuffers[*].
void commitArtSyncStaged();

// ArtNet bridge dispatch: routes control opcodes (poll, sync, address, etc.)
void artnetBridgeDispatch(uint16_t op, const uint8_t* p, int n, uint32_t ip);

// Start TimeCode send task (core 0).
void artnetTimecodeStart();
void artnetTimecodeStop();
