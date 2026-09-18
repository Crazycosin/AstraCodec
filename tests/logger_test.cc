// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/logger.h"

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

}  // namespace
}  // namespace astra
