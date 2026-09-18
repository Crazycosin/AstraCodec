#!/usr/bin/env bash
set -euo pipefail
astra_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$astra_root"
astra_preset="${1:-debug}"
case "$astra_preset" in debug|release|asan|tsan) ;; *) printf '未知 preset\n' >&2; exit 1 ;; esac
astra_program="$astra_root/build/$astra_preset/astracodec"
test -x "$astra_program"
cmake "-DASTRA_PROGRAM=$astra_program" -DASTRA_MODE=version -P cmake/VerifyCli.cmake
cmake "-DASTRA_PROGRAM=$astra_program" -DASTRA_MODE=help -P cmake/VerifyCli.cmake
"$astra_program" --version
"$astra_program" --help
