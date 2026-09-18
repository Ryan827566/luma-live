$ErrorActionPreference = 'Stop'

Write-Host '=== LumaLive - Visual Studio 2026 Configure ===' -ForegroundColor Cyan

# ------------------------------------------------------------
# 1. Locate CMake
# ------------------------------------------------------------

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue

if ($null -eq $cmakeCommand) {
    Write-Host ''
    Write-Host 'CMake was not found in PATH.' -ForegroundColor Red
    Write-Host ''
    Write-Host 'Please verify with:' -ForegroundColor Yellow
    Write-Host '    where.exe cmake' -ForegroundColor White
    Write-Host '    cmake --version' -ForegroundColor White
    Write-Host ''
    exit 1
}

$CMake = $cmakeCommand.Source

Write-Host "CMake executable: $CMake" -ForegroundColor DarkGray

# ------------------------------------------------------------
# 2. Check CMake version
# ------------------------------------------------------------

Write-Host ''
Write-Host 'Checking CMake version...' -ForegroundColor Cyan

& $CMake --version

if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host 'Unable to execute CMake.' -ForegroundColor Red
    exit $LASTEXITCODE
}

# ------------------------------------------------------------
# 3. Visual Studio 2026 generator
# ------------------------------------------------------------

$Generator = 'Visual Studio 18 2026'

Write-Host ''
Write-Host "Using generator: $Generator" -ForegroundColor Cyan

# ------------------------------------------------------------
# 4. Resolve project root
# ------------------------------------------------------------

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

Set-Location $Root

Write-Host ''
Write-Host "Project root: $Root" -ForegroundColor DarkGray

# ------------------------------------------------------------
# 5. Verify CMakeLists.txt
# ------------------------------------------------------------

$CMakeLists = Join-Path $Root 'CMakeLists.txt'

if (-not (Test-Path $CMakeLists)) {
    Write-Host ''
    Write-Host 'CMakeLists.txt was not found.' -ForegroundColor Red
    Write-Host $CMakeLists -ForegroundColor Yellow
    exit 1
}

# ------------------------------------------------------------
# 6. Build directory
# ------------------------------------------------------------

$BuildDir = Join-Path $Root 'build\vs2026-x64'

Write-Host ''
Write-Host "Build directory: $BuildDir" -ForegroundColor DarkGray

# ------------------------------------------------------------
# 7. Clean previous CMake configuration
# ------------------------------------------------------------

if (Test-Path $BuildDir) {
    Write-Host ''
    Write-Host 'Removing previous CMake configuration...' -ForegroundColor Yellow

    Remove-Item $BuildDir -Recurse -Force
}

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

# ------------------------------------------------------------
# 8. Configure Visual Studio 2026
# ------------------------------------------------------------

Write-Host ''
Write-Host 'Configuring LumaLive with Visual Studio 2026 x64...' -ForegroundColor Cyan
Write-Host ''

& $CMake `
    -S $Root `
    -B $BuildDir `
    -G $Generator `
    -A x64

if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host '=============================================' -ForegroundColor Red
    Write-Host ' CMake configuration FAILED' -ForegroundColor Red
    Write-Host '=============================================' -ForegroundColor Red
    Write-Host ''
    exit $LASTEXITCODE
}

# ------------------------------------------------------------
# 9. Locate generated solution
# ------------------------------------------------------------

$Solution = Join-Path $BuildDir 'LumaLive.sln'
$SolutionX = Join-Path $BuildDir 'LumaLive.slnx'

$OpenTarget = $null

if (Test-Path $Solution) {
    $OpenTarget = $Solution
}
elseif (Test-Path $SolutionX) {
    $OpenTarget = $SolutionX
}

if ($null -eq $OpenTarget) {
    Write-Host ''
    Write-Host 'CMake completed, but no Visual Studio solution was generated.' -ForegroundColor Red
    Write-Host ''
    Write-Host "Expected directory:" -ForegroundColor Yellow
    Write-Host $BuildDir
    Write-Host ''
    exit 1
}

# ------------------------------------------------------------
# 10. Success
# ------------------------------------------------------------

Write-Host ''
Write-Host '=============================================' -ForegroundColor Green
Write-Host ' LumaLive - Visual Studio 2026' -ForegroundColor Green
Write-Host ' Configuration completed successfully.' -ForegroundColor Green
Write-Host '=============================================' -ForegroundColor Green

Write-Host ''
Write-Host 'Solution:' -ForegroundColor Cyan
Write-Host $OpenTarget -ForegroundColor White

# ------------------------------------------------------------
# 11. Open Visual Studio
# ------------------------------------------------------------

Write-Host ''
Write-Host 'Opening Visual Studio 2026...' -ForegroundColor Cyan

Start-Process $OpenTarget