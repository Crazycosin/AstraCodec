#!/usr/bin/env bash
set -euo pipefail

astra_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$astra_root"
astra_preset="${1:-debug}"
case "$astra_preset" in
  debug|release|asan|tsan) ;;
  *) printf '未知 preset\n' >&2; exit 1 ;;
esac
astra_jobs="${ASTRA_BUILD_JOBS:-4}"
if [[ ! "$astra_jobs" =~ ^[1-9][0-9]*$ ]]; then
  printf 'ASTRA_BUILD_JOBS 必须为正整数\n' >&2
  exit 1
fi
if ! command -v jq >/dev/null; then
  printf '缺少命令：jq，无法生成 JSON 证据\n' >&2
  exit 1
fi

astra_evidence_parent="$astra_root/build/$astra_preset/test-output/stage0"
umask 077
mkdir -p "$astra_evidence_parent"
astra_evidence="$(mktemp -d "$astra_evidence_parent/bootstrap.XXXXXXXX")"
: >"$astra_evidence/stages.jsonl"
: >"$astra_evidence/preflight.log"
astra_started_epoch="$(date +%s)"
astra_started_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
astra_commit=""

write_report() {
  local astra_status="$1"
  local astra_exit_code="$2"
  local astra_finished_epoch
  local astra_finished_at
  astra_finished_epoch="$(date +%s)"
  astra_finished_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  jq -s --arg preset "$astra_preset" --arg started_at "$astra_started_at" \
    --arg finished_at "$astra_finished_at" --arg commit "$astra_commit" \
    --arg status "$astra_status" --argjson exit_code "$astra_exit_code" \
    --argjson started_epoch "$astra_started_epoch" \
    --argjson finished_epoch "$astra_finished_epoch" \
    '{schema_version: 1, preset: $preset, commit: $commit,
      started_at: $started_at, finished_at: $finished_at,
      elapsed_seconds: ($finished_epoch - $started_epoch), stages: .,
      status: $status, exit_code: $exit_code}' \
    "$astra_evidence/stages.jsonl" >"$astra_evidence/report.json"
}

fail_preflight() {
  local astra_reason="$1"
  local astra_finished_epoch
  astra_finished_epoch="$(date +%s)"
  printf '%s\n' "$astra_reason" >>"$astra_evidence/preflight.log"
  jq -n --arg name preflight \
    --argjson started_epoch "$astra_started_epoch" \
    --argjson finished_epoch "$astra_finished_epoch" \
    '{name: $name, started_epoch: $started_epoch,
      finished_epoch: $finished_epoch,
      elapsed_seconds: ($finished_epoch - $started_epoch), exit_code: 1}' \
    >>"$astra_evidence/stages.jsonl"
  write_report failed 1
  printf '%s；证据目录：%s\n' "$astra_reason" "$astra_evidence" >&2
  exit 1
}

for astra_tool in cmake ninja git pkg-config ffmpeg ffprobe; do
  if ! command -v "$astra_tool" >/dev/null; then
    fail_preflight "缺少命令：$astra_tool"
  fi
done
for astra_package in libavformat libavcodec libavutil 'fmt >= 9'; do
  if ! pkg-config --exists "$astra_package"; then
    fail_preflight "pkg-config 依赖不可用：$astra_package"
  fi
done
if ! git rev-parse HEAD >"$astra_evidence/git-commit.txt" \
    2>>"$astra_evidence/preflight.log"; then
  fail_preflight '获取 Git commit 失败'
fi
astra_commit="$(<"$astra_evidence/git-commit.txt")"
if ! git status --short >"$astra_evidence/git-status.txt" \
    2>>"$astra_evidence/preflight.log"; then
  fail_preflight '查询 Git 状态失败'
fi
if ! cmake --version >"$astra_evidence/cmake-version.txt" \
    2>>"$astra_evidence/preflight.log"; then
  fail_preflight '查询 CMake 版本失败'
fi
if ! ninja --version >"$astra_evidence/ninja-version.txt" \
    2>>"$astra_evidence/preflight.log"; then
  fail_preflight '查询 Ninja 版本失败'
fi
if ! pkg-config --modversion libavformat libavcodec libavutil fmt \
    >"$astra_evidence/dependency-versions.txt" \
    2>>"$astra_evidence/preflight.log"; then
  fail_preflight '查询依赖版本失败'
fi

run_stage() {
  local astra_name="$1"
  shift
  local astra_stage_started
  local astra_stage_finished
  local astra_stage_status=0
  astra_stage_started="$(date +%s)"
  "$@" >"$astra_evidence/$astra_name.log" 2>&1 || astra_stage_status=$?
  astra_stage_finished="$(date +%s)"
  jq -n --arg name "$astra_name" \
    --argjson started_epoch "$astra_stage_started" \
    --argjson finished_epoch "$astra_stage_finished" \
    --argjson exit_code "$astra_stage_status" \
    '{name: $name, started_epoch: $started_epoch,
      finished_epoch: $finished_epoch,
      elapsed_seconds: ($finished_epoch - $started_epoch),
      exit_code: $exit_code}' \
    >>"$astra_evidence/stages.jsonl"
  if ((astra_stage_status != 0)); then
    write_report failed "$astra_stage_status"
    printf '%s 失败，退出码 %s；证据目录：%s\n' \
      "$astra_name" "$astra_stage_status" "$astra_evidence" >&2
    exit "$astra_stage_status"
  fi
}

run_stage configure cmake --preset "$astra_preset"
run_stage build cmake --build --preset "$astra_preset" --parallel "$astra_jobs"
run_stage ctest ctest --preset "$astra_preset" --output-on-failure
run_stage sample-verification env \
  "ASTRA_SAMPLE_EVIDENCE_PARENT=$astra_evidence/sample-assets" \
  bash scripts/verify_samples.sh \
  "$astra_root/build/$astra_preset/astracodec_verify_samples" "$astra_root"
run_stage smoke bash scripts/run_smoke.sh "$astra_preset"

write_report passed 0
jq -e '.status == "passed" and .exit_code == 0 and
  (.stages | length == 5) and
  (.stages | all(.exit_code == 0))' "$astra_evidence/report.json" >/dev/null
printf '%s\n' "$astra_evidence"
