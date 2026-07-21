@echo off
echo Enabling Windows Hypervisor Platform for QEMU hardware acceleration...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath dism.exe -Verb RunAs -Wait -ArgumentList '/online /Enable-Feature /FeatureName:HypervisorPlatform /All /NoRestart'"
if errorlevel 1 (
    echo Failed to enable Windows Hypervisor Platform.
    pause
    exit /b 1
)
echo.
echo Acceleration is enabled. Restart Windows before running NovaOS.
pause
