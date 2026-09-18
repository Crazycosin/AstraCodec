# S0 Day 1 开发报告

日期：2026-09-18。范围：用户授权的 Day 1 基础工程开发。状态：实现及本机四种配置验证已完成；格式、静态分析和独立新目录构建通过。阶段 0 尚未整体验收。

## 实施结果

建立 `cmake/core/apps/tests/third_party/samples/docs` 基础目录、target-based CMake、Debug/Release/ASan/TSan presets、GoogleTest/CTest、真实 compile database 和 Google 风格 dot 配置。提供已准备主机的 `run_day1`、版本/帮助 smoke、只读质量检查、可选 hooks 和两平台 CI 配置。

Result/ErrorCode、同步 Crash、基础异步 Logger、VersionInfo 与 `astracodec --version/--help` 已实现。用户确认 D03：“所有错误均按目录规则崩溃”。Result 的失败工厂立即终止，不返回错误状态；正常路径采用 RAII 和明确所有权。错误用例使用实际子进程，不以崩溃证明资源清理。

C++ project standards 技能影响本次工程：采用 target 编译/链接配置、C++20、抽象 Logger、有限异步队列、真实 GoogleTest、格式/命名/静态分析、独立 sanitizer 和明确的未覆盖记录。本地 Google 指南的 accessor 与 include guard 规则通过精确例外保留。

## 实际环境

| 项目 | 本机查询值 |
| --- | --- |
| OS / CPU | macOS 15.7.7，x86_64，16 logical cores |
| 编译器 | AppleClang 17.0.0.17000604 |
| CMake / Ninja / pkg-config | 4.4.2 / 1.13.2 / 3.0.5 |
| FFmpeg | 9.0.1 |
| libavformat header/runtime | 63.1.101 / 63.1.101 |
| libavcodec header/runtime | 63.1.101 / 63.1.101 |
| libavutil header/runtime | 61.1.101 / 61.1.101 |
| FFmpeg 实际许可证返回值 | GPL version 3 or later |
| fmt | 12.2.0，同一 pkg-config target 探测头文件与库 |
| GoogleTest / nlohmann JSON | 1.17.0 / 3.12.0，固定官方 archive 与 SHA256 |
| clang-format / clang-tidy | 21.1.6 / LLVM 21.1.6，项目隔离环境 |
| 验证时分支 / HEAD | feature/stage-0-foundation / 3b1fad7409750dd8de491e139fea69db0320604b |
| 验证时 Git 状态 | dirty=true；验证针对该 HEAD 的工作区实现；阶段标签未创建 |

这是一组本机验证记录，不代表完整 D02/D05 平台矩阵已经批准。系统安装保持不变；依赖 archive 和质量工具保存在忽略目录。真实 H.264 decoder 和 libx264 encoder 能力查询通过，没有项目媒体转码。

报告保存验证时的构建来源；Day 1 发布提交通过当前分支的 Git 历史查询，提交说明为 `s0d1任务`。发布提交不代表阶段 0 已验收或阶段标签已创建。

## 测试登记和通过条件

登记 22 项 GoogleTest 与 4 项 CLI 检查，共 26 项。CTest 标签为 foundation 24 项、smoke 2 项；没有过滤掉应执行测试。

| 类型 | 实际验证 |
| --- | --- |
| 依赖 2 项 | FFmpeg 真实链接、header/runtime/license 查询，实际 H.264/libx264 能力 |
| ErrorCode 3 项 | 枚举数值/名称、真实 SIGABRT 和 JSON 操作位置、未知枚举立即终止 |
| Result 6 项 | 成功与独立复制、unique_ptr 所有权、移动赋值、void、消费来源重复使用、失败工厂立即终止 |
| Logger 9 项 | 实际 JSONL 写入与 escaping、过滤/pts/dts、4 producer 各 100 条及各自顺序、5 次正常服务生命周期、4 producer 每轮同步 Log/Flush 后读取文件、容量/配置/open/限速/错误上下文和停止后使用 |
| VersionInfo 2 项 | 同一真实构建来源与环境、必需版本字段 |
| CLI 4 项 | version/help 成功且 stdout/stderr 分离；真实非法参数与关闭 stdout 后异常终止，错误码为 1/8 |

并发 Flush 用例每轮先完成所有 producer 的 Log，通过 barrier 后分别 Flush 并读取实际文件断言本轮记录已存在，全部读取结束才开始下一轮写入。它检查 Flush 返回时的交付状态，最终另检查 200 条完整记录和重复 Shutdown。没有任意 sleep、mock、伪造库或假成功。

四种模式的已准备主机基础链使用：

