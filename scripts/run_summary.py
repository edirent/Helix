#!/usr/bin/env python3
"""
Run engine for a short replay and emit summary:
- maker/taker fill counts
- fee_ratio
- net_sharpe over 1s/10s buckets
- spread_cost_ticks quantiles per side
- adv_penalty_ticks mean (maker only)
"""
import argparse
import csv
import json
import subprocess
import sys
import re
import tempfile
from pathlib import Path
import numpy as np

ENGINE_BIN = Path("cpp_engine/build/helix_engine_main")
DEFAULT_REPLAY = Path("data/replay/bybit_l2.csv")

FILL_RE = re.compile(
    r"fill side=(?P<side>\d).*spread_paid_ticks=(?P<spr>[\d.e+-]+).*liq=(?P<liq>[MT]).*"
    r"adv_ticks=(?P<adv>[\d.e+-]+).*fee=(?P<fee>[\d.e+-]+).*gross=(?P<gross>[\d.e+-]+)"
)

def parse_log(log_path: Path):
    fills = []
    net_sharpes = {"1s": None, "10s": None}
    with log_path.open() as f:
        for line in f:
            m = FILL_RE.search(line)
            if m:
                side = "BUY" if m.group("side") == "0" else "SELL"
                fills.append(
                    dict(
                        side=side,
                        liq=m.group("liq"),
                        spread=float(m.group("spr")),
                        adv=float(m.group("adv")),
                        fee=float(m.group("fee")),
                        gross=float(m.group("gross")),
                    )
                )
            if "net_sharpe_1s" in line:
                parts = dict(
                    kv.split("=") for kv in line.strip().split() if "=" in kv
                )
                net_sharpes["1s"] = float(parts.get("net_sharpe_1s", 0))
                net_sharpes["10s"] = float(parts.get("net_sharpe_10s", 0))
    return fills, net_sharpes

def summarize(fills, net_sharpes):
    makers = [f for f in fills if f["liq"] == "M"]
    takers = [f for f in fills if f["liq"] == "T"]
    fees = sum(f["fee"] for f in fills)
    gross = sum(f["gross"] for f in fills)
    fee_ratio = fees / gross if gross else float("inf")
    spread_q = {}
    for side in ("BUY", "SELL"):
        vals = [f["spread"] for f in fills if f["side"] == side]
        if vals:
            spread_q[side] = np.percentile(vals, [50, 90, 99]).tolist()
    adv_mean = (
        sum(f["adv"] for f in makers) / len(makers) if makers else 0.0
    )
    return {
        "fills_total": len(fills),
        "makers": len(makers),
        "takers": len(takers),
        "fee_ratio": fee_ratio,
        "net_sharpe_1s": net_sharpes["1s"],
        "net_sharpe_10s": net_sharpes["10s"],
        "spread_cost_ticks": spread_q,
        "adv_penalty_ticks_mean": adv_mean,
    }

def looks_like_l2_csv(path: Path) -> bool:
    try:
        with path.open() as f:
            reader = csv.reader(f)
            header = next(reader, [])
            cols = set([h.strip().lower() for h in header])
            needed = {"seq", "prev_seq", "price", "size"}
            return needed.issubset(cols) and ("book_side" in cols or "side" in cols)
    except Exception:
        return False


def find_candidate_replay() -> Path | None:
    data_dir = Path("data/replay")
    if not data_dir.exists():
        return None
    candidates = []
    for p in data_dir.glob("*.csv"):
        if looks_like_l2_csv(p):
            candidates.append((p.stat().st_mtime, p))
    if not candidates:
        return None
    candidates.sort(reverse=True)
    return candidates[0][1]


def ensure_replay_path(path: Path) -> Path:
    if path.exists() and looks_like_l2_csv(path):
        return path
    if path.exists():
        sys.stderr.write(f"[run_summary] 文件存在但缺少 L2 必需列(seq/prev_seq/price/size/book_side): {path}\n")
    candidate = find_candidate_replay()
    if candidate:
        sys.stderr.write(f"[run_summary] 指定文件无效，改用最新的 L2 CSV: {candidate}\n")
        return candidate
    sys.stderr.write(
        "[run_summary] 找不到可用的 L2 增量 CSV。\n"
        "请先录制真实 L2：例如\n"
        "  go run ./gateway/cmd/bybit_recorder -symbol BTCUSDT -duration 30s -depth 50 -out data/replay/bybit_l2.csv\n"
    )
    sys.exit(1)


def run_and_report():
    parser = argparse.ArgumentParser(description="Run engine on an L2 delta CSV and summarize fills.")
    parser.add_argument("--replay", type=Path, default=DEFAULT_REPLAY, help="L2 delta CSV (seq,prev_seq,book_side,price,size,ts_ms)")
    parser.add_argument("--engine", type=Path, default=ENGINE_BIN, help="Path to helix_engine_main")
    parser.add_argument("--no-actions", action="store_true", help="Disable strategy actions (engine --no_actions)")
    args = parser.parse_args()

    replay_csv = ensure_replay_path(args.replay)
    engine_bin = args.engine
    if not engine_bin.exists():
        sys.stderr.write(f"[run_summary] 找不到引擎可执行文件: {engine_bin}，先运行 cmake 构建。\n")
        sys.exit(1)

    log_fd, log_path = tempfile.mkstemp(prefix="helix_run_", suffix=".log")
    log_file = Path(log_path)
    # Run engine; stderr holds fill logs and summary
    cmd = [str(engine_bin), str(replay_csv)]
    if args.no_actions:
        cmd.append("--no_actions")
    subprocess.run(cmd, stderr=open(log_file, "w"), stdout=subprocess.DEVNULL, check=True)
    fills, sharpes = parse_log(log_file)
    summary = summarize(fills, sharpes)
    print(json.dumps(summary, indent=2))
    print(f"# log at {log_file}")

if __name__ == "__main__":
    run_and_report()
