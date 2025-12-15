#!/usr/bin/env python3
# 简易研究脚本：调用 C++ feature engine 计算特征，构造研究表，做条件期望并保存图像。
import argparse
from pathlib import Path
import sys
import subprocess
import tempfile

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


def ensure_feature_dump(binary: Path) -> None:
    if binary.exists():
        return
    build_dir = ROOT / "cpp_engine" / "build"
    cmake_cmd = ["cmake", "-S", str(ROOT / "cpp_engine"), "-B", str(build_dir)]
    build_cmd = ["cmake", "--build", str(build_dir), "--target", "feature_dump"]
    for cmd in (cmake_cmd, build_cmd):
        res = subprocess.run(cmd, cwd=ROOT, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"命令失败: {' '.join(cmd)}")


def run_feature_dump(input_csv: Path, binary: Path) -> pd.DataFrame:
    ensure_feature_dump(binary)
    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as tmp:
        tmp_path = Path(tmp.name)
    try:
        cmd = [str(binary), str(input_csv), str(tmp_path)]
        res = subprocess.run(cmd, cwd=ROOT, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"feature_dump 运行失败: {' '.join(cmd)}")
        return pd.read_csv(tmp_path)
    finally:
        if tmp_path.exists():
            tmp_path.unlink()


def main():
    parser = argparse.ArgumentParser(description="对 L1 imbalance 做条件期望检验")
    parser.add_argument("csv", type=Path, help="输入 CSV（含 ts_ms,best_bid,best_ask,bid_size,ask_size）")
    parser.add_argument("--delta-ms", type=int, default=100, help="Δt 毫秒（默认 100ms）")
    parser.add_argument("--out", type=Path, default=Path("condexp.png"), help="输出图像文件")
    parser.add_argument(
        "--feature-bin",
        type=Path,
        default=ROOT / "cpp_engine" / "build" / "feature_dump",
        help="C++ feature_dump 可执行文件路径（默认使用构建目录）",
    )
    args = parser.parse_args()

    df_feat = run_feature_dump(args.csv, args.feature_bin)

    cfg = ResearchTableConfig(ts_col="ts_ms", mid_col="mid", feature_col="imbalance", delta_ms=args.delta_ms)
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
