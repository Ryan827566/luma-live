# 稳定账户 user_id 展示：独立 QA（2026-11-27）

状态：独立验收通过。冻结三文件 diff 审查无阻断；root 构建完成后，英文真实账户 UI 全套只运行一次，退出码 0，耗时 50.547 秒。自有 Job 已清零，私有 profile 已删除，controller 无错误；运行前后源码及二进制哈希一致且二进制与 root 提供值相同。

## 本次实际验证

- Profile 完整 `User ID` 与内存认证 fixture 成功保存的 `row.id` 一致；期望值经私有 stdin 读取，不使用昵称推断。
- 详情控件具备 `ES_READONLY`；通过 `EM_SETSEL/EM_GETSEL` 确认整个 ID 子串可被选中。未测试 Ctrl+A 或真实复制。
- 昵称更新为合成值后，显示的 ID 不变；退出后旧 ID 从详情清除，再次登录恢复同一个 ID。
- 密码更新使会话失效后，详情不保留旧 ID。
- 既有英文全套流程通过，包括入口校验、MFA/恢复、隐藏命令拒绝、表单与确认布局、详情滚动、模拟交付失败、邮箱/手机验证、SMS 登录、密码修改、跨客户端恢复、删除及端点保存。未启用 `--real-sms-resend`。
- 布局断言覆盖 compact 与当前桌面允许的宽窗口；日志实际宽窗口为 1386×788，不能宣称实际达到 1440×900。

## 审查结论与证据

`../frozen.diff` 中生产改动仅在已认证的 Profile 且 user_id 非空时添加本地化标签与完整字段。fixture 新增测试内存映射，只有成功 Upsert 后记入服务器记录的 ID；不增加正式服务 API，不返回 token。UI 测试断言使用真实控件消息且无剪贴板调用。无阻断发现。

`independent-run.json` 保存环境、耗时、SHA256 及 Job/profile 清理结果；`independent-run.log` 保存完整 suite PASS 摘要；`run_independent.py` 保存有界独立执行方法。子测试 timeout 220 秒，controller 等待 232 秒加 Job 清理 8 秒，未触及上限。没有重复重试。

| 文件 | SHA256 |
| --- | --- |
| luma_studio.exe | `4c247a9b4cf8552d9d7dde9bd704b14a37b3edd6f5d07b25e4e0ac15880b5f2d` |
| luma_account_ui_fixture.exe | `9f8367f07e2b8989da1aca28ecebd30f548959c69506c00184c0909e91c88897` |
| AccountPanel.cpp | `4afdc80c1e43e7323a9fe981b11b759dcaf2e3ba10cd41d95d6ad373fa70d1c3` |
| AccountUiFixture.cpp | `1aadd57f296bda68b23254e768e1ba1e5a0b3185fd2a39bbe18b55cc6e88a594` |
| test_account_ui.py | `9219c5f0ee7140591b56526c344d7d9dafc6b38a0147d98198a11570122f9d24` |

范围：已认证 Profile 详情展示完整服务器 user_id；昵称修改不改变 ID；退出或密码修改使会话失效后清除详情中的旧 ID；重新登录恢复；只读详情可完整选中，不打开或改写用户剪贴板。

测试采用随机 loopback 端口的内存认证 fixture，仅私有 stdin 查询合成账户 ID；正式服务、真实账号及 token 不参与。执行包在自有 kill-on-close Job 中，240 秒上限，APPDATA/LOCALAPPDATA/TEMP/TMP 全部位于 D 盘。

边界：仅 English UI，其他语言、实际剪贴板复制未验收；不尝试 WindowStation 权限、不构建、不执行 Git、不改开发源码。

本次新增证据仅存本 D 盘目录，Python `-B`；没有执行主动 C 盘写入。操作系统、Codex 应用内部不可控写入未测量。
