// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/configuration.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/base/error.h"
#include "core/base/resources.h"

namespace astra {
namespace {

constexpr std::size_t kMaximumConfigurationBytes = std::size_t{256} * 1024;
constexpr int kMaximumJsonDepth = 8;
constexpr std::uint64_t kMaximumLogFileBytes =
    std::uint64_t{1024} * 1024 * 1024 * 1024;
constexpr std::uint64_t kMaximumRateRecords = 1000000;
constexpr std::int64_t kMaximumRateIntervalMilliseconds =
    std::int64_t{60} * 60 * 1000;
constexpr std::int64_t kMaximumShutdownMilliseconds =
    std::int64_t{5} * 60 * 1000;

[[noreturn]] void InvalidConfiguration(std::string_view reason) {
  Crash(ErrorCode::kInvalidConfiguration,
        "foundation 配置无效：" + std::string(reason));
}

struct FileCloser {
  void operator()(std::FILE* file) const {
    if (std::fclose(file) != 0) {
      Crash(ErrorCode::kReadFailure, "关闭 foundation 配置失败");
    }
  }
};

using InputFile = std::unique_ptr<std::FILE, FileCloser>;

bool IsMissing(const std::error_code& error,
               const std::filesystem::file_status& status) {
  return error == std::errc::no_such_file_or_directory ||
         (!error && status.type() == std::filesystem::file_type::not_found);
}

std::string ReadConfiguration(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    Crash(ErrorCode::kReadFailure, "读取 foundation 配置大小失败");
  }
  if (size > kMaximumConfigurationBytes) {
    InvalidConfiguration("文件大小超过 256 KiB");
  }
  InputFile file(std::fopen(path.string().c_str(), "rb"));
  if (!file) Crash(ErrorCode::kReadFailure, "打开 foundation 配置失败");
  std::string text;
  text.reserve(static_cast<std::size_t>(size));
  std::array<char, 16384> buffer{};
  while (true) {
    const auto count = std::fread(buffer.data(), 1, buffer.size(), file.get());
    if (count != 0) {
      if (count > kMaximumConfigurationBytes - text.size()) {
        InvalidConfiguration("文件大小超过 256 KiB");
      }
      text.append(buffer.data(), count);
    }
    if (count < buffer.size()) {
      if (std::ferror(file.get()) != 0) {
        Crash(ErrorCode::kReadFailure, "读取 foundation 配置失败");
      }
      if (std::feof(file.get()) != 0) break;
      Crash(ErrorCode::kReadFailure, "foundation 配置读取没有继续执行");
    }
  }
  return text;
}

nlohmann::json ParseConfiguration(const std::string& text) {
  std::vector<std::set<std::string>> object_keys;
  const auto callback = [&object_keys](int depth,
                                       nlohmann::json::parse_event_t event,
                                       nlohmann::json& value) {
    if (depth > kMaximumJsonDepth) InvalidConfiguration("JSON 层级超过限制");
    if (event == nlohmann::json::parse_event_t::object_start) {
      object_keys.emplace_back();
    } else if (event == nlohmann::json::parse_event_t::key) {
      if (object_keys.empty()) InvalidConfiguration("JSON object 状态无效");
      const auto& key = value.get_ref<const std::string&>();
      if (!object_keys.back().insert(key).second) {
        InvalidConfiguration("JSON key 重复");
      }
    } else if (event == nlohmann::json::parse_event_t::object_end) {
      if (object_keys.empty()) InvalidConfiguration("JSON object 状态无效");
      object_keys.pop_back();
    }
    return true;
  };
  auto document = nlohmann::json::parse(text, callback, false);
  if (document.is_discarded()) InvalidConfiguration("JSON 语法错误");
  return document;
}

void RequireFields(const nlohmann::json& value,
                   std::initializer_list<std::string_view> fields) {
  if (!value.is_object()) InvalidConfiguration("要求 object 字段");
  if (value.size() != fields.size()) {
    InvalidConfiguration("字段缺失或含有未知字段");
  }
  for (const auto field : fields) {
    if (!value.contains(field)) {
      InvalidConfiguration("字段缺失或含有未知字段");
    }
  }
}

std::string StringField(const nlohmann::json& value, std::string_view field,
                        std::size_t maximum_length = 4096) {
  const auto& entry = value.at(field);
  if (!entry.is_string()) InvalidConfiguration("要求 string 字段");
  auto text = entry.get<std::string>();
  if (text.empty() || text.size() > maximum_length) {
    InvalidConfiguration("string 长度超过允许范围");
  }
  if (std::any_of(text.begin(), text.end(), [](unsigned char character) {
        return character < 32 || character == 127;
      })) {
    InvalidConfiguration("string 包含控制字符");
  }
  return text;
}

std::uint64_t UnsignedField(const nlohmann::json& value, std::string_view field,
                            std::uint64_t minimum, std::uint64_t maximum) {
  const auto& entry = value.at(field);
  if (!entry.is_number_integer()) {
    InvalidConfiguration("要求非负 integer 字段");
  }
  std::uint64_t number = 0;
  if (entry.is_number_unsigned()) {
    number = entry.get<std::uint64_t>();
  } else {
    const auto signed_number = entry.get<std::int64_t>();
    if (signed_number < 0) {
      InvalidConfiguration("非负 integer 超过允许范围");
    }
    number = static_cast<std::uint64_t>(signed_number);
  }
  if (number < minimum || number > maximum) {
    InvalidConfiguration("非负 integer 超过允许范围");
  }
  return number;
}

std::filesystem::path RelativePathField(const nlohmann::json& value,
                                        std::string_view field) {
  const std::filesystem::path path(StringField(value, field));
  if (path.is_absolute() || path.has_root_path()) {
    InvalidConfiguration("资源路径必须相对于 runtime root");
  }
  for (const auto& component : path) {
    if (component.empty() || component == "." || component == "..") {
      InvalidConfiguration("资源路径含有非法组件");
    }
  }
  return path;
}

LogSeverity ParseSeverity(const nlohmann::json& logger) {
  const auto severity = StringField(logger, "minimum_severity", 16);
  if (severity == "debug") return LogSeverity::kDebug;
  if (severity == "info") return LogSeverity::kInfo;
  if (severity == "warning") return LogSeverity::kWarning;
  if (severity == "error") return LogSeverity::kError;
  if (severity == "fatal") return LogSeverity::kFatal;
  InvalidConfiguration("minimum_severity 不受支持");
}

LogSink ParseSink(const nlohmann::json& logger) {
  const auto sink = StringField(logger, "sink", 16);
  if (sink == "stderr") return LogSink::kStderr;
  if (sink == "file") return LogSink::kFile;
  InvalidConfiguration("logger sink 不受支持");
}

LoggerConfig ParseLogger(const nlohmann::json& value,
                         const ResourceManager& resources) {
  RequireFields(value, {"minimum_severity", "sink", "file_path",
                        "queue_capacity", "maximum_record_bytes",
                        "maximum_file_bytes", "maximum_records_per_interval",
                        "rate_interval_ms", "shutdown_timeout_ms"});
  LoggerConfig logger;
  logger.minimum_severity = ParseSeverity(value);
  logger.sink = ParseSink(value);
  const auto& file_path = value.at("file_path");
  if (logger.sink == LogSink::kStderr) {
    if (!file_path.is_null()) {
      InvalidConfiguration("stderr sink 要求 file_path 为 null");
    }
  } else {
    if (!file_path.is_string()) {
      InvalidConfiguration("file sink 要求 string file_path");
    }
    logger.file_path =
        resources.ResolveOutputFile(RelativePathField(value, "file_path"));
  }
  logger.queue_capacity = static_cast<std::size_t>(
      UnsignedField(value, "queue_capacity", 1, 65536));
  logger.maximum_record_bytes = static_cast<std::size_t>(UnsignedField(
      value, "maximum_record_bytes", 1, std::uint64_t{1024} * 1024));
  logger.maximum_file_bytes = static_cast<std::uintmax_t>(
      UnsignedField(value, "maximum_file_bytes", 1, kMaximumLogFileBytes));
  logger.maximum_records_per_interval = static_cast<std::size_t>(UnsignedField(
      value, "maximum_records_per_interval", 0, kMaximumRateRecords));
  logger.rate_interval = std::chrono::milliseconds(UnsignedField(
      value, "rate_interval_ms", 1, kMaximumRateIntervalMilliseconds));
  logger.shutdown_timeout = std::chrono::milliseconds(UnsignedField(
      value, "shutdown_timeout_ms", 1, kMaximumShutdownMilliseconds));
  return logger;
}

std::filesystem::path ResolveExplicitConfiguration(
    const ResourceManager& runtime_resources,
    const std::filesystem::path& explicit_path) {
  if (explicit_path.empty()) {
    InvalidConfiguration("显式配置路径为空");
  }
  if (!explicit_path.is_absolute() && !explicit_path.has_root_path()) {
    return runtime_resources.ResolveFile(explicit_path);
  }
  if (explicit_path.filename().empty()) {
    InvalidConfiguration("显式配置路径缺少文件名");
  }
  const ResourceManager configuration_directory(explicit_path.parent_path());
  return configuration_directory.ResolveFile(explicit_path.filename());
}

std::optional<std::filesystem::path> ResolveDefaultConfiguration(
    const ResourceManager& runtime_resources) {
  std::error_code error;
  const auto directory_path = runtime_resources.Root() / "configs";
  const auto directory_status =
      std::filesystem::symlink_status(directory_path, error);
  if (IsMissing(error, directory_status)) return std::nullopt;
  if (error) {
    Crash(ErrorCode::kReadFailure, "查询默认配置目录失败");
  }
  const auto resolved_directory = runtime_resources.ResolveDirectory("configs");
  const auto configuration_path = resolved_directory / "foundation.json";
  const auto configuration_status =
      std::filesystem::symlink_status(configuration_path, error);
  if (IsMissing(error, configuration_status)) return std::nullopt;
  if (error) Crash(ErrorCode::kReadFailure, "查询默认配置失败");
  return runtime_resources.ResolveFile("configs/foundation.json");
}

FoundationConfig BuiltInConfiguration(const ResourceManager& resources) {
  FoundationConfig configuration;
  configuration.sample_root = resources.ResolveDirectory("samples");
  configuration.manifest_path = resources.ResolveFile("sample_manifest.json");
  return configuration;
}

FoundationConfig ParseFoundationConfiguration(
    const nlohmann::json& document, const ResourceManager& resources) {
  RequireFields(document,
                {"schema_version", "logger", "sample_root", "manifest_path"});
  const auto schema_version = UnsignedField(document, "schema_version", 1, 1);
  FoundationConfig configuration;
  configuration.schema_version = static_cast<int>(schema_version);
  configuration.logger = ParseLogger(document.at("logger"), resources);
  configuration.sample_root =
      resources.ResolveDirectory(RelativePathField(document, "sample_root"));
  configuration.manifest_path =
      resources.ResolveFile(RelativePathField(document, "manifest_path"));
  return configuration;
}

}  // namespace

FoundationConfig LoadFoundationConfig(
    const std::filesystem::path& runtime_root,
    const std::optional<std::filesystem::path>& explicit_config) {
  const ResourceManager resources(runtime_root);
  if (explicit_config.has_value()) {
    const auto path = ResolveExplicitConfiguration(resources, *explicit_config);
    return ParseFoundationConfiguration(
        ParseConfiguration(ReadConfiguration(path)), resources);
  }
  const auto default_path = ResolveDefaultConfiguration(resources);
  if (!default_path.has_value()) return BuiltInConfiguration(resources);
  return ParseFoundationConfiguration(
      ParseConfiguration(ReadConfiguration(*default_path)), resources);
}

}  // namespace astra
