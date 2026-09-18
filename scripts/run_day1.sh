#!/usr/bin/env bash
set -euo pipefail
astra_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$astra_root"
astra_preset="${1:-debug}"
case "$astra_preset" in debug|release|asan|tsan) ;; *) printf '未知 preset\n' >&2; exit 1 ;; esac
cmake --preset "$astra_preset"
cmake --build --preset "$astra_preset" --parallel "${ASTRA_BUILD_JOBS:-4}"
ctest --preset "$astra_preset" --output-on-failure
bash scripts/run_smoke.sh "$astra_preset"
