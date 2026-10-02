# Call and meeting AI integration

Status: configurable integration, not real-provider acceptance. The GitHub product baseline has now been located: docs/LumaLive_V1.0_AI_Native_完整产品需求文档_PRD.docx, Git blob 488c548b2d6737c985312f4fc3417e628739932e. The local copy matches this hash. Its AI voice checklist includes ASR, speaker separation interfaces, subtitles, punctuation, keywords, translation, TTS and voiceover interfaces; not all are complete.

## Run

Use Python 3.10+ and start `python LumaLive/server/ai-gateway/python/gateway.py` from the repository root. No extra packages are required. Configure these environment variables in the gateway process before starting it:

- LUMALIVE_AI_BASE_URL: provider API base including any /v1 prefix. HTTPS required except loopback mock servers.
- LUMALIVE_AI_API_KEY: provider key, kept out of source control and chat.
- LUMALIVE_AI_TRANSCRIBE_MODEL: a model supporting the multipart /audio/transcriptions endpoint.
- LUMALIVE_AI_CHAT_MODEL: a model supporting /chat/completions.

The gateway binds 127.0.0.1:19740. GET /health reports configuration status without secrets. Open the Studio or meeting window, join a call/meeting, open AI subtitles, and explicitly enable transcription after notifying participants. The native client uses this local gateway; it does not store the provider key. The transcript can be selected and copied from the AI panel. Request summary/action items after transcript appears. Closing the panel stops transcription; reopening retains the current session text. Starting a new session clears text and requires enabling again.

Audio is uploaded in per-speaker five-second WAV batches, not streaming ASR. Silence and chunk boundaries can affect quality; this requires real-provider testing. Queue overload drops segments with a visible status. Pending work is discarded on stop; already transmitted requests cannot be recalled. Results from prior sessions are discarded. Transcript is memory-only and limited to 64 KB. There is no automatic recording archive or speaker diarization model; speaker labels come from media tracks.

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
