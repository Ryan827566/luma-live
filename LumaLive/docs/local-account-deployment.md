# 本机账户服务部署与测试

## 证书事务只读诊断（2026-10-20）

现在可以读取错误提示涉及的事务记录，不必手工打开含私钥的事务目录：

```powershell
C:/Python314/python.exe -B scripts/windows/manage_account_certificates.py transaction-status
C:/Python314/python.exe -B scripts/windows/manage_account_certificates.py transaction-status --transaction-id <32位事务ID>
```

默认只检查当前 pending；没有 pending 返回 `no_pending`，不推断历史操作成功。指定 ID 可检查 `committed` 或 `rolled_back` 历史记录。
没有终态或 pending 的记录标为 `incomplete`；双重终态、同 ID 的 pending 与终态并存、清单/快照损坏等标为 `invalid`。
报告中的 `current_live_pair` 单独区分 `old/new/mixed/external/unreadable`；
后续续期改变了活动文件或历史证书已到期，不会把原来的有效终态记录误判为失败。

对当前 pending，`recovery_assessment=files_eligible` 仅表示检查时的文件条件允许尝试现有恢复流程：
旧证书及 CA 有效、快照链/用途/IP/密钥匹配、信任未变、活动文件各自属于旧/新快照。
过期的未用候选不阻碍有效旧证书恢复；旧证书过期、文件丢失或外来文件、信任变化会报告具体阻断码。
这不是恢复成功保证：诊断不读取运行配置/投递密钥，不连接账户服务，不检查所有权或运行时准备情况，也不自动恢复。
只有在维护窗口内明确允许中断会话时，才执行下文实现的 `recover --allow-service-restart`。

命令对部署锁使用零等待；占用时返回 `busy`，不读取事务文件，`pending=null` 表示尚未观察。
锁访问失败与占用分别报告。JSON 读取实际限制为 128 KiB，证书解析共用十秒预算，错误只输出固定码而不回显文件内容或底层异常。
退出码：0 为无 pending 或有效历史检查；1 为存在 pending 或部署操作忙；2 为损坏、恢复阻断或检查失败。
历史查询遇到另一个当前 pending 会保留历史结果，同时提示检查当前事务。
`state` 可保留超时前已读到的事务类别；必须结合 `issues`、`recovery_assessment` 和退出码判断核验是否完成，不能仅凭 `committed` 推断本次检查成功。

14 项独立诊断测试通过，包括真实跨进程占锁/放弃锁、两种半对替换、旧/新/CA 期限、历史终态与后续文件变化、
坏 JSON/形状/大小/ID/hash、快照损坏、冲突和缺失终态、异常信息脱敏及只读 CLI。
每次诊断前后比较文件哈希，禁止调用生命周期、网络、运行配置和事务写入方法；测试目录清理完成。
13 项既有证书回归也通过，一名协作者完成最终只读审查，无阻断项。报告：`output/account-certificate-inspection-tests.json`。
本轮未重启正式服务、改正式证书或重编客户端，正式激活及其他未完成范围保持不变。

## 同一 CA 下的证书激活与可恢复回滚（2026-10-20）

已实现计划内叶证书续期事务，并完成隔离验收。只允许从正在运行、所有权明确、在线证书与磁盘一致的账户服务开始；
旧证书和候选证书开始时都必须还剩一天以上有效期。正式证书尚未激活，本轮没有重启正式服务或操作用户已打开的客户端。

激活与恢复共用部署操作锁。执行前保存旧/新证书和私钥、公有 CA、内容哈希及部署绑定，并发布 `pending.json`，随后才停止所属服务。
停止确认所有权释放和端口空闲后，逐个原子替换证书/私钥，再以现有 Windows Job 启动机制启动，验证实际在线 TLS 指纹及完整协议响应。
事务文件采用同目录临时文件、内容刷新和 Windows write-through 重命名；提交点是把 pending 记录重命名为事务目录内的终态记录。
重命名后返回异常时，只有再次核对终态记录、清单、活动文件、信任和在线指纹，才接受已提交或已恢复的结果。

任意 pending 状态都表示恢复旧证书，包括新服务已就绪但尚未提交的中断。普通 `prepare/start/restart` 在副作用之前拒绝执行；
`status` 明确报告待恢复。终态记录提交前的自动回滚失败会保留 pending，允许修复原因后重试恢复；
若回滚记录已提交而后续核验失败，则报告结果无法确认，要求检查事务记录和 TLS 状态，不误称仍有 pending。
恢复会接受旧/新文件混合状态，但拒绝覆盖两者以外的文件或变化的 CA/客户端信任。
旧证书必须仍有效；未使用的新候选已过期不会阻碍恢复有效旧证书。恢复过程中不复制或重置数据库、供应商配置或管理密钥。

