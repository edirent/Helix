from typing import Dict, Any

from .base_strategy import Action, BaseStrategy, Side


class MicroTrendStrategy(BaseStrategy):
    """Momentum-driven microtrend strategy."""

    def __init__(self, threshold: float = 0.01, max_size: float = 1.0):
        super().__init__("microtrend", max_size=max_size)
        self.threshold = threshold

    def on_feature(self, feature: Dict[str, Any]) -> Action:
        strength = float(feature.get("trend_strength", 0.0))
        imbalance = float(feature.get("imbalance", 0.0))
        size = self.clamp(abs(strength) * self.max_size)

        if strength > self.threshold and imbalance > 0:
            return Action(Side.BUY, size, reason="trend_up")
        if strength < -self.threshold and imbalance < 0:
            return Action(Side.SELL, size, reason="trend_down")
        return Action(Side.HOLD, 0.0, reason="flat")
