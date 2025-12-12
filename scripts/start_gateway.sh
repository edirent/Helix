#!/usr/bin/env bash
set -euo pipefail

go run ./gateway/cmd/gateway "$@"
