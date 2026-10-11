# Call and meeting AI integration

Status: configurable integration, not real-provider acceptance. The GitHub product baseline has now been located: docs/LumaLive_V1.0_AI_Native_完整产品需求文档_PRD.docx, Git blob 488c548b2d6737c985312f4fc3417e628739932e. The local copy matches this hash. Its AI voice checklist includes ASR, speaker separation interfaces, subtitles, punctuation, keywords, translation, TTS and voiceover interfaces; not all are complete.

## Run

Use Python 3.10+ and start `python LumaLive/server/ai-gateway/python/gateway.py` from the repository root. No extra packages are required. Configure the provider in the client AI > Service settings page, save it, and run the gateway under the same Windows user. Required fields are API base URL, API key, transcription model and chat model; speech model and voice are optional but must be supplied together. Saving does not validate a provider connection. Alternatively set these environment variables in the gateway process:

- LUMALIVE_AI_BASE_URL: provider API base including any /v1 prefix. HTTPS required except loopback mock servers.
- LUMALIVE_AI_API_KEY: provider key, kept out of source control and chat.
- LUMALIVE_AI_TRANSCRIBE_MODEL: a model supporting the multipart /audio/transcriptions endpoint.
- LUMALIVE_AI_CHAT_MODEL: a model supporting /chat/completions.

The gateway binds 127.0.0.1:19740. GET /health reports configuration status without secrets. Open the Studio or meeting window, join a call/meeting, open AI subtitles, and explicitly enable transcription after notifying participants. The native client uses this local gateway; it stores saved provider settings encrypted with current-user Windows DPAPI in %LOCALAPPDATA%/LumaLive/ai-provider.dat. No key is sent to the local HTTP gateway. The transcript can be selected and copied from the AI panel. Request summary/action items after transcript appears. Closing the panel stops transcription; reopening retains the current session text. Starting a new session clears text and requires enabling again.

Audio is uploaded in per-speaker five-second WAV batches, not streaming ASR. Silence and chunk boundaries can affect quality; this requires real-provider testing. Queue overload drops segments with a visible status. Pending work is discarded on stop; already transmitted requests cannot be recalled. Results from prior sessions are discarded. The live transcript is limited to 64 KB and stays in memory until the user explicitly chooses Save record. Saved Markdown snapshots persist under %LOCALAPPDATA%/LumaLive/records/call or meeting, including available summaries, translations and keywords. History opens saved content separately from the live transcript. There is no automatic recording archive or speaker diarization model; speaker labels come from media tracks.

## Verified locally

- Release native client and SessionAiTests: default-off, WAV request shape, attributed transcript, summary input, stop, reset/late-result isolation and visible provider errors.
- Gateway: 8 local mock HTTP tests passed: configuration/secrets, multipart transcription, UTF-8 summary JSON, request validation, timeout, upstream redaction, URL policy, concurrency bounds.
- Meeting UI regression passed at 96/144/192 DPI and with real three-member signaling roster.

No real provider was configured or called. Model quality, billing, latency, full UI interaction and original-document feature completeness are not accepted. The Python gateway currently runs separately and needs Python installed; it is not packaged with the Windows executable.

## Translation and speech follow-up

Added on-demand transcript translation (zh/en/ja/ko/es) and summary TTS requests with local playback controls. Gateway TTS configuration is optional: LUMALIVE_AI_TTS_MODEL and LUMALIVE_AI_TTS_VOICE; missing values return an explicit error. Endpoint /speech accepts text/plain and returns bounded, validated PCM WAV. AI speech is identified as generated speech. This is summary read-aloud, not cloned voices or live interpreted audio. Reference wire format: https://developers.openai.com/api/reference/resources/audio/subresources/speech/methods/create .

Native HTTP integration harness: python LumaLive/server/ai-gateway/python/test_native_client.py <absolute path to luma_session_ai_gateway_tests.exe>. It starts actual loopback gateway/mock provider servers on ephemeral ports and launches the C++ client using WinHTTP. No real provider or customer audio is used. LUMALIVE_AI_GATEWAY_PORT overrides only the loopback port for testing.

