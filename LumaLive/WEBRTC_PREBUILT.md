# LumaLive WebRTC prebuilt integration

LumaLive's native adapter consumes the **raw libwebrtc API** (`api/peer_connection_interface.h`) and a matching `webrtc.lib`. It is not compatible with wrapper-only packages whose public API is different.

## Supported layouts

```text
webrtc-sdk-root/
├── include/
│   ├── api/
│   ├── rtc_base/
│   ├── media/
│   └── ...
└── lib/
    └── webrtc.lib
```

or `webrtc.lib` directly under the SDK root's `lib`/root location.

## Configure

```powershell
.\Use-PrebuiltWebRTC.ps1 -SdkRoot D:\path\to\raw-libwebrtc
cmake -S . -B out\Release -G Ninja -DLUMALIVE_WEBRTC_SDK_ROOT="D:\path\to\raw-libwebrtc"
cmake --build out\Release --config Release
```

The project also continues to support the user's source checkout/output:

```text
LUMALIVE_WEBRTC_ROOT = WebRTC src root
LUMALIVE_WEBRTC_OUT  = WebRTC GN output directory
```

This is preferable when the exact WebRTC revision used by LumaLive must match the user's own build.

## Prebuilt sources checked during development

`webrtc-sdk/libwebrtc` publishes Windows x64 binary releases, but those releases are a **C++ wrapper** rather than the raw Chromium `api/* + webrtc.lib` layout consumed by this adapter, so they are not silently substituted. citeturn3view0

Microsoft's WinRTC package also publishes a static `webrtc.lib` and headers, but its current public NuGet package is based on the much older M84-era fork, so it is not used as a drop-in for the modern adapter. citeturn5search0turn0search6

For that reason the build remains revision-agnostic and accepts either the user's current WebRTC checkout or a matching raw prebuilt package.
