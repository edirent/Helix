from typing import Optional


class GrpcClient:
    """Stub gRPC client to send actions back to the engine/gateway."""

    def __init__(self, endpoint: str):
        self.endpoint = endpoint

    def send_action(self, action: dict) -> None:
        print(f"[gRPC -> {self.endpoint}] action {action}")

    def recv_feature(self) -> Optional[dict]:
        return None
