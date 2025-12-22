# Helix — Deterministic Microstructure Replay + Matching + Metrics (Research-Grade)

Helix is a **deterministic** market microstructure replay and execution-simulation stack designed to answer one question:

> “If I had placed these orders under realistic exchange rules, fees, and latency, what would have happened — and can I prove the engine didn’t lie?”

Helix focuses on:
- **Order book replay (L2 deltas)** with strict invariants
- **Optional trade tape ingestion** for maker fills
- **Latency + causality** (decision time → delayed execution on a later book)
- **Rules + fee enforcement** via configurable venue profiles
- **Order lifecycle correctness** (place/cancel/replace/expire + tests)
- **Structured outputs** (`fills.csv`, `metrics.json`) and CI gates that fail loudly

> **Note:** Helix is for **research and validation**. It is *not* a production trading system.

---

## 1) Architecture Overview

### End-to-end pipeline (core idea)
```mermaid
graph TD
    A[L2 Delta CSV (Book)\n+ optional Trades CSV] -->|Inputs| B[C++ Engine]
    
    subgraph "C++ Engine"
        B1[TickReplay -> FeatureEngine -> Decision/Demo -> Rules]
        B1 -->|Orders| B2[OrderManager\n(lifecycle)]
        B2 -->|Taker| B3[MatchingEngine]
        B2 -->|Maker| B4[MakerQueueSim]
        B3 --> B5[RiskEngine\n(realized/unrealized/fees)]
        B4 --> B5
    end
    
    B5 --> C[fills.csv + metrics.json]

```

### What “deterministic” means here

Given the **same inputs**:

* L2 delta CSV
* trades CSV (if used)
* rules config
* latency config / fit
* engine flags

You should get **identical outputs** (metrics and fills), aside from `run_id`/timestamps.

---

## 2) Repository Layout (high-level)

* `cpp_engine/`
* `src/` — main engine loop, matching, replay, outputs
* `include/engine/` — shared engine modules (latency, maker_queue, order_utils, types, deterministic hash)
* `tests/` — deterministic CI gates and synthetic regressions
* `build/` — CMake build output (ignored)


* `gateway/`
* recorders and utilities (L2 recorder, trades recorder — WS/HTTP variants)


* `scripts/`
* helpers: summaries, bookcheck diffs, gate checks


* `data/replay/`
* replay CSVs / mini tapes used for local testing and CI (if included)


* `runs/`
* per-run structured outputs (usually untracked)



---

## 3) Prerequisites

### C++

* CMake (>= 3.20 recommended)
* A C++17 compiler (clang or gcc)
* Linux/macOS recommended

### Go

* Go 1.20+ (for recorders)

### Python

* Python 3.10+
* `numpy` (and any additional deps used by scripts)

---

## 4) Build

1. **Configure and Build**
```bash
cmake -S cpp_engine -B cpp_engine/build
cmake --build cpp_engine/build -j

```


2. **Run tests (full suite)**
```bash
ctest --test-dir cpp_engine/build --output-on-failure

```


3. **List tests**
```bash
ctest --test-dir cpp_engine/build -N

```



---

## 5) Inputs: Data Formats

### 5.1 L2 Delta CSV (required)

Must contain fields like:

* `ts_ms` — event timestamp (ms)
* `seq` / `prev_seq` — strict sequence continuity
* `type` — delta type (implementation-defined)
* `book_side` — bid or ask only
* `price`, `size`

**Strict rules:**

* `negative size` → fatal
* `abs(size) < eps` treated as delete level
* `seq gap` or `rollback` → fatal
* `crossed book` or `invalid top-of-book` → fatal (invariant enforcement)

### 5.2 Trades CSV (optional, required for realistic maker)

If you want maker fills to be trade-driven, you provide `--trades <file>`.
**Fields (typical):** `ts_ms`, `side`, `price`, `size`, plus identifiers like `exec_id`.

> Engine will use trades to drive maker queue consumption before depth deltas.

### 5.3 Bookcheck CSV (optional, for determinism audits)

Bookcheck captures top-of-book snapshots at a fixed stride:

* `ts_ms`, `seq`, `best_bid`, `best_ask`, `bid_size`, `ask_size`

Helix supports:

* recorder-side bookcheck
* replay-side bookcheck
* `diff` script to enforce equality within tolerance

---

## 6) Running the Engine

**Binary location:** `cpp_engine/build/helix_engine_main`

### 6.1 “No actions” sanity run

Validates that “market data replay does not magically trade” (must be zero fills).

```bash
./cpp_engine/build/helix_engine_main data/replay/btc_l2.csv \
  --no_actions \
  --run_id sanity_no_actions

```

**Expected:** `fills_total = 0`, `fees = 0`, `net_total = 0`, no fill rows in `fills.csv`.

### 6.2 Taker demo run

Validates rules, fee, latency, and causality for takers.

```bash
./cpp_engine/build/helix_engine_main data/replay/btc_l2.csv \
  --trades data/replay/btc_trades.csv \
  --demo_only \
  --demo_notional 200 \
  --demo_interval_ms 5 \
  --demo_max 4000 \
  --rules_config venue_rules.yaml \
  --latency_fit latency_fit.json \
  --run_id taker_demo

```