## Keyword extraction and localized states

POST /keywords accepts a UTF-8 text/plain transcript and uses the configured chat model to return at most ten transcript-grounded phrases in the source language. The native Extract keywords action uses the current attributed transcript. Its output is model-generated and requires user review; no keyword output is hardcoded in production. Transcript, summary, translation and keyword text stay scoped to the call/meeting session.

AI status and section headings use Chinese/English UI resources. Raw provider diagnostics are available through LastError for diagnostics, while the interface shows a localized request-failure message. Caption/summary/translation/keyword pending, ready, empty and queue-busy states are distinct. Speech-length failures use their own guidance and do not stop caption collection. Closing the client stops AI; switching hosted pages hides the panel and retains its session, so it does not implicitly revoke transcription consent. Explicitly stop captions to stop uploads.

The native keyword HTTP test uses a local mock provider and verifies request routing, not semantic model quality. Python gateway tests include invalid keyword content type, empty/oversized input and redacted upstream errors.

Verified 2026-10-01: Release client and native AI test targets built. Native unit tests passed, including keyword routing/reset, missing-input guidance, oversized speech without stopping captions, and queued-keyword recovery after transcript cap. Native WinHTTP -> gateway -> mock provider passed five operations (ASR, summary, translation, WAV TTS, keywords). Python gateway suite passed 10 tests. Hosted UI smoke passed localized empty-keyword guidance, three-participant membership retention, fullscreen/context separation and clean shutdown. No real AI provider or physical audio playback was used.


## Automatic translation and captions in video tiles

Enable captions, select the translation language, then enable automatic translation in the AI page. Each completed five-second ASR segment queues a translation; stopping captions also stops automatic translation. Switching the target rejects older queued/in-flight translations. The source and translated text appear on the corresponding call/meeting participant tile; meeting participant labels remain unobstructed. Only the latest utterance for that track is shown for up to ten seconds. Stop/reset, silence and hidden/disconnected meeting members do not retain visible captions. Late translations cannot attach to newer speech. The file player's native video surface does not support this overlay.

The b2f8c214 checkpoint subsequently passed Release build, automatic translation cancellation/restart unit tests and hosted three-member UI navigation. This remains segmented translation through a configured provider, not streaming recognition or a real-provider quality/latency acceptance.


## Canceling generated speech (2026-10-03)

Stop reading now stops playback, removes queued speech jobs, discards ready audio, and invalidates late responses from in-flight generation. Canceling speech releases only the busy state owned by that speech job; it cannot unlock a newer summary/keyword operation. Generation already sent to the provider cannot be recalled. Chinese/English UI status confirms stopping when speech was pending/ready. Unit tests passed for queued, in-flight and ready cancellation, late-result isolation, unrelated pending work and subsequent new speech. Native HTTP/mock-provider integration still passed all five operations. Physical speaker playback and real TTS quality remain pending.


Speech playback lifecycle follow-up: the UI receives audio and its cancellation revision atomically. A session reset, caption stop/restart or transcript limit invalidates already-consumed audio; the panel stops its playback on the next UI refresh (normally within 300 ms), including when its hosted page is hidden. Transcript-limit shutdown also discards buffered speech. This extends cancellation beyond queued requests to playback that has already started. Physical audibility remains a manual check.

Verified 2026-10-03: rebuilt actual luma_studio.exe after user closed the running instance; SessionAi revision/reset tests and three-member hosted navigation/clean shutdown passed. Physical playback cutoff is still a manual check.


## Desktop settings and explicit archives (2026-10-11)

The Session, History and Service settings subpages are embedded in the existing AI page. Switching views does not stop captions or overwrite the live transcript. Call and meeting archives are isolated, while provider settings are shared for the current Windows user. The session is not automatically archived on reset or close. Saved Markdown is ordinary local text, not DPAPI-encrypted audio/transcript storage.

