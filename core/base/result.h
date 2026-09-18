// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#ifndef ASTRACODEC_CORE_BASE_RESULT_H_
#define ASTRACODEC_CORE_BASE_RESULT_H_

#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "core/base/error.h"

namespace astra {

// D03 规定失败构造立即崩溃，Result 不保存可继续传播的错误状态。
template <typename T>
class [[nodiscard]] Result {
 public:
  static Result Success(T value) { return Result(std::move(value)); }
  [[noreturn]] static Result Failure(
      ErrorCode code, std::string_view message,
      std::source_location location = std::source_location::current()) {
    Crash(code, message, location);
  }

  Result(const Result& other)
    requires std::is_copy_constructible_v<T>
      : value_(other.value()) {}
  Result& operator=(const Result& other)
    requires(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>)
  {
    other.CheckValue();
    value_ = other.value_;
    return *this;
  }
  Result(Result&& other) noexcept : value_(MoveStorage(other)) {}
  Result& operator=(Result&& other) noexcept {
    other.CheckValue();
    if (this != &other) {
      value_ = std::move(other.value_);
      other.value_.reset();
    }
    return *this;
  }
  ~Result() = default;

  // 本地 Google 指南允许与成员一致的 accessor 名称。
  const T& value() const& {  // NOLINT(readability-identifier-naming)
    if (!value_.has_value())
      Crash(ErrorCode::kInternalInvariant, "Result 的值已经转移");
    return *value_;
  }
  T& value() & {  // NOLINT(readability-identifier-naming)
    if (!value_.has_value())
      Crash(ErrorCode::kInternalInvariant, "Result 的值已经转移");
    return *value_;
  }
  T TakeValue() && {
    T result = std::move(value());
    value_.reset();
    return result;
  }

 private:
  explicit Result(T value) : value_(std::move(value)) {}
  static std::optional<T> MoveStorage(Result& other) {
    other.CheckValue();
    auto storage = std::move(other.value_);
    other.value_.reset();
    return storage;
  }
  void CheckValue() const {
    if (!value_.has_value()) {
      Crash(ErrorCode::kInternalInvariant, "Result 的值已经转移");
    }
  }
  std::optional<T> value_;
};

template <>
class [[nodiscard]] Result<void> {
 public:
  static Result Success() { return Result(); }
  [[noreturn]] static Result Failure(
      ErrorCode code, std::string_view message,
      std::source_location location = std::source_location::current()) {
    Crash(code, message, location);
  }
  Result(const Result&) = default;
  Result& operator=(const Result&) = default;
  Result(Result&&) = default;
  Result& operator=(Result&&) = default;
  ~Result() = default;

 private:
  Result() = default;
};

}  // namespace astra

#endif  // ASTRACODEC_CORE_BASE_RESULT_H_
