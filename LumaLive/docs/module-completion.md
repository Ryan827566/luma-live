# Latest checkpoint: TLS diagnostics and renewal candidates (2026-10-18)

`scripts/windows/manage_account_certificates.py status` now checks CA/leaf expiry, disk certificate chain/IP/server purpose/key pairing, adjacent client trust and the served leaf under a three-second TLS/protocol deadline. The real deployment is healthy; `output/account-certificate-status.json` records the check.

`stage` and `inspect` prepare/revalidate a private, same-CA renewal candidate with a new RSA3072 key and independent serial, without writing active certificate/trust/serial files or restarting services. Thirteen isolated OpenSSL/TLS tests passed, including expired old leaf, failed issuance, stale/tampered candidates, wrong key/IP/purpose, source fingerprint checks and probe timeouts; see `output/account-certificate-tests.json`. Production renewal was not staged or activated. Activation/rollback, CA rotation and unattended renewal remain open, along with the existing disaster-recovery, provider, physical UI and product capability gates. The actual EXE and its earlier acceptance remain unchanged.

# Earlier checkpoint: encrypted account backup and isolated restore (2026-10-18)

Root `Backup-LumaLive.cmd` now creates an online current-user DPAPI backup of the two account tables and their schema. Publication requires an actual restore from the encrypted archive into a separate temporary database/role and matching counts, content hashes and schema hashes from the same exported snapshot. Production registration/login sessions continue; no service lifecycle or client setting is changed.

Real local account backup/restore passed. Eleven failure-boundary tests and a PostgreSQL fixture drill with concurrent UPDATE passed; `output/account-backup-integration.json` records the snapshot isolation result. Cleanup failures identify remaining temporary resources without force-dropping databases. This is a same-user/same-cluster restore drill, not complete disaster recovery: cross-machine restore, live replacement/rollback, automated retention, crash cleanup, deployment-secret backup and certificate renewal remain open. The actual client binary and its prior acceptance are unchanged. See local-account-deployment.md for commands and boundaries.

# Earlier checkpoint: one-click local startup and corrected SMS flow (2026-10-18)

Root `Start-LumaLive.cmd` now starts/checks this project's PostgreSQL/TLS account deployment and opens the correct production client with isolated local connection settings. `启动说明.md` explains registration and email/password login. The latest actual EXE passed all27 unified checks/675 layout states, plus real PostgreSQL/TLS registration and email/password login into the workspace, logout and client restart/re-login. Three launcher checks cover inherited-environment isolation and startup failure reporting.

SMS entrance clutter is removed, number edits and failed resends preserve the correct challenge state, and progressive SMS MFA no longer invalidates the SMS code before the user can supply the second factor. Real email/SMS delivery is still disabled pending provider configuration; it is not needed for ordinary registration/email-password login. See login-acceptance.md for the exact binary hash and reports. Backup/recovery, certificate renewal and physical UI acceptance remain next deployment work; video/meeting/global-AI completion and broadcasting priority are unchanged.

# Earlier checkpoint: real local account path passed (2026-10-17)

Production TLS account service is now running at127.0.0.1:9100 against the real PostgreSQL instance15432/luma_auth. The actual GUI passed registration, persisted verifier/salt, login/workspace identity, logout, service restart/re-login and deletion of the generated test account. See local-account-deployment.md and output/local-account-verification.json. This supersedes earlier statements that no backend was running. Session tokens remain in memory; restart requires sign-in.

Windows10 caption color rejection is handled by themed client chrome, with actual screenshots and move/resize/minimize/maximize/restore tests. Delivery is explicitly disabled until configured; HTTPS webhook integration exists but real delivery is not accepted. The complete account module is not declared commercially finished: supplier acceptance, backup/restore, unattended system-service installation, certificate renewal, physical IME/Narrator/multimonitor and pressure testing remain. Other video/meeting/global-AI work remains as tracked below; do not begin broadcasting based on this account checkpoint.

# Latest checkpoint: focused entrance verified (2026-10-17)

320-DIP forms, inside-field eye, exact page cleanup and accurate registration errors passed25 actual-EXE checks /648 native layout states. See login-acceptance.md for hash and screenshots. DWM caption colors are unsupported in the current test environment; same-color titlebar remains open. PostgreSQL is now running with application-role readiness and repeat-start checks passed. TLS/provider deployment is in-progress unverified code, not covered by the UI checkpoint.

# Latest priority: real local account deployment (2026-10-16)

User requires production-oriented implementation and authorized deployment on this Windows PC; real email/SMS providers are not opened yet, so implement configurable integration without pretending delivery works. Minimal entrance checkpoint passed25 actual-EXE checks and405 synthetic-DPI layout cases; see latest login-acceptance.md. Local server was rebuilt, but no running PostgreSQL/auth backend or saved connection existed. Native PostgreSQL provisioning is underway; TLS, provider configuration and actual persistent registration/login are next gates. Preserve every user change; directly main only.

# Current priority: authenticated Codex-style workspace (2026-10-14)

