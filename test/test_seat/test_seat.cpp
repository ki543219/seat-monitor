#include <string.h>
#include <unity.h>

#include "seat.h"

using seat::Event;
using seat::State;

void setUp() {}
void tearDown() {}

static const size_t N = 96 * 96;
static uint8_t base[N], frame[N];

void test_classify() {
  TEST_ASSERT_EQUAL(State::Occupied, seat::classify(true, 0.0f, 0.1f));
  TEST_ASSERT_EQUAL(State::Occupied, seat::classify(true, 0.9f, 0.1f));
  TEST_ASSERT_EQUAL(State::Away, seat::classify(false, 0.3f, 0.1f));
  TEST_ASSERT_EQUAL(State::Empty, seat::classify(false, 0.05f, 0.1f));
}

void test_person_hysteresis() {
  // Scores logged while someone sat at the desk wobbled between 0.42 and 0.67.
  const float seated[] = {0.67f, 0.63f, 0.57f, 0.52f, 0.43f, 0.61f, 0.42f};
  bool present = false;
  present = seat::person_present(0.67f, present, 0.5f, 0.3f);
  TEST_ASSERT_TRUE(present);
  for (float s : seated) {
    present = seat::person_present(s, present, 0.5f, 0.3f);
    TEST_ASSERT_TRUE(present);
  }
  present = seat::person_present(0.12f, present, 0.5f, 0.3f);  // empty seat
  TEST_ASSERT_FALSE(present);
  present = seat::person_present(0.42f, present, 0.5f, 0.3f);  // not enough to sit back down
  TEST_ASSERT_FALSE(present);
}

void test_baseline_refresh_only_when_empty() {
  const uint32_t min_empty = 120000, interval = 300000;
  TEST_ASSERT_TRUE(seat::should_refresh_baseline(State::Empty, State::Empty, 130000, 400000, min_empty, interval));
  // never with someone (or their bag) in view
  TEST_ASSERT_FALSE(seat::should_refresh_baseline(State::Occupied, State::Occupied, 900000, 900000, min_empty, interval));
  TEST_ASSERT_FALSE(seat::should_refresh_baseline(State::Away, State::Away, 900000, 900000, min_empty, interval));
  // empty, but this frame looks different (someone walking in): wait
  TEST_ASSERT_FALSE(seat::should_refresh_baseline(State::Empty, State::Away, 900000, 900000, min_empty, interval));
  // just became empty, or refreshed recently
  TEST_ASSERT_FALSE(seat::should_refresh_baseline(State::Empty, State::Empty, 60000, 900000, min_empty, interval));
  TEST_ASSERT_FALSE(seat::should_refresh_baseline(State::Empty, State::Empty, 900000, 100000, min_empty, interval));
}

void test_diff_identical_is_zero() {
  memset(base, 100, N);
  memcpy(frame, base, N);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, seat::diff_ratio(frame, base, N, 25));
}

void test_diff_ignores_global_brightness() {
  memset(base, 100, N);
  memset(frame, 160, N);  // lights turned on: every pixel brighter by the same amount
  TEST_ASSERT_EQUAL_FLOAT(0.0f, seat::diff_ratio(frame, base, N, 25));
}

void test_diff_detects_object() {
  memset(base, 100, N);
  memcpy(frame, base, N);
  memset(frame, 230, N / 4);  // a bright bag covering a quarter of the view
  float r = seat::diff_ratio(frame, base, N, 25);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, r);
}

void test_tracker_ignores_single_glitch() {
  seat::Tracker t({3, 60000});
  for (int i = 0; i < 3; i++) t.update(State::Empty, i * 500);
  TEST_ASSERT_EQUAL(State::Empty, t.state());

  Event ev = t.update(State::Occupied, 2000);  // one noisy frame
  TEST_ASSERT_EQUAL(Event::Kind::None, ev.kind);
  t.update(State::Empty, 2500);
  t.update(State::Occupied, 3000);
  t.update(State::Occupied, 3500);
  TEST_ASSERT_EQUAL(State::Empty, t.state());  // run was broken, count restarted
}

void test_tracker_changes_after_confirm_frames() {
  seat::Tracker t({3, 60000});
  for (int i = 0; i < 3; i++) t.update(State::Empty, 0);
  t.update(State::Occupied, 10000);
  t.update(State::Occupied, 10500);
  Event ev = t.update(State::Occupied, 11000);
  TEST_ASSERT_EQUAL(Event::Kind::Changed, ev.kind);
  TEST_ASSERT_EQUAL(State::Empty, ev.from);
  TEST_ASSERT_EQUAL(State::Occupied, ev.to);
  TEST_ASSERT_EQUAL_UINT32(11000, ev.duration_ms);
}

