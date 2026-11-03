-- Clears all recorded events and heartbeats, e.g. before a demo. IRREVERSIBLE.
-- Schema, policies and device tokens stay. Run it yourself in the Supabase SQL Editor.
truncate public.seat_events, public.heartbeats restart identity;
