#include "seat.h"

namespace seat {

const char* to_string(State s) {
  switch (s) {
    case State::Empty: return "empty";
    case State::Occupied: return "occupied";
    case State::Away: return "away";
    default: return "unknown";
  }
}

float diff_ratio(const uint8_t* frame, const uint8_t* baseline, size_t n,
                 uint8_t pixel_threshold) {
  if (n == 0) return 0.0f;
  int64_t sum_f = 0, sum_b = 0;
  for (size_t i = 0; i < n; i++) {
    sum_f += frame[i];
    sum_b += baseline[i];
  }
  // Compensate global lighting changes so only local changes count.
  const int offset = static_cast<int>((sum_f - sum_b) / static_cast<int64_t>(n));
  size_t changed = 0;
  for (size_t i = 0; i < n; i++) {
    int d = static_cast<int>(frame[i]) - static_cast<int>(baseline[i]) - offset;
    if (d < 0) d = -d;
    if (d > pixel_threshold) changed++;
  }
  return static_cast<float>(changed) / static_cast<float>(n);
}

State classify(bool person, float diff_ratio, float diff_threshold) {
  if (person) return State::Occupied;
  return diff_ratio > diff_threshold ? State::Away : State::Empty;
}

Event Tracker::update(State observed, uint32_t now_ms) {
  Event ev;
  if (observed == state_) {
    candidate_count_ = 0;
  } else {
    if (observed == candidate_ && candidate_count_ > 0) {
      candidate_count_++;
    } else {
      candidate_ = observed;
      candidate_count_ = 1;
    }
    if (candidate_count_ >= cfg_.confirm_frames) {
      ev.kind = Event::Kind::Changed;
      ev.from = state_;
      ev.to = observed;
      ev.duration_ms = state_ == State::Unknown ? 0 : now_ms - since_ms_;
      state_ = observed;
      since_ms_ = now_ms;
      candidate_count_ = 0;
      alerted_ = false;
      return ev;
    }
  }

  if (state_ == State::Away && !alerted_ && now_ms - since_ms_ >= cfg_.away_alert_ms) {
    alerted_ = true;
    ev.kind = Event::Kind::AwayTooLong;
    ev.from = State::Away;
    ev.to = State::Away;
    ev.duration_ms = now_ms - since_ms_;
  }
  return ev;
}

}  // namespace seat
