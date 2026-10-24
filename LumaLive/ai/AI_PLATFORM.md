# LumaLive AI Platform

## Architecture
Feature -> Gateway -> Orchestrator -> Model Router -> Provider -> Model/API.

Automation runs alongside the request path:
User goal -> Planner/Assistant -> Workflow Definition -> Trigger -> Workflow Engine -> Tool Registry -> LumaLive Tool.

The platform is provider-neutral. Feature and automation code never owns provider-specific HTTP credentials.

## Implemented foundation
- Common request/response/capability contracts.
- Provider registry with priority and optional provider selection.
- Deterministic provider for offline development and CI.
- Model router.
- Orchestrator.
- In-memory AI task queue with immutable task snapshots.
- Single AI gateway entry point.
- Unified feature layer for Chat, Director, Host, Editor, Moderation, Subtitles, Vision, Studio Copilot and Meeting Assistant.
- Event trigger matching.
- Workflow validation and execution diagnostics.
- OpenAI-compatible and Anthropic provider adapters with injectable HTTP transport and environment-based API-key loading.
- Standalone CMake/CTest validation spanning core, providers, router, orchestrator, gateway, features, task, and automation.

## Production provider plan
The deterministic provider is not a production model. Provider adapters implement the shared provider contract:
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

## Feature and automation contracts
Each feature receives a FeatureRequest and is routed through the same gateway. Automation tools receive ToolCall arguments and return an execution output string. Workflow execution records completed steps, step outputs, and the failed step ID.

## Runtime data
Meeting AI will later consume room events, WebRTC audio/video, transcript segments, participant identity, meeting metadata, files and historical knowledge through explicit context adapters.

## Validation
Run from `LumaLive/ai/ci`:
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
