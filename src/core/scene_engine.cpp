// Scene engine + fade engine + NVS storage.
// Stores up to MAX_SCENES presets, each with a full 4-universe DMX frame
// (2048 bytes = 4 x 513). NVS has a 512-byte blob limit, so each scene is
// stored as 4 chunks (one per output) in separate NVS keys.
// The fade engine runs on core 1 (DMX task) — it linearly interpolates
// channel values from the current frame to the target frame over fadeMs.
#include "scene_engine.h"
#include "dmx_buffer.h"
#include "config_schema.h"
#include "sender_tracker.h"
#include "frame_router.h"
#include "artnet.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#ifdef CONFIG_SPIRAM_SUPPORT
#include <esp_heap_caps.h>
#endif

// Allocate scenes from PSRAM when available, internal heap otherwise.
Scene* g_scenes = nullptr;
uint8_t g_sceneHome = 0;

static void allocScenes() {
    if (g_scenes) return;
#ifdef CONFIG_SPIRAM_SUPPORT
    g_scenes = (Scene*)heap_caps_malloc(sizeof(Scene) * MAX_SCENES, MALLOC_CAP_SPIRAM);
    if (!g_scenes) {
        // Fallback to internal if PSRAM allocation fails
        g_scenes = (Scene*)malloc(sizeof(Scene) * MAX_SCENES);
    }
#else
    g_scenes = (Scene*)malloc(sizeof(Scene) * MAX_SCENES);
#endif
    if (g_scenes) memset(g_scenes, 0, sizeof(Scene) * MAX_SCENES);
}

// --- Fade state ---
struct FadeState {
    bool     active;
    uint32_t startMs;
    uint32_t durationMs;
    uint8_t  from[DMX_PACKET_SIZE];
    uint8_t  to[DMX_PACKET_SIZE];
    int      outIdx;
};
static FadeState g_fade[MAX_OUTPUTS];

// --- NVS storage ---
static const char* SCENE_NS = "scenes";

static String sceneKey(int idx, int chunk) {
    return "scn_s" + String(idx) + "c" + String(chunk);
}

static String sceneMetaKey(int idx) {
    return "scn_s" + String(idx) + "m";
}

bool sceneSaveNvs(int idx) {
    if (idx < 0 || idx >= MAX_SCENES) return false;
    if (!g_scenes) return false;
    Preferences p;
    if (!p.begin(SCENE_NS, false)) return false;
    char nameBuf[32];
    memset(nameBuf, 0, sizeof(nameBuf));
    strncpy(nameBuf, g_scenes[idx].name, 31);
    p.putBytes(sceneMetaKey(idx).c_str(), nameBuf, 32);
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        p.putBytes(sceneKey(idx, o).c_str(), g_scenes[idx].data[o], DMX_PACKET_SIZE);
    }
    p.end();
    return true;
}

bool sceneLoadNvs(int idx) {
    if (idx < 0 || idx >= MAX_SCENES) return false;
    if (!g_scenes) return false;
    Preferences p;
    if (!p.begin(SCENE_NS, true)) return false;
    if (!p.isKey(sceneMetaKey(idx).c_str())) { p.end(); return false; }
    char nameBuf[32];
    p.getBytes(sceneMetaKey(idx).c_str(), nameBuf, 32);
    strncpy(g_scenes[idx].name, nameBuf, 31);
    g_scenes[idx].name[31] = '\0';
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        p.getBytes(sceneKey(idx, o).c_str(), g_scenes[idx].data[o], DMX_PACKET_SIZE);
    }
    g_scenes[idx].active = false;
    p.end();
    return true;
}

bool sceneEraseNvs(int idx) {
    if (idx < 0 || idx >= MAX_SCENES) return false;
    Preferences p;
    if (!p.begin(SCENE_NS, false)) return false;
    p.remove(sceneMetaKey(idx).c_str());
    for (int o = 0; o < MAX_OUTPUTS; o++)
        p.remove(sceneKey(idx, o).c_str());
    p.end();
    memset(&g_scenes[idx], 0, sizeof(Scene));
    return true;
}

