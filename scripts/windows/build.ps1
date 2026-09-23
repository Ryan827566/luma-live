<#
.SYNOPSIS
  Configure and build LumaLive (VS 2026 generator + v143 toolset for VS 2022 compatibility).
  Prints a per-target status summary at the end.

.DESCRIPTION
  Default behavior is INCREMENTAL build for fast daily development.
  Use -Clean for a full rebuild after changing CMakeLists.txt / runtime settings.

  Parallelism (big speed-up on full rebuilds):
    * MSBuild project-level parallelism via -- /m:N
    * cl.exe file-level parallelism via the CL=/MP env var (no CMake changes needed)
  Default job count is the number of logical processors; override with -Jobs N.

.EXAMPLE
  .\build.ps1                     # Incremental build: Debug + Release, with summary
  .\build.ps1 -Clean              # Full clean rebuild + summary (parallel)
  .\build.ps1 -Clean -Jobs 8      # Full rebuild with 8 parallel jobs
  .\build.ps1 -Config Debug       # Incremental build: Debug only
  .\build.ps1 -Reconfigure        # Re-run cmake configure, then incremental build
  .\build.ps1 -NoClMp             # Disable file-level /MP (falls back to MSBuild-only parallelism)
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel','All')]
    [string]$Config = 'All',

    [string]$Generator = 'Visual Studio 18 2026',
    [string]$Toolset = 'v143',
    [string]$Arch = 'x64',
    [string]$BuildDir = 'build',

    # 0 = auto-detect (logical processor count). N>0 = use exactly N parallel jobs.
    [int]$Jobs = 0,

    [switch]$Clean,
    [switch]$Reconfigure,

    # Disable cl.exe /MP (parallel compilation of .cpp files inside a single project).
    # Useful if /MP causes trouble (e.g. conflicts with /Gm or weird PDB issues).
    [switch]$NoClMp
)

$ErrorActionPreference = 'Stop'

# --------------------------------------------------------------------------
# 0. Force UTF-8 for all external process I/O.
#    Fixes garbled non-ASCII output from CMake / MSBuild on non-UTF-8 consoles
#    (e.g. Chinese Windows with code page 936).
# --------------------------------------------------------------------------
try { chcp 65001 | Out-Null } catch { }
$utf8 = New-Object System.Text.UTF8Encoding $false
[Console]::OutputEncoding = $utf8
[Console]::InputEncoding  = $utf8
$OutputEncoding           = $utf8

# --------------------------------------------------------------------------
# 0b. Resolve parallelism
# --------------------------------------------------------------------------
if ($Jobs -le 0) {
    $Jobs = [Environment]::ProcessorCount
    if ($Jobs -le 0) { $Jobs = 4 }   # very defensive fallback
}

