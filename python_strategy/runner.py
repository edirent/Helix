import time
from typing import Dict, Iterable, List, Tuple

from python_strategy.features.feature_builder import FeatureBuilder
from python_strategy.model.loader import ModelLoader
from python_strategy.model.predict import Predictor
from python_strategy.strategies.base_strategy import Side
from python_strategy.strategies.mean_reversion import MeanReversionStrategy
from python_strategy.strategies.microtrend import MicroTrendStrategy
from python_strategy.transport.grpc_client import GrpcClient
from python_strategy.transport.zmq_client import ZmqClient


def synthetic_stream() -> Iterable[Tuple[Dict[str, float], List[Dict[str, float]]]]:
    books = [
        {"best_bid": 100.0, "best_ask": 100.5, "bid_size": 5.0, "ask_size": 6.0},
        {"best_bid": 100.1, "best_ask": 100.6, "bid_size": 6.0, "ask_size": 5.5},
        {"best_bid": 100.2, "best_ask": 100.7, "bid_size": 7.0, "ask_size": 4.5},
        {"best_bid": 100.15, "best_ask": 100.65, "bid_size": 5.5, "ask_size": 5.0},
    ]
    trades = [
        [{"side": "buy", "qty": 1.0, "price": 100.4}],
        [{"side": "buy", "qty": 2.0, "price": 100.55}],
        [{"side": "sell", "qty": 1.5, "price": 100.65}],
        [{"side": "sell", "qty": 0.8, "price": 100.6}],
    ]
    return zip(books, trades)


def choose_action(actions) -> dict:
    for action in actions:
        if action.side != Side.HOLD:
            return {"side": action.side.value, "size": action.size, "reason": action.reason}
    # default to first (likely HOLD)
    first = actions[0]
    return {"side": first.side.value, "size": first.size, "reason": first.reason}


def main() -> None:
    builder = FeatureBuilder()
    model = ModelLoader().load()
    predictor = Predictor(model)
    strategies = [
        MicroTrendStrategy(threshold=0.01, max_size=1.5),
        MeanReversionStrategy(spread_multiple=0.2, max_size=1.0),
    ]

    feature_pub = ZmqClient("tcp://localhost:7001")
    action_sink = GrpcClient("0.0.0.0:50051")

    for book, trades in synthetic_stream():
        feature = builder.build(book, trades)
        score = predictor.predict(feature)
        feature_payload = feature.to_dict()
        feature_payload["model_score"] = score
        feature_pub.publish_feature(feature_payload)

        actions = [strategy.on_feature(feature_payload) for strategy in strategies]
        action = choose_action(actions)
        action_sink.send_action(action)
        time.sleep(0.1)


if __name__ == "__main__":
    main()
