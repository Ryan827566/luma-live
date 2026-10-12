param([string]$BinaryDirectory = (Join-Path $PSScriptRoot '../../output/camera-preview/Release'))
$ErrorActionPreference = 'Stop'
$binaryRoot = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$client = Join-Path $binaryRoot 'luma_studio.exe'
$fixtureExe = Join-Path $binaryRoot 'luma_meeting_ui_fixture.exe'
$results = Join-Path $binaryRoot 'ui-checks'
New-Item -ItemType Directory -Path $results -Force | Out-Null
$keys = @('LUMALIVE_MEETING_RENDER_PATH','LUMALIVE_MEETING_RENDER_DPI','LUMALIVE_MEETING_TEST_SERVER','LUMALIVE_MEETING_TEST_ROOM','LUMALIVE_MEETING_TEST_IDENTITY','LUMALIVE_MEETING_TEST_ACTION','LUMALIVE_MEETING_TEST_CONTROLS')
$saved = @{}
foreach ($key in $keys) { $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process'); [Environment]::SetEnvironmentVariable($key, $null, 'Process') }
function Invoke-Render {
    if (Test-Path -LiteralPath $env:LUMALIVE_MEETING_RENDER_PATH) { Remove-Item -LiteralPath $env:LUMALIVE_MEETING_RENDER_PATH }
    $process = Start-Process -FilePath $client -ArgumentList '--meeting' -WindowStyle Hidden -PassThru
    $processHandle = $process.Handle # Retain handle so Windows PowerShell can read ExitCode after exit.
    if (-not $process.WaitForExit(20000)) { $process.Kill(); throw 'Meeting UI render timed out' }
    if ($process.ExitCode -ne 0) { throw "Meeting UI check failed: $($process.ExitCode)" }
    if (-not (Test-Path -LiteralPath $env:LUMALIVE_MEETING_RENDER_PATH)) { throw 'Render file missing' }
}
$fixture = $null
try {
    foreach ($dpi in @(96,144,192)) {
        $env:LUMALIVE_MEETING_RENDER_DPI = "$dpi"
        $env:LUMALIVE_MEETING_RENDER_PATH = Join-Path $results "idle-$dpi.bmp"
        Invoke-Render
        Write-Output "PASS: idle render at $dpi DPI"
    }
    $env:LUMALIVE_MEETING_RENDER_DPI = '96'
    $fixtureLog = Join-Path $results 'fixture.log'
    $fixture = Start-Process -FilePath $fixtureExe -WindowStyle Hidden -PassThru -RedirectStandardOutput $fixtureLog -RedirectStandardError (Join-Path $results 'fixture-error.log')
    $fixtureHandle = $fixture.Handle
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Test-Path -LiteralPath $fixtureLog) -and ((Get-Content -LiteralPath $fixtureLog -Raw) -match 'READY:')) { break }
        Start-Sleep -Milliseconds 50
    }
    $env:LUMALIVE_MEETING_RENDER_PATH = Join-Path $results 'joined-controls.bmp'
    $env:LUMALIVE_MEETING_TEST_SERVER = '127.0.0.1:19730'
    $env:LUMALIVE_MEETING_TEST_ROOM = 'ui-review'
    $env:LUMALIVE_MEETING_TEST_IDENTITY = 'review-host'
    $env:LUMALIVE_MEETING_TEST_ACTION = 'create'
    $env:LUMALIVE_MEETING_TEST_CONTROLS = '1'
    Invoke-Render
    if (-not $fixture.WaitForExit(5000)) { throw 'Fixture did not end after UI left' }
    if ($fixture.ExitCode -ne 0) { throw "Fixture failed with exit code: $($fixture.ExitCode)" }
    Write-Output 'PASS: three-member UI, pin/grid/focus/speaker controls, mute toggle and fullscreen restore'
} finally {
    if ($fixture -and -not $fixture.HasExited) { $fixture.Kill(); $fixture.WaitForExit() }
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
}
