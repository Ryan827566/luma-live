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


2026-10-03 verified UI checkpoint:
- AI page now shares dark surfaces, type, spacing, hover/focus and responsive action grouping with the workspace. Its subtitle identifies the call or meeting context; records remain isolated. Native transcript selection/scroll is retained during background updates.
- Added original LumaLive light-beam/play/connected-media mark as vector master, PNG and 16/24/32/48/64/256 ICO; resource 101 is linked into the actual EXE. The generator is reproducible with Pillow.
- Display-language helper supports simplified Chinese, traditional Chinese conversion and English, with English fallback for other languages. Full localization coverage and culturally reviewed traditional wording are still pending.
- End-meeting confirmation is inline. Native button-click regression also exercises Escape cancellation without ending the session, then confirms end. Fixed IsDialogMessage swallowing Escape by requesting that key in the child subclass.
- Unified verifier PASSED: executable build, locale helper, SessionAi state, caption rendering, gateway mock tests, native HTTP/mock-provider integration, three-participant hosted navigation, standalone 96/144/192 DPI renders and meeting control regression. The Python DPI runner removes the PowerShell launch dependency; system execution policy is unchanged.
- Actual hosted call/meeting/AI screenshots inspected in output/ui-visual-review, with English captures under en-US; diagnostic meeting renders in output/next/Release/ui-checks. Diagnostic DPI rendering is not physical multi-monitor/DPI acceptance. Current EXE SHA-256: 595cbbfecd2e29cb7c09e9c7900e2f0bfc00d068d97b5f19a28256d3665c2553.
- Still pending: complete visual polishing (including native dropdown/scrollbar styling), transitions/reduced-motion, full keyboard/accessibility and multi-monitor workflows, complete locale coverage, Figma structural fidelity, real AI provider and physical multi-device media acceptance. This is a verified development checkpoint, not final product/module acceptance; do not merge main on this basis.


2026-10-03 keyboard checkpoint: shared owner-drawn buttons explicitly handle Enter through dialog navigation, ignore held-key autorepeat, and leave disabled controls inactive. AI Tab and reverse-Tab order now follows the visual action order at both responsive column counts. Hosted regression verifies forward/reverse native tab traversal and queued Enter activation of keyword guidance, plus existing confirmation Escape and three-member flows. Full verify_client.py passed on the rebuilt executable; SHA-256: 02b6909fd6ca0914dbbc87ac02d05d16e0e54a80baa2b090159cbc2d67f3bfbd. This does not complete screen-reader or full physical keyboard/multi-monitor acceptance.


2026-10-04 native-control checkpoint:
- Call, meeting and AI dropdown fields share dark fill, border, chevron, focus and disabled styling while preserving native popup/selection/type-ahead behavior. Closed height is consistently 34 DIP, including the meeting DPI diagnostic override; independent review identified and corrected the former 30/34 mismatch.
- Shared button hover uses a finite transition and reads Windows client-area animation preference; hidden/disabled/destroyed controls stop timers. This implements reduced-motion behavior, but interactive OS-setting and physical multi-monitor acceptance remain pending.
- Named call/meeting inputs, device lists, volume, AI translation language and transcript through native accessibility annotations; annotations are cleared at window destruction. Programmatic native-name tests verify initial naming, renaming without changing input values, and querying the actual hosted AI field from a separate process. Full Narrator workflows remain pending.
- Short AI records hide an unnecessary vertical scrollbar. Overflow still uses the native scrollbar; wheel scrolling and return to the short-record state are covered in hosted regression. Overflow scrollbar skin remains native and is not claimed fully dark.
- Actual EXE rebuilt; full verify_client.py passed (9 checks), including accessibility, language, AI/mock integration, three-member navigation and DPI diagnostics. English and traditional Chinese hosted navigation also passed. One earlier gateway health request aborted with WinError 10053; two subsequent complete runs passed without changing gateway code. Do not characterize that intermittent failure as a fixed gateway bug.
- Screenshots inspected in output/ui-controls-20261004; source for accessible annotations: https://learn.microsoft.com/en-us/windows/win32/api/oleacc/nn-oleacc-iaccpropservices . Current tested EXE SHA-256: b50efb03d864a56af605c904bae4e8477edea2e6da8ebdbd889b4ca95ea3e0ee.
- Remaining product acceptance includes full hosted small-screen/high-DPI layouts, physical multi-monitor changes, complete screen-reader flows/localization, and real-provider/physical-media checks. Development branch checkpoint only.
