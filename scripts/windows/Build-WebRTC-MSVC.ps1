#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

# Build standalone WebRTC with clang-cl but WITHOUT Chromium's custom libc++.
# This is the ABI-compatible configuration for the MSVC-built LumaLive app.
#
# Result:
#   LumaLive_Environment_Installer\third_party\src\out\Release\obj\webrtc.lib

$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$WebRtcRoot = Join-Path $RepoRoot 'LumaLive_Environment_Installer\third_party\src'
$OutDir = Join-Path $WebRtcRoot 'out\Release'

if (-not (Test-Path (Join-Path $WebRtcRoot 'BUILD.gn'))) {
    throw "WebRTC source checkout was not found: $WebRtcRoot"
}

$GnCandidates = @(
    (Join-Path $WebRtcRoot 'buildtools\win\gn.exe'),
    (Join-Path $env:DEPOT_TOOLS_WIN 'gn.exe')
)
$Gn = $GnCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1

if (-not $Gn) {
    $GnOnPath = Get-Command gn -ErrorAction SilentlyContinue
    if ($GnOnPath) { $Gn = $GnOnPath.Source }
}
if (-not $Gn) {
    throw "gn.exe was not found. Ensure the WebRTC/depot_tools checkout is prepared and gn is on PATH."
}

$NinjaCandidates = @(
    (Join-Path $WebRtcRoot 'third_party\ninja\ninja.exe'),
    (Join-Path $env:DEPOT_TOOLS_WIN 'ninja.exe')
)
$Ninja = $NinjaCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $Ninja) {
    $NinjaOnPath = Get-Command ninja -ErrorAction SilentlyContinue
    if ($NinjaOnPath) { $Ninja = $NinjaOnPath.Source }
}
if (-not $Ninja) {
    $NinjaOnPath = Get-Command autoninja -ErrorAction SilentlyContinue
    if ($NinjaOnPath) { $Ninja = $NinjaOnPath.Source }
}
if (-not $Ninja) {
    throw "ninja.exe/autoninja was not found. Ensure depot_tools is installed and on PATH."
}

$ArgsFile = Join-Path $OutDir 'args.gn'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$GnArgs = @(
    'target_os = "win"',
    'target_cpu = "x64"',
    'is_debug = false',
    'is_component_build = false',
    'use_custom_libcxx = false',
    'rtc_include_tests = false',
    'rtc_include_internal_audio_device = true',
    'rtc_build_examples = false',
    'is_clang = true'
)

Set-Content -Path $ArgsFile -Value ($GnArgs -join [Environment]::NewLine) -Encoding UTF8

Write-Host "WebRTC source : $WebRtcRoot" -ForegroundColor DarkGray
Write-Host "GN args file   : $ArgsFile" -ForegroundColor DarkGray
Write-Host "Build output   : $OutDir" -ForegroundColor DarkGray
Write-Host ""
Write-Host "Generating WebRTC build files..." -ForegroundColor Cyan

& $Gn gen $OutDir
if ($LASTEXITCODE -ne 0) {
    throw "GN generation failed."
}

Write-Host "Building complete WebRTC static library..." -ForegroundColor Cyan
$NinjaName = Split-Path $Ninja -Leaf
if ($NinjaName -ieq 'autoninja.exe') {
    & $Ninja -C $OutDir webrtc
} else {
    & $Ninja -C $OutDir webrtc
}
if ($LASTEXITCODE -ne 0) {
    throw "WebRTC build failed."
}

$BuiltLib = Join-Path $OutDir 'obj\webrtc.lib'
if (-not (Test-Path $BuiltLib)) {
    throw "WebRTC build reported success but the expected library was not found: $BuiltLib"
}

Write-Host ""
Write-Host "WebRTC MSVC-STL-compatible build completed." -ForegroundColor Green
Write-Host "Library: $BuiltLib"
Write-Host ""
Write-Host "Next step:" -ForegroundColor Cyan
Write-Host "  powershell -ExecutionPolicy Bypass -File .\scripts\windows\Import-WebRTC-SDK.ps1"
