# 外部依赖记录

FFmpeg 和 fmt 通过系统 pkg-config 探测。GoogleTest 1.17.0 和 nlohmann JSON 3.12.0 可以显式选择 SYSTEM 或 FETCH；选择失败及时终止配置，不自动切换依赖来源。

FETCH 使用固定官方 archive 和 SHA256，各构建模式的源码与构建产物保存在被忽略的 `build/<preset>/_deps/`，下载缓存位于 `.cache/dependencies/`。不把 third-party 源码纳入项目风格检查，不构建 GoogleMock。

| 组件 | 版本要求 | 来源和许可证 |
| --- | --- | --- |
| FFmpeg libavformat/libavcodec/libavutil | 查询实际头文件和运行时版本；相同 major 且 runtime 不低于 header | 系统 FFmpeg 包；运行时 license API 的实际返回值记录在 --version |
| fmt | pkg-config 至少 9；本机验证 12.2.0 | 同一 pkg-config target 提供头文件和库；MIT |
| GoogleTest | 1.17.0 | google/googletest 官方 archive；BSD-3-Clause |
| nlohmann JSON | 3.12.0 | nlohmann/json 官方 archive；MIT |

archive SHA256 和获取地址以 `cmake/AstraDependencies.cmake` 为唯一配置依据。第三方许可证原文件保留在实际源码或系统包目录。发布时需要按实际链接组合准备完整许可证资料。
