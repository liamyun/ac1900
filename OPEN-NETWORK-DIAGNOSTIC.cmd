@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%F in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$c=@((Join-Path $PWD 'runtime\logs\network-diagnostic.log'),(Join-Path $PWD 'bin\Release\runtime\logs\network-diagnostic.log'),(Join-Path $PWD 'bin\Debug\runtime\logs\network-diagnostic.log')); $f=$c ^| ? {Test-Path $_} ^| Get-Item ^| Sort-Object LastWriteTime -Descending ^| Select-Object -First 1; if($f){$f.FullName}"`) do set "LOG=%%F"
if not defined LOG (
  echo Network diagnostic log not found under project/runtime or bin/Release/runtime or bin/Debug/runtime.
  echo Start PersonalCEF once, reproduce the problem, then run this file again.
  pause
  exit /b 1
)
echo Opening:
echo %LOG%
notepad.exe "%LOG%"
