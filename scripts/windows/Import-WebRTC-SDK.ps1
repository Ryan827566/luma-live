#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$WebRtcRoot = $env:LUMALIVE_WEBRTC_SOURCE,
    [string]$WebRtcOut = $env:LUMALIVE_WEBRTC_OUT
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$SdkRoot = Join-Path $RepoRoot 'third_party\webrtc'

if (-not $WebRtcRoot) {
    $candidates = @(
        (Join-Path $RepoRoot 'third_party\src'),
        (Join-Path $RepoRoot 'LumaLive_Environment_Installer\third_party\src')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate 'BUILD.gn')) {
            $WebRtcRoot = (Resolve-Path $candidate).Path
            break
        }
    }
}
if (-not $WebRtcRoot) {
    throw 'WebRTC source checkout not found. Pass -WebRtcRoot or set LUMALIVE_WEBRTC_SOURCE.'
}

if (-not $WebRtcOut) {
    $WebRtcOut = Join-Path $WebRtcRoot 'out\Release'
}
if (-not (Test-Path (Join-Path $WebRtcRoot 'api\peer_connection_interface.h'))) {
    throw "Invalid WebRTC source root: $WebRtcRoot"
}

$WebRtcLib = Join-Path $WebRtcOut 'obj\webrtc.lib'
if (-not (Test-Path $WebRtcLib)) {
    throw "WebRTC library not found: $WebRtcLib"
}

$AbseilRoot = Join-Path $WebRtcRoot 'third_party\abseil-cpp'
if (-not (Test-Path (Join-Path $AbseilRoot 'absl\strings\string_view.h'))) {
    throw "Matching Abseil headers not found: $AbseilRoot"
}

$IncludeRoot = Join-Path $SdkRoot 'include'
$ReleaseLibRoot = Join-Path $SdkRoot 'release'
$DebugLibRoot = Join-Path $SdkRoot 'debug'

New-Item -ItemType Directory -Force -Path $IncludeRoot,$ReleaseLibRoot,$DebugLibRoot | Out-Null

robocopy $WebRtcRoot $IncludeRoot /E /COPY:DAT /R:2 /W:1 /XD out .git | Out-Null
if ($LASTEXITCODE -gt 7) { throw "WebRTC header copy failed: $LASTEXITCODE" }

robocopy (Join-Path $AbseilRoot 'absl') (Join-Path $IncludeRoot 'absl') /E /COPY:DAT /R:2 /W:1 | Out-Null
if ($LASTEXITCODE -gt 7) { throw "Abseil header copy failed: $LASTEXITCODE" }

Copy-Item $WebRtcLib (Join-Path $ReleaseLibRoot 'webrtc.lib') -Force

$DebugOut = Join-Path (Split-Path $WebRtcOut -Parent) 'Debug'
$DebugLib = Join-Path $DebugOut 'obj\webrtc.lib'
if (Test-Path $DebugLib) {
    Copy-Item $DebugLib (Join-Path $DebugLibRoot 'webrtc.lib') -Force
    Write-Host "Debug WebRTC library imported." -ForegroundColor Green
}

Write-Host "WebRTC SDK imported successfully." -ForegroundColor Green
Write-Host "SDK: $SdkRoot" -ForegroundColor Cyan
Write-Host "Release lib: $(Join-Path $ReleaseLibRoot 'webrtc.lib')" -ForegroundColor Cyan
if (Test-Path (Join-Path $IncludeRoot 'absl\strings\string_view.h')) {
    Write-Host "Abseil headers: PASS" -ForegroundColor Green
}
