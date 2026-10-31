# LumaLive 独立验收记录（2026-11-01）

记录者：独立 QA agent。最终核验时间：北京时间 2026-11-01 06:13。范围只包含离线 PE 元数据工具及账户失效后的通话别名清理。没有改生产文件、操作 Git、启动正式账户服务或长 soak。两项有界改动验收完成，可由主 agent 提交；这不代表完整产品或原 soak 通过。

## A：离线 PE 工具

结论：该有界工具可发布。已核对 D 盘 `scripts/windows/inspect_ntdll_entry.py` 与本次独立通过结果的源码 SHA256 一致：`9a92fa6fa626a7acd87f55995befdc4b0b61bd346ddcb2093a671554eaaebc29`。

独立测试脚本：`work/acceptance/20261101/test_offline_pe_independent.py`，SHA256 `620a8ea1d7845cba68b16d62aeb0e661667c3827c2753d5ff74e96652a4cf683`。14 项通过，0 失败、0 错误、0 跳过。测试使用独立合成 PE 字节，不加载 DLL、不附加进程；覆盖有效导出与 RSDS、截断头、错误 PE 指针、错误架构魔数、区段/文件边界、虚拟尾、非法 RVA、巨大导出计数、错误 ordinal、未终止字符串、runtime 记录余数和调试数据越界。

已执行命令（工作目录 `C:\Users\syb15\Documents\Codex\2026-10-23\shen`）：

```powershell
python -B work/acceptance/20261101/test_offline_pe_independent.py --source work/ntdll-inspection-20261101/inspect_ntdll_entry.py --result work/acceptance/20261101/pe-independent-approved.json
```

首次普通沙箱运行因临时目录 `WinError 5` 失败；`pe-independent-first.json` 保留该环境错误。通过正常 require_escalated 审批的同一测试运行完成 14 项；本次恢复没有重复测试。独立核对开发者 `developer-tests-final.log` 为 12 项通过，其坏输入旧报告保持、原子替换失败等故障注入属于开发者测试；QA 另静态核对先完成解析才发布、同目录临时文件 flush/fsync 后 replace 及失败清理路径。

真实本机 DLL 报告 `work/ntdll-inspection-20261101/ntdll-entry-20261101.json` 的 ntdll SHA256 为 `36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d`；RVA `0x4d110` 没有同址导出，只落在 runtime 记录 `[0x4d110, 0x4dd29)`。最近导出不能用来命名私有例程。10/29 实验未记录 ntdll 文件 hash，不能直接把本次版本绑定到历史进程。线程创建者、300 秒触发路径及原 soak 稳定性失败仍未解决；工具验收通过不代表稳定性通过。

## B：账户失效清理通话别名

静态审查通过。旧 DisplayName 自动赋给 Identity 的方案已撤回：现有 Join 将编辑框用作实际 `peer_id`，服务端要求 1–128 UTF-8 字节且同房间唯一。账户显示名没有这两项保证。本轮生产 diff 仅在 `!authenticated` 清理分支新增 3 行，恢复既有 `studio-PID`。未修改登录默认值、Join、RTC、协议或身份认证权限。

D 盘应用源码 SHA256：

- `LumaLive/client/ui/studio-preview/src/StudioPreview.cpp`：`d6e7e0b42c52d2b1db05f1a290695cf853a6004a511ea04db945ef7840db02e7`
- `scripts/windows/test_call_account_identity.py`：首次为 `70ce3b0605a3f1bed1d5fce03c1e21ca576f5a23e97844a9e08d56b20cb7094b`；修正测试初始化等待后的最终版本为 `22aba3fe65f87643cd50ad364bf93a41339635cb0b40bb54d272e6720f74264e`。

真实构建通过，退出 0，用时 142.922 秒。首次 UI 专项在 1.781 秒以退出 1 失败，`Call display-name control missing`，尚未开始登录。该受控 Job 已清空，`owned_job_empty=true`、无 controller 错误。主 agent 明确移交 `build/rtc-config-20261029` 的唯一执行权；未并行使用该构建目录。

