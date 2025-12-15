from __future__ import annotations

from typing import Optional

import matplotlib.pyplot as plt
import pandas as pd


def conditional_expectation(
    df: pd.DataFrame, feature_col: str = "feature", y_col: str = "y", q: int = 10
) -> pd.DataFrame:
    """
    Compute decile-bin conditional expectation: E[y | feature ∈ bin].

    Returns columns: bin, mu, n, se, tstat.
    """
    if df.empty:
        return pd.DataFrame(columns=["bin", "mu", "n", "se", "tstat"])

    needed = [feature_col, y_col]
    missing = [c for c in needed if c not in df.columns]
    if missing:
        raise KeyError(f"Missing required columns: {missing}")

    working = df.dropna(subset=needed).copy()
    if working.empty:
        return pd.DataFrame(columns=["bin", "mu", "n", "se", "tstat"])

    working["bin"] = pd.qcut(working[feature_col], q=q, duplicates="drop")
    grouped = working.groupby("bin")[y_col]
    stats = pd.DataFrame(
        {
            "mu": grouped.mean(),
            "n": grouped.size(),
            "se": grouped.std(ddof=1) / grouped.size().pow(0.5),
        }
    )
    stats["tstat"] = stats["mu"] / stats["se"]
    stats = stats.reset_index()
    return stats


def plot_condexp(stats: pd.DataFrame, ax: Optional[plt.Axes] = None, title: Optional[str] = None):
    """
    Plot conditional expectation by bin with error bars (mu ± se).
    """
    if stats.empty:
        raise ValueError("No stats to plot")

    local = stats.reset_index(drop=True)
    x = range(len(local))

    if ax is None:
        _, ax = plt.subplots(figsize=(7, 4))

    ax.errorbar(x, local["mu"], yerr=local["se"], fmt="-o", capsize=3)
    ax.axhline(0, color="gray", linewidth=1, linestyle="--")
    ax.set_xticks(list(x))
    ax.set_xticklabels([str(b) for b in local["bin"]], rotation=35, ha="right")
    ax.set_ylabel("E[y | bin]")
    if title:
        ax.set_title(title)
    ax.grid(True, linestyle="--", alpha=0.4)
    return ax
