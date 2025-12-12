from typing import Dict


def compute(orderbook: Dict[str, float]) -> Dict[str, float]:
    best_bid = float(orderbook.get("best_bid", 0.0))
    best_ask = float(orderbook.get("best_ask", 0.0))
    bid_size = float(orderbook.get("bid_size", 0.0))
    ask_size = float(orderbook.get("ask_size", 0.0))

    spread = max(0.0, best_ask - best_bid)
    mid = best_bid + spread / 2 if spread > 0 else best_bid
    depth = bid_size + ask_size

    imbalance = (bid_size - ask_size) / depth if depth > 0 else 0.0
    microprice = (
        (best_ask * bid_size + best_bid * ask_size) / depth if depth > 0 else mid
    )

    return {
        "best_bid": best_bid,
        "best_ask": best_ask,
        "bid_size": bid_size,
        "ask_size": ask_size,
        "spread": spread,
        "mid": mid,
        "imbalance": imbalance,
        "microprice": microprice,
    }
