// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/logger.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/base/error.h"

namespace astra {
namespace {

std::string_view SeverityName(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::kDebug:
      return "debug";
    case LogSeverity::kInfo:
      return "info";
    case LogSeverity::kWarning:
      return "warning";
    case LogSeverity::kError:
      return "error";
    case LogSeverity::kFatal:
      return "fatal";
  }
  Crash(ErrorCode::kInvalidArgument, "未知日志级别");
}

std::string SerializeRecord(const LogRecord& record,
                            std::source_location source) {
  const auto timestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  std::ostringstream thread_id;
  thread_id << std::this_thread::get_id();
  nlohmann::json output{{"timestamp_us", timestamp},
                        {"severity", SeverityName(record.severity)},
                        {"thread_id", thread_id.str()},
                        {"job_id", record.job_id},
                        {"node", record.node},
                        {"pts", record.pts ? nlohmann::json(*record.pts)
                                           : nlohmann::json(nullptr)},
                        {"dts", record.dts ? nlohmann::json(*record.dts)
                                           : nlohmann::json(nullptr)},
                        {"error_code", static_cast<int>(record.error_code)},
                        {"message", record.message},
                        {"source", source.file_name()},
                        {"line", source.line()},
                        {"function", source.function_name()},
                        {"fields", RedactSensitiveFields(record.fields)}};
  return output.dump() + '\n';
}

class AsyncLogger final : public Logger {
 public:
  explicit AsyncLogger(LoggerConfig config) : config_(std::move(config)) {
    SeverityName(config_.minimum_severity);
    if (config_.queue_capacity == 0 || config_.queue_capacity > 65536 ||
        config_.maximum_record_bytes == 0 ||
        config_.maximum_record_bytes > 1048576 ||
        config_.shutdown_timeout.count() <= 0 ||
        config_.rate_interval.count() <= 0) {
      Crash(ErrorCode::kInvalidConfiguration, "Logger 配置超出允许范围");
    }
    if (config_.sink == LogSink::kFile) {
      if (config_.file_path.empty() || config_.maximum_file_bytes == 0) {
        Crash(ErrorCode::kInvalidConfiguration, "文件日志缺少路径或容量");
      }
      std::error_code error;
      const bool exists = std::filesystem::exists(config_.file_path, error);
      if (error) Crash(ErrorCode::kLogFailure, "无法查询日志文件");
      if (exists) {
        file_bytes_ = std::filesystem::file_size(config_.file_path, error);
        if (error || file_bytes_ >= config_.maximum_file_bytes) {
          Crash(ErrorCode::kLogFailure, "日志文件类型或容量不符合要求");
        }
      }
      file_.open(config_.file_path, std::ios::binary | std::ios::app);
      if (!file_) Crash(ErrorCode::kLogFailure, "无法打开日志文件");
      stream_ = &file_;
    } else if (config_.sink == LogSink::kStderr) {
      stream_ = &std::cerr;
    } else {
      Crash(ErrorCode::kInvalidConfiguration, "未知日志 sink");
    }
    worker_ = std::jthread([this] { Run(); });
  }

  ~AsyncLogger() override { Shutdown(); }
  AsyncLogger(const AsyncLogger&) = delete;
  AsyncLogger& operator=(const AsyncLogger&) = delete;
  AsyncLogger(AsyncLogger&&) = delete;
  AsyncLogger& operator=(AsyncLogger&&) = delete;

 private:
  void DoLog(LogRecord record, std::source_location source) override {
    SeverityName(record.severity);
    if (record.error_code != ErrorCode::kOk ||
        record.severity == LogSeverity::kError ||
        record.severity == LogSeverity::kFatal) {
      Crash(record.error_code == ErrorCode::kOk ? ErrorCode::kLogFailure
                                                : record.error_code,
            record.message, source,
            {record.job_id, record.node, record.pts, record.dts,
             std::move(record.fields)});
    }
    if (record.job_id.empty() || record.node.empty()) {
      Crash(ErrorCode::kInvalidArgument, "日志缺少 job_id 或 node", source);
    }
    std::size_t input_bytes = 0;
    const auto check_size = [this, &input_bytes, source](std::size_t bytes) {
      if (bytes > config_.maximum_record_bytes - input_bytes) {
        Crash(ErrorCode::kLogFailure, "日志记录超过容量", source);
      }
      input_bytes += bytes;
    };
    check_size(record.message.size());
    check_size(record.job_id.size());
    check_size(record.node.size());
    for (const auto& [key, value] : record.fields) {
      check_size(key.size());
      check_size(value.size());
      check_size(4);
    }
    auto text = SerializeRecord(record, source);
    if (text.size() > config_.maximum_record_bytes) {
      Crash(ErrorCode::kLogFailure, "日志记录超过容量", source);
    }
    std::lock_guard lock(mutex_);
    if (stopping_) Crash(ErrorCode::kInternalInvariant, "Logger 已经停止");
    if (record.severity < config_.minimum_severity) return;
    if (queue_.size() >= config_.queue_capacity) {
      Crash(ErrorCode::kLogFailure, "日志队列容量已满", source);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - rate_window_ >= config_.rate_interval) {
      rate_window_ = now;
      rate_records_ = 0;
    }
    if (config_.maximum_records_per_interval != 0 &&
        rate_records_ >= config_.maximum_records_per_interval) {
      Crash(ErrorCode::kLogFailure, "日志速率超过限制", source);
    }
    ++rate_records_;
    queue_.push_back({++accepted_sequence_, std::move(text)});
    condition_.notify_all();
  }

