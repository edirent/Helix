from __future__ import annotations

from dataclasses import dataclass

import pandas as pd
from pandas.api.types import is_datetime64_any_dtype

DEFAULT_RESPONSE_HORIZON_MS = 100


@dataclass(frozen=True)
class ResponseConfig:
    ts_col: str = "ts_ms"
    mid_col: str = "mid"
    delta_ms: int = DEFAULT_RESPONSE_HORIZON_MS
    ts_unit: str = "ms"


def add_response(df: pd.DataFrame, cfg: ResponseConfig = ResponseConfig()) -> pd.DataFrame:
    """
    Compute y(t) = mid(t + Δt) - mid(t) with Δt fixed (default 100ms).

    - Uses feature engine mid values.
    - Drops rows where the future mid does not exist (tail of the series).
    - Keeps the original columns and adds `future_mid` and `y`.
    """
    if df.empty:
        return df.copy()

    missing = [col for col in (cfg.ts_col, cfg.mid_col) if col not in df.columns]
    if missing:
        raise KeyError(f"Missing required columns: {missing}")

    working = df.dropna(subset=[cfg.ts_col, cfg.mid_col]).copy()
    ts_series = working[cfg.ts_col]
    ts_dt = ts_series if is_datetime64_any_dtype(ts_series) else pd.to_datetime(ts_series, unit=cfg.ts_unit)

    working["_ts_dt"] = ts_dt
    working = working.sort_values("_ts_dt").reset_index(drop=True)

    target_ts = working["_ts_dt"] + pd.to_timedelta(cfg.delta_ms, unit="ms")
    lookup = pd.merge_asof(
        target_ts.to_frame("_ts_target").reset_index(drop=True),
        working[["_ts_dt", cfg.mid_col]].rename(columns={"_ts_dt": "_ts_lookup"}).sort_values("_ts_lookup"),
        left_on="_ts_target",
        right_on="_ts_lookup",
        direction="forward",
    )

    working["future_mid"] = lookup[cfg.mid_col].to_numpy()
    working["y"] = working["future_mid"] - working[cfg.mid_col]
    working = working.dropna(subset=["future_mid"])
    return working.drop(columns=["_ts_dt"])
