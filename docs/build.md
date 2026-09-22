# 基础工程构建和验证

范围为 S0 Day 1–2，工作目录为项目根目录。已 clone 工程的一键入口已经提供；外层 clone、空项目缓存和 900 秒首次复现验收仍待 S0 Day 3–4 完成。

## 依赖

C++20 编译器、CMake ≥ 3.25、Ninja、Git、pkg-config、Threads、FFmpeg 开发库 `libavformat/libavcodec/libavutil`、fmt pkg-config package ≥ 9，以及 `ffmpeg`、`ffprobe`、jq。质量入口额外需要 ripgrep、clang-format 21.1.6 和 clang-tidy 21.1.6。

GoogleTest 固定 1.17.0，nlohmann JSON 固定 3.12.0。presets 显式选择 `ASTRA_DEPENDENCY_PROVIDER=FETCH`，使用官方 archive 和 SHA256；本地 `.cache/dependencies/` 已有对应 archive 时校验后使用，否则从指定官方地址获取。下载或校验失败终止配置。系统依赖不会由脚本自动安装。

使用预装的相同固定版本时，显式执行 `cmake --preset debug -DASTRA_DEPENDENCY_PROVIDER=SYSTEM`。SYSTEM 与 FETCH 不自动切换。重新运行标准 preset 会重新选择 FETCH。依赖许可证见 [外部依赖记录](../third_party/README.md)。系统库仅查询实际版本，不承诺未经验证的版本组合。

## 已 clone 一键入口

```bash
bash scripts/bootstrap.sh debug
bash scripts/bootstrap.sh release
bash scripts/bootstrap.sh asan
bash scripts/bootstrap.sh tsan
```

入口依次检查主机工具和 pkg-config 包，执行 configure、build、全量 CTest、独立样本核查及最终 version/help。默认使用四个并行构建任务；`ASTRA_BUILD_JOBS` 可设置正整数。报告、前置检查记录和各步骤日志位于 `build/<preset>/test-output/stage0/bootstrap.<id>/`。依赖缺失或执行步骤失败时写入失败状态和退出码，并在 stderr 提供证据目录；缺少 jq 时只能立即输出诊断。此入口不安装系统包，不执行外层 clone，也不代表首次复现的 900 秒目标已经通过。

## 分项构建、测试和 smoke

```bash
bash scripts/run_day1.sh debug
bash scripts/run_day1.sh release
bash scripts/run_day1.sh asan
bash scripts/run_day1.sh tsan
```

`run_day1.sh` 保留为基础回归入口。当前全量 CTest 已包含 manifest、配置、资源路径、实际资产与安装布局核查；脚本最后执行 version/help smoke。它不生成 bootstrap 的分项报告。

单独执行：

```bash
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --preset debug --output-on-failure
bash scripts/run_smoke.sh debug
bash scripts/verify_samples.sh build/debug/astracodec_verify_samples .
```

程序位于 `build/<preset>/astracodec`，资产核查程序位于 `build/<preset>/astracodec_verify_samples`，GoogleTest 程序为 `astra_foundation_tests`。真实 compile database 位于各模式的 `compile_commands.json`。构建信息由 CMake 查询当前 Git HEAD、dirty 状态及工具链生成；源码归档没有 Git 元数据时目前不能配置，归档 provenance 参数属于后续工作。

资产核查程序接受 `runtime_root` 和可选显式配置路径：

```bash
build/debug/astracodec_verify_samples .
build/debug/astracodec_verify_samples . configs/foundation.json
```

`scripts/verify_samples.sh` 先调用该程序进行 schema、路径和 SHA256 核查，再使用 ffprobe/jq 检查 stream、分辨率、像素格式、帧率、time base、时长、帧数、逐帧 CFR、B 帧和音频，最后使用 `ffmpeg -xerror` 解码全部 stream。证据位于 `build/sample-verification/run.<id>/`；CTest 设置自己的证据目录 `build/<preset>/test-output/sample-verification/`。

安装命令把两个程序放入 `bin/`，把配置、manifest、LICENSE 和固定样本放入 `share/astracodec/`。运行安装后的核查程序时，将该 share 目录作为 `runtime_root`。CTest 的 `installed_sample_assets` 每次使用独立、含空格的目录，在非项目工作目录中验证此布局；`bootstrap_missing_dependency` 检查实际缺失依赖的诊断和失败报告。

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

`foundation.yml` 配置 Linux/macOS 的 Debug、Release、ASan/UBSan、TSan，调用 bootstrap 并核查固定样本；Debug 额外执行格式和静态分析，并保存测试产物。当前 Day 2 修改尚未在远端运行，工作流也不包含外层 clone 与空项目缓存计时，不能作为两平台已通过证据。

## 运行记录和问题处理

CTest 原始结果为 `build/<preset>/Testing/Temporary/LastTest.log`；日志测试、样本核查、安装布局和 bootstrap 证据位于 `build/<preset>/test-output/`。媒体生成与首次独立核查证据位于 `build/day2-assets/`。这些目录被忽略，阶段报告保存可核查的结果与路径；完整验收仍需外层复现归档和环境基线。

缺少系统依赖时查看 CMake 的 required package 诊断并准备准确的开发包；禁止替换成假 target。网络不可用时只能显式提供同一 SHA256 的 archive，不跳过校验。应用错误输出 JSON fatal 诊断并立即崩溃，不承诺清理、补写产物或完成异步日志。正常退出才保证 Logger Flush/Shutdown。
