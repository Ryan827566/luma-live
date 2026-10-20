# Account page and authentication acceptance

## 2026-10-20: certificate transaction isolation acceptance

Same-CA leaf activation and rollback now retain a private write-ahead journal, serialize with deployment commands and verify the certificate actually served after owned startup. An interrupted transaction blocks ordinary preparation/start/restart until explicit recovery restores the saved old pair. Activation and recovery require `--allow-service-restart`; they invalidate active sessions and are not routine client checks.

Nineteen isolated native TLS/Windows lifecycle tests passed with fixture cleanup complete, including five process-exit fault boundaries and terminal-record failures. The thirteen certificate, fourteen operation, fourteen shutdown and three launcher regressions also passed. One helper reviewed the final code without blocking findings. See `output/account-certificate-transaction-tests.json` and local-account-deployment.md for the exact scope and limitations.

No production activation/recovery/restart, GUI rerun or binary rebuild occurred. Read-only production TLS remained healthy with no pending recovery, and actual EXE SHA256 remained `5577a41e560fcafbead23d012fac6057ba41b3bdf338aac0df7740587b00a610`. The user's open client was left untouched. This is isolated deployment acceptance; production renewal, expired-old emergency repair, CA rotation and physical UI/provider acceptance remain open.

## 2026-10-20: deployment serialization and startup ownership boundary

Account preparation/start/stop/restart and delivery-configuration publication now share a Windows command mutex. Startup uses a gated worker in an owned Job, confirms its supervisor identity/readiness, and cleans the entire owned subtree on startup failure before unlocking. Provider token input does not hold the mutex; saving merges with the latest other-channel configuration.

Fourteen isolated operation tests, fourteen service-control regressions and three launcher tests passed. The new cases use random Windows control objects, dynamic ports and the in-memory account executable, including startup cancellation during the server-to-Job assignment window and a ready service surviving its startup caller's exit. Reports: `output/account-operation-tests.json` and `output/account-service-control-tests.json`. Final read-only review by one helper found no blocking issues.

This is deployment-control work. No production restart, certificate activation, GUI rerun or client rebuild was performed. Read-only production status remains ready and actual EXE SHA256 remains `5577a41e560fcafbead23d012fac6057ba41b3bdf338aac0df7740587b00a610`. The actual GUI evidence below is unchanged; certificate switching, rollback and recovery still need isolated acceptance and later authorized production acceptance.

## 2026-10-20: service shutdown acknowledgement boundary

Fourteen isolated service-control tests passed after replacing state-file-based stop completion with Windows ownership release and port-vacancy checks. Fixtures run the actual Python supervisor/Windows Job with the in-memory account executable, including accepted connections held open during stop or closed beforehand; remaining cases exercise startup delays, missing metadata, owner abandonment, timeouts and foreign listeners. See `output/account-service-control-tests.json` and local-account-deployment.md.

No production service stop/restart, certificate change, GUI rerun or client rebuild was performed for this slice. The actual-client acceptance below remains the GUI evidence. Certificate activation/rollback and concurrent deployment-operation serialization are still open.

## 2026-10-19: successful resend after the real cooldown

Actual client SHA256 `5577a41e560fcafbead23d012fac6057ba41b3bdf338aac0df7740587b00a610` passed the optional real-time resend acceptance. The test waits at least 61 seconds after consuming the initial fixture delivery, with no clock injection. In this run the measured interval was 61.001 seconds. Resend from an outstanding MFA prompt preserved the phone, cleared the SMS code, cleared/hidden MFA, and generated a new private fixture delivery. The old code failed against the new challenge; the new code required a fresh second factor and then unlocked the workspace. The full account UI workflow and cleanup also passed.

```powershell
C:/Python314/python.exe -B scripts/windows/test_account_ui.py output/next/Release/luma_studio.exe --real-sms-resend
```

The separate `output/account-ui-sms-resend.json` records timing, client/fixture hashes and assertions without codes or recovery secrets. Only `status=passed` with `fixture_cleanup_complete=true` is complete evidence; a `running` report can indicate an interrupted or failed startup. The consumed mailbox makes new delivery independently observable. If random OTP values coincide, the report explicitly marks old/new-code discrimination unavailable instead of failing a valid collision. This run had distinct codes and recorded rejection.

