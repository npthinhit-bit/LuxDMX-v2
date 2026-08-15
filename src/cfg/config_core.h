#pragma once
#include <Arduino.h>
#include <esp_err.h>
#include "config_schema.h"
#include "config_types.h"

namespace cfgcore {

void load();
void save();
esp_err_t setValue(const String& key, const String& val, String& err);
esp_err_t getValue(const String& key, String& out);
void dump(String& out, bool maskSecrets);
esp_err_t applyTemplate(const String& name, String& err);
esp_err_t applyTemplateText(const char* text, String& err, int depth = 0);
void resetToTemplate();
esp_err_t resetTo(const String& name, String& err);
    esp_err_t importJson(const String& json, String& err);
    void exportJson(String& out, bool maskSecrets = true);
    void exportXml(String& out, bool maskSecrets = true);
    esp_err_t importXml(const String& xml, String& err);

}; // namespace cfgcore

// Convenience wrappers used across modules that don't want to reach into cfgcore.
inline void saveConfig() { cfgcore::save(); }
inline void loadConfig() { cfgcore::load(); }

struct CfgTemplate { const char* name; const char* text; };
extern const CfgTemplate CONFIG_TEMPLATES[];
extern const size_t CONFIG_TEMPLATE_COUNT;
