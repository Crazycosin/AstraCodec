// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#ifndef ASTRACODEC_CORE_BASE_LOGGER_H_
#define ASTRACODEC_CORE_BASE_LOGGER_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <utility>

#include "core/base/error.h"

namespace astra {

enum class LogSeverity : std::uint8_t {
  kDebug,
  kInfo,
  kWarning,
  kError,
  kFatal
};
enum class LogSink : std::uint8_t { kStderr, kFile };

struct LoggerConfig {
  LogSeverity minimum_severity = LogSeverity::kInfo;
  LogSink sink = LogSink::kStderr;
  std::filesystem::path file_path;
  std::size_t queue_capacity = 4096;
  std::size_t maximum_record_bytes = 65536;
  std::uintmax_t maximum_file_bytes = std::uintmax_t{16} * 1024 * 1024;
  std::size_t maximum_records_per_interval = 0;
  std::chrono::milliseconds rate_interval{1000};
  std::chrono::milliseconds shutdown_timeout{5000};
};

struct LogRecord {
  LogSeverity severity = LogSeverity::kInfo;
  std::string job_id = "foundation";
  std::string node = "foundation";
  std::optional<std::int64_t> pts;
  std::optional<std::int64_t> dts;
  ErrorCode error_code = ErrorCode::kOk;
  std::string message;
  std::map<std::string, std::string> fields;
};

// Log 和 Flush 支持并发。Flush 等待进入时已接受的记录完成写入和刷新。
// Shutdown 由唯一拥有者在全部 Log/Flush 调用结束后调用，可重复调用。
// error/fatal、容量超限、I/O 和等待超时均按 D03 立即崩溃。
class Logger {
 public:
  virtual ~Logger() = default;
  void Log(LogRecord record,
           std::source_location source = std::source_location::current()) {
    DoLog(std::move(record), source);
  }
  virtual void Flush() = 0;
  virtual void Shutdown() = 0;

 protected:
  virtual void DoLog(LogRecord record, std::source_location source) = 0;
};

std::unique_ptr<Logger> CreateLogger(LoggerConfig config);

}  // namespace astra

#endif  // ASTRACODEC_CORE_BASE_LOGGER_H_
