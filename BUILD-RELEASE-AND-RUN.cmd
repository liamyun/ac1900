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
set "BUILD=%ROOT%\build\vs2022"
if not exist "%BUILD%\PersonalCEF.sln" (
  echo Solution missing. Run START-HERE.cmd first.
  pause
  exit /b 1
)
taskkill /F /IM PersonalCEF.exe >nul 2>nul
echo Building PersonalCEF Release x64...
"%CMAKE_EXE%" --build "%BUILD%" --config Release --target PersonalCEF --parallel
if errorlevel 1 (
  echo BUILD FAILED.
  pause
  exit /b 1
)
set "EXE=%ROOT%\bin\Release\PersonalCEF.exe"
if not exist "%EXE%" (
  echo ERROR: Build succeeded but PersonalCEF.exe was not found.
  pause
  exit /b 1
)
echo Starting: %EXE%
start "" "%EXE%"
