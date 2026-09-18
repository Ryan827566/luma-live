$ErrorActionPreference = 'Stop'
Write-Host '=== LumaLive VS 2026 Environment Check ===' -ForegroundColor Cyan
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) { throw 'CMake was not found in PATH.' }
Write-Host "CMake: $($cmake.Source)"
cmake --version | Select-Object -First 1
Write-Host ''
Write-Host 'Supported generators:' -ForegroundColor Cyan
cmake --help | Select-String 'Visual Studio 18 2026|Visual Studio 18 2026'
Write-Host ''
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format table
} else {
    Write-Warning "vswhere.exe not found at $vswhere"
}
