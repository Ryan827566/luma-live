@echo off
setlocal
pushd "%~dp0..\.."
if errorlevel 1 exit /b 2

if exist "C:\Python314\python.exe" goto bundled_python
where py.exe >nul 2>&1
if errorlevel 1 goto missing_python
py.exe -3 scripts\windows\verify_client.py %*
set "LUMA_TEST_EXIT=%ERRORLEVEL%"
goto report

:bundled_python
"C:\Python314\python.exe" scripts\windows\verify_client.py %*
set "LUMA_TEST_EXIT=%ERRORLEVEL%"
goto report

:missing_python
echo Python 3 was not found. Install Python 3 or provide C:\Python314\python.exe.
set "LUMA_TEST_EXIT=2"

:report
echo.
echo LumaLive client verification finished with exit code %LUMA_TEST_EXIT%.
echo Report: %CD%\output\client-verification.json
popd
if defined CI goto finish
if defined LUMALIVE_TEST_NONINTERACTIVE goto finish
rem Pause only when an interactive console has unredirected input and output.
powershell.exe -NoLogo -NoProfile -NonInteractive -Command "if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected -and -not [Console]::IsOutputRedirected) { exit 0 } else { exit 1 }"
if errorlevel 1 goto finish
pause

:finish
exit /b %LUMA_TEST_EXIT%
