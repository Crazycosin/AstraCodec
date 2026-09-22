// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/configuration.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace astra {
namespace {

std::filesystem::path NewRuntimeRoot() {
  static std::atomic<unsigned int> sequence{0};
  const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::path(ASTRA_TEST_OUTPUT_ROOT) /
                    ("configuration-" + std::to_string(time) + "-" +
                     std::to_string(sequence++));
  std::error_code error;
  std::filesystem::create_directories(root / "samples", error);
  if (error) std::abort();
  return root;
}

void CreateDirectory(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) std::abort();
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) std::abort();
  if (std::fwrite(text.data(), 1, text.size(), file) != text.size()) {
    std::abort();
  }
  if (std::fclose(file) != 0) std::abort();
}

void AddRuntimeFiles(const std::filesystem::path& root) {
  WriteText(root / "sample_manifest.json", "{}\n");
}

nlohmann::json ValidConfiguration() {
  return {{"schema_version", 1},
          {"logger",
           {{"minimum_severity", "info"},
            {"sink", "stderr"},
            {"file_path", nullptr},
            {"queue_capacity", 4096},
            {"maximum_record_bytes", 65536},
            {"maximum_file_bytes", 16777216},
            {"maximum_records_per_interval", 0},
            {"rate_interval_ms", 1000},
            {"shutdown_timeout_ms", 5000}}},
          {"sample_root", "samples"},
          {"manifest_path", "sample_manifest.json"}};
}

void WriteDefaultConfiguration(const std::filesystem::path& root,
                               const nlohmann::json& document) {
  CreateDirectory(root / "configs");
  WriteText(root / "configs/foundation.json", document.dump());
}

class CurrentPathGuard {
 public:
  CurrentPathGuard() {
    std::error_code error;
    path_ = std::filesystem::current_path(error);
    if (error) std::abort();
  }
  CurrentPathGuard(const CurrentPathGuard&) = delete;
  CurrentPathGuard& operator=(const CurrentPathGuard&) = delete;
  CurrentPathGuard(CurrentPathGuard&&) = delete;
  CurrentPathGuard& operator=(CurrentPathGuard&&) = delete;
  ~CurrentPathGuard() {
    std::error_code error;
    std::filesystem::current_path(path_, error);
    if (error) std::abort();
  }

 private:
  std::filesystem::path path_;
};

void ChangeCurrentPath(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::current_path(path, error);
  if (error) std::abort();
}

TEST(ConfigurationTest, MissingDefaultUsesBuiltInValuesIndependentOfCwd) {
  const auto root = NewRuntimeRoot();
  AddRuntimeFiles(root);
  const auto other_directory = NewRuntimeRoot();
  FoundationConfig configuration;
  {
    CurrentPathGuard guard;
    ChangeCurrentPath(other_directory);
    configuration = LoadFoundationConfig(root);
  }
  EXPECT_EQ(configuration.schema_version, 1);
  EXPECT_EQ(configuration.logger.minimum_severity, LogSeverity::kInfo);
  EXPECT_EQ(configuration.logger.sink, LogSink::kStderr);
  EXPECT_TRUE(configuration.logger.file_path.empty());
  EXPECT_EQ(configuration.sample_root, root / "samples");
  EXPECT_EQ(configuration.manifest_path, root / "sample_manifest.json");
}

TEST(ConfigurationTest, LoadsEveryTypedLoggerSettingAndRuntimePath) {
  const auto root = NewRuntimeRoot();
  AddRuntimeFiles(root);
  CreateDirectory(root / "logs");
  auto document = ValidConfiguration();
  document["logger"]["minimum_severity"] = "warning";
  document["logger"]["sink"] = "file";
  document["logger"]["file_path"] = "logs/foundation.jsonl";
  document["logger"]["queue_capacity"] = 128;
  document["logger"]["maximum_record_bytes"] = 32768;
  document["logger"]["maximum_file_bytes"] = 33554432;
  document["logger"]["maximum_records_per_interval"] = 25;
  document["logger"]["rate_interval_ms"] = 250;
  document["logger"]["shutdown_timeout_ms"] = 7500;
  WriteDefaultConfiguration(root, document);
  const auto configuration = LoadFoundationConfig(root);
  EXPECT_EQ(configuration.schema_version, 1);
  EXPECT_EQ(configuration.logger.minimum_severity, LogSeverity::kWarning);
  EXPECT_EQ(configuration.logger.sink, LogSink::kFile);
  EXPECT_EQ(configuration.logger.file_path, root / "logs/foundation.jsonl");
  EXPECT_EQ(configuration.logger.queue_capacity, 128);
  EXPECT_EQ(configuration.logger.maximum_record_bytes, 32768);
  EXPECT_EQ(configuration.logger.maximum_file_bytes, 33554432);
  EXPECT_EQ(configuration.logger.maximum_records_per_interval, 25);
  EXPECT_EQ(configuration.logger.rate_interval, std::chrono::milliseconds(250));
  EXPECT_EQ(configuration.logger.shutdown_timeout,
            std::chrono::milliseconds(7500));
  EXPECT_EQ(configuration.sample_root, root / "samples");
  EXPECT_EQ(configuration.manifest_path, root / "sample_manifest.json");
}

