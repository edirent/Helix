#!/usr/bin/env python3
"""
Minimal GUI to record Bybit L1 via websocket and feed it into the C++ backtest.
Pick symbol (BTC/ETH) and duration, then:
1) "Record" -> runs go-based recorder writing CSV under data/replay/.
2) "Run Engine" -> builds and runs helix_engine_main against the latest CSV.
"""
import subprocess
import threading
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox

ROOT = Path(__file__).resolve().parents[1]


def run_cmd(label: str, cmd: str) -> str:
    """Run shell command at repo root and return combined output."""
    proc = subprocess.run(
        ["bash", "-lc", cmd],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    out = proc.stdout + proc.stderr
    if proc.returncode != 0:
        out += f"\n(exit {proc.returncode})"
    return f"[{label}]$ {cmd}\n{out.strip()}\n"


def threaded(fn):
    def wrapper(*args, **kwargs):
        t = threading.Thread(target=fn, args=args, kwargs=kwargs, daemon=True)
        t.start()
    return wrapper


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Helix Bybit回测")
        self.geometry("620x420")
        self.symbol = tk.StringVar(value="BTCUSDT")
        self.duration_s = tk.IntVar(value=60)
        self._build_ui()

    def _build_ui(self):
        frm = ttk.Frame(self, padding=10)
        frm.pack(fill="both", expand=True)

        ttk.Label(frm, text="交易对").grid(row=0, column=0, sticky="w")
        ttk.Combobox(
            frm,
            textvariable=self.symbol,
            values=["BTCUSDT", "ETHUSDT"],
            state="readonly",
            width=12,
        ).grid(row=0, column=1, sticky="w", padx=6)

        ttk.Label(frm, text="录制时长(秒)").grid(row=1, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.duration_s, width=10).grid(row=1, column=1, sticky="w", padx=6)

        btn_row = ttk.Frame(frm)
        btn_row.grid(row=2, column=0, columnspan=3, pady=8, sticky="w")
        ttk.Button(btn_row, text="Record", command=self.record).pack(side="left", padx=4)
        ttk.Button(btn_row, text="Run Engine", command=self.run_engine).pack(side="left", padx=4)

        self.log = tk.Text(frm, height=16)
        self.log.grid(row=3, column=0, columnspan=3, sticky="nsew")
        frm.rowconfigure(3, weight=1)
        frm.columnconfigure(2, weight=1)

    def append_log(self, text: str):
        self.log.insert("end", text + "\n")
        self.log.see("end")

    @threaded
    def record(self):
        try:
            dur = int(self.duration_s.get())
            if dur <= 0:
                raise ValueError
        except Exception:
            messagebox.showerror("错误", "录制时长必须是正整数秒")
            return

        symbol = self.symbol.get()
        out_file = ROOT / "data" / "replay" / f"bybit_{symbol.lower()}.csv"
        cmd = (
            'export PATH="$HOME/.local/go/bin:$HOME/go/bin:$PATH"; '
            f'cd "{ROOT}/gateway" && '
            f'go run ./cmd/bybit_recorder -symbol {symbol} -duration {dur}s -out ../data/replay/{out_file.name}'
        )
        self.append_log(run_cmd("record", cmd))
        messagebox.showinfo("完成", f"录制结束: {out_file}")

    @threaded
    def run_engine(self):
        symbol = self.symbol.get()
        csv_path = ROOT / "data" / "replay" / f"bybit_{symbol.lower()}.csv"
        if not csv_path.exists():
            messagebox.showerror("错误", f"找不到数据文件: {csv_path}\n先录制一次。")
            return
        cmd = (
            f'cd "{ROOT}" && '
            "cmake -S cpp_engine -B cpp_engine/build && "
            "cmake --build cpp_engine/build && "
            f'./cpp_engine/build/helix_engine_main "{csv_path}"'
        )
        self.append_log(run_cmd("engine", cmd))


if __name__ == "__main__":
    App().mainloop()
