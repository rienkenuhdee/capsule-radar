// Aircraft identity via adsbdb.com (free, no API key): GET /v0/aircraft/{hex}.
// Used when a flight has no route — which is every GA aircraft, since routes come
// from airline schedule databases keyed on callsign.
#include "acinfo_client.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

#define ACINFO_CACHE_MAX 150   // wrap before it can crowd NVS
#define ACINFO_FMT_VER   1     // bump to invalidate cached entries if the format changes

void acinfo_cache_begin() {
    Preferences p;
    if (!p.begin("acinfo", false)) return;
    if (p.getUChar("__v", 0) != ACINFO_FMT_VER) { p.clear(); p.putUChar("__v", ACINFO_FMT_VER); }
    p.end();
}

bool acinfo_cache_get(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                      char *owner, size_t on) {
    if (rn) reg[0] = 0;
    if (mn) model[0] = 0;
    if (on) owner[0] = 0;
    if (!hex || !hex[0]) return false;
    Preferences p;
    if (!p.begin("acinfo", true)) return false;
    String v = p.getString(hex, "");    // "reg|model|owner"
    p.end();
    if (v.length() == 0) return false;
    const int b1 = v.indexOf('|');
    if (b1 < 0) return false;
    const int b2 = v.indexOf('|', b1 + 1);
    if (b2 < 0) return false;
    if (rn) snprintf(reg,   rn, "%s", v.substring(0, b1).c_str());
    if (mn) snprintf(model, mn, "%s", v.substring(b1 + 1, b2).c_str());
    if (on) snprintf(owner, on, "%s", v.substring(b2 + 1).c_str());
    return true;
}

void acinfo_cache_put(const char *hex, const char *reg, const char *model, const char *owner) {
    if (!hex || !hex[0]) return;
    Preferences p;
    if (!p.begin("acinfo", false)) return;
    int n = p.getInt("__n", 0);
    if (n >= ACINFO_CACHE_MAX) { p.clear(); p.putUChar("__v", ACINFO_FMT_VER); n = 0; }
    String v = String(reg ? reg : "") + "|" + String(model ? model : "") + "|" +
               String(owner ? owner : "");
    if (p.putString(hex, v) > 0) p.putInt("__n", n + 1);
    p.end();
}

// "DAHER" + "TBM 960" -> "DAHER TBM 960", but skip the manufacturer when the type
// string already leads with it (adsbdb has both "Boeing"/"737NG 85P/W" and
// "Cessna"/"Cessna 172S"), so the label never stutters.
static void join_model(const char *mfr, const char *type, char *out, size_t n) {
    if (!n) return;
    out[0] = 0;
    if (!type || !type[0]) { snprintf(out, n, "%s", mfr ? mfr : ""); return; }
    if (!mfr || !mfr[0])   { snprintf(out, n, "%s", type); return; }
    if (strncasecmp(type, mfr, strlen(mfr)) == 0) { snprintf(out, n, "%s", type); return; }
    snprintf(out, n, "%s %s", mfr, type);
}

bool acinfo_fetch(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                  char *owner, size_t on) {
    if (rn) reg[0] = 0;
    if (mn) model[0] = 0;
    if (on) owner[0] = 0;
    if (!hex || !hex[0] || WiFi.status() != WL_CONNECTED) return false;

    char url[96];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/aircraft/%s", hex);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(3000);   // short: runs on the feed task, don't stall the live poll
    http.setTimeout(6000);
    if (!http.begin(client, url)) return false;
    http.addHeader("User-Agent", ADSB_USER_AGENT);

    const int code = http.GET();
    if (code != 200) { http.end(); return false; }   // 404 = airframe not in the database

    JsonDocument filter;
    filter["response"]["aircraft"]["registration"] = true;
    filter["response"]["aircraft"]["type"] = true;
    filter["response"]["aircraft"]["manufacturer"] = true;
    filter["response"]["aircraft"]["registered_owner"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;

    JsonObjectConst a = doc["response"]["aircraft"].as<JsonObjectConst>();
    if (a.isNull()) return false;

    if (rn) snprintf(reg, rn, "%s", (const char *)(a["registration"] | ""));
    if (mn) join_model((const char *)(a["manufacturer"] | ""),
                       (const char *)(a["type"] | ""), model, mn);
    if (on) snprintf(owner, on, "%s", (const char *)(a["registered_owner"] | ""));
    return (rn && reg[0]) || (mn && model[0]) || (on && owner[0]);
}
