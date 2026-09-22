// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#ifndef ASTRACODEC_CORE_BASE_CONFIGURATION_H_
#define ASTRACODEC_CORE_BASE_CONFIGURATION_H_

#include <filesystem>
#include <optional>

#include "core/base/logger.h"

namespace astra {

struct FoundationConfig {
  int schema_version = 1;
  LoggerConfig logger;
  std::filesystem::path sample_root;
  std::filesystem::path manifest_path;
};

// 所有资产和输出路径基于 runtime_root。显式配置优先且必须存在。
// 默认 configs/foundation.json 缺失时使用固定默认值，其它错误立即崩溃。
[[nodiscard]] FoundationConfig LoadFoundationConfig(
    const std::filesystem::path& runtime_root,
    const std::optional<std::filesystem::path>& explicit_config = std::nullopt);

}  // namespace astra

#endif  // ASTRACODEC_CORE_BASE_CONFIGURATION_H_
