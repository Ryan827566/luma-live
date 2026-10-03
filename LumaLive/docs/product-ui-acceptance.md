# Product UI delivery contract

Status: requirements and source audit, not visual acceptance.

## Product direction

A Windows live-communication workspace for calling, meetings and live production, with a shared AI assistant. Preserve the supplied Figma dark neutral/blue brand direction. One top-level client window; module navigation stays on the far left. AI is a cross-module capability and is not owned by the video module. User priority changed on 2026-10-03: complete this UI checklist before continuing other functionality.

## Design sources

- User Figma: https://www.figma.com/design/FYKhhlXGpcTp2pNrxHUWHe/ (node 4-3464). Prior screenshot access does not establish complete structural fidelity.
- Taste repository redesign guidance: https://github.com/Leonxlnx/taste-skill/blob/main/skills/redesign-skill/SKILL.md. Read on 2026-10-01. Audit first, preserve the existing stack and behavior, implement targeted changes, test again. This source was read remotely; no successful local installation is claimed.
- Current main Taste skill excludes realtime collaborative product UI; do not transplant marketing heroes, scroll effects, or its web framework defaults into this native application. User choices, including left navigation, take precedence over generic skill suggestions. Never invent realistic-looking participant data or metrics.

## Observed issues to resolve

Inspected the generated standalone meeting idle render at output/next/Release/ui-checks/idle-96.bmp and current source. This is diagnostic standalone output, not evidence of the full hosted workspace appearance.

- Idle meeting renders a mostly empty roster and disabled in-meeting controls. Replace with a deliberate pre-join page; show roster/media controls when they become relevant.
- Server host/port and generated participant ID dominate the first-use flow. Put connection configuration in settings/advanced disclosure, and keep join/create focused on meeting and display name.
- Create and join have identical visual weight. Separate the two intents with a clear primary action and meaningful hierarchy.
- Native edit/combo outlines and AI white surfaces conflict with the dark workspace. Use common semantic surface, text, border, focus and state tokens.
- The call workspace still mixes English headings with Chinese copy. Move all owned UI text, errors, statuses and accessibility names into locale resources selected from Windows display language. Provider content and OS-reported device names are data, not UI translation keys; label them clearly without falsifying their names.
- Meeting/AI controls still need keyboard traversal, focus restoration, screen-reader names and multi-monitor checks in the hosted shell. Rendering at DPI scales alone is insufficient.
- Application icon and taskbar resources are absent. Create a distinct LumaLive mark representing live media and shared intelligence, then validate 16/24/32/48/256 pixel variants.

## Visual and interaction acceptance

1. Persistent left navigation with selected, hover, focus and incoming-call states. Switching pages retains relevant sessions and user input.
2. Single main content hierarchy per page. Use a documented type scale and spacing scale, consistent icon family and restrained blue accent. No decorative metrics or filler headings.
3. User-visible loading, empty, connecting, failed, disconnected and retry states; destructive actions require an inline confirmation instead of spawning another feature window.
4. Validate at 100%, 150% and 200% display scaling with short and long localized strings. No overlap, clipped labels, inaccessible controls or hidden destructive actions.
5. Motion must explain state changes and respect reduced-motion preferences; no continuous decoration around live video.
6. Validate complete keyboard workflows, focus indicators and accessible names, alongside actual screenshots of the hosted application.
7. Real-provider AI configuration and opt-in status remain explicit. The global assistant must identify which business context it is using and keep scopes separate.

Do not mark product UI complete based on compilation, synthetic media tests or this document. Require an actual rendered and interacted-with client plus resolved acceptance findings.


2026-10-03 UI checkpoint (not visually accepted): added shared native theme/button hover/focus tokens. Call workspace labels now use Chinese/English pairs, technical headings were simplified, connection configuration collapsed, and host-close confirmation reveals the meeting page. Meeting prejoin now focuses on room/name; idle roster/media actions are hidden. Host leave/end/remove/close use inline confirm/cancel rather than MessageBox. Navigation test updated for these controls. Figma structure API again failed with no-selection response; screenshot reviewed only, no exact-fidelity claim. Taste redesign guidance reread remotely; no local installation claim. AI visual rewrite, icon assets, broader locale coverage, full DPI/keyboard/visual acceptance are still pending. No newly built UI executable is claimed by this checkpoint.
