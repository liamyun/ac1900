@echo off
setlocal EnableExtensions
for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
set "DEVENV="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe" set "DEVENV=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"
if not defined DEVENV if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.exe" set "DEVENV=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.exe"
if not defined DEVENV if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\devenv.exe" set "DEVENV=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\devenv.exe"
if not defined DEVENV if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`powershell.exe -NoProfile -Command "$p = ^& '%VSWHERE%' -latest -products '*' -version '[17.0,18.0)' -find 'Common7\IDE\devenv.exe'; if ($p) { $p ^| Select-Object -First 1 }"`) do set "DEVENV=%%I"
set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if defined DEVENV for %%I in ("%DEVENV%") do if exist "%%~dpICommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%%~dpICommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not defined CMAKE_EXE (
  echo ERROR: CMake was not found.
  pause
  exit /b 1
)
if not "%~1"=="" (
  >"%ROOT%\CEF-VERSION.txt" echo %~1
)
set /p "CEF_VERSION="<"%ROOT%\CEF-VERSION.txt"
if not defined CEF_VERSION (
  echo ERROR: CEF-VERSION.txt is empty.
  pause
  exit /b 1
)
set "CEF_ROOT=%ROOT%\deps\cef\cef_binary_%CEF_VERSION%_windows64"
echo Updating PersonalCEF to CEF: %CEF_VERSION%
"%CMAKE_EXE%" -S "%ROOT%\bootstrap" -B "%ROOT%\build\bootstrap"
if errorlevel 1 goto :error
set "PERSONALCEF_PROJECT_ROOT=%ROOT%"
set "PERSONALCEF_CEF_ROOT=%CEF_ROOT%"
set "PERSONALCEF_FORCE_SYNC=1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\sync_cefclient.ps1" -ForceSync
if errorlevel 1 goto :error
if exist "%ROOT%\build\vs2022" rmdir /S /Q "%ROOT%\build\vs2022"
"%CMAKE_EXE%" -S "%ROOT%" -B "%ROOT%\build\vs2022" -G "Visual Studio 17 2022" -A x64 "-DCEF_ROOT=%CEF_ROOT%" -DUSE_SANDBOX=OFF
if errorlevel 1 goto :error
echo.
echo UPDATE COMPLETE.
if defined DEVENV start "" "%DEVENV%" "%ROOT%\build\vs2022\PersonalCEF.sln"
exit /b 0
:error
echo.
echo UPDATE FAILED. The new CEF may have changed cefclient source/API around one of our adapter hooks.
echo Send the patch error to ChatGPT; the untouched CEF SDK is still safe under deps\cef.
pause
exit /b 1
