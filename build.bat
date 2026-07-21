@echo off
setlocal
set "NOVA_ACTION=%~1"
if "%NOVA_ACTION%"=="" set "NOVA_ACTION=run"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" "%NOVA_ACTION%"
if errorlevel 1 pause
endlocal
