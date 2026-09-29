// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/logger.h"

#include <array>
#include <atomic>
#include <barrier>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <libproc.h>

#include <mach/mach.h>
#else
#include <sys/syscall.h>
#endif
#endif

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace astra {
namespace {

std::filesystem::path NewLogPath() {
  static std::atomic<unsigned int> sequence{0};
  const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::path(ASTRA_TEST_OUTPUT_ROOT) /
         ("logger-" + std::to_string(time) + "-" + std::to_string(sequence++) +
          ".jsonl");
}

LoggerConfig FileConfig(const std::filesystem::path& path) {
  LoggerConfig config;
  config.sink = LogSink::kFile;
  config.file_path = path;
  return config;
}

std::vector<nlohmann::json> ReadRecords(const std::filesystem::path& path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.good());
  std::vector<nlohmann::json> records;
  std::string line;
  while (std::getline(input, line)) {
    auto record = nlohmann::json::parse(line, nullptr, false);
    EXPECT_FALSE(record.is_discarded());
    records.push_back(std::move(record));
  }
  EXPECT_TRUE(input.eof());
  return records;
}

#if defined(__unix__) || defined(__APPLE__)
class OwnedDescriptor {
 public:
  OwnedDescriptor() = default;
  OwnedDescriptor(const OwnedDescriptor&) = delete;
  OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;
  OwnedDescriptor(OwnedDescriptor&&) = delete;
  OwnedDescriptor& operator=(OwnedDescriptor&&) = delete;
  ~OwnedDescriptor() {
    if (value_ >= 0) close(value_);
  }

  void Reset(int value) {
    if (value_ >= 0) close(value_);
    value_ = value;
  }

  int Get() const { return value_; }

 private:
  int value_ = -1;
};

#if defined(__APPLE__)
using PlatformThreadId = std::uint64_t;

std::set<PlatformThreadId> PlatformThreadIds() {
  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t thread_count = 0;
  if (task_threads(mach_task_self(), &threads, &thread_count) != KERN_SUCCESS) {
    return {};
  }
  std::set<PlatformThreadId> identifiers;
  for (mach_msg_type_number_t index = 0; index < thread_count; ++index) {
    thread_identifier_info_data_t info{};
    mach_msg_type_number_t info_count = THREAD_IDENTIFIER_INFO_COUNT;
    if (thread_info(threads[index], THREAD_IDENTIFIER_INFO,
                    reinterpret_cast<thread_info_t>(&info),
                    &info_count) == KERN_SUCCESS) {
      identifiers.insert(info.thread_id);
    }
    mach_port_deallocate(mach_task_self(), threads[index]);
  }
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                static_cast<vm_size_t>(thread_count) * sizeof(thread_t));
  return identifiers;
}

bool PlatformThreadIsWaiting(thread_t thread) {
  thread_basic_info_data_t info{};
  mach_msg_type_number_t info_count = THREAD_BASIC_INFO_COUNT;
  return thread_info(thread, THREAD_BASIC_INFO,
                     reinterpret_cast<thread_info_t>(&info),
                     &info_count) == KERN_SUCCESS &&
         info.run_state == TH_STATE_WAITING;
}

class WorkerSuspender {
 public:
  WorkerSuspender() : initial_threads_(PlatformThreadIds()) {}
  WorkerSuspender(const WorkerSuspender&) = delete;
  WorkerSuspender& operator=(const WorkerSuspender&) = delete;
  WorkerSuspender(WorkerSuspender&&) = delete;
  WorkerSuspender& operator=(WorkerSuspender&&) = delete;
  ~WorkerSuspender() {
    if (suspended_thread_ != MACH_PORT_NULL) {
      thread_resume(suspended_thread_);
      mach_port_deallocate(mach_task_self(), suspended_thread_);
    }
  }

