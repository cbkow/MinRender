@echo off
REM MSVC + Ninja build driver. Loads the Visual Studio x64 environment
REM and forwards the rest of the command line to cmake. Use like:
REM   scripts\build_msvc.bat -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release ...
REM Or for a rebuild:
REM   scripts\build_msvc.bat --build build --target minrender --config Release

setlocal
REM Resolve the VS install via vswhere so the script survives machine
REM and version changes; fall back to the known 2022 Community path.
REM The "(x86)" in the vswhere path breaks cmd's for-loop parens parsing
REM (even quoted — same class of issue as noted in package.bat), so pushd
REM into the installer dir and invoke vswhere.exe bare instead.
set "VS_ROOT="
set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" goto :vswhere_done
pushd "%VSWHERE_DIR%"
for /f "delims=" %%i in ('.\vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VS_ROOT=%%i"
popd
:vswhere_done
if not defined VS_ROOT set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" (
  echo ERROR: vcvarsall.bat not found under %VS_ROOT%
  exit /b 1
)

call "%VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%

set "PATH=C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;%PATH%"
cmake %*
exit /b %errorlevel%
