#include <Arduino.h>
#include <WiFi.h>
#include <seat.h>

#include "camera.h"
#include "config.h"
#include "detector.h"
#include "uploader.h"

namespace {

constexpr size_t kPixels = detector::kSize * detector::kSize;
constexpr int kBootButton = 0;  // hold BOOT for 1 s to re-capture the empty-seat baseline

uint8_t* baseline = nullptr;
seat::Tracker tracker({CONFIRM_FRAMES, AWAY_ALERT_MS, LEAVE_FRAMES});
uint32_t last_frame_ms = 0;
uint32_t last_heartbeat_ms = 0;
bool person_seen = false;
uint32_t button_down_ms = 0;

bool capture_baseline() {
  // Discard a few frames so auto exposure settles.
  for (int i = 0; i < 5; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(100);
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb || fb->len != kPixels) {
    if (fb) esp_camera_fb_return(fb);
    return false;
  }
  memcpy(baseline, fb->buf, kPixels);
  esp_camera_fb_return(fb);
  Serial.println("baseline captured (seat should be empty)");
  return true;
}

void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Wi-Fi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print('.');
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " connected" : " not yet (will retry)");
  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

time_t now_unix() {
  time_t t = time(nullptr);
  return t > 1700000000 ? t : 0;  // 0 until NTP has synced
}

// Dataset collection (tools/capture.py): host sends 'F', device replies "FRM0" + raw 96x96 pixels.
void serve_frame_request() {
  bool requested = false;
  while (Serial.available()) {
    if (Serial.read() == 'F') requested = true;
  }
  if (!requested) return;
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;
  if (fb->len == kPixels) {
    Serial.write(reinterpret_cast<const uint8_t*>("FRM0"), 4);
    Serial.write(fb->buf, kPixels);
  }
  esp_camera_fb_return(fb);
}

void check_button() {
  if (digitalRead(kBootButton) == LOW) {
    if (button_down_ms == 0) button_down_ms = millis();
    if (millis() - button_down_ms > 1000) {
      capture_baseline();
      button_down_ms = 0;
      while (digitalRead(kBootButton) == LOW) delay(10);
    }
  } else {
    button_down_ms = 0;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("seat monitor %s, device %s, seat %s\n", FW_VERSION, DEVICE_ID, SEAT_ID);
  pinMode(kBootButton, INPUT_PULLUP);

  baseline = static_cast<uint8_t*>(ps_malloc(kPixels));
  if (!baseline || !camera_init() || !detector::init()) {
    Serial.println("init failed, restarting");
    delay(3000);
    ESP.restart();
  }
  while (!capture_baseline()) delay(500);
  connect_wifi();
}

void loop() {
  serve_frame_request();
  check_button();
  uploader::tick();

  if (millis() - last_frame_ms < FRAME_INTERVAL_MS) return;
  last_frame_ms = millis();

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;
  if (fb->len != kPixels) {
    esp_camera_fb_return(fb);
    return;
  }
  uint32_t t0 = millis();
  float score = detector::person_score(fb->buf);
  uint32_t infer_ms = millis() - t0;
  float diff = seat::diff_ratio(fb->buf, baseline, kPixels, PIXEL_THRESHOLD);
  esp_camera_fb_return(fb);
  if (score < 0) return;

  person_seen = seat::person_present(score, person_seen, PERSON_ON, PERSON_OFF);
  seat::State observed = seat::classify(person_seen, diff, DIFF_THRESHOLD);
  seat::Event ev = tracker.update(observed, millis());

  // label:value pairs so the Arduino IDE Serial Plotter can graph them (all on a 0..1 scale).
  // seat: 1 = occupied, 0.5 = away, 0 = empty.
  const seat::State st = tracker.state();
  Serial.printf("person:%.2f diff:%.2f person_on:%.2f person_off:%.2f diff_threshold:%.2f seat:%.1f\n",
                score, diff, PERSON_ON, PERSON_OFF, DIFF_THRESHOLD,
                st == seat::State::Occupied ? 1.0f : st == seat::State::Away ? 0.5f : 0.0f);

  if (ev.kind != seat::Event::Kind::None) {
    Serial.printf("# event %s -> %s (%lus)\n", seat::to_string(ev.from), seat::to_string(ev.to),
                  (unsigned long)(ev.duration_ms / 1000));
    uploader::push({ev, now_unix(), score, diff});
  }

  if (millis() - last_heartbeat_ms >= HEARTBEAT_MS || last_heartbeat_ms == 0) {
    last_heartbeat_ms = millis();
    bool ok = uploader::heartbeat(st, score, diff);
    Serial.printf("# heartbeat %s, state %s, infer %lums, queue %u\n", ok ? "sent" : "failed",
                  seat::to_string(st), (unsigned long)infer_ms, (unsigned)uploader::pending());
  }
}
