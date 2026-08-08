#include "config_core.h"
#include "config_enums.h"
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>

// Live configuration singleton — defined here so every module sees one copy.
// On the firmware build NVS.load() populates it; host tests redefine their
// own via the test/ TU (which is excluded from the firmware build_src_filter).
Config cfg;

#ifndef CFG_PREF_NS
#define CFG_PREF_NS "dmxgw"
#endif

#ifndef DEFAULT_TEMPLATE
#define DEFAULT_TEMPLATE _base
#endif
#define CFG_STR2(x) #x
#define CFG_STR(x)  CFG_STR2(x)

static const char* const OUT_KEY_PREFIXES[] = { "a", "b", "c", "d" };
static const char* const OUT_KEY_LEGACY[]   = { "o0", "o1", "o2", "o3" };
static const int OUT_KEY_PREFIX_COUNT = (int)(sizeof(OUT_KEY_PREFIXES) / sizeof(OUT_KEY_PREFIXES[0]));

namespace cfgcore {

static String outKey(int i, const char* suffix) {
    if (i < 0 || i >= OUT_KEY_PREFIX_COUNT) return String("o") + i + "_" + suffix;
    return String(OUT_KEY_PREFIXES[i]) + "_" + suffix;
}
static String outKeyLegacy(int i, const char* suffix) {
    return String("o") + i + "_" + suffix;
}

static void* rootAddr(const CfgField& f)            { return (char*)&cfg + f.offset; }
static void* outAddr(int i, const CfgOutputField& f){ return (char*)&cfg.outputs[i] + f.offset; }

static int neutralInt(int32_t mn) { return mn <= -1 ? -1 : (int)mn; }

static void writeTyped(void* a, CfgKind kind, int32_t mn, int32_t mx, const String& val) {
    switch (kind) {
        case CfgKind::Bool:  *(bool*)a = (val == "1" || val == "true" || val == "on" || val == "yes"); break;
        case CfgKind::Int:
        case CfgKind::Enum: { long v = atol(val.c_str()); *(int*)a = (int)constrain(v, mn, mx); break; }
        case CfgKind::Str:   *(String*)a = val; break;
    }
}

static String readTyped(void* a, CfgKind kind) {
    switch (kind) {
        case CfgKind::Bool:           return *(bool*)a ? "true" : "false";
        case CfgKind::Int:
        case CfgKind::Enum:           return String(*(int*)a);
        default:                      return *(String*)a;
    }
}

static bool resolve(const String& key, void*& a, CfgKind& kind, int32_t& mn, int32_t& mx, uint16_t& flags) {
    const char* k = key.c_str();
    int idx = -1; const char* suf = nullptr;
    if (k[0] >= 'a' && k[0] <= 'd' && k[0] < 'a' + OUT_KEY_PREFIX_COUNT && k[1] == '_') {
        idx = k[0] - 'a'; suf = k + 2;
    }
    if (idx < 0 && k[0] == 'o' && k[1] >= '0' && k[1] <= '9' && k[2] == '_') {
        idx = k[1] - '0'; suf = k + 3;
    }
    if (idx >= 0 && idx < MAX_OUTPUTS && suf) {
        for (size_t j = 0; j < OUTPUT_FIELD_COUNT; j++)
            if (strcmp(OUTPUT_FIELDS[j].suffix, suf) == 0) {
                const CfgOutputField& f = OUTPUT_FIELDS[j];
                a = outAddr(idx, f); kind = f.kind; mn = f.min; mx = f.max; flags = f.flags;
                return true;
            }
        return false;
    }
    for (size_t j = 0; j < CONFIG_FIELD_COUNT; j++)
        if (strcmp(CONFIG_FIELDS[j].key, k) == 0) {
            const CfgField& f = CONFIG_FIELDS[j];
            a = rootAddr(f); kind = f.kind; mn = f.min; mx = f.max; flags = f.flags;
            return true;
        }
    return false;
}

bool setValue(const String& key, const String& val, String& err) {
    void* a; CfgKind kind; int32_t mn, mx; uint16_t flags;
    if (!resolve(key, a, kind, mn, mx, flags)) { err = String("unknown key: ") + key; return false; }
    writeTyped(a, kind, mn, mx, val);
    return true;
}

bool getValue(const String& key, String& out) {
    void* a; CfgKind kind; int32_t mn, mx; uint16_t flags;
    if (!resolve(key, a, kind, mn, mx, flags)) return false;
    out = readTyped(a, kind);
    return true;
}

static void applyNeutral() {
    for (size_t j = 0; j < CONFIG_FIELD_COUNT; j++) {
        const CfgField& f = CONFIG_FIELDS[j]; void* a = rootAddr(f);
        if (f.kind == CfgKind::Bool)      *(bool*)a   = false;
        else if (f.kind == CfgKind::Str)  *(String*)a = "";
        else                              *(int*)a    = neutralInt(f.min);
    }
    for (int i = 0; i < MAX_OUTPUTS; i++)
        for (size_t j = 0; j < OUTPUT_FIELD_COUNT; j++) {
            const CfgOutputField& f = OUTPUT_FIELDS[j]; void* a = outAddr(i, f);
            if (f.kind == CfgKind::Bool)      *(bool*)a   = false;
            else if (f.kind == CfgKind::Str)  *(String*)a = "";
            else                              *(int*)a    = neutralInt(f.min);
        }
}

static bool applyNamed(const String& name, String& err, int depth);

bool applyTemplateText(const char* text, String& err, int depth) {
    if (depth > 8) { err = "template nesting too deep"; return false; }
    char line[192];
    const char* p = text;
    while (*p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len); line[len] = 0;
        p = nl ? nl + 1 : p + len;

        char* s = line; while (*s == ' ' || *s == '\t') s++;
        char* e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
        if (*s == 0 || *s == '#') continue;
        char* eq = strchr(s, '='); if (!eq) continue;
        *eq = 0; char* k = s; char* v = eq + 1;
        char* ke = k + strlen(k); while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;
        while (*v == ' ' || *v == '\t') v++;

        if (strcmp(k, "extends") == 0) { if (!applyNamed(String(v), err, depth + 1)) return false; continue; }
        String e2; if (!setValue(String(k), String(v), e2)) { err = e2; return false; }
    }
    return true;
}

