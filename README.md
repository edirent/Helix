Helix Framework
===============

Scaffold for a low-latency multi-language trading stack following the provided UML. Components:
- C++ engine: replay -> feature -> decision -> risk -> matching -> recorder, connected via a lock-free ring buffer event bus.
- Go gateway: websocket adapters, orderbook manager, smart router, transport publishers.
- Python strategy: feature builders, model bridge, and strategy logic driving decisions.

Quick Start (dev)
-----------------
- Build C++ engine: `cmake -S cpp_engine -B cpp_engine/build && cmake --build cpp_engine/build`
- Run gateway (simulated feeds): `go run ./gateway/cmd/gateway`
- Run python strategy (simulated loop): `python python_strategy/runner.py`

Layout
------
- `config/` starter configs for engine, gateway, strategy.
- `scripts/` helper launchers for gateway/engine/strategy.
- `data/` raw, processed, and replay inputs.
- `cpp_engine/` C++ core with headers in `include/`, sources in `src/`, tests in `tests/`.
- `gateway/` Go services with websocket adapters, router, executor, transport.
- `python_strategy/` Strategy harness with features, model stubs, transports, and runners.

Notes
-----
- All components are intentionally minimal and self-contained (no external deps) to ease customization.
- Replace simulated publishers/feeds with your real transports (ZMQ, gRPC, exchange websockets) as you wire things up.
