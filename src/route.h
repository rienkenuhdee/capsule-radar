#pragma once
// Shared route state (origin -> destination by callsign). Portable: the UI thread
// requests a lookup, a network task fulfils it, the UI reads the result.
#include <stddef.h>

void route_request(const char *callsign);                     // UI: want a route for this callsign
bool route_pending(char *callOut, size_t n);                  // task: is a lookup needed? returns callsign

// `suspect`: the claimed destination disagrees with the aircraft's observed track
// (see route_looks_suspect in main.cpp / GitHub #7). The UI shows the route
// de-emphasized instead of presenting it as fact.
void route_store(const char *callsign, const char *from, const char *to,
                 bool suspect = false);
bool route_get(const char *callsign, char *from, size_t fn, char *to, size_t tn,
               bool *suspect = nullptr);
