# Call and meeting AI integration

Status: configurable integration, not real-provider acceptance. The original user DOCX is currently unavailable at its supplied path; the full AI feature checklist still needs reconciliation.

## Run

Use Python 3.10+ and start `python LumaLive/server/ai-gateway/python/gateway.py` from the repository root. No extra packages are required. Configure these environment variables in the gateway process before starting it:

- LUMALIVE_AI_BASE_URL: provider API base including any /v1 prefix. HTTPS required except loopback mock servers.
- LUMALIVE_AI_API_KEY: provider key, kept out of source control and chat.
- LUMALIVE_AI_TRANSCRIBE_MODEL: a model supporting the multipart /audio/transcriptions endpoint.
- LUMALIVE_AI_CHAT_MODEL: a model supporting /chat/completions.

The gateway binds 127.0.0.1:19740. GET /health reports configuration status without secrets. Open the Studio or meeting window, join a call/meeting, open AI subtitles, and explicitly enable transcription after notifying participants. The native client uses this local gateway; it does not store the provider key. The transcript can be selected and copied from the AI panel. Request summary/action items after transcript appears. Closing the panel stops transcription; reopening retains the current session text. Starting a new session clears text and requires enabling again.

Audio is uploaded in per-speaker five-second WAV batches, not streaming ASR. Silence and chunk boundaries can affect quality; this requires real-provider testing. Queue overload drops segments with a visible status. Pending work is discarded on stop; already transmitted requests cannot be recalled. Results from prior sessions are discarded. Transcript is memory-only and limited to 64 KB. There is no automatic recording archive, translation, or speaker diarization model; speaker labels come from media tracks.

## Verified locally

- Release native client and SessionAiTests: default-off, WAV request shape, attributed transcript, summary input, stop, reset/late-result isolation and visible provider errors.
- Gateway: 8 local mock HTTP tests passed: configuration/secrets, multipart transcription, UTF-8 summary JSON, request validation, timeout, upstream redaction, URL policy, concurrency bounds.
- Meeting UI regression passed at 96/144/192 DPI and with real three-member signaling roster.

No real provider was configured or called. Model quality, billing, latency, full UI interaction and original-document feature completeness are not accepted. The Python gateway currently runs separately and needs Python installed; it is not packaged with the Windows executable.