  bool SuspendNewThread() {
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t thread_count = 0;
    if (task_threads(mach_task_self(), &threads, &thread_count) !=
        KERN_SUCCESS) {
      return false;
    }
    thread_t candidate = MACH_PORT_NULL;
    int new_threads = 0;
    for (mach_msg_type_number_t index = 0; index < thread_count; ++index) {
      thread_identifier_info_data_t info{};
      mach_msg_type_number_t info_count = THREAD_IDENTIFIER_INFO_COUNT;
      const bool has_identifier =
          thread_info(threads[index], THREAD_IDENTIFIER_INFO,
                      reinterpret_cast<thread_info_t>(&info),
                      &info_count) == KERN_SUCCESS;
      if (has_identifier && !initial_threads_.contains(info.thread_id)) {
        ++new_threads;
        if (candidate == MACH_PORT_NULL) {
          candidate = threads[index];
          continue;
        }
      }
      mach_port_deallocate(mach_task_self(), threads[index]);
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                  static_cast<vm_size_t>(thread_count) * sizeof(thread_t));
    if (new_threads != 1 || candidate == MACH_PORT_NULL) {
      if (candidate != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), candidate);
      }
      return false;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!PlatformThreadIsWaiting(candidate) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    if (!PlatformThreadIsWaiting(candidate) ||
        thread_suspend(candidate) != KERN_SUCCESS) {
      mach_port_deallocate(mach_task_self(), candidate);
      return false;
    }
    suspended_thread_ = candidate;
    return true;
  }

 private:
  std::set<PlatformThreadId> initial_threads_;
  thread_t suspended_thread_ = MACH_PORT_NULL;
};
#else
using PlatformThreadId = pid_t;

std::set<PlatformThreadId> PlatformThreadIds() {
  std::set<PlatformThreadId> identifiers;
  const std::filesystem::path task_directory("/proc/self/task");
  for (const auto& entry :
       std::filesystem::directory_iterator(task_directory)) {
    const auto text = entry.path().filename().string();
    PlatformThreadId identifier = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), identifier);
    if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
      identifiers.insert(identifier);
    }
  }
  return identifiers;
}

bool PlatformThreadIsWaiting(PlatformThreadId identifier) {
  std::ifstream input(std::filesystem::path("/proc/self/task") /
                      std::to_string(identifier) / "stat");
  std::string stat;
  if (!std::getline(input, stat)) return false;
  const auto name_end = stat.rfind(") ");
  if (name_end == std::string::npos || name_end + 2 >= stat.size()) {
    return false;
  }
  const char state = stat[name_end + 2];
  return state == 'S' || state == 'I';
}

volatile sig_atomic_t g_suspend_ack_descriptor = -1;
volatile sig_atomic_t g_suspend_release_descriptor = -1;

void SuspendWorkerSignalHandler(int) {
  const char marker = 'R';
  if (write(static_cast<int>(g_suspend_ack_descriptor), &marker, 1) != 1) {
    _exit(120);
  }
  char release = '\0';
  if (read(static_cast<int>(g_suspend_release_descriptor), &release, 1) != 1) {
    _exit(121);
  }
}

class WorkerSuspender {
 public:
  WorkerSuspender() : initial_threads_(PlatformThreadIds()) {
    int ack_descriptors[2];
    int release_descriptors[2];
    if (pipe(ack_descriptors) != 0) return;
    ack_reader_.Reset(ack_descriptors[0]);
    ack_writer_.Reset(ack_descriptors[1]);
    if (pipe(release_descriptors) != 0) return;
    release_reader_.Reset(release_descriptors[0]);
    release_writer_.Reset(release_descriptors[1]);
    struct sigaction action{};
    action.sa_handler = SuspendWorkerSignalHandler;
    if (sigemptyset(&action.sa_mask) != 0) return;
    action.sa_flags = 0;
    if (sigaction(SIGUSR1, &action, &previous_action_) != 0) return;
    action_installed_ = true;
  }
  WorkerSuspender(const WorkerSuspender&) = delete;
  WorkerSuspender& operator=(const WorkerSuspender&) = delete;
  WorkerSuspender(WorkerSuspender&&) = delete;
  WorkerSuspender& operator=(WorkerSuspender&&) = delete;
  ~WorkerSuspender() {
    if (suspended_) {
      const char release = 'R';
      if (write(release_writer_.Get(), &release, 1) != 1) std::abort();
    }
    if (action_installed_ &&
        sigaction(SIGUSR1, &previous_action_, nullptr) != 0) {
      std::abort();
    }
    g_suspend_ack_descriptor = -1;
    g_suspend_release_descriptor = -1;
  }

