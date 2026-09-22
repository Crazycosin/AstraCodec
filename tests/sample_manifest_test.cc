// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/sample_manifest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/base/error.h"

namespace astra {
namespace {

constexpr std::string_view kS01Sha256 =
    "14cf27d368a81590a0bc59b9fe580beef0d29f9daf27eeeaf42cdad8864d9358";

std::filesystem::path NewTestPath(std::string_view name) {
  static std::atomic<unsigned int> sequence{0};
  const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::path(ASTRA_TEST_OUTPUT_ROOT) /
         ("sample-manifest-" + std::to_string(time) + "-" +
          std::to_string(sequence++) + "-" + std::string(name));
}

void WriteFile(const std::filesystem::path& path, std::string_view contents) {
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) {
    Crash(ErrorCode::kWriteFailure, "测试文件无法创建");
  }
  if (std::fwrite(contents.data(), 1, contents.size(), file) !=
      contents.size()) {
    Crash(ErrorCode::kWriteFailure, "测试文件无法写入");
  }
  if (std::fclose(file) != 0) {
    Crash(ErrorCode::kWriteFailure, "测试文件无法关闭");
  }
}

void AppendFile(const std::filesystem::path& path, std::string_view contents) {
  std::FILE* file = std::fopen(path.string().c_str(), "ab");
  if (file == nullptr) {
    Crash(ErrorCode::kWriteFailure, "测试文件无法打开");
  }
  if (std::fwrite(contents.data(), 1, contents.size(), file) !=
      contents.size()) {
    Crash(ErrorCode::kWriteFailure, "测试文件无法追加");
  }
  if (std::fclose(file) != 0) {
    Crash(ErrorCode::kWriteFailure, "测试文件无法关闭");
  }
}

nlohmann::json SampleJson(std::string filename = "s01_720p_h264.mp4",
                          std::string hash = std::string(kS01Sha256)) {
  return {{"id", "S01"},
          {"filename", std::move(filename)},
          {"codec", "h264"},
          {"container", "mp4"},
          {"resolution", {{"width", 1280}, {"height", 720}}},
          {"fps", {{"numerator", 30}, {"denominator", 1}}},
          {"duration_seconds", 3.0},
          {"sha256", std::move(hash)},
          {"pixel_format", "yuv420p"},
          {"video_frame_count", 90},
          {"time_base", {{"numerator", 1}, {"denominator", 30000}}},
          {"has_b_frames", false},
          {"audio", nullptr},
          {"source",
           {{"method", "ffmpeg_lavfi"},
            {"description",
             "FFmpeg testsrc2 生成的视频，编码参数固定，未使用外部媒体素材。"},
            {"license", "GPL-3.0-only; see repository LICENSE"},
            {"generation_command",
             "ffmpeg -hide_banner -n -f lavfi -i "
             "testsrc2=size=1280x720:rate=30:duration=3 -c:v libx264 "
             "-preset medium -crf 28 -pix_fmt yuv420p -g 30 -bf 0 "
             "-threads 1 -movflags +faststart -video_track_timescale 30000 "
             "-an samples/s01_720p_h264.mp4"},
            {"tool_version", "ffmpeg 9.0.1; libx264 core 165 r3222 b35605a"}}}};
}

nlohmann::json ManifestJson() {
  return {{"schema_version", 1},
          {"samples", nlohmann::json::array({SampleJson()})}};
}

std::filesystem::path WriteManifest(const nlohmann::json& document) {
  const auto path = NewTestPath("manifest.json");
  WriteFile(path, document.dump());
  return path;
}

void ExpectInvalidManifest(const nlohmann::json& document) {
  const auto path = WriteManifest(document);
  EXPECT_DEATH(
      {
        const auto manifest = LoadSampleManifest(path);
        (void)manifest;
      },
      "sample manifest");
}

TEST(SampleManifestTest, LoadsEveryTypedField) {
  const auto manifest = LoadSampleManifest(
      std::filesystem::path(ASTRA_SOURCE_ROOT) / "sample_manifest.json");

  ASSERT_EQ(manifest.schema_version, 1);
  ASSERT_EQ(manifest.samples.size(), 3);
  const auto& sample = manifest.samples.at(2);
  EXPECT_EQ(sample.id, "S03");
  EXPECT_EQ(sample.filename, "s03_bframes_audio.mp4");
  EXPECT_EQ(sample.codec, "h264");
  EXPECT_EQ(sample.container, "mp4");
  EXPECT_EQ(sample.resolution.width, 1280);
  EXPECT_EQ(sample.resolution.height, 720);
  EXPECT_EQ(sample.fps.numerator, 30000);
  EXPECT_EQ(sample.fps.denominator, 1001);
  EXPECT_DOUBLE_EQ(sample.duration_seconds, 5.005);
  EXPECT_EQ(sample.sha256,
            "601183e88b078862a9646e69f89740a3d86a51479ebf06d9349f9bfd29b18cdd");
  EXPECT_EQ(sample.pixel_format, "yuv420p");
  EXPECT_EQ(sample.video_frame_count, 150);
  EXPECT_EQ(sample.time_base.numerator, 1);
  EXPECT_EQ(sample.time_base.denominator, 30000);
  EXPECT_TRUE(sample.has_b_frames);
  ASSERT_TRUE(sample.audio.has_value());
  // ASSERT_TRUE 已检查该 optional，静态分析无法识别 GoogleTest 宏。
  const auto& audio =
      *sample.audio;  // NOLINT(bugprone-unchecked-optional-access)
  EXPECT_EQ(audio.codec, "aac");
  EXPECT_EQ(audio.sample_rate, 48000);
  EXPECT_EQ(audio.channels, 2);
  EXPECT_EQ(sample.source.method, "ffmpeg_lavfi");
  EXPECT_EQ(sample.source.license, "GPL-3.0-only; see repository LICENSE");
}

TEST(SampleManifestTest, AcceptsAnExplicitNullAudioField) {
  const auto manifest = LoadSampleManifest(WriteManifest(ManifestJson()));
  ASSERT_EQ(manifest.samples.size(), 1);
  EXPECT_FALSE(manifest.samples.front().audio.has_value());
}

TEST(SampleManifestTest, RejectsSchemaAndFieldSetViolations) {
  auto unsupported_schema = ManifestJson();
  unsupported_schema["schema_version"] = 2;
  ExpectInvalidManifest(unsupported_schema);

  auto missing_field = ManifestJson();
  missing_field["samples"][0].erase("codec");
  ExpectInvalidManifest(missing_field);

  auto unknown_field = ManifestJson();
  unknown_field["samples"][0]["unexpected"] = true;
  ExpectInvalidManifest(unknown_field);

  const auto duplicate_key = NewTestPath("duplicate-key.json");
  WriteFile(duplicate_key,
            R"({"schema_version":1,"schema_version":1,"samples":[]})");
  ASSERT_DEATH(
      {
        const auto manifest = LoadSampleManifest(duplicate_key);
        (void)manifest;
      },
      "JSON key");
}

TEST(SampleManifestTest, RejectsWrongTypesAndInvalidNumbers) {
  auto wrong_type = ManifestJson();
  wrong_type["samples"][0]["fps"]["numerator"] = "30";
  ExpectInvalidManifest(wrong_type);

  auto zero_denominator = ManifestJson();
  zero_denominator["samples"][0]["fps"]["denominator"] = 0;
  ExpectInvalidManifest(zero_denominator);

  auto excessive_fps = ManifestJson();
  excessive_fps["samples"][0]["fps"] = {{"numerator", 241}, {"denominator", 1}};
  ExpectInvalidManifest(excessive_fps);

  auto negative_duration = ManifestJson();
  negative_duration["samples"][0]["duration_seconds"] = -1;
  ExpectInvalidManifest(negative_duration);
}

TEST(SampleManifestTest, RejectsInvalidIdentityHashAndPath) {
  auto duplicate_id = ManifestJson();
  auto second_sample = SampleJson("second.bin");
  duplicate_id["samples"].push_back(std::move(second_sample));
  ExpectInvalidManifest(duplicate_id);

  auto duplicate_filename = ManifestJson();
  auto duplicate_path = SampleJson();
  duplicate_path["id"] = "S02";
  duplicate_filename["samples"].push_back(std::move(duplicate_path));
  ExpectInvalidManifest(duplicate_filename);

  auto invalid_hash = ManifestJson();
  invalid_hash["samples"][0]["sha256"] = std::string(64, 'A');
  ExpectInvalidManifest(invalid_hash);

  auto traversal = ManifestJson();
  traversal["samples"][0]["filename"] = "../outside.bin";
  ExpectInvalidManifest(traversal);
}

TEST(SampleManifestTest, RejectsInvalidJsonAndEmptySamples) {
  const auto invalid_json = NewTestPath("invalid.json");
  WriteFile(invalid_json, "{invalid json");
  ASSERT_DEATH(
      {
        const auto manifest = LoadSampleManifest(invalid_json);
        (void)manifest;
      },
      "JSON");

  auto empty_samples = ManifestJson();
  empty_samples["samples"] = nlohmann::json::array();
  ExpectInvalidManifest(empty_samples);
}

TEST(SampleManifestTest, VerifiesTrackedMediaAndRejectsModifiedCopy) {
  const auto source_root = std::filesystem::path(ASTRA_SOURCE_ROOT);
  const auto manifest =
      LoadSampleManifest(source_root / "sample_manifest.json");
  EXPECT_NO_FATAL_FAILURE(
      VerifySampleHashes(manifest, source_root / "samples"));

  const auto modified_root = NewTestPath("modified-media");
  std::error_code error;
  std::filesystem::create_directories(modified_root, error);
  if (error) {
    Crash(ErrorCode::kWriteFailure, "测试媒体目录无法创建");
  }
  const auto& source_sample = manifest.samples.front();
  const auto modified_path = modified_root / source_sample.filename;
  std::filesystem::copy_file(source_root / "samples" / source_sample.filename,
                             modified_path, error);
  if (error) {
    Crash(ErrorCode::kWriteFailure, "测试媒体无法复制");
  }
  const SampleManifest copied_manifest{manifest.schema_version,
                                       {source_sample}};
  EXPECT_NO_FATAL_FAILURE(VerifySampleHashes(copied_manifest, modified_root));

  AppendFile(modified_path, "modified");
  ASSERT_DEATH(VerifySampleHashes(copied_manifest, modified_root), "SHA256");
}

TEST(SampleManifestTest, RejectsMissingSampleFile) {
  auto document = ManifestJson();
  document["samples"][0]["filename"] =
      NewTestPath("missing.bin").filename().string();
  const auto manifest = LoadSampleManifest(WriteManifest(document));
  ASSERT_DEATH(VerifySampleHashes(
                   manifest, std::filesystem::path(ASTRA_TEST_OUTPUT_ROOT)),
               "资源路径内容缺失");
}

}  // namespace
}  // namespace astra
