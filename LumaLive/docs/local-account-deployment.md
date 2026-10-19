# 本机账户服务部署与测试

## 停止完成确认加固（2026-10-20）

`manage_account_service.py stop` 现在等待受管进程释放 Windows 所有权互斥量，并确认账户端口已空闲。
`process.json` 仅供诊断；文件提前消失、缺失或残留都不能直接代表服务已停止。管理进程先清理自己拥有的子进程和状态，再释放所有权。
启动期间停止事件尚未创建时会在同一总期限内等待；没有所属管理进程但端口被其他监听器占用、访问权限错误或停止超时均明确失败，不按 PID 杀进程。

14 项隔离测试通过，覆盖真实 Windows 互斥量/事件、异常退出、无响应与延迟端口释放，并用实际管理程序/Job 对象停止独立内存账户服务。
实际建立请求连接后，保持连接直到停止返回、先关闭连接再停止两种情况均通过；跨进程所有权断言保留互斥量句柄，避免对象提前销毁掩盖遗漏释放。
报告为 `output/account-service-control-tests.json`，可复现命令：

```powershell
C:/Python314/python.exe -B scripts/windows/test_account_service_control.py -v
```

测试使用随机控制对象、动态端口和专属临时目录；没有停止/重启正式账户服务，也没有改变证书或实际客户端 EXE。
运行中的正式管理进程仍使用启动时加载的旧代码，后续经授权重启才加载本次修复。
此修复仅为证书切换补齐停止确认条件；准备/启动/停止的共享操作锁、可恢复证书切换、切换后验证与失败回滚仍待实现，生产证书激活仍未验收。

## 短信真实冷却时间界面验收（2026-10-19）

实际客户端已在独立内存账户服务下完成真实等待 61.001 秒后的重发验收：保留手机号、清空短信码和 MFA 输入、隐藏 MFA，并收到新一次测试投递；旧验证码在新挑战下被拒绝，新验证码再次要求第二因子，提交后成功进入工作区。完整界面流程和测试资源清理均通过，证据为 `output/account-ui-sms-resend.json`。默认快速测试与可选 `--real-sms-resend` 命令见 login-acceptance.md。

本轮未改生产代码、证书或实际 EXE，也未启动/停止生产服务。这里验证的是实际界面与测试投递适配器之间的重发流程，不能代替供应商真实短信送达验收；证书激活/回滚及其他部署缺口仍待完成。

## TLS 到期检查与续期候选（2026-10-18）

运行以下只读检查，不会启动/停止服务、改证书或修改 Windows 信任库：

```powershell
C:/Python314/python.exe -B scripts/windows/manage_account_certificates.py status
```

检查分别报告 CA、磁盘服务器证书、客户端相邻 CA 的有效期和 SHA256；验证证书链、服务器用途、
`127.0.0.1`、服务器私钥配对，以及实际在线 TLS 证书是否与磁盘一致。在线探测使用客户端 CA，
连接、握手及完整协议响应共用三秒总期限。默认提前 30 天提示，可指定 `--warning-days 60`。
退出码：0 正常、1 临近到期、2 检查失败；`issues` 和 `warnings` 给出对应原因，不会自动修复或重启。

本轮真实部署检查正常，报告 `output/account-certificate-status.json`。当前服务器证书到期时间为
**2027-10-17 06:53:58（北京时间）**，当前 CA 到期时间为 **2031-10-16 06:53:57（北京时间）**；
这两个时间仅对应本次检查的证书，以之后运行 `status` 的结果为准。

需要准备续期时，可先生成一个独立候选：

```powershell
C:/Python314/python.exe -B scripts/windows/manage_account_certificates.py stage --days 365
C:/Python314/python.exe -B scripts/windows/manage_account_certificates.py inspect --candidate-id <候选ID>
```

候选在 `output/services/account-certificate-staging/<候选ID>`，目录只允许当前用户/SYSTEM。
它使用新的 RSA3072 私钥、现有 CA 签名和独立随机序列号；不会改动当前 CA 序列文件、服务器证书或客户端 CA。
候选有效期为 1–365 天且必须处于 CA 有效期内。旧服务器证书过期或服务离线不妨碍生成合法候选。
签名、证书链/IP/用途、密钥配对、有效期和来源指纹都通过检查后，目录才从 `.partial` 发布；
`inspect` 会重新核验，拒绝被改动、已过期或因当前证书/客户端 CA 变化而过时的候选。
候选内含私钥，不应分享；失败的 `.partial` 目录不属于已验证候选。

