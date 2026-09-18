$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Root = Join-Path $RepoRoot 'LumaLive'
Set-Location $Root
function Require($path,$label){ if(-not(Test-Path $path)){throw "$label not found: $path"} }
Write-Host '=== LumaLive Windows Runtime Verification ===' -ForegroundColor Cyan
Require 'CMakeLists.txt' 'Project root'
Require 'apps\luma-studio' 'Studio app'
Require 'client\media-pipeline' 'Media pipeline'
Require 'client\webrtc' 'WebRTC client'
Require 'client\signaling' 'Signaling client'
Require 'server\signaling' 'Signaling server'
$wr=$env:LUMALIVE_WEBRTC_ROOT
$wo=$env:LUMALIVE_WEBRTC_OUT
if(-not $wr){$wr='D:\project\luma-live\LumaLive_Environment_Installer\third_party\src'}
if(-not $wo -and (Test-Path $wr)){ $wo=Join-Path $wr 'out\Release' }
Require (Join-Path $wr 'api\peer_connection_interface.h') 'WebRTC headers'
Require (Join-Path $wo 'obj\webrtc.lib') 'WebRTC complete static library'
Write-Host "WebRTC headers: OK" -ForegroundColor Green
Write-Host "WebRTC library: OK" -ForegroundColor Green
Write-Host "Project structure: OK" -ForegroundColor Green
Write-Host 'Next: run Build-Windows.ps1, then start luma_server and luma_studio for real-device validation.' -ForegroundColor Yellow