The running gateway reads saved settings for each new request. Explicit environment variables take precedence per field; a complete environment configuration bypasses the desktop file. An invalid saved file causes an unconfigured health response instead of silently keeping stale credentials. Health only reports configuration availability, never real-provider validation. The Python gateway remains a separately launched process, not a packaged background service.

Verified: native settings roundtrip/corruption/URL validation, atomic archive save/list/load and context isolation, C++ DPAPI writer to Python reader, request-level config reload through HTTP, session snapshot content, masked key input, invalid-save preservation, actual hosted History/Settings navigation and line breaks. Full verifier: 13 checks passed on EXE SHA-256 d1f2da2980739dc5afa345ebcf55c48813441e2a380bb3259c90f6ddc0b47cf7. Real provider behavior, physical audio/video and final product acceptance remain pending.


## Local AI service checks (2026-10-11)

Service settings now offers an explicit asynchronous Check local service action: unavailable, configuration required, configuration ready, or incompatible response. Ready is not real-provider validation. Checks call only loopback, send no secrets/audio/transcript and do not call the provider. Editing/saving or closing invalidates old results; checks are disabled while dirty or a previous request drains. Redirects, duplicate/unknown protocol headers, invalid ports and non-200 responses cannot be reported ready. Older gateways need updating for the new GET /health protocol headers.

All 15 actual-EXE checks passed, including native lifecycle, production-gateway HTTP integration, hosted settings/status and three-member navigation/DPI regressions. Compact English 1024x700 navigation/captures passed before the final display-only newline correction; the full verifier passed afterward. Live and archived AI output now normalize display line endings. EXE SHA-256: 810e8a24d846ace18926f56ff6abf9ea6a0f1911895bdb25befaa2ae8884ecd8. Real provider, physical media/TURN and final product/module acceptance remain pending.


## Explicit archive deletion (2026-10-11)

History now supports deleting the selected saved record through an inline confirmation. Cancel is focused by default; Escape, internal tab switches and leaving the AI workspace cancel pending deletion. Hidden/old confirmations cannot delete records. Only the selected saved file is removed; the active transcript and other call/meeting records are preserved. Deletion is permanent and the confirmation says so. The backend validates context/ID and deletes through a validated regular-file handle while holding ancestor directory locks. Missing/invalid records report localized errors.

All 15 actual-EXE checks passed, including selected-file deletion, invalid/directory rejection, other-context preservation and hosted confirmation/cancellation tests. Compact English navigation passed; English and simplified Chinese 1024x700 captures in output/ui-archive-delete-20261011 were inspected. Independent scoped UI review found no blocking issue. EXE SHA-256: 11593f2a5b62c7a2eb91e3a4301367da4647d191e2d2489f816676e26c5b4a89. This does not establish final UI, real-provider or physical media acceptance.


## Packaged local AI service (2026-10-12)

Service settings now starts/stops the bundled loopback gateway without a terminal or machine-wide Python installation. Startup is asynchronous and bounded. The child runs hidden inside a kill-on-close Windows Job, survives page switches, and closes when its owning client exits. Cancellation, failed startup, early process exit and readiness timeout release owned processes. A compatible externally owned service is reported separately and never stopped by this client. Starting the service does not enable captions or upload audio; provider configuration readiness does not validate the real provider.

The packaging script pins Python 3.14.8 x64, verifies the official archive SHA-256 and writes a distribution manifest while retaining the runtime license. The isolated embedded runtime imports gateway/provider modules from its adjacent bundle. First-time developer packaging requires explicit --download or a supplied archive; end users do not need Python.

All 18 actual-EXE verifier checks passed, including native process ownership/hidden launch/cancellation/timeout/external-service tests and hosted UI start, stop, restart, page-switch retention and owner-exit cleanup against the packaged gateway. Compact English 1024x700 navigation passed; English and Chinese captures are in output/ui-ai-lifecycle-20261012. Tested EXE SHA-256: d31f8e795e138071b59864a85db4e92b998e902ff331ad67fe486ae75ddf2c11. No real provider, physical media/TURN or final module acceptance is implied.
