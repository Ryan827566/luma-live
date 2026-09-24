# Video call acceptance checkpoint — 2026-09-24

Status: partial implementation; not approved as a complete module. Do not begin live broadcasting until video-call acceptance and submission are complete.

Verified this round:
- Release build and full PreviewCallTests passed after cancellable server receive fix: consent, targeting, rejection, cancel, busy, decoded bidirectional synthetic video/audio, hangup, leave/rejoin, duplicate identity, spoof rejection, timeout and server disconnect.
- SignalingShutdownTests passed: idle sockets, partial wire header/body, bounded shutdown and server restart (three rounds).
- Previous Media Foundation playback test passed with both audio and video and advancing playback position.

Saved but still awaiting integrated build and tests:
- Native GetStats network statistics (RTT, cumulative packet loss, jitter), ICE restart API, expanded loopback statistics checks. Initial compile found an explicit scoped_refptr constructor error; corrected, not rebuilt yet.
- PreviewCall reconnect negotiation, media-source state notification, one-second statistics polling and expanded restart tests.
- UI reconnect, network indicators, remote stopped-video state, capture-failure feedback, compact layout adjustment.
- Run the expanded PreviewCallTests with a sufficient timeout (its new restart scenarios may need more than the existing CTest 35 seconds).

Environment-dependent acceptance:
- Actual display capture failed because this execution session cannot access the desktop. The screen test marks unavailable display as SKIP (77); this is not a pass. Run it from the interactive Windows desktop.
- Real camera/microphone device controls, physical speaker audibility, and screen-share stop/resume still need interactive verification.
- Two-machine, NAT/TURN and changed-network-path recovery remain unverified. User currently has no second machine and authorized completing locally verifiable work first.
- Identity selection currently uses registered room participant IDs; account/contact integration is not implemented.

Next run: preserve local changes, check GitHub main, build the saved work, run call/media/shutdown tests, inspect actual UI, address remaining failures, update this evidence. Do not claim a module pass based only on synthetic media.

Budget rule: at approximately 20% observable remaining usage, stop development, checkpoint/publish, then stop the turn. Exact per-turn token balance is unavailable; account usage is the observable proxy.

## 2026-09-25 verification update

Release builds passed for the Studio client, signaling server, call tests, shutdown tests and screen test. Native WebRTC loopback also built and passed: 16 decoded video frames, 50 audible PCM blocks, valid real network statistics.

Expanded call tests passed with independent 440 Hz / 733 Hz audio sources: 31/31 decoded video frames, 98/94 non-silent audio blocks at initial connection; caller-initiated and callee-initiated ICE restart both retained identity/duration and continued bidirectional decoded media. Both controllers delivered real inbound network statistics. Source off/resume notifications, consent, third-party isolation, rejection, busy, cancel, hangup, rejoin, duplicate/spoof/stale invitation handling, timeout and server disconnect passed. Identical test tones had previously been suppressed asymmetrically; independent source tones resolved that test-input issue without lowering assertions.

Shutdown regression passed again. Actual display capture remains SKIPPED (77) because the sandbox desktop is unavailable, not a pass. Layout-only render passed and shows the formerly cropped bottom source controls within the client area; it does not verify live video presentation or audible speaker output.

UI reconnect remains enabled for an established call that has moved into recovery. Full module acceptance remains pending physical-device/manual UI checks and contact/user integration noted above. No broadcast development was started.

Local runnable preview is prepared in outputs/LumaLive-call-preview (outside the source repository). Launch signaling server, then two Studio instances; join the same room with distinct participant IDs; select a participant and call, then accept in the other window. Use headphones for two clients on one computer. The automated call test uses synthetic sources, while actual devices require the interactive desktop.

Device interruption follow-up: completed capture workers are joined before a restart, and Media Foundation Flush no longer runs under the reader-slot mutex. UI detects unexpected camera/microphone termination, clears stale local video and remote source state, resets controls and shows an error. Release build and full call regression passed after this change. Actual device unplug/replug is still pending interactive hardware acceptance.
