# 账户邮件与短信投递配置

生产服务拒绝 development/mock 提供商。当前尚未开通供应商，因此部署明确配置为 `disabled`：用户名/密码注册和登录可用，验证码投递返回失败，不伪造发送成功，也不通过生产协议返回验证码。

已实现 `webhook` 适配器。管理员提供 HTTPS 网关，网关再对接实际邮件或短信供应商。该适配器不是已经验收的具体供应商品牌集成。

```powershell
C:/Python314/python.exe scripts/windows/manage_account_service.py configure-provider --channel email --mode webhook --url https://your-gateway.example/deliver
C:/Python314/python.exe scripts/windows/manage_account_service.py configure-provider --channel sms --mode webhook --url https://your-gateway.example/deliver
C:/Python314/python.exe scripts/windows/manage_account_service.py restart
```

命令在本地终端隐藏输入 Bearer token，使用当前用户 DPAPI 保存，重启时在进程环境中加载。不把 token 写入命令行、Git、日志或聊天。停用使用 `--mode disabled`，不需要 URL 或 token。修改配置后需重启服务，已有会话将失效。

网关接收 POST JSON：

```json
{"version":1,"channel":"email","to":"recipient@example.com","purpose":"password_reset","code":"generated-token","expires_in_seconds":1800}
```

`channel` 为 `email` 或 `sms`；短信 `to` 为规范化国际号码。`purpose` 由账户服务提供。网关仅在已经接受投递时返回2xx；其他状态视为失败。应用不读取或记录响应正文，不进行应用层 POST 重试。网关应自行实现幂等、模板、供应商限额及投递反馈。

仅允许 HTTPS、系统证书校验、无 URL 内嵌凭据/查询参数/片段；禁用重定向、Cookie 和自动认证，防止验证码与 Bearer token 被重定向发送。DNS/连接/发送/接收各有五秒超时，不宣称整个投递总共五秒。[WinHTTP 配置依据](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags)。

直接部署服务可配置 `LUMALIVE_EMAIL_PROVIDER` / `LUMALIVE_SMS_PROVIDER`，以及对应的 `_WEBHOOK_URL` / `_WEBHOOK_TOKEN`。生产启动不会把缺失配置默认为开发投递。

已验证：停用不会接受投递、生产拒绝开发提供商、HTTPS URL/头注入配置校验和无效请求拒绝。尚未验证真实网关成功投递、供应商回执、费用及送达率；没有真实供应商凭据时不能将这些标记为完成。
