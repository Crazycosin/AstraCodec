# S0 Day 2 实施进度

进度更新：2026-09-22 21:05（Asia/Shanghai）。状态：Day 2 本机开发与验证完成；阶段 0 整体验收未完成。错误规则继续按 D03：所有错误立即崩溃。

## 已完成任务

- [x] 保存三个真实固定 MP4、固定 SHA256、manifest、生成方法与许可记录。
- [x] 实现强类型 manifest 解析、FFmpeg SHA256 校验和错误子进程测试。
- [x] 实现外部基础 JSON 配置、集中资源路径及专项测试。
- [x] 提供已 clone 项目的 bootstrap，包含依赖检查、构建、全量 CTest、资产核查与最终 version/help。
- [x] 使用 ffprobe/jq 核查 stream、逐帧 CFR、B 帧和音频，并使用 FFmpeg 完整解码。
- [x] 验证源码目录和安装目录两种 runtime 布局，以及非项目工作目录调用。
- [x] 使用独立、含空格的目录验证安装布局；验证缺失依赖的诊断和失败报告。
- [x] 执行 Debug、Release、ASan/UBSan、TSan 回归、格式和编译数据库静态分析。
- [x] 更新 README、构建说明、基础模块说明、本地 STAGE-0.md 与本报告。

## 固定媒体

| ID | 内容 | 字节数 | SHA256 | 核查结果 |
| --- | --- | ---: | --- | --- |
| S01 | 1280x720，H.264，yuv420p，30/1 CFR，3 秒，90 帧 | 566747 | `14cf27d368a81590a0bc59b9fe580beef0d29f9daf27eeeaf42cdad8864d9358` | progressive；无 B 帧；无音频；完整解码通过 |
| S02 | 1920x1080，H.264，yuv420p，30/1 CFR，5 秒，150 帧 | 1829477 | `a0f07d7dc2c29a73d982b30dd6f20e0691bfe768507a22cf405aece6a592aafd` | progressive；无 B 帧；无音频；完整解码通过 |
| S03 | 1280x720，H.264，yuv420p，30000/1001 CFR，5.005 秒，150 帧 | 1130009 | `601183e88b078862a9646e69f89740a3d86a51479ebf06d9349f9bfd29b18cdd` | progressive；105 个 B 帧；AAC 48000 Hz 双声道、235 个音频帧；完整解码通过 |

三个文件由本机 FFmpeg 9.0.1 的 testsrc2/sine、libx264 和 AAC encoder 生成，没有使用外部媒体素材。生成命令、工具版本和项目许可记录在 `sample_manifest.json` 与 `samples/README.md`。固定文件可使用所记录命令在相同工具环境复核；跨工具版本重新编码不能自动替换受版本控制的文件与散列。

首次生成与复核证据位于被忽略的 `build/day2-assets/`。独立核查报告位于 `build/sample-verification/run.ji3NnREW/report.json`；原始 probe、逐帧 JSON、解码输出和工具版本位于同一目录。各 preset 的 CTest 也保存独立资产证据。

## 模块和入口

- `ResourceManager` 集中处理 runtime root、只读文件、目录和输出文件，拒绝父目录引用、绝对资产路径、符号链接、缺失项和错误文件类型。
- `FoundationConfig` 解析 schema 1，支持默认配置路径和显式配置优先级，检查重复 key、未知字段、类型、数值范围与 Logger 路径。
- `SampleManifest` 解析 14 类必需信息，限制文件和样本数量，使用 FFmpeg libavutil 计算实际 SHA256。
- `astracodec_verify_samples` 读取配置、manifest 与三个文件，输出核查 JSON；参数、配置、路径、读取和散列错误立即崩溃。
- `scripts/verify_samples.sh` 独立检查散列、probe、CFR、B 帧、音频和完整解码。
- `scripts/bootstrap.sh` 为已 clone 工程提供五步入口：configure、build、CTest、独立样本核查、smoke，并保存分项状态与耗时；依赖检查和各步骤失败时记录诊断、退出码与失败报告。
- 安装布局为 `bin/` 与 `share/astracodec/`；CTest 在非项目工作目录中使用独立、含空格的安装目录运行资产核查程序。

## 本机验证

主机为 macOS 15.7.7 x86_64，AppleClang 17.0.0，CMake 4.4.2，Ninja 1.13.2，FFmpeg/ffprobe 9.0.1，jq 1.7.1，fmt 12.2.0。FFmpeg 库版本为 libavformat 63.1.101、libavcodec 63.1.101、libavutil 61.1.101。

CTest 共 52 项：44 项 GoogleTest 与 8 项 CLI、资产、安装布局及缺失依赖检查。新增专项为 Configuration 9 项、ResourceManager 5 项、SampleManifest 8 项；媒体散列错误测试复制受版本控制的 S01 并修改副本，原文件保持不变。

| 模式 | 结果 | 检查 |
| --- | --- | --- |
| Debug | 52/52 | 告警作为错误、完整 CTest、资产与安装布局 |
| Release | 52/52 | 优化构建、完整 CTest、资产与安装布局 |
| ASan/UBSan | 52/52 | `halt_on_error=1`，完整 CTest |
| TSan | 52/52 | `halt_on_error=1`，完整 CTest |

已 clone Debug bootstrap 成功，报告为 `build/debug/test-output/stage0/bootstrap.jH1mhcfN/report.json`。该次已准备环境用时 20 秒，其中 configure 1 秒、build 0 秒、CTest 16 秒、独立样本核查 3 秒、smoke 0 秒。依赖 archive 和构建产物已经存在，因此该结果只证明入口和分项记录可用，不用于 E04 空项目缓存或 900 秒验收。缺失依赖检查使用真实 pkg-config，在隔离的包搜索目录下验证非零退出、依赖名称和 `status=failed` 报告。

clang-format 21.1.6 检查全部第一方 C++ 文件；clang-tidy 21.1.6 使用 Debug 的真实 compile database 检查 16 个翻译单元。第一方阻断诊断为零。系统与第三方头文件统计由检查工具排除，未将其计为项目通过项。

## 仍待执行

- [ ] 外层真实 clone 入口、固定 ref 和全程证据归档。
- [ ] 空项目缓存首次复现、两个平台分别测量及 900 秒目标。
- [ ] 第二平台和 Day 2 修改后的远端 CI 结果。
- [ ] Logger 的实际队列超限、受限存储、等待超时、正常中断和完整资源检查。
- [ ] 完整阶段 baseline、逐项验收、阶段 1 移交、评审和 `v0.1-foundation` 标签。

Day 2 不包含项目媒体转码、媒体 Pipeline、编码性能或质量算法。Day 2 文件已纳入 `s0d2任务` 提交，分支为 `feature/stage-0-foundation`；远端 CI 结果仍待获取。
