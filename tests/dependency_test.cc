// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include <gtest/gtest.h>

#include "core/base/version.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
#include <libavformat/avformat.h>
#include <libavformat/version.h>
#include <libavutil/avutil.h>
#include <libavutil/version.h>
}

namespace astra {
namespace {

TEST(DependencyTest, ActualFfmpegHeadersAndRuntimeAreCompatible) {
  const auto info = GetVersionInfo();
  EXPECT_EQ(info.libraries[0].header_version, LIBAVFORMAT_VERSION_INT);
  EXPECT_EQ(info.libraries[0].runtime_version, avformat_version());
  EXPECT_EQ(info.libraries[1].header_version, LIBAVCODEC_VERSION_INT);
  EXPECT_EQ(info.libraries[1].runtime_version, avcodec_version());
  EXPECT_EQ(info.libraries[2].header_version, LIBAVUTIL_VERSION_INT);
  EXPECT_EQ(info.libraries[2].runtime_version, avutil_version());
  EXPECT_EQ(info.ffmpeg_version, av_version_info());
  ASSERT_TRUE(IsFfmpegRuntimeCompatible(info));
  for (const auto& library : info.libraries) {
    EXPECT_GT(library.runtime_version, 0U);
    EXPECT_FALSE(library.license.empty());
  }
}

TEST(DependencyTest, H264DecoderAndEncoderCapabilitiesAreQueried) {
  ASSERT_NE(avcodec_find_decoder(AV_CODEC_ID_H264), nullptr);
  // 能力检查读取真实编码器，不执行阶段 1 转码。
  ASSERT_NE(avcodec_find_encoder_by_name("libx264"), nullptr);
}

}  // namespace
}  // namespace astra
