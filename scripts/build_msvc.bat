@echo off
REM MSVC + Ninja build driver. Loads the Visual Studio x64 environment
REM and forwards the rest of the command line to cmake. Use like:
REM   scripts\build_msvc.bat -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release ...
REM Or for a rebuild:
REM   scripts\build_msvc.bat --build build --target minrender --config Release

setlocal
set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
if not exist "%VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" (
  echo ERROR: vcvarsall.bat not found under %VS_ROOT%
  exit /b 1
)

call "%VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%

set "PATH=C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;%PATH%"
cmake %*
exit /b %errorlevel%
