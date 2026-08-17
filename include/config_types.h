#pragma once
#include <Arduino.h>
#include <stddef.h>

// Config schema descriptors -- the SINGLE SOURCE OF TRUTH for every persisted
// setting's STRUCTURE (name, type, constraint). NVS load/save, /info.json, the
// /config web form, and the serial console all iterate these tables.
// Defaults do NOT live here. They come from board TEMPLATES (templates/*.ini).
// Resolution order at load(): neutral (from the constraint) -> active template -> saved NVS.
// "neutral" is derived from the field: a pin (min == -1) -> -1 (disabled);
// an int/enum -> its min (first/off option); bool -> false; string -> "".

enum class CfgKind : uint8_t
{
    Int,
    Bool,
    Str,
    Enum
};

enum CfgFlags : uint16_t
{
    CFG_NONE     = 0,
    CFG_SECRET   = 1 << 0,  // mask the value in serial dumps (passwords)
    CFG_REBOOT   = 1 << 1,  // takes effect only after a reboot
    CFG_READONLY = 1 << 2,  // shown but not settable (runtime/derived)
    CFG_NOWEB    = 1 << 3,  // not part of the /config form (has its own route)
    CFG_KEEPNE   = 1 << 4,  // a blank web field is ignored, never blanks the value
    CFG_LIVE     = 1 << 5,  // applies the instant it is saved (no reboot)
};

struct CfgField
{
    const char*        key;
    const char*        jsonKey;
    CfgKind            kind;
    uint16_t           offset;  // offsetof(Config, member)
    int32_t            min, max;
    const char*        label;
    const char*        group;
    uint16_t           flags;
    const char* const* enumLabels;
    uint8_t            enumCount;
};

struct CfgOutputField
{
    const char*        suffix;
    const char*        jsonKey;
    CfgKind            kind;
    uint16_t           offset;      // offsetof(DmxOutput, member)
    const char*        legacyKey0;  // output-0 legacy NVS fallback (nullptr if none)
    int32_t            min, max;
    const char*        label;
    uint16_t           flags;
    const char* const* enumLabels;
    uint8_t            enumCount;
};

extern const CfgField       CONFIG_FIELDS[];
extern const size_t         CONFIG_FIELD_COUNT;
extern const CfgOutputField OUTPUT_FIELDS[];
extern const size_t         OUTPUT_FIELD_COUNT;
