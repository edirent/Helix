import math
from typing import Callable

from python_strategy.features.feature_builder import FeatureVector


class Predictor:
    """Wraps a model callable and exposes a probability-like score."""

    def __init__(self, model_fn: Callable[[dict], float]):
        self.model_fn = model_fn

    def predict(self, feature: FeatureVector) -> float:
        raw = float(self.model_fn(feature.to_dict()))
        return 1.0 / (1.0 + math.exp(-raw))
