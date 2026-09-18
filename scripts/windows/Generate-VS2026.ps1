$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Root = Join-Path $RepoRoot 'LumaLive'
Set-Location $Root
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($null -eq $cmakeCommand) { throw 'CMake was not found in PATH.' }
$CMake = $cmakeCommand.Source
Write-Host '=== LumaLive - Visual Studio 2026 Configure ===' -ForegroundColor Cyan
& $CMake --version
if ($LASTEXITCODE -ne 0) { throw 'Unable to execute CMake.' }
$BuildDir = Join-Path $Root 'build\vs2026-x64'
if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
& $CMake --preset windows-vs2026-x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
$Solution = Join-Path $BuildDir 'LumaLive.sln'
$SolutionX = Join-Path $BuildDir 'LumaLive.slnx'
$OpenTarget = if (Test-Path $Solution) { $Solution } elseif (Test-Path $SolutionX) { $SolutionX } else { $null }
if ($null -eq $OpenTarget) { throw "CMake completed but no Visual Studio solution was generated in $BuildDir." }
Write-Host 'Configuration completed successfully.' -ForegroundColor Green
Write-Host "Solution: $OpenTarget" -ForegroundColor Cyan
Start-Process $OpenTarget
