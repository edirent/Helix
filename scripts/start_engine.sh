#!/usr/bin/env bash
set -euo pipefail

cmake -S cpp_engine -B cpp_engine/build
cmake --build cpp_engine/build
./cpp_engine/build/helix_engine_main "$@"
