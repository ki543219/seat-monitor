-- CI only: recreate the parts of a Supabase database that schema.sql relies on, on plain Postgres.
do $$
begin
  if not exists (select 1 from pg_roles where rolname = 'anon') then create role anon nologin; end if;
  if not exists (select 1 from pg_roles where rolname = 'authenticated') then create role authenticated nologin; end if;
end;
$$;
grant usage on schema public to anon, authenticated;
-- Supabase grants table privileges to the API roles by default; RLS policies then decide.
alter default privileges in schema public grant select, insert on tables to anon;
