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

if not defined DEVENV (
  echo ERROR: Visual Studio 2022 was not found.
  goto :error
)
if not defined CMAKE_EXE (
  echo ERROR: CMake was not found.
  goto :error
)
set /p "CEF_VERSION="<"%ROOT%\CEF-VERSION.txt"
if not defined CEF_VERSION (
  echo ERROR: CEF-VERSION.txt is empty.
  goto :error
)
set "CEF_ROOT=%ROOT%\deps\cef\cef_binary_%CEF_VERSION%_windows64"
set "BUILD=%ROOT%\build\vs2022"

echo ============================================================
echo PersonalCEF r9 CLEAN - Generate VS2022 solution
echo ============================================================
echo CEF: %CEF_VERSION%
echo.

echo [1/3] Preparing CEF SDK...
if not exist "%CEF_ROOT%\CMakeLists.txt" (
  "%CMAKE_EXE%" -S "%ROOT%\bootstrap" -B "%ROOT%\build\bootstrap"
  if errorlevel 1 goto :error
)

echo.
echo [2/3] Syncing official cefclient framework into this project...
set "PERSONALCEF_PROJECT_ROOT=%ROOT%"
set "PERSONALCEF_CEF_ROOT=%CEF_ROOT%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\sync_cefclient.ps1"
if errorlevel 1 goto :error

echo.
echo [3/3] Generating PersonalCEF.sln...
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 "-DCEF_ROOT=%CEF_ROOT%" -DUSE_SANDBOX=OFF
if errorlevel 1 goto :error
if not exist "%BUILD%\PersonalCEF.sln" (
  echo ERROR: PersonalCEF.sln was not generated.
  goto :error
)

echo.
echo Opening: %BUILD%\PersonalCEF.sln
start "" "%DEVENV%" "%BUILD%\PersonalCEF.sln"
exit /b 0
:error
echo.
echo FAILED. Send the last error lines to ChatGPT.
pause
exit /b 1