# cl.exe file-level parallelism (per-project, via CL env var).
# /MP is forwarded to cl.exe for every compile; you do NOT need to edit CMakeLists.txt.
if (-not $NoClMp) {
    if ($env:CL) {
        # Preserve any pre-existing CL flags, append /MP.
        if ($env:CL -notmatch '(?:^|\s)/MP(?:\s|$)') {
            $env:CL = "$($env:CL) /MP$Jobs"
        }
    } else {
        $env:CL = "/MP$Jobs"
    }
    Write-Host "==> cl.exe file-level parallelism: /MP$Jobs (via CL env var)" -ForegroundColor Gray
} else {
    Write-Host "==> cl.exe file-level parallelism: DISABLED (-NoClMp)" -ForegroundColor DarkGray
}
Write-Host "==> MSBuild project-level parallelism: /m:$Jobs" -ForegroundColor Gray

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
        [string]$LogDir,
        [int]$Jobs
    )

    $logFile = Join-Path $LogDir "build-$Config.log"
    $lines = New-Object System.Collections.Generic.List[string]

    # Pass MSBuild flags after "--".
    #   /m:N -> MSBuild project-level parallelism
    $msbuildFlags = @("/m:$Jobs")

    $cmakeArgs = @('--build', $BuildDir, '--config', $Config) + $ExtraArgs + @('--') + $msbuildFlags

    Write-Host ""
    Write-Host "==> Building configuration: $Config" -ForegroundColor Cyan
    Write-Host "    Command: cmake $($cmakeArgs -join ' ')" -ForegroundColor DarkGray
    Write-Host ""

    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    # Run cmake and capture ALL output (stdout + stderr).
    #
    # Why temporarily disable ErrorActionPreference?
    #   With $ErrorActionPreference = 'Stop', any line MSBuild writes to
    #   stderr becomes a terminating error and aborts the script before
    #   the summary report is printed. We inspect $LASTEXITCODE ourselves,
    #   so relaxing this inside the function is safe.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $exitCode = 0
    try {
        & cmake @cmakeArgs 2>&1 | ForEach-Object {
            $line = $_.ToString()
            $lines.Add($line)
            Write-Host $line
        }
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $prevEAP
    }

    $sw.Stop()
    $elapsed = $sw.Elapsed

    # Persist log (UTF-8 with BOM so Notepad / VS Code auto-detect correctly)
    $lines | Set-Content -Path $logFile -Encoding UTF8

    # --- Parse the log ---
    $succeeded = @{}
    $failed    = @{}
    $skipped   = @{}

    foreach ($line in $lines) {
        # Success line, e.g.:
        #   "  3>  luma_admin_audit.vcxproj -> D:\...\luma_admin_audit.lib"
        $m = [regex]::Match($line, '^\s*(?:\d+>)?\s*(\S+\.vcxproj)\s*->\s*(.+?)\s*$')
        if ($m.Success) {
            $succeeded[$m.Groups[1].Value] = $m.Groups[2].Value
            continue
        }

        # Failure line. MSBuild errors have this exact shape:
        #   "foo.cpp(10,5): error C2039: ..."           -> compiler
        #   "LINK : fatal error LNK1104: ..."           -> linker
        #   "foo.cpp(10,5): fatal error C1001: ..."     -> compiler fatal
        # And always end with " [D:\...\xxx.vcxproj]".
        # Strict pattern avoids false positives from warnings that mention
        # an identifier literally named "error".
        if ($line -match ':\s*(?:fatal\s+)?error\s+[A-Z]+\d+') {
            $m2 = [regex]::Match($line, '\[[A-Za-z]:[^\]]*[\\/](\w[\w\.\-]*\.vcxproj)\]')
            if ($m2.Success) {
                $proj = $m2.Groups[1].Value
                if (-not $failed.ContainsKey($proj)) {
                    $failed[$proj] = New-Object System.Collections.Generic.List[string]
                }
                $failed[$proj].Add($line.Trim())
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
        Elapsed   = $elapsed
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
    $r = Invoke-LumaBuild -BuildDir $BuildDir -Config $cfg -ExtraArgs $buildExtra -LogDir $logDir -Jobs $Jobs
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
    Write-Host ("Elapsed       : {0:hh\:mm\:ss\.fff}" -f $r.Elapsed)
    Write-Host "Exit code     : $($r.ExitCode)"
    Write-Host "Full log      : $($r.LogFile)" -ForegroundColor DarkGray

    if ($bad -gt 0) {
        Write-Host ""
        Write-Host "  Failed targets (with reasons):" -ForegroundColor Red
        foreach ($proj in ($r.Failed.Keys | Sort-Object)) {
            Write-Host ("    FAIL  {0}" -f $proj) -ForegroundColor Red
            $reasons = $r.Failed[$proj]
            if ($reasons -is [System.Collections.Generic.List[string]]) {
                $reasons | Select-Object -First 5 | ForEach-Object {
                    Write-Host ("          {0}" -f $_) -ForegroundColor DarkRed
                }
                if ($reasons.Count -gt 5) {
                    Write-Host ("          ... and {0} more" -f ($reasons.Count - 5)) -ForegroundColor DarkRed
                }
            }
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