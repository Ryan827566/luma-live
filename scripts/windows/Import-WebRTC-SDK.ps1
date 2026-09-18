$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$SdkRoot = Join-Path $RepoRoot 'third_party\webrtc\windows-x64'
$WebRtcRoot = Join-Path $RepoRoot 'LumaLive_Environment_Installer\third_party\src'
$WebRtcLib = Join-Path $WebRtcRoot 'out\Release\obj\webrtc.lib'

if (-not (Test-Path (Join-Path $WebRtcRoot 'api\peer_connection_interface.h'))) {
    throw "WebRTC source was not found at $WebRtcRoot"
}
if (-not (Test-Path $WebRtcLib)) {
    throw "WebRTC library was not found at $WebRtcLib"
}

$IncludeRoot = Join-Path $SdkRoot 'include'
$LibRoot = Join-Path $SdkRoot 'lib'
New-Item -ItemType Directory -Force -Path $IncludeRoot, $LibRoot | Out-Null

Write-Host "Copying WebRTC headers..." -ForegroundColor Cyan
robocopy $WebRtcRoot $IncludeRoot /E /COPY:DAT /R:2 /W:1 /XD out .git | Out-Null
if ($LASTEXITCODE -gt 7) { throw "Header copy failed with robocopy exit code $LASTEXITCODE" }

Write-Host "Copying webrtc.lib..." -ForegroundColor Cyan
Copy-Item $WebRtcLib (Join-Path $LibRoot 'webrtc.lib') -Force

Write-Host "Vendored WebRTC SDK is ready: $SdkRoot" -ForegroundColor Green