### 6.3 Maker demo run

Validates maker placement, trade-driven fills, queue time, and adverse selection.

```bash
./cpp_engine/build/helix_engine_main data/replay/btc_l2.csv \
  --trades data/replay/btc_trades.csv \
  --demo_only \
  --maker_demo \
  --maker_notional 200 \
  --maker_interval_ms 5 \
  --maker_max 2000 \
  --maker_ttl_ms 2000 \
  --adv_horizon_ms 100 \
  --adv_fatal_missing 1 \
  --rules_config venue_rules.yaml \
  --latency_fit latency_fit.json \
  --run_id maker_demo

```

---

## 7) Outputs (Structured, Per Run)

Each run writes to:

* `runs/<run_id>/fills.csv`
* `runs/<run_id>/metrics.json`

### 7.1 `fills.csv` (per fill)

Contains traceable columns like:

* `seq`, `ts_ms`, `order_id`
* `side`, `liquidity` (M/T), `src` (DEMO/STRAT/etc.)
* `status`, `reason` (reject reasons if applicable)
* `vwap`, `filled`, `unfilled`, `levels`
* `fee`, `fee_bps`
* `mid`, `best`, `spread`, `slip_ticks`
* `exec_cost_ticks_signed`, `crossing`
* **Maker-only:** `queue_time_ms`, `adv_selection_ticks`

### 7.2 `metrics.json` (per run)

Includes:

* **PnL:** realized, unrealized, fees, gross, net_total
* **Identity gate:** `identity_ok` (fatal if broken)
* **Sharpe with guards:** `sharpe_1s`, `sharpe_10s` (std/eps handling)
* **Drawdown:** `max_drawdown`
* **Turnover, Fill-rate**
* **Counts:** `fills_total`, `n_maker_fills`, `n_taker_fills`, rejects and reason counts
* **Distributions:**
* `fee_bps_maker` / `fee_bps_taker` (p50/p90/p99)
* `exec_cost_ticks_signed_maker/taker` (p50/p90/p99/std)


* **Lifecycle metrics:** orders placed/cancelled/replaced/expired, illegal transitions, noops, peaks
* **Maker analytics:** `maker_fill_rate`, queue-time quantiles, `maker_adv_selection_ticks`
* **Trade tape analytics:** `trade_ts_skew_ms` stats (n matches tape after draining)
* **Config provenance:** rules and fee sources, latency fit source

---

## 8) Validation Gates (What “step” you are at)

Helix is designed around **gates**. Passing a gate means the engine has a specific correctness property.

* **Gate 0:** Clean build & tests enumerate (`cmake ... && ctest -N`)
* **Gate 1:** Replay invariants are fatal (negative qty / seq gap / crossed book must exit non-zero).
* **Gate 2:** Matching conservation & depth behavior (partial fills, FOK rejects, empty-side rejects, fuzz tests).
* **Gate 3:** Determinism utilities (stable hashing, stable latency seeding).
* **Gate 4:** Latency causality (decision at `t`, fixed latency `L`, fill happens at `t+L` on the later book).
* **Gate 5:** Crossing classification (crossing limit order must be equivalent to market taker).
* **Gate 6:** PnL bookkeeping identity (`net_total == realized + unrealized - fees` always).
* **Gate 7:** Rules & Fees are configurable and enforced (normalization, min_notional, fee bps).
* **Gate 8:** Lifecycle correctness (terminal orders can’t fill, cancel/replace/expire semantics).
* **Gate 9:** End-to-end mini tape in CI.
* Run engine on short L2+trades “mini tape”.
* Run `gate9_check.py` to enforce: identity_ok, trade skew, fee bps, adv-selection samples, and determinism.



---

## 9) Troubleshooting (Common Failures)

**`best_bid/best_ask invalid` (fatal)**

> Your replay input is broken (crossed book, missing side, inconsistent seq).
> **Fix:** Fix the recorder, not the engine.

**`ZeroAfterRounding` rejects**

> Rules normalization floored your qty to zero.
> **Fix:** Increase notional or adjust step size in rules config.

**`adv missing` (fatal)**

> Your maker fill happened too near end-of-replay to collect horizon sample.
> **Fix:** Extend replay window, increase trade coverage, or set `--adv_fatal_missing 0` (debug only).

**`Bookcheck mismatch`**

> Recorder and replay don’t agree on book reconstruction.
> **Fix:** Treat as serious; backtest is untrusted until fixed.

---

## 10) Extending Helix

1. **Add a new venue rules profile:**
* Add to `venue_rules.yaml`.
* Ensure `RulesEngine` handles tick/step/min_notional and fee currency/rounding.
* Add tests for normalization and rejects.


2. **Add a new metric:**
* Compute from structured fills / aggregates.
* Write to `metrics.json`.
* Add a unit test or gate check (do not rely on eyeballing logs).


3. **Add a new gate:**
* Write a synthetic test in `cpp_engine/tests/`.
* Make it deterministic and small.
* Wire into CMake/CTest.



---


```

Would you like me to generate the **Chinese version** of this README as well?

```
