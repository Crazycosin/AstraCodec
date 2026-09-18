// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "core/base/error.h"
#include "core/base/logger.h"
#include "core/base/version.h"

int main(int argc, char* argv[]) {
  const auto info = astra::GetVersionInfo();
  if (!astra::IsFfmpegRuntimeCompatible(info)) {
    astra::Crash(astra::ErrorCode::kVersionMismatch,
                 "FFmpeg 运行库与编译头文件不兼容");
  }
  auto logger = astra::CreateLogger({});
  astra::LogRecord startup;
  startup.node = "startup";
  startup.message = "AstraCodec startup";
  startup.fields = {
      {"version", info.project_version},
      {"git_commit", info.git_commit},
      {"git_dirty", info.git_dirty},
      {"compiler", info.compiler},
      {"platform", info.platform},
      {"cpu_architecture", info.cpu_architecture},
      {"cpu_logical_cores", info.cpu_logical_cores == 0
                                ? "unavailable"
                                : std::to_string(info.cpu_logical_cores)},
      {"ffmpeg", info.ffmpeg_version}};
  logger->Log(std::move(startup));

  if (argc != 2) {
    astra::Crash(astra::ErrorCode::kInvalidArgument,
                 "请使用 --version 或 --help");
  }
  const std::string_view argument(argv[1]);
  std::string output;
  if (argument == "--version") {
    output = astra::FormatVersionInfo(info);
  } else if (argument == "--help") {
    output =
        "AstraCodec 工程基础程序\n用法：astracodec --version | --help\n"
        "  --version  显示版本、Git、编译器、平台、CPU 和 FFmpeg 信息\n"
        "  --help     显示帮助\n";
  } else {
    astra::Crash(astra::ErrorCode::kInvalidArgument, "未知程序参数");
  }
  if (std::fwrite(output.data(), 1, output.size(), stdout) != output.size() ||
      std::fflush(stdout) != 0) {
    astra::Crash(astra::ErrorCode::kWriteFailure, "标准输出写入失败");
  }
  logger->Shutdown();
  return 0;
}
