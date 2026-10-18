@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 2

if exist "C:\Python314\python.exe" goto local_python
where py.exe >nul 2>&1
if errorlevel 1 goto missing_python
py.exe -3 -B scripts\windows\manage_account_backup.py create
set "LUMA_BACKUP_EXIT=%ERRORLEVEL%"
goto finish

:local_python
"C:\Python314\python.exe" -B scripts\windows\manage_account_backup.py create
set "LUMA_BACKUP_EXIT=%ERRORLEVEL%"
goto finish

:missing_python
echo Python 3 was not found. Install Python 3 or provide C:\Python314\python.exe.
set "LUMA_BACKUP_EXIT=2"

:finish
popd
echo.
if "%LUMA_BACKUP_EXIT%"=="0" echo Encrypted account backup and isolated restore verification completed.
if not "%LUMA_BACKUP_EXIT%"=="0" echo Account backup failed. See the error above. No live database was replaced.
if defined CI exit /b %LUMA_BACKUP_EXIT%
if defined LUMALIVE_BACKUP_NONINTERACTIVE exit /b %LUMA_BACKUP_EXIT%
pause
exit /b %LUMA_BACKUP_EXIT%
