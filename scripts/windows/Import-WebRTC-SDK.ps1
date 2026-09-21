#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$SdkRoot = Join-Path $RepoRoot 'third_party\webrtc\windows-x64'

$roots=@(
 (Join-Path $RepoRoot 'LumaLive_Environment_Installer\third_party\src'),
 (Join-Path $RepoRoot 'third_party\src'),
 (Join-Path $RepoRoot 'third_party\webrtc\src')
)
$WebRtcRoot=$null
foreach($r in $roots){
 if(Test-Path (Join-Path $r 'BUILD.gn')){$WebRtcRoot=(Resolve-Path $r).Path;break}
 if(Test-Path $r){
  $hit=Get-ChildItem $r -Filter BUILD.gn -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object {$_.FullName -notmatch '\\out\\'} | Select-Object -First 1
  if($hit){$WebRtcRoot=$hit.Directory.FullName;break}
 }
}
if(-not $WebRtcRoot -and $env:LUMALIVE_WEBRTC_SOURCE -and (Test-Path (Join-Path $env:LUMALIVE_WEBRTC_SOURCE 'BUILD.gn'))){
 $WebRtcRoot=(Resolve-Path $env:LUMALIVE_WEBRTC_SOURCE).Path
}
if(-not $WebRtcRoot){throw 'WebRTC source checkout was not found.'}

$WebRtcOut=Join-Path $WebRtcRoot 'out\LumaLiveRelease'
$WebRtcLib=Join-Path $WebRtcOut 'obj\webrtc.lib'
$AbseilRoot=Join-Path $WebRtcRoot 'third_party\abseil-cpp'
if(-not (Test-Path (Join-Path $WebRtcRoot 'api\peer_connection_interface.h'))){throw "Invalid WebRTC source: $WebRtcRoot"}
if(-not (Test-Path $WebRtcLib)){throw "WebRTC library not found: $WebRtcLib. Run Build-WebRTC-MSVC.ps1 first."}
if(-not (Test-Path (Join-Path $AbseilRoot 'absl\strings\string_view.h'))){throw "Matching Abseil headers not found: $AbseilRoot"}

$IncludeRoot=Join-Path $SdkRoot 'include'
$LibRoot=Join-Path $SdkRoot 'lib'
New-Item -ItemType Directory -Force -Path $IncludeRoot,$LibRoot | Out-Null

robocopy $WebRtcRoot $IncludeRoot /E /COPY:DAT /R:2 /W:1 /XD out .git | Out-Null
if($LASTEXITCODE -gt 7){throw "WebRTC header copy failed: $LASTEXITCODE"}
robocopy (Join-Path $AbseilRoot 'absl') (Join-Path $IncludeRoot 'absl') /E /COPY:DAT /R:2 /W:1 | Out-Null
if($LASTEXITCODE -gt 7){throw "Abseil header copy failed: $LASTEXITCODE"}
Copy-Item $WebRtcLib (Join-Path $LibRoot 'webrtc.lib') -Force

Write-Host "WebRTC SDK imported successfully." -ForegroundColor Green
Write-Host "Source: $WebRtcRoot"
Write-Host "SDK:    $SdkRoot"
Write-Host "Lib:    $(Join-Path $LibRoot 'webrtc.lib')"
