$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$WebRtcRoot = $env:LUMALIVE_WEBRTC_ROOT
$WebRtcOut  = $env:LUMALIVE_WEBRTC_OUT
if (-not $WebRtcRoot) {
    $candidate = 'D:\project\luma\LumaLive_Environment_Installer\third_party\src'
    if (Test-Path (Join-Path $candidate 'api\peer_connection_interface.h')) { $WebRtcRoot = $candidate }
}
if (-not $WebRtcOut -and $WebRtcRoot) {
    $candidateOut = Join-Path $WebRtcRoot 'out\Release'
    if (Test-Path (Join-Path $candidateOut 'obj\webrtc.lib')) { $WebRtcOut = $candidateOut }
}

if (-not $WebRtcRoot -or -not (Test-Path (Join-Path $WebRtcRoot 'api\peer_connection_interface.h'))) {
    throw 'WebRTC source was not found. Set LUMALIVE_WEBRTC_ROOT to the WebRTC checkout root.'
}
if (-not $WebRtcOut -or -not (Test-Path (Join-Path $WebRtcOut 'obj\webrtc.lib'))) {
    throw 'WebRTC library was not found. Build the WebRTC GN target //:webrtc first (obj\webrtc.lib).' 
}

Write-Host "WebRTC source: $WebRtcRoot" -ForegroundColor DarkGray
Write-Host "WebRTC output: $WebRtcOut" -ForegroundColor DarkGray
Write-Host 'Configuring LumaLive with Visual Studio 2026 x64...' -ForegroundColor Cyan
cmake --preset windows-vs2026-x64 -DLUMALIVE_WEBRTC_ROOT="$WebRtcRoot" -DLUMALIVE_WEBRTC_OUT="$WebRtcOut"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

cmake --build --preset windows-vs2026-release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

Write-Host 'Running tests...' -ForegroundColor Cyan
ctest --test-dir build\vs2026-x64 -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

Write-Host 'Build and tests completed.' -ForegroundColor Green
Write-Host 'Studio: build\vs2026-x64\apps\luma-studio\Release\luma_studio.exe'
Write-Host 'Server: build\vs2026-x64\apps\luma-server\Release\luma_server.exe'
