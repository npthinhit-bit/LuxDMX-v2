#include "config_serial.h"
#include <stdlib.h>

namespace cfgserial {

static Stream* g_io = nullptr;
static Hooks   g_hooks;
static String  g_line;
static bool    g_announced = false;

void begin(Stream& io, const Hooks& hooks) {
    g_io        = &io;
    g_hooks     = hooks;
    g_line      = "";
    g_announced = false;
}

static void appendChar(String& s, char c) { char b[2] = {c, 0}; s += b; }

static String helpText() {
    return
        "LuxDMX-v2 config:\n"
        "  dump                 print every setting as key=value\n"
        "  <key>=<value> ...    set one or more fields (partial)\n"
        "  get <key>            -> key=value\n"
        "  set <key> <value>    -> OK | ERR\n"
        "  save [reboot]        persist to NVS; reboot if asked\n"
        "  wifi <ssid> [pass]   set WiFi credentials and reconnect\n"
        "  reboot | factory     restart / wipe + restart\n"
        "  help";
}

static String applyKv(const String& line) {
    int ok = 0, fail = 0; String firstErr;
    int i = 0, n = line.length();
    while (i < n) {
        while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == ',')) i++;
        int start = i;
        while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != ',') i++;
        if (i <= start) continue;
        String tok = line.substring(start, i);
        int eq = tok.indexOf('=');
        if (eq < 0) { fail++; if (!firstErr.length()) firstErr = String("bad token: ") + tok; continue; }
        String k = tok.substring(0, eq), v = tok.substring(eq + 1);
        String e;
        if (cfgcore::setValue(k, v, e) == ESP_OK) ok++;
        else { fail++; if (!firstErr.length()) firstErr = e; }
    }
    if (fail) return String("ERR ") + firstErr + " (" + ok + " ok, " + fail + " failed)";
    return String("OK ") + ok;
}

String execute(const String& line) {
    String l = line; l.trim();
    if (l.length() == 0) return "";

    int sp = l.indexOf(' ');
    String verb = sp < 0 ? l : l.substring(0, sp);
    String rest = sp < 0 ? String("") : l.substring(sp + 1);
    rest.trim();

    if (verb.indexOf('=') >= 0) return applyKv(l);

    verb.toLowerCase();
    if (verb == "help" || verb == "?") return helpText();
    if (verb == "dump") { String s; cfgcore::dump(s, true); return s; }
    if (verb == "get") {
        String v;
        if (rest.length() && cfgcore::getValue(rest, v) == ESP_OK) return rest + "=" + v;
        return String("ERR unknown key: ") + rest;
    }
    if (verb == "set") {
        int s2 = rest.indexOf(' ');
        if (s2 < 0) return "ERR usage: set <key> <value>";
        String k = rest.substring(0, s2);
        String v = rest.substring(s2 + 1); v.trim();
        String e;
        return cfgcore::setValue(k, v, e) == ESP_OK ? String("OK") : (String("ERR ") + e);
    }
    if (verb == "save") {
        bool rb = (rest == "reboot");
        if (g_hooks.save) g_hooks.save(rb);
        return rb ? "OK saved, rebooting" : "OK saved";
    }
    if (verb == "wifi") {
        int s2 = rest.indexOf(' ');
        String ssid = s2 < 0 ? rest : rest.substring(0, s2);
        String pass = s2 < 0 ? String("") : rest.substring(s2 + 1);
        pass.trim();
        if (ssid.length() == 0) return "ERR usage: wifi <ssid> [password]";
        if (g_hooks.wifi && g_hooks.wifi(ssid, pass)) return String("OK wifi set, connecting to ") + ssid;
        return "ERR wifi not available";
    }
    if (verb == "reboot")  { if (g_hooks.reboot)  g_hooks.reboot();  return "OK rebooting"; }
    if (verb == "factory") { if (g_hooks.factory) g_hooks.factory(); return "OK factory reset, rebooting"; }
    return String("ERR unknown command: ") + verb + " (try 'help')";
}

void poll() {
    if (!g_io) return;
    if (!g_announced) {
        g_announced = true;
        g_io->println("[cfg] serial config ready - type 'help' (or 'dump')");
    }
    while (g_io->available() > 0) {
        int c = g_io->read();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (g_line.length() == 0) continue;
            String resp = execute(g_line);
            g_line = "";
            if (resp.length()) g_io->println(resp);
        } else if (g_line.length() < 600) {
            appendChar(g_line, (char)c);
        }
    }
}

}  // namespace cfgserial
