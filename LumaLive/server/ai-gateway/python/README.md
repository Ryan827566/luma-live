# Local AI gateway (Python standard library)

Requires Python 3.10 or newer; no pip packages. Start this process before enabling AI in the native client. It binds only `127.0.0.1:19740`.

Set these variables in the gateway process environment (restart after changing them):

- `LUMALIVE_AI_BASE_URL`: provider API root, including `/v1` where required, for example `https://api.openai.com/v1`.
- `LUMALIVE_AI_API_KEY`: provider key. Keep it out of source control and command history.
- `LUMALIVE_AI_TRANSCRIBE_MODEL`: an audio transcription model supported by the provider.
- `LUMALIVE_AI_CHAT_MODEL`: a model supporting Chat Completions and system messages.

From the repository root:

```powershell
python -B LumaLive/server/ai-gateway/python/gateway.py
```

`GET http://127.0.0.1:19740/health` reports configuration validity, without contacting the provider or revealing credentials. `provider_validated: false` explicitly means health is not a live provider check. All four settings are required; unavailable configuration returns HTTP 503 for AI requests.

The native client sends raw PCM WAV to `POST /transcribe` (`audio/wav`, maximum 2 MiB) and UTF-8 transcript text to `POST /summary` (`text/plain`, maximum 128 KiB). Successful responses are UTF-8 plain text. The gateway converts audio to multipart `/audio/transcriptions` (JSON response format), and transcript to `/chat/completions`. The summary requests decisions and action items without inventing owners or deadlines. Model compatibility, credentials, quotas and provider availability must be validated with the chosen provider.

Only enable the native client's AI option after obtaining participant consent: audio and transcripts are sent to the configured provider. This service stores no recordings or transcripts, emits no request logs, and gives generic provider errors. Provider retention policies still apply. There is no user authentication on this local endpoint; other local processes can call it. Browser Origin requests and non-loopback Host headers are rejected. Upstream redirects and inherited proxy configuration are disabled. HTTPS is required except for loopback test/local providers.

Limits: four concurrent handlers, eight queued connections, 10-second client socket timeout, 25-second upstream socket timeout, 1 MiB provider response bound. Socket timeouts are inactivity limits, not strict total wall-clock deadlines. This is a local development companion, not a production multi-user server. It performs chunk-based transcription, not streaming ASR or speaker diarization.

Run the isolated tests (mock upstream on loopback only, no external network):

```powershell
python -B -m unittest discover -s LumaLive/server/ai-gateway/python -p test_gateway.py -v
```

API formats: [audio transcription](https://developers.openai.com/api/reference/resources/audio/subresources/transcriptions/methods/create), [Chat Completions](https://platform.openai.com/docs/api-reference/chat/create).
