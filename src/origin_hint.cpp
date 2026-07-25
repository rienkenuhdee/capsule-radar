#include "origin_hint.h"
#include "airports.h"
#include <map>
#include <string>
#include <mutex>
#include <string.h>
#include <math.h>
#if defined(ARDUINO)
  #include <Arduino.h>   // Serial log when a hint is recorded
#endif

// Tuning. A hint is only ever a fallback (shown when adsbdb has nothing), so it
// favors "usually right" over "provably right".
#define HINT_MAX_KM      5.0f      // aircraft must be this close to the airport
#define HINT_MAX_ALT_FT  6000.0f   // climb-out gate: below this...
#define HINT_MIN_FPM     300.0f    // ...and climbing at least this
#define HINT_EXPIRE_MS   (6UL * 3600UL * 1000UL)   // forget hints after 6 h (hex reuse next day)
#define HINT_MAX_ENTRIES 96        // bound the map (busy airspace)

struct Entry {
    char     iata[4] = {0};      // inferred departure airport ("" = none yet)
    bool     seenAirborne = false; // ever seen flying -> on ground later means it LANDED
    bool     seenCruise = false;   // ever seen in cruise -> low+climbing later is a go-around,
                                   // not a climb-out (don't tag the missed airport as origin)
    uint32_t ms = 0;             // last touch, for expiry/eviction
};

static std::mutex s_m;
static std::map<std::string, Entry> s_map;

void origin_hint_update(const std::vector<Aircraft>& acs, uint32_t nowMs) {
    std::lock_guard<std::mutex> g(s_m);

    for (const Aircraft &ac : acs) {
        if (ac.hex.length() == 0) continue;
        Entry &e = s_map[std::string(ac.hex.c_str())];
        e.ms = nowMs;

        // Track what we've seen this hex do, so later observations can be read
        // correctly: an aircraft we watched fly and now see on the ground LANDED
        // (not departing), and one we saw in cruise that is now low and climbing
        // is going around (not climbing out). GA cruises low, so "cruise" also
        // means level-ish flight above pattern altitude, not just high altitude.
        if (!ac.onGround) {
            e.seenAirborne = true;
            if (!isnan(ac.altBaro) &&
                (ac.altBaro > 8000.0f ||
                 (ac.altBaro > 3000.0f && !isnan(ac.baroRate) && fabsf(ac.baroRate) < HINT_MIN_FPM)))
                e.seenCruise = true;
        }

        if (e.iata[0]) continue;   // already hinted; first observation wins

        // Departure evidence: first seen sitting on the ground, or climbing out
        // low. Both must be close to an airport to count.
        const bool onGroundFresh = ac.onGround && !e.seenAirborne;
        const bool climbingOut   = !ac.onGround && !e.seenCruise &&
                                   !isnan(ac.altBaro) && ac.altBaro < HINT_MAX_ALT_FT &&
                                   !isnan(ac.baroRate) && ac.baroRate > HINT_MIN_FPM;
        if (!onGroundFresh && !climbingOut) continue;

        char iata[4];
        float dKm;
        if (airports_nearest_iata(ac.lat, ac.lon, HINT_MAX_KM, iata, &dKm, nullptr) && iata[0]) {
            memcpy(e.iata, iata, 4);
#if defined(ARDUINO)
            Serial.printf("[origin] %s from %s (%.1f km, %s%.0f ft)\n",
                          ac.hex.c_str(), iata, (double)dKm,
                          ac.onGround ? "ground" : "", ac.onGround ? 0.0 : (double)ac.altBaro);
#endif
        }
    }

    // Expiry + hard bound. Oldest-first eviction only kicks in when over the cap.
    for (auto it = s_map.begin(); it != s_map.end();) {
        if (nowMs - it->second.ms > HINT_EXPIRE_MS) it = s_map.erase(it);
        else ++it;
    }
    while (s_map.size() > HINT_MAX_ENTRIES) {
        auto oldest = s_map.begin();
        for (auto it = s_map.begin(); it != s_map.end(); ++it)
            if ((int32_t)(it->second.ms - oldest->second.ms) < 0) oldest = it;
        s_map.erase(oldest);
    }
}

bool origin_hint_get(const char *hex, char iata[4]) {
    if (iata) iata[0] = 0;
    if (!hex || !hex[0]) return false;
    std::lock_guard<std::mutex> g(s_m);
    auto it = s_map.find(std::string(hex));
    if (it == s_map.end() || !it->second.iata[0]) return false;
    memcpy(iata, it->second.iata, 4);
    return true;
}
