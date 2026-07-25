// Shared aircraft-identity state. std::mutex works on both ESP32 (Arduino/FreeRTOS)
// and the native simulator, mirroring route.cpp.
#include "acinfo.h"
#include <string.h>
#include <stdio.h>
#include <mutex>

static std::mutex s_m;
static char s_want[10]    = "";   // hex the UI asked about
static char s_doneHex[10] = "";   // hex the stored result belongs to
static char s_reg[16]     = "";
static char s_model[32]   = "";
static char s_owner[40]   = "";

void acinfo_request(const char *hex) {
    std::lock_guard<std::mutex> g(s_m);
    snprintf(s_want, sizeof(s_want), "%s", hex ? hex : "");
}

bool acinfo_pending(char *hexOut, size_t n) {
    std::lock_guard<std::mutex> g(s_m);
    if (s_want[0] && strcmp(s_want, s_doneHex) != 0) {
        snprintf(hexOut, n, "%s", s_want);
        return true;
    }
    return false;
}

void acinfo_store(const char *hex, const char *reg, const char *model, const char *owner) {
    std::lock_guard<std::mutex> g(s_m);
    snprintf(s_doneHex, sizeof(s_doneHex), "%s", hex ? hex : "");
    snprintf(s_reg,   sizeof(s_reg),   "%s", reg   ? reg   : "");
    snprintf(s_model, sizeof(s_model), "%s", model ? model : "");
    snprintf(s_owner, sizeof(s_owner), "%s", owner ? owner : "");
}

bool acinfo_get(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                char *owner, size_t on) {
    std::lock_guard<std::mutex> g(s_m);
    if (hex && s_doneHex[0] && strcmp(hex, s_doneHex) == 0) {
        if (rn) snprintf(reg,   rn, "%s", s_reg);
        if (mn) snprintf(model, mn, "%s", s_model);
        if (on) snprintf(owner, on, "%s", s_owner);
        return true;
    }
    return false;
}
