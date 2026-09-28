# Meeting development checkpoint — 2026-09-25

Status: server foundation implemented; NOT a usable video meeting yet. The user explicitly authorized developing meetings before live broadcasting. Preserve existing one-to-one call behavior.

Implemented:
- Dedicated MeetingService model with host, members, connection-bound participation and unique session epoch; call-room membership is separate.
- Explicit create/join, six-participant capacity, targeted meeting SDP/ICE routing and media-state announcements.
- Host-only removal/end, membership revocation, member leave, host disconnect ends the meeting, stale-session rejection.
- Unique transport connection IDs and existing send lifetime tokens avoid routing an old delivery to a recycled socket handle.
- Review found state/delivery ordering could race between join and end. A dedicated dispatch lock now serializes meeting state transitions and delivery; final Release build, three-client meeting signaling test and shutdown regression all passed after this fix. Sends retain the existing bounded timeout. This global lock is suitable only for the initial small-meeting implementation and should become per-meeting serialized dispatch before scale-out.

Evidence:
- Release build and three-client TCP signaling test passed for creation/join, targeted negotiation, outsider isolation, ordinary-member permission denial, media-state fanout, removal, leave, end, stale epoch and host disconnect.
- Existing one-to-one call regression passed after protocol integration, including decoded bidirectional video/audio, caller/callee ICE restart, network stats and lifecycle checks.
- No real multiparty audio/video was tested: the meeting test only exchanges signaling, not WebRTC media.

Next:
1. Extend concurrency and capacity/rejoin tests.
2. Implement a dedicated meeting client controller with one peer connection per remote member, explicit meeting consent and bounded participant/session lifetime. Do not duplicate PreviewCall invitation semantics.
3. Add timestamp-aware mixed remote audio (not concatenated per-peer PCM), per-participant video sinks and device/source fanout.
4. Integrate meeting create/join/leave, roster, grid, host controls and clear media/error states into Studio.
5. Verify three local clients receive each other's decoded media, isolation, mute/video/share, removal/end and failures; physical-device/two-machine/TURN checks remain separately pending.

Policy: host departure ends the current meeting; no host transfer yet. Participant names are session identifiers, not authenticated user accounts. Removing a member revokes current membership; it is not an account ban. No meeting UI is shipped in this checkpoint. Do not begin broadcasting until meeting scope is implemented, verified and committed.

## One-time remaining-budget extension

User authorized spending the remaining quota for this turn only; future turns retain the 20% checkpoint/stop rule. Added and passed six-member capacity, vacancy reuse, duplicate identity rejection and binding cleanup checks, plus ten TCP join/end races with completion barriers asserting no Joined event arrives after Ended. Release build and all meeting signaling tests passed. Multiparty media and UI are still not implemented.

## Meeting client session controller

Added MeetingSession with UI-thread lifecycle/state, queued transport events, registration timeout, bounded event queue, epoch filtering, participant roster and host identity, host-only local removal/end controls, source/microphone state propagation, membership-gated SDP/ICE handoff, and cleanup on leave/end/removal/failure. This is a control layer, not a multiparty media implementation or meeting UI.

Release build and three-client MeetingSessionTests passed: synchronized roster/roles, media-state updates, ordinary-member permission denial, host removal, rejoin using the same client object, leave, end, and missing-meeting failure. Next integrate per-peer WebRTC media and clocked audio mixing, then the meeting UI and three-way decoded-media acceptance.

## Three-client decoded media and meeting preview checkpoint

Implemented per-member native WebRTC mesh connections, membership-instance cleanup, bounded signaling/ICE queues, explicit local source controls and remote media-state gates. Added a common 10 ms playout mixer with bounded per-peer queues, stereo downmix, clipping and removal cleanup. WebRTC handles incoming network jitter; the mixer consumes decoded PCM on a shared output clock.

Verified in Release: MeetingSessionTests, MeetingAudioMixerTests and MeetingMediaTests passed. Three local clients exchanged synthetic video and distinct audio tones over six directed native WebRTC receive paths; every client decoded remote video, received audible PCM and produced non-silent mixed output. Host removal and meeting end stopped the relevant media. This verifies actual codecs/transports using synthetic sources, not physical cameras, microphones or speakers.

Added a separate native meeting preview window, accessible through the Studio meeting button or --meeting. It includes create/join/leave, roster/grid, camera/microphone/screen controls, mixed speaker output and host remove/end controls. Studio and signaling server Release builds passed; the application's offscreen empty-window render exited successfully. Interactive UI flows and device output are not yet accepted. The offscreen diagnostic does not fully render native edit controls and is not a product-design acceptance screenshot.

Remaining: end-to-end UI acceptance, device selection, peer failure/recovery, pin/speaker/fullscreen interaction, product-quality Figma styling and physical-device/TURN validation. Meeting work remains partial on the development branch; do not merge this checkpoint as a completed module or start broadcasting.

