# 基础工程构建和验证

范围为 S0 Day 1，工作目录为项目根目录。完整 clone、样本准备和 900 秒首次复现入口尚未提供；以下命令验证已准备主机上的基础工程。

## 依赖

C++20 编译器、CMake ≥ 3.25、Ninja、Git、pkg-config、Threads、FFmpeg 开发库 `libavformat/libavcodec/libavutil`、fmt pkg-config package ≥ 9。质量入口额外需要 ripgrep、clang-format 21.1.6 和 clang-tidy 21.1.6。

GoogleTest 固定 1.17.0，nlohmann JSON 固定 3.12.0。presets 显式选择 `ASTRA_DEPENDENCY_PROVIDER=FETCH`，使用官方 archive 和 SHA256；本地 `.cache/dependencies/` 已有对应 archive 时校验后使用，否则从指定官方地址获取。下载或校验失败终止配置。系统依赖不会由脚本自动安装。

使用预装的相同固定版本时，显式执行 `cmake --preset debug -DASTRA_DEPENDENCY_PROVIDER=SYSTEM`。SYSTEM 与 FETCH 不自动切换。重新运行标准 preset 会重新选择 FETCH。依赖许可证见 [外部依赖记录](../third_party/README.md)。系统库仅查询实际版本，不承诺未经验证的版本组合。

## 构建、测试和 smoke

```bash
bash scripts/run_day1.sh debug
bash scripts/run_day1.sh release
bash scripts/run_day1.sh asan
bash scripts/run_day1.sh tsan
```

每次执行 configure → build → 全量 CTest → version/help smoke；任一步失败立即返回非零。默认四个并行构建任务，`ASTRA_BUILD_JOBS` 可显式调整。此入口不执行 clone、安装系统包或验证媒体资产，不等同阶段 0 的 bootstrap。

单独执行：

```bash
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --preset debug --output-on-failure
bash scripts/run_smoke.sh debug
```

程序位于 `build/<preset>/astracodec`，测试为 `astra_foundation_tests`。真实 compile database 位于各模式的 `compile_commands.json`。构建信息由 CMake 查询当前 Git HEAD、dirty 状态及工具链生成；源码归档没有 Git 元数据时目前不能配置，归档 provenance 参数属于后续工作。

ASan 与 UBSan 组合使用；TSan 独立使用，均不关闭失败报告。CTest sanitizer presets 设置 `halt_on_error=1`，UBSan 同时输出调用位置。工具支持条件为 Linux/macOS，Windows 尚未验证。项目代码启用 `-fno-exceptions` 和 `JSON_NOEXCEPTION`，第一方 `-Wall -Wextra -Wpedantic -Werror`；第三方库没有全部重新编译为 sanitizer 模式，不能宣称已检查全部系统库内部行为。

## 格式和静态分析

```bash
ASTRA_CLANG_FORMAT=.cache/quality/bin/clang-format \
ASTRA_CLANG_TIDY=.cache/quality/bin/clang-tidy \
bash scripts/check_quality.sh debug
```

工具通过显式环境变量选择，未设置时使用 PATH。检查不修改源码，覆盖 `core/apps/tests` 第一方 `.h/.cc`；clang-tidy 使用真实 compile database。独立 LLVM 在 macOS 上通过 `xcrun` 查询 SDK 和 libc++ include，通过 pkg-config 查询 fmt include，不在工程中记录本机专用路径。使用其他 fmt 安装方式时须提供对应 pkg-config 信息。

`.clang-format` 基于 Google，按关联头文件、C 标准头、C++ 标准头、第三方和项目头分组。`.clang-tidy` 保持 Google、命名、bugprone、performance 等检查；Google include guard 的末尾 `_H_` 使用精确命名例外。本地指南允许 `value()` accessor，注释中的精确 NOLINT 仅覆盖该项。Result 异常测试故意使用已转移来源，相关测试行记录相应例外，不影响生产代码检查。

## hooks 和 CI

仓库 hooks 尚未自动启用。希望启用时在当前仓库执行：

```bash
git config --local core.hooksPath .githooks
```

pre-commit 只读检查 Git 暂存版本的 C++ 格式；pre-push 执行 Debug 基础链和质量入口。需要把质量工具放入 PATH 或设置上述环境变量。hooks 不修改文件，不修改全局 Git 配置。

`foundation.yml` 配置 Linux/macOS 的 Debug、Release、ASan/UBSan、TSan，Debug 额外执行格式和静态分析，并保存测试产物。远端 CI 尚未运行，工作流不包含完整样本/冷环境验收，不能作为两平台已通过证据。

## 运行记录和问题处理

CTest 原始结果为 `build/<preset>/Testing/Temporary/LastTest.log`；真实日志测试文件为 `build/<preset>/test-output/*.jsonl`。各目录被忽略，阶段报告保存精简结果；后续完整验收需增加原始证据归档和环境基线。

缺少系统依赖时查看 CMake 的 required package 诊断并准备准确的开发包；禁止替换成假 target。网络不可用时只能显式提供同一 SHA256 的 archive，不跳过校验。应用错误输出 JSON fatal 诊断并立即崩溃，不承诺清理、补写产物或完成异步日志。正常退出才保证 Logger Flush/Shutdown。
