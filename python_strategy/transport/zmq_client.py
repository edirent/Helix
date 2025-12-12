import queue
from typing import Optional


class ZmqClient:
    """Lightweight stand-in for a real ZMQ client."""

    def __init__(self, endpoint: str):
        self.endpoint = endpoint
        self._inbox: "queue.Queue" = queue.Queue()

    def publish_feature(self, feature: dict) -> None:
        print(f"[ZMQ -> {self.endpoint}] feature {feature}")

    def recv_action(self, timeout: float = 0.0) -> Optional[dict]:
        try:
            return self._inbox.get(timeout=timeout)
        except queue.Empty:
            return None

    def inject_action(self, action: dict) -> None:
        """Used in tests/simulation to push actions into the queue."""
        self._inbox.put(action)