## Meeting preview input rendering follow-up

Fixed label alignment to actual input coordinates and added explicit dark edit/list control colors. The diagnostic render now shows the window without activation before asking native controls to paint; hidden edit controls previously omitted their contents. Render resource allocation failure now releases partial GDI allocations. Release Studio build and render both returned 0. Visual inspection confirms server, meeting ID and participant ID fields are visible with their default values. This resolves the earlier empty-window diagnostic limitation, not full meeting UI/device acceptance.

## Meeting page redesign and controls checkpoint

Active project is D:/workspace/luma-live. Camera investigation is paused at the user's request. Rebuilt the native meeting page with Chinese text, dark styled controls, a central video grid, member/device sidebar and bottom media toolbar. Added camera/microphone selection and refresh, participant status, preserved roster selection, pin/unpin, grid/focus/speaker layouts, fullscreen/Escape, clipboard meeting details, and explicit host leave/close/end/remove confirmations. Speaker selection uses decoded/local PCM peaks with a 1.5 second hold; it is implemented but automatic audible-speaker switching still needs interactive acceptance.

Fresh standalone Release build passed. Application-owned rendering passed at requested 1280x850 and 1200x720 (minimum outer height clamps to 760). Real local signaling fixture joined two participants to the UI host; the three-member render and fixture both returned 0. This uses actual roster state, not fake participants. The diagnostic fails if its requested three-member meeting does not form. Independent read-only review found no P0/P1 blocker. DPI scaling/cross-monitor behavior and complete keyboard/interactive device flows remain pending. Figma screenshot was available, but design context returned no selected layer; this is an extension of the existing client style, not certified pixel-exact Figma reproduction.

Three-client media and session tests passed with additional checks: mute/video-off reject continuing synthetic capture input, source changes propagate, decoded media resumes, removal stops delivery, same-ID rejoin works, and meeting end cleans up. These validate real native codecs with synthetic sources, not physical camera/mic/speaker/screen devices.

Run output/camera-preview/Release/Start-Meeting.cmd. UI helper luma_meeting_ui_fixture creates a local server on 19730 and two device-off participants for at most 30 seconds. Core meeting UI is implemented, but full module/product acceptance is incomplete; keep this checkpoint on the development branch.

## Meeting DPI and interaction regression checkpoint

Added DPI-scaled native controls, drawing and minimum window size, with WM_DPICHANGED handling. Release build passed. scripts/windows/Test-MeetingUi.ps1 passed application-owned rendering at 96, 144 and 192 DPI, followed by a real three-member signaling fixture. Diagnostic command checks passed for pin-to-grid, grid/focus/speaker modes, speaker mute restoration, fullscreen bounds restoration and joined-state button availability. These exercise UI command routes, not physical keyboard/mouse or cross-monitor acceptance. Physical cross-monitor behavior and automatic audible-speaker switching remain pending.

On 2026-09-28 the user confirmed local camera display works. This is user-confirmed local preview only; it does not establish physical multiparty audio/video or TURN acceptance. Broadcasting remains deferred.

## Active-speaker selection follow-up

Extracted the UI speaker selector and corrected hold timing: continued speech by the same member no longer renews the hold. Muted/removed members lose speaker eligibility immediately; silence retains the last eligible speaker, and a fresh meeting resets selection. Stale samples are discarded by the UI. Deterministic Release tests passed for noise threshold, hold timing, continued speech, silence, mute/leave, empty roster, rejoin and reset. This is selection-policy coverage; interactive audible-speaker acceptance remains pending.

Release client build and 96/144/192 DPI plus real-three-member UI command regression passed. Fixed the PowerShell test process-handle lifetime so Windows PowerShell reads the fixture exit code reliably, and removed prior render output before each check. Meeting session and three-client synthetic decoded-media regressions passed again.

Initial CTest run found the signaling and mixer executables absent in the migrated output directory (Not Run). Built both targets, then signaling, mixer and speaker tests all passed. No missing executable is counted as a passing test.

## Peer connection state checkpoint

User explicitly allowed this turn to continue below the usual 18% reserve; the exception does not change future runs. Exposed per-peer transport state on the UI thread and added distinct connecting/disconnected/failed hints to video tiles. Unhealthy connections no longer present a cached frame as current video; disconnected/closed events clear queued mixed audio for that peer. This is visibility, not automatic reconnection.

Release Studio and media tests built. Three-client decoded media regression passed, including checks that all six decoded paths report connected and leave clears peer state. DPI renders and real-three-member UI command checks passed. Fault-state labels are implemented but network-fault injection remains pending. Next priority is coordinated peer recovery with stale negotiation rejection and dual-end decoded-media recovery tests.
