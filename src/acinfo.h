#pragma once
// Shared aircraft-identity state (registration / model / owner by ICAO hex).
// Same request-pending-store-get pattern as route.h: the UI thread asks, the
// network task fulfils, the UI reads.
//
// This is the fallback for aircraft with no route. Departure and arrival airports
// are not broadcast in ADS-B — they come from schedule databases keyed on airline
// callsigns, so GA traffic has no route in any provider. What it DOES have is an
// airframe record, which is far more useful than "Route unavailable".
#include <stddef.h>

void acinfo_request(const char *hex);                 // UI: want identity for this hex
bool acinfo_pending(char *hexOut, size_t n);          // task: is a lookup needed?
void acinfo_store(const char *hex, const char *reg, const char *model, const char *owner);
bool acinfo_get(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                char *owner, size_t on);