  bool SuspendNewThread() {
    if (!action_installed_) return false;
    const auto current_threads = PlatformThreadIds();
    PlatformThreadId worker = 0;
    int new_threads = 0;
    for (const auto identifier : current_threads) {
      if (!initial_threads_.contains(identifier)) {
        worker = identifier;
        ++new_threads;
      }
    }
    if (new_threads != 1) return false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!PlatformThreadIsWaiting(worker) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    if (!PlatformThreadIsWaiting(worker)) return false;
    g_suspend_ack_descriptor = ack_writer_.Get();
    g_suspend_release_descriptor = release_reader_.Get();
    if (syscall(SYS_tgkill, getpid(), worker, SIGUSR1) != 0) return false;
    char marker = '\0';
    if (read(ack_reader_.Get(), &marker, 1) != 1 || marker != 'R') {
      return false;
    }
    suspended_ = true;
    return true;
  }

 private:
  std::set<PlatformThreadId> initial_threads_;
  OwnedDescriptor ack_reader_;
  OwnedDescriptor ack_writer_;
  OwnedDescriptor release_reader_;
  OwnedDescriptor release_writer_;
  struct sigaction previous_action_{};
  bool action_installed_ = false;
  bool suspended_ = false;
};
#endif

std::size_t OpenDescriptorCount() {
  std::size_t count = 0;
  for (int descriptor = 0; descriptor < 4096; ++descriptor) {
    errno = 0;
    if (fcntl(descriptor, F_GETFD) >= 0 || errno != EBADF) ++count;
  }
  return count;
}

std::size_t ThreadCount() {
#if defined(__APPLE__)
  proc_taskinfo task_info{};
  if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task_info,
                   sizeof(task_info)) != sizeof(task_info)) {
    return 0;
  }
  return static_cast<std::size_t>(task_info.pti_threadnum);
#else
  const std::filesystem::path task_directory("/proc/self/task");
  if (!std::filesystem::exists(task_directory)) return 0;
  return static_cast<std::size_t>(
      std::distance(std::filesystem::directory_iterator(task_directory),
                    std::filesystem::directory_iterator{}));
#endif
}

void WarmUpThreadRuntime() {
  auto logger = CreateLogger(FileConfig(NewLogPath()));
  logger->Shutdown();
}

int RunSigintLoggerChild(int ready_descriptor,
                         const std::filesystem::path& path) {
  sigset_t signals;
  if (sigemptyset(&signals) != 0 || sigaddset(&signals, SIGINT) != 0 ||
      sigprocmask(SIG_BLOCK, &signals, nullptr) != 0) {
    return 2;
  }
  auto logger = CreateLogger(FileConfig(path));
  std::atomic<bool> running{true};
  std::atomic<bool> ready{false};
  std::jthread producer([&] {
    std::uint64_t index = 0;
    while (running.load()) {
      LogRecord record;
      record.fields.emplace("index", std::to_string(index++));
      logger->Log(std::move(record));
      logger->Flush();
      if (!ready.exchange(true)) {
        const char marker = 'R';
        if (write(ready_descriptor, &marker, 1) != 1) std::abort();
      }
    }
  });
  int received = 0;
  const int wait_result = sigwait(&signals, &received);
  running.store(false);
  producer.join();
  if (wait_result != 0 || received != SIGINT) return 3;
  logger->Flush();
  logger->Shutdown();
  logger.reset();
  return 0;
}
#endif

TEST(LoggerTest, ActualFileContainsStructuredFieldsAndRedaction) {
  const auto path = NewLogPath();
  auto logger = CreateLogger(FileConfig(path));
  LogRecord record;
  record.node = "format_test";
  record.message = "actual line\nwith escaping";
  record.fields = {{"Api_Key", "sensitive value"},
                   {"parameter", "public value"}};
  logger->Log(record);
  logger->Flush();
  const auto records = ReadRecords(path);
  ASSERT_EQ(records.size(), 1);
  EXPECT_EQ(records[0]["message"], record.message);
  EXPECT_EQ(records[0]["fields"]["Api_Key"], "[REDACTED]");
  EXPECT_EQ(records[0]["fields"]["parameter"], "public value");
  for (const auto* field :
       {"timestamp_us", "severity", "thread_id", "job_id", "node", "pts", "dts",
        "error_code", "source", "line", "function"}) {
    EXPECT_TRUE(records[0].contains(field)) << field;
  }
  EXPECT_TRUE(records[0]["pts"].is_null());
  EXPECT_TRUE(records[0]["dts"].is_null());
  EXPECT_EQ(records[0]["source"], __FILE__);
  EXPECT_GT(records[0]["line"].get<unsigned int>(), 0);
  logger->Shutdown();
}

