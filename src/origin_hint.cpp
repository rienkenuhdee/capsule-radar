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
#define HINT_GROUND_KM    3.0f     // on the ground: must be AT the field
#define HINT_CLIMB_KM     12.0f    // climbing out: it has already moved off the field
#define HINT_MAX_ALT_FT   6000.0f  // climb-out gate: below this...
#define HINT_MIN_FPM      300.0f   // ...and climbing at least this
#define HINT_MIN_CLIMB_FT 200.0f   // ...or gained this much since we last saw it
                                   // (small aircraft often omit baro_rate entirely)
#define HINT_EXPIRE_MS   (6UL * 3600UL * 1000UL)   // forget hints after 6 h (hex reuse next day)
#define HINT_MAX_ENTRIES 96        // bound the map (busy airspace)

struct Entry {
    char     id[5] = {0};          // inferred departure airport ("" = none yet)
    bool     seenAirborne = false; // ever seen flying -> on ground later means it LANDED
    bool     seenCruise = false;   // ever seen in cruise -> low+climbing later is a go-around,
                                   // not a climb-out (don't tag the missed airport as origin)
    float    lastAlt = NAN;        // previous altitude, to derive a climb without baro_rate
    uint32_t ms = 0;               // last touch, for expiry/eviction
};

static std::mutex s_m;
static std::map<std::string, Entry> s_map;

void origin_hint_update(const std::vector<Aircraft>& acs, uint32_t nowMs) {
    std::lock_guard<std::mutex> g(s_m);

    for (const Aircraft &ac : acs) {
        if (ac.hex.length() == 0) continue;
        Entry &e = s_map[std::string(ac.hex.c_str())];
        e.ms = nowMs;
        const float prevAlt = e.lastAlt;
        if (!ac.onGround && !isnan(ac.altBaro)) e.lastAlt = ac.altBaro;
        else if (ac.onGround)                   e.lastAlt = 0.0f;

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

        if (e.id[0]) continue;   // already hinted; first observation wins

        // Departure evidence: first seen sitting on the ground, or climbing out low.
        // The climb can come from baro_rate or, when the aircraft doesn't transmit it
        // (common on GA transponders), from the altitude gained since the last poll.
        const bool onGroundFresh = ac.onGround && !e.seenAirborne;
        bool climbingOut = false;
        if (!ac.onGround && !e.seenCruise && !isnan(ac.altBaro) && ac.altBaro < HINT_MAX_ALT_FT) {
            const bool rateClimb  = !isnan(ac.baroRate) && ac.baroRate > HINT_MIN_FPM;
            const bool deltaClimb = !isnan(prevAlt) && (ac.altBaro - prevAlt) > HINT_MIN_CLIMB_FT;
            climbingOut = rateClimb || deltaClimb;
        }
        if (!onGroundFresh && !climbingOut) continue;

        char id[5];
        float dKm;
        const float maxKm = onGroundFresh ? HINT_GROUND_KM : HINT_CLIMB_KM;
        // minClass 0: small GA fields count here — they are exactly the airports
        // the aircraft with no adsbdb route departed from.
        if (airports_nearest(ac.lat, ac.lon, maxKm, 0, id, &dKm, nullptr) && id[0]) {
            memcpy(e.id, id, 5);
#if defined(ARDUINO)
            Serial.printf("[origin] %s from %s (%.1f km, %s)\n",
                          ac.hex.c_str(), id, (double)dKm,
                          onGroundFresh ? "ground" : "climb-out");
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

bool origin_hint_get(const char *hex, char id[5]) {
    if (id) id[0] = 0;
    if (!hex || !hex[0]) return false;
    std::lock_guard<std::mutex> g(s_m);
    auto it = s_map.find(std::string(hex));
    if (it == s_map.end() || !it->second.id[0]) return false;
    memcpy(id, it->second.id, 5);
    return true;
}
