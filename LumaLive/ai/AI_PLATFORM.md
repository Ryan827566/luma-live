# LumaLive AI Platform

## Architecture
Feature -> Gateway -> Orchestrator -> Model Router -> Provider -> Model/API.

The platform is deliberately provider-neutral. Feature code never owns API keys or provider-specific HTTP logic.

## Implemented foundation
- Common request/response/capability contracts.
- Provider registry with priority and optional provider selection.
- Deterministic provider for offline development and CI.
- Model router.
- Orchestrator.
- In-memory AI task queue.
- Single AI gateway entry point.
- Unified feature layer for Chat, Director, Host, Editor, Moderation, Subtitles, Vision, Studio Copilot and Meeting Assistant.
- Standalone CMake/CTest validation for the AI foundation.

## Production provider plan
The deterministic provider is not a production model. Production adapters will implement the same provider contract:
- OpenAI-compatible LLM
- Anthropic
- DeepSeek
- Ollama/local models
- ASR
- TTS
- Vision
- Translation
- Moderation

Secrets must be supplied through environment/secret management; never commit API keys.

## Feature contracts
Each feature receives a FeatureRequest and is routed through the same gateway. This prevents duplicated authentication, provider selection, telemetry, retries and cost policy.

## Runtime data
Meeting AI will later consume room events, WebRTC audio/video, transcript segments, participant identity, meeting metadata, files and historical knowledge through explicit context adapters.

## Validation
Run from LumaLive/ai/ci:
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
