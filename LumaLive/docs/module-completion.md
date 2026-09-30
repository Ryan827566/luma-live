# Video/call/meeting completion ledger

Product baseline: repository docs/LumaLive_V1.0_AI_Native_完整产品需求文档_PRD.docx (blob 488c548b2d6737c985312f4fc3417e628739932e). This is the full platform PRD, not solely a meeting checklist. User requires call/meeting first, broadcasting later.

## Verified local functionality

One-to-one call signaling/consent, synthetic bidirectional media, caller/callee restart, source controls and cleanup have prior passing evidence. Meeting create/join/leave/end/remove, device controls, layouts, pin/fullscreen, media on/off, source change, same-ID rejoin and per-peer manual reconnect have passing local evidence. Current test additionally injects a native failed event, actually closes the peer transport and verifies automatic rebuild and resumed bidirectional decoded audio/video from either negotiation role while the third peer continues. This is not a physical network-interface outage test. Retry attempts are bounded to three, backoff is applied, and stable connection resets the retry count.

AI: opt-in per-track five-second WAV transcription, transcript, summary/action items, on-demand translation and configurable TTS/read-aloud integration. Native HTTP gateway integration uses a mock upstream; real model and physical playback acceptance remain pending. ASR punctuation depends on the selected provider. Model-based diarization, streaming ASR, continuous live translation, dedicated keyword extraction and full voiceover workflow are not complete.

## Remaining delivery gates

- Real-provider AI model quality, latency and cost checks after provider configuration.
- Physical audio/video/screen-share interruption and two-computer/TURN acceptance (user has no second computer).
- Full keyboard/device interactions and product-level visual acceptance.
- Remaining PRD AI voice features listed above; do not substitute mock output for implementation.

## Latest UI requirements

After functionality, replace preview windows with one native main window and tabs for video calls, meetings and AI assistant. Do not open meeting and AI features in separate windows. Apply Taste Skill and the existing Figma language; current layout is a functional preview, not final product UI. All UI strings must use language resources selected from the Windows display language, including error/AI status strings; do not mix Chinese and English. These requirements are recorded, not yet implemented. Taste Skill installation was attempted using skill-installer but failed writing its temporary repo.zip even after explicit filesystem permission; do not claim it installed.

Navigation placement: all module selectors belong in the far-left sidebar of the single main window, not across the top. Provide a distinctive LumaLive application/taskbar icon combining live video, audio and AI identity. Both requirements are pending implementation.

2026-09-30 checkpoint: Release build succeeded in output/next/Release. Native SessionAi unit suite passed. Native WinHTTP-to-gateway integration passed ASR, summary, translation and TTS WAV through four actual HTTP requests to a mock provider. This does not verify real-provider quality or physical speaker playback.
