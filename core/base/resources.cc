// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/resources.h"

#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>

#include "core/base/error.h"

namespace astra {
namespace {

void ValidatePathSyntax(const std::filesystem::path& path,
                        bool allow_absolute) {
  if (path.empty() || path.native().find('\0') != std::string::npos) {
    Crash(ErrorCode::kAssetUnavailable, "资源路径为空或包含 null");
  }
  if (!allow_absolute && (path.is_absolute() || path.has_root_path())) {
    Crash(ErrorCode::kAssetUnavailable, "资源路径必须相对于资源目录");
  }
  for (const auto& component : path) {
    if (component == "..") {
      Crash(ErrorCode::kAssetUnavailable, "资源路径包含禁止的父目录引用");
    }
  }
}

// 参数名称明确表示包含关系，两个 filesystem path 均为必要输入。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool IsWithin(const std::filesystem::path& root,
              const std::filesystem::path& path) {
  auto path_iterator = path.begin();
  for (const auto& component : root) {
    if (path_iterator == path.end() || *path_iterator != component)
      return false;
    ++path_iterator;
  }
  return true;
}

void ValidateExistingComponents(const std::filesystem::path& path,
                                bool allow_missing_file) {
  auto current = path.root_path();
  const auto relative = path.relative_path();
  for (auto iterator = relative.begin(); iterator != relative.end();
       ++iterator) {
    if (*iterator == ".") continue;
    current /= *iterator;
    const bool is_last = std::next(iterator) == relative.end();
    std::error_code error;
    const auto status = std::filesystem::symlink_status(current, error);
    if (is_last && allow_missing_file &&
        (error == std::errc::no_such_file_or_directory ||
         (!error && status.type() == std::filesystem::file_type::not_found))) {
      return;
    }
    if (error || !std::filesystem::exists(status)) {
      Crash(ErrorCode::kAssetUnavailable, "资源路径内容缺失或无法读取");
    }
    if (std::filesystem::is_symlink(status)) {
      Crash(ErrorCode::kAssetUnavailable, "资源路径包含符号链接");
    }
    if (!is_last && !std::filesystem::is_directory(status)) {
      Crash(ErrorCode::kAssetUnavailable, "资源路径的父目录类型错误");
    }
  }
}

std::filesystem::path CanonicalExisting(const std::filesystem::path& path) {
  ValidateExistingComponents(path, false);
  std::error_code error;
  const auto canonical = std::filesystem::canonical(path, error);
  if (error) Crash(ErrorCode::kAssetUnavailable, "资源路径无法 canonical");
  return canonical;
}

std::filesystem::path ResolveExisting(const std::filesystem::path& root,
                                      const std::filesystem::path& relative,
                                      bool require_directory) {
  ValidatePathSyntax(relative, false);
  const auto path = CanonicalExisting(root / relative);
  if (!IsWithin(root, path)) {
    Crash(ErrorCode::kAssetUnavailable, "资源路径超出资源目录");
  }
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error ||
      (require_directory ? !std::filesystem::is_directory(status)
                         : !std::filesystem::is_regular_file(status))) {
    Crash(ErrorCode::kAssetUnavailable, "资源路径的文件类型错误");
  }
  return path;
}

}  // namespace

ResourceManager::ResourceManager(const std::filesystem::path& root) {
  ValidatePathSyntax(root, true);
  std::error_code error;
  const auto absolute = std::filesystem::absolute(root, error);
  if (error) Crash(ErrorCode::kAssetUnavailable, "资源目录无法转换为绝对路径");
  root_ = CanonicalExisting(absolute);
  const auto status = std::filesystem::status(root_, error);
  if (error || !std::filesystem::is_directory(status)) {
    Crash(ErrorCode::kAssetUnavailable, "资源目录不存在或类型错误");
  }
}

const std::filesystem::path& ResourceManager::Root() const {
  if (root_.empty()) {
    Crash(ErrorCode::kInternalInvariant, "ResourceManager 已经转移所有权");
  }
  return root_;
}

std::filesystem::path ResourceManager::ResolveFile(
    const std::filesystem::path& relative) const {
  return ResolveExisting(Root(), relative, false);
}

std::filesystem::path ResourceManager::ResolveDirectory(
    const std::filesystem::path& relative) const {
  return ResolveExisting(Root(), relative, true);
}

std::filesystem::path ResourceManager::ResolveOutputFile(
    const std::filesystem::path& relative) const {
  ValidatePathSyntax(relative, false);
  if (relative.filename().empty() || relative.filename() == ".") {
    Crash(ErrorCode::kAssetUnavailable, "输出路径必须指定文件名");
  }
  const auto path = Root() / relative;
  ValidateExistingComponents(path, true);
  const auto parent = CanonicalExisting(path.parent_path());
  if (!IsWithin(Root(), parent)) {
    Crash(ErrorCode::kAssetUnavailable, "输出路径超出资源目录");
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory ||
      (!error && status.type() == std::filesystem::file_type::not_found)) {
    return parent / path.filename();
  }
  if (error || !std::filesystem::is_regular_file(status)) {
    Crash(ErrorCode::kAssetUnavailable, "输出文件类型错误或无法读取");
  }
  return parent / path.filename();
}

}  // namespace astra
