@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Generate-VS2026.ps1"
if errorlevel 1 pause
