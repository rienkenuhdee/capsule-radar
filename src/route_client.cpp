// Route lookup via adsbdb.com (free, no API key): GET /v0/callsign/{callsign}.
// Returns origin/destination city names (English). Device-only.
#include "route_client.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>
#include <math.h>   // NAN: "no destination coords" sentinel
#include <time.h>   // route-cache TTL

#define ROUTE_CACHE_MAX 200   // wrap the cache before it can crowd NVS

// strip spaces -> a valid NVS key (callsigns are <= 8 chars)
static void route_key(const char *callsign, char *out, size_t on) {
    size_t j = 0;
    for (const char *p = callsign; *p && j < on - 1; ++p)
        if (*p != ' ') out[j++] = *p;
    out[j] = 0;
}

#define ROUTE_FMT_VER 4   // bump to invalidate cached routes when the label format changes

void route_cache_begin() {
    Preferences p;
    if (!p.begin("routes", false)) return;
    if (p.getUChar("__v", 0) != ROUTE_FMT_VER) { p.clear(); p.putUChar("__v", ROUTE_FMT_VER); }
    p.end();
}

bool route_cache_get(const char *callsign, char *from, size_t fn, char *to, size_t tn,
                     double *destLat, double *destLon, char *airline, size_t an) {
    if (fn) from[0] = 0;
    if (tn) to[0] = 0;
    if (an) airline[0] = 0;
    if (destLat) *destLat = NAN;
    if (destLon) *destLon = NAN;
    if (!callsign || !callsign[0]) return false;
    char key[12];
    route_key(callsign, key, sizeof(key));
    if (!key[0]) return false;
    Preferences p;
    if (!p.begin("routes", true)) return false;
    String v = p.getString(key, "");     // "epoch|from|to|destLat|destLon|airline"
    p.end();
    if (v.length() == 0) return false;
    int cut[5];                          // the 5 separators around the 6 fields
    int pos = -1;
    for (int i = 0; i < 5; ++i) {
        pos = v.indexOf('|', pos + 1);
        if (pos < 0) return false;
        cut[i] = pos;
    }
    const uint32_t ts = (uint32_t)v.substring(0, cut[0]).toInt();
    const uint32_t now = (uint32_t)time(nullptr);    // expire stale routes (reused callsigns)
    if (now > 1700000000UL && ts > 1700000000UL && (now - ts) > 7200UL) return false;  // 2 h TTL
    // Was 24h: many carriers (esp. low-cost/charter) reuse the same callsign for
    // different city pairs across a day, so a day-old cached route can point at a
    // completely different flight than the one currently being tracked (GitHub #7).
    snprintf(from, fn, "%s", v.substring(cut[0] + 1, cut[1]).c_str());
    snprintf(to, tn, "%s", v.substring(cut[1] + 1, cut[2]).c_str());
    const String slat = v.substring(cut[2] + 1, cut[3]);
    const String slon = v.substring(cut[3] + 1, cut[4]);
    if (slat.length() && slon.length()) {
        if (destLat) *destLat = atof(slat.c_str());
        if (destLon) *destLon = atof(slon.c_str());
    }
    if (an) snprintf(airline, an, "%s", v.substring(cut[4] + 1).c_str());
    return true;
}

void route_cache_put(const char *callsign, const char *from, const char *to,
                     double destLat, double destLon, const char *airline) {
    if (!callsign || !callsign[0]) return;
    char key[12];
    route_key(callsign, key, sizeof(key));
    if (!key[0]) return;
    Preferences p;
    if (!p.begin("routes", false)) return;
    int n = p.getInt("__n", 0);
    if (n >= ROUTE_CACHE_MAX) { p.clear(); n = 0; }   // wrap to bound NVS usage
    char coords[32] = "|";               // "|lat|lon", empty fields when unknown
    if (!isnan(destLat) && !isnan(destLon))
        snprintf(coords, sizeof(coords), "%.4f|%.4f", destLat, destLon);
    String v = String((uint32_t)time(nullptr)) + "|" + String(from ? from : "") + "|" +
               String(to ? to : "") + "|" + coords + "|" + String(airline ? airline : "");
    if (p.putString(key, v) > 0) p.putInt("__n", n + 1);
    p.end();
}