TEST(LoggerTest, SeverityFilteringAndExplicitTimestamps) {
  const auto path = NewLogPath();
  auto config = FileConfig(path);
  config.minimum_severity = LogSeverity::kWarning;
  auto logger = CreateLogger(config);
  logger->Log({});
  LogRecord warning;
  warning.severity = LogSeverity::kWarning;
  warning.pts = 1001;
  warning.dts = -2002;
  logger->Log(warning);
  logger->Shutdown();
  const auto records = ReadRecords(path);
  ASSERT_EQ(records.size(), 1);
  EXPECT_EQ(records[0]["pts"], 1001);
  EXPECT_EQ(records[0]["dts"], -2002);
}

TEST(LoggerTest, RealConcurrentProducersAreCompleteAndOrderedPerProducer) {
  const auto path = NewLogPath();
  auto config = FileConfig(path);
  config.queue_capacity = 4096;
  auto logger = CreateLogger(config);
  {
    std::array<std::jthread, 4> producers;
    for (std::size_t producer = 0; producer < producers.size(); ++producer) {
      producers[producer] = std::jthread([producer, &logger] {
        for (int index = 0; index < 100; ++index) {
          LogRecord record;
          record.fields = {{"producer", std::to_string(producer)},
                           {"index", std::to_string(index)}};
          logger->Log(std::move(record));
        }
      });
    }
  }
  logger->Flush();
  logger->Shutdown();
  const auto records = ReadRecords(path);
  ASSERT_EQ(records.size(), 400);
  std::map<std::string, int> next_index;
  for (const auto& record : records) {
    const auto producer = record["fields"]["producer"].get<std::string>();
    EXPECT_EQ(record["fields"]["index"],
              std::to_string(next_index[producer]++));
  }
  EXPECT_EQ(next_index.size(), 4);
  for (const auto& [producer, count] : next_index)
    EXPECT_EQ(count, 100) << producer;
}

TEST(LoggerTest, RepeatedActualServiceLifecyclesFlushWithoutExplicitShutdown) {
  for (int cycle = 0; cycle < 5; ++cycle) {
    const auto path = NewLogPath();
    {
      auto logger = CreateLogger(FileConfig(path));
      logger->Log({});
    }
    EXPECT_EQ(ReadRecords(path).size(), 1);
  }
}

TEST(LoggerTest, ConcurrentFlushRequestsObserveTheirAcceptedRecords) {
  const auto path = NewLogPath();
  auto logger = CreateLogger(FileConfig(path));
  std::barrier phase(4);
  {
    std::array<std::jthread, 4> producers;
    for (std::size_t producer = 0; producer < producers.size(); ++producer) {
      producers[producer] = std::jthread([producer, &logger, &phase, &path] {
        for (int index = 0; index < 50; ++index) {
          LogRecord record;
          record.fields = {{"producer", std::to_string(producer)},
                           {"index", std::to_string(index)}};
          logger->Log(std::move(record));
          phase.arrive_and_wait();
          logger->Flush();
          EXPECT_EQ(ReadRecords(path).size(),
                    static_cast<std::size_t>((index + 1) * 4));
          phase.arrive_and_wait();
        }
      });
    }
  }
  logger->Shutdown();
  logger->Shutdown();
  EXPECT_EQ(ReadRecords(path).size(), 200);
}

TEST(LoggerTest, InvalidRecordAndStoppedFlushTerminate) {
  ASSERT_DEATH(
      {
        auto logger = CreateLogger(FileConfig(NewLogPath()));
        LogRecord record;
        record.node.clear();
        logger->Log(record);
      },
      "日志缺少 job_id 或 node");
  ASSERT_DEATH(
      {
        auto logger = CreateLogger(FileConfig(NewLogPath()));
        logger->Shutdown();
        logger->Flush();
      },
      "无法刷新已停止的 Logger");
  ASSERT_DEATH(
      {
        auto config = FileConfig(NewLogPath());
        config.maximum_record_bytes = 1;
        auto logger = CreateLogger(config);
        logger->Log({});
      },
      "日志记录超过容量");
}

