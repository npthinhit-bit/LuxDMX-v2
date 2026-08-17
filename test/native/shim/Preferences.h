#pragma once
#include "Arduino.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

// In-memory Preferences shim for native host tests -- stores key/value pairs
// so that save()/load() round-trips work correctly. The real NVS is not
// available on the host, so this static map stands in for it.
class Preferences
{
    static std::map<std::string, std::string>& storage()
    {
        static std::map<std::string, std::string> s;
        return s;
    }

public:
    bool begin(const char* ns, bool rw = true)
    {
        (void)ns;
        (void)rw;
        return true;
    }
    void end() {}

    static void clearAll()
    {
        storage().clear();
    }

    bool isKey(const char* key)
    {
        return storage().find(key) != storage().end();
    }
    void putBytes(const char* key, const void* val, size_t len)
    {
        std::string& v = storage()[key];
        v.resize(len);
        if (len)
            std::memcpy(&v[0], val, len);
    }
    void getBytes(const char* key, void* out, size_t len)
    {
        auto it = storage().find(key);
        if (it == storage().end())
        {
            std::memset(out, 0, len);
            return;
        }
        size_t copy = std::min(len, it->second.size());
        std::memcpy(out, it->second.data(), copy);
        if (it->second.size() < len)
            std::memset((char*)out + it->second.size(), 0, len - it->second.size());
    }
    void putUChar(const char* key, uint8_t val)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)val);
        storage()[key] = buf;
    }
    uint8_t getUChar(const char* key, uint8_t defaultVal = 0)
    {
        auto it = storage().find(key);
        return it != storage().end() ? (uint8_t)atoi(it->second.c_str()) : defaultVal;
    }
    bool getBool(const char* key, bool defaultVal = false)
    {
        auto it = storage().find(key);
        if (it == storage().end())
            return defaultVal;
        return it->second == "true";
    }
    void putBool(const char* key, bool val)
    {
        storage()[key] = val ? "true" : "false";
    }
    int getInt(const char* key, int defaultVal = 0)
    {
        auto it = storage().find(key);
        return it != storage().end() ? atoi(it->second.c_str()) : defaultVal;
    }
    void putInt(const char* key, int val)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", val);
        storage()[key] = buf;
    }
    void putUInt(const char* key, uint32_t val)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", (unsigned)val);
        storage()[key] = buf;
    }
    uint32_t getUInt(const char* key, uint32_t defaultVal = 0)
    {
        auto it = storage().find(key);
        return it != storage().end() ? (uint32_t)strtoul(it->second.c_str(), nullptr, 10) : defaultVal;
    }
    void putULong(const char* key, unsigned long val)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", val);
        storage()[key] = buf;
    }
    unsigned long getULong(const char* key, unsigned long defaultVal = 0)
    {
        auto it = storage().find(key);
        return it != storage().end() ? strtoul(it->second.c_str(), nullptr, 10) : defaultVal;
    }
    void putString(const char* key, const String& val)
    {
        storage()[key] = val.c_str();
    }
    String getString(const char* key, const String& defaultVal = "")
    {
        auto it = storage().find(key);
        return it != storage().end() ? String(it->second.c_str()) : defaultVal;
    }
    void remove(const char* key)
    {
        storage().erase(key);
    }
};
