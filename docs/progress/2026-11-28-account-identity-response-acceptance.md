# 账户成功响应身份边界：独立 QA（2026-11-28）

状态：独立验收通过。收到 root 修后构建冻结通知后，只执行一次 `luma_client_account_tests.exe --identity-worker`；退出码 0，18/18 格通过，elapsed 0.125 秒，stderr 为空。独立驱动逐格复算预期，确认无遗漏/重复；源文件及二进制运行前后 hash 一致。owned Job 已清零，D 盘私有 profile 已删除，controller 无错误。

最新红测已只读核对：root 当前代码构建 15.219 秒，通过；新 `--identity-worker` 0.438 秒、退出码 1，日志恰好 18 格，10 FAIL / 8 PASS。六个空 ID 与四个既有会话错 ID 全部被旧客户端成功接受且保留 token，构成失败；六个同 ID与两个新登录切换 ID 通过。来源 `../release-build-integration-red-20261128.json`、`../release-build-identity-red-20261128.log`，该运行 Job 空。root 提供的红测二进制 SHA256 为 `08f890ee1a44fb643ca53969464261ec6a6072835064bb065e7a8afdb015ee9c`。独立 QA 没有重跑红测。

`run_independent.py` 执行前已通过 Python AST 语法检查；实际核对了 root 指定二进制 SHA256、完整 18 格无缺失/重复、每格独立预期、运行前后输入哈希及 owned Job/profile 清理。

## 修后实际结果与审查

六个同 ID 操作和两个新登录切换 ID 仍成功；六个空 ID 与四个既有会话错 ID 均失败，且在测试调用 Stop 前，running/authenticated/token_retained 均为 0、snapshot_id 为空，没有异常抛出。原生内置断言另外核对 refresh token、username、display_name 清空。Validate 返回空 refresh-token 字段时仍保留已有非空 refresh token，正例通过。

只读审查六处生产校验均发生在会话字段赋值前；密码/短信登录只要求非空，另外四操作要求与当前身份相同。拒绝路径调用统一 `InvalidIdentityResponseLocked()`，先 `DisconnectLocked()`，复用 `ClearSessionLocked()` 清状态，再返回 `invalid_response`。没有发现本次范围内阻断。

本次证据：`independent-run.json`（精确命令、30 秒 timeout、私有环境、18 格结果、前后 hash 和清理）；`native.stdout.log`（完整逐格状态）；`native.stderr.log`（0 字节）。真实产品 AuthService 的完整客户端/服务器集成和 UI 另由 root 验证，本记录只计合成 peer 的真实 TCP/AuthWire 客户端边界。

| 文件 | 修后 SHA256 |
| --- | --- |
| luma_client_account_tests.exe | `c7aeab591522d446ebf1e31b9e744fd650a1ed4441547f46f97a3316267434bd` |
| AccountService.cpp | `62f894082a63072553493d08aed428ace031cced8f04478a93140fcddaf5eb79` |
| AccountServiceTests.cpp | `7829f4c62bfb10cf26e00a3041a9f93ce9cb2ecdd082378df9b166e5b4f28e87` |
| AuthWire.hpp | `2bb80bf0c019f6d51426c08232e3a7937024a194ec8320689c45e8065f47f24b` |

## 原始复现的可信边界

已读取 `../missing-user-id-repro.json` 与 `../probe_missing_user_id.py`，并核对 `AuthTlsFixture.cpp` 的 idle 模式确实调用 Login、ValidateSession 和 IsAuthenticated。原生旧二进制 SHA256 为 `7663dc9de029c88f55fa85766d3ff274a2f275388700a4f9e10def4e1523ddba`；两组分别 61.63 秒、退出码 0。合成对端先回应注册/密码挑战，再在 LoginOk 和 Validate 的 LoginOk 将 user_id 设为空，客户端仍报告成功。

这证明实际原生账户客户端接受空身份的 AuthWire TCP 响应。它不是产品正式 AuthService 集成，也不是 TLS 验收：测试显式 `LUMALIVE_AUTH_ENV=test`、清空 CA，使用随机 loopback 明文 socket；`PASS TLS client` 仅是复用 fixture 的固定输出。原始脚本没有 owned Job 清理字段，不补称其已证明 Job 清零；本次不重跑 61 秒 probe。

## 服务端契约核对

只读检查 `AuthServiceImpl.cpp`：注册用 16 随机字节的十六进制值创建 ID；密码/短信登录的 LoginOk 第 6 字段来自实际用户记录的 `id`。Validate/Refresh 先查当前 token 对应 session，再取同一个 user record 返回 ID；Validate 故意不重发 refresh token，客户端应保留旧值。Get/UpdateProfile 第 1 字段返回该用户 `id`；更新昵称、邮箱、头像不会修改 `id`。

因此已有会话的 Validate/Refresh/Get/Update 不应接受不同 ID；新的密码或短信登录允许切换到另一个非空 opaque ID。此次不要求固定 UUID/hex 格式，不将服务器当前生成格式误作客户端通用协议限制。

## 已独立执行的 18 格矩阵（全部符合预期）

| 操作 | 同 ID | 空 ID | 另一个非空 ID |
| --- | --- | --- | --- |
| Login | 成功，保持 ID | 拒绝并断连清状态 | 新登录允许切换 |
| PhoneLogin | 成功，保持 ID | 拒绝并断连清状态 | 新登录允许切换 |
| ValidateSession | 成功，保留 refresh token | 拒绝并断连清状态 | 拒绝并断连清状态 |
| RefreshSession | 成功 | 拒绝并断连清状态 | 拒绝并断连清状态 |
| GetProfile | 成功 | 拒绝并断连清状态 | 拒绝并断连清状态 |
| UpdateProfile | 成功 | 拒绝并断连清状态 | 拒绝并断连清状态 |

开发 worker 每格先通过真实 TCP 种入已登录会话，再发送该操作的合成成功响应；断言异常不外抛，失败时 IsRunning/IsAuthenticated 均 false，token/refresh token/ID/username/display_name 清空。已有空的 Sessions/SecurityEvents 也保持空；这不证明非空缓存的清除。快照在测试的 Stop 之前采集，不能把事后 Stop 清理误判成产品主动拒绝清理。

独立驱动只运行新 `--identity-worker` 一次，30 秒测试 timeout、自有 kill-on-close Job、输入前后 hash、逐格数量与结果核对、Job 清零。零输出、少于 18 格、未执行或仅进程退出 0 均不能判通过。没有构建或改开发文件，没有操作 Git、UI、剪贴板、正式服务或真实账户。

所有新增证据在本 D 盘目录；Python `-B`，TEMP/TMP/APPDATA/LOCALAPPDATA 均指向 D 盘私有目录。没有执行主动 C 盘写入；操作系统、Codex 内部不可控写入未测量。
