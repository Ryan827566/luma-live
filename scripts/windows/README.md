# Windows build scripts

All Windows/PowerShell build helpers are kept in this directory.

- `Prepare-WebRTC.ps1` — validate an existing WebRTC checkout/build and generate local CMake variables.
- `Build-WebRTC.ps1` — build the WebRTC `//:webrtc` target when a rebuild is required.
- `Build-Windows.ps1` — configure, build and test LumaLive Release using the existing WebRTC output.
- `Generate-VS2026.ps1` — configure the Visual Studio 2026 solution only and open it.
- `Check-VS2026.ps1` — inspect CMake and Visual Studio tool availability.
- `Verify-LumaLive-Windows.ps1` — verify Windows/WebRTC prerequisites.
- `Use-PrebuiltWebRTC.ps1` — validate a raw prebuilt WebRTC SDK.
- `Open-LumaLive-VS2026.bat` — convenience launcher for the VS2026 configure script.

## Output layout

Generated CMake/Visual Studio/Ninja intermediate files stay under:

`LumaLive/build/vs2026-x64`

Final Release executables, DLLs and LIB files are emitted together under:

`output/Release`

Debug builds use `output/Debug`.

Generated `build/` and `output/` contents are ignored by Git.
