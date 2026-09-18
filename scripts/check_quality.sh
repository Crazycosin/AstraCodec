#!/usr/bin/env bash
set -euo pipefail
astra_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$astra_root"
astra_format="${ASTRA_CLANG_FORMAT:-clang-format}"
astra_tidy="${ASTRA_CLANG_TIDY:-clang-tidy}"
astra_preset="${1:-debug}"
case "$astra_preset" in debug|release|asan|tsan) ;; *) printf '未知 preset\n' >&2; exit 1 ;; esac
test -f "build/$astra_preset/compile_commands.json"
astra_files=()
astra_sources=()
while IFS= read -r astra_file; do
  astra_files+=("$astra_file")
  if [[ "$astra_file" == *.cc ]]; then astra_sources+=("$astra_file"); fi
done < <(rg --files core apps tests -g '*.h' -g '*.cc')
test "${#astra_sources[@]}" -gt 0
"$astra_format" --version
"$astra_format" --dry-run --Werror "${astra_files[@]}"
astra_extra_args=()
astra_fmt_include="$(pkg-config --variable=includedir fmt)"
test -d "$astra_fmt_include"
astra_extra_args+=(--extra-arg=-isystem "--extra-arg=$astra_fmt_include")
if [[ "$(uname -s)" == Darwin ]]; then
  astra_sdk="$(xcrun --show-sdk-path)"
  test -d "$astra_sdk/usr/include/c++/v1"
  astra_extra_args+=(--extra-arg=-isysroot "--extra-arg=$astra_sdk"
    --extra-arg=-isystem "--extra-arg=$astra_sdk/usr/include/c++/v1")
fi
"$astra_tidy" --version
"$astra_tidy" -p "build/$astra_preset" "${astra_sources[@]}" "${astra_extra_args[@]}"
