#!/usr/bin/env bash
set -euo pipefail

astra_process_started_epoch="$(date +%s)"

if (( $# < 4 || $# > 5 )); then
  printf '用法：%s <repo_url> <40位commit> <新目标绝对路径> <证据父目录绝对路径> [fetch_ref]\n' "$0" >&2
  exit 2
fi

astra_repo_url="$1"
astra_expected_commit="$2"
astra_target="$3"
astra_evidence_parent="$4"
astra_fetch_ref="${5:-}"

if [[ "$astra_evidence_parent" != /* || ! -d "$astra_evidence_parent" ||
      -L "$astra_evidence_parent" ]]; then
  printf '证据父目录必须是已有的普通绝对路径\n' >&2
  exit 2
fi
astra_evidence_parent="$(cd "$astra_evidence_parent" && pwd -P)"
umask 077
astra_evidence="$(mktemp -d "$astra_evidence_parent/stage0-acceptance.XXXXXXXX")"
: >"$astra_evidence/stages.jsonl"
: >"$astra_evidence/preflight.log"

astra_clone_started_epoch=null
astra_smoke_finished_epoch=null
astra_clone_to_smoke_seconds=null
astra_resolved_commit=""
astra_active_stage=""
astra_active_started_epoch=""
astra_failure_reason=""
astra_bootstrap_started=false

record_stage() {
  local astra_name="$1"
  local astra_started_epoch="$2"
  local astra_finished_epoch="$3"
  local astra_exit_code="$4"
  jq -n --arg name "$astra_name" \
    --argjson started_epoch "$astra_started_epoch" \
    --argjson finished_epoch "$astra_finished_epoch" \
    --argjson exit_code "$astra_exit_code" \
    '{name: $name, started_epoch: $started_epoch,
      finished_epoch: $finished_epoch,
      elapsed_seconds: ($finished_epoch - $started_epoch),
      exit_code: $exit_code}' >>"$astra_evidence/stages.jsonl"
}

capture_bootstrap_evidence() {
  local -a astra_candidates=()
  local -a astra_matches=()
  local astra_candidate
  shopt -s nullglob
  astra_candidates=(
    "$astra_target"/build/debug/test-output/stage0/bootstrap.*/report.json)
  shopt -u nullglob
  for astra_candidate in "${astra_candidates[@]}"; do
    if jq -e --arg commit "$astra_expected_commit" \
      '.commit == $commit and .status == "passed" and .exit_code == 0 and
       (.stages | map(select(.name == "smoke" and .exit_code == 0)) |
        length == 1)' "$astra_candidate" >/dev/null; then
      astra_matches+=("$astra_candidate")
    fi
  done
  (( ${#astra_matches[@]} == 1 )) || return 1
  cp -R "${astra_matches[0]%/report.json}" \
    "$astra_evidence/bootstrap-evidence"
}

finish() {
  local astra_exit_code="$?"
  local astra_finished_epoch
  local astra_report_body
  local astra_report_started_epoch
  local astra_status=failed
  local astra_within_limit=false
  local astra_bootstrap_report=null
  trap - EXIT INT TERM HUP

  if [[ -n "$astra_active_stage" ]]; then
    record_stage "$astra_active_stage" "$astra_active_started_epoch" \
      "$(date +%s)" "$astra_exit_code"
  fi
  if [[ "$astra_bootstrap_started" == true ]] &&
      ! capture_bootstrap_evidence && (( astra_exit_code == 0 )); then
    astra_exit_code=1
    astra_failure_reason='复制 bootstrap 证据失败'
  fi
  if [[ -f "$astra_evidence/bootstrap-evidence/report.json" ]]; then
    astra_bootstrap_report=bootstrap-evidence/report.json
    local astra_report="$astra_evidence/$astra_bootstrap_report"
    local astra_inner_commit
    astra_inner_commit="$(jq -er '.commit' "$astra_report")" || true
    if [[ "$astra_inner_commit" == "$astra_expected_commit" ]] &&
        jq -e '.status == "passed" and .exit_code == 0' \
          "$astra_report" >/dev/null; then
      astra_smoke_finished_epoch="$(jq -er \
        '.stages | map(select(.name == "smoke" and .exit_code == 0)) |
          last | .finished_epoch' "$astra_report")" || true
    fi
  fi
  if [[ "$astra_clone_started_epoch" != null &&
        "$astra_smoke_finished_epoch" =~ ^[0-9]+$ ]]; then
    astra_clone_to_smoke_seconds=$((
      astra_smoke_finished_epoch - astra_clone_started_epoch))
    if (( astra_clone_to_smoke_seconds >= 0 &&
          astra_clone_to_smoke_seconds <= 900 )); then
      astra_within_limit=true
    fi
  fi
  if (( astra_exit_code == 0 )) && [[ "$astra_within_limit" == true ]]; then
    astra_status=passed
  else
    if (( astra_exit_code == 0 )); then
      astra_exit_code=1
      astra_failure_reason='完整链超过 900 秒或未取得成功的 smoke 证据'
    fi
  fi
  astra_report_started_epoch="$(date +%s)"
  if astra_report_body="$(jq -s --arg repo_url "$astra_repo_url" \
      --arg expected_commit "$astra_expected_commit" \
      --arg resolved_commit "$astra_resolved_commit" \
      --arg fetch_ref "$astra_fetch_ref" \
      --arg target "$astra_target" \
      --arg evidence_dir "$astra_evidence" \
      --arg bootstrap_report "$astra_bootstrap_report" \
      --arg failure_reason "$astra_failure_reason" \
      --arg status "$astra_status" \
      --argjson process_started_epoch "$astra_process_started_epoch" \
      --argjson clone_started_epoch "$astra_clone_started_epoch" \
      --argjson smoke_finished_epoch "$astra_smoke_finished_epoch" \
      --argjson report_started_epoch "$astra_report_started_epoch" \
      --argjson clone_to_smoke_seconds "$astra_clone_to_smoke_seconds" \
      --argjson within_limit "$astra_within_limit" \
      --argjson exit_code "$astra_exit_code" \
      '{schema_version: 1, repository: {url: $repo_url,
        expected_commit: $expected_commit, resolved_commit: $resolved_commit,
        fetch_ref: $fetch_ref}, target: $target, evidence_dir: $evidence_dir,
        process_started_epoch: $process_started_epoch,
        clone_started_epoch: $clone_started_epoch,
        smoke_finished_epoch: $smoke_finished_epoch,
        finished_epoch: null,
        clone_to_smoke_seconds: $clone_to_smoke_seconds,
        script_elapsed_seconds: null,
        report_generation: {started_epoch: $report_started_epoch,
          finished_epoch: null, elapsed_seconds: null},
        limit_seconds: 900, within_limit: $within_limit,
        bootstrap_report: (if $bootstrap_report == "null" then null
          else $bootstrap_report end), stages: ., status: $status,
        exit_code: $exit_code, failure_reason: $failure_reason}' \
      "$astra_evidence/stages.jsonl")"; then
    astra_finished_epoch="$(date +%s)"
    if ! jq --argjson finished_epoch "$astra_finished_epoch" \
      '.finished_epoch = $finished_epoch |
       .script_elapsed_seconds =
         ($finished_epoch - .process_started_epoch) |
       .report_generation.finished_epoch = $finished_epoch |
       .report_generation.elapsed_seconds =
         ($finished_epoch - .report_generation.started_epoch)' \
      <<<"$astra_report_body" >"$astra_evidence/report.json"; then
      astra_exit_code=1
      printf '写入外层报告失败；证据目录：%s\n' "$astra_evidence" >&2
    fi
  else
    astra_exit_code=1
    printf '生成外层报告失败；证据目录：%s\n' "$astra_evidence" >&2
  fi
  if (( astra_exit_code == 0 )); then
    printf '%s\n' "$astra_evidence"
  else
    printf '外层验收失败；证据目录：%s\n' "$astra_evidence" >&2
  fi
  exit "$astra_exit_code"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

fail_preflight() {
  astra_failure_reason="$1"
  printf '%s\n' "$astra_failure_reason" >>"$astra_evidence/preflight.log"
  exit 2
}

[[ "$astra_expected_commit" =~ ^[0-9a-f]{40}$ ]] ||
  fail_preflight '必须提供完整的小写 commit SHA-1'
[[ "$astra_target" == /* ]] || fail_preflight '目标必须使用绝对路径'
astra_target_name="${astra_target##*/}"
[[ "$astra_target_name" =~ ^astracodec-stage0-[A-Za-z0-9][A-Za-z0-9._-]*$ ]] ||
  fail_preflight '目标目录名称必须以 astracodec-stage0- 开头'
astra_target_parent="${astra_target%/*}"
[[ -d "$astra_target_parent" && ! -L "$astra_target_parent" ]] ||
  fail_preflight '目标父目录必须已经存在且不能是符号链接'
astra_target_parent="$(cd "$astra_target_parent" && pwd -P)"
[[ "$astra_target" == "$astra_target_parent/$astra_target_name" ]] ||
  fail_preflight '目标路径必须是规范绝对路径'
[[ ! -e "$astra_target" && ! -L "$astra_target" ]] ||
  fail_preflight '目标目录已经存在'
if [[ -n "$astra_fetch_ref" ]]; then
  [[ "$astra_fetch_ref" =~ ^refs/[A-Za-z0-9][A-Za-z0-9._/-]*$ ]] ||
    fail_preflight 'fetch_ref 必须是完整 Git ref'
fi
case "$astra_repo_url" in
  /*)
    [[ -d "$astra_repo_url" ]] || fail_preflight '本地仓库目录不存在'
    ;;
  https://*|ssh://*|git@*:*) ;;
  *) fail_preflight 'repo_url 必须是本地绝对路径或受支持的 Git 地址' ;;
esac
if [[ "$astra_repo_url" == https://* ]]; then
  [[ ! "$astra_repo_url" =~ ^https://[^/]*@ &&
      "$astra_repo_url" != *\?* && "$astra_repo_url" != *\#* ]] ||
    fail_preflight 'HTTPS 地址不得包含凭据、查询参数或片段'
fi
for astra_tool in git jq cmake ninja pkg-config ffmpeg ffprobe c++; do
  command -v "$astra_tool" >/dev/null ||
    fail_preflight "缺少命令：$astra_tool"
done

record_environment() {
  {
    printf '%s\n' 'uname:'
    uname -a
    printf '%s\n' 'git:'
    git --version
    printf '%s\n' 'cmake:'
    cmake --version
    printf '%s\n' 'ninja:'
    ninja --version
    printf '%s\n' 'compiler:'
    c++ --version
    printf '%s\n' 'pkg-config:'
    pkg-config --version
    pkg-config --modversion libavformat libavcodec libavutil fmt
    printf '%s\n' 'ffmpeg:'
    ffmpeg -version
    printf '%s\n' 'ffprobe:'
    ffprobe -version
    printf '%s\n' 'jq:'
    jq --version
  }
}

run_stage() {
  local astra_name="$1"
  shift
  local astra_exit_code=0
  local astra_finished_epoch
  astra_active_stage="$astra_name"
  astra_active_started_epoch="$(date +%s)"
  "$@" >"$astra_evidence/$astra_name.log" 2>&1 || astra_exit_code=$?
  astra_finished_epoch="$(date +%s)"
  record_stage "$astra_name" "$astra_active_started_epoch" \
    "$astra_finished_epoch" "$astra_exit_code"
  astra_active_stage=""
  if (( astra_exit_code != 0 )); then
    astra_failure_reason="$astra_name 退出码 $astra_exit_code"
    exit "$astra_exit_code"
  fi
}

verify_checkout() {
  local astra_actual_commit
  astra_actual_commit="$(git -C "$astra_target" rev-parse HEAD)"
  [[ "$astra_actual_commit" == "$astra_expected_commit" ]] || return 1
  [[ -z "$(git -C "$astra_target" status --porcelain --untracked-files=all)" ]]
}

run_stage environment record_environment
astra_clone_started_epoch="$(date +%s)"
run_stage clone git clone --no-local --no-checkout -- \
  "$astra_repo_url" "$astra_target"
if [[ -n "$astra_fetch_ref" ]]; then
  run_stage fetch git -C "$astra_target" fetch --no-tags origin "$astra_fetch_ref"
fi
run_stage checkout git -C "$astra_target" checkout --detach \
  "$astra_expected_commit"
run_stage verify_checkout verify_checkout
astra_resolved_commit="$astra_expected_commit"
astra_bootstrap_started=true
run_stage bootstrap bash "$astra_target/scripts/bootstrap.sh" debug
