param(
    [string]$WebRtcRoot = $env:LUMALIVE_WEBRTC_ROOT,
    [string]$WebRtcOut = $env:LUMALIVE_WEBRTC_OUT
)
$ErrorActionPreference = 'Stop'
if (-not $WebRtcRoot) {
    $candidate = 'D:\project\luma-live\LumaLive_Environment_Installer\third_party\src'
    if (Test-Path (Join-Path $candidate 'api\peer_connection_interface.h')) { $WebRtcRoot = $candidate }
}
if (-not $WebRtcRoot) { throw 'Set LUMALIVE_WEBRTC_ROOT or pass -WebRtcRoot.' }
if (-not $WebRtcOut) { $WebRtcOut = Join-Path $WebRtcRoot 'out\Release' }
Write-Host '=== LumaLive WebRTC Environment Preparation ===' -ForegroundColor Cyan
function Require($path,$label){ if(-not(Test-Path $path)){throw "$label not found: $path"} }
Require (Join-Path $WebRtcRoot 'BUILD.gn') 'WebRTC source'
Require (Join-Path $WebRtcRoot 'api\peer_connection_interface.h') 'WebRTC headers'
Require (Join-Path $WebRtcOut 'obj\webrtc.lib') 'WebRTC umbrella library'
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfigRoot = Join-Path $RepoRoot 'LumaLive\build\webrtc'
New-Item -ItemType Directory -Force -Path $ConfigRoot | Out-Null
$ConfigFile = Join-Path $ConfigRoot 'LumaLiveWebRTC.cmake'
@"
set(LUMALIVE_WEBRTC_ROOT "$($WebRtcRoot.Replace('\','/'))" CACHE PATH "LumaLive WebRTC source/header root")
set(LUMALIVE_WEBRTC_OUT "$($WebRtcOut.Replace('\','/'))" CACHE PATH "LumaLive WebRTC GN output directory")
set(LUMALIVE_WEBRTC_OBJ "$((Join-Path $WebRtcOut 'obj').Replace('\','/'))" CACHE PATH "LumaLive WebRTC object/lib directory")
set(LUMALIVE_WEBRTC_LIB "$((Join-Path $WebRtcOut 'obj\webrtc.lib').Replace('\','/'))" CACHE FILEPATH "LumaLive WebRTC umbrella library")
"@ | Set-Content -Path $ConfigFile -Encoding UTF8
$env:LUMALIVE_WEBRTC_ROOT = $WebRtcRoot
$env:LUMALIVE_WEBRTC_OUT = $WebRtcOut
Write-Host "WebRTC root: $WebRtcRoot" -ForegroundColor Green
Write-Host "WebRTC output: $WebRtcOut" -ForegroundColor Green
Write-Host "Generated: $ConfigFile" -ForegroundColor Green
Write-Host 'WebRTC environment check: PASS' -ForegroundColor Green
