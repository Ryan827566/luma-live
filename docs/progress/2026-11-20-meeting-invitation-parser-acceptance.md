# 会议邀请解析独立验收

日期：2026-11-20。范围：解析器与纯 C++ 测试独立验收通过；真实剪贴板粘贴 UI 未验收，不构成发布依据。

## 当前结论

- 真实 UI 隔离运行在创建 WindowStation 时被系统拒绝（WinError 5，Access denied），尚未启动客户端或认证 fixture，未执行任何粘贴动作。
- 未打开或改写用户剪贴板。失败运行的 owned Job 已清零，私有 profile 已清理。
- 该失败属于测试环境隔离不可用，既不能说明产品粘贴失败，也不能视为产品通过。
- root 决定先提交解析器、纯测试及 CMake；MeetingPreview UI 与 Python QA 脚本保留候选，不发布。
- 不再尝试 WindowStation 权限，不启动 UI，不触碰用户剪贴板。

## 独立执行与审查结果

直接执行现有 Release `luma_meeting_invitation_tests.exe`，未重编，设置 15 秒 timeout，实际退出码 0，stderr 为空。四组输出全部 PASS：

1. 中英三行 Copy 格式、CRLF/LF、可选末尾换行、原样字段、结果独立持有内存。
2. DNS/IPv4、默认端口 9000、十进制端口 1–65535、主机边界；明确拒绝当前 Join 不支持的 IPv6。
3. 房间号最多 128 UTF-8 字节、合法 Unicode scalar/surrogate 对及边界。
4. 缺失/重复/未知键、额外行、超长文本、控制字符、嵌入 NUL 拒绝。

已独立读取解析器与测试源码：解析函数无剪贴板、身份、连接或配置写入副作用；成功返回 server/room 自有字符串，完整验证后返回结果。CMake 注册独立 C++20 纯测试、`/utf-8` 和 10 秒 CTest timeout，并纳入 verification 依赖。

已读取 root 原始 CTest 日志与集成 JSON：6/6 通过，包含 call、RTC config、meeting session、meeting media、invitation、network settings；CTest 日志耗时 14.59 秒，build 144.203 秒，集成 Job 清零。独立检查的 studio SHA256 与该集成 JSON 相同。

审 UI 候选时发现 `value()` 旧固定 256 wchar 缓冲会截断解析器接受的 260 字符 server。已向 root 报告，并在冻结源码确认改为 `GetWindowTextLengthW` 动态读取。此为源码确认，不能替代真实粘贴或加入测试。冻结源码中 Joining/Joined 防护位于读取剪贴板前；先完整解析再写 server/room，Identity 未更改，粘贴分支无连接调用。

## 验收边界

仅解析器、纯 C++ 测试及 CMake 可随本轮提交。未完成真实粘贴后的字段保真、展开、无自动连接、无效内容保留、Joining/Joined UI 防护、中英布局截图或硬件键盘验收。Python 脚本仅语法检查通过及隔离失败清理已证实，其余 UI 断言没有执行，不能计入通过项。没有生成有效 UI 截图。

独立 SHA256：

| 文件 | SHA256 |
| --- | --- |
| luma_meeting_invitation_tests.exe | `781ab217adf31698ef317c2479d5836602fa59911d8506dca09b7ac77d9251d9` |
| luma_studio.exe | `7e997cd874025e157781c69fc675d77cc7b07f2d2094e2f519d97d5eb1c3779b` |
| MeetingInvitation.hpp | `fc73a43ec392eee8eb12991214edea6a91b41f310f070f996d3284bc21a96ad3` |
| MeetingInvitationTests.cpp | `bf05411925f8784483b20e9e3e8337f6a90396834c0f5b20148505a478aef93e` |
| CMakeLists.txt | `17129aa80d2dbb27e5097afdbe4f620d70b5a22b4d458fea7d9fb518640b830d` |

## 证据

- 隔离失败及清理报告：`invitation-ui-en-US-3123a07eed2040128d366e2079c235c4-final.json`。
- root 集成记录：`../release-build-integration-20261120.json`、`../release-build-ctest-20261120.log`。
- 独立解析器实际执行：`parser-independent-20261120.json`（退出码、二进制 SHA256、完整 stdout/stderr）。

## 存储

本次新增脚本与 QA 证据均在 D 盘；Python 使用 `-B`，TEMP/TMP 指向 D 盘。没有执行主动 C 盘写入；没有测量操作系统或 Codex 内部不可控写入。
