# LumaLive Phase 2 — Windows Device Enumeration Acceptance

## Scope
This phase adds the first real Windows device layer for the media pipeline. It does not yet claim camera/microphone frame capture or WebRTC publishing.

## Implemented
- `CaptureDeviceInfo` / `CaptureDeviceList` / device type contracts.
- `IDeviceCaptureService` lifecycle and device enumeration contract.
- Windows Media Foundation startup/shutdown.
- Windows camera enumeration through `MFEnumDeviceSources`.
- Windows microphone enumeration through `MFEnumDeviceSources`.
- Stable UTF-8 device names and IDs exposed to the cross-platform contract.
- CMake linkage for Windows Media Foundation (`mfplat`, `mfuuid`, `ole32`).
- Automated contract test for lifecycle, pre-start behavior, and device record validity.

## Self-acceptance
- GCC C++20 compile of the new service: PASS.
- Automated `DeviceCaptureServiceTests`: PASS.
- Test output: `DeviceCaptureServiceTests: PASS`.
- Existing media-pipeline source remains compilable under the same C++20 smoke-test approach.

## Windows hardware acceptance still required
A real Windows machine must verify:
1. Service starts without MF initialization errors.
2. A connected webcam appears with a non-empty name and ID.
3. A connected microphone appears with a non-empty name and ID.
4. Service stops cleanly.

Frame capture is intentionally the next phase; this phase does not pretend enumeration is capture.
