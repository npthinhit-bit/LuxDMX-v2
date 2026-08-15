#pragma once
#include <stdint.h>
#include "rdm_types.h"
#include "config_schema.h"

// Scene count: 32 for PSRAM targets (S3), 8 for non-PSRAM targets (ESP32).
// Override via -DCONFIG_LUXDMX_MAX_SCENES=N build flag.
#ifndef CONFIG_LUXDMX_MAX_SCENES
  #ifdef CONFIG_SPIRAM_SUPPORT
    #define CONFIG_LUXDMX_MAX_SCENES 32
  #else
    #define CONFIG_LUXDMX_MAX_SCENES 8
  #endif
#endif
#define MAX_SCENES CONFIG_LUXDMX_MAX_SCENES

struct Scene {
    char     name[32];
    uint16_t fadeTimeMs;
    uint8_t  triggerMask;   // bitfield: which triggers can fire this scene
    uint8_t  priority;       // scene priority vs live network data (0-255)
    uint8_t  data[MAX_OUTPUTS][DMX_PACKET_SIZE];  // per-output frame (start code + 512 slots)
    bool     active;
};

extern Scene* g_scenes;
extern uint8_t g_sceneHome;

// Load/save a single scene to NVS.
bool sceneSaveNvs(int idx);
bool sceneLoadNvs(int idx);
bool sceneEraseNvs(int idx);

// Load all scenes from NVS (call at boot).
void sceneLoadAll();

// Recall a scene: starts the fade engine on the given output (or all if -1).
void sceneRecall(int presetIdx, uint16_t fadeMs, int outIdx);

// Recall the "home" scene.
void sceneRecallHome(int outIdx);
void sceneFadeStep();

// Manual scene trigger from web UI / serial.
// WebSocket command: { "cmd": "scene", "play": <n>, "fade": <ms> }
bool sceneTriggerPlay(int idx, uint16_t fadeMs);
void sceneSave(int idx);
void sceneCheckTimecodeTrigger();
