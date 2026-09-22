@echo off
setlocal
set "REPOROOT=%~dp0..\.."
cd /d "%REPOROOT%\LumaLive"
powershell -NoProfile -ExecutionPolicy Bypass -File "%REPOROOT%\scripts\windows\Generate-VS2026.ps1"
if errorlevel 1 pause