static bool applyNamed(const String& name, String& err, int depth) {
    for (size_t i = 0; i < CONFIG_TEMPLATE_COUNT; i++)
        if (strcmp(CONFIG_TEMPLATES[i].name, name.c_str()) == 0)
            return applyTemplateText(CONFIG_TEMPLATES[i].text, err, depth);
    err = String("unknown template: ") + name;
    return false;
}

bool applyTemplate(const String& name, String& err) { return applyNamed(name, err, 1); }

void resetToTemplate() {
    applyNeutral();
    String err; applyTemplate(CFG_STR(DEFAULT_TEMPLATE), err);
}

bool resetTo(const String& name, String& err) {
    applyNeutral();
    return applyTemplate(name, err);
}

void load() {
    resetToTemplate();

    Preferences prefs; prefs.begin(CFG_PREF_NS, false);
    for (size_t j = 0; j < CONFIG_FIELD_COUNT; j++) {
        const CfgField& f = CONFIG_FIELDS[j]; void* a = rootAddr(f);
        if (f.kind == CfgKind::Bool)      *(bool*)a = prefs.getBool(f.key, *(bool*)a);
        else if (f.kind == CfgKind::Str)  { if (prefs.isKey(f.key)) *(String*)a = prefs.getString(f.key, *(String*)a); }
        else { int v = prefs.getInt(f.key, *(int*)a); *(int*)a = (int)constrain(v, f.min, f.max); }
    }
    for (int i = 0; i < MAX_OUTPUTS; i++)
        for (size_t j = 0; j < OUTPUT_FIELD_COUNT; j++) {
            const CfgOutputField& f = OUTPUT_FIELDS[j]; void* a = outAddr(i, f);
            String key = outKey(i, f.suffix);
            bool migrated = false;
            if (i < 2 && !prefs.isKey(key.c_str())) {
                String legacy = outKeyLegacy(i, f.suffix);
                if (prefs.isKey(legacy.c_str())) {
                    if (f.kind == CfgKind::Bool) {
                        bool v = prefs.getBool(legacy.c_str(), *(bool*)a);
                        *(bool*)a = v; prefs.putBool(key.c_str(), v);
                    } else {
                        int base = *(int*)a;
                        if (f.legacyKey0 && i == 0) base = prefs.getInt(f.legacyKey0, base);
                        int v = prefs.getInt(legacy.c_str(), base);
                        *(int*)a = (int)constrain(v, f.min, f.max);
                        prefs.putInt(key.c_str(), *(int*)a);
                    }
                    prefs.remove(legacy.c_str());
                    migrated = true;
                }
            }
            if (migrated) continue;
            if (f.kind == CfgKind::Bool) { *(bool*)a = prefs.getBool(key.c_str(), *(bool*)a); continue; }
            int base = *(int*)a;
            if (f.legacyKey0 && i == 0) base = prefs.getInt(f.legacyKey0, base);
            int v = prefs.getInt(key.c_str(), base);
            *(int*)a = (int)constrain(v, f.min, f.max);
        }
    if (!prefs.isKey("fbmode") && prefs.isKey("apfb"))
        cfg.linkLossMode = prefs.getBool("apfb", false) ? WIRED_FB_AP : WIRED_FB_RETRY;
    prefs.end();

    cfg.apFallback = (cfg.linkLossMode == WIRED_FB_AP);
}

