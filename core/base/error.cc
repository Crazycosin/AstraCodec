// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/error.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace astra {

std::map<std::string, std::string> RedactSensitiveFields(
    std::map<std::string, std::string> fields) {
  for (auto& [key, value] : fields) {
    auto normalized = key;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) {
                     return character >= 'A' && character <= 'Z'
                                ? static_cast<char>(character + ('a' - 'A'))
                                : static_cast<char>(character);
                   });
    for (const auto* sensitive : {"password", "secret", "token", "credential",
                                  "authorization", "api_key"}) {
      if (normalized.find(sensitive) != std::string::npos) {
        value = "[REDACTED]";
        break;
      }
    }
  }
  return fields;
}

std::string_view ErrorCodeName(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk:
      return "ok";
    case ErrorCode::kInvalidArgument:
      return "invalid_argument";
    case ErrorCode::kInvalidConfiguration:
      return "invalid_configuration";
    case ErrorCode::kDependencyUnavailable:
      return "dependency_unavailable";
    case ErrorCode::kVersionMismatch:
      return "version_mismatch";
    case ErrorCode::kAssetUnavailable:
      return "asset_unavailable";
    case ErrorCode::kHashMismatch:
      return "hash_mismatch";
    case ErrorCode::kReadFailure:
      return "read_failure";
    case ErrorCode::kWriteFailure:
      return "write_failure";
    case ErrorCode::kLogFailure:
      return "log_failure";
    case ErrorCode::kInternalInvariant:
      return "internal_invariant";
  }
  Crash(ErrorCode::kInternalInvariant, "未知 ErrorCode");
}

[[noreturn]] void Crash(ErrorCode code, std::string_view message,
                        std::source_location location,
                        CrashContext context) noexcept {
  const auto timestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  std::ostringstream thread_id;
  thread_id << std::this_thread::get_id();
  const nlohmann::json diagnostic{
      {"timestamp_us", timestamp},
      {"thread_id", thread_id.str()},
      {"severity", "fatal"},
      {"job_id", context.job_id},
      {"node", context.node},
      {"pts",
       context.pts ? nlohmann::json(*context.pts) : nlohmann::json(nullptr)},
      {"dts",
       context.dts ? nlohmann::json(*context.dts) : nlohmann::json(nullptr)},
      {"error_code", static_cast<int>(code)},
      {"message", message},
      {"source", location.file_name()},
      {"line", location.line()},
      {"function", location.function_name()},
      {"fields", RedactSensitiveFields(std::move(context.fields))}};
  const auto text = diagnostic.dump();
  // 唯一同步崩溃诊断入口独立于 Logger；诊断写入失败也立即终止。
  if (std::fwrite(text.data(), 1, text.size(), stderr) != text.size())
    std::abort();
  if (std::fwrite("\n", 1, 1, stderr) != 1) std::abort();
  if (std::fflush(stderr) != 0) std::abort();
  std::abort();
}

}  // namespace astra
