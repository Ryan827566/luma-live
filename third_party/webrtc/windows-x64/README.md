# Vendored WebRTC SDK

This directory contains the Windows x64 WebRTC SDK used by LumaLive.

Expected layout:

- include/ - WebRTC public headers plus the transitive headers required by those headers.
- lib/webrtc.lib - the prebuilt WebRTC static library.
- lib/*.lib - any additional prebuilt libraries required by the SDK, if applicable.

The LumaLive build scripts resolve this directory relative to the repository root. They do not use a developer-specific Windows path.

The WebRTC static library is tracked with Git LFS because GitHub blocks regular Git files larger than 100 MiB.

Run scripts/windows/Import-WebRTC-SDK.ps1 on a machine that already has the WebRTC source/build to populate this directory.
