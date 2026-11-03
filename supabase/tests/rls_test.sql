-- CI: the API role (anon) may insert only with its device's token, may read, and may never
-- update or delete. Any unexpected result raises and fails the job (psql ON_ERROR_STOP).
\set ON_ERROR_STOP on

insert into private.device_tokens (device_id, token_sha256)
values ('ci-device', encode(sha256(convert_to('ci-secret', 'UTF8')), 'hex'))
on conflict (device_id) do update set token_sha256 = excluded.token_sha256;

set role anon;

-- correct token: inserts are accepted
select set_config('request.headers', '{"x-device-token":"ci-secret"}', false);
insert into public.seat_events (device_id, seat_id, kind, from_state, to_state, duration_s)
values ('ci-device', 'ci-seat', 'changed', 'unknown', 'empty', 0);
insert into public.heartbeats (device_id, seat_id, state) values ('ci-device', 'ci-seat', 'empty');

do $$
declare
  hdr text;
begin
  -- wrong or missing token: rejected
  foreach hdr in array array['{"x-device-token":"wrong"}', '{}'] loop
    perform set_config('request.headers', hdr, true);
    begin
      insert into public.seat_events (device_id, seat_id, kind, from_state, to_state, duration_s)
      values ('ci-device', 'ci-seat', 'changed', 'unknown', 'empty', 0);
      raise exception 'insert accepted with headers %', hdr;
    exception when insufficient_privilege then null;
    end;
  end loop;

  -- a valid token only works for its own device
  perform set_config('request.headers', '{"x-device-token":"ci-secret"}', true);
  begin
    insert into public.heartbeats (device_id, seat_id) values ('other-device', 'x');
    raise exception 'token accepted for another device';
  exception when insufficient_privilege then null;
  end;

  -- unknown event kinds are rejected by the check constraint
  begin
    insert into public.seat_events (device_id, seat_id, kind, from_state, to_state, duration_s)
    values ('ci-device', 'ci-seat', 'bogus', 'unknown', 'empty', 0);
    raise exception 'unknown kind accepted';
  exception when check_violation then null;
  end;

  -- no update or delete through the API
  begin
    update public.seat_events set duration_s = 1;
    raise exception 'update allowed';
  exception when insufficient_privilege then null;
  end;
  begin
    delete from public.seat_events;
    raise exception 'delete allowed';
  exception when insufficient_privilege then null;
  end;

  -- the status page can read, and the token table stays private
  if (select count(*) from public.seat_events) < 1 then raise exception 'anon cannot read events'; end if;
  begin
    perform 1 from private.device_tokens;
    raise exception 'token table readable';
  exception when insufficient_privilege then null;
  end;
end;
$$;

reset role;
select 'rls tests passed' as result;