TEST(LoggerTest, InvalidCapacityAndRealOpenFailureTerminate) {
  LoggerConfig invalid;
  invalid.queue_capacity = 0;
  ASSERT_DEATH(CreateLogger(invalid), "Logger 配置超出允许范围");
  auto unavailable = FileConfig(NewLogPath() / "missing-parent" / "log.jsonl");
  ASSERT_DEATH(CreateLogger(unavailable), "无法打开日志文件");
}

TEST(LoggerTest, ActualFileLimitAndRateLimitTerminate) {
  auto limited = FileConfig(NewLogPath());
  limited.maximum_file_bytes = 1;
  ASSERT_DEATH(
      {
        auto logger = CreateLogger(limited);
        logger->Log({});
        logger->Flush();
      },
      "日志文件容量超限");
  auto rate_limited = FileConfig(NewLogPath());
  rate_limited.maximum_records_per_interval = 1;
  rate_limited.rate_interval = std::chrono::seconds(60);
  ASSERT_DEATH(
      {
        auto logger = CreateLogger(rate_limited);
        logger->Log({});
        logger->Log({});
      },
      "日志速率超过限制");
}

TEST(LoggerTest, ErrorLogAndUseAfterShutdownTerminate) {
  ASSERT_DEATH(
      {
        auto logger = CreateLogger(FileConfig(NewLogPath()));
        LogRecord record;
        record.severity = LogSeverity::kError;
        record.message = "actual error context";
        record.job_id = "failed_job";
        record.node = "failed_node";
        record.pts = 123;
        record.dts = 122;
        record.fields.emplace("api_key", "sensitive value");
        logger->Log(record);
      },
      "REDACTED.*failed_job.*actual error context.*failed_node.*123");
  ASSERT_DEATH(
      {
        auto logger = CreateLogger(FileConfig(NewLogPath()));
        logger->Shutdown();
        logger->Log({});
      },
      "Logger 已经停止");
}

#if defined(__unix__) || defined(__APPLE__)
TEST(LoggerTest, SuspendedActualWorkerCausesQueueOverflow) {
  auto config = FileConfig(NewLogPath());
  config.queue_capacity = 1;
  ASSERT_DEATH(
      {
        WarmUpThreadRuntime();
        WorkerSuspender suspender;
        auto logger = CreateLogger(config);
        ASSERT_TRUE(suspender.SuspendNewThread());
        logger->Log({});
        logger->Log({});
      },
      "日志队列容量已满");
}

TEST(LoggerTest, SuspendedActualWorkerCausesFlushAndShutdownTimeout) {
  auto flush_config = FileConfig(NewLogPath());
  flush_config.shutdown_timeout = std::chrono::milliseconds(1);
  ASSERT_DEATH(
      {
        WarmUpThreadRuntime();
        WorkerSuspender suspender;
        auto logger = CreateLogger(flush_config);
        ASSERT_TRUE(suspender.SuspendNewThread());
        logger->Log({});
        logger->Flush();
      },
      "日志刷新超过等待期限");

  auto shutdown_config = FileConfig(NewLogPath());
  shutdown_config.shutdown_timeout = std::chrono::milliseconds(1);
  ASSERT_DEATH(
      {
        WarmUpThreadRuntime();
        WorkerSuspender suspender;
        auto logger = CreateLogger(shutdown_config);
        ASSERT_TRUE(suspender.SuspendNewThread());
        logger->Log({});
        logger->Shutdown();
      },
      "日志停止超过等待期限");
}

