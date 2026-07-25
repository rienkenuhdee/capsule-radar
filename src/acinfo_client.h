#pragma once
// Aircraft identity by ICAO hex via adsbdb.com (free, no key):
// GET /v0/aircraft/{hex} -> registration, model, registered owner.
// Device-only (uses WiFi/HTTPS).
#include <stddef.h>

bool acinfo_fetch(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                  char *owner, size_t on);

// NVS cache. Unlike routes, a hex->airframe mapping is effectively permanent, so
// entries never expire — they only get wrapped when the cache fills.
void acinfo_cache_begin();
bool acinfo_cache_get(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                      char *owner, size_t on);
void acinfo_cache_put(const char *hex, const char *reg, const char *model, const char *owner);