以下为维护窗口命令说明，**本轮未在正式部署执行**。两条命令都会中断会话，必须在用户结束使用后安排；缺少显式参数会被拒绝：

```powershell
C:/Python314/python.exe -B scripts/windows/manage_account_certificates.py activate --candidate-id <已检查的候选ID> --allow-service-restart
C:/Python314/python.exe -B scripts/windows/manage_account_certificates.py recover --allow-service-restart
```

无 pending 时 `recover` 返回无需恢复，不会重启服务。事务保存在
`output/services/account/certificate-transactions/<事务ID>`，继承当前 Windows 用户和 SYSTEM 的私有目录权限；
终态为 `committed.json` 或 `rolled-back.json`。旧/新私钥快照仍保留，不自动清理，不能上传这些目录。

19 项隔离测试全部通过，报告 `output/account-certificate-transaction-tests.json` 的 `passed=true`、`fixture_cleanup_complete=true`。
使用实际内存账户 TLS fixture、随机 Windows 控制对象和动态端口，显式 `AUTH_ENV=test`、空 CA 环境；
五个真实进程退出点覆盖发布 intent、停止完成、半对文件替换、新服务就绪和回滚半对替换，随后均能恢复旧证书并确认在线服务。
其余覆盖启动/探测失败、日志与快照损坏、外部信任变化、过期未用候选、终态记录写入及返回异常。
13 项既有证书检查、14 项操作锁检查、14 项停止回归和 3 项启动器检查也通过。一名协作者完成最终只读审查，无阻断项。

首轮事务测试的功能断言通过，但清理因 supervisor 日志句柄尚未关闭而失败；测试现已等待自己持有的进程句柄退出，最终完整运行通过。
自动审批拒绝删除首轮遗留目录 `output/services/certificate-transaction-tests-cxw02b04`，理由仅为 `blocked by policy`，该目录原样保留，不再尝试删除。
此前同样被拒绝的 `output/services/certificate-tests-66db2gm3` 也继续保留。这些遗留目录不属于最终通过轮次的清理结果。

正式只读检查：服务就绪、TLS healthy、无 pending，实际客户端 SHA256 保持
`5577a41e560fcafbead23d012fac6057ba41b3bdf338aac0df7740587b00a610`。
当前验收仅覆盖同用户/同机、旧证书尚有效的计划续期和进程退出恢复。
正式维护窗口激活、旧证书已过期时的应急修复、CA 轮换、无人值守续期、事务保留策略、真实断电/磁盘损坏恢复和完整灾备仍未验收。

## 部署操作互斥与启动失败回收（2026-10-20）

准备、启动、停止、完整重启及投递配置发布共用一个 Windows 操作互斥量，避免同时写配置或交错启动/停止。
同一线程可嵌套调用；等待锁有期限，`stop` 的原有总期限同时包含等待操作锁的时间。
投递密钥输入发生在锁外，保存时才获取锁、重读最新配置并合并当前渠道，保留期间另一渠道的更新。
只读 `status` 不等待操作锁。

启动方先把后台 worker 纳入临时 Windows Job，再通过 stdin 发出启动许可；worker 确认自己确实属于该 Job 后才可创建账户服务器。
只有状态中的管理进程 PID 对应本次 worker，且服务就绪时，才解除临时 Job 的关闭即终止标志并完成交接。
启动超时或交接失败会终止该 Job 内的进程树，并确认活动进程数归零后才释放操作锁。
启动成功后，命令退出不影响服务，服务器仍由管理进程自己的 Job 持有。
直接调用 `run` 已拒绝，请使用 `start`；内部 worker 入口不是独立启动命令。

14 项操作隔离测试、14 项停止回归和 3 项启动器检查通过，一名协作者完成最终只读审查，无阻断项。
新测试覆盖真实跨进程互斥、递归与放弃锁、提示期间其他渠道更新、伪造 worker、许可发送失败、
尚未读取许可及服务器尚未加入其自身 Job 时超时、交接失败，以及启动命令退出后服务继续运行。
报告为 `output/account-operation-tests.json`、`output/account-service-control-tests.json`，可复现操作测试：

```powershell
C:/Python314/python.exe -B scripts/windows/test_account_operation_lock.py -v
```

本轮没有停止或重启正式账户服务、生成或激活正式证书、重编实际 EXE，也没有操作用户已打开的客户端。
只读正式状态为就绪，实际 EXE 哈希保持不变。运行中的管理进程仍保留先前加载的代码。
本次共享锁为后续证书激活/回滚提供互斥基础；取得放弃的互斥量不代表文件事务已恢复。
证书切换日志、崩溃恢复、切换后验证、失败回滚与 CA 轮换仍待实现和隔离验收。

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
