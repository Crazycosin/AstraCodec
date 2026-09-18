// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/error.h"

#include <array>
#include <csignal>

#include <gtest/gtest.h>

namespace astra {
namespace {

TEST(ErrorTest, NamesAreDefinedAndStable) {
  constexpr std::array codes{ErrorCode::kOk,
                             ErrorCode::kInvalidArgument,
                             ErrorCode::kInvalidConfiguration,
                             ErrorCode::kDependencyUnavailable,
                             ErrorCode::kVersionMismatch,
                             ErrorCode::kAssetUnavailable,
                             ErrorCode::kHashMismatch,
                             ErrorCode::kReadFailure,
                             ErrorCode::kWriteFailure,
                             ErrorCode::kLogFailure,
                             ErrorCode::kInternalInvariant};
  for (std::size_t index = 0; index < codes.size(); ++index) {
    EXPECT_EQ(static_cast<std::size_t>(codes[index]), index);
    EXPECT_FALSE(ErrorCodeName(codes[index]).empty());
  }
  EXPECT_EQ(ErrorCodeName(ErrorCode::kInvalidArgument), "invalid_argument");
}

TEST(ErrorTest, CrashEmitsCodeSourceAndTerminatesProcess) {
#ifndef _WIN32
  ASSERT_EXIT(Crash(ErrorCode::kInvalidArgument, "actual error test"),
              testing::KilledBySignal(SIGABRT),
              "\"error_code\":1.*actual error test");
#else
  ASSERT_DEATH(Crash(ErrorCode::kInvalidArgument, "actual error test"),
               "actual error test");
#endif
}

TEST(ErrorTest, UnknownCodeIsAnInvariantFailure) {
  ASSERT_DEATH(ErrorCodeName(static_cast<ErrorCode>(999)), "未知 ErrorCode");
}

}  // namespace
}  // namespace astra
