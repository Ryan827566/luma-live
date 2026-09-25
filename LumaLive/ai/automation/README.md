# LumaLive AI Automation Foundation

This module makes the product goal executable: the AI can turn a user goal into a workflow and execute that workflow through registered LumaLive tools.

## Responsibilities

- **Trigger** describes how a workflow starts.
- **WorkflowDefinition** describes ordered business steps.
- **AiToolRegistry** exposes safe, named platform capabilities.
- **AiWorkflowEngine** executes steps and reports progress/failure.

This is intentionally provider-neutral. LLMs are responsible for planning; the workflow engine is responsible for deterministic execution.

## Next increments

1. Add an AI planner that converts natural-language goals into validated WorkflowDefinition objects.
2. Add event/schedule/condition trigger services.
3. Add approval gates for side effects such as publishing, deleting, inviting, or sending external messages.
4. Add execution persistence, cancellation, retries and idempotency.
5. Register real LumaLive tools for meeting, WebRTC, media, chat, streaming and studio.
