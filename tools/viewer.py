"""Live local viewer: what the ESP32 camera sees and how the seat is being judged.

Usage: python tools/viewer.py [--port COM3]

Shows the 96x96 camera frame, person score / scene diff against their thresholds, the
current seat state and the device's event log. Frames stay in memory only: nothing is
saved to disk or sent anywhere. Close the Arduino IDE serial monitor/plotter first
(a serial port can only be open in one program).
"""
import argparse
import queue
import re
import threading
import time
import tkinter as tk

import serial
from PIL import Image, ImageTk

SIZE = 96
SCALE = 4
MAGIC = b"FRM0"
FRAME_EVERY_S = 0.3

STATE = {  # seat value in the plot line -> label, color
    1.0: ("자리에 있음", "#1f7a55"),
    0.5: ("자리 비움 (짐 있음)", "#dd8530"),
    0.0: ("빈 자리", "#8c9893"),
}
PLOT = re.compile(r"(\w+):(-?\d+(?:\.\d+)?)")


def reader(port: serial.Serial, out: queue.Queue, stop: threading.Event) -> None:
    """Splits the serial stream into text lines and binary frames."""
    buf = b""
    last_req = 0.0
    waiting = False
    while not stop.is_set():
        now = time.time()
        if not waiting or now - last_req > 2:  # re-request if a frame got lost
            port.write(b"F")
            last_req, waiting = now, True
        buf += port.read(port.in_waiting or 1)
        while True:
            m = buf.find(MAGIC)
            nl = buf.find(b"\n")
            if m != -1 and (nl == -1 or m < nl):
                if len(buf) < m + 4 + SIZE * SIZE:
                    break
                out.put(("frame", buf[m + 4 : m + 4 + SIZE * SIZE]))
                buf = buf[m + 4 + SIZE * SIZE :]
                waiting = False
                time.sleep(max(0, FRAME_EVERY_S - (time.time() - last_req)))
            elif nl != -1:
                out.put(("line", buf[:nl].decode("utf-8", "replace").strip()))
                buf = buf[nl + 1 :]
            else:
                break


class Viewer:
    def __init__(self, root: tk.Tk, port_name: str):
        self.root = root
        self.q: queue.Queue = queue.Queue()
        self.stop = threading.Event()
        root.title("Seat Monitor — 로컬 뷰어 (저장·전송 없음)")
        root.configure(bg="#f4f6f5")

        left = tk.Frame(root, bg="#f4f6f5")
        left.pack(side="left", padx=16, pady=16, anchor="n")
        self.img_label = tk.Label(left, bg="#000", width=SIZE * SCALE, height=SIZE * SCALE)
        self.img_label.pack()
        self.fps = tk.Label(left, text="카메라 연결 중…", bg="#f4f6f5", fg="#7a8580", font=("Malgun Gothic", 9))
        self.fps.pack(anchor="w", pady=(6, 0))

        right = tk.Frame(root, bg="#f4f6f5")
        right.pack(side="left", padx=(0, 16), pady=16, fill="both", expand=True)
        self.state = tk.Label(right, text="확인 중", bg="#f4f6f5", fg="#16201b", font=("Malgun Gothic", 22, "bold"), anchor="w")
        self.state.pack(fill="x")
        self.bars = tk.Canvas(right, width=380, height=150, bg="#ffffff", highlightthickness=1, highlightbackground="#e2e7e4")
        self.bars.pack(fill="x", pady=(10, 10))
        tk.Label(right, text="기기 로그", bg="#f4f6f5", fg="#47534d", font=("Malgun Gothic", 10, "bold"), anchor="w").pack(fill="x")
        self.log = tk.Text(right, width=52, height=14, font=("Consolas", 9), bg="#ffffff", fg="#16201b", relief="flat")
        self.log.pack(fill="both", expand=True)

        self.values = {}
        self.frames = []
        self.port = serial.Serial()
        self.port.port, self.port.baudrate, self.port.timeout = port_name, 115200, 0.1
        self.port.dtr = self.port.rts = False  # opening must not reset the board
        try:
            self.port.open()
        except serial.SerialException as e:
            self.state.config(text="포트를 열 수 없음", fg="#b3261e")
            self.add_log(f"{port_name}: {e}\nArduino IDE 시리얼 모니터/플로터가 열려 있으면 닫아 주세요.")
            return
        threading.Thread(target=reader, args=(self.port, self.q, self.stop), daemon=True).start()
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(50, self.pump)

    def add_log(self, text: str) -> None:
        self.log.insert("end", time.strftime("%H:%M:%S ") + text + "\n")
        self.log.see("end")

    def pump(self) -> None:
        while not self.q.empty():
            kind, data = self.q.get()
            if kind == "frame":
                img = Image.frombytes("L", (SIZE, SIZE), data).resize((SIZE * SCALE, SIZE * SCALE), Image.NEAREST)
                self.photo = ImageTk.PhotoImage(img)
                self.img_label.config(image=self.photo, width=SIZE * SCALE, height=SIZE * SCALE)
                self.frames = [t for t in self.frames if time.time() - t < 3] + [time.time()]
                self.fps.config(text=f"ESP32 카메라 96×96 · {len(self.frames) / 3:.1f} fps · 화면은 저장되지 않음")
            elif data.startswith("person:"):
                self.values = {k: float(v) for k, v in PLOT.findall(data)}
                self.draw()
            elif data:
                self.add_log(data.lstrip("# "))
        self.root.after(50, self.pump)

    def draw(self) -> None:
        v = self.values
        label, color = STATE.get(v.get("seat", -1), ("확인 중", "#16201b"))
        self.state.config(text=label, fg=color)
        c = self.bars
        c.delete("all")
        w = int(c.winfo_width()) or 380
        x0, x1 = 120, w - 20

        def bar(y, name, value, marks, fill):
            c.create_text(12, y + 9, text=name, anchor="w", font=("Malgun Gothic", 10), fill="#47534d")
            c.create_rectangle(x0, y, x1, y + 18, fill="#eef1ef", outline="")
            c.create_rectangle(x0, y, x0 + (x1 - x0) * max(0, min(1, value)), y + 18, fill=fill, outline="")
            for m, txt in marks:
                x = x0 + (x1 - x0) * m
                c.create_line(x, y - 4, x, y + 22, fill="#16201b", width=2)
                c.create_text(x, y + 30, text=txt, font=("Malgun Gothic", 8), fill="#47534d")
            c.create_text(x1, y - 8, text=f"{value:.2f}", anchor="e", font=("Consolas", 10, "bold"), fill="#16201b")

        bar(22, "사람 점수", v.get("person", 0), [(v.get("person_off", 0.3), "떠남 기준"), (v.get("person_on", 0.5), "앉음 기준")], "#1f7a55")
        bar(92, "화면 변화(짐)", v.get("diff", 0), [(v.get("diff_threshold", 0.08), "짐 기준")], "#dd8530")

    def close(self) -> None:
        self.stop.set()
        time.sleep(0.2)
        if self.port.is_open:
            self.port.close()
        self.root.destroy()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    args = ap.parse_args()
    root = tk.Tk()
    Viewer(root, args.port)
    root.mainloop()


if __name__ == "__main__":
    main()