**尚未激活：** 这两个命令只生成和检查候选，报告明确标记 `activated=false`。本轮只在隔离测试 CA 下验证续期，
没有签发或替换真实部署证书。原子切换、重启后验证、失败回滚、CA 轮换和自动续期调度仍待实现；
不能把候选生成视为完整证书续期交付。

13 项隔离测试覆盖分片响应、总期限、错误密钥/IP/用途、来源或客户端 CA 不一致、CA 寿命不足、
旧证书过期后生成候选、签名失败不发布、候选被改动/过时、只读检查和 OpenSSL 超时诊断。
报告 `output/account-certificate-tests.json`。生成工具使用项目内最小 `account-openssl.cnf`，避免继承外部 OpenSSL 配置。

## 账户在线备份与隔离恢复演练（2026-10-18）

账户服务运行时，双击根目录 `Backup-LumaLive.cmd`。无需关闭客户端或退出登录。
脚本在固定的本机受管集群中导出账户表、安全活动表及其表结构/索引，使用当前 Windows 用户 DPAPI 加密，
再从该密文恢复到独立、随机命名的临时数据库。恢复角色没有生产库写入权限。
只有行数、逐行内容摘要和表结构摘要全部与导出时同一个只读快照一致，且临时库/角色清理成功后，
备份目录才从 `.partial` 原子发布为成功备份。备份期间正常账户写入不会混入旧快照。

成功目录在 `output/services/account-backups/<备份 ID>`，仅当前用户/SYSTEM 可访问，包含：

- `accounts.dump.dpapi`：加密的 PostgreSQL 自定义格式归档；包含密码验证材料及 MFA 密钥等敏感账户字段。
- `manifest.json`：来源集群、格式、校验和、快照摘要；不含密码、连接串或账户明细。
- `verification.json`：恢复验证时间、一致性结果与临时资源清理结果。

也可以在项目根目录执行：

```powershell
C:/Python314/python.exe -B scripts/windows/manage_account_backup.py create
C:/Python314/python.exe -B scripts/windows/manage_account_backup.py verify --backup-id <完整备份ID>
```

`verify` 会重新解密、恢复及校验已有备份。它只接受本项目的备份 ID，不能指定生产库为恢复目标；
不替换现有库、不停止服务、不终止现有连接。归档损坏或来源身份不符时，在创建恢复库前失败。
备份/验证任务互斥；等待锁和 SQL 执行均有期限。若临时库有额外连接或身份变化，清理会失败并报告名称，
不会强制删除。失败的 `.partial` 目录不算已验证备份，正常异常路径会删除临时明文文件。

本轮已对真实本机账户数据完成加密备份和隔离恢复验证；独立 fixture 还验证了快照期间并发 UPDATE、
DPAPI 往返、表结构/数据摘要及临时资源清理，报告 `output/account-backup-integration.json`。
11 项失败边界单测覆盖损坏归档、跨部署/路径拒绝、恢复失败、清理失败、未验证备份不发布及加密大小边界。

**范围边界：** 当前仅覆盖 `luma_auth_users` 和 `luma_auth_security_events` 两张表及其结构，明文归档上限 256 MiB。
不备份进程内会话/验证码、TLS 私钥、投递配置或 PostgreSQL 管理凭据，也不做定时备份/自动保留清理。
DPAPI 与当前 Windows 用户保护上下文绑定，校验器还限定原集群身份；不能凭此文件单独恢复到新电脑/新用户。
本机同盘副本不防磁盘损坏。断电后的临时明文残留清理、异机灾难恢复、正式库切换/回滚、自动备份保留策略
仍待交付，不应把这次隔离恢复演练描述为完整灾难恢复验收。

## 日常启动

2026-10-18：日常使用请双击项目根目录 `Start-LumaLive.cmd`。脚本启动/检查数据库、TLS 账户服务、证书与运行库，确认就绪后打开 `output/next/Release/luma_studio.exe`。`--server-only` 只启动服务。启动失败会保留错误窗口。参见根目录 `启动说明.md`。

启动器用项目内 `output/services/account/local-client.conf` 固定连接本机服务，并为本次客户端进程清理继承的测试模式和 TLS 覆盖项；不覆盖其他部署的用户连接设置。注册后用邮箱和密码登录即可进入主界面，无须先验证邮箱。真实邮件验证码/短信服务仍未配置，不能把邮箱密码登录等同于真实邮件投递。

