# 基础模块设计与使用

## 模块关系

`astra_core` 包含 ErrorCode/Crash、Result、Logger、VersionInfo、FoundationConfig、ResourceManager 和 SampleManifest。应用只依赖公共接口；AsyncLogger 具体类位于 `.cc` 内，上层持有 `unique_ptr<Logger>`。CMake 通过 target 表达编译、include、链接和 sanitizer 参数。`astracodec` 提供 `--version` 与 `--help`，`astracodec_verify_samples` 提供基础资产核查。

## 错误和 Result

D03 已由用户确认：所有错误按目录规则立即崩溃。`Crash` 同步输出 JSON 至 stderr 后 `abort`；诊断包括时间、线程、操作位置、错误码和上下文。诊断写入失败同样终止。内存分配或第三方组件内部终止时，不能保证该诊断完整输出。

ErrorCode 数值：0 成功；1 参数；2 配置；3 依赖；4 版本；5 资产；6 散列；7 读；8 写；9 日志；10 内部不变量。应用失败为进程异常终止，不把这些数值映射为常规 CLI 退出码；成功退出为 0。CMake 和脚本前置检查使用工具自身的失败状态。

`Result<T>::Success` 保存成功值，支持条件成立时的复制、移动值和 `void`。`Failure` 在工厂调用时立即 Crash，不返回错误状态。移动使来源失效；`TakeValue` 只接受右值并消费一次。访问、复制或移动失效来源立即 Crash。对已消费目标重新赋入有效来源允许重新使用。Result 不提供延迟传播错误、catch、恢复或不完整成功对象。

该规则影响来源方案的后续预期异常处理与清理要求；阶段 1 开发前必须复核其相关计划。当前 STAGE-1.md 保留原文件，未据 Day 1 实施认定媒体异常处理可验收。

## Logger

正常 producer 不写 sink，记录进入有限队列，唯一 `jthread` 写实际 stderr 或 append 文件。线程没有 detach。`Log` 和 `Flush` 支持并发；每个 Flush 保存请求代次和进入时的 accepted sequence，等待该批记录写入并刷新。后续请求不会提高较早请求的目标。记录保存 producer 顺序，多个 producer 的跨线程顺序由取得队列锁的顺序决定。

Shutdown 由唯一拥有者在所有 Log/Flush 调用结束后执行，停止接受、完成全部写入和刷新、join worker、关闭文件；重复 Shutdown 允许。服务析构执行相同正常结束流程。停止后的 Log/Flush 属于错误。

默认容量：队列 4096 条，记录 65536 bytes，文件 16 MiB，等待期限 5000 ms，最低级别 info。单记录上限可配置至 1 MiB，队列至 65536；编码前检查输入容量，编码后检查最终 JSON 容量。刷新请求同样有限。限速默认关闭，可显式配置真实时间窗口和条数。配置对象在创建后不可修改，数值单位由类型表达。

error/fatal 或非零错误码立即同步 Crash，保存原记录 message、job_id、node、pts/dts 和 fields，不等待异步队列。容量满、限速超限、记录/文件超限、文件查询/打开/写入/刷新/关闭失败、等待超时均立即 Crash；没有丢弃、备用 sink、重试、自动文件删除或轮转。`FoundationConfig` 已将现有 Logger 数值放入外部 JSON；文件保留由调用者管理，自动轮转和保留方法仍待 D04 后续工作确定。

JSON 字段包含 `timestamp_us/severity/thread_id/job_id/node/pts/dts/error_code/message/source/line/function/fields`。无媒体 pts/dts 时为 null。字段名含 password、secret、token、credential、authorization、api_key 时，不区分大小写地将字段值替换为 `[REDACTED]`。脱敏不扫描自由文本；message、job_id、node 和普通字段禁止放入凭据。JSON escaping 由 nlohmann 完成。

Log 的默认 source_location 在调用处取得，调用者可显式传入上游操作位置；不使用 LogRecord 构造器所在头文件作为业务位置。

禁止把崩溃测试当作正常线程结束、Flush 或资源释放证据。当前真实测试覆盖四个 producer、并发 Flush、五次服务生命周期、实际文件、过滤与脱敏、文件/记录容量、限速和若干非法操作；实际队列超限、磁盘写满和等待超时尚没有完整证据。

## VersionInfo 和依赖

项目版本来自 CMake project。commit、dirty、编译器、平台和架构由构建环境提供；逻辑核心数和 FFmpeg 信息使用实际运行查询。核心数无法查询时显示 unavailable；当前未查询 CPU 型号。

三个 FFmpeg 库分别打印 header/runtime 版本与运行许可，检查相同 major 且 runtime 不低于 header。FFmpeg 原始版本字符串不被重新解析。CLI 与 startup 记录的版本信息来自同一对象；版本、帮助写 stdout，日志写 stderr。stdout 使用逐项检查结果的 fwrite/fflush，fmt 只用于生成字符串。

实际 libx264 encoder 与 H.264 decoder 能力查询属于环境准备，未执行项目转码。真实不兼容 FFmpeg 环境尚未验证，不能用构造的版本对象代替完整环境测试。

## 配置和资源路径

`LoadFoundationConfig(runtime_root, explicit_config)` 返回强类型配置。显式配置路径优先；未提供显式路径时读取 `configs/foundation.json`；默认配置文件缺失时使用代码中记录的基础默认值。显式文件缺失、JSON 语法错误、重复 key、未知字段、类型或范围错误均立即 Crash。配置文件上限为 256 KiB，schema 版本当前为 1。

配置包含 Logger 等级、sink、文件路径、队列容量、记录与文件容量、限速区间、停止期限，以及 sample root 和 manifest 路径。项目示例文件要求列出全部字段。stderr sink 的 `file_path` 必须为 null；file sink 的路径必须位于 runtime root 内且父目录已存在。

`ResourceManager` 在构造时保存 canonical runtime root。读取文件、读取目录和输出文件三类接口拒绝空路径、绝对资产路径、父目录引用、符号链接、缺失项和错误文件类型。结果不依赖调用进程随后改变的工作目录。只读方法可并发使用，调用期间目录内容须保持不变。

## Manifest 和固定样本

`LoadSampleManifest` 使用 nlohmann JSON 解析 schema 1，文件上限为 1 MiB，样本上限为 64。它检查必需字段、重复 key、未知字段、重复 ID/文件名、有理数、时长、SHA256、路径与整数范围。`VerifySampleHashes` 通过 `ResourceManager` 读取文件，并使用 FFmpeg libavutil SHA256 比较固定散列；单文件上限为 1 GiB。

根目录 `sample_manifest.json` 保存 S01–S03 的尺寸、fps、时长、散列、像素格式、帧数、time base、B 帧、音频及来源信息。三个 MP4 为 FFmpeg testsrc2/sine 生成并固定保存的测试素材，具体参数见 [样本说明](../samples/README.md)。重新运行生成命令只用于复核；工具版本改变时不得自动替换 manifest 中的散列。

媒体语义核查由 `scripts/verify_samples.sh` 负责。C++ 程序检查配置、路径、manifest 与文件字节；脚本通过 ffprobe/jq 和 FFmpeg 完整解码独立核查媒体内容。此能力只验证输入资产，没有实现项目转码。

## 后续接入限制

当前不包含外层 clone 入口、空项目缓存计时、第二平台实际结果、完整 Logger 异常/资源检查或阶段标签。新增模块继续使用现有 target、测试、错误分类、配置、资源路径和质量规则。Logger 内部队列不代表媒体 Pipeline 已实现。
