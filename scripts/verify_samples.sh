#!/usr/bin/env bash
set -euo pipefail

astra_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
if [[ "$#" -ne 2 && "$#" -ne 3 ]]; then
  printf '用法：verify_samples.sh <verifier> <runtime_root> [config_path]\n' >&2
  exit 1
fi
astra_program="$1"
astra_runtime_root="$(cd "$2" && pwd -P)"
astra_config_path="${3:-}"
test -x "$astra_program"
for astra_tool in cmake ffmpeg ffprobe jq; do
  command -v "$astra_tool" >/dev/null
done

astra_evidence_parent="${ASTRA_SAMPLE_EVIDENCE_PARENT:-$astra_root/build/sample-verification}"
case "$astra_evidence_parent/" in
  "$astra_root/build/"*) ;;
  *) printf '样本证据目录必须位于项目 build 目录\n' >&2; exit 1 ;;
esac
case "/$astra_evidence_parent/" in
  *'/../'*|*'/./'*) printf '样本证据目录包含非法路径组件\n' >&2; exit 1 ;;
esac
mkdir -p "$astra_evidence_parent"
astra_evidence_parent="$(cd "$astra_evidence_parent" && pwd -P)"
case "$astra_evidence_parent/" in
  "$astra_root/build/"*) ;;
  *) printf '样本证据目录超出项目 build 目录\n' >&2; exit 1 ;;
esac
astra_evidence="$(mktemp -d "$astra_evidence_parent/run.XXXXXXXX")"
umask 077
ffmpeg -version >"$astra_evidence/ffmpeg-version.txt"
ffprobe -version >"$astra_evidence/ffprobe-version.txt"
jq --version >"$astra_evidence/jq-version.txt"

astra_verifier_arguments=("$astra_runtime_root")
if [[ -n "$astra_config_path" ]]; then
  astra_verifier_arguments+=("$astra_config_path")
fi
"$astra_program" "${astra_verifier_arguments[@]}" \
  >"$astra_evidence/verifier.json" \
  2>"$astra_evidence/verifier.stderr.jsonl"
jq -e '
  .schema_version == 1 and
  (.manifest_path | type == "string") and
  (.sample_root | type == "string") and
  (.verified_samples | type == "array" and length == 3)
' "$astra_evidence/verifier.json" >/dev/null

astra_manifest_path="$(jq -er '.manifest_path' "$astra_evidence/verifier.json")"
astra_sample_root="$(jq -er '.sample_root' "$astra_evidence/verifier.json")"
test -f "$astra_manifest_path"
test -d "$astra_sample_root"
astra_sample_count="$(jq -er '.samples | length' "$astra_manifest_path")"
test "$astra_sample_count" -eq 3
jq -e '[.samples[].id] | sort == ["S01", "S02", "S03"]' \
  "$astra_manifest_path" >/dev/null
: >"$astra_evidence/samples.jsonl"

astra_index=0
while IFS= read -r astra_sample; do
  astra_id="$(jq -er '.id' <<<"$astra_sample")"
  astra_filename="$(jq -er '.filename' <<<"$astra_sample")"
  astra_expected_hash="$(jq -er '.sha256' <<<"$astra_sample")"
  astra_file="$astra_sample_root/$astra_filename"
  test -f "$astra_file"

  astra_prefix="$astra_evidence/sample-$astra_index"
  cmake -E sha256sum "$astra_file" >"$astra_prefix.sha256"
  astra_actual_hash="$(awk '{print $1}' "$astra_prefix.sha256")"
  test "$astra_actual_hash" = "$astra_expected_hash"

  ffprobe -v error -count_frames -show_streams -show_format -of json \
    "$astra_file" >"$astra_prefix.probe.json"
  ffprobe -v error -show_frames -show_entries \
    frame=media_type,pts,best_effort_timestamp,pict_type \
    -of json "$astra_file" >"$astra_prefix.frames.json"

  jq -e --argjson expected "$astra_sample" '
    ([.streams[] | select(.codec_type == "video")]) as $video |
    ([.streams[] | select(.codec_type == "audio")]) as $audio |
    ($video | length == 1) and
    ($video[0].codec_name == $expected.codec) and
    ($video[0].width == $expected.resolution.width) and
    ($video[0].height == $expected.resolution.height) and
    ($video[0].pix_fmt == $expected.pixel_format) and
    ($video[0].field_order == "progressive") and
    ($video[0].r_frame_rate ==
      (($expected.fps.numerator | tostring) + "/" +
       ($expected.fps.denominator | tostring))) and
    ($video[0].time_base ==
      (($expected.time_base.numerator | tostring) + "/" +
       ($expected.time_base.denominator | tostring))) and
    (($video[0].nb_read_frames | tonumber) == $expected.video_frame_count) and
    ((.format.format_name | split(",") | index($expected.container)) != null) and
    (((.format.duration | tonumber) - $expected.duration_seconds) | fabs < 0.001) and
    (if $expected.audio == null then
       ($audio | length == 0)
     else
       ($audio | length == 1) and
       ($audio[0].codec_name == $expected.audio.codec) and
       (($audio[0].sample_rate | tonumber) == $expected.audio.sample_rate) and
       ($audio[0].channels == $expected.audio.channels)
     end)
  ' "$astra_prefix.probe.json" >/dev/null

  jq -e --argjson expected "$astra_sample" '
    ([.frames[] | select(.media_type == "video")]) as $video |
    ([.frames[] | select(.media_type == "audio")]) as $audio |
    ($video | length == $expected.video_frame_count) and
    (if $expected.audio == null then
       ($audio | length == 0)
     else
       ($audio | length > 0)
     end) and
    (([$video[] | select(.pict_type == "B")] | length > 0) ==
      $expected.has_b_frames) and
    (($expected.time_base.denominator * $expected.fps.denominator) %
      ($expected.time_base.numerator * $expected.fps.numerator) == 0) and
    (($expected.time_base.denominator * $expected.fps.denominator) /
      ($expected.time_base.numerator * $expected.fps.numerator)) as $step |
    ([range(1; $video | length) |
       (($video[.].best_effort_timestamp // $video[.].pts) | tonumber) -
       (($video[. - 1].best_effort_timestamp // $video[. - 1].pts) | tonumber)] |
      all(. == $step))
  ' "$astra_prefix.frames.json" >/dev/null

  ffmpeg -v error -xerror -i "$astra_file" -map 0 -f null - \
    >"$astra_prefix.decode.stdout" 2>"$astra_prefix.decode.stderr"
  jq -n --arg id "$astra_id" --arg filename "$astra_filename" \
    --arg sha256 "$astra_actual_hash" \
    --argjson bytes "$(wc -c <"$astra_file")" \
    '{id: $id, filename: $filename, sha256: $sha256, bytes: $bytes,
      probe: "passed", cfr: "passed", complete_decode: "passed"}' \
    >>"$astra_evidence/samples.jsonl"
  astra_index=$((astra_index + 1))
done < <(jq -c '.samples[]' "$astra_manifest_path")
test "$astra_index" -eq "$astra_sample_count"

jq -s --arg completed_at "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" \
  --arg manifest_path "$astra_manifest_path" \
  '{schema_version: 1, completed_at: $completed_at,
    manifest_path: $manifest_path, samples: .}' \
  "$astra_evidence/samples.jsonl" >"$astra_evidence/report.json"
jq -e '.samples as $samples | ($samples | length == 3) and
  ($samples | all(.probe == "passed" and .cfr == "passed" and
    .complete_decode == "passed"))' \
  "$astra_evidence/report.json" >/dev/null
printf '%s\n' "$astra_evidence"
