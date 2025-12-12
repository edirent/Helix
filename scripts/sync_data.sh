#!/usr/bin/env bash
set -euo pipefail

# Placeholder for downloading or syncing replay data.
mkdir -p data/replay
echo "timestamp,bid,ask,bid_size,ask_size" > data/replay/synthetic.csv
echo "synthetic replay seed written to data/replay/synthetic.csv"
