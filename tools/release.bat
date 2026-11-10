@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL%" exit /b 9009
"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\scripts\Build-AstraMailPackage.ps1" %*
set "BUILD_RC=%ERRORLEVEL%"
endlocal & exit /b %BUILD_RC%
