# AstraCodec

H264/H265/AV1 + 编码器架构 + 码控 + ROI/JND + FFmpeg + NEON + 云媒体处理

## 当前进度

进度更新：2026-09-22。S0 Day 1 与 Day 2 本机开发及验证完成，阶段 0 整体验收未完成。

- 已完成：基础模块、三个固定 MP4、manifest、外部配置、集中资源路径、安装布局和已 clone bootstrap。
- 已通过：macOS x86_64 的 Debug、Release、ASan/UBSan、TSan 各 52/52；三个媒体的散列、元数据、CFR、B 帧、音频与完整解码；格式与静态分析。
- 下一项：S0 Day 3 的外层 clone 入口、完整 Logger 异常/资源验证、远端 CI 与空缓存复现。

[查看 Day 1 进度、验证证据与未完成项目](docs/reports/stage0_day1.md)。本地逐项计划保存在 `STAGE-0.md` 和 `STAGE-1.md`，按现有 `.gitignore` 规则不上传。媒体转码尚未实现。

[查看 Day 2 实施结果](docs/reports/stage0_day2.md)。尚未验证的阶段 0 项目继续保持未完成。

## 构建入口

```bash
bash scripts/bootstrap.sh debug
bash scripts/verify_samples.sh build/debug/astracodec_verify_samples .
```

依赖、质量检查和 sanitizer 命令见 [构建说明](docs/build.md)，接口行为见 [基础模块](docs/foundation.md)，验证状态见 [Day 1 报告](docs/reports/stage0_day1.md)与 [Day 2 报告](docs/reports/stage0_day2.md)。需求依据保留在本地 `STAGE-0.md` 和 `STAGE-1.md`。
