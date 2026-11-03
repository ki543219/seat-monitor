"""Guided accuracy test on the real device: sit, leave with belongings, clear the seat.

Usage: python tools/measure.py [rounds] [--port COM3]

For each round you follow the prompt and press Enter; the script watches the seat state the
device reports over serial and records whether the expected state was reached, and how long
it took. A final 60 s seated run counts false changes. Results go to results/accuracy.md as
numbers only (no camera images are requested or stored). Close the viewer / Arduino IDE first.
"""
import argparse
import re
import statistics
import time
from pathlib import Path

import serial

SEAT = {1.0: "occupied", 0.5: "away", 0.0: "empty"}
LABEL = {"occupied": "자리에 있음", "away": "자리 비움 (짐 있음)", "empty": "빈 자리"}
STEPS = [  # (prompt, expected state, timeout s): timeouts cover LEAVE_FRAMES (~10 s) + margin
    ("자리에 앉으세요", "occupied", 15),
    ("가방이나 책을 자리에 두고, 카메라 밖으로 나가세요", "away", 25),
    ("(카메라 밖에서 손만 넣어) 짐을 치우세요", "empty", 15),
]


class Device:
    def __init__(self, port: str):
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.2
        self.s.dtr = self.s.rts = False  # do not reset the board
        self.s.open()
        self.buf = b""

    def states(self):
        """Yields the seat state from each plot line as it arrives."""
        self.buf += self.s.read(self.s.in_waiting or 1)
        *lines, self.buf = self.buf.split(b"\n")
        for line in lines:
            m = re.search(rb"seat:(\d\.\d)", line)
            if m:
                yield SEAT.get(float(m.group(1)))

    def wait_for(self, want: str, timeout: float):
        start, last = time.time(), None
        self.s.reset_input_buffer()
        self.buf = b""
        while time.time() - start < timeout:
            for st in self.states():
                last = st
                if st == want:
                    return True, time.time() - start, st
        return False, None, last


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("rounds", type=int, nargs="?", default=5)
    ap.add_argument("--port", default="COM3")
    args = ap.parse_args()
    dev = Device(args.port)
    results = {want: [] for _, want, _ in STEPS}

    print("카메라 밖에서 시작하세요. 자리는 비어 있어야 합니다.\n")
    for r in range(1, args.rounds + 1):
        for prompt, want, timeout in STEPS:
            input(f"[{r}/{args.rounds}] {prompt} → 마치면 Enter ")
            ok, secs, got = dev.wait_for(want, timeout)
            results[want].append((ok, secs))
            print(f"    {'✓' if ok else '✗'} {LABEL[want]}" + (f" ({secs:.1f}초)" if ok else f" 실패: {LABEL.get(got, got)}"))

    input("\n마지막: 자리에 앉아 60초 동안 평소처럼 공부하세요 (움직여도 됨) → 앉았으면 Enter ")
    dev.wait_for("occupied", 15)
    flips, start, prev = 0, time.time(), "occupied"
    while time.time() - start < 60:
        for st in dev.states():
            if st and st != prev:
                flips += 1
                prev = st
    print(f"    60초 동안 잘못 바뀐 횟수: {flips}")

    rows = []
    for _, want, _ in STEPS:
        res = results[want]
        times = [s for ok, s in res if ok]
        rows.append(f"| {LABEL[want]} | {sum(ok for ok, _ in res)}/{len(res)} | "
                    + (f"{statistics.mean(times):.1f}초" if times else "-") + " |")
    total_ok = sum(ok for res in results.values() for ok, _ in res)
    total = sum(len(res) for res in results.values())
    out = Path("results/accuracy.md")
    out.parent.mkdir(exist_ok=True)
    out.write_text("\n".join([
        f"측정일 {time.strftime('%Y-%m-%d %H:%M')} · {args.rounds}회 반복 · `tools/measure.py`",
        "",
        "| 상황 | 성공 | 평균 감지 시간 |",
        "|---|---|---|",
        *rows,
        f"| **전체** | **{total_ok}/{total} ({total_ok / total * 100:.0f}%)** | |",
        "",
        f"앉은 채 60초 동안 상태가 잘못 바뀐 횟수: **{flips}회**",
        "",
    ]), encoding="utf-8")
    print(f"\n저장: {out}\n" + out.read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
