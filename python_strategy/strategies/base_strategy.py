from dataclasses import dataclass
from enum import Enum
from typing import Dict, Any


class Side(str, Enum):
    BUY = "BUY"
    SELL = "SELL"
    HOLD = "HOLD"


@dataclass
class Action:
    side: Side
    size: float
    reason: str = ""


class BaseStrategy:
    """Common interface for all strategies."""

    def __init__(self, name: str, max_size: float = 1.0):
        self.name = name
        self.max_size = max_size

    def on_feature(self, feature: Dict[str, Any]) -> Action:
        raise NotImplementedError("on_feature must be implemented by subclasses.")

    def clamp(self, size: float) -> float:
        return max(-self.max_size, min(self.max_size, size))
