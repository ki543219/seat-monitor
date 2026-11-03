#include "settings.h"

#include <Preferences.h>

#include "config.h"

namespace {

Settings current;

struct Field {
  const char* key;  // NVS keys are at most 15 characters
  String Settings::*member;
  const char* compiled;
};

const Field kFields[] = {
    {"wifi_ssid", &Settings::wifi_ssid, WIFI_SSID},
    {"wifi_pass", &Settings::wifi_password, WIFI_PASSWORD},
    {"sb_url", &Settings::supabase_url, SUPABASE_URL},
    {"sb_key", &Settings::supabase_key, SUPABASE_KEY},
    {"device_id", &Settings::device_id, DEVICE_ID},
    {"seat_id", &Settings::seat_id, SEAT_ID},
    {"dev_token", &Settings::device_token, DEVICE_TOKEN},
};

}  // namespace

const Settings& settings() { return current; }

void settings_load() {
  Preferences prefs;
  prefs.begin("seatmon", false);
  for (const Field& f : kFields) {
#if HAVE_LOCAL_SECRETS
    current.*f.member = f.compiled;
    if (prefs.getString(f.key, "") != f.compiled) prefs.putString(f.key, f.compiled);
#else
    current.*f.member = prefs.getString(f.key, "");
#endif
  }
  prefs.end();
  Serial.printf("# settings %s (%s)\n", current.valid() ? "ok" : "MISSING: flash once over USB with secrets.h",
                HAVE_LOCAL_SECRETS ? "from secrets.h, saved to NVS" : "from NVS");
}
