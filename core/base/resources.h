// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#ifndef ASTRACODEC_CORE_BASE_RESOURCES_H_
#define ASTRACODEC_CORE_BASE_RESOURCES_H_

#include <filesystem>

namespace astra {

// 保存真实资源目录。只读方法可并发调用；调用期间目录内容须保持不变。
// 路径错误、缺失、符号链接和错误文件类型均立即崩溃。
class ResourceManager {
 public:
  explicit ResourceManager(const std::filesystem::path& root);
  ResourceManager(const ResourceManager&) = default;
  ResourceManager& operator=(const ResourceManager&) = default;
  ResourceManager(ResourceManager&&) = delete;
  ResourceManager& operator=(ResourceManager&&) = delete;
  ~ResourceManager() = default;

  [[nodiscard]] const std::filesystem::path& Root() const;
  [[nodiscard]] std::filesystem::path ResolveFile(
      const std::filesystem::path& relative) const;
  [[nodiscard]] std::filesystem::path ResolveDirectory(
      const std::filesystem::path& relative) const;
  // 输出文件可尚未创建，父目录必须存在；已存在的输出必须为普通文件。
  [[nodiscard]] std::filesystem::path ResolveOutputFile(
      const std::filesystem::path& relative) const;

 private:
  std::filesystem::path root_;
};

}  // namespace astra

#endif  // ASTRACODEC_CORE_BASE_RESOURCES_H_
