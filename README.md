# AstraCodec

H264/H265/AV1 + 编码器架构 + 码控 + ROI/JND + FFmpeg + NEON + 云媒体处理

## 当前进度

进度更新：2026-09-29。S0 Day 1–2 已完成，Day 3 本机开发与验证完成；远端和第二平台待验收，阶段 0 整体验收未完成。

- 已完成：新增六项 Logger 异常与资源检查、外层 clone 验收入口和成功及两类失败自测，并配置 CI 空项目缓存断言、重复 bootstrap 与远端外层链。
- 已通过：macOS x86_64 的 Debug、Release、ASan/UBSan、TSan 各 58/58；三个媒体的散列、元数据、CFR、B 帧、音频与完整解码；格式与静态分析。
- 本地外层链：固定提交 `29397900d1d3f558093412df6e55e2aa101f84fa` 从 clone 到 smoke 用时 169 秒，含报告生成的脚本总耗时 170 秒；无效提交按预期在 checkout 返回 128，已有目标目录保护通过。
- 待验证：使用 Day 3 固定提交复跑外层链、远端 CI、第二平台和阶段环境基线。

[查看 Day 1 进度、验证证据与未完成项目](docs/reports/stage0_day1.md)。本地逐项计划保存在 `STAGE-0.md` 和 `STAGE-1.md`，按现有 `.gitignore` 规则不上传。媒体转码尚未实现。

[查看 Day 2 实施结果](docs/reports/stage0_day2.md)。尚未验证的阶段 0 项目继续保持未完成。

[查看 Day 3 当前进度](docs/reports/stage0_day3.md)。

## 构建入口

```bash
bash scripts/bootstrap.sh debug
bash scripts/verify_samples.sh build/debug/astracodec_verify_samples .
```

依赖、质量检查和 sanitizer 命令见 [构建说明](docs/build.md)，接口行为见 [基础模块](docs/foundation.md)，验证状态见 [Day 1 报告](docs/reports/stage0_day1.md)、[Day 2 报告](docs/reports/stage0_day2.md)与 [Day 3 报告](docs/reports/stage0_day3.md)。需求依据保留在本地 `STAGE-0.md` 和 `STAGE-1.md`。
