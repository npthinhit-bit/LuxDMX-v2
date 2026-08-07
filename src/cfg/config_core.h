#pragma once
#include <Arduino.h>
#include "config_schema.h"
#include "config_types.h"

namespace cfgcore {

void load();
void save();
bool setValue(const String& key, const String& val, String& err);
bool getValue(const String& key, String& out);
void dump(String& out, bool maskSecrets);
bool applyTemplate(const String& name, String& err);
bool applyTemplateText(const char* text, String& err, int depth = 0);
void resetToTemplate();
bool resetTo(const String& name, String& err);

}; // namespace cfgcore

// Convenience wrappers used across modules that don't want to reach into cfgcore.
inline void saveConfig() { cfgcore::save(); }
inline void loadConfig() { cfgcore::load(); }

struct CfgTemplate { const char* name; const char* text; };
extern const CfgTemplate CONFIG_TEMPLATES[];
extern const size_t CONFIG_TEMPLATE_COUNT;