The default invocation retains its fast path and separately passed after the opt-in run. Both modes use an isolated in-memory server with `AUTH_ENV=test` and empty client CA, and affect only their own client processes. A final SHA256 comparison confirmed the actual EXE was unchanged, and the production service remained ready. No production service restart, certificate activation or EXE rebuild is required. This supersedes the October18 real-60-second resend exclusion below; real supplier delivery and physical input/accessibility acceptance remain open.

## 2026-10-18: TLS diagnostics and candidate boundary

Read-only real-deployment TLS diagnostics passed: disk/served leaf and adjacent client CA match, with valid chain/IP/server purpose/key pairing. Thirteen isolated certificate tests passed for renewal candidate creation and failure handling. No production certificate was staged/activated, no service was restarted and no EXE was rebuilt. These results establish diagnostics and candidate preparation, not renewal activation/rollback or fresh GUI acceptance. See local-account-deployment.md and `output/account-certificate-{status,tests}.json`.

## 2026-10-18: online backup acceptance boundary

The two persisted account tables now have current-user encrypted online backups and isolated restore verification using the same exported snapshot. Real deployment data/schema equality and a separate concurrent-update fixture passed, alongside 11 failure-boundary checks. No client/server binary was rebuilt and no production service was restarted for this work; the GUI acceptance below remains the latest actual-EXE evidence. Complete disaster recovery, certificate renewal and real delivery-provider acceptance remain separate gates. See local-account-deployment.md for the backup scope and reports.

## 2026-10-18: local launcher, email login and focused SMS flow

Double-click the repository-root `Start-LumaLive.cmd` to start/check PostgreSQL and the TLS account service and open the actual production client. It uses a project-local endpoint file and removes inherited test/TLS overrides from the child process. Service preparation/readiness errors and immediate nonzero client exits keep the command window open with an error. Root `启动说明.md` documents registration and **email + password** login; real email/SMS delivery still requires provider configuration.

SMS sign-in now shows only calling code/phone number, Send code and Password sign-in. A successful request reveals the SMS code and sign-in/resend actions. Register, password recovery, more-options and manual MFA links are absent from this flow. Editing to a different normalized phone number clears the old code and returns to the first step; equivalent formatting retains it. A rejected resend preserves the outstanding challenge. Request acknowledgements use conditional wording because the protocol intentionally hides unknown phone numbers and provider rejection.

MFA appears only after the server requests it for a valid SMS code. Missing MFA no longer consumes the SMS challenge; wrong supplied factors use the existing shared five-attempt budget, and a correct recovery code is still one-use. Native regression advances the injected test clock for the second SMS request, preserving production resend throttling.

Verification on actual `output/next/Release/luma_studio.exe`, SHA256 `5577a41e560fcafbead23d012fac6057ba41b3bdf338aac0df7740587b00a610`: all **27** unified checks passed, including **675** native entrance/layout states and GUI SMS/IME-Enter/number-edit/throttled-resend/MFA flows. Three launcher checks passed for environment isolation, fail-closed readiness and immediate client failure. Real PostgreSQL + TLS GUI acceptance separately passed registration, email/password sign-in and workspace identity, logout, client restart/email re-login, and deletion of only the generated test account (`output/local-account-email-verification.json`). The account service remains running. Current UI captures: `output/ui-sms-20261018/zh-CN` (isolated UI fixtures, not proof of real SMS delivery). Actual supplier delivery, physical IME/Narrator/multimonitor and successful resend after a real 60-second interval remain outside this run.

Earlier sections below are dated history; use this checkpoint and `local-account-deployment.md` for current startup instructions and deployment boundaries.

## Delivery scope (2026-10-13)

Development now takes place on `main`, at the user's request. The complete video/AI branch and account-registration branch were merged, preserving both histories. The original local index and changed files were backed up before reconciling the working copy to main.

