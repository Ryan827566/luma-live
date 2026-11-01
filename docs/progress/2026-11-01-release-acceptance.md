# LumaLive 日期发布包独立验收记录

验收对象：`output/releases/2026-11-01_23-38-35`，北京时间 2026-11-01。

结论：本轮发布整理、启动器和真实客户端启动退出验收通过。独立检查没有发现阻塞本次开发版交付的问题。该结论不代表真实账号登录、音视频设备、通话、会议或外部 AI 提供商已验收。

## 实际发布包

独立逐项读取发布目录，确认 `manifest.json` 列出的 42 个 payload 文件均存在，实际字节长度和 SHA256 全部匹配。清单以外仅有清单自身；核对时尚未附加 `build-verification.json`。后来附加的构建或验收记录应作为单独证据保留，不应改写原始打包清单来暗示打包时已经完成运行验收。

可执行文件仅有 `luma_studio.exe` 与 `ai-service/runtime/python.exe`。没有打包账户服务、账户测试 fixture、私钥、账户连接配置、数据库、日志或 DPAPI 凭据文件。此处依据严格文件清单、实际目录检查和合成秘密标记排除测试，不宣称对任意二进制内容做过完整秘密审计。

| 文件 | SHA256 |
| --- | --- |
| `manifest.json` | `f92c0f7a0904aa62868929cb10f3a9625c7832483469635d1feb265809975c34` |
| `luma_studio.exe` | `e62e21660704caeec4068ded53f9d31c249d8e760c9e7382c82baddb41bde0ea` |
| `Start-LumaLive.cmd` | `0a78e6dea4f4ff33b1eabf1e8214506a7acd197b7a9ffd5a187a16dc2153013b` |

## 真实 Win32 启动退出

主流程运行可版本化的 `scripts/windows/test_client_release_smoke.py`，独立 QA 复核其 JSON 证据、实际文件哈希和私有目录清理结果。驱动确实调用包内 `Start-LumaLive.cmd`，没有直接运行 EXE 作为替代路径。

- 由控制器先把 actor 归入专属 kill-on-close Windows Job，再通过 stdin permit 允许启动。
- 清除继承的 LumaLive 及相关 AI 环境覆盖，设置独立的 `LOCALAPPDATA`、`APPDATA`、临时目录和账户配置。
- 账户端点仅为独占绑定的本机随机未监听端口，没有运行账户 fixture，也没有登录或连接正式账户服务。
- 只匹配该 Job 内、完整进程路径等于发布 EXE 的窗口；账户入口及主窗口控件已加载。
- 实际客户端 PID 为 `50100`，启动器 PID 为 `52800`。向已确认的客户端窗口发送 `WM_CLOSE` 后，客户端与启动器均正常返回 `0`。
- 结束时确认 Job 进程数为零、私有 profile 已删除。总耗时 `1.672` 秒；驱动的 actor 上限为 32 秒，Job 清理上限为 8 秒。

证据文件：`real-release-smoke-409f807f832545efae61db433a6aab41-final.json`。

证据 SHA256：`00c23446e42f0b62b751a5136d5c59ef56cd36635551e7b51e4d9a407351bf99`。

复核汇总：`actual-release-independent-audit.json`。

## 独立合成及故障检查

运行命令：`python -B independent_release_acceptance.py`。该次脚本从独立 QA 目录读取候选，6 组检查全部通过，累计约 10.4 秒。

| 检查 | 结果 |
| --- | --- |
| 中文及空格输入输出路径；北京时间目录；清单真实哈希与大小；排除模拟秘密和服务文件 | 通过 |
| 同秒输出名称冲突保持原包；最后 rename 时并发出现同名目录也不覆盖 | 通过 |
| 注入启动文件写入错误，仅清理本次 staging，保留已有发布文件 | 通过 |
| 必需运行时内容损坏时拒绝生成发布包 | 通过 |
| CLI 缺少输入时非零退出，不输出成功信息、不生成发布目录 | 通过 |
| 真实 `cmd.exe` 启动合成 .NET EXE：中文空格路径、外部 cwd、只选包内 EXE、返回 0/37、缺失 EXE 返回 2 | 通过 |

最后一项运行的是合成 Win32 进程；它证明启动器行为，不证明 LumaLive 业务功能。全部合成测试没有运行真实客户端、账户服务、fixture、设备或外部提供商。

证据文件：`independent-release-acceptance-39eb7e575efa4d4692dc4a2b7c1dc3e6.json`。

## 源码对应与复跑

独立检查确认仓库内下列文件与被验收候选的 SHA256 一致：

| 文件 | SHA256 |
| --- | --- |
| `scripts/windows/package_client_release.py` | `2bfcc15611ff82077e77634f782acf3c14e6b00fc23b9dd65b65ebd87159de4e` |
| `scripts/windows/release_launchers.py` | `8d518bbbe6b989766ebd979da1a6ff632902003b8f6df9c1a2c92a09e02429fb` |
| `scripts/windows/test_client_release_smoke.py` | `722fea1cc6b86aad8e72ec0fad7b9378fbd6a051783e74019e953933621985bd` |

已整理可版本化的独立测试 `scripts/windows/test_independent_release_acceptance.py`：从自身位置确定仓库，使用 `--evidence-dir` 写入唯一命名的证据与合成 fixture，不依赖开发候选所在的个人目录。它与上述已执行脚本的测试逻辑相同，仅改为仓库相对导入及显式证据目录；移入仓库后的包装入口尚未由独立 QA 重跑。需要 Windows、Python，以及 Windows .NET Framework 的 `csc.exe` 来生成很小的合成启动探针。

从仓库根目录可执行：

```powershell
python -B scripts/windows/test_independent_release_acceptance.py --evidence-dir output/acceptance/release-synthetic
python -B scripts/windows/test_client_release_smoke.py output/releases/2026-11-01_23-38-35 --evidence-dir output/acceptance/release-smoke
```

真实客户端 smoke 应串行执行，避免与其他客户端构建或启动验证争用资源。

## 验证边界

打包清单对构建和验收保留 `unverified`，Git 来源也未从当前 HEAD 推断。构建证明应查看主流程单独保存的 `build-verification.json`。

主流程另行报告在实际包内使用嵌入式 Python 成功导入 `ssl`、`ctypes`、`gateway`、`provider_settings`；该项属于本地模块加载，不涉及网络请求、模型请求或外部 AI 能力。本次独立 QA 未重复执行该导入命令。

本轮没有执行真实设备测试、双端音视频通信、账号注册登录、真实提供商调用、长时间稳定性测试或全 27 项功能回归。没有据此宣称这些能力通过。

## 主流程后续复核（2026-11-01 23:45）

上述源码哈希表记录独立 QA 初次核对时的字节。提交前仅去除 release_launchers.py 的 UTF-8 BOM/文件末尾空行，最终源码 SHA256 为 6260697ac4b5f5888345f48253789c8455a6381aa9e647237309015696b9ca4b。未改变运行逻辑；主流程再次逐字节核对该最终源码生成的 Start-LumaLive.cmd 与已验收发布包一致。

版本化独立脚本已移入仓库并实际复跑，6/6 通过；证据 independent-release-acceptance-012a07ffc9ed41d18083c75d4f7ea398.json。此为主流程在 D 盘应用后的复跑，不冒称独立 QA 又执行了一次。最终包已补充 build-verification.json；原始 manifest 与42项payload均未改写。
