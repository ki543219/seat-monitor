#include "uploader.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"

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
           DEVICE_ID, SEAT_ID, r.ts > 0 ? "\"ts\":" : "", ts, r.ts > 0 ? "," : "",
           kind_name(r.event.kind), seat::to_string(r.event.from), seat::to_string(r.event.to),
           (unsigned long)(r.event.duration_ms / 1000), r.person_score, r.diff_ratio, FW_VERSION);
  return String(buf);
}

bool post(const Record& r) {
  WiFiClientSecure client;
  client.setInsecure();  // demo only: pin the Supabase root CA for real deployments
  HTTPClient http;
  if (!http.begin(client, String(SUPABASE_URL) + "/rest/v1/seat_events")) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(8000);
  int code = http.POST(to_json(r));
  if (code != 201) Serial.printf("upload failed: HTTP %d %s\n", code, http.getString().c_str());
  http.end();
  return code == 201;
}

}  // namespace

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
  if (post(queue[head])) {
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
