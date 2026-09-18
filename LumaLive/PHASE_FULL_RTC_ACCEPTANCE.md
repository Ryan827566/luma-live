# LumaLive Full RTC Milestone Acceptance

## Target path
Windows camera/microphone -> Media Capture -> VideoFrame/AudioFrame -> Media Pipeline -> WebRTC tracks -> PeerConnection -> SDP/ICE -> DTLS/SRTP -> TCP signaling -> LumaLive Room runtime.

## Implemented in source
- Windows Media Foundation camera/microphone enumeration and capture.
- NV12 video frames and PCM S16 audio frames into Media Pipeline.
- Media Pipeline frame sink for downstream RTC consumers.
- Native WebRTC adapter guarded by `LUMALIVE_HAS_WEBRTC`.
- WebRTC PeerConnectionFactory/PeerConnection, Unified Plan, local tracks, SDP offer/answer and ICE callbacks.
- Native NV12 -> I420 conversion before WebRTC video injection.
- TCP signaling transport with room membership and targeted Offer/Answer/ICE routing.
- LumaLive signaling server executable.
- LivePublishSession wiring capture -> pipeline -> WebRTC -> signaling.

## Required Windows validation before this milestone is considered accepted
1. Build WebRTC target `//:webrtc` and confirm `out\\Release\\obj\\webrtc.lib` exists.
2. Configure LumaLive with `LUMALIVE_WEBRTC_ROOT` and `LUMALIVE_WEBRTC_OUT`.
3. Build with VS2026/CMake 4.4+.
4. Run CTest.
5. Enumerate a real camera and microphone.
6. Start capture and verify non-zero video/audio frame counters.
7. Start the signaling server.
8. Run two LumaLive peers in the same room.
9. Verify Offer/Answer and ICE messages.
10. Verify PeerConnection reaches Connected/Completed.
11. Verify the receiving peer obtains remote audio/video.
12. Verify stop/leave closes capture, PeerConnection and signaling cleanly.

## Important
The Linux-side source/compile checks do not replace the Windows/WebRTC hardware acceptance above. The milestone must not be labeled fully accepted until the Windows checks are run successfully.

## Internal source-level verification performed
- C++20 compile smoke for signaling, native WebRTC adapter (fallback path), and LivePublishSession: PASS.
- Signaling wire encode/decode round-trip including ICE `candidate_mid`: PASS.
- Windows-specific WebRTC link cannot be truthfully claimed from the Linux validation environment; final native link/hardware test requires Windows and a matching raw `webrtc.lib` + headers.
