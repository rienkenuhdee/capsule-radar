#pragma once
// Airport markers for the radar scope. Projects the embedded OurAirports list
// (airports_data.h) like the coastline: cull to the scope, great-circle project,
// cache screen markers, draw in the static chrome layer. Large airports get a small
// ring + ident label; medium airports are a faint dot. Small fields are in the data
// (for departure inference) but are never drawn. Projection is done only on a
// home/range change, never per frame.
#include <lvgl.h>
#include <stdint.h>

void airports_project(double homeLat, double homeLon, double rangeKm,
                      float cx, float cy, float rOuterPx);

void airports_draw(lv_draw_ctx_t *ctx, lv_color_t color, lv_opa_t opa);

// Nearest airport, entirely offline from the embedded data. `id` receives the
// short display ident (IATA, else local/ICAO code), up to 4 chars + NUL.
// minClass gates how obscure a match may be:
//   2 = large only, 1 = large+medium (recognizable, for the weather view),
//   0 = include small GA fields (for departure inference).
bool airports_nearest(double lat, double lon, float maxKm, uint8_t minClass,
                      char id[5], float *distKm, float *bearingDeg);
