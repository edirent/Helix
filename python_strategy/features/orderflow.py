from typing import Dict, Iterable


def compute(trades: Iterable[Dict[str, float]]) -> Dict[str, float]:
    """Aggregate lightweight orderflow stats from a trade tape."""
    pressure_bid = 0.0
    pressure_ask = 0.0
    last_price = 0.0
    prev_price = 0.0
    last_size = 0.0

    for trade in trades:
        side = str(trade.get("side", "")).lower()
        qty = float(trade.get("qty", 0.0))
        price = float(trade.get("price", 0.0))
        if side == "buy":
            pressure_bid += qty
        elif side == "sell":
            pressure_ask += qty
        prev_price = last_price
        last_price = price
        last_size = qty

    trend_strength = (last_price - prev_price) if last_price and prev_price else 0.0
    sweep_signal = last_size

    return {
        "pressure_bid": pressure_bid,
        "pressure_ask": pressure_ask,
        "trend_strength": trend_strength,
        "sweep_signal": sweep_signal,
        "last_price": last_price,
        "last_size": last_size,
    }
