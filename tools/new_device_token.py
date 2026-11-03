"""Create a device token: prints the line for include/secrets.h and the SQL that registers it.

Usage: python tools/new_device_token.py xiao-01

Only the SHA-256 hash goes into the database; keep the token itself in secrets.h (git-ignored).
"""
import hashlib
import secrets
import sys


def main(device_id: str) -> None:
    token = secrets.token_urlsafe(32)
    digest = hashlib.sha256(token.encode()).hexdigest()
    print("# include/secrets.h")
    print(f'#define DEVICE_TOKEN "{token}"')
    print()
    print("-- Supabase SQL Editor")
    print("insert into private.device_tokens (device_id, token_sha256)")
    print(f"values ('{device_id}', '{digest}')")
    print("on conflict (device_id) do update set token_sha256 = excluded.token_sha256;")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