The actual client at `output/next/Release/luma_studio.exe` hosts **Account** in its left sidebar. `--account` opens this page inside the same workspace, including the full application entry point. This is independent of the AI gateway; credentials are never sent to an AI service.

- Signed out: password or SMS sign-in, account creation with password confirmation, password recovery, service address/port.
- Signed in: profile editing, sign-out, inline account deletion, email/phone verification, password changes, two-step verification with a visible authenticator key and recovery code, security activity, device listing/removal/sign-out of other devices and session refresh.
- Background requests retain page state while the user switches modules. Closing the workspace does not wait for the UI worker. Password fields clear after submission; raw server diagnostics and tokens are not placed in status messages. The authenticator setup key/recovery code is deliberately displayed for the user to save, and clears when navigating account tabs.
- Confirmation captures the selected device identity; changing a phone number requires a matching new verification challenge. Escape cancels inline confirmation. Enter submits a focused single-line field except during IME composition.
- Simplified/traditional Chinese and English reuse the existing system-language resources, native edit accessibility, dark controls, keyboard focus and reduced-motion-aware hover treatment. Other system languages currently fall back to English.

## Corrected authentication defects

1. Natural access-token expiry no longer destroys a still-valid refresh session. Expired refresh credentials and revoked sessions are removed; client user/session caches clear on revocation.
2. Password reset accepts a valid token obtained by a different client instance and generates fresh password salt. The server validates token, salt and verifier formats.
3. Connection establishment and each complete protocol send/receive have a fixed five-second deadline. Partial bytes do not reset it. Failed transport closes the connection and clears cached identity. These are phase deadlines, not a five-second limit on an entire multi-step login or password derivation.
4. Empty edit fields return empty strings in the legacy diagnostic form. The production main entry now routes account access to the hosted page.
5. CMake no longer references the nonexistent server-local AuthCrypto.cpp. Shared authentication cryptography links into both client and server, including the standalone client build. Authentication assertions remain enabled in Release regression targets.

## Verification

- `luma_auth_integration_tests`: real local client/server protocol against an isolated in-memory store, including expired access/refresh, revoke/cache cleanup, cross-instance reset, verification/MFA, password changes and audit events.
- `luma_client_account_tests`: an unresponsive local peer, bounded receive failure and prompt Stop after failure.
- `scripts/windows/test_account_ui.py`: actual shipped EXE at 1024x700 with a separate test-only in-memory auth server; connection/port validation, required fields, mismatched passwords, register, incorrect/correct login, profile update, navigation retention, inline delete cancellation, device list, MFA on/off, activity, logout and recovery request.
- `scripts/windows/verify_client.py` includes these three checks in the existing actual-EXE regression, records the binary SHA256 and all logs. Check `output/client-verification.json` for the current run result rather than inferring that a build equals full acceptance.
- Account screenshots are under `output/ui-account-20261013`. Synthetic IME messages do not replace a real input-method or Narrator acceptance session.

Latest run: all 23 actual-EXE checks passed. EXE SHA256 `14427495494c0670884b65ff499f3ba68db7e6c8e68dcf818ee230655bca8266`. Compact Chinese sign-in/registration/recovery/connection captures were inspected.

## How to try it

Open `output/next/Release/luma_studio.exe`, choose **Account**, then **Connection**. Use the address/port of your running authentication service (default 127.0.0.1:9100), connect and create/sign in to an account.

For automated local acceptance only, run `C:/Python314/python.exe scripts/windows/test_account_ui.py output/next/Release/luma_studio.exe` from the repository root. It launches and cleans up its own in-memory fixture and temporary accounts, without touching a configured PostgreSQL database.

## Remaining production gates

The login UI is connected to the repository's real account protocol. This does **not** complete production identity deployment. The bundled email/SMS providers currently implement development/test delivery only; production providers, PostgreSQL deployment/persistence acceptance and authenticated encrypted transport still require implementation/configuration and acceptance. No real email or SMS was sent in these tests. The UI does not persist passwords or login tokens; remembered-session behavior is not implemented. Physical multi-monitor DPI, real IME/Narrator and final visual review remain separate from automated geometry checks.

