#!/usr/bin/env python3
"""
Compare recorder and replay bookcheck CSVs for equality within tolerance.
"""
import csv
import sys
from pathlib import Path

TOL = 1e-6

def read_rows(path):
    rows = []
    with path.open() as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(
                dict(
                    ts_ms=int(r["ts_ms"]),
                    seq=int(r["seq"]),
                    best_bid=float(r["best_bid"]),
                    best_ask=float(r["best_ask"]),
                    bid_size=float(r["bid_size"]),
                    ask_size=float(r["ask_size"]),
                )
            )
    return rows

def close(a, b, tol=TOL):
    return abs(a - b) <= tol

def main():
    if len(sys.argv) != 3:
        print("usage: bookcheck_diff.py recorder_bookcheck.csv replay_bookcheck.csv")
        sys.exit(1)
    rec_path = Path(sys.argv[1])
    rep_path = Path(sys.argv[2])
    rec = read_rows(rec_path)
    rep = read_rows(rep_path)
    if len(rec) != len(rep):
        print(f"length mismatch: recorder={len(rec)} replay={len(rep)}")
        sys.exit(1)
    for i, (r, p) in enumerate(zip(rec, rep)):
        for key in ("ts_ms", "seq"):
            if r[key] != p[key]:
                print(f"row {i} {key} mismatch: {r[key]} vs {p[key]}")
                sys.exit(1)
        for key in ("best_bid", "best_ask", "bid_size", "ask_size"):
            if not close(r[key], p[key]):
                print(f"row {i} {key} mismatch: {r[key]} vs {p[key]}")
                sys.exit(1)
    print("bookcheck files match")

if __name__ == "__main__":
    main()
