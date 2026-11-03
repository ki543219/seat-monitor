#pragma once
// Wi-Fi / Supabase credentials live in include/secrets.h (git-ignored).
// Copy include/secrets.example.h to include/secrets.h and fill it in.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// Detection tuning
// Person score hysteresis. Seated scores wobbled 0.42-0.67 and an empty seat scored
// 0.05-0.14, so a single 0.6 threshold flapped between occupied and away.
#define PERSON_ON 0.5f          // not present -> present at or above this
#define PERSON_OFF 0.3f         // present -> not present below this
#define PIXEL_THRESHOLD 25      // per-pixel change vs. baseline (0..255)
#define DIFF_THRESHOLD 0.08f    // changed-pixel fraction above this = something on the seat
#define CONFIRM_FRAMES 4        // frames in a row before a state change is accepted (~2 s)
#define LEAVE_FRAMES 20         // frames without a person before leaving occupied (~10 s)
#define FRAME_INTERVAL_MS 500
#define AWAY_ALERT_MS (30UL * 60 * 1000)  // absence before alert: 30 min; e.g. 60 s for a demo
#define HEARTBEAT_MS (60UL * 1000)        // liveness row for the status page