Additional keyboard validation on the same EXE passed queued Enter submission and simulated IME Enter suppression. The IME assertion checks that no request/page transition occurs; native composition is allowed to change edit text. Traditional Chinese compact account captures were also inspected. This does not certify a physical IME.

Account details now hide the scrollbar for short text and recalculate overflow after content, layout and DPI changes. Actual-EXE coverage verifies short/long/short transitions and scrolling security activity; all 22 checks passed on this binary.

## Connection settings persistence (2026-10-13)

A successful explicit connection saves only the IPv4 address and port under `%LOCALAPPDATA%/LumaLive/account-connection.conf`. A fresh client restores these fields; it does not reconnect automatically or remember credentials. Failed connections do not replace the saved endpoint. Saving runs with the request worker; a save error is shown separately while the successful connection stays usable. Malformed/oversized configuration falls back to defaults with a visible message and leaves the file untouched.

The native settings regression covers bounded parsing, strict IPv4/port validation, Unicode paths, atomic replacement failure and preservation of prior contents. Actual-EXE UI tests use a temporary `LUMALIVE_ACCOUNT_SETTINGS_PATH`, verify restart restoration and empty credentials, then verify failed connections and malformed files preserve saved bytes. Tests never modify the user's account connection settings.

After this integration, all 23 actual-EXE checks passed, including the added native account settings regression.

## Authentication challenge bounds (2026-10-13)

Registration, password login and password change now validate the entire server challenge before password derivation: packet type/count, 16-byte salts, 32-byte nonce, hexadecimal encoding, login MFA flag and decimal iteration count from 1 through the current protocol ceiling of 600000. Invalid parameters disconnect the client, clear cached identity and return the stable `invalid_challenge` error, displayed as invalid service authentication data rather than a user verification-code error.

The client regression adds 77 scripted loopback-server cases for malformed/oversized iteration counts and malformed fields. Each establishes a synthetic prior identity and verifies prompt failure without an exception or a proof/verifier reply, followed by cleared session state. A separate process with a ten-second watchdog prevents an old unbounded calculation from hanging the suite. Existing real local-server tests cover the normal 600000-iteration path. These bounds do not supply the still-missing authenticated encrypted transport.

The full 23-check verifier passed after the challenge-boundary fix, including the 77 malformed-server cases and normal local-server account workflows.

## Full account workflow and transport deadline follow-up (2026-10-13)

The native client now uses nonblocking sockets with monotonic deadlines for connect, send and receive. Regression cases cover a peer that keeps sending one byte every 200 ms, a silent peer, refused connections, fragmented responses and two protocol frames arriving in a single write. A timed-out authenticated connection loses cached identity. The explicit deadline of a black-holed connect and send-buffer saturation are not separately injected in these tests; they use the same deadline-aware transport path.

The hosted account test now supplies isolated email/SMS adapters through explicit service construction. Only the test fixture exposes captured synthetic codes through its private stdin/stdout pipe; no external delivery, user database or production debug-code response is used. Coverage includes delivery failures with localized status, invalid/correct email verification, phone-number/challenge mismatch, successful phone verification and SMS sign-in, incorrect/correct password changes, rejected old password, reset completed in a fresh client, consumed reset-token rejection and confirmed account deletion. This validates local end-to-end workflows, not production email/SMS providers.

All 23 actual-EXE checks passed after the deadline and delivery-flow changes. The real-provider, TLS, database-deployment and physical-device gates remain open.

Password visibility now works for registration, recovery, password changes and other password fields; it resets when changing pages. Actions follow the form instead of sitting at the window bottom. Native UI regression verifies masking/reveal/restoration and hidden reveal controls on profile, with all 23 checks passed. Compact Chinese captures are in output/ui-account-forms-20261013.


## 2026-10-14 authenticated workspace gate

Production starts at sign-in/registration only. Registration does not unlock modules. Valid sign-in shows the workspace and sidebar name/email. Direct hidden commands and legacy meeting switches cannot bypass authentication; meeting renders now use a separate diagnostic executable requiring an absolute output path.

