#pragma once
// Seat occupancy logic. Hardware independent so it can be unit tested on the host.
#include <stddef.h>
#include <stdint.h>

namespace seat {

enum class State : uint8_t {
  Unknown,   // not decided yet (boot)
  Empty,     // no person, scene matches the empty-seat baseline
  Occupied,  // person present
  Away,      // no person, but something (bags, books) left on the seat
};

const char* to_string(State s);

// Fraction (0..1) of pixels that differ from the baseline by more than
// pixel_threshold, after removing the overall (median) brightness difference.
float diff_ratio(const uint8_t* frame, const uint8_t* baseline, size_t n,
                 uint8_t pixel_threshold);

// Person decision with hysteresis: a present person stays present until the score drops
// below off_threshold, so a score wobbling around one threshold does not flap.
bool person_present(float score, bool was_present, float on_threshold, float off_threshold);

// Single-frame observation from the model output and the scene difference.
State classify(bool person, float diff_ratio, float diff_threshold);

struct Config {
  uint8_t confirm_frames;  // identical observations in a row needed to change state
  uint32_t away_alert_ms;  // absence (person left, with or without belongings) longer than
                           // this raises one AwayTooLong event
  uint8_t leave_frames;    // frames needed to leave Occupied (0 or omitted = confirm_frames).
                           // Longer, so a person briefly missed by the model is not counted as gone.
};

struct Event {
  enum class Kind : uint8_t { None, Changed, AwayTooLong };
  Kind kind = Kind::None;
  State from = State::Unknown;
  State to = State::Unknown;
  uint32_t duration_ms = 0;  // time spent in `from` (Changed) or absent so far (AwayTooLong)
};

// Debounces per-frame observations into stable states and emits events.
class Tracker {
 public:
  explicit Tracker(Config cfg) : cfg_(cfg) {}

  Event update(State observed, uint32_t now_ms);
  State state() const { return state_; }

 private:
  Config cfg_;
  State state_ = State::Unknown;
  uint32_t since_ms_ = 0;
  State candidate_ = State::Unknown;
  uint8_t candidate_count_ = 0;
  bool absent_ = false;  // person left the seat and has not come back
  uint32_t absent_since_ms_ = 0;
  bool alerted_ = false;
};

}  // namespace seat
