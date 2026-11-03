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

void test_tracker_away_alert_fires_once() {
  seat::Tracker t({1, 30000});
  t.update(State::Away, 1000);
  TEST_ASSERT_EQUAL(Event::Kind::None, t.update(State::Away, 20000).kind);

  Event ev = t.update(State::Away, 31000);
  TEST_ASSERT_EQUAL(Event::Kind::AwayTooLong, ev.kind);
  TEST_ASSERT_EQUAL_UINT32(30000, ev.duration_ms);
  TEST_ASSERT_EQUAL(Event::Kind::None, t.update(State::Away, 40000).kind);

  // Coming back and leaving again re-arms the alert.
  t.update(State::Occupied, 41000);
  t.update(State::Away, 42000);
  TEST_ASSERT_EQUAL(Event::Kind::AwayTooLong, t.update(State::Away, 72000).kind);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_classify);
  RUN_TEST(test_diff_identical_is_zero);
  RUN_TEST(test_diff_ignores_global_brightness);
  RUN_TEST(test_diff_detects_object);
  RUN_TEST(test_tracker_ignores_single_glitch);
  RUN_TEST(test_tracker_changes_after_confirm_frames);
  RUN_TEST(test_tracker_away_alert_fires_once);
  return UNITY_END();
}
