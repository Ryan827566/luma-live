#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

# Build-Windows.ps1 is under <repo>\scripts\windows\.
# Resolve the repository root from the script location; no machine-specific absolute paths are used.
$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$Root = Join-Path $RepoRoot 'LumaLive'
$WebRtcSdkRoot = Join-Path $RepoRoot 'third_party\webrtc'
$WebRtcLibRoot = Join-Path $WebRtcSdkRoot 'release'
$WebRtcHeader = Join-Path $WebRtcSdkRoot 'include\api\peer_connection_interface.h'
$WebRtcLib = Join-Path $WebRtcLibRoot 'webrtc.lib'

if (-not (Test-Path $Root)) { throw "LumaLive source directory was not found: $Root" }
if (-not (Test-Path $WebRtcHeader)) { throw "WebRTC headers are missing. Expected: $WebRtcHeader" }
if (-not (Test-Path $WebRtcLib)) { throw "WebRTC library is missing. Expected: $WebRtcLib" }

# The existing SDK was proven to contain Chromium libc++ std::__Cr symbols.
# Rebuild/import automatically when dumpbin detects that incompatible ABI.
$Dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
if ($Dumpbin) {
    Write-Host "Checking WebRTC C++ ABI..." -ForegroundColor Cyan
    $badAbi = & $Dumpbin.Source /symbols $WebRtcLib 2>$null |
        Select-String 'std::__Cr::' | Select-Object -First 1
    if ($badAbi) {
        Write-Host "Detected incompatible Chromium libc++ ABI in webrtc.lib." -ForegroundColor Yellow
        & (Join-Path $RepoRoot 'scripts\windows\Build-WebRTC-MSVC.ps1')
        if ($LASTEXITCODE -ne 0) { throw "WebRTC MSVC-STL rebuild failed." }
        & (Join-Path $RepoRoot 'scripts\windows\Import-WebRTC-SDK.ps1')
        if ($LASTEXITCODE -ne 0) { throw "WebRTC SDK import failed." }
    }
}

Write-Host "Repository root: $RepoRoot" -ForegroundColor DarkGray
Write-Host "LumaLive source:  $Root" -ForegroundColor DarkGray
Write-Host "WebRTC headers:  $WebRtcSdkRoot\include" -ForegroundColor DarkGray
Write-Host "WebRTC libraries: $WebRtcLibRoot" -ForegroundColor DarkGray

$ProgramFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
$VsWhereCandidates = @(
    (Join-Path $ProgramFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$VsWhere = $VsWhereCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$VsInstances = @()
if ($VsWhere) {
    try { $VsInstances = @( & $VsWhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json 2>$null | ConvertFrom-Json ) }
    catch { $VsInstances = @() }
}
$Vs2026 = $VsInstances | Where-Object { $_.installationVersion -and ([version]$_.installationVersion).Major -ge 18 } | Select-Object -First 1
$Vs2022 = $VsInstances | Where-Object { $_.installationVersion -and ([version]$_.installationVersion).Major -eq 17 } | Select-Object -First 1
$CMakeHelp = (& cmake --help 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'CMake was not found on PATH. Install CMake 4.2+ and reopen PowerShell.' }

Push-Location $Root
try {
    if ($Vs2026 -and $CMakeHelp -match 'Visual Studio 18 2026') {
        $BuildDir = 'build\vs2026-x64'
        $Configuration = 'Release'
        $BuildLabel = 'Visual Studio 2026 x64'
        Write-Host "Selected: $BuildLabel" -ForegroundColor Cyan
        cmake --preset windows-vs2026-x64 -DLUMALIVE_WEBRTC_SDK_ROOT="$WebRtcSdkRoot"
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed for Visual Studio 2026.' }
        cmake --build --preset windows-vs2026-release --parallel
        if ($LASTEXITCODE -ne 0) { throw 'Release build failed for Visual Studio 2026.' }
    }
    elseif ($Vs2022 -and $CMakeHelp -match 'Visual Studio 17 2022') {
        $BuildDir = 'build\vs2022-x64'
        $Configuration = 'Release'
        $BuildLabel = 'Visual Studio 2022 x64'
        Write-Host "Selected: $BuildLabel" -ForegroundColor Cyan
        cmake -S . -B $BuildDir -G 'Visual Studio 17 2022' -A x64 -T v143 -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DLUMALIVE_WEBRTC_SDK_ROOT="$WebRtcSdkRoot"
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed for Visual Studio 2022.' }
        cmake --build $BuildDir --config $Configuration --parallel
        if ($LASTEXITCODE -ne 0) { throw 'Release build failed for Visual Studio 2022.' }
    }
    else { throw 'No supported Visual Studio generator was found. Install Visual Studio 2026 or 2022 with the C++ build tools, and CMake 4.2+.' }

    Write-Host 'Running tests...' -ForegroundColor Cyan
    ctest --test-dir $BuildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

    $OutputDir = Join-Path $RepoRoot 'output\Release'
    Write-Host ''
    Write-Host 'Build and tests completed.' -ForegroundColor Green
    Write-Host "Compiler:         $BuildLabel"
    Write-Host "Build directory:  $Root\$BuildDir"
    Write-Host "Final artifacts:  $OutputDir"
}
finally { Pop-Location }