The user requests Codex-style login and post-login workspace layouts. The latest instruction adds an authentication gate: only sign-in/registration/recovery/connection are available before authentication. Registration still requires a separate successful sign-in. The sidebar then shows the current name/email; sign-out or lost session returns to the entrance and stops media and this client's owned AI gateway. This supersedes the earlier no-gate scope. See [codex-style-ui-plan.md](codex-style-ui-plan.md) for estimates and [account-flow-and-storage.md](account-flow-and-storage.md) for the account server, database, tokens and deployment boundaries.

Latest call styling verification: all 24 unified checks passed, client SHA256 `25497616faeaeba6e8104cba20ab982ac5ded181b5441b86ebfd342c92680b6e`. See login-acceptance.md for actual coverage and limitations.

Main-only development continues. The neutral theme, icon navigation and centered signed-out forms were published in `9954f15b`. The account settings layout is also refined and locally verified. Call page visual hierarchy is refined, while video/control proportions, meeting/AI layout refinement and final visual acceptance remain pending. Quota wrap-ups must include a work/test/remaining/progress/time table. Detailed dated verification below is historical; the newest acceptance entry identifies the current binary and workflow coverage.

---

# Current checkpoint: main and hosted account page (2026-10-13)

Both `codex/video-meetings` and `codex/auth-registration-login` are merged into main (merge commits 26b616ba and 9c447563). Local development is now on main, without creating another branch. The former stale local HEAD/index was reconciled after content comparison and backup; preserve the independent legacy SVG difference.

The client now includes a hosted Account page in the left navigation. Registration/login/recovery, profile, security and devices use the account service asynchronously. Authentication expiry, revocation, reset across clients and stalled requests have focused regression fixes. See [login-acceptance.md](login-acceptance.md) for scope, testing and remaining production-provider/transport gates. This does not imply the existing video/AI real-provider or physical-device gates are complete.

Account detail scrollbars now follow content size. Successful connection endpoints persist across client restarts, with invalid-file recovery and isolated native/UI tests; passwords and session tokens are not saved.

Password challenges are now fully validated before derivation, preventing unbounded or malformed server parameters from stalling the account worker. All 23 actual-EXE checks, including 77 malformed loopback-server cases, passed.

Account transport now has fixed connect/frame deadlines that are not renewed by partial traffic. Local UI verification now completes email/phone verification, SMS login, password change, cross-client reset and account deletion using isolated delivery adapters. All 23 actual-EXE checks passed; these adapters never contact external delivery services.

The detailed earlier snapshots below are chronological history; their earlier branch/UI instructions are superseded by the user's main-only development request and the latest checkpoint.

---

# Video/call/meeting completion ledger

## Current snapshot (2026-10-12)

User priority is product UI first, then remaining call/meeting/shared-AI capabilities, broadcasting last. Use product-ui-acceptance.md for the newest UI evidence; dated entries below are historical and must not be interpreted as current pending-work lists.

Implemented and locally verified: single main window, far-left navigation, isolated call/meeting AI contexts, new nine-size icon, dark AI surface/native combo and edit borders, themed native volume slider and AI transcript/history/participant-list scrollbars, inline meeting confirmations, native navigation order, cancellation focus restoration, 1024x700 compact layouts reduced-motion-aware button hover, and stable AI transcript reading/following across incoming updates and hidden workspaces. Simplified Chinese, traditional Chinese conversion and English are supported; other display languages fall back to English. This is not final product UI acceptance.

AI workspace checkpoint: inline Session, History and Service settings pages now work inside the main window. Provider configuration is saved using current-user Windows DPAPI encryption and reloaded by the gateway on each request. Explicit Save record persists a Markdown snapshot in separate call/meeting folders; History lists and opens those records without replacing the live session. History supports cancellable background content/date search within the latest 1,000 records, with explicit partial/unreadable-result states. It also exports Markdown in a background worker into a user-visible destination without replacing existing files. Explicit deletion now uses inline confirmation with Escape/navigation cancellation, preserving other records and the active session. These paths passed native, Python interoperability and actual-client UI tests. The packaged local gateway can now be started and stopped from Service settings; no real provider was contacted.

Local service checks now distinguish missing service/configuration from readiness, invalidate stale results and do not imply provider validation. Live and archived AI text preserve line breaks. All 19 actual-EXE checks passed, including packaged-runtime imports, native process ownership and actual-client start/stop/exit. See latest UI/AI acceptance entries.

Still open in UI: dropdown-popup overflow scrollbar polish, complete localization and Narrator/IME workflows, physical multi-monitor DPI changes, smaller-screen handling and final visual acceptance. Current actual EXE verification and hashes are in product-ui-acceptance.md and output/client-verification.json.

Still open in capabilities: account/contact call entry, streaming ASR, model speaker diarization, continuous low-latency interpretation, full voiceover, global AI orchestration/adapters beyond calls/meetings, real slow-network export timing. Existing five-second ASR/translation, summaries/actions, keywords and TTS are configurable integrations; real-provider accuracy/latency/cost and physical playback are not accepted.

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
