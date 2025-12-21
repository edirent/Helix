#!/usr/bin/env python3
import argparse
import json
import math
import os
import sys
from pathlib import Path


def load_metrics(path: Path) -> dict:
    with path.open() as f:
        return json.load(f)


def count_trades(path: Path) -> int:
    if not path.exists():
        return 0
    n = 0
    with path.open() as f:
        header = f.readline()
        if not header:
            return 0
        for line in f:
            if line.strip():
                n += 1
    return n


def expect_eq(actual, expected, name, tol=1e-6):
    if not math.isfinite(actual) or abs(actual - expected) > tol:
        raise AssertionError(f"{name}: expected {expected}, got {actual}")


def main():
    parser = argparse.ArgumentParser(description="Gate9 metrics validator")
    parser.add_argument("--metrics", type=Path, default=None, help="Path to metrics.json")
    parser.add_argument("--trades", type=Path, default=None, help="Path to trades CSV")
    parser.add_argument("--mode", choices=["maker", "taker", "any"], default=os.getenv("GATE9_MODE", "any"))
    args = parser.parse_args()

    metrics_path = args.metrics or (Path(os.getenv("GATE9_METRICS")) if os.getenv("GATE9_METRICS") else None)
    trades_path = args.trades or (Path(os.getenv("GATE9_TRADES")) if os.getenv("GATE9_TRADES") else None)

    if metrics_path is None:
        print("gate9_check: no metrics path provided; set --metrics or GATE9_METRICS", file=sys.stderr)
        sys.exit(77)
    if not metrics_path.exists():
        print(f"gate9_check: metrics path not found: {metrics_path}", file=sys.stderr)
        sys.exit(77)

    m = load_metrics(metrics_path)
    failures = []

    try:
        if not m.get("identity_ok", False):
            failures.append("identity_ok is false")

        # Trade skew coverage
        skew = m.get("trade_ts_skew_ms", {})
        skew_n = skew.get("n", 0)
        if trades_path:
            trades_n = count_trades(trades_path)
            if trades_n > 0 and skew_n < 0.9 * trades_n:
                failures.append(f"trade_ts_skew_ms.n {skew_n} < 0.9 * trades_rows {trades_n}")
        else:
            if skew_n <= 0:
                failures.append("trade_ts_skew_ms.n not positive and no trades file provided")

        n_maker = m.get("n_maker_fills", m.get("makers", 0))
        n_taker = m.get("n_taker_fills", m.get("takers", 0))

        fee_m = m.get("fee_bps_maker", {})
        fee_t = m.get("fee_bps_taker", {})
        if n_maker > 0:
            try:
                expect_eq(float(fee_m.get("p99", 0)), 2.0, "fee_bps_maker.p99")
            except AssertionError as e:
                failures.append(str(e))
        if n_taker > 0:
            try:
                expect_eq(float(fee_t.get("p99", 0)), 6.0, "fee_bps_taker.p99")
            except AssertionError as e:
                failures.append(str(e))

        adv = m.get("maker_adv_selection_ticks", {})
        adv_count = adv.get("count", 0)
        if n_maker > 0 and adv_count != n_maker:
            failures.append(f"adv_selection count {adv_count} != n_maker_fills {n_maker}")

        mode = args.mode
        if mode == "maker" and n_maker < 200:
            failures.append(f"maker mode requires n_maker_fills >= 200, got {n_maker}")
        if mode == "taker" and n_taker <= 0:
            failures.append("taker mode requires taker fills > 0")

    except Exception as e:  # catch parsing errors as failures
        failures.append(f"exception during checks: {e}")

    if failures:
        for f in failures:
            print("FAIL:", f, file=sys.stderr)
        sys.exit(1)

    print("gate9_check: PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
