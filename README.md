# AstraCodec

H264/H265/AV1 + 编码器架构 + 码控 + ROI/JND + FFmpeg + NEON + 云媒体处理

## 当前进度

进度更新：2026-09-18。S0 Day 1 基础实现与本机验证完成，Day 2 尚未启动，阶段 0 整体验收未完成。

- 已完成：CMake/presets、Result/ErrorCode、基础异步 Logger、VersionInfo、version/help、GoogleTest/CTest 和本机质量入口。
- 已通过：macOS x86_64 的 Debug、Release、ASan/UBSan、TSan 各 26/26；独立新目录构建、格式与静态分析。
- 下一步：S0 Day 2 的固定样本、manifest、资产校验，以及完整 bootstrap、配置和路径工作。

[查看 Day 1 进度、验证证据与未完成项目](docs/reports/stage0_day1.md)。本地逐项计划保存在 `STAGE-0.md` 和 `STAGE-1.md`，按现有 `.gitignore` 规则不上传。媒体转码尚未实现。

## 构建入口

```bash
bash scripts/run_day1.sh debug
bash scripts/run_day1.sh release
```

依赖、质量检查和 sanitizer 命令见 [构建说明](docs/build.md)，接口行为见 [基础模块](docs/foundation.md)，实际验证状态见 [Day 1 报告](docs/reports/stage0_day1.md)。需求依据保留在本地 `STAGE-0.md` 和 `STAGE-1.md`。
