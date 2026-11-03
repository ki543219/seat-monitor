-- Seat monitor schema. Safe to run again: every statement is idempotent.
-- Devices insert and the status page (docs/index.html) reads with the publishable (anon)
-- key; nobody can update or delete rows with that key.

-- Seat state changes, long-absence alerts and study sessions.
create table if not exists public.seat_events (
  id bigint generated always as identity primary key,
  ts timestamptz not null default now(),
  device_id text not null,
  seat_id text not null,
  kind text not null,
  from_state text not null check (from_state in ('unknown', 'empty', 'occupied', 'away')),
  to_state text not null check (to_state in ('unknown', 'empty', 'occupied', 'away')),
  duration_s integer not null check (duration_s >= 0),  -- seconds spent in from_state / absent / session length
  person_score real,
  diff_ratio real,
  fw_version text
);

-- Added for the gesture study timer: seated seconds within a study session.
alter table public.seat_events add column if not exists studied_s integer check (studied_s >= 0);

-- changed: state transition. away_too_long: person absent past the limit (to_state tells
-- whether belongings were still visible). study_start / study_stop: gesture study timer.
alter table public.seat_events drop constraint if exists seat_events_kind_check;
alter table public.seat_events add constraint seat_events_kind_check
  check (kind in ('changed', 'away_too_long', 'study_start', 'study_stop'));

create index if not exists seat_events_seat_ts on public.seat_events (seat_id, ts desc);

alter table public.seat_events enable row level security;

drop policy if exists "devices can insert" on public.seat_events;
create policy "devices can insert" on public.seat_events
  for insert to anon with check (true);

drop policy if exists "anyone can read" on public.seat_events;
create policy "anyone can read" on public.seat_events
  for select to anon using (true);

-- Device liveness, posted every minute. The page shows the device as online/offline.
create table if not exists public.heartbeats (
  id bigint generated always as identity primary key,
  ts timestamptz not null default now(),
  device_id text not null,
  seat_id text not null,
  state text,
  person_score real,
  diff_ratio real,
  rssi integer,
  uptime_s integer,
  queue integer,
  fw_version text
);

create index if not exists heartbeats_device_ts on public.heartbeats (device_id, ts desc);

alter table public.heartbeats enable row level security;

drop policy if exists "devices can insert" on public.heartbeats;
create policy "devices can insert" on public.heartbeats
  for insert to anon with check (true);

drop policy if exists "anyone can read" on public.heartbeats;
create policy "anyone can read" on public.heartbeats
  for select to anon using (true);

-- Daily summary per seat: time used, time left with belongings, absences, alerts.
create or replace view public.seat_daily with (security_invoker = true) as
select
  seat_id,
  (ts at time zone 'Asia/Seoul')::date as day,
  round(sum(duration_s) filter (where kind = 'changed' and from_state = 'occupied') / 60.0, 1) as occupied_min,
  round(sum(duration_s) filter (where kind = 'changed' and from_state = 'away') / 60.0, 1) as away_min,
  count(*) filter (where kind = 'changed' and to_state = 'away') as away_count,
  count(*) filter (where kind = 'away_too_long') as away_alerts
from public.seat_events
group by seat_id, day;
