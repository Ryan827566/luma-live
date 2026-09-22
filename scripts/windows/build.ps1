<#
.SYNOPSIS
  Configure and build LumaLive (VS 2026 generator + v143 toolset for VS 2022 compatibility).
  Prints a per-target status summary at the end.

.DESCRIPTION
  Default behavior is INCREMENTAL build for fast daily development.
  Use -Clean for a full rebuild after changing CMakeLists.txt / runtime settings.

.EXAMPLE
  .\build.ps1                     # Incremental build: Debug + Release, with summary
  .\build.ps1 -Config Debug       # Incremental build: Debug only
  .\build.ps1 -Clean              # Full clean rebuild + summary
  .\build.ps1 -Reconfigure        # Re-run cmake configure, then incremental build
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel','All')]
    [string]$Config = 'All',

    [string]$Generator = 'Visual Studio 18 2026',
    [string]$Toolset = 'v143',
    [string]$Arch = 'x64',
    [string]$BuildDir = 'build',

    [switch]$Clean,
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'

# --------------------------------------------------------------------------
# 0. Force UTF-8 for all external process I/O.
#    Fixes garbled non-ASCII output from CMake / MSBuild on non-UTF-8 consoles
#    (e.g. Chinese Windows with code page 936).
# --------------------------------------------------------------------------
try {
    chcp 65001 | Out-Null
} catch {
    # chcp may not be available in some restricted hosts; ignore.
}
$utf8 = New-Object System.Text.UTF8Encoding $false
[Console]::OutputEncoding = $utf8
[Console]::InputEncoding  = $utf8
$OutputEncoding           = $utf8

# --------------------------------------------------------------------------
# 1. Environment checks
# --------------------------------------------------------------------------
Write-Host "==> Checking environment ..." -ForegroundColor Cyan

if (-not (Test-Path 'CMakeLists.txt')) {
    Write-Error "CMakeLists.txt not found. Run this script from the project root directory."
    exit 1
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Error "cmake not found. Ensure CMake is installed and added to PATH."
    exit 1
}
Write-Host "    CMake: $($cmake.Source)" -ForegroundColor Gray
cmake --version | Select-Object -First 1

# --------------------------------------------------------------------------
# 2. Decide whether to (re)configure
# --------------------------------------------------------------------------
$cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
$needConfigure = $false

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "==> [Clean] Removing build directory: $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
    $needConfigure = $true
}
elseif (-not (Test-Path $cacheFile)) {
    Write-Host "==> No CMake cache found, will configure." -ForegroundColor Yellow
    $needConfigure = $true
}
elseif ($Reconfigure) {
    Write-Host "==> [Reconfigure] Forcing cmake configure." -ForegroundColor Yellow
    $needConfigure = $true
}
else {
    Write-Host "==> Reusing existing build directory (incremental build)." -ForegroundColor Green
    Write-Host "    Tip: use -Clean for a full rebuild, -Reconfigure if CMake files changed." -ForegroundColor DarkGray
}

if ($needConfigure) {
    Write-Host "==> Configuring CMake ..." -ForegroundColor Cyan
    Write-Host "    Generator : $Generator" -ForegroundColor Gray
    Write-Host "    Toolset   : $Toolset" -ForegroundColor Gray
    Write-Host "    Arch      : $Arch" -ForegroundColor Gray
    Write-Host "    BuildDir  : $BuildDir" -ForegroundColor Gray

    cmake -S . -B $BuildDir -G $Generator -A $Arch -T $Toolset
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed with exit code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

# --------------------------------------------------------------------------
# 3. Helper: run one build, capture log, parse per-target result
# --------------------------------------------------------------------------
$logDir = Join-Path (Get-Location) 'build-logs'
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }

function Invoke-LumaBuild {
    param(
        [string]$BuildDir,
        [string]$Config,
        [string[]]$ExtraArgs,
        [string]$LogDir
    )

    $logFile = Join-Path $LogDir "build-$Config.log"
    $lines = New-Object System.Collections.Generic.List[string]

    $cmakeArgs = @('--build', $BuildDir, '--config', $Config) + $ExtraArgs

    Write-Host ""
    Write-Host "==> Building configuration: $Config" -ForegroundColor Cyan
    Write-Host "    Command: cmake $($cmakeArgs -join ' ')" -ForegroundColor DarkGray
    Write-Host ""

    & cmake @cmakeArgs 2>&1 | ForEach-Object {
        $line = $_.ToString()
        $lines.Add($line)
        Write-Host $line
    }
    $exitCode = $LASTEXITCODE

    # Persist log (UTF-8 with BOM so Notepad / VS Code auto-detect correctly)
    $lines | Set-Content -Path $logFile -Encoding UTF8

    # --- Parse the log ---
    $succeeded = @{}
    $failed    = @{}
    $skipped   = @{}

    foreach ($line in $lines) {
        # Success line: "  3>  luma_admin_audit.vcxproj -> D:\...\luma_admin_audit.lib"
        $m = [regex]::Match($line, '^\s*(?:\d+>)?\s*(\S+\.vcxproj)\s*->\s*(.+?)\s*$')
        if ($m.Success) {
            $succeeded[$m.Groups[1].Value] = $m.Groups[2].Value
            continue
        }

        # Failure line: contains "error" and ends with "[D:\...\xxx.vcxproj]"
        if ($line -match '\berror\b') {
            $m2 = [regex]::Match($line, '\[[A-Za-z]:[^\]]*[\\/](\w[\w\.\-]*\.vcxproj)\]')
            if ($m2.Success) {
                $failed[$m2.Groups[1].Value] = $true
                continue
            }
        }

        # Skipped line: contains "Skipping" with "project: xxx"
        if ($line -match 'Skipping') {
            $m3 = [regex]::Match($line, 'project\s*:\s*(\S+),')
            if ($m3.Success) {
                $skipped[$m3.Groups[1].Value] = $true
            }
        }
    }

    return [pscustomobject]@{
        Config    = $Config
        ExitCode  = $exitCode
        LogFile   = $logFile
        Succeeded = $succeeded
        Failed    = $failed
        Skipped   = $skipped
    }
}

# --------------------------------------------------------------------------
# 4. Build each requested configuration
# --------------------------------------------------------------------------
$configs = if ($Config -eq 'All') { @('Debug','Release') } else { @($Config) }

$buildExtra = @()
if ($Clean) { $buildExtra += '--clean-first' }

$results = @()
foreach ($cfg in $configs) {
    $r = Invoke-LumaBuild -BuildDir $BuildDir -Config $cfg -ExtraArgs $buildExtra -LogDir $logDir
    $results += $r
}

# --------------------------------------------------------------------------
# 5. Print summary report
# --------------------------------------------------------------------------
Write-Host ""
Write-Host "======================================================================" -ForegroundColor White
Write-Host "                        BUILD SUMMARY REPORT                          " -ForegroundColor White
Write-Host "======================================================================" -ForegroundColor White

$anyFail = $false
foreach ($r in $results) {
    $ok    = $r.Succeeded.Keys.Count
    $bad   = $r.Failed.Keys.Count
    $skip  = $r.Skipped.Keys.Count
    $total = $ok + $bad

    Write-Host ""
    Write-Host "Configuration : $($r.Config)" -ForegroundColor Cyan
    Write-Host "Total targets : $total"
    if ($bad -gt 0) {
        Write-Host "Succeeded     : $ok"  -ForegroundColor Green
        Write-Host "Failed        : $bad" -ForegroundColor Red
        $anyFail = $true
    } else {
        Write-Host "Succeeded     : $ok"  -ForegroundColor Green
        Write-Host "Failed        : 0"   -ForegroundColor Green
    }
    if ($skip -gt 0) {
        Write-Host "Skipped       : $skip" -ForegroundColor DarkGray
    }
    Write-Host "Exit code     : $($r.ExitCode)"
    Write-Host "Full log      : $($r.LogFile)" -ForegroundColor DarkGray

    if ($bad -gt 0) {
        Write-Host ""
        Write-Host "  Failed targets:" -ForegroundColor Red
        foreach ($proj in ($r.Failed.Keys | Sort-Object)) {
            Write-Host ("    {0,-6}{1}" -f "FAIL", $proj) -ForegroundColor Red
        }
    }

    if ($ok -gt 0) {
        Write-Host ""
        Write-Host "  Succeeded targets:" -ForegroundColor Green
        foreach ($proj in ($r.Succeeded.Keys | Sort-Object)) {
            Write-Host ("    {0,-6}{1}" -f "PASS", $proj) -ForegroundColor Green
        }
    }

    if ($skip -gt 0) {
        Write-Host ""
        Write-Host "  Skipped targets:" -ForegroundColor DarkGray
        foreach ($proj in ($r.Skipped.Keys | Sort-Object)) {
            Write-Host ("    {0,-6}{1}" -f "SKIP", $proj) -ForegroundColor DarkGray
        }
    }
}

Write-Host ""
Write-Host "======================================================================" -ForegroundColor White
if ($anyFail) {
    Write-Host "RESULT: SOME TARGETS FAILED" -ForegroundColor Red
    Write-Host "======================================================================" -ForegroundColor White
    exit 1
} else {
    Write-Host "RESULT: ALL TARGETS PASSED" -ForegroundColor Green
    Write-Host "======================================================================" -ForegroundColor White
    exit 0
}