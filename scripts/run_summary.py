#!/usr/bin/env python3
"""
Run engine for a short replay and emit summary:
- maker/taker fill counts
- fee_ratio
- net_sharpe over 1s/10s buckets
- spread_cost_ticks quantiles per side
- adv_penalty_ticks mean (maker only)
- fee_bps / filled_notional sanity (demo mode)
"""
import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path
import numpy as np

ENGINE_BIN = Path("cpp_engine/build/helix_engine_main")
DEFAULT_REPLAY = Path("data/replay/bybit_l2.csv")

def kv_pairs(line: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for tok in line.strip().split():
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        out[k] = v
    return out

def parse_log(log_path: Path):
    fills = []
    net_sharpes = {"1s": None, "10s": None}
    with log_path.open() as f:
        for line in f:
            if "fill side=" in line:
                kv = kv_pairs(line)
                try:
                    side = "BUY" if kv.get("side", "") == "0" else "SELL"
                    fills.append(
                        dict(
                            side=side,
                            liq=kv.get("liq", ""),
                            spread=float(kv.get("spread_paid_ticks", "0")),
                            adv=float(kv.get("adv_ticks", "0")),
                            fee=float(kv.get("fee", "0")),
                            gross=float(kv.get("gross", "0")),
                            fee_bps=float(kv["fee_bps"]) if "fee_bps" in kv else None,
                            target_notional=float(kv.get("target_notional", "0")),
                            filled_notional=float(kv.get("filled_notional", "0")),
                            src=kv.get("src", ""),
                            best=float(kv["best"]) if "best" in kv else None,
                            mid=float(kv["mid"]) if "mid" in kv else None,
                            exec_cost=float(kv["exec_cost_ticks_signed"]) if "exec_cost_ticks_signed" in kv else None,
                        )
                    )
                except ValueError:
                    continue
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
    adv_mean = sum(f["adv"] for f in makers) / len(makers) if makers else 0.0
    fee_bps_vals = [f["fee_bps"] for f in fills if f.get("fee_bps") is not None]
    notional_target = [f["target_notional"] for f in fills if f.get("target_notional") is not None]
    notional_filled = [f["filled_notional"] for f in fills if f.get("filled_notional") is not None]
    fill_to_target = [
        f["filled_notional"] / f["target_notional"]
        for f in fills
        if f.get("target_notional", 0) > 0
    ]
    exec_costs = [f["exec_cost"] for f in fills if f.get("exec_cost") is not None]
    src_counts = {}
    for f in fills:
        src_counts[f.get("src", "")] = src_counts.get(f.get("src", ""), 0) + 1
    return {
        "fills_total": len(fills),
        "makers": len(makers),
        "takers": len(takers),
        "fee_ratio": fee_ratio,
        "net_sharpe_1s": net_sharpes["1s"],
        "net_sharpe_10s": net_sharpes["10s"],
        "spread_cost_ticks": spread_q,
        "adv_penalty_ticks_mean": adv_mean,
        "fee_bps_p50": float(np.percentile(fee_bps_vals, 50)) if fee_bps_vals else None,
        "fee_bps_p90": float(np.percentile(fee_bps_vals, 90)) if fee_bps_vals else None,
        "filled_notional_p50": float(np.percentile(notional_filled, 50)) if notional_filled else None,
        "filled_notional_p90": float(np.percentile(notional_filled, 90)) if notional_filled else None,
        "filled_to_target_p50": float(np.percentile(fill_to_target, 50)) if fill_to_target else None,
        "filled_to_target_p99": float(np.percentile(fill_to_target, 99)) if fill_to_target else None,
        "exec_cost_ticks_signed": {
            "p50": float(np.percentile(exec_costs, 50)) if exec_costs else None,
            "p90": float(np.percentile(exec_costs, 90)) if exec_costs else None,
            "p99": float(np.percentile(exec_costs, 99)) if exec_costs else None,
            "min": float(np.min(exec_costs)) if exec_costs else None,
            "max": float(np.max(exec_costs)) if exec_costs else None,
        },
        "src_counts": src_counts,
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
    parser.add_argument("--demo-notional", type=float, default=None, help="Run demo taker mode with fixed notional (quote)")
    parser.add_argument("--demo-interval-ms", type=int, default=500, help="Interval between demo actions")
    parser.add_argument("--demo-max", type=int, default=30, help="Max demo actions to issue")
    parser.add_argument("--demo-only", action="store_true", help="Summarize only src=DEMO fills")
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
    if args.demo_notional is not None:
        cmd.extend(
            [
                "--demo_notional",
                str(args.demo_notional),
                "--demo_interval_ms",
                str(args.demo_interval_ms),
                "--demo_max",
                str(args.demo_max),
            ]
        )
    subprocess.run(cmd, stderr=open(log_file, "w"), stdout=subprocess.DEVNULL, check=True)
    fills, sharpes = parse_log(log_file)
    if args.demo_only:
        fills = [f for f in fills if f.get("src") == "DEMO"]
    summary = summarize(fills, sharpes)
    print(json.dumps(summary, indent=2))
    print(f"# log at {log_file}")

if __name__ == "__main__":
    run_and_report()
