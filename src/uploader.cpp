#include "uploader.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "ca_bundle.h"
#include "config.h"
#include "settings.h"

namespace uploader {
namespace {

constexpr size_t kCapacity = 64;
constexpr uint32_t kRetryMs = 5000;

Record queue[kCapacity];
size_t head = 0, count = 0;
uint32_t last_fail_ms = 0;
bool failed_before = false;

const char* kind_name(seat::Event::Kind k) {
  return k == seat::Event::Kind::AwayTooLong ? "away_too_long" : "changed";
}

String to_json(const Record& r) {
  char ts[32] = "";
  if (r.ts > 0) {
    struct tm t;
    gmtime_r(&r.ts, &t);
    strftime(ts, sizeof(ts), "\"%Y-%m-%dT%H:%M:%SZ\"", &t);
  }
  char buf[384];
  snprintf(buf, sizeof(buf),
           "{\"device_id\":\"%s\",\"seat_id\":\"%s\",%s%s%s"
           "\"kind\":\"%s\",\"from_state\":\"%s\",\"to_state\":\"%s\","
           "\"duration_s\":%lu,\"person_score\":%.3f,\"diff_ratio\":%.3f,"
           "\"fw_version\":\"%s\"}",
           settings().device_id.c_str(), settings().seat_id.c_str(), r.ts > 0 ? "\"ts\":" : "", ts, r.ts > 0 ? "," : "",
           kind_name(r.event.kind), seat::to_string(r.event.from), seat::to_string(r.event.to),
           (unsigned long)(r.event.duration_ms / 1000), r.person_score, r.diff_ratio, FW_VERSION);
  return String(buf);
}

bool post(const char* table, const String& body) {
  // Certificate validity needs the real date; until NTP syncs, keep events queued.
  if (time(nullptr) < 1700000000 || !settings().valid()) return false;
  const Settings& s = settings();
  WiFiClientSecure client;
  client.setCACert(kCaBundle);  // verify the server against bundled roots (tools/make_ca_bundle.ps1)
  HTTPClient http;
  if (!http.begin(client, s.supabase_url + "/rest/v1/" + table)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", s.supabase_key);
  http.addHeader("Authorization", "Bearer " + s.supabase_key);
  http.addHeader("Prefer", "return=minimal");
  http.addHeader("x-device-token", s.device_token);  // checked by the insert policies
  http.setTimeout(8000);
  int code = http.POST(body);
  if (code != 201) Serial.printf("upload to %s failed: HTTP %d %s\n", table, code, http.getString().c_str());
  http.end();
  return code == 201;
}

}  // namespace

bool heartbeat(seat::State state, float person_score, float diff_ratio) {
  if (WiFi.status() != WL_CONNECTED) return false;
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"device_id\":\"%s\",\"seat_id\":\"%s\",\"state\":\"%s\",\"person_score\":%.3f,"
           "\"diff_ratio\":%.3f,\"rssi\":%d,\"uptime_s\":%lu,\"queue\":%u,\"fw_version\":\"%s\"}",
           settings().device_id.c_str(), settings().seat_id.c_str(), seat::to_string(state), person_score, diff_ratio, (int)WiFi.RSSI(),
           (unsigned long)(millis() / 1000), (unsigned)count, FW_VERSION);
  return post("heartbeats", String(buf));
}

void push(const Record& r) {
  if (count == kCapacity) {  // drop the oldest
    head = (head + 1) % kCapacity;
    count--;
  }
  queue[(head + count) % kCapacity] = r;
  count++;
}

void tick() {
  if (count == 0 || WiFi.status() != WL_CONNECTED) return;
  if (failed_before && millis() - last_fail_ms < kRetryMs) return;
  if (post("seat_events", to_json(queue[head]))) {
    head = (head + 1) % kCapacity;
    count--;
    failed_before = false;
  } else {
    failed_before = true;
    last_fail_ms = millis();
  }
}

size_t pending() { return count; }

}  // namespace uploader
