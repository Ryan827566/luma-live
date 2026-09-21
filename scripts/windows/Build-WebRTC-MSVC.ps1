#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

# Build WebRTC with clang-cl and MSVC STL (use_custom_libcxx=false).
# This fixes the std::__Cr / Chromium-libc++ ABI mismatch.

$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$roots = @(
  (Join-Path $RepoRoot 'LumaLive_Environment_Installer\third_party\src'),
  (Join-Path $RepoRoot 'third_party\src'),
  (Join-Path $RepoRoot 'third_party\webrtc\src')
)

$WebRtcRoot = $null
foreach ($r in $roots) {
  if (Test-Path (Join-Path $r 'BUILD.gn')) { $WebRtcRoot=(Resolve-Path $r).Path; break }
  if (Test-Path $r) {
    $hit=Get-ChildItem $r -Filter BUILD.gn -Recurse -File -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -notmatch '\\out\\' } | Select-Object -First 1
    if ($hit) { $WebRtcRoot=$hit.Directory.FullName; break }
  }
}
if (-not $WebRtcRoot -and $env:LUMALIVE_WEBRTC_SOURCE -and
    (Test-Path (Join-Path $env:LUMALIVE_WEBRTC_SOURCE 'BUILD.gn'))) {
  $WebRtcRoot=(Resolve-Path $env:LUMALIVE_WEBRTC_SOURCE).Path
}
if (-not $WebRtcRoot) {
  throw 'WebRTC source was not found. The script checked the repository WebRTC locations and LUMALIVE_WEBRTC_SOURCE.'
}

$OutDir=Join-Path $WebRtcRoot 'out\LumaLiveRelease'
$Gn=(Get-Command gn -ErrorAction SilentlyContinue).Source
if (-not $Gn) {
  $c=Join-Path $env:DEPOT_TOOLS_WIN 'gn.exe'; if (Test-Path $c) {$Gn=$c}
}
if (-not $Gn) { throw 'gn.exe was not found.' }

$Ninja=(Get-Command ninja -ErrorAction SilentlyContinue).Source
if (-not $Ninja) {$Ninja=(Get-Command autoninja -ErrorAction SilentlyContinue).Source}
if (-not $Ninja) {
  $c=Join-Path $env:DEPOT_TOOLS_WIN 'ninja.exe'; if (Test-Path $c) {$Ninja=$c}
}
if (-not $Ninja) { throw 'ninja.exe/autoninja was not found.' }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
@(
 'target_os = "win"',
 'target_cpu = "x64"',
 'is_debug = false',
 'is_component_build = false',
 'use_custom_libcxx = false',
 'rtc_include_tests = false',
 'rtc_include_internal_audio_device = true',
 'rtc_build_examples = false',
 'is_clang = true'
) | Set-Content (Join-Path $OutDir 'args.gn') -Encoding UTF8

Write-Host "WebRTC source: $WebRtcRoot" -ForegroundColor DarkGray
Write-Host "Output: $OutDir" -ForegroundColor DarkGray
Write-Host "ABI: MSVC STL (use_custom_libcxx=false)" -ForegroundColor Cyan

& $Gn gen $OutDir
if ($LASTEXITCODE -ne 0) { throw 'GN generation failed.' }
& $Ninja -C $OutDir webrtc
if ($LASTEXITCODE -ne 0) { throw 'WebRTC build failed.' }

$BuiltLib=Join-Path $OutDir 'obj\webrtc.lib'
if (-not (Test-Path $BuiltLib)) { throw "Expected WebRTC library was not produced: $BuiltLib" }

$Dumpbin=Get-Command dumpbin -ErrorAction SilentlyContinue
if ($Dumpbin) {
  $bad=& $Dumpbin.Source /symbols $BuiltLib 2>$null | Select-String 'std::__Cr::' | Select-Object -First 1
  if ($bad) { throw 'The generated WebRTC library still contains std::__Cr symbols.' }
}

Write-Host "WebRTC MSVC-STL build completed: $BuiltLib" -ForegroundColor Green
Write-Host "Next: powershell -ExecutionPolicy Bypass -File .\scripts\windows\Import-WebRTC-SDK.ps1" -ForegroundColor Cyan
