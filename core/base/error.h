// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#ifndef ASTRACODEC_CORE_BASE_ERROR_H_
#define ASTRACODEC_CORE_BASE_ERROR_H_

#include <cstdint>
#include <map>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

namespace astra {

enum class ErrorCode : std::uint8_t {
  kOk = 0,
  kInvalidArgument = 1,
  kInvalidConfiguration = 2,
  kDependencyUnavailable = 3,
  kVersionMismatch = 4,
  kAssetUnavailable = 5,
  kHashMismatch = 6,
  kReadFailure = 7,
  kWriteFailure = 8,
  kLogFailure = 9,
  kInternalInvariant = 10,
};

std::string_view ErrorCodeName(ErrorCode code) noexcept;

struct CrashContext {
  std::string_view job_id = "foundation";
  std::string_view node = "foundation";
  std::optional<std::int64_t> pts;
  std::optional<std::int64_t> dts;
  std::map<std::string, std::string> fields;
};

std::map<std::string, std::string> RedactSensitiveFields(
    std::map<std::string, std::string> fields);

// 按已批准的 D03 输出当前错误并立即终止，不承诺清理或刷新异步日志。
[[noreturn]] void Crash(
    ErrorCode code, std::string_view message,
    std::source_location location = std::source_location::current(),
    CrashContext context = {}) noexcept;

}  // namespace astra

#endif  // ASTRACODEC_CORE_BASE_ERROR_H_
