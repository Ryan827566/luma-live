@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 2

if exist "C:\Python314\python.exe" goto local_python
where py.exe >nul 2>&1
if errorlevel 1 goto missing_python
py.exe -3 scripts\windows\start_local_lumalive.py %*
set "LUMA_START_EXIT=%ERRORLEVEL%"
goto finish

:local_python
"C:\Python314\python.exe" scripts\windows\start_local_lumalive.py %*
set "LUMA_START_EXIT=%ERRORLEVEL%"
goto finish

:missing_python
echo Python 3 was not found. Install Python 3 or provide C:\Python314\python.exe.
set "LUMA_START_EXIT=2"

:finish
popd
if "%LUMA_START_EXIT%"=="0" exit /b 0
echo.
echo LumaLive did not start. See the error above and output\services\account logs.
if defined CI exit /b %LUMA_START_EXIT%
if defined LUMALIVE_START_NONINTERACTIVE exit /b %LUMA_START_EXIT%
pause
exit /b %LUMA_START_EXIT%
