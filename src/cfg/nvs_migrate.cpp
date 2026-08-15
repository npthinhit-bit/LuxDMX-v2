#include "nvs_migrate.h"
#include "config_schema.h"
#include "config_types.h"
#include "config_enums.h"
#include "scene_engine.h"
#include <Preferences.h>
#include <string.h>

namespace nvs_migrate {

static const char* const OUT_PREFIX[] = { "a", "b", "c", "d" };
static const char* const OUT_LEGACY[] = { "o0", "o1", "o2", "o3" };

int migrateNvsKeys(const char* ns) {
    Preferences prefs;
    prefs.begin(ns, false);
    int migrated = 0;

    for (int i = 0; i < 2 && i < MAX_OUTPUTS; i++) {
        for (size_t j = 0; j < OUTPUT_FIELD_COUNT; j++) {
            const CfgOutputField& f = OUTPUT_FIELDS[j];
            char lkey[32], nkey[32];
            snprintf(lkey, sizeof(lkey), "%s_%s", OUT_LEGACY[i], f.suffix);
            snprintf(nkey, sizeof(nkey), "%s_%s", OUT_PREFIX[i],  f.suffix);

            if (prefs.isKey(nkey)) continue;   // already migrated (or never had the old key)
            if (!prefs.isKey(lkey)) continue;

            if (f.kind == CfgKind::Bool) {
                bool v = prefs.getBool(lkey, *(bool*)nullptr);
                prefs.putBool(nkey, v);
            } else {
                int v = prefs.getInt(lkey, 0);
                prefs.putInt(nkey, v);
            }
            prefs.remove(lkey);
            migrated++;
        }
    }

    // apfb -> fbmode migration: an old device that only saved the apFallback bool
    // gets its link-loss policy derived from it.
    if (!prefs.isKey("fbmode") && prefs.isKey("apfb")) {
        bool fb = prefs.getBool("apfb", false);
        prefs.putInt("fbmode", fb ? WIRED_FB_AP : WIRED_FB_RETRY);
        prefs.remove("apfb");
        migrated++;
    }

    prefs.end();

    // Scene key namespacing: prefix old "s{idx}c{chunk}" / "s{idx}m" keys with
    // "scn_" to avoid colliding with future NVS keys. Scenes are stored in the
    // "scenes" namespace (see scene_engine.cpp SCENE_NS), separate from `ns` above,
    // so migrate them with their own Preferences handle. Safe to call repeatably:
    // it skips keys already migrated and drops any stale old key left behind.
    Preferences sprefs;
    if (sprefs.begin("scenes", false)) {
        for (int idx = 0; idx < MAX_SCENES; idx++) {
            // Meta key: "s{idx}m" -> "scn_s{idx}m" (32-byte name+home blob)
            char oldMeta[16], newMeta[24];
            snprintf(oldMeta, sizeof(oldMeta), "s%dm", idx);
            snprintf(newMeta, sizeof(newMeta), "scn_s%dm", idx);
            if (sprefs.isKey(newMeta)) {
                if (sprefs.isKey(oldMeta)) sprefs.remove(oldMeta);
            } else if (sprefs.isKey(oldMeta)) {
                uint8_t buf[32];
                sprefs.getBytes(oldMeta, buf, sizeof(buf));
                sprefs.putBytes(newMeta, buf, sizeof(buf));
                sprefs.remove(oldMeta);
                migrated++;
            }
            // Chunk keys: "s{idx}c{chunk}" -> "scn_s{idx}c{chunk}" (per-output frame)
            for (int o = 0; o < MAX_OUTPUTS; o++) {
                char oldKey[16], newKey[24];
                snprintf(oldKey, sizeof(oldKey), "s%dc%d", idx, o);
                snprintf(newKey, sizeof(newKey), "scn_s%dc%d", idx, o);
                if (sprefs.isKey(newKey)) {
                    if (sprefs.isKey(oldKey)) sprefs.remove(oldKey);
                    continue;
                }
                if (sprefs.isKey(oldKey)) {
                    uint8_t frame[DMX_PACKET_SIZE];
                    sprefs.getBytes(oldKey, frame, sizeof(frame));
                    sprefs.putBytes(newKey, frame, sizeof(frame));
                    sprefs.remove(oldKey);
                    migrated++;
                }
            }
        }
        sprefs.end();
    }

    return migrated;
}

} // namespace nvs_migrate
