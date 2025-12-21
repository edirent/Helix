#!/usr/bin/env python3
"""
Simple latency fitter: read CSV/tsv with latency_ms column (or raw numbers per line)
and emit JSON with base/jitter/tail/tail_prob derived from quantiles.
"""
import argparse
import csv
import json
import math
from pathlib import Path
from statistics import median


def load_samples(path: Path):
    samples = []
    with path.open() as f:
        first_line = f.readline()
        f.seek(0)
        has_comma = "," in first_line
        reader = csv.DictReader(f) if has_comma else None
        if reader and "latency_ms" in reader.fieldnames:
            for row in reader:
                try:
                    samples.append(float(row["latency_ms"]))
                except (KeyError, ValueError):
                    continue
        else:
            # treat as plain numbers per line
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    samples.append(float(line.split(",")[0]))
                except ValueError:
                    continue
    return samples


def pct(values, p):
    if not values:
        return 0.0
    idx = int(round((p / 100.0) * (len(values) - 1)))
    return sorted(values)[idx]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path, help="CSV or plaintext with latency_ms column or raw numbers")
    ap.add_argument("--out", type=Path, default=Path("config/latency_fit.json"))
    args = ap.parse_args()

    samples = load_samples(args.input)
    if not samples:
        raise SystemExit("no samples found")

    p50 = pct(samples, 50)
    p90 = pct(samples, 90)
    p99 = pct(samples, 99)
    tail_prob = sum(1 for s in samples if s > p90) / len(samples)
    fit = {
        "base_ms": p50,
        "jitter_ms": max(p90 - p50, 0.0),
        "tail_ms": max(p99 - p90, 0.0),
        "tail_prob": tail_prob,
        "n": len(samples),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(fit, indent=2))
    print(json.dumps(fit, indent=2))


if __name__ == "__main__":
    main()