`scripts/windows/test_local_account_deployment.py --keep-service-running` 可验证真实 GUI 注册、邮箱密码登录解锁工作区、退出、客户端重开后邮箱登录、删除本次测试账户，不重启服务。报告 `output/local-account-email-verification.json`。原 `--allow-service-restart` 保留真实服务重启验收。

2026-10-17：正式程序已在这台 Windows 电脑上完成真实注册、登录及重启后登录验收。该部署仅供本机使用，不是公网部署。

| 项目 | 当前实际配置 |
|---|---|
| 客户端 | `D:\workspace\luma-live\output\next\Release\luma_studio.exe`，直接双击 |
| 账户服务器进程 | `luma_auth_server.exe`，由后台 Python 管理程序持有和关闭 |
| 账户服务地址 | `127.0.0.1:9100`，TLS；默认只监听本机 |
| 数据库 | PostgreSQL 17，`127.0.0.1:15432`，数据库/应用角色 `luma_auth` |
| 账户数据 | `output/services/postgresql/data` 内的真实 PostgreSQL 数据；`luma_auth_users` 保存账户、盐与密码派生验证值，不保存明文密码 |
| 安全活动 | 数据库 `luma_auth_security_events` |
| 会话 | 仍在服务端内存；服务重启后需要重新登录，账户本身保留 |
| 数据库密码/投递配置 | 当前 Windows 用户 DPAPI 加密文件；不提交 Git，不写进客户端，不在命令行传入 |
| TLS | 专用本机 CA，只放入客户端目录，不安装到 Windows 全局信任库；私钥目录仅当前用户/SYSTEM 可访问 |
| 邮件和短信 | 当前明确停用；可配置 HTTPS 投递网关，未验证真实提供商，不会返回假验证码 |
| 自动启动 | 尚未安装：写入当前用户 Startup 目录被系统拒绝；目前后台服务保持运行，重启电脑后需执行下方start命令 |

## 直接测试

1. 双击上述客户端，在“注册”页填入用户名、邮箱、密码与确认密码。
2. 注册成功后返回登录，使用新账户登录；登录成功才显示功能工作区与账户信息。
3. 退出再登录。关闭客户端不会删除账户。

服务地址已通过部署配置写入当前用户的 `LOCALAPPDATA/LumaLive/account-connection.conf`，入口不暴露服务连接表单。原有不同地址的配置会被保留并提示，不会偷偷覆盖。

## 管理命令

在仓库根目录运行：

```powershell
C:/Python314/python.exe scripts/windows/manage_account_service.py status
C:/Python314/python.exe scripts/windows/manage_account_service.py start
C:/Python314/python.exe scripts/windows/manage_account_service.py stop
C:/Python314/python.exe scripts/windows/manage_account_service.py restart
```

`start` 检查数据库、证书、客户端信任文件和 OpenSSL 库，并可重复执行。后台管理程序通过独有事件通知自己创建的服务停止，Job 对象防止管理程序退出后遗留子进程；不根据保存的 PID 杀进程。重启会使现有会话失效。

本机证书初次生成/检查使用已安装 Git 中的 OpenSSL 命令行工具，运行时使用已校验 PostgreSQL 包中的 OpenSSL 3 DLL。服务器证书有效期一年，启动前检查至少剩余一天；自动证书轮换、无人登录的 Windows 服务安装和完整灾难恢复仍未交付。账户在线备份与隔离恢复演练见本文开头。

日志：`output/services/account/{service,supervisor}.log`。不要分享 `credentials.dpapi`、`providers.dpapi` 或 TLS 私钥。停止服务不会删除数据库。

## 已执行的真实验收

`scripts/windows/test_local_account_deployment.py --allow-service-restart` 对实际客户端和上述 PostgreSQL/TLS 服务执行：注册落库、登录解锁工作区与显示账户名、退出锁定、重启服务后账户保留并重新登录、通过客户端删除本次生成的测试账户。报告：`output/local-account-verification.json`，客户端 SHA256 `22b201b08c762e90de464e2046f46cc59df6590a93e0d4f89b2d5b457aa8200f`。测试结束保持服务运行。

此脚本会重启本机账户服务，不能当作不影响已有会话的日常健康检查。普通检查使用 `status`。本轮早期两次自动化失败来自测试在控件创建完成前填值，已补就绪等待；这两次生成的测试账户已清理。

独立 TLS 验收覆盖证书链/IP、不降级为明文、五秒握手与分片帧截止、登录后空闲61秒以及同时存在空闲/未完成握手连接时的停止。真实邮件、短信、公网部署、长期压力、备份恢复和物理输入法/读屏/多屏仍为独立待验收项。
