// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#ifndef ASTRACODEC_CORE_BASE_SAMPLE_MANIFEST_H_
#define ASTRACODEC_CORE_BASE_SAMPLE_MANIFEST_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace astra {

struct Rational {
  int numerator;
  int denominator;
};

struct SampleResolution {
  int width;
  int height;
};

struct AudioSampleInfo {
  std::string codec;
  int sample_rate;
  int channels;
};

struct SampleSourceInfo {
  std::string method;
  std::string description;
  std::string license;
  std::string generation_command;
  std::string tool_version;
};

struct SampleInfo {
  std::string id;
  std::filesystem::path filename;
  std::string codec;
  std::string container;
  SampleResolution resolution;
  Rational fps;
  double duration_seconds;
  std::string sha256;
  std::string pixel_format;
  std::int64_t video_frame_count;
  Rational time_base;
  bool has_b_frames;
  std::optional<AudioSampleInfo> audio;
  SampleSourceInfo source;
};

struct SampleManifest {
  int schema_version;
  std::vector<SampleInfo> samples;
};

// 加载并验证 schema 1；文件最多 1 MiB，最多 64 个样本。
// 语法、字段、类型和范围错误立即 Crash，调用支持并发且没有共享状态。
[[nodiscard]] SampleManifest LoadSampleManifest(
    const std::filesystem::path& manifest_path);

// 通过集中资源路径逐个读取实际文件并比较 SHA256；文件最多 1 GiB。
// 本接口验证文件字节，媒体参数和完整解码由独立 FFmpeg 检查提供。
void VerifySampleHashes(const SampleManifest& manifest,
                        const std::filesystem::path& sample_root);

}  // namespace astra

#endif  // ASTRACODEC_CORE_BASE_SAMPLE_MANIFEST_H_
