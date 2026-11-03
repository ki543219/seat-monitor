#include "ota.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "ca_bundle.h"
#include "config.h"

namespace ota {

void check() {
  if (FW_VERSION[0] != 'v') return;  // only release builds update themselves
  if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) return;

  WiFiClientSecure client;
  client.setCACert(kCaBundle);

  // /releases/latest redirects to /releases/tag/<tag>; read the tag without following it.
  HTTPClient http;
  http.begin(client, OTA_REPO "/releases/latest");
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* keys[] = {"Location"};
  http.collectHeaders(keys, 1);
  int code = http.GET();
  String location = http.header("Location");
  http.end();
  int at = location.indexOf("/releases/tag/");
  if (code != 302 || at < 0) {
    Serial.printf("# ota: no release found (HTTP %d)\n", code);
    return;
  }
  String tag = location.substring(at + strlen("/releases/tag/"));
  if (tag == FW_VERSION) {
    Serial.printf("# ota: up to date (%s)\n", FW_VERSION);
    return;
  }

  Serial.printf("# ota: updating %s -> %s\n", FW_VERSION, tag.c_str());
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // asset URL redirects to the CDN
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return r = httpUpdate.update(client, OTA_REPO "/releases/download/" + tag + "/firmware.bin");
  if (r == HTTP_UPDATE_FAILED) Serial.printf("# ota: failed: %s\n", httpUpdate.getLastErrorString().c_str());
}

}  // namespace ota
