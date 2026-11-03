#pragma once
#include <stdint.h>

// Runs the person detection model on a 96x96 grayscale frame.
namespace detector {

constexpr int kSize = 96;

bool init();
// Returns the person probability (0..1), or a negative value on failure.
float person_score(const uint8_t* gray96);

}  // namespace detector
