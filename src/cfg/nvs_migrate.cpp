#include "nvs_migrate.h"
#include "config_schema.h"
#include "config_types.h"
#include "config_enums.h"
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
    return migrated;
}

} // namespace nvs_migrate
