# 本机账户服务部署与测试

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

本机证书初次生成/检查使用已安装 Git 中的 OpenSSL 命令行工具，运行时使用已校验 PostgreSQL 包中的 OpenSSL 3 DLL。服务器证书有效期一年，启动前检查至少剩余一天；自动证书轮换、无人登录的 Windows 服务安装、备份恢复演练仍未交付。

日志：`output/services/account/{service,supervisor}.log`。不要分享 `credentials.dpapi`、`providers.dpapi` 或 TLS 私钥。停止服务不会删除数据库。

## 已执行的真实验收

`scripts/windows/test_local_account_deployment.py --allow-service-restart` 对实际客户端和上述 PostgreSQL/TLS 服务执行：注册落库、登录解锁工作区与显示账户名、退出锁定、重启服务后账户保留并重新登录、通过客户端删除本次生成的测试账户。报告：`output/local-account-verification.json`，客户端 SHA256 `22b201b08c762e90de464e2046f46cc59df6590a93e0d4f89b2d5b457aa8200f`。测试结束保持服务运行。

此脚本会重启本机账户服务，不能当作不影响已有会话的日常健康检查。普通检查使用 `status`。本轮早期两次自动化失败来自测试在控件创建完成前填值，已补就绪等待；这两次生成的测试账户已清理。

独立 TLS 验收覆盖证书链/IP、不降级为明文、五秒握手与分片帧截止、登录后空闲61秒以及同时存在空闲/未完成握手连接时的停止。真实邮件、短信、公网部署、长期压力、备份恢复和物理输入法/读屏/多屏仍为独立待验收项。
