// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/version.h"

#include <string>
#include <thread>

#include <fmt/format.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
#include <libavformat/avformat.h>
#include <libavformat/version.h>
#include <libavutil/avutil.h>
#include <libavutil/version.h>
}

#include "core/base/build_info.h"

namespace astra {
namespace {

std::string FormatLibraryVersion(unsigned int version) {
  return fmt::format("{}.{}.{}", AV_VERSION_MAJOR(version),
                     AV_VERSION_MINOR(version), AV_VERSION_MICRO(version));
}

}  // namespace

VersionInfo GetVersionInfo() {
  return {build::kProjectVersion,
          build::kGitCommit,
          build::kGitDirty,
          build::kCompiler,
          build::kPlatform,
          build::kCpuArchitecture,
          std::thread::hardware_concurrency(),
          av_version_info(),
          {{{"libavformat", LIBAVFORMAT_VERSION_INT, avformat_version(),
             avformat_license()},
            {"libavcodec", LIBAVCODEC_VERSION_INT, avcodec_version(),
             avcodec_license()},
            {"libavutil", LIBAVUTIL_VERSION_INT, avutil_version(),
             avutil_license()}}}};
}

bool IsFfmpegRuntimeCompatible(const VersionInfo& info) {
  for (const auto& library : info.libraries) {
    if (AV_VERSION_MAJOR(library.header_version) !=
            AV_VERSION_MAJOR(library.runtime_version) ||
        library.runtime_version < library.header_version) {
      return false;
    }
  }
  return true;
}

std::string FormatVersionInfo(const VersionInfo& info) {
  std::string text = fmt::format(
      "AstraCodec {}\ngit_commit={}\ngit_dirty={}\ncompiler={}\nplatform={}\n"
      "cpu_architecture={}\ncpu_logical_cores={}\nffmpeg={}\n",
      info.project_version, info.git_commit, info.git_dirty, info.compiler,
      info.platform, info.cpu_architecture,
      info.cpu_logical_cores == 0 ? "unavailable"
                                  : std::to_string(info.cpu_logical_cores),
      info.ffmpeg_version);
  for (const auto& library : info.libraries) {
    text += fmt::format("{} header={} runtime={} license={}\n", library.name,
                        FormatLibraryVersion(library.header_version),
                        FormatLibraryVersion(library.runtime_version),
                        library.license);
  }
  return text;
}

}  // namespace astra
