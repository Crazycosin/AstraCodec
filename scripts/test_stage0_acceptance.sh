#!/usr/bin/env bash
set -euo pipefail

astra_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
astra_commit="$(git -C "$astra_root" rev-parse HEAD)"
astra_test_parent="$astra_root/build/stage0-acceptance-tests"
mkdir -p "$astra_test_parent"
astra_run_root="$(mktemp -d "$astra_test_parent/run.XXXXXXXX")"
mkdir -p \
  "$astra_run_root/targets" \
  "$astra_run_root/success-evidence" \
  "$astra_run_root/failure-evidence" \
  "$astra_run_root/preflight-evidence"

outer_report() {
  local astra_parent="$1"
  local -a astra_directories=()
  shopt -s nullglob
  astra_directories=("$astra_parent"/stage0-acceptance.*)
  shopt -u nullglob
  (( ${#astra_directories[@]} == 1 )) || return 1
  [[ -f "${astra_directories[0]}/report.json" ]] || return 1
  printf '%s\n' "${astra_directories[0]}/report.json"
}

astra_success_target="$astra_run_root/targets/astracodec-stage0-success"
astra_success_output="$astra_run_root/success.out"
bash "$astra_root/scripts/run_stage0_acceptance.sh" \
  "$astra_root" \
  "$astra_commit" \
  "$astra_success_target" \
  "$astra_run_root/success-evidence" >"$astra_success_output"
astra_success_report="$(outer_report "$astra_run_root/success-evidence")"
astra_success_evidence="${astra_success_report%/report.json}"
[[ "$(tail -n 1 "$astra_success_output")" == "$astra_success_evidence" ]]
jq -e --arg commit "$astra_commit" \
  '.status == "passed" and .exit_code == 0 and .within_limit == true and
   .repository.expected_commit == $commit and
   .repository.resolved_commit == $commit and
   .clone_to_smoke_seconds >= 0 and .clone_to_smoke_seconds <= 900 and
   .finished_epoch >= .report_generation.started_epoch and
   .report_generation.finished_epoch == .finished_epoch and
   .report_generation.elapsed_seconds >= 0 and
   .script_elapsed_seconds == (.finished_epoch - .process_started_epoch) and
   .bootstrap_report == "bootstrap-evidence/report.json" and
   ([.stages[].name] ==
     ["environment", "clone", "checkout", "verify_checkout", "bootstrap"])' \
  "$astra_success_report" >/dev/null
jq -e --arg commit "$astra_commit" \
  '.status == "passed" and .exit_code == 0 and .commit == $commit and
   (.stages | map(select(.name == "smoke" and .exit_code == 0)) |
    length == 1)' \
  "$astra_success_evidence/bootstrap-evidence/report.json" >/dev/null
[[ "$(git -C "$astra_success_target" rev-parse HEAD)" == "$astra_commit" ]]
[[ -z "$(git -C "$astra_success_target" status --porcelain \
  --untracked-files=all)" ]]

astra_failure_target="$astra_run_root/targets/astracodec-stage0-failure"
astra_failure_exit=0
bash "$astra_root/scripts/run_stage0_acceptance.sh" \
  "$astra_root" \
  0000000000000000000000000000000000000000 \
  "$astra_failure_target" \
  "$astra_run_root/failure-evidence" \
  >"$astra_run_root/failure.out" 2>"$astra_run_root/failure.err" ||
  astra_failure_exit=$?
(( astra_failure_exit != 0 ))
astra_failure_report="$(outer_report "$astra_run_root/failure-evidence")"
jq -e \
  '.status == "failed" and .exit_code != 0 and
   .repository.resolved_commit == "" and .bootstrap_report == null and
   .finished_epoch >= .report_generation.started_epoch and
   .report_generation.finished_epoch == .finished_epoch and
   .script_elapsed_seconds == (.finished_epoch - .process_started_epoch) and
   .failure_reason != "" and
   ([.stages[].name] == ["environment", "clone", "checkout"]) and
   (.stages[-1].exit_code != 0)' \
  "$astra_failure_report" >/dev/null

astra_preflight_target="$astra_run_root/targets/astracodec-stage0-existing"
mkdir "$astra_preflight_target"
printf '%s\n' preserve >"$astra_preflight_target/preserve.txt"
astra_preflight_exit=0
bash "$astra_root/scripts/run_stage0_acceptance.sh" \
  "$astra_root" \
  "$astra_commit" \
  "$astra_preflight_target" \
  "$astra_run_root/preflight-evidence" \
  >"$astra_run_root/preflight.out" 2>"$astra_run_root/preflight.err" ||
  astra_preflight_exit=$?
(( astra_preflight_exit == 2 ))
astra_preflight_report="$(outer_report "$astra_run_root/preflight-evidence")"
jq -e \
  '.status == "failed" and .exit_code == 2 and
   .failure_reason == "目标目录已经存在" and .bootstrap_report == null and
   .clone_started_epoch == null and .stages == [] and
   .report_generation.finished_epoch == .finished_epoch' \
  "$astra_preflight_report" >/dev/null
[[ "$(<"$astra_preflight_target/preserve.txt")" == preserve ]]

printf '%s\n' "$astra_run_root"
