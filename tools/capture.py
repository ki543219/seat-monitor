"""Collect 96x96 grayscale training images from the ESP32 camera over serial.

Usage: python tools/capture.py <label> [count] [--port COM3]
  e.g. python tools/capture.py palm 150

Saves PNGs to data/gesture/<label>/. Close any serial monitor first (the port can only be opened once).
"""
import argparse
import time
from pathlib import Path

import serial
from PIL import Image

SIZE = 96
MAGIC = b"FRM0"


def read_frame(port: serial.Serial, timeout: float = 3.0) -> bytes | None:
    port.reset_input_buffer()
    port.write(b"F")
    deadline = time.time() + timeout
    window = b""
    while time.time() < deadline:
        window = (window + port.read(1))[-4:]
        if window == MAGIC:
            data = port.read(SIZE * SIZE)
            return data if len(data) == SIZE * SIZE else None
    return None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("label")
    ap.add_argument("count", type=int, nargs="?", default=150)
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--interval", type=float, default=0.25, help="seconds between frames")
    args = ap.parse_args()

    out = Path("data/gesture") / args.label
    out.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%H%M%S")

    with serial.Serial(args.port, 115200, timeout=1) as port:
        for i in (3, 2, 1):
            print(f"{args.label}: {i}...", flush=True)
            time.sleep(1)
        print("capturing — move your hand around: closer, farther, left, right, tilted")
        saved = 0
        while saved < args.count:
            frame = read_frame(port)
            if frame is None:
                print("  no frame, retrying")
                continue
            Image.frombytes("L", (SIZE, SIZE), frame).save(out / f"{args.label}_{stamp}_{saved:03d}.png")
            saved += 1
            if saved % 10 == 0:
                print(f"  {saved}/{args.count}", flush=True)
            time.sleep(args.interval)
    print(f"done: {saved} images in {out} (total {len(list(out.glob('*.png')))})")


if __name__ == "__main__":
    main()
