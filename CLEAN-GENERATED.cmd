@echo off
setlocal
for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "PERSONALCEF_PROJECT_ROOT=%ROOT%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\clean_generated.ps1"
pause
