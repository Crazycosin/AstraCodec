// Copyright 2026 AstraCodec 项目作者，遵循项目 LICENSE。
#include "core/base/resources.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

namespace astra {
namespace {

std::filesystem::path NewResourceRoot() {
  static std::atomic<unsigned int> sequence{0};
  const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root =
      std::filesystem::path(ASTRA_TEST_OUTPUT_ROOT) /
      ("resources-" + std::to_string(time) + "-" + std::to_string(sequence++));
  std::error_code error;
  std::filesystem::create_directories(root / "data", error);
  if (error) std::abort();
  std::filesystem::create_directories(root / "output", error);
  if (error) std::abort();
  return root;
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) std::abort();
  if (std::fwrite(text.data(), 1, text.size(), file) != text.size()) {
    std::abort();
  }
  if (std::fclose(file) != 0) std::abort();
}

class CurrentPathGuard {
 public:
  CurrentPathGuard() {
    std::error_code error;
    path_ = std::filesystem::current_path(error);
    if (error) std::abort();
  }
  CurrentPathGuard(const CurrentPathGuard&) = delete;
  CurrentPathGuard& operator=(const CurrentPathGuard&) = delete;
  CurrentPathGuard(CurrentPathGuard&&) = delete;
  CurrentPathGuard& operator=(CurrentPathGuard&&) = delete;
  ~CurrentPathGuard() {
    std::error_code error;
    std::filesystem::current_path(path_, error);
    if (error) std::abort();
  }

 private:
  std::filesystem::path path_;
};

void ChangeCurrentPath(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::current_path(path, error);
  if (error) std::abort();
}

TEST(ResourceManagerTest, ResolvesValidatedFilesDirectoriesAndOutputs) {
  const auto root = NewResourceRoot();
  WriteText(root / "data/input.bin", "actual bytes");
  WriteText(root / "output/existing.jsonl", "existing\n");
  const ResourceManager resources(root);
  EXPECT_TRUE(resources.Root().is_absolute());
  EXPECT_EQ(resources.ResolveDirectory("data"), resources.Root() / "data");
  EXPECT_EQ(resources.ResolveFile("data/input.bin"),
            resources.Root() / "data/input.bin");
  EXPECT_EQ(resources.ResolveOutputFile("output/new.jsonl"),
            resources.Root() / "output/new.jsonl");
  EXPECT_EQ(resources.ResolveOutputFile("output/existing.jsonl"),
            resources.Root() / "output/existing.jsonl");
}

TEST(ResourceManagerTest, StoredRootDoesNotDependOnLaterCurrentDirectory) {
  const auto root = NewResourceRoot();
  WriteText(root / "data/input.bin", "actual bytes");
  const auto relative_root =
      std::filesystem::relative(root, std::filesystem::current_path());
  const ResourceManager resources(relative_root);
  const auto other_directory = NewResourceRoot();
  {
    CurrentPathGuard guard;
    ChangeCurrentPath(other_directory);
    EXPECT_EQ(resources.ResolveFile("data/input.bin"),
              resources.Root() / "data/input.bin");
  }
}

TEST(ResourceManagerTest, RejectsTraversalMissingEntriesAndWrongTypes) {
  const auto root = NewResourceRoot();
  WriteText(root / "data/input.bin", "actual bytes");
  const ResourceManager resources(root);
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveFile("../outside.bin");
        (void)path;
      },
      "禁止的父目录引用");
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveFile(root / "data/input.bin");
        (void)path;
      },
      "必须相对于资源目录");
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveFile("data/missing.bin");
        (void)path;
      },
      "内容缺失或无法读取");
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveFile("data");
        (void)path;
      },
      "文件类型错误");
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveDirectory("data/input.bin");
        (void)path;
      },
      "文件类型错误");
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveOutputFile("missing/result.jsonl");
        (void)path;
      },
      "内容缺失或无法读取");
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveOutputFile("output/");
        (void)path;
      },
      "必须指定文件名");
}

#ifndef _WIN32
TEST(ResourceManagerTest, RejectsSymbolicLinksAtEveryResourceBoundary) {
  const auto root = NewResourceRoot();
  WriteText(root / "data/input.bin", "actual bytes");
  std::error_code error;
  std::filesystem::create_symlink(root / "data/input.bin", root / "linked_file",
                                  error);
  ASSERT_FALSE(error) << error.message();
  std::filesystem::create_directory_symlink(root / "data",
                                            root / "linked_directory", error);
  ASSERT_FALSE(error) << error.message();
  const ResourceManager resources(root);
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveFile("linked_file");
        (void)path;
      },
      "包含符号链接");
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveDirectory("linked_directory");
        (void)path;
      },
      "包含符号链接");
  ASSERT_DEATH(
      {
        const auto path = resources.ResolveOutputFile("linked_file");
        (void)path;
      },
      "包含符号链接");
  const auto root_link =
      root.parent_path() / (root.filename().string() + "-link");
  std::filesystem::create_directory_symlink(root, root_link, error);
  ASSERT_FALSE(error) << error.message();
  ASSERT_DEATH(
      {
        const ResourceManager linked_resources(root_link);
        (void)linked_resources;
      },
      "包含符号链接");
}
#endif

TEST(ResourceManagerTest, ConstructorRejectsMissingOrFileRoot) {
  const auto root = NewResourceRoot();
  WriteText(root / "root.bin", "actual bytes");
  ASSERT_DEATH(
      {
        const ResourceManager resources(root / "missing");
        (void)resources;
      },
      "内容缺失或无法读取");
  ASSERT_DEATH(
      {
        const ResourceManager resources(root / "root.bin");
        (void)resources;
      },
      "目录不存在或类型错误");
}

}  // namespace
}  // namespace astra
