# Windows 本机账户数据库

此脚本部署 LumaLive 独立使用的 PostgreSQL 实例。它不是账户服务器，也不会启动开发验证码提供商；客户端注册、登录仍需账户服务器与该数据库正确连接。

## 范围与数据位置

| 项目 | 配置 |
| --- | --- |
| 运行时 | PostgreSQL 17.11，EDB Windows x64 二进制包修订 3 |
| 地址 | `127.0.0.1:15432`，仅本机回环地址 |
| 数据库 / 应用角色 | `luma_auth`；应用角色没有超级用户、建库、建角色或复制权限 |
| 数据目录 | `output/services/postgresql/data`，退出客户端不会删除 |
| 运行时与 libpq | `output/services/postgresql/runtime/pgsql/bin` |
| 日志 | `output/services/postgresql/postgresql.log` |
| 密钥 | `output/services/postgresql/credentials.dpapi`，Windows 当前用户 DPAPI 加密 |
| 目录权限 | 当前 Windows 用户与 SYSTEM；运行数据不提交 Git |
| 开机启动 | 此脚本尚未安装 Windows 服务或计划任务，需要主动启动 |

脚本只管理带自身身份记录的新实例；遇到未知数据目录、不同所有者、不同端口或版本会停止，不覆盖已有数据库，不修改防火墙与全局 PATH。

## 启动、检查与停止

在仓库根目录运行，Python 路径按本机安装调整：

```powershell
C:/Python314/python.exe scripts/windows/bootstrap_local_postgres.py --download --start
C:/Python314/python.exe scripts/windows/bootstrap_local_postgres.py
C:/Python314/python.exe scripts/windows/bootstrap_local_postgres.py --start
C:/Python314/python.exe scripts/windows/bootstrap_local_postgres.py --stop
```

第一次需要下载运行时；下载中断可重新执行，已有连续分片保留。完整文件必须通过固定 SHA-256 校验后才会解压或运行。再次 `--start` 不重新生成密码、不清库，并检查实例身份、监听地址、应用角色权限与实际登录查询。无参数只报告本地运行状态，不代表账户业务或外部提供商已验收。

启动账户服务器的本地部署代码可调用 `bootstrap_local_postgres.connection_environment()`，在内存中取得连接环境。不要打印返回值、保存明文连接串、把密码放入命令行或提交仓库。当前用户 DPAPI 文件不能直接当作其他 Windows 账户的可移植凭据。

## 下载来源

PostgreSQL 官方 [Windows 下载页](https://www.postgresql.org/download/windows/) 指向 EDB 的 Windows 二进制包。脚本固定使用 [17.11-3 ZIP](https://get.enterprisedb.com/postgresql/postgresql-17.11-3-windows-x64-binaries.zip)，SHA-256 来自 [EDB 发布者提供的校验记录](https://github.com/EnterpriseDB/edb-installers/issues/682)：

```text
4b8db0930c38f6ef845db919551dedda3b6b845aeb0927b3d79a6e8e9e4537cf
```

## 验收边界

数据库初始化、角色最小权限和重启后的数据保留应分别记录实际测试结果。它们不能替代账户服务器 TLS、注册与登录业务、邮件与短信提供商、备份恢复、Windows 服务自启动的验收。这里的本机数据库只监听回环地址；跨电脑访问需要另行设计部署与访问策略。


## 2026-10-17 本机验证

运行时按固定哈希下载并解压；专用实例已监听127.0.0.1:15432。已验证重复启动不重新初始化、应用角色实际查询、身份与权限检查，以及创建独立测试表→写入→正常停止→重启→读回相同内容。临时测试表已删除，实例保持运行。记录：`output/postgresql-verification.json`。修复pg_ctl启动子进程继承输出管道导致父进程等待EOF不返回的问题，使用临时文件收集输出并保留进程超时。

此验收仅证明数据库部署及持久化，账户TLS业务和正式服务启动另行验证。
