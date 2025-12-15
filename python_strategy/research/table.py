from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

import pandas as pd

from .response import ResponseConfig, add_response


@dataclass(frozen=True)
class ResearchTableConfig:
    ts_col: str = "ts_ms"
    mid_col: str = "mid"
    feature_col: str = "feature"
    delta_ms: int = ResponseConfig().delta_ms

    def response_cfg(self) -> ResponseConfig:
        return ResponseConfig(ts_col=self.ts_col, mid_col=self.mid_col, delta_ms=self.delta_ms)


def build_research_table(df: pd.DataFrame, cfg: ResearchTableConfig) -> pd.DataFrame:
    """
    Construct the research table with columns:
    ts, mid, feature, future_mid, y = future_mid - mid.

    Rows without a valid future_mid (tail) are dropped.
    """
    missing = [c for c in (cfg.ts_col, cfg.mid_col, cfg.feature_col) if c not in df.columns]
    if missing:
        raise KeyError(f"Missing required columns: {missing}")

    enriched = add_response(df, cfg.response_cfg())
    enriched = enriched.dropna(subset=[cfg.feature_col, "future_mid", "y"])

    keep: Sequence[str] = [cfg.ts_col, cfg.mid_col, cfg.feature_col, "future_mid", "y"]
    return enriched.loc[:, keep]


def load_research_table(path: Path | str, cfg: ResearchTableConfig) -> pd.DataFrame:
    """
    Load an engine CSV (ts | mid | spread | f1 | f2 | ...) and return the research table.
    """
    df = pd.read_csv(path)
    return build_research_table(df, cfg)


def iter_research_tables(paths: Iterable[Path | str], cfg: ResearchTableConfig):
    """Yield per-file research tables for a collection of CSV paths."""
    for p in paths:
        yield Path(p), load_research_table(p, cfg)