 public:
  void Flush() override {
    std::unique_lock lock(mutex_);
    if (stopping_)
      Crash(ErrorCode::kInternalInvariant, "无法刷新已停止的 Logger");
    if (flush_requests_.size() >= config_.queue_capacity) {
      Crash(ErrorCode::kLogFailure, "日志刷新请求容量已满");
    }
    const auto request = ++flush_requested_;
    flush_requests_.push_back({request, accepted_sequence_});
    condition_.notify_all();
    if (!condition_.wait_for(lock, config_.shutdown_timeout, [this, request] {
          return flush_completed_ >= request;
        })) {
      Crash(ErrorCode::kLogFailure, "日志刷新超过等待期限");
    }
  }

  void Shutdown() override {
    std::unique_lock lock(mutex_);
    if (stopped_) return;
    stopping_ = true;
    flush_requests_.push_back({++flush_requested_, accepted_sequence_});
    condition_.notify_all();
    if (!condition_.wait_for(lock, config_.shutdown_timeout,
                             [this] { return stopped_; })) {
      Crash(ErrorCode::kLogFailure, "日志停止超过等待期限");
    }
    lock.unlock();
    worker_.join();
    if (file_.is_open()) {
      file_.close();
      if (file_.fail()) Crash(ErrorCode::kLogFailure, "关闭日志文件失败");
    }
  }

 private:
  struct QueuedRecord {
    std::uint64_t sequence;
    std::string text;
  };

  struct FlushRequest {
    std::uint64_t generation;
    std::uint64_t target;
  };

  void WriteLine(const std::string& text) {
    if (config_.sink == LogSink::kFile &&
        (file_bytes_ > config_.maximum_file_bytes ||
         text.size() > config_.maximum_file_bytes - file_bytes_)) {
      Crash(ErrorCode::kLogFailure, "日志文件容量超限");
    }
    stream_->write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!*stream_) Crash(ErrorCode::kLogFailure, "日志 sink 写入失败");
    file_bytes_ += text.size();
  }

  void Run() {
    std::unique_lock lock(mutex_);
    for (;;) {
      condition_.wait(lock, [this] {
        return stopping_ || !queue_.empty() || !flush_requests_.empty();
      });
      if (!flush_requests_.empty() &&
          written_sequence_ >= flush_requests_.front().target) {
        const auto request = flush_requests_.front();
        flush_requests_.pop_front();
        lock.unlock();
        stream_->flush();
        if (!*stream_) Crash(ErrorCode::kLogFailure, "日志 sink 刷新失败");
        lock.lock();
        flush_completed_ = request.generation;
        condition_.notify_all();
        continue;
      }
      if (!queue_.empty()) {
        auto record = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        WriteLine(record.text);
        lock.lock();
        written_sequence_ = record.sequence;
        continue;
      }
      if (stopping_) {
        stopped_ = true;
        condition_.notify_all();
        return;
      }
    }
  }

  const LoggerConfig config_;
  std::ofstream file_;
  std::ostream* stream_ = nullptr;
  std::uintmax_t file_bytes_ = 0;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<QueuedRecord> queue_;
  std::deque<FlushRequest> flush_requests_;
  std::uint64_t accepted_sequence_ = 0;
  std::uint64_t written_sequence_ = 0;
  std::uint64_t flush_requested_ = 0;
  std::uint64_t flush_completed_ = 0;
  bool stopping_ = false;
  bool stopped_ = false;
  std::chrono::steady_clock::time_point rate_window_ =
      std::chrono::steady_clock::now();
  std::size_t rate_records_ = 0;
  std::jthread worker_;
};

}  // namespace

std::unique_ptr<Logger> CreateLogger(LoggerConfig config) {
  return std::make_unique<AsyncLogger>(std::move(config));
}

}  // namespace astra
