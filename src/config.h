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
#define PERSON_THRESHOLD 0.6f   // person score above this counts as a person
#define PIXEL_THRESHOLD 25      // per-pixel change vs. baseline (0..255)
#define DIFF_THRESHOLD 0.08f    // changed-pixel fraction above this = something on the seat
#define CONFIRM_FRAMES 4        // frames in a row before a state change is accepted
#define FRAME_INTERVAL_MS 500
#define AWAY_ALERT_MS (30UL * 60 * 1000)  // 30 min; set to e.g. 60 s for a demo
