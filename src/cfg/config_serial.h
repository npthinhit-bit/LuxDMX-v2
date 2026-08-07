#pragma once
#include <Arduino.h>
#include "config_core.h"

namespace cfgserial {

struct Hooks {
    void (*save)(bool reboot)                           = nullptr;
    void (*reboot)()                                    = nullptr;
    void (*factory)()                                   = nullptr;
    bool (*wifi)(const String& ssid, const String& pass) = nullptr;
};

void begin(Stream& io, const Hooks& hooks);
void poll();
String execute(const String& line);

}  // namespace cfgserial
