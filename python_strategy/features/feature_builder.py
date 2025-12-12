from dataclasses import dataclass
from typing import Dict, Iterable, List

from . import microstructure, orderflow


@dataclass
class FeatureVector:
    imbalance: float = 0.0
    microprice: float = 0.0
    pressure_bid: float = 0.0
    pressure_ask: float = 0.0
    sweep_signal: float = 0.0
    trend_strength: float = 0.0
    spread: float = 0.0
    mid: float = 0.0

    def to_dict(self) -> Dict[str, float]:
        return {
            "imbalance": self.imbalance,
            "microprice": self.microprice,
            "pressure_bid": self.pressure_bid,
            "pressure_ask": self.pressure_ask,
            "sweep_signal": self.sweep_signal,
            "trend_strength": self.trend_strength,
            "spread": self.spread,
            "mid": self.mid,
        }


class FeatureBuilder:
    """Compose microstructure and orderflow features into a single vector."""

    def build(self, orderbook: Dict[str, float], trades: Iterable[Dict[str, float]]) -> FeatureVector:
        micro = microstructure.compute(orderbook)
        flow = orderflow.compute(trades)
        return FeatureVector(
            imbalance=micro["imbalance"],
            microprice=micro["microprice"],
            pressure_bid=flow["pressure_bid"],
            pressure_ask=flow["pressure_ask"],
            sweep_signal=flow["sweep_signal"],
            trend_strength=flow["trend_strength"],
            spread=micro["spread"],
            mid=micro["mid"],
        )

    def stream(self, books: List[Dict[str, float]], trades_stream: List[Iterable[Dict[str, float]]]):
        for book, trades in zip(books, trades_stream):
            yield self.build(book, trades)
