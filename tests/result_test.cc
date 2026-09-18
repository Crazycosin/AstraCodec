// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/result.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

namespace astra {
namespace {

TEST(ResultTest, SuccessfulValueAndCopyAreComplete) {
  const auto result = Result<std::string>::Success("actual value");
  auto copy = result;
  EXPECT_EQ(result.value(), "actual value");
  EXPECT_EQ(copy.value(), result.value());
  copy.value() = "modified copy";
  EXPECT_EQ(result.value(), "actual value");
}

TEST(ResultTest, ExclusiveOwnershipMovesAndIsConsumedOnce) {
  static_assert(!std::is_copy_constructible_v<Result<std::unique_ptr<int>>>);
  auto result =
      Result<std::unique_ptr<int>>::Success(std::make_unique<int>(42));
  auto moved = std::move(result);
  const auto pointer = std::move(moved).TakeValue();
  ASSERT_NE(pointer, nullptr);
  EXPECT_EQ(*pointer, 42);
  // 故意调用已转移来源，验证崩溃边界。
  // NOLINTBEGIN(bugprone-use-after-move)
  ASSERT_DEATH(result.value(), "Result 的值已经转移");
  ASSERT_DEATH(std::move(moved).TakeValue(), "Result 的值已经转移");
  // NOLINTEND(bugprone-use-after-move)
}

TEST(ResultTest, MoveAssignmentTransfersOwnership) {
  auto left = Result<std::unique_ptr<int>>::Success(std::make_unique<int>(1));
  auto right = Result<std::unique_ptr<int>>::Success(std::make_unique<int>(2));
  left = std::move(right);
  EXPECT_EQ(*left.value(), 2);
  // NOLINTBEGIN(bugprone-use-after-move)
  ASSERT_DEATH(right.value(), "Result 的值已经转移");
  // NOLINTEND(bugprone-use-after-move)
}

TEST(ResultTest, VoidSuccessIsAvailable) {
  [[maybe_unused]] const auto result = Result<void>::Success();
}

TEST(ResultTest, ReusingTransferredSourcesTerminatesAtCopyOrMove) {
  // 本用例专门验证已转移来源的非法使用和复制，均在真实子进程中执行。
  // NOLINTBEGIN(bugprone-use-after-move,performance-unnecessary-copy-initialization)
  auto source = Result<std::string>::Success("owned value");
  [[maybe_unused]] const auto value = std::move(source).TakeValue();
  ASSERT_DEATH(
      {
        auto copy = source;
        (void)copy;
      },
      "Result 的值已经转移");
  ASSERT_DEATH(
      {
        auto moved = std::move(source);
        (void)moved;
      },
      "Result 的值已经转移");
  auto target = Result<std::string>::Success("target");
  ASSERT_DEATH(target = source, "Result 的值已经转移");
  ASSERT_DEATH(target = std::move(source), "Result 的值已经转移");
  // NOLINTEND(bugprone-use-after-move,performance-unnecessary-copy-initialization)
}

TEST(ResultTest, FailureConstructionTerminatesAtTheFactory) {
  ASSERT_DEATH((void)Result<int>::Failure(ErrorCode::kReadFailure,
                                          "actual read failure"),
               "\"error_code\":7.*actual read failure");
  ASSERT_DEATH((void)Result<void>::Failure(ErrorCode::kWriteFailure,
                                           "actual write failure"),
               "\"error_code\":8.*actual write failure");
}

}  // namespace
}  // namespace astra
