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
  // Compensate global lighting changes with the median pixel difference. Unlike the mean,
  // the median is not pulled by an object as long as it covers less than half the view.
  static uint32_t hist[511];
  for (uint32_t& h : hist) h = 0;
  for (size_t i = 0; i < n; i++) {
    hist[static_cast<int>(frame[i]) - static_cast<int>(baseline[i]) + 255]++;
  }
  int offset = 0;
  size_t acc = 0;
  for (int d = 0; d < 511; d++) {
    acc += hist[d];
    if (acc > n / 2) {
      offset = d - 255;
      break;
    }
  }
  size_t changed = 0;
  for (size_t i = 0; i < n; i++) {
    int d = static_cast<int>(frame[i]) - static_cast<int>(baseline[i]) - offset;
    if (d < 0) d = -d;
    if (d > pixel_threshold) changed++;
  }
  return static_cast<float>(changed) / static_cast<float>(n);
}

bool person_present(float score, bool was_present, float on_threshold, float off_threshold) {
  return was_present ? score >= off_threshold : score >= on_threshold;
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
      // An absence starts when the person leaves; moving between Away and Empty
      // (belongings taken or left) does not restart it.
      if (observed == State::Occupied) {
        absent_ = false;
      } else if (state_ == State::Occupied) {
        absent_ = true;
        absent_since_ms_ = now_ms;
        alerted_ = false;
      }
      state_ = observed;
      since_ms_ = now_ms;
      candidate_count_ = 0;
      return ev;
    }
  }

  if (absent_ && !alerted_ && now_ms - absent_since_ms_ >= cfg_.away_alert_ms) {
    alerted_ = true;
    ev.kind = Event::Kind::AwayTooLong;
    ev.from = state_;
    ev.to = state_;  // Away: belongings still on the seat, Empty: left without them
    ev.duration_ms = now_ms - absent_since_ms_;
  }
  return ev;
}

}  // namespace seat
