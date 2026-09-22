# LumaLive 第一阶段验收测试文档

版本：Phase 1 Foundation Acceptance v1.0  
分支：main  
范围：基础工程、Signaling、CommandBus、Media Pipeline、Device Capture、LivePublishSession、WebRTC 基础配置。

> 本阶段不验收 SFU、多观众直播、完整 Studio、AI、IM/社交等功能。真实双端 WebRTC 音视频联调属于下一阶段重点。

## 一、测试前准备

在 PowerShell 执行：

```powershell
cd D:\project\luma\LumaLiveMerged_RTC_Integration_FinalCandidate_v2
git checkout main
git pull origin main
git log -12 --oneline
cmake --version
git --version
```

确认 WebRTC 环境已经准备好。项目支持：
- SDK：<WEBRTC_SDK_ROOT>\include\api\peer_connection_interface.h + libwebrtc.lib
- GN：<WEBRTC_ROOT>\api\peer_connection_interface.h + <WEBRTC_OUT>\obj\webrtc.lib

## 二、P1-01 CMake 配置

进入源码根目录：

```powershell
cd D:\project\luma\LumaLiveMerged_RTC_Integration_FinalCandidate_v2\LumaLive
Remove-Item -Recurse -Force build-phase1 -ErrorAction SilentlyContinue
cmake -S . -B build-phase1 -G "Visual Studio 17 2022" -A x64
```

验收：
- 无 CMake Error
- 成功生成 VS 工程
- 无 luma-server 重复注册错误

记录：PASS / FAIL + 完整错误。

## 三、P1-02 Signaling Wire Codec

验证 SignalingMessage 编码→解码后字段完全一致，重点检查：
- type
- sequence
- room_id
- peer_id
- target_peer_id
- sdp
- candidate
- candidate_mid
- value

重点案例：
```
candidate = candidate:1
candidate_mid = 0
```

预期：`codec roundtrip OK`，candidate_mid 不丢失。

## 四、P1-03 TCP Signaling

启动服务器（路径按 VS 实际输出调整）：

```powershell
.\apps\luma-server\Debug\luma_server.exe 19000
```

预期：
```
LumaLive signaling server listening on 19000
Press Enter to stop.
```

建立两个客户端：
- A：room=test-room，peer=peer-a
- B：room=test-room，peer=peer-b

逐项验收：
1. A Connect：PASS / FAIL
2. A JoinRoom：PASS / FAIL
3. B Connect：PASS / FAIL
4. B JoinRoom：PASS / FAIL
5. A 收到 B PeerJoined：PASS / FAIL
6. B 得知 A 已存在：PASS / FAIL
7. A→B Offer 转发：PASS / FAIL
8. A→B Answer 转发：PASS / FAIL
9. A→B ICE 转发：PASS / FAIL
10. ICE candidate_mid=0 保持：PASS / FAIL
11. Ping→Pong：PASS / FAIL
12. A Leave 后 B 收到 PeerLeft：PASS / FAIL
13. Server 房间 Peer 数减少：PASS / FAIL

## 五、P1-04 TCP 并发发送

背景：SDP、ICE、Ping/Pong 可能来自不同线程；未串行化 socket 写入时可能造成帧交错、decode failure。

测试：
- 连续进行 Join/Leave、Offer/Answer、ICE、Ping/Pong
- 至少 100 次消息收发

验收：无 decode failure、乱码、崩溃、死锁、房间状态错误。

记录：
```
循环次数：
错误次数：
PASS / FAIL
```

## 六、P1-05 CommandBus 重入

验证 Handler 执行期间再次 Register/Unregister/Dispatch 不死锁。

测试案例：
```
Dispatch("outer")
  -> Register("inner")
  -> Unregister("inner")
```

测试必须有超时保护。

预期：`command bus reentrancy regression OK`。

记录：PASS / FAIL；是否死锁。

## 七、P1-06 AudioFrame

验证 `AudioFrame::IsValid()`。

必测：

| 格式 | channels | 数据长度 | 预期 |
|---|---:|---:|---|
| S16 | 2 | 480 | 有效 |
| S16 | 2 | 479 | 无效 |
| S32 | 1 | 480 | 有效 |
| S32 | 1 | 479 | 无效 |
| Float32 | 2 | 480 | 有效 |
| Float32 | 2 | 479 | 无效 |
| 未知格式 | 任意 | 非空 | 无效 |

