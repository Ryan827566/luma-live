# Video/call/meeting completion ledger

## Current snapshot (2026-10-10)

User priority is product UI first, then remaining call/meeting/shared-AI capabilities, broadcasting last. Use product-ui-acceptance.md for the newest UI evidence; dated entries below are historical and must not be interpreted as current pending-work lists.

Implemented and locally verified: single main window, far-left navigation, isolated call/meeting AI contexts, new nine-size icon, dark AI surface/native combo and edit borders, inline meeting confirmations, native navigation order, cancellation focus restoration, 1024x700 compact layouts and reduced-motion-aware button hover. Simplified Chinese, traditional Chinese conversion and English are supported; other display languages fall back to English. This is not final product UI acceptance.

Still open in UI: native overflow scrollbar/slider polish, complete localization and Narrator/IME workflows, physical multi-monitor DPI changes, smaller-screen handling and final visual acceptance. Current actual EXE verification and hashes are in product-ui-acceptance.md and output/client-verification.json.

Still open in capabilities: account/contact call entry, streaming ASR, model speaker diarization, continuous low-latency interpretation, full voiceover, client-facing provider configuration, durable transcript archive and global AI orchestration/adapters beyond calls/meetings. Existing five-second ASR/translation, summaries/actions, keywords and TTS are configurable integrations; real-provider accuracy/latency/cost and physical playback are not accepted.

Physical device interruption, screen-sharing recovery, audible speaker switching, two-PC media and TURN remain separate acceptance gates. The user confirmed local camera preview works. Do not repeat the historical camera access-denied investigation. Do not merge the entire module to main or begin broadcasting based solely on local mock/synthetic tests.

## Baseline and historical evidence

Product baseline: repository docs/LumaLive_V1.0_AI_Native_完整产品需求文档_PRD.docx (blob 488c548b2d6737c985312f4fc3417e628739932e). This is the full platform PRD, not solely a meeting checklist. User requires call/meeting first, broadcasting later.

## Verified local functionality

One-to-one call signaling/consent, synthetic bidirectional media, caller/callee restart, source controls and cleanup have prior passing evidence. Meeting create/join/leave/end/remove, device controls, layouts, pin/fullscreen, media on/off, source change, same-ID rejoin and per-peer manual reconnect have passing local evidence. Current test additionally injects a native failed event, actually closes the peer transport and verifies automatic rebuild and resumed bidirectional decoded audio/video from either negotiation role while the third peer continues. This is not a physical network-interface outage test. Retry attempts are bounded to three, backoff is applied, and stable connection resets the retry count.

AI: opt-in per-track five-second WAV transcription, transcript, summary/action items, on-demand and opt-in automatic segmented translation, transcript-grounded keyword extraction and configurable TTS/read-aloud integration. Native HTTP gateway integration uses a mock upstream; real model and physical playback acceptance remain pending. ASR punctuation depends on the selected provider. Model-based diarization, streaming ASR, low-latency continuous interpretation and full voiceover workflow are not complete.

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

2026-10-01 AI follow-up: added /keywords through the configured chat provider and a native extraction action, with bounded input and attributed transcript. SessionAi now exposes typed statuses, localized Chinese/English messages/section headings and a separate diagnostic error accessor; provider errors no longer inject English/raw diagnostic strings into UI status. Empty transcript, missing summary and busy actions give guidance. Transcript-cap queue clearing releases pending text jobs, and TTS has a separate 4000-character limit message. Other UI languages and full call/meeting localization remain pending.

Verified 2026-10-01: Release client and native AI test targets built. Native unit tests passed, including keyword routing/reset, missing-input guidance, oversized speech without stopping captions, and queued-keyword recovery after transcript cap. Native WinHTTP -> gateway -> mock provider passed five operations (ASR, summary, translation, WAV TTS, keywords). Python gateway suite passed 10 tests. Hosted UI smoke passed localized empty-keyword guidance, three-participant membership retention, fullscreen/context separation and clean shutdown. No real AI provider or physical audio playback was used.


2026-10-02 caption integration: automatic translation checkpoint b2f8c214 was subsequently verified with the Release build, cancellation/restart unit tests and three-member hosted navigation regression. Call and meeting video tiles now render per-track original captions and automatic translations, bounded to two lines per language. Captions expire after ten seconds, clear on stop/reset or silence, and translated results require matching session, translation option and utterance serial. Empty successful ASR responses produce no transcript row. These are five-second chunks, not streaming ASR or model-based speaker diarization. Active native file playback uses its own video surface and has no caption overlay. Real-provider and physical-media acceptance remain pending.


2026-10-03 executable verification correction: earlier caption work built luma_studio_preview (a static library) while some UI checks still launched an older executable. Those checks did not establish the new code's UI behavior. luma_studio and luma_signaling_server were subsequently linked on 2026-10-02 at 19:16 local time. On 2026-10-03 the updated EXE passed hosted three-member navigation/session exclusivity/clean shutdown and standalone 96/144/192 DPI plus meeting command regressions. Future checks use scripts/windows/verify_client.py, which explicitly builds the executable and records its hash; the component tests alone are insufficient. Manual testing instructions are in client-testing.md.


2026-10-03 speech cancellation checkpoint: actual luma_studio.exe rebuilt; SessionAi cancellation, caption GDI, gateway (10 tests), native HTTP (5 operations) and three-member hosted navigation passed. Unified verifier correctly reports failed overall: its final Windows PowerShell -File DPI invocation was blocked by the system script execution policy before running. Do not label this unified run passed. Earlier separate DPI/meeting-control checks in this turn passed on the relinked caption EXE, before the speech cancellation change. Report and per-check logs are under output/client-verification.json; current executable SHA-256 is fb221bcf1d686628936720f9579ed958ad2e2fa41f0e13fedda85d4e2e869e4d. No execution policy was changed.


2026-10-03 verified UI checkpoint:
- AI page now shares dark surfaces, type, spacing, hover/focus and responsive action grouping with the workspace. Its subtitle identifies the call or meeting context; records remain isolated. Native transcript selection/scroll is retained during background updates.
- Added original LumaLive light-beam/play/connected-media mark as vector master, PNG and 16/24/32/48/64/256 ICO; resource 101 is linked into the actual EXE. The generator is reproducible with Pillow.
- Display-language helper supports simplified Chinese, traditional Chinese conversion and English, with English fallback for other languages. Full localization coverage and culturally reviewed traditional wording are still pending.
- End-meeting confirmation is inline. Native button-click regression also exercises Escape cancellation without ending the session, then confirms end. Fixed IsDialogMessage swallowing Escape by requesting that key in the child subclass.
- Unified verifier PASSED: executable build, locale helper, SessionAi state, caption rendering, gateway mock tests, native HTTP/mock-provider integration, three-participant hosted navigation, standalone 96/144/192 DPI renders and meeting control regression. The Python DPI runner removes the PowerShell launch dependency; system execution policy is unchanged.
- Actual hosted call/meeting/AI screenshots inspected in output/ui-visual-review, with English captures under en-US; diagnostic meeting renders in output/next/Release/ui-checks. Diagnostic DPI rendering is not physical multi-monitor/DPI acceptance. Current EXE SHA-256: 595cbbfecd2e29cb7c09e9c7900e2f0bfc00d068d97b5f19a28256d3665c2553.
- Still pending: complete visual polishing (including native dropdown/scrollbar styling), transitions/reduced-motion, full keyboard/accessibility and multi-monitor workflows, complete locale coverage, Figma structural fidelity, real AI provider and physical multi-device media acceptance. This is a verified development checkpoint, not final product/module acceptance; do not merge main on this basis.
