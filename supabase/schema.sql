-- Seat monitor event log. Devices insert and the status page (docs/index.html) reads
-- with the publishable (anon) key; nobody can update or delete rows with that key.
create table if not exists public.seat_events (
  id bigint generated always as identity primary key,
  ts timestamptz not null default now(),
  device_id text not null,
  seat_id text not null,
  kind text not null check (kind in ('changed', 'away_too_long')),
  from_state text not null check (from_state in ('unknown', 'empty', 'occupied', 'away')),
  to_state text not null check (to_state in ('unknown', 'empty', 'occupied', 'away')),
  duration_s integer not null check (duration_s >= 0),  -- seconds spent in from_state
  person_score real,
  diff_ratio real,
  fw_version text
);

create index if not exists seat_events_seat_ts on public.seat_events (seat_id, ts desc);

alter table public.seat_events enable row level security;

drop policy if exists "devices can insert" on public.seat_events;
create policy "devices can insert" on public.seat_events
  for insert to anon with check (true);

-- Seat states are shown on the public status page.
drop policy if exists "anyone can read" on public.seat_events;
create policy "anyone can read" on public.seat_events
  for select to anon using (true);

-- Daily summary per seat: time used, time left with belongings, long-away alerts.
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
