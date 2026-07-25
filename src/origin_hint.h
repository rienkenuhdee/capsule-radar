#pragma once
// Locally-inferred departure airports ("origin hints"), keyed by ICAO hex.
//
// Small/GA aircraft usually have no route in adsbdb, so the detail card showed
// nothing. But if we saw the aircraft on the ground, or climbing out low, right
// next to an airport, we KNOW where it departed — no database needed. The UI
// shows "From <IATA>" when a route lookup comes back empty.
//
// Portable (no Arduino deps): the feed task calls update, the UI thread calls
// get; a mutex guards the map, same pattern as route.cpp.
#include <vector>
#include <stdint.h>
#include "aircraft.h"

// Scan a fresh snapshot for hint candidates. Cheap: only unhinted aircraft that
// are on the ground / climbing out low get a (bbox-prefiltered) airport lookup.
void origin_hint_update(const std::vector<Aircraft>& acs, uint32_t nowMs);

// Departure airport for this hex, if one was inferred. `id` is the short ident
// (IATA, else local/ICAO code), up to 4 chars + NUL.
bool origin_hint_get(const char *hex, char id[5]);
