"""CI gate for src/ca_bundle.h, the root CAs the firmware trusts for HTTPS.

Fails if a root expires within 180 days, or if the live hosts the firmware talks to no longer
verify against the bundle alone (a CA switch on their side would silently stop uploads).

Usage: python tools/check_ca_bundle.py [host ...]
"""
import datetime as dt
import re
import socket
import ssl
import sys

from cryptography import x509

# Supabase uploads, plus GitHub release check and download for OTA updates.
DEFAULT_HOSTS = ["haivgsjhzpzzfctvtpht.supabase.co", "github.com", "release-assets.githubusercontent.com"]
MIN_DAYS = 180


def main(hosts: list[str]) -> None:
    src = open("src/ca_bundle.h", encoding="ascii").read()
    pem = "".join(s.replace("\\n", "\n") for s in re.findall(r'"([^"]*)"', src))
    errors = []

    now = dt.datetime.now(dt.timezone.utc)
    for block in re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.S):
        cert = x509.load_pem_x509_certificate(block.encode())
        days = (cert.not_valid_after_utc - now).days
        print(f"{cert.subject.rfc4514_string()}: {days} days left")
        if days < MIN_DAYS:
            errors.append(f"{cert.subject.rfc4514_string()} expires in {days} days")

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.load_verify_locations(cadata=pem)
    for host in hosts:
        try:
            with socket.create_connection((host, 443), timeout=10) as sock, \
                    ctx.wrap_socket(sock, server_hostname=host) as tls:
                print(f"{host}: verified ({tls.version()})")
        except (ssl.SSLError, OSError) as e:
            errors.append(f"{host}: {e}")

    if errors:
        sys.exit("FAIL\n- " + "\n- ".join(errors))
    print("OK")


if __name__ == "__main__":
    main(sys.argv[1:] or DEFAULT_HOSTS)
