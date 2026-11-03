#pragma once
#include <Arduino.h>

// Wi-Fi, Supabase and device credentials.
// Local builds have them in include/secrets.h and save them to flash (NVS) at boot.
// Release builds (made in CI without secrets, installed over the air) read them back from NVS,
// so the public firmware.bin never contains a password or token.
struct Settings {
  String wifi_ssid, wifi_password, supabase_url, supabase_key, device_id, seat_id, device_token;
  bool valid() const { return wifi_ssid.length() && supabase_url.length() && device_token.length(); }
};

const Settings& settings();
void settings_load();