// Most recognizable short airport label: a cleaned-up name ("Teesside", "Palma de
// Mallorca", "London Heathrow"), falling back to the municipality, then the IATA code.
static void pick_airport(JsonObjectConst ap, char *out, size_t n) {
    String s = (const char *)(ap["name"] | "");
    s.replace(" International Airport", "");
    s.replace(" Regional Airport", "");
    s.replace(" Airport", "");
    s.replace(" International", "");
    s.trim();
    if (s.length() == 0 || s.length() > 18) {           // name missing or too long -> municipality/IATA
        const char *muni = ap["municipality"] | "";
        const char *iata = ap["iata_code"] | "";
        snprintf(out, n, "%s", muni[0] ? muni : iata);
        return;
    }
    snprintf(out, n, "%s", s.c_str());
}

// Operator name for the card. adsbdb carries the legal company name, so trim the
// corporate tail ("American Airlines Inc." -> "American Airlines") to fit one line.
static void pick_airline(JsonObjectConst al, char *out, size_t n) {
    if (n) out[0] = 0;
    if (al.isNull()) return;
    String s = (const char *)(al["name"] | "");
    static const char *TAIL[] = { " Inc.", " Inc", " Ltd.", " Ltd", " LLC", " L.L.C.",
                                  " Co.", " Corp.", " Corporation", " Company",
                                  " AG", " S.A.", " SA", " N.V.", " NV", " PLC", " Plc",
                                  " Pty", " GmbH", " d.o.o.", " A/S", " AB", " AS" };
    for (const char *t : TAIL) {
        const int len = (int)strlen(t);
        if (s.length() > (unsigned)len && s.endsWith(t)) {
            s.remove(s.length() - len);
            s.trim();
        }
    }
    s.trim();
    snprintf(out, n, "%s", s.c_str());
}

bool route_fetch(const char *callsign, char *from, size_t fn, char *to, size_t tn,
                 double *destLat, double *destLon, char *airline, size_t an) {
    if (fn) from[0] = 0;
    if (tn) to[0] = 0;
    if (an) airline[0] = 0;
    if (destLat) *destLat = NAN;
    if (destLon) *destLon = NAN;
    if (!callsign || !callsign[0] || WiFi.status() != WL_CONNECTED) return false;

    // strip spaces from the callsign
    char cs[12];
    size_t j = 0;
    for (const char *p = callsign; *p && j < sizeof(cs) - 1; ++p)
        if (*p != ' ') cs[j++] = *p;
    cs[j] = 0;
    if (j == 0) return false;

    char url[96];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", cs);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(3000);   // short: runs on the feed task, don't stall the live poll
    http.setTimeout(6000);
    if (!http.begin(client, url)) return false;
    http.addHeader("User-Agent", ADSB_USER_AGENT);

    const int code = http.GET();
    if (code != 200) { http.end(); return false; }

    JsonDocument filter;
    filter["response"]["flightroute"]["airline"]["name"] = true;   // operator, for the card
    filter["response"]["flightroute"]["origin"]["municipality"] = true;
    filter["response"]["flightroute"]["origin"]["iata_code"] = true;
    filter["response"]["flightroute"]["origin"]["name"] = true;
    filter["response"]["flightroute"]["destination"]["municipality"] = true;
    filter["response"]["flightroute"]["destination"]["iata_code"] = true;
    filter["response"]["flightroute"]["destination"]["name"] = true;
    filter["response"]["flightroute"]["destination"]["latitude"] = true;   // for the track
    filter["response"]["flightroute"]["destination"]["longitude"] = true;  // sanity check (#7)

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;

    JsonObjectConst fr = doc["response"]["flightroute"].as<JsonObjectConst>();
    if (fr.isNull()) return false;   // "unknown callsign" etc.

    pick_airport(fr["origin"].as<JsonObjectConst>(), from, fn);
    pick_airport(fr["destination"].as<JsonObjectConst>(), to, tn);
    JsonObjectConst dst = fr["destination"].as<JsonObjectConst>();
    if (!dst.isNull() && dst["latitude"].is<double>() && dst["longitude"].is<double>()) {
        if (destLat) *destLat = dst["latitude"].as<double>();
        if (destLon) *destLon = dst["longitude"].as<double>();
    }
    if (an) pick_airline(fr["airline"].as<JsonObjectConst>(), airline, an);
    return (from[0] || to[0] || (an && airline[0]));
}