已定位测试初始化竞态：StudioPreview 先创建 Account 导航 129，再创建 Identity 118，最后初始 Layout 才创建账户页。测试仅等 129 就返回根窗口，可能早于 118 创建。QA 候选把根窗口就绪条件增加 118 和子账户页；保留后续断言，生产 3 行不改。主 agent 守卫旧文件 hash 后应用修订，QA 确认 D 盘测试与候选 hash 一致，再执行一次仅 UI 重试，无再次构建。重试通过：退出 0、75.156 秒、`owned_job_empty=true`、`controller_error_type=null`。首次报告及三份日志已另存本目录 `first-*`，没有覆盖失败证据。

本次真实构建二进制 SHA256：`luma_studio.exe` 为 `55aea27ee2edc6d7375e43843eaeed65de759e9d6df6e84d76728b25e2de4477`；`luma_account_ui_fixture.exe` 为 `2ccbe3a86f3b08004e73f357ba2ddd8c2754dab6fbdbc60cfdd4eaabe7cdf824`。

实际启动命令（工作目录 `D:\workspace\luma-live`，正常 require_escalated 审批）：

```powershell
& 'C:\Users\syb15\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -B 'C:\Users\syb15\Documents\Codex\2026-10-23\shen\work\run_account_alias_20261101.py'
```

wrapper 在给 actor permit 之前把它加入 kill-on-close Job；actor 再确认成员身份。构建 240 秒、UI 专项 180 秒、总 watchdog 460 秒，收尾清空所属进程树。只构建 `luma_studio`、`luma_account_ui_fixture`，测试随机端口内存账户夹具及临时客户端 profile，不加入通话房间、不打开采集设备、不停止用户客户端。

仅 UI 重试的实际命令（同一 D 盘工作目录，正常 require_escalated 审批）：

```powershell
& 'C:\Users\syb15\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -B 'C:\Users\syb15\Documents\Codex\2026-10-23\shen\work\acceptance\20261101\run_account_alias_retry.py'
```

重试 wrapper 沿用 Job/permit 机制，UI 180 秒、总 watchdog 220 秒，结果独立写入本 QA 目录。日志实际输出：`PASS: editable call ID preserved in-session; logout, account switch and authentication loss clear private aliases`。这属于真实客户端控件与本机真实账户协议/内存存储验收，未 mock 认证成功，不涉及正式 PostgreSQL/TLS 服务。

专项读取真实 EXE 的 HWND 控件，不以日志接口回显替代 UI。覆盖同会话手改/清空保持、profile 保存不覆写、登出重置、第二账户不继承、两个账户相同显示名不进入 peer_id、账户夹具失联后重置。未执行新 Unicode 变体、跨网通话、第二电脑或真实设备测试；完整产品账户身份与通话显示名的分离仍未实现。

首次证据位置：`work/account-alias-integration-20261101.json`、`work/account-alias-build-20261101.log`、`work/account-alias-alias-ui-20261101.log`、`work/account-alias-integration-controller-20261101.log`。成功重试证据位置：本 QA 目录的 `account-alias-integration-20261101.json`（SHA256 `88d01bb118269250b18dea9807a30df5659b3dbd9af99f67164c7b0a406ec5fc`）、`account-alias-alias-ui-20261101.log` 和 controller 日志。用户可复验原始专项，但须使用已构建同目录的两个 EXE，并使用受控 wrapper 清理本次夹具与客户端。

测试判定：退出 0 且日志 PASS、所有检查 passed、Job 清空为通过；任何断言/超时/非零退出/Job 未清空均为失败。不要仅依据上一次通过日志。未执行的跨网、真实硬件、全量客户端验收和账户稳定性不能从本专项推导为通过。QA 已释放构建目录独占执行权，后续不再启动任务。
