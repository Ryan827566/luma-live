# 共享 AI 独立 QA（2026-11-21）

状态：通过。网关 UTF-8 输出边界修复已独立通过相关 HTTP/mock 回归及完整网关 12 项测试；冻结两文件 diff 已核对，无阻断发现。SessionAi 本轮无已复现产品缺陷、零修改，不继续审理论竞态。

## 已完成独立实际验证

使用 Python `-B`，30 秒受控 timeout，执行新增 `test_provider_text_must_be_encodable_as_utf8` 与相邻 `test_summary_json_and_utf8`、`test_upstream_failures_are_redacted`。3 项全部通过（0.461 秒，退出码 0），证据 `gateway-independent-20261121.json`。

新增回归使用真实随机 loopback HTTP 网关和 mock 上游；四路 summary/keywords/translate/transcribe 各验证 lone high surrogate、lone low surrogate 均返回固定 502 文案；合法 emoji 仍返回 200。12 次上游请求均由 fixture 记录。独立读取开发修前日志，确认八个无效字符子例原报 `RemoteDisconnected`；独立未恢复生产文件重跑修前。

源码确认编码发生于成功响应头发送之前，`UnicodeEncodeError` 被转换为可控 `GatewayError(502, ...)`，错误正文不含提供方内容。现有 `/speech` 二进制返回分支保持不变。

随后按 root 的完整验收要求独立运行 `python -B -m unittest -v test_gateway`：12/12 通过，退出码 0，1.742 秒，45 秒受控 timeout。证据 `gateway-full-independent-20261121.json`。全套包含并发槽、URL策略、health、keywords、multipart转写、provider timeout、请求校验、成功 UTF-8 和错误脱敏。两次执行记录的源码 SHA256 完全一致；此后不再重复测试。

独立读取 root 提供的 `../gateway-frozen.diff`：生产改动仅 5 行编码/异常处理，测试改动仅新增 21 行方法，无无关变更。上游各文本方法已检查结果类型为字符串；编码异常在 200 响应之前转为 502，既有 `reply` 接收 bytes，Content-Length 继续按实际字节计算。新增用例同时覆盖 invalid high/low surrogate、包含私有文本的异常脱敏及合法非 BMP 字符，当前无需再增同义反例。

当前实际测试源码 SHA256：

- `gateway.py`：`bbc1ec8574cdbf45ded55f11f79a19b6007607a821a2142e62808c932c9540e6`
- `test_gateway.py`：`44d56f0af0848b647be73ee84b3a83f99b194e0d9412afa73bdbf3708e4b788b`

## 已核对覆盖

- `SessionAiTests.cpp`：默认关闭不上传、WAV、说话人归属、摘要/关键词、档案、暂停、重置代际、迟到结果、失败提示脱敏、语音取消与新任务 busy 状态、取消排队语音、字幕容量上限释放 busy、自动翻译关闭/目标切换/字幕版本匹配、静音。
- `test_gateway.py`：真实随机 loopback HTTP mock provider 的 multipart/UTF-8/关键词、请求类型与长度/来源校验、配置 health 不请求提供方且脱敏、提供方状态/重定向/JSON/容量异常、URL策略、并发槽限制、timeout。
- `SessionAiGatewayTests.cpp`：依赖已有本地网关 fixture 的 WinHTTP 整合检查；本轮尚未运行，不自行启动固定端口服务。

## 边界

不改开发文件、不重编、不执行 Git。会议邀请 UI 继续保持未验收候选，未操作。原生 WinHTTP、正式服务、真实外部模型、打包/发布不属于此独立 HTTP 回归结论；root 负责原生整合验证。

所有新证据存放于本 D 盘目录；Python 使用 `-B`，TEMP/TMP 指向 `D:\workspace\luma-live\output\codex-temp`。C 盘只读。
