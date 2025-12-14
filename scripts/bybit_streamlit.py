#!/usr/bin/env python3
"""
Streamlit GUI for recording Bybit L1 data and running the C++ backtest.
"""
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Tuple

import streamlit as st

ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data" / "replay"


@dataclass
class CmdResult:
    ok: bool
    output: str


def run_cmd(label: str, cmd: str) -> CmdResult:
    """Run shell command in repo root, return success flag and combined output."""
    proc = subprocess.run(
        ["bash", "-lc", cmd],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    output = f"$ {cmd}\n{proc.stdout}{proc.stderr}".strip()
    if proc.returncode != 0:
        output += f"\n(exit {proc.returncode})"
    return CmdResult(ok=proc.returncode == 0, output=output)


def record(symbol: str, depth: int, duration: int, outfile: Path) -> CmdResult:
    outfile.parent.mkdir(parents=True, exist_ok=True)
    cmd = (
        'export PATH="$HOME/.local/go/bin:$HOME/go/bin:$PATH"; '
        f'cd "{ROOT}/gateway" && '
        f'go run ./cmd/bybit_recorder -symbol {symbol} -duration {duration}s -depth {depth} -out ../data/replay/{outfile.name}'
    )
    return run_cmd("record", cmd)


def run_engine(csv_path: Path) -> CmdResult:
    cmd = (
        f'cd "{ROOT}" && '
        "cmake -S cpp_engine -B cpp_engine/build && "
        "cmake --build cpp_engine/build && "
        f'./cpp_engine/build/helix_engine_main "{csv_path}"'
    )
    return run_cmd("engine", cmd)


def layout_header():
    st.set_page_config(page_title="Helix Bybit 回测", layout="wide")
    st.title("Helix Bybit 回测面板")
    st.caption("录制 Bybit 顶级盘口 -> 写入 CSV -> 跑 C++ 引擎")


def layout_controls() -> Tuple[str, int, int]:
    col1, col2, col3 = st.columns([1, 1, 1])
    with col1:
        symbol = st.selectbox("交易对", ["BTCUSDT", "ETHUSDT"], index=0)
    with col2:
        duration = st.slider("录制时长 (秒)", min_value=10, max_value=300, value=60, step=10)
    with col3:
        depth = st.radio("深度", options=[1, 50], index=0, horizontal=True)
    return symbol, duration, depth


def main():
    layout_header()
    symbol, duration, depth = layout_controls()

    latest_file = DATA_DIR / f"bybit_{symbol.lower()}.csv"
    st.write(f"输出文件: `{latest_file}`")

    if "log" not in st.session_state:
        st.session_state.log = ""

    colA, colB = st.columns(2)

    with colA:
        if st.button("开始录制", type="primary"):
            with st.spinner("录制中..."):
                res = record(symbol, depth, duration, latest_file)
            st.session_state.log = res.output + "\n\n" + st.session_state.log
            if res.ok:
                st.success(f"录制完成: {latest_file}")
            else:
                st.error("录制失败，查看日志")

    with colB:
        if st.button("运行引擎"):
            if not latest_file.exists():
                st.error(f"缺少数据文件: {latest_file}，请先录制。")
            else:
                with st.spinner("回测中..."):
                    res = run_engine(latest_file)
                st.session_state.log = res.output + "\n\n" + st.session_state.log
                if res.ok:
                    st.success("引擎运行完毕")
                else:
                    st.error("引擎运行失败，查看日志")

    st.divider()
    st.subheader("日志")
    st.text_area(
        label="",
        value=st.session_state.log,
        height=240,
        label_visibility="collapsed",
    )
    st.markdown(
        """
**提示**
- 首次使用先安装：`pip install streamlit`
- 运行方式：`cd ~/Helix && streamlit run scripts/bybit_streamlit.py`
- 确保 Go 在 PATH（脚本已自动添加 `~/.local/go/bin:$HOME/go/bin`）。
        """.strip()
    )


if __name__ == "__main__":
    main()