规则：data.size 必须是 bytes_per_sample × channels 的整数倍。

记录：S16 / S32 / Float32 / Unknown 各 PASS / FAIL。

## 八、P1-07 DeviceCaptureService

运行：
```
luma_device_capture_tests
```

预期：
```
DeviceCaptureServiceTests: PASS
```

验收：
- 初始 IsRunning=false
- Start=OK
- IsRunning=true
- Camera 枚举的 type/name/id 正确且非空
- Microphone 枚举的 type/name/id 正确且非空
- 第二次 Start 失败且不崩溃
- Stop=OK
- Stop 后 IsRunning=false
- 第二次 Stop 返回失败且不崩溃

## 九、P1-08 LivePublishSession

重点验证启动失败回滚。每个失败点后都必须：
- running=false
- capture 已清理
- pipeline 已清理
- rtc 已关闭
- signaling 已关闭
- 下一次 Start 不受上一次失败影响

失败点：
1. WebRTC 初始化
2. Pipeline Start
3. Capture Start
4. Camera Start
5. Microphone Start
6. Signaling Connect
7. JoinRoom
8. Signaling Send

另外验证正常 Stop：
- 不死锁
- 不崩溃
- 所有资源关闭
- running=false

## 十、P1-09 WebRTC 基础

没有原生 WebRTC SDK 时，先运行：
```
luma_client_webrtc_tests
```
预期进程返回码 0。

有 SDK 时重新配置：
```powershell
cmake -S . -B build-phase1-webrtc -G "Visual Studio 17 2022" -A x64 -DLUMALIVE_WEBRTC_SDK_ROOT="你的路径"
```

确认 CMake 能找到：
- api/peer_connection_interface.h
- libwebrtc.lib

> 本阶段不要求真实双端摄像头/麦克风通话。

## 十一、P1-10 综合稳定性

分别连续执行至少 20 次：
- Server Start → Stop
- Signaling Connect → Join → Ping/Pong → Leave → Close
- LivePublishSession Start → Stop

不得出现：
- 崩溃
- 死锁
- 永久卡死
- 无法再次启动
- socket 无法释放
- 房间残留

## 十二、第一阶段最终验收

以下全部 PASS 才算第一阶段通过：

```
P1-01 CMake                 PASS
P1-02 Signaling Codec       PASS
P1-03 TCP Signaling         PASS
P1-04 并发发送              PASS
P1-05 CommandBus            PASS
P1-06 AudioFrame            PASS
P1-07 DeviceCapture         PASS
P1-08 LivePublishSession    PASS
P1-09 WebRTC 基础           PASS
P1-10 综合稳定性            PASS
```

## 十三、反馈格式

测试完成后直接复制：

```
LumaLive 第一阶段验收

P1-01 CMake：PASS / FAIL
P1-02 Signaling Codec：PASS / FAIL
P1-03 TCP Signaling：PASS / FAIL
P1-04 并发发送：PASS / FAIL
P1-05 CommandBus：PASS / FAIL
P1-06 AudioFrame：PASS / FAIL
P1-07 DeviceCapture：PASS / FAIL
P1-08 LivePublishSession：PASS / FAIL
P1-09 WebRTC：PASS / FAIL
P1-10 综合稳定性：PASS / FAIL

总体：PASS / FAIL

错误信息：
（没有就写“无”）
```

如果 FAIL，请同时提供：测试编号、执行命令、完整终端输出、截图（如有）。不要先自行改代码，我根据失败项定位和修复。

## 十四、本阶段修复对应

- 9091824 — Fix signaling wire protocol compatibility
- 681b13c — Fix CommandBus reentrant dispatch deadlock
- e56fd52 — Validate audio frame sample alignment
- c1c89f5 / b00ac9b — Serialize TCP signaling frame sends
- fb4993f / cc5a244 / 0b852a2 / e802805 — LivePublishSession startup rollback/cleanup
- be278c4 — Fix duplicate luma-server CMake registration
- 9e73e50 / 50f16f4 / fac0857 — server signaling send/LeaveRoom/include fixes
