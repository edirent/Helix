from typing import Dict, Any

from .base_strategy import Action, BaseStrategy, Side


class MeanReversionStrategy(BaseStrategy):
    """Fade dislocations between microprice and mid."""

    def __init__(self, spread_multiple: float = 0.25, max_size: float = 1.0):
        super().__init__("mean_reversion", max_size=max_size)
        self.spread_multiple = spread_multiple

    def on_feature(self, feature: Dict[str, Any]) -> Action:
        microprice = float(feature.get("microprice", 0.0))
        mid = float(feature.get("mid", microprice))
        spread = float(feature.get("spread", 0.0))

        deviation = microprice - mid
        trigger = spread * self.spread_multiple
        size = self.clamp((abs(deviation) / (spread + 1e-6)) * self.max_size)

        if deviation > trigger:
            return Action(Side.SELL, size, reason="mean_revert_sell")
        if deviation < -trigger:
            return Action(Side.BUY, size, reason="mean_revert_buy")
        return Action(Side.HOLD, 0.0, reason="within_band")
