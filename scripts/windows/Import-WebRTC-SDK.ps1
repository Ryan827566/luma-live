#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

# This script lives in scripts\windows, so the repository root is three levels up.
$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$SdkRoot = Join-Path $RepoRoot 'third_party\webrtc\windows-x64'
$WebRtcRoot = Join-Path $RepoRoot 'LumaLive_Environment_Installer\third_party\src'
$WebRtcOut = Join-Path $WebRtcRoot 'out\Release'
$WebRtcLib = Join-Path $WebRtcOut 'obj\webrtc.lib'
$AbseilRoot = Join-Path $WebRtcRoot 'third_party\abseil-cpp'
$AbseilHeaders = Join-Path $AbseilRoot 'absl\strings\string_view.h'

if (-not (Test-Path (Join-Path $WebRtcRoot 'api\peer_connection_interface.h'))) {
    throw "WebRTC source was not found at $WebRtcRoot"
}
if (-not (Test-Path $WebRtcLib)) {
    throw "WebRTC library was not found at $WebRtcLib. Run Build-WebRTC-MSVC.ps1 first."
}
if (-not (Test-Path $AbseilHeaders)) {
    throw "The WebRTC source checkout does not contain its matching Abseil headers: $AbseilRoot"
}

$IncludeRoot = Join-Path $SdkRoot 'include'
$LibRoot = Join-Path $SdkRoot 'lib'
New-Item -ItemType Directory -Force -Path $IncludeRoot, $LibRoot | Out-Null

Write-Host "Copying WebRTC headers..." -ForegroundColor Cyan
robocopy $WebRtcRoot $IncludeRoot /E /COPY:DAT /R:2 /W:1 /XD out .git | Out-Null
if ($LASTEXITCODE -gt 7) { throw "WebRTC header copy failed with robocopy exit code $LASTEXITCODE" }

Write-Host "Copying the exact Abseil headers used by this WebRTC checkout..." -ForegroundColor Cyan
$AbslDestination = Join-Path $IncludeRoot 'absl'
robocopy (Join-Path $AbseilRoot 'absl') $AbslDestination /E /COPY:DAT /R:2 /W:1 | Out-Null
if ($LASTEXITCODE -gt 7) { throw "Abseil header copy failed with robocopy exit code $LASTEXITCODE" }

Write-Host "Copying WebRTC static library..." -ForegroundColor Cyan
Copy-Item $WebRtcLib (Join-Path $LibRoot 'webrtc.lib') -Force

Write-Host ""
Write-Host "Vendored WebRTC SDK is ready:" -ForegroundColor Green
Write-Host "  $SdkRoot"
Write-Host "  Headers: $IncludeRoot"
Write-Host "  Library: $(Join-Path $LibRoot 'webrtc.lib')"
