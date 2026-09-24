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