void test_tracker_leaving_needs_more_frames() {
  seat::Tracker t({2, 60000, 6});
  t.update(State::Occupied, 0);
  t.update(State::Occupied, 500);
  TEST_ASSERT_EQUAL(State::Occupied, t.state());

  // Model misses the person for 5 frames, then sees them again: still occupied.
  for (int i = 1; i <= 5; i++) t.update(State::Away, 500 + i * 500);
  t.update(State::Occupied, 3500);
  TEST_ASSERT_EQUAL(State::Occupied, t.state());

  // Really gone: 6 frames in a row.
  Event ev;
  for (int i = 1; i <= 6; i++) ev = t.update(State::Away, 3500 + i * 500);
  TEST_ASSERT_EQUAL(Event::Kind::Changed, ev.kind);
  TEST_ASSERT_EQUAL(State::Away, t.state());

  // Sitting back down uses the short confirm count.
  t.update(State::Occupied, 7000);
  t.update(State::Occupied, 7500);
  TEST_ASSERT_EQUAL(State::Occupied, t.state());
}

void test_tracker_away_alert_fires_once() {
  seat::Tracker t({1, 30000});
  t.update(State::Occupied, 0);
  t.update(State::Away, 1000);  // left belongings and walked away
  TEST_ASSERT_EQUAL(Event::Kind::None, t.update(State::Away, 20000).kind);

  Event ev = t.update(State::Away, 31000);
  TEST_ASSERT_EQUAL(Event::Kind::AwayTooLong, ev.kind);
  TEST_ASSERT_EQUAL(State::Away, ev.to);
  TEST_ASSERT_EQUAL_UINT32(30000, ev.duration_ms);
  TEST_ASSERT_EQUAL(Event::Kind::None, t.update(State::Away, 40000).kind);

  // Coming back and leaving again re-arms the alert.
  t.update(State::Occupied, 41000);
  t.update(State::Away, 42000);
  TEST_ASSERT_EQUAL(Event::Kind::AwayTooLong, t.update(State::Away, 72000).kind);
}

void test_tracker_alert_without_visible_belongings() {
  // Belongings may be out of the camera's view; absence alone must raise the alert.
  seat::Tracker t({1, 30000});
  t.update(State::Occupied, 0);
  t.update(State::Empty, 5000);
  Event ev = t.update(State::Empty, 35000);
  TEST_ASSERT_EQUAL(Event::Kind::AwayTooLong, ev.kind);
  TEST_ASSERT_EQUAL(State::Empty, ev.to);
  TEST_ASSERT_EQUAL_UINT32(30000, ev.duration_ms);
}

void test_tracker_absence_spans_away_and_empty() {
  seat::Tracker t({1, 30000});
  t.update(State::Occupied, 0);
  t.update(State::Away, 10000);   // left with bag on the seat
  t.update(State::Empty, 25000);  // someone moved the bag: still the same absence
  TEST_ASSERT_EQUAL(Event::Kind::AwayTooLong, t.update(State::Empty, 40000).kind);
}

void test_tracker_no_alert_if_never_occupied() {
  seat::Tracker t({1, 30000});
  t.update(State::Empty, 0);
  TEST_ASSERT_EQUAL(Event::Kind::None, t.update(State::Empty, 3600000).kind);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_classify);
  RUN_TEST(test_person_hysteresis);
  RUN_TEST(test_baseline_refresh_only_when_empty);
  RUN_TEST(test_diff_identical_is_zero);
  RUN_TEST(test_diff_ignores_global_brightness);
  RUN_TEST(test_diff_detects_object);
  RUN_TEST(test_tracker_ignores_single_glitch);
  RUN_TEST(test_tracker_changes_after_confirm_frames);
  RUN_TEST(test_tracker_leaving_needs_more_frames);
  RUN_TEST(test_tracker_away_alert_fires_once);
  RUN_TEST(test_tracker_alert_without_visible_belongings);
  RUN_TEST(test_tracker_absence_spans_away_and_empty);
  RUN_TEST(test_tracker_no_alert_if_never_occupied);
  return UNITY_END();
}