void save() {
    Preferences prefs; prefs.begin(CFG_PREF_NS, false);
    for (size_t j = 0; j < CONFIG_FIELD_COUNT; j++) {
        const CfgField& f = CONFIG_FIELDS[j]; void* a = rootAddr(f);
        if (f.kind == CfgKind::Bool)      prefs.putBool(f.key, *(bool*)a);
        else if (f.kind == CfgKind::Str)  prefs.putString(f.key, *(String*)a);
        else                              prefs.putInt(f.key, *(int*)a);
    }
    for (int i = 0; i < MAX_OUTPUTS; i++)
        for (size_t j = 0; j < OUTPUT_FIELD_COUNT; j++) {
            const CfgOutputField& f = OUTPUT_FIELDS[j]; void* a = outAddr(i, f);
            String key = outKey(i, f.suffix);
            if (f.kind == CfgKind::Bool) prefs.putBool(key.c_str(), *(bool*)a);
            else                         prefs.putInt(key.c_str(), *(int*)a);
        }
    prefs.putBool("apfb", cfg.linkLossMode == WIRED_FB_AP);
    prefs.end();
}

void dump(String& out, bool maskSecrets) {
    for (size_t j = 0; j < CONFIG_FIELD_COUNT; j++) {
        const CfgField& f = CONFIG_FIELDS[j];
        String v; getValue(f.key, v);
        if (maskSecrets && (f.flags & CFG_SECRET)) v = "***";
        out += f.key; out += "="; out += v; out += "\n";
    }
    for (int i = 0; i < MAX_OUTPUTS; i++)
        for (size_t j = 0; j < OUTPUT_FIELD_COUNT; j++) {
            const CfgOutputField& f = OUTPUT_FIELDS[j];
            String key = outKey(i, f.suffix);
            String v; getValue(key, v);
            if (maskSecrets && (f.flags & CFG_SECRET)) v = "***";
            out += key; out += "="; out += v; out += "\n";
        }
}

void exportJson(String& out, bool maskSecrets) {
    out = "{";
    bool first = true;
    for (size_t j = 0; j < CONFIG_FIELD_COUNT; j++) {
        const CfgField& f = CONFIG_FIELDS[j];
        if (!first) out += ",";
        first = false;
        String v; getValue(f.key, v);
        if (maskSecrets && (f.flags & CFG_SECRET)) v = "***";
        char buf[32];
        out += "\"" + String(f.jsonKey) + "\":";
        if (f.kind == CfgKind::Str) {
            out += "\"" + v + "\"";
        } else if (f.kind == CfgKind::Bool) {
            out += v;
        } else {
            out += v;
        }
    }
    out += ",\"outputs\":[";
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (i) out += ",";
        out += "{";
        bool of = true;
        for (size_t j = 0; j < OUTPUT_FIELD_COUNT; j++) {
            const CfgOutputField& f = OUTPUT_FIELDS[j];
            if (!of) out += ",";
            of = false;
            String key = outKey(i, f.suffix);
            String v; getValue(key, v);
            if (maskSecrets && (f.flags & CFG_SECRET)) v = "***";
            out += "\"" + String(f.jsonKey) + "\":";
            if (f.kind == CfgKind::Str) out += "\"" + v + "\"";
            else if (f.kind == CfgKind::Bool) out += v;
            else out += v;
        }
        out += "}";
    }
    out += "]}";
}

bool importJson(const String& json, String& err) {
    // Simple line-based import: parse "key=value" pairs from JSON-like text.
    // Full JSON parser is overkill for this embedded use case; we scan for
    // "key":"value" or "key":value patterns.
    const char* p = json.c_str();
    int len = json.length();
    int pos = 0;
    bool ok = true;

    while (pos < len) {
        // Find key
        int kq = json.indexOf('"', pos);
        if (kq < 0) break;
        int kq2 = json.indexOf('"', kq + 1);
        if (kq2 < 0) break;
        String key = json.substring(kq + 1, kq2);
        pos = kq2 + 1;

        // Find colon
        int colon = json.indexOf(':', pos);
        if (colon < 0) break;
        pos = colon + 1;

        // Find value
        int vq = json.indexOf('"', pos);
        if (vq >= 0 && vq < colon + 5) {
            int vq2 = json.indexOf('"', vq + 1);
            if (vq2 < 0) break;
            String val = json.substring(vq + 1, vq2);
            pos = vq2 + 1;
            String e2;
            if (!setValue(key, val, e2)) ok = false;
        } else {
            // Numeric or boolean value
            int valEnd = pos;
            while (valEnd < len && json[valEnd] != ',' && json[valEnd] != '}' && json[valEnd] != '\n')
                valEnd++;
            String val = json.substring(pos, valEnd);
            val.trim();
            pos = valEnd;
            String e2;
            if (!setValue(key, val, e2)) ok = false;
        }
    }
    if (!ok) err = "some keys not recognized";
    return ok;
}

} // namespace cfgcore
