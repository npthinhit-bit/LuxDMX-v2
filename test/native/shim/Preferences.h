#pragma once
#include <string>
#include <cstring>
#include <cstdint>

// Minimal Preferences shim for native host tests — stub for NVS key-value store.
class Preferences {
public:
    bool begin(const char* ns, bool rw = true) { return true; }
    void end() {}
    bool isKey(const char* key) { return false; }
    void putBytes(const char* key, const void* val, size_t len) {}
    void getBytes(const char* key, void* out, size_t len) {}
    void putUChar(const char* key, uint8_t val) {}
    uint8_t getUChar(const char* key, uint8_t defaultVal = 0) { return defaultVal; }
    bool getBool(const char* key, bool defaultVal = false) { return defaultVal; }
    void putBool(const char* key, bool val) {}
    int getInt(const char* key, int defaultVal = 0) { return defaultVal; }
    void putInt(const char* key, int val) {}
    void putUInt(const char* key, uint32_t val) {}
    uint32_t getUInt(const char* key, uint32_t defaultVal = 0) { return defaultVal; }
    void putULong(const char* key, unsigned long val) {}
    unsigned long getULong(const char* key, unsigned long defaultVal = 0) { return defaultVal; }
    void putString(const char* key, const String& val) {}
    String getString(const char* key, const String& defaultVal = "") { return defaultVal; }
    void remove(const char* key) {}
};
