from typing import Callable, Dict, Optional


class ModelLoader:
    """Placeholder model loader. Swap out with your serialized model format."""

    def __init__(self, path: Optional[str] = None):
        self.path = path

    def load(self) -> Callable[[Dict[str, float]], float]:
        def model(features: Dict[str, float]) -> float:
            # Simple linear score as a placeholder.
            weights = {
                "imbalance": 0.5,
                "trend_strength": 1.0,
                "sweep_signal": 0.2,
            }
            return sum(weights.get(k, 0.0) * v for k, v in features.items())

        return model