Immutable UI snapshots avoid reading worker-owned service state. Quiet 60-second validation preserves edits and pending confirmations on success, and returns to sign-in on failure even while the account page is hidden. Pending confirmations no longer suspend validation. Already-running account requests finish before validation. Logout stops primary media and AI speech before destroying pages that may wait for HTTP, stops the owned gateway, and restores controls for re-login. Physical device/audio shutdown remains separate acceptance.

All 24 unified checks passed on client SHA256 `99ce034743201dbb3bffa463ebacf74baecde48a1eed8c1532778a1664d6bed4`; report `output/client-verification.json`. Checks combine native tests, actual-client workflows and independent meeting render diagnostics. New tests cover real local registration/sign-in, identity, locked commands, logout/re-entry, hidden pending confirmation during server loss, owned AI gateway logout cleanup and restart. Independent review findings were fixed and re-reviewed. No user database or real provider was contacted.

Compact Chinese/English captures: `output/ui-auth-gate-20261014/{zh-CN,en-US}`. Login, registration and account profile were inspected. Known visual follow-up: the English account title needs DT_NOPREFIX to render its ampersand literally; post-login account layout and product-wide final visual acceptance remain pending. Real SMS/email, TLS, PostgreSQL deployment, physical media/TURN/provider checks remain open.


## 2026-10-14 account settings visual refinement

The authenticated Account page now uses a centered, bounded content region and quiet native tabs. Form results and inline confirmations follow their initiating actions. Short profile/connection details use measured text height without a large empty frame; device, MFA and activity records retain native selection and scrolling. Literal ampersands in page titles render correctly. No authentication protocol or storage behavior changed.

All 24 unified checks passed on EXE SHA256 `5ed686e1eaf1a113d3398ef685c206cce5bcdfc2531513efc92cb2b322f070b2`. The account workflow additionally exercises all authenticated pages, five security modes, delete/device/MFA confirmations and cancellation, result proximity and short/long text scrolling at 1024x700 and 1386x788. The requested 1440x900 window was capped by Windows at 1386x788, so full 1440x900 and physical DPI acceptance are not claimed. The independent code review found no blocking issue.

Captures in `output/ui-account-refinement-20261014/en-US`, `zh-CN` and `zh-wide` include login, profile, deletion confirmation, security, password, device and connection pages. Their capture-manifest.json files bind every set to this EXE hash and actual client dimensions. English profile/delete and Chinese password/wide-profile images were inspected. Physical IME/Narrator/multiple monitors, real provider/media/TURN and final whole-product visual acceptance remain separate.


## 2026-10-16：简洁登录入口与可复现验收

- 默认登录仅显示账号、密码、登录和上下文链接；注册、找回密码、短信与双重验证按需展开，未登录不显示服务连接或账户页签。注册只填写用户名、邮箱、密码与确认，初始显示名称取用户名，登录后可编辑。
- 找回密码先填写账户，发送后显示验证码与新密码；短信先填写手机号，成功请求后显示验证码。区号根据 Windows 区域设置选中，可切换；完整 +E.164 号码优先，验证码仍绑定规范化号码。
- 密码显示/隐藏改为可聚焦、可读屏的小眼睛。登录默认560×560 DIP，范围480×560至960×800并受当前工作区限制；登录成功展开工作区，退出恢复入口尺寸。采用本机 Codex 的紧凑引导尺寸，不宣称所有 Codex 版本都只有这一尺寸。
- 应用图标保留LumaLive原创图形，ICO资源现含与本机Codex资源相同的15档像素尺寸；界面图标20 DIP，按DPI加载图标，不将单一32像素图重复放大。
- 实际客户端25项统一检查全部通过；SHA256 `574c35d6b90e7870c1da11eca2a067106a498f127e2d8c97164544a0f20bcb97`。其中405组原生入口几何/文字测量覆盖简体、繁体、英文，96/144/192模拟DPI、三种尺寸和15种状态。真实UI协议验证注册→登录→工作区、MFA凭据保留、短信区号与挑战绑定、重置、退出、无效连接不覆盖配置。
- 截图：`output/ui-focused-entrance-20261016/{zh-CN,en-US}`，包含注册、初始找回/验证码页、短信、双重验证与工作区；清单绑定实际EXE哈希。物理IME/读屏/多屏另待验收。
- `scripts/windows/run_client_checks.cmd`可双击运行统一测试，交互终端保留结果；测试EXE是命令行测试，不是客户端。
- 本机正式账户服务仍未部署成功。已重新构建 `output/next/Release/luma_auth_server.exe`；无运行账户服务/PG/连接配置，解释了用户无法登录。PostgreSQL部署进行中；TLS和真实投递配置仍未完成。上述测试使用独立内存fixture，不代表生产服务验收。


