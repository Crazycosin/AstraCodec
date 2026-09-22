// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/base/configuration.h"
#include "core/base/error.h"
#include "core/base/logger.h"
#include "core/base/sample_manifest.h"
#include "core/base/version.h"

int main(int argc, char* argv[]) {
  if (argc != 2 && argc != 3) {
    astra::Crash(
        astra::ErrorCode::kInvalidArgument,
        "用法：astracodec_verify_samples <runtime_root> [config_path]");
  }
  const auto version = astra::GetVersionInfo();
  if (!astra::IsFfmpegRuntimeCompatible(version)) {
    astra::Crash(astra::ErrorCode::kVersionMismatch,
                 "FFmpeg 运行库与编译头文件不兼容");
  }
  const std::filesystem::path runtime_root(argv[1]);
  std::optional<std::filesystem::path> explicit_config;
  if (argc == 3) {
    explicit_config = std::filesystem::path(argv[2]);
  }
  const auto config =
      astra::LoadFoundationConfig(runtime_root, explicit_config);
  auto logger = astra::CreateLogger(config.logger);
  astra::LogRecord startup;
  startup.node = "sample_verification";
  startup.message = "AstraCodec sample verification startup";
  startup.fields = {{"git_commit", version.git_commit},
                    {"manifest_path", config.manifest_path.string()},
                    {"sample_root", config.sample_root.string()}};
  logger->Log(std::move(startup));
  const auto manifest = astra::LoadSampleManifest(config.manifest_path);
  astra::VerifySampleHashes(manifest, config.sample_root);
  nlohmann::json verified = nlohmann::json::array();
  for (const auto& sample : manifest.samples) {
    verified.push_back({{"id", sample.id},
                        {"filename", sample.filename.string()},
                        {"sha256", sample.sha256}});
  }
  const nlohmann::json report = {
      {"schema_version", 1},
      {"git_commit", version.git_commit},
      {"manifest_path", config.manifest_path.string()},
      {"sample_root", config.sample_root.string()},
      {"verified_samples", std::move(verified)}};
  const std::string output = report.dump() + "\n";
  if (std::fwrite(output.data(), 1, output.size(), stdout) != output.size() ||
      std::fflush(stdout) != 0) {
    astra::Crash(astra::ErrorCode::kWriteFailure, "样本核查报告写入失败");
  }
  logger->Shutdown();
  return 0;
}