```bash
bash scripts/run_day1.sh debug
bash scripts/run_day1.sh release
bash scripts/run_day1.sh asan
bash scripts/run_day1.sh tsan
```

| 配置 | 最新完整基础链 | CTest wall time |
| --- | --- | --- |
| Debug | configure/build、26/26、version/help 通过 | 10.12 秒 |
| Release | configure/build、26/26、version/help 通过 | 9.19 秒 |
| ASan/UBSan | configure/build、26/26、version/help 通过 | 12.34 秒 |
| TSan | configure/build、26/26、version/help 通过 | 25.67 秒 |

上述测试并行运行于同一主机，wall time 为 CTest 自身报告，不是性能基线或 900 秒完整复现结果。

项目第一方启用 `-Werror`、`-fno-exceptions` 和 `JSON_NOEXCEPTION`。ASan/UBSan 组合、TSan 单独构建；CTest 设置相应 `halt_on_error=1`。测试通过仅表示这些实际执行路径未报告相应问题。macOS 本次未单独执行 LSan/等效泄漏测量或 OS 线程/句柄计数，不能宣称泄漏与资源检查全部完成。系统 FFmpeg/fmt 与 GoogleTest 没有全部重新编译为 sanitizer 模式。

## 质量、复核和证据

质量入口使用实际 Debug compile database：

```bash
ASTRA_CLANG_FORMAT=.cache/quality/bin/clang-format \
ASTRA_CLANG_TIDY=.cache/quality/bin/clang-tidy \
bash scripts/check_quality.sh debug
```

clang-format 检查 13 个第一方文件通过，clang-tidy 检查 9 个翻译单元及关联第一方头文件通过，质量入口退出为 0。生成目录、第三方与工具链不作为项目诊断范围。错误子进程测试中故意使用已消费来源的精确 NOLINT 有中文理由，不关闭生产路径检查。

独立目录检查为 `cmake --preset debug -B build/day1-clean-final`、构建及全量 CTest，26/26 通过，CTest wall time 为 6.25 秒。该目录从没有项目构建产物的状态创建，使用已准备系统依赖和本地 archive，不能替代空项目缓存、真实 clone 或 900 秒验收。

原始证据保存于 `build/<preset>/Testing/Temporary/LastTest.log`、`build/<preset>/day1-verification.log`、`build/<preset>/test-output/*.jsonl`；质量证据为 `build/debug/day1-quality.log`，独立目录结果为 `build/day1-clean-final/Testing/Temporary/LastTest.log`。这些目录被忽略，尚未形成跨机器原始证据归档。

独立只读审查覆盖 Result 状态、Logger 并发请求与正常 Shutdown、崩溃诊断和 stdout 错误。Source DOCX SHA256 为 `29c1dffd22f0b440e0c47c41facd84e6f05c7d0b2e038635efd05297c90b6763`；STAGE-1.md SHA256 为 `57440c82078ff647ebf2c6e890b2e16eb65aa3db838c31288b0d5f15bae7e81f`，均保持不变。用户已有 `.gitignore`、本地规范和参考配置保持不变。

当前 `.gitignore` 排除来源 DOCX 和 `STAGE-*.md`；本次保留该规则。完整阶段移交时还需确认计划与来源资料的版本保存方式。

## 阶段追溯与后续输入

| 要求 | Day 1 状态 |
| --- | --- |
| R01/R02/R03/R07/R09 | 基础目录、presets、真实依赖、CTest 和 CLI smoke 已提供；完整 bootstrap/清洁矩阵继续待执行 |
| R04/R05/R06 | Result/ErrorCode、基础 Logger、版本 CLI 本机通过；适用错误规则为 D03 |
| R16/R17 | 质量与 sanitizer 本机执行；hooks 未自动启用，CI 未远端执行，完整异常/资源项目尚未完成 |
| R08/R10/R11/R12/R14/R15/R18 | 固定资产、全链 clone/计时、完整 CI、第二平台实际结果、基线/整体验收/标签、外部配置和资源路径继续待执行 |
| R13 | 基础接口与构建说明已记录，完整移交待阶段验收 |

未覆盖：真实队列超限、磁盘写满/只读存储、等待超时、正常 SIGINT、真实不兼容 FFmpeg 环境、全量配置路径与 manifest、第二平台及冷环境。日志没有自动轮转/删除，外部配置和完整 D04 仍待确定。文本凭据不自动脱敏，调用者禁止写入。

Day 2 输入为现有 target/测试/日志/版本接口，以及合法固定 S01–S03、manifest、配置/路径和完整 bootstrap 的选择。完整 D01/D02/D04–D08 仍须按计划确认。阶段 0 退出清单保持未完成，不能据本机 Day 1 结果进入阶段 1 或创建 v0.1-foundation。