## 2026-10-17：入口收窄、切页清理和注册错误

- 入口表单固定上限320 DIP；小眼睛嵌入密码框右侧，文本右边距留出空间。切换登录、短信、注册、找回密码时精确隐藏旧控件并清空旧字段，重新进入时恢复密码遮罩。空状态文字控件隐藏，长错误按实际文字高度排版。
- 用户名、邮箱、过短/过长密码与两次密码不一致在本地明确提示；服务器保存失败与用户名/邮箱已注册分别提示，不再统一误报输入有误。测试fixture故障注入确认保存失败不会进入工作区。
- 实际EXE统一25项全部通过，SHA256 `37080432db134932ccb01c05ef22d58feffc9b4cb5a442eaf33566b797d47f80`。原生648组检查覆盖简体/繁体/英文、96/144/192模拟DPI、三种尺寸、精确可见控件集合、切页清空与密码遮罩；模拟DPI不是实际多屏验收。
- 完整窗口截图 `output/ui-login-final-20261017/{zh-CN,en-US}`。已检查中文登录/注册/重置及英文最小宽度注册，输入框和眼睛无意外重叠。
- 已请求Windows11原生标题栏同色，但当前测试环境DWM颜色属性返回E_INVALIDARG，截图仍为系统旧式标题栏。**标题栏同色尚未在本机交付**，需要后续兼容实现，不能将API调用等同视觉通过。
- 统一验证改为一次MSBuild依赖目标，仍构建实际客户端及原有测试，减少重复启动开销。
- PostgreSQL本机实例已运行，应用账号连接和重复启动检查通过；账户TLS/正式服务启动仍在开发，本轮UI测试仍使用隔离内存fixture。


## 2026-10-17：标题栏兼容与真实账户部署

- 对DWM颜色不受支持的系统，复用品牌行作为同色标题栏，保留窗口移动、边角缩放、最小化/最大化/还原/关闭和系统菜单。实际截图 `output/ui-chrome-20261017/zh-CN` 已检查：旧浅色标题栏消失、密码眼睛位于框内。自动检查覆盖命中区域、缩放边角和窗口按钮；物理多屏/读屏另验收。
- 账户客户端生产默认要求TLS，验证CA/IP；测试模式显式隔离，不读取部署相邻CA。服务默认只监听127.0.0.1。TLS专用脚本和原生account/auth三项通过；修复停止时未完成握手等待过久的问题。
- 真实PostgreSQL账户服务已运行，真实GUI完成注册/登录/退出/服务重启后再次登录/删除测试账户，报告 `output/local-account-verification.json`。被测实际客户端SHA256 `22b201b08c762e90de464e2046f46cc59df6590a93e0d4f89b2d5b457aa8200f`。具体启动、存储和限制见local-account-deployment.md。
- 邮件/SMS现在支持明确disabled与可配置HTTPS webhook；生产不使用开发假投递。真实投递尚未验收，详见account-delivery.md。

最终统一回归：上述22b201b0实际EXE的27项检查全部通过（含邮件/SMS配置拒绝、账户UI、门禁、AI与会议回归）。最新完整窗口截图 `output/ui-account-production-20261017/zh-CN` 清单绑定同一EXE哈希。账户服务仍保持运行；Windows Startup目录写入被系统拒绝，自动启动尚未安装，不宣称已交付开机启动。
