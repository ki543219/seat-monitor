#pragma once
#include <seat.h>
#include <time.h>

// Queues seat events and posts them to the Supabase REST API.
// Events survive short Wi-Fi drops; when the queue is full the oldest is dropped.
namespace uploader {

struct Record {
  seat::Event event;
  time_t ts;  // unix time, 0 if NTP not synced yet (DB default now() is used)
  float person_score;
  float diff_ratio;
};

void push(const Record& r);
// Sends at most one queued record. Call every loop.
void tick();
size_t pending();

}  // namespace uploader
