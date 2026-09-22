# 固定媒体样本

阶段 0 使用三个可重复生成的 MP4。根目录 `sample_manifest.json` 记录字段、SHA256、生成命令、工具版本和许可信息。

- `s01_720p_h264.mp4`：1280x720、H.264 High、yuv420p、30/1 CFR、3 秒、90 帧、无 B 帧、无音频。SHA256 为 `14cf27d368a81590a0bc59b9fe580beef0d29f9daf27eeeaf42cdad8864d9358`。
- `s02_1080p_h264.mp4`：1920x1080、H.264 High、yuv420p、30/1 CFR、5 秒、150 帧、无 B 帧、无音频。SHA256 为 `a0f07d7dc2c29a73d982b30dd6f20e0691bfe768507a22cf405aece6a592aafd`。
- `s03_bframes_audio.mp4`：1280x720、H.264 High、yuv420p、30000/1001 CFR、5.005 秒、150 帧，其中 105 帧为 B 帧；音频为 AAC LC、48000 Hz、双声道。SHA256 为 `601183e88b078862a9646e69f89740a3d86a51479ebf06d9349f9bfd29b18cdd`。

样本由 FFmpeg 9.0.1 的 `testsrc2` 与 `sine` 数据源生成，未使用外部媒体素材。样本按仓库 `LICENSE` 中的 GNU General Public License Version 3 提供。

验证证据位于忽略提交的 `build/day2-assets/`：包含生成记录、`ffprobe` JSON、逐帧信息、SHA256 和完整解码结果。三个文件均已使用 `ffmpeg -xerror` 解码，退出状态均为 0。
