@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%F in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$c=@((Join-Path $PWD 'runtime\logs\network-diagnostic.log'),(Join-Path $PWD 'bin\Release\runtime\logs\network-diagnostic.log'),(Join-Path $PWD 'bin\Debug\runtime\logs\network-diagnostic.log')); $f=$c ^| ? {Test-Path $_} ^| Get-Item ^| Sort-Object LastWriteTime -Descending ^| Select-Object -First 1; if($f){$f.FullName}"`) do set "LOG=%%F"
if not defined LOG (
  echo Network diagnostic log not found.
  pause
  exit /b 1
)
set "OUT=%TEMP%\PersonalCEF-network-focus.txt"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='%LOG%'; Get-Content -LiteralPath $p | ? {$_ -match '(^| )NAV |CONFIG |BROWSER_CREATED|subscriptions|checkout_pricing_config|pageConfigs/billing|http=403|cf_mitigated=challenge'} | Set-Content -Encoding UTF8 -LiteralPath '%OUT%'"
echo Filtered diagnostic:
echo %OUT%
notepad.exe "%OUT%"
