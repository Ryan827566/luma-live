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