TEST(ConfigurationTest, ExplicitRelativeConfigurationHasPriorityAndStableRoot) {
  const auto root = NewRuntimeRoot();
  AddRuntimeFiles(root);
  CreateDirectory(root / "configs");
  WriteText(root / "configs/foundation.json", "{invalid default");
  CreateDirectory(root / "custom");
  auto document = ValidConfiguration();
  document["logger"]["queue_capacity"] = 321;
  WriteText(root / "custom/explicit.json", document.dump());
  const auto other_directory = NewRuntimeRoot();
  FoundationConfig configuration;
  {
    CurrentPathGuard guard;
    ChangeCurrentPath(other_directory);
    configuration = LoadFoundationConfig(
        root, std::filesystem::path("custom/explicit.json"));
  }
  EXPECT_EQ(configuration.logger.queue_capacity, 321);
  EXPECT_EQ(configuration.sample_root, root / "samples");
}

TEST(ConfigurationTest, ExplicitAbsoluteConfigurationMayBeExternal) {
  const auto root = NewRuntimeRoot();
  AddRuntimeFiles(root);
  const auto external = NewRuntimeRoot();
  const auto path = external / "explicit.json";
  WriteText(path, ValidConfiguration().dump());
  const auto configuration = LoadFoundationConfig(root, path);
  EXPECT_EQ(configuration.sample_root, root / "samples");
  EXPECT_EQ(configuration.manifest_path, root / "sample_manifest.json");
}

TEST(ConfigurationTest, RejectsMalformedDuplicateAndUnknownFields) {
  const auto malformed_root = NewRuntimeRoot();
  AddRuntimeFiles(malformed_root);
  CreateDirectory(malformed_root / "configs");
  WriteText(malformed_root / "configs/foundation.json", "{invalid");
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(malformed_root);
        (void)configuration;
      },
      "JSON 语法错误");

  const auto duplicate_root = NewRuntimeRoot();
  AddRuntimeFiles(duplicate_root);
  CreateDirectory(duplicate_root / "configs");
  WriteText(duplicate_root / "configs/foundation.json",
            "{\"schema_version\":1,\"schema_version\":1}");
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(duplicate_root);
        (void)configuration;
      },
      "JSON key 重复");

  const auto unknown_root = NewRuntimeRoot();
  AddRuntimeFiles(unknown_root);
  auto unknown = ValidConfiguration();
  unknown["unknown"] = true;
  WriteDefaultConfiguration(unknown_root, unknown);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(unknown_root);
        (void)configuration;
      },
      "未知字段");

  const auto logger_unknown_root = NewRuntimeRoot();
  AddRuntimeFiles(logger_unknown_root);
  auto logger_unknown = ValidConfiguration();
  logger_unknown["logger"]["unknown"] = true;
  WriteDefaultConfiguration(logger_unknown_root, logger_unknown);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(logger_unknown_root);
        (void)configuration;
      },
      "未知字段");
}

TEST(ConfigurationTest, RejectsWrongTypesUnsupportedValuesAndRanges) {
  const auto wrong_type_root = NewRuntimeRoot();
  AddRuntimeFiles(wrong_type_root);
  auto wrong_type = ValidConfiguration();
  wrong_type["logger"]["queue_capacity"] = "4096";
  WriteDefaultConfiguration(wrong_type_root, wrong_type);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(wrong_type_root);
        (void)configuration;
      },
      "要求非负 integer");

  const auto range_root = NewRuntimeRoot();
  AddRuntimeFiles(range_root);
  auto range = ValidConfiguration();
  range["logger"]["queue_capacity"] = 65537;
  WriteDefaultConfiguration(range_root, range);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(range_root);
        (void)configuration;
      },
      "超过允许范围");

  const auto schema_root = NewRuntimeRoot();
  AddRuntimeFiles(schema_root);
  auto schema = ValidConfiguration();
  schema["schema_version"] = 2;
  WriteDefaultConfiguration(schema_root, schema);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(schema_root);
        (void)configuration;
      },
      "超过允许范围");

  const auto sink_root = NewRuntimeRoot();
  AddRuntimeFiles(sink_root);
  auto sink = ValidConfiguration();
  sink["logger"]["sink"] = "network";
  WriteDefaultConfiguration(sink_root, sink);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(sink_root);
        (void)configuration;
      },
      "sink 不受支持");

  const auto file_path_root = NewRuntimeRoot();
  AddRuntimeFiles(file_path_root);
  auto file_path = ValidConfiguration();
  file_path["logger"]["file_path"] = "logs/unexpected.jsonl";
  WriteDefaultConfiguration(file_path_root, file_path);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(file_path_root);
        (void)configuration;
      },
      "stderr sink 要求 file_path 为 null");
}

