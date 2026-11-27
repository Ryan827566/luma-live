# 原生 AI 响应边界独立验收（2026-11-23）

状态：独立验收通过。冻结源码审查无阻断；收到 root 今日构建完成通知后，在自有 kill-on-close Job 中独立运行十二场景一次，12/12 通过，退出码 0，耗时 7.984 秒。输入前后哈希一致且原生二进制与 root 提供的 SHA256 相同；Job 最终已清零，无 controller 错误。

## 实际结果

| 场景 | 结果 |
| --- | --- |
| 精确 Content-Length、chunked、连接关闭定界，各覆盖五路 | 3/3 通过 |
| transcribe / summary / translate / keywords / speech 截断 | 5/5 拒绝部分响应且重试恢复 |
| 空转写截断 | 拒绝且重试恢复 |
| summary 声明超限、非法长度、超大数字 | 3/3 拒绝且重试恢复 |

全部场景原生退出码 0、stderr 为空、server 线程退出。每个失败注入目标恰好收到两次请求，证明失败后重试确实触达网络；原生断言验证部分内容未入档、未产生可取语音、成功重试清空错误。报告 `independent-cases.json` 保存逐场景路由与 stdout；`independent-run.json` 保存 Job、耗时及前后 SHA256；`independent-run.log` 保存完整运行摘要。未重跑任何场景。

## 冻结哈希

| 文件 | SHA256 |
| --- | --- |
| luma_session_ai_gateway_tests.exe | `3e836f64fcf31935503bdb108de7c93a6e1e1577dda552947cd4744f8bfbf3e2` |
| SessionAi.hpp | `43cba3916c4b7af93394d840249a78ab4e34ca857ac7b958e9572527c72dae0a` |
| SessionAiGatewayTests.cpp | `26ea8406c318e6d28ee686c9e983ddaeb7f161a6e03b3a4394746ff4dabab4d4` |
| test_native_response_boundaries.py | `c905b6438c30b12f92497faf2709cce3e888f113462df6556577d00b8e7c235f` |
| verify_client.py | `c9128b4252be23b982ec03961397b32e80353e474fbf5bb2cf1db3c3782be652` |

## 源码审查

已读取昨日 `frozen.diff`：`SessionAi.hpp` 为 HTTP Content-Length 增加逐位数字/上限校验及读取后字节数匹配；测试 C++ 增加失败后恢复路径；`verify_client.py` 增加新 harness 总回归入口（Python `-B`、360 秒 timeout、报告置于仓库 D 盘 output）。新 Python harness 已全文审查，随机 loopback 端口、每个子进程 25 秒 timeout、服务线程结束检查、失败目标必须恰好请求两次。

当前无阻断发现。长度解析先检查乘加边界，不会先溢出；保留无 Content-Length 的 chunked/连接关闭定界支持；原生测试会拒绝 RequestFailed 缺失、部分内容入档/语音泄漏、无法重试或错误残留。旧版基线 7 failed / 5 passed 已读，不重跑修前；malformed/huge 原已通过，只计保留回归。

覆盖边界：未穷举所有 framing 或文本/语音 limit-1/limit/limit+1；无真实模型、正式服务、UI/硬件验证。只有 root+独立 QA 收尾此包；不构建、不改开发源、不执行 Git。

所有新增输出在本 D 盘目录，Python `-B`，TEMP/TMP 为 `D:\workspace\luma-live\output\codex-temp`。C 盘只读，没有执行主动 C 盘写入；操作系统或 Codex 内部不可控写入未测量。
