// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/sample_manifest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/base/error.h"
#include "core/base/resources.h"

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

namespace astra {
namespace {

constexpr std::size_t kMaximumManifestBytes = std::size_t{1024} * 1024;
constexpr std::size_t kMaximumSamples = 64;
constexpr std::uintmax_t kMaximumSampleBytes =
    std::uintmax_t{1024} * 1024 * 1024;
constexpr int kMaximumJsonDepth = 16;

[[noreturn]] void InvalidManifest(std::string_view reason) {
  Crash(ErrorCode::kInvalidConfiguration,
        "sample manifest 无效：" + std::string(reason));
}

struct FileCloser {
  void operator()(std::FILE* file) const {
    if (std::fclose(file) != 0)
      Crash(ErrorCode::kReadFailure, "关闭 sample 文件失败");
  }
};

using InputFile = std::unique_ptr<std::FILE, FileCloser>;

InputFile OpenInput(const std::filesystem::path& path) {
  InputFile file(std::fopen(path.string().c_str(), "rb"));
  if (!file) Crash(ErrorCode::kReadFailure, "打开 sample 文件失败");
  return file;
}

std::uintmax_t CheckedSize(const std::filesystem::path& path,
                           std::uintmax_t maximum_bytes) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) Crash(ErrorCode::kReadFailure, "读取 sample 文件大小失败");
  if (size > maximum_bytes)
    Crash(ErrorCode::kReadFailure, "sample 文件大小超过限制");
  return size;
}

std::size_t ReadChunk(std::FILE& file,
                      std::array<std::uint8_t, 65536>& buffer) {
  const auto count = std::fread(buffer.data(), 1, buffer.size(), &file);
  if (std::ferror(&file) != 0)
    Crash(ErrorCode::kReadFailure, "读取 sample 文件失败");
  if (count == 0 && std::feof(&file) == 0)
    Crash(ErrorCode::kReadFailure, "sample 文件读取没有继续执行");
  return count;
}

std::string ReadManifest(const std::filesystem::path& path) {
  std::error_code error;
  const auto absolute_path = std::filesystem::absolute(path, error);
  if (error) Crash(ErrorCode::kReadFailure, "计算 manifest 路径失败");
  const ResourceManager resources(absolute_path.parent_path());
  const auto file_path = resources.ResolveFile(absolute_path.filename());
  const auto size = CheckedSize(file_path, kMaximumManifestBytes);
  auto file = OpenInput(file_path);
  std::string text;
  text.reserve(static_cast<std::size_t>(size));
  std::array<std::uint8_t, 65536> buffer{};
  while (const auto count = ReadChunk(*file, buffer)) {
    if (count > kMaximumManifestBytes - text.size())
      Crash(ErrorCode::kReadFailure, "manifest 文件大小超过限制");
    text.append(reinterpret_cast<const char*>(buffer.data()), count);
  }
  return text;
}

nlohmann::json ParseManifest(const std::string& text) {
  std::vector<std::set<std::string>> object_keys;
  const auto callback = [&object_keys](int depth,
                                       nlohmann::json::parse_event_t event,
                                       nlohmann::json& value) {
    if (depth > kMaximumJsonDepth) InvalidManifest("JSON 层级超过限制");
    if (event == nlohmann::json::parse_event_t::object_start) {
      object_keys.emplace_back();
    } else if (event == nlohmann::json::parse_event_t::key) {
      if (object_keys.empty()) InvalidManifest("JSON object 状态无效");
      const auto& key = value.get_ref<const std::string&>();
      if (!object_keys.back().insert(key).second)
        InvalidManifest("JSON key 重复");
    } else if (event == nlohmann::json::parse_event_t::object_end) {
      if (object_keys.empty()) InvalidManifest("JSON object 状态无效");
      object_keys.pop_back();
    }
    return true;
  };
  auto document = nlohmann::json::parse(text, callback, false);
  if (document.is_discarded()) InvalidManifest("JSON 语法错误");
  return document;
}

