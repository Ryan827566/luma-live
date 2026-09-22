[CmdletBinding()]
param(
    [string]$WebRtcRoot = $env:LUMALIVE_WEBRTC_SOURCE,
    [string]$WebRtcOut = $env:LUMALIVE_WEBRTC_OUT
)

$ErrorActionPreference = 'Stop'

if (-not $WebRtcRoot) {
    throw 'Set LUMALIVE_WEBRTC_SOURCE or pass -WebRtcRoot.'
}
if (-not (Test-Path (Join-Path $WebRtcRoot 'BUILD.gn'))) {
    throw "WebRTC source checkout not found: $WebRtcRoot"
}
if (-not $WebRtcOut) {
    $WebRtcOut = Join-Path $WebRtcRoot 'out\Release'
}
if (-not (Test-Path (Join-Path $WebRtcOut 'obj\webrtc.lib'))) {
    throw "WebRTC library not found: $(Join-Path $WebRtcOut 'obj\webrtc.lib')"
}

$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$ConfigRoot = Join-Path $RepoRoot 'LumaLive\build\webrtc'
New-Item -ItemType Directory -Force -Path $ConfigRoot | Out-Null
$ConfigFile = Join-Path $ConfigRoot 'LumaLiveWebRTC.cmake'

@"
set(LUMALIVE_WEBRTC_SOURCE "$($WebRtcRoot.Replace('','/'))" CACHE PATH "Optional local WebRTC source checkout")
set(LUMALIVE_WEBRTC_OUT "$($WebRtcOut.Replace('','/'))" CACHE PATH "Optional local WebRTC GN output directory")
set(LUMALIVE_WEBRTC_LIB "$((Join-Path $WebRtcOut 'obj\webrtc.lib').Replace('','/'))" CACHE FILEPATH "Optional local WebRTC umbrella library")
"@ | Set-Content -Path $ConfigFile -Encoding UTF8

Write-Host "WebRTC source: $WebRtcRoot" -ForegroundColor Green
Write-Host "WebRTC output: $WebRtcOut" -ForegroundColor Green
Write-Host "Generated: $ConfigFile" -ForegroundColor Green
Write-Host 'Environment check: PASS' -ForegroundColor Green