void sceneLoadAll() {
    allocScenes();
    Preferences p;
    p.begin(SCENE_NS, true);
    for (int i = 0; i < MAX_SCENES; i++) {
        if (sceneLoadNvs(i)) {
            g_scenes[i].active = false;
        }
    }
    p.end();
    // Load home scene index
    Preferences hp;
    if (hp.begin("dmxgw", true)) {
        g_sceneHome = hp.getUChar("home", 0);
        hp.end();
    }
}

void sceneSave(int idx) {
    if (idx < 0 || idx >= MAX_SCENES) return;
    sceneSaveNvs(idx);
    Preferences p;
    if (p.begin("dmxgw", false)) {
        p.putUChar("home", g_sceneHome);
        p.end();
    }
}

// --- Fade engine ---
void sceneRecall(int presetIdx, uint16_t fadeMs, int outIdx) {
    if (!g_scenes) return;
    if (presetIdx < 0 || presetIdx >= MAX_SCENES) return;
    Scene& sc = g_scenes[presetIdx];
    if (outIdx < 0) {
        // Recall on all enabled outputs
        for (int o = 0; o < MAX_OUTPUTS; o++) {
            if (!cfg.outputs[o].enabled) continue;
            sceneRecall(presetIdx, fadeMs, o);
        }
        return;
    }
    // Snapshot current frame as fade source
    uint8_t cur[DMX_PACKET_SIZE];
    if (!dmxBufSnapshot(outIdx, cur)) {
        memset(cur, 0, sizeof(cur));
    }
    FadeState& fs = g_fade[outIdx];
    fs.active = true;
    fs.startMs = millis();
    fs.durationMs = fadeMs;
    memcpy(fs.from, cur, DMX_PACKET_SIZE);
    memcpy(fs.to, sc.data[outIdx], DMX_PACKET_SIZE);
    fs.outIdx = outIdx;
    g_scenes[presetIdx].active = true;
}

void sceneRecallHome(int outIdx) {
    sceneRecall(g_sceneHome, 0, outIdx);
}

// Step the fade engine. Called from dmxTxTask or main loop.
void sceneFadeStep() {
    uint32_t now = millis();
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        FadeState& fs = g_fade[o];
        if (!fs.active) continue;
        uint32_t elapsed = now - fs.startMs;
        if (elapsed >= fs.durationMs) {
            // Fade complete
            dmxBufWriteBegin(o);
            memcpy(&dmxBuffers[o].data[1], fs.to + 1, 512);
            dmxBufWriteEnd(o);
            fs.active = false;
        } else if (fs.durationMs > 0) {
            // Linear interpolate
            float frac = (float)elapsed / (float)fs.durationMs;
            dmxBufWriteBegin(o);
            for (int c = 1; c < DMX_PACKET_SIZE; c++) {
                int32_t from = fs.from[c];
                int32_t to = fs.to[c];
                dmxBuffers[o].data[c] = (uint8_t)(from + (to - from) * frac);
            }
            dmxBufWriteEnd(o);
        }
    }
}

// Manual scene trigger from web UI / serial.
// WebSocket command: { "cmd": "scene", "play": <n>, "fade": <ms> }
bool sceneTriggerPlay(int idx, uint16_t fadeMs) {
    if (idx < 0 || idx >= MAX_SCENES) return false;
    sceneRecall(idx, fadeMs, -1);
    return true;
}

// Called when a new Art-Net TimeCode frame arrives.
// Scene triggerMask bits: 0=timecode trigger enabled for this scene.
void sceneCheckTimecodeTrigger() {
    if (!g_scenes) return;
    // Placeholder: Phase 3 proper will implement per-scene timecode matching
    // (e.g. "play scene N at hh:mm:ss:frame"). For now, scenes with
    // triggerMask bit 0 set fire whenever any timecode arrives.
    for (int i = 0; i < MAX_SCENES; i++) {
        if (g_scenes[i].active) continue;
        if (g_scenes[i].triggerMask & 0x01) {
            sceneTriggerPlay(i, g_scenes[i].fadeTimeMs);
        }
    }
}
