#!/usr/bin/env python3
# 简易研究脚本：读取 L1 CSV，构造研究表，做条件期望并保存图像。
import argparse
from pathlib import Path
import sys

import matplotlib.pyplot as plt
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from python_strategy.research import (
    ResearchTableConfig,
    build_research_table,
    conditional_expectation,
    plot_condexp,
)


def compute_feature(df: pd.DataFrame, bid_col="best_bid", ask_col="best_ask", bid_sz="bid_size", ask_sz="ask_size"):
    """用 L1 档构造 mid 和 imbalance 特征。"""
    df = df.copy()
    spread = (df[ask_col] - df[bid_col]).clip(lower=0.0)
    df["mid"] = df[bid_col] + spread / 2.0
    depth = df[bid_sz] + df[ask_sz]
    df["feature"] = (df[bid_sz] - df[ask_sz]) / depth.replace(0, pd.NA)
    return df


def main():
    parser = argparse.ArgumentParser(description="对 L1 imbalance 做条件期望检验")
    parser.add_argument("csv", type=Path, help="输入 CSV（含 ts_ms,best_bid,best_ask,bid_size,ask_size）")
    parser.add_argument("--delta-ms", type=int, default=100, help="Δt 毫秒（默认 100ms）")
    parser.add_argument("--out", type=Path, default=Path("condexp.png"), help="输出图像文件")
    args = parser.parse_args()

    raw = pd.read_csv(args.csv)
    df_feat = compute_feature(raw)

    cfg = ResearchTableConfig(ts_col="ts_ms", mid_col="mid", feature_col="feature", delta_ms=args.delta_ms)
    table = build_research_table(df_feat, cfg)

    stats = conditional_expectation(table, feature_col=cfg.feature_col, y_col="y", q=10)
    print("条件期望统计（按分位数桶）：")
    print(stats)

    ax = plot_condexp(stats, title=f"imbalance -> Δmid {args.delta_ms}ms")
    plt.tight_layout()
    plt.savefig(args.out, dpi=180)
    print(f"图像已保存到 {args.out}")


if __name__ == "__main__":
    main()
