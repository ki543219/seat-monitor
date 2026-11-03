-- Seat monitor schema. Safe to run again: every statement is idempotent.
-- The status page (docs/index.html) reads with the publishable (anon) key. Writes need a
-- per-device token as well (x-device-token header), so the public key alone cannot insert
-- fake rows. Nobody can update or delete rows through the API.

-- Device tokens: only SHA-256 hashes, in a schema the API does not expose.
-- Register a device with the SQL printed by tools/new_device_token.py.
create schema if not exists private;
create table if not exists private.device_tokens (
  device_id text primary key,
  token_sha256 text not null
);
revoke all on private.device_tokens from public;

create or replace function private.device_token_ok(p_device_id text)
returns boolean
language sql stable security definer set search_path = ''
as $$
  select exists (
    select 1 from private.device_tokens t
    where t.device_id = p_device_id
      and t.token_sha256 = encode(sha256(convert_to(coalesce(
            current_setting('request.headers', true)::json ->> 'x-device-token', ''), 'UTF8')), 'hex'));
$$;
revoke all on function private.device_token_ok(text) from public;
grant usage on schema private to anon;
grant execute on function private.device_token_ok(text) to anon;

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
  for insert to anon with check (private.device_token_ok(device_id));

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
  for insert to anon with check (private.device_token_ok(device_id));

drop policy if exists "anyone can read" on public.heartbeats;
create policy "anyone can read" on public.heartbeats
  for select to anon using (true);

-- Heartbeats are only for "is the device alive": keep two days (about 2,900 rows per device).
create or replace function private.prune_heartbeats()
returns trigger
language plpgsql security definer set search_path = ''
as $$
begin
  delete from public.heartbeats where ts < now() - interval '2 days';
  return null;
end;
$$;
drop trigger if exists prune_heartbeats on public.heartbeats;
create trigger prune_heartbeats after insert on public.heartbeats
  for each statement execute function private.prune_heartbeats();

-- Live updates for the status page (Supabase Realtime), when the publication exists.
do $$
begin
  if exists (select 1 from pg_publication where pubname = 'supabase_realtime')
     and not exists (select 1 from pg_publication_tables
                     where pubname = 'supabase_realtime' and schemaname = 'public' and tablename = 'seat_events') then
    alter publication supabase_realtime add table public.seat_events;
  end if;
end;
$$;

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