void RequireFields(const nlohmann::json& value,
                   std::initializer_list<std::string_view> fields) {
  if (!value.is_object()) InvalidManifest("要求 object 字段");
  if (value.size() != fields.size()) InvalidManifest("字段缺失或未知字段");
  for (const auto field : fields) {
    if (!value.contains(field)) InvalidManifest("字段缺失或未知字段");
  }
}

std::string StringField(const nlohmann::json& value, std::string_view field,
                        std::size_t maximum_length = 256) {
  const auto& entry = value.at(field);
  if (!entry.is_string()) InvalidManifest("要求 string 字段");
  auto text = entry.get<std::string>();
  if (text.empty() || text.size() > maximum_length)
    InvalidManifest("string 长度超过允许范围");
  if (std::any_of(text.begin(), text.end(), [](unsigned char character) {
        return character < 32 || character == 127;
      }))
    InvalidManifest("string 包含控制字符");
  return text;
}

std::int64_t IntegerField(const nlohmann::json& value, std::string_view field,
                          std::int64_t minimum, std::int64_t maximum) {
  const auto& entry = value.at(field);
  if (!entry.is_number_integer()) InvalidManifest("要求 integer 字段");
  std::int64_t number = 0;
  if (entry.is_number_unsigned()) {
    const auto unsigned_number = entry.get<std::uint64_t>();
    if (unsigned_number > static_cast<std::uint64_t>(maximum))
      InvalidManifest("integer 超过允许范围");
    number = static_cast<std::int64_t>(unsigned_number);
  } else {
    number = entry.get<std::int64_t>();
  }
  if (number < minimum || number > maximum)
    InvalidManifest("integer 超过允许范围");
  return number;
}

Rational RationalField(const nlohmann::json& value, std::string_view field) {
  const auto& entry = value.at(field);
  RequireFields(entry, {"numerator", "denominator"});
  return {static_cast<int>(IntegerField(entry, "numerator", 1,
                                        std::numeric_limits<int>::max())),
          static_cast<int>(IntegerField(entry, "denominator", 1,
                                        std::numeric_limits<int>::max()))};
}

SampleResolution ResolutionField(const nlohmann::json& value) {
  const auto& entry = value.at("resolution");
  RequireFields(entry, {"width", "height"});
  return {static_cast<int>(IntegerField(entry, "width", 1, 16384)),
          static_cast<int>(IntegerField(entry, "height", 1, 16384))};
}

std::optional<AudioSampleInfo> AudioField(const nlohmann::json& value) {
  const auto& entry = value.at("audio");
  if (entry.is_null()) return std::nullopt;
  RequireFields(entry, {"codec", "sample_rate", "channels"});
  return AudioSampleInfo{
      StringField(entry, "codec"),
      static_cast<int>(IntegerField(entry, "sample_rate", 1, 384000)),
      static_cast<int>(IntegerField(entry, "channels", 1, 64))};
}

SampleSourceInfo SourceField(const nlohmann::json& value) {
  const auto& entry = value.at("source");
  RequireFields(entry, {"method", "description", "license",
                        "generation_command", "tool_version"});
  return {StringField(entry, "method"), StringField(entry, "description", 4096),
          StringField(entry, "license", 4096),
          StringField(entry, "generation_command", 16384),
          StringField(entry, "tool_version", 4096)};
}

