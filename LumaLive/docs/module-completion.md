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

AI scope clarification: AI is a cross-module platform capability, not owned by video. The global assistant entry must serve call, meeting, live and account/login contexts through explicit adapters with separate permissions and data. SessionAi currently implements the call/meeting adapter only; global orchestration and other adapters are not complete. Login must not depend on AI availability or send passwords/tokens to a model.

Sidebar checkpoint: module entry controls moved to the left. Call AI panel can be embedded as a child page, retaining its session when hidden. Meeting is still a separate window; full single-window delivery, localization and product visual design remain pending.

2026-10-01 single-window checkpoint: production entry now hosts call, meeting and context-specific AI pages in the same top-level window. Meeting objects and timers survive tab switches; embedded children do not post process quit. Session exclusivity prevents meeting/call device competition, and joining a meeting releases inactive call preview capture. A sidebar label exposes incoming calls while another page is shown. Standalone --meeting remains for diagnostics. Native navigation test passed with three actual local signaling participants, retained membership, call-join blocking, meeting end, fullscreen restoration and clean shutdown. This is functional shell acceptance, not final product visual acceptance; system-language resources, brand icon, global AI orchestration and physical-device tests remain pending. The two AI instances are currently call/meeting adapters, not the completed cross-module AI platform.

Final checkpoint verification: rebuilt with CMake runtime output configuration (not MSBuild OutDir override, which can link stale static libraries). Navigation/three-member smoke test and standalone 96/144/192 DPI render plus meeting command regression passed on the rebuilt executable at output/next/Release/luma_studio.exe. Independent review covered lifecycle, device ownership and hidden-page incoming calls; fixes were applied.

Localization first tranche: sidebar and AI panel chrome now choose Chinese/English from GetUserDefaultUILanguage, and translation-language names are consistently localized. This does not complete app localization: call/meeting content and SessionAi/provider status strings still need resource migration, other display languages currently fall back to English, and traditional Chinese resources remain pending.

Localization checkpoint verification: Release build and native navigation/three-member flow regression passed, including checking the caption action against the current Windows display language. Other language environments, full string coverage and visual localization acceptance remain pending.
