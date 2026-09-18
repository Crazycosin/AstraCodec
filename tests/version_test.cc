// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/version.h"

#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "core/base/build_info.h"

namespace astra {
namespace {

TEST(VersionTest, MetadataMatchesTheConfiguredBuild) {
  const auto info = GetVersionInfo();
  EXPECT_EQ(info.project_version, build::kProjectVersion);
  EXPECT_EQ(info.git_commit, build::kGitCommit);
  EXPECT_EQ(info.git_commit.size(), 40);
  EXPECT_TRUE(info.git_dirty == "true" || info.git_dirty == "false");
  EXPECT_EQ(info.compiler, build::kCompiler);
  EXPECT_EQ(info.platform, build::kPlatform);
  EXPECT_EQ(info.cpu_architecture, build::kCpuArchitecture);
  EXPECT_EQ(info.cpu_logical_cores, std::thread::hardware_concurrency());
}

TEST(VersionTest, VersionOutputContainsAllRequiredFields) {
  const auto text = FormatVersionInfo(GetVersionInfo());
  for (const auto* field :
       {"AstraCodec", "git_commit=", "git_dirty=", "compiler=", "platform=",
        "cpu_architecture=", "cpu_logical_cores=", "ffmpeg=", "libavformat",
        "libavcodec", "libavutil"}) {
    EXPECT_NE(text.find(field), std::string::npos) << field;
  }
}

}  // namespace
}  // namespace astra