SampleInfo ParseSample(const nlohmann::json& value) {
  RequireFields(
      value, {"id", "filename", "codec", "container", "resolution", "fps",
              "duration_seconds", "sha256", "pixel_format", "video_frame_count",
              "time_base", "has_b_frames", "audio", "source"});
  const auto& duration = value.at("duration_seconds");
  if (!duration.is_number()) InvalidManifest("要求数值 duration_seconds");
  const auto duration_seconds = duration.get<double>();
  if (!std::isfinite(duration_seconds) || duration_seconds <= 0 ||
      duration_seconds > 3600)
    InvalidManifest("duration_seconds 超过允许范围");
  const auto& has_b_frames = value.at("has_b_frames");
  if (!has_b_frames.is_boolean()) InvalidManifest("要求 boolean has_b_frames");
  auto hash = StringField(value, "sha256");
  if (hash.size() != 64 ||
      !std::all_of(hash.begin(), hash.end(), [](unsigned char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      }))
    InvalidManifest("sha256 要求 64 个小写 hexadecimal 字符");
  const std::filesystem::path filename(StringField(value, "filename", 4096));
  if (filename.is_absolute() || filename.has_root_path())
    InvalidManifest("filename 要求相对路径");
  for (const auto& component : filename) {
    if (component == "." || component == ".." || component.empty())
      InvalidManifest("filename 包含非法路径组件");
  }
  const auto fps = RationalField(value, "fps");
  if (static_cast<std::int64_t>(fps.numerator) >
      static_cast<std::int64_t>(fps.denominator) * 240)
    InvalidManifest("fps 超过 240");
  return {StringField(value, "id"),
          filename,
          StringField(value, "codec"),
          StringField(value, "container"),
          ResolutionField(value),
          fps,
          duration_seconds,
          std::move(hash),
          StringField(value, "pixel_format"),
          IntegerField(value, "video_frame_count", 1, 10000000),
          RationalField(value, "time_base"),
          has_b_frames.get<bool>(),
          AudioField(value),
          SourceField(value)};
}

struct ShaCloser {
  void operator()(AVSHA* context) const { av_free(context); }
};

std::string FileHash(const std::filesystem::path& path) {
  CheckedSize(path, kMaximumSampleBytes);
  auto file = OpenInput(path);
  std::unique_ptr<AVSHA, ShaCloser> sha(av_sha_alloc());
  if (!sha || av_sha_init(sha.get(), 256) != 0)
    Crash(ErrorCode::kDependencyUnavailable, "初始化 FFmpeg SHA256 失败");
  std::array<std::uint8_t, 65536> buffer{};
  std::uintmax_t total = 0;
  while (const auto count = ReadChunk(*file, buffer)) {
    if (count > kMaximumSampleBytes - total)
      Crash(ErrorCode::kReadFailure, "sample 文件大小超过限制");
    total += count;
    av_sha_update(sha.get(), buffer.data(), count);
  }
  std::array<std::uint8_t, 32> digest{};
  av_sha_final(sha.get(), digest.data());
  constexpr std::string_view hex_characters = "0123456789abcdef";
  std::string text;
  text.reserve(digest.size() * 2);
  for (const auto byte : digest) {
    text.push_back(hex_characters[byte >> 4]);
    text.push_back(hex_characters[byte & 15]);
  }
  return text;
}

}  // namespace

SampleManifest LoadSampleManifest(const std::filesystem::path& manifest_path) {
  const auto document = ParseManifest(ReadManifest(manifest_path));
  RequireFields(document, {"schema_version", "samples"});
  const auto schema = IntegerField(document, "schema_version", 1, 1);
  const auto& entries = document.at("samples");
  if (!entries.is_array() || entries.empty() ||
      entries.size() > kMaximumSamples)
    InvalidManifest("samples 要求 1 至 64 个样本组成的 array");
  SampleManifest manifest{static_cast<int>(schema), {}};
  manifest.samples.reserve(entries.size());
  std::set<std::string> ids;
  std::set<std::filesystem::path> filenames;
  for (const auto& entry : entries) {
    auto sample = ParseSample(entry);
    if (!ids.insert(sample.id).second) InvalidManifest("sample id 重复");
    if (!filenames.insert(sample.filename.lexically_normal()).second)
      InvalidManifest("sample filename 重复");
    manifest.samples.push_back(std::move(sample));
  }
  return manifest;
}

void VerifySampleHashes(const SampleManifest& manifest,
                        const std::filesystem::path& sample_root) {
  const ResourceManager resources(sample_root);
  for (const auto& sample : manifest.samples) {
    const auto path = resources.ResolveFile(sample.filename);
    if (FileHash(path) != sample.sha256)
      Crash(ErrorCode::kHashMismatch, "sample SHA256 不匹配：" + sample.id);
  }
}

}  // namespace astra
