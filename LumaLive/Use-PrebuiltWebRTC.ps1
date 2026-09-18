param(
  [Parameter(Mandatory=$true)][string]$SdkRoot
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path $SdkRoot).Path
$header=Join-Path $root 'include\api\peer_connection_interface.h'
$lib=Join-Path $root 'lib\webrtc.lib'
if(-not(Test-Path $lib)){ $lib=Join-Path $root 'libwebrtc.lib' }
if(-not(Test-Path $header)){ throw "Not a raw libwebrtc SDK: missing include\api\peer_connection_interface.h" }
if(-not(Test-Path $lib)){ throw "Not a raw libwebrtc SDK: missing lib\webrtc.lib or libwebrtc.lib" }
Write-Host "Raw libwebrtc SDK validated." -ForegroundColor Green
Write-Host "Use: cmake -S . -B out -DLUMALIVE_WEBRTC_SDK_ROOT=\"$root\"" -ForegroundColor Cyan