TEST(LoggerTest, ActualProcessFileLimitCausesSinkFailure) {
  auto config = FileConfig(NewLogPath());
  int descriptors[2];
  ASSERT_EQ(pipe(descriptors), 0);
  OwnedDescriptor diagnostic_reader;
  OwnedDescriptor diagnostic_writer;
  diagnostic_reader.Reset(descriptors[0]);
  diagnostic_writer.Reset(descriptors[1]);
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    close(diagnostic_reader.Get());
    if (dup2(diagnostic_writer.Get(), STDERR_FILENO) < 0) _exit(122);
    close(diagnostic_writer.Get());
    struct sigaction ignore_action{};
    ignore_action.sa_handler = SIG_IGN;
    if (sigemptyset(&ignore_action.sa_mask) != 0 ||
        sigaction(SIGXFSZ, &ignore_action, nullptr) != 0) {
      _exit(123);
    }
    rlimit file_limit{};
    file_limit.rlim_cur = 1;
    file_limit.rlim_max = 1;
    if (setrlimit(RLIMIT_FSIZE, &file_limit) != 0) _exit(124);
    auto logger = CreateLogger(config);
    logger->Log({});
    logger->Flush();
    _exit(0);
  }
  diagnostic_writer.Reset(-1);
  std::string diagnostic;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto bytes =
        read(diagnostic_reader.Get(), buffer.data(), buffer.size());
    if (bytes > 0) {
      diagnostic.append(buffer.data(), static_cast<std::size_t>(bytes));
      continue;
    }
    if (bytes < 0 && errno == EINTR) continue;
    ASSERT_EQ(bytes, 0);
    break;
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFSIGNALED(status));
  EXPECT_EQ(WTERMSIG(status), SIGABRT);
  const auto record = nlohmann::json::parse(diagnostic, nullptr, false);
  ASSERT_FALSE(record.is_discarded()) << diagnostic;
  EXPECT_EQ(record["error_code"], static_cast<int>(ErrorCode::kLogFailure));
  EXPECT_TRUE(record["message"] == "日志 sink 写入失败" ||
              record["message"] == "日志 sink 刷新失败")
      << diagnostic;
}

TEST(LoggerTest, ReadOnlyDirectoryRejectsActualFileSink) {
  const auto directory = NewLogPath();
  ASSERT_TRUE(std::filesystem::create_directory(directory));
  ASSERT_EQ(chmod(directory.c_str(), 0500), 0);
  const auto path = directory / "log.jsonl";
  OwnedDescriptor probe;
  probe.Reset(open(path.c_str(), O_WRONLY | O_CREAT, 0600));
  if (probe.Get() >= 0) {
    ASSERT_EQ(chmod(directory.c_str(), 0700), 0);
    GTEST_SKIP() << "当前进程可写入受限目录，权限检查不适用";
  }
  EXPECT_TRUE(errno == EACCES || errno == EPERM);
  EXPECT_DEATH(CreateLogger(FileConfig(path)), "无法打开日志文件");
  ASSERT_EQ(chmod(directory.c_str(), 0700), 0);
}

TEST(LoggerTest, SigintStopsActiveProducerAndClosesLogger) {
  const auto path = NewLogPath();
  int descriptors[2];
  ASSERT_EQ(pipe(descriptors), 0);
  OwnedDescriptor ready_reader;
  OwnedDescriptor ready_writer;
  ready_reader.Reset(descriptors[0]);
  ready_writer.Reset(descriptors[1]);
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    close(ready_reader.Get());
    const int result = RunSigintLoggerChild(ready_writer.Get(), path);
    close(ready_writer.Get());
    _exit(result);
  }
  ready_writer.Reset(-1);
  char marker = '\0';
  ASSERT_EQ(read(ready_reader.Get(), &marker, 1), 1);
  ASSERT_EQ(marker, 'R');
  ASSERT_EQ(kill(child, SIGINT), 0);
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  EXPECT_FALSE(ReadRecords(path).empty());
}

TEST(LoggerTest, RepeatedNormalShutdownReleasesThreadsAndDescriptors) {
  WarmUpThreadRuntime();
  const auto initial_descriptors = OpenDescriptorCount();
  const auto initial_threads = ThreadCount();
  ASSERT_GT(initial_threads, 0);
  for (int cycle = 0; cycle < 5; ++cycle) {
    const auto path = NewLogPath();
    auto logger = CreateLogger(FileConfig(path));
    logger->Log({});
    logger->Flush();
    logger->Shutdown();
    logger.reset();
    EXPECT_EQ(ReadRecords(path).size(), 1);
  }
  EXPECT_EQ(OpenDescriptorCount(), initial_descriptors);
  EXPECT_EQ(ThreadCount(), initial_threads);
}
#endif

}  // namespace
}  // namespace astra
