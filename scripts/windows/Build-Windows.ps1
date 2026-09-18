$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Root = Join-Path $RepoRoot 'LumaLive'
$WebRtcSdkRoot = Join-Path $RepoRoot 'third_party\webrtc\windows-x64'
Set-Location $Root

$WebRtcHeader = Join-Path $WebRtcSdkRoot 'include\api\peer_connection_interface.h'
$WebRtcLib = Join-Path $WebRtcSdkRoot 'lib\webrtc.lib'

if (-not (Test-Path $WebRtcHeader) -or -not (Test-Path $WebRtcLib)) {
    throw "Vendored WebRTC SDK is missing. Expected files under $WebRtcSdkRoot. Run scripts\windows\Import-WebRTC-SDK.ps1 once to import the existing WebRTC build into this repository."
}

Write-Host "Using repository WebRTC SDK: $WebRtcSdkRoot" -ForegroundColor Cyan
Write-Host 'Configuring LumaLive with Visual Studio 2026 x64...' -ForegroundColor Cyan
cmake --preset windows-vs2026-x64 -DLUMALIVE_WEBRTC_SDK_ROOT="$WebRtcSdkRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

cmake --build --preset windows-vs2026-release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

Write-Host 'Running tests...' -ForegroundColor Cyan
ctest --test-dir build\vs2026-x64 -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

Write-Host 'Build and tests completed.' -ForegroundColor Green
Write-Host 'Final artifacts: output\Release'
Write-Host 'Server: build\vs2026-x64\apps\luma-server\Release\luma_server.exe'
