param(
  [string]$WebRtcRoot = $env:LUMALIVE_WEBRTC_ROOT,
  [string]$OutDir = 'out\Release'
)
$ErrorActionPreference = 'Stop'
if(-not $WebRtcRoot){ $WebRtcRoot='D:\project\luma\LumaLive_Environment_Installer\third_party\src' }
if(-not(Test-Path (Join-Path $WebRtcRoot 'BUILD.gn'))){ throw "WebRTC checkout not found: $WebRtcRoot" }
Set-Location $WebRtcRoot
Write-Host "Building WebRTC target //:webrtc ..." -ForegroundColor Cyan
autoninja -C $OutDir webrtc
if($LASTEXITCODE -ne 0){ throw 'WebRTC //:webrtc build failed.' }
$lib=Join-Path $OutDir 'obj\webrtc.lib'
if(-not(Test-Path $lib)){ throw "WebRTC build reported success but $lib was not found." }
Write-Host "WebRTC ready: $lib" -ForegroundColor Green
