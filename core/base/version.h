// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#ifndef ASTRACODEC_CORE_BASE_VERSION_H_
#define ASTRACODEC_CORE_BASE_VERSION_H_

#include <array>
#include <string>

namespace astra {

struct LibraryVersion {
  std::string name;
  unsigned int header_version;
  unsigned int runtime_version;
  std::string license;
};

struct VersionInfo {
  std::string project_version;
  std::string git_commit;
  std::string git_dirty;
  std::string compiler;
  std::string platform;
  std::string cpu_architecture;
  unsigned int cpu_logical_cores;
  std::string ffmpeg_version;
  std::array<LibraryVersion, 3> libraries;
};

VersionInfo GetVersionInfo();
std::string FormatVersionInfo(const VersionInfo& info);
bool IsFfmpegRuntimeCompatible(const VersionInfo& info);

}  // namespace astra

#endif  // ASTRACODEC_CORE_BASE_VERSION_H_
