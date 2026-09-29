# S0 Day 3 实施进度

进度更新：2026-09-29 22:33（Asia/Shanghai）。状态：本机开发与验证完成；远端和第二平台待验收，阶段 0 整体验收未完成。错误规则继续按 D03：所有应用错误立即崩溃。

## 当日工作

- [x] 提供固定 commit、明确目标目录的外层真实 clone 入口，保存步骤状态、耗时和原始证据。
- [x] 验证 Logger 的实际队列超限、受限存储、等待超时、正常 SIGINT 与资源释放。
- [x] 配置 CI 空项目缓存断言、重复 bootstrap、远端外层链和证据上传；远端运行仍待验收。
- [x] 运行本机 Debug、Release、ASan/UBSan、TSan 回归及格式、静态分析。
- [ ] 核对远端 CI 与第二平台结果；未取得结果前保持待验收。

## 已完成实现

- `scripts/run_stage0_acceptance.sh` 接收仓库地址、40 位 commit、新目标绝对路径、证据父目录和可选 fetch ref。入口执行 environment、clone、可选 fetch、checkout、verify_checkout 与 Debug bootstrap，并检查 clone 至成功 smoke 不超过 900 秒。
- `scripts/test_stage0_acceptance.sh` 执行一条成功链、无效 commit 失败链和已有目标目录保护链，核查外层报告、内层 bootstrap 报告、commit、工作区状态、步骤序列、退出码、计时断言及已有文件保持不变。
- Logger 新增六项测试：队列容量、Flush/Shutdown 等待期限、进程文件大小限制、只读目录、SIGINT 有序停止，以及重复生命周期后的线程和文件描述符数量。
- macOS 使用系统线程接口暂停实际 Logger worker；Linux 使用 `/proc/self/task`、定向信号与管道完成同类控制。测试不替换 Logger 或 sink。
- `.github/workflows/foundation.yml` 在首次 bootstrap 前检查 `build` 与 `.cache/dependencies` 均不存在；Debug 矩阵重复执行 bootstrap，并从 GitHub 远端按 `GITHUB_SHA` 运行外层链和上传证据。

## 本机验证结果

- 当前登记 50 项 GoogleTest 与 8 项 CLI、资产、部署及缺失依赖检查，共 58 项。
- Debug、Release、ASan/UBSan、TSan 各 58/58 通过。
- 四种模式的 15 项 Logger 测试均通过；TSan 修正后再次完成 58/58 全量回归。
- clang-format 21.1.6 与 clang-tidy 21.1.6 全量检查通过，静态分析使用真实 Debug compile database。
- 外层成功链以本地仓库源和固定提交 `29397900d1d3f558093412df6e55e2aa101f84fa` 执行，clone 至 smoke 为 169 秒，含报告生成的脚本总耗时为 170 秒，900 秒断言通过。
- 无效 commit 链在 checkout 返回 128，外层报告为 `failed`，`bootstrap_report` 为 null，未把失败链记为成功。
- 已有目标目录链返回 2，报告记录预检失败，目标内的 `preserve.txt` 保持原内容。
- 169 秒记录对应已提交的 Day 2 版本，用于验证 Day 3 外层入口；Day 3 改动尚未提交，因此仍须在固定 Day 3 commit 上复跑。

## 证据位置

- 外层成功报告：`build/stage0-acceptance-tests/run.MBege3q4/success-evidence/stage0-acceptance.yocTXLMx/report.json`
- 对应 bootstrap 报告：`build/stage0-acceptance-tests/run.MBege3q4/success-evidence/stage0-acceptance.yocTXLMx/bootstrap-evidence/report.json`
- 无效 commit 报告：`build/stage0-acceptance-tests/run.MBege3q4/failure-evidence/stage0-acceptance.zwCP01Ez/report.json`
- 已有目标目录报告：`build/stage0-acceptance-tests/run.MBege3q4/preflight-evidence/stage0-acceptance.xvGTuWAe/report.json`
- 四种模式 CTest 日志：`build/<preset>/Testing/Temporary/LastTest.log`
- Logger JSONL 与资源检查输出：`build/<preset>/test-output/`

以上运行证据按仓库规则保存在被忽略的构建目录；可提交的进度和结论由本报告记录。

## 待完成项目

- [ ] 形成 Day 3 固定 commit 后，以该 commit 再运行外层完整链。
- [ ] 运行并核对远端 Linux/macOS CI；当前仅完成 workflow 配置，不能作为第二平台通过证据。
- [ ] 按 D06 确认正式首次复现条件，完成阶段环境基线与报告。
- [ ] 完成阶段 0 评审、阶段 1 移交和版本标签。

Day 1–2 的实现与验证结果见 [Day 2 报告](stage0_day2.md)。