TEST(ConfigurationTest, RejectsOversizedFilesAndExplicitMissingFile) {
  const auto oversized_root = NewRuntimeRoot();
  AddRuntimeFiles(oversized_root);
  CreateDirectory(oversized_root / "configs");
  WriteText(oversized_root / "configs/foundation.json",
            std::string(256 * 1024 + 1, 'x'));
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(oversized_root);
        (void)configuration;
      },
      "文件大小超过 256 KiB");

  const auto missing_root = NewRuntimeRoot();
  AddRuntimeFiles(missing_root);
  WriteDefaultConfiguration(missing_root, ValidConfiguration());
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(
            missing_root, std::filesystem::path("configs/missing.json"));
        (void)configuration;
      },
      "内容缺失或无法读取");
}

TEST(ConfigurationTest, RejectsEscapingAndWrongResourcePaths) {
  const auto traversal_root = NewRuntimeRoot();
  AddRuntimeFiles(traversal_root);
  auto traversal = ValidConfiguration();
  traversal["sample_root"] = "../samples";
  WriteDefaultConfiguration(traversal_root, traversal);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(traversal_root);
        (void)configuration;
      },
      "非法组件");

  const auto absolute_root = NewRuntimeRoot();
  AddRuntimeFiles(absolute_root);
  auto absolute = ValidConfiguration();
  absolute["manifest_path"] = (absolute_root / "sample_manifest.json").string();
  WriteDefaultConfiguration(absolute_root, absolute);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(absolute_root);
        (void)configuration;
      },
      "必须相对于 runtime root");

  const auto wrong_type_root = NewRuntimeRoot();
  AddRuntimeFiles(wrong_type_root);
  auto wrong_type = ValidConfiguration();
  wrong_type["sample_root"] = "sample_manifest.json";
  WriteDefaultConfiguration(wrong_type_root, wrong_type);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(wrong_type_root);
        (void)configuration;
      },
      "文件类型错误");

  const auto output_root = NewRuntimeRoot();
  AddRuntimeFiles(output_root);
  auto output = ValidConfiguration();
  output["logger"]["sink"] = "file";
  output["logger"]["file_path"] = "missing/log.jsonl";
  WriteDefaultConfiguration(output_root, output);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(output_root);
        (void)configuration;
      },
      "内容缺失或无法读取");
}

#ifndef _WIN32
TEST(ConfigurationTest, RejectsSymbolicLinksForConfigurationAndResources) {
  const auto linked_configuration_root = NewRuntimeRoot();
  AddRuntimeFiles(linked_configuration_root);
  CreateDirectory(linked_configuration_root / "configs");
  WriteText(linked_configuration_root / "actual.json",
            ValidConfiguration().dump());
  std::error_code error;
  std::filesystem::create_symlink(
      linked_configuration_root / "actual.json",
      linked_configuration_root / "configs/foundation.json", error);
  ASSERT_FALSE(error) << error.message();
  ASSERT_DEATH(
      {
        const auto configuration =
            LoadFoundationConfig(linked_configuration_root);
        (void)configuration;
      },
      "包含符号链接");

  const auto linked_sample_root = NewRuntimeRoot();
  AddRuntimeFiles(linked_sample_root);
  std::filesystem::create_directory_symlink(linked_sample_root / "samples",
                                            linked_sample_root / "sample_link",
                                            error);
  ASSERT_FALSE(error) << error.message();
  auto linked_sample = ValidConfiguration();
  linked_sample["sample_root"] = "sample_link";
  WriteDefaultConfiguration(linked_sample_root, linked_sample);
  ASSERT_DEATH(
      {
        const auto configuration = LoadFoundationConfig(linked_sample_root);
        (void)configuration;
      },
      "包含符号链接");
}
#endif

}  // namespace
}  // namespace astra
