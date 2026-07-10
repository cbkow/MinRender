@echo off
REM scripts\package.bat — stage everything Inno Setup needs under
REM build\deploy\ and optionally run the Inno Setup compiler.
REM
REM Prerequisites (run these first):
REM   scripts\build_msvc.bat --build build --target minrender mr-restart minrender-headless --config Release
REM   cd mr-agent && cargo build --release && cd ..    (if agent needs rebuilding)
REM
REM Usage:
REM   scripts\package.bat          — just stage to build\deploy
REM   scripts\package.bat --iss    — stage and compile the installer

setlocal

set "REPO=%~dp0.."
set "BUILD=%REPO%\build"
set "DEPLOY=%BUILD%\deploy"
set "QT_BIN=C:\Qt\6.11.1\msvc2022_64\bin"
REM windeployqt needs VCINSTALLDIR to locate the VC++ runtime redist DLLs
REM (vcruntime140, msvcp140). Without it they're silently skipped and the
REM app won't launch on machines without the VC++ redistributable.
if not defined VCINSTALLDIR set "VCINSTALLDIR=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\"
set "AGENT=%REPO%\mr-agent\target\release\mr-agent.exe"
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

REM --- Sanity checks ---
if not exist "%BUILD%\minRender.exe" (
  echo ERROR: %BUILD%\minRender.exe missing. Run scripts\build_msvc.bat first.
  exit /b 1
)
if not exist "%BUILD%\mr-restart.exe" (
  echo ERROR: %BUILD%\mr-restart.exe missing. Build the mr-restart target too.
  exit /b 1
)
if not exist "%QT_BIN%\windeployqt.exe" (
  echo ERROR: windeployqt.exe not found at %QT_BIN%
  exit /b 1
)

REM --- Fresh deploy folder ---
if exist "%DEPLOY%" rmdir /S /Q "%DEPLOY%"
mkdir "%DEPLOY%" || exit /b 1

REM --- Executables ---
copy /Y "%BUILD%\minRender.exe"          "%DEPLOY%\" >nul || exit /b 1
copy /Y "%BUILD%\minrender-headless.exe" "%DEPLOY%\" >nul || exit /b 1
copy /Y "%BUILD%\mr-restart.exe"         "%DEPLOY%\" >nul || exit /b 1

REM WinSparkle.dll — CMake's POST_BUILD drops it next to minRender.exe when
REM external\winsparkle is vendored (auto-update enabled). Optional: absent in
REM dev builds without the updater. Must ship beside the exe so the app loads it.
if exist "%BUILD%\WinSparkle.dll" (
  copy /Y "%BUILD%\WinSparkle.dll" "%DEPLOY%\" >nul || exit /b 1
) else (
  echo WARN: %BUILD%\WinSparkle.dll not found - auto-update disabled in this build.
)

REM Frame-preview decode DLLs (OpenEXR/Imath/png/jpeg-turbo/tiff + deps) —
REM vendored at external\install-win64 (vcpkg export, see CMakeLists). Optional:
REM absent means the build has no preview pane and needs nothing shipped.
if exist "%REPO%\external\install-win64\bin\*.dll" (
  copy /Y "%REPO%\external\install-win64\bin\*.dll" "%DEPLOY%\" >nul || exit /b 1
) else (
  echo WARN: external\install-win64\bin has no DLLs - frame preview absent in this build.
)

REM Rust-built agent — warn if missing, don't fail (dev may not have rebuilt)
if exist "%AGENT%" (
  copy /Y "%AGENT%" "%DEPLOY%\" >nul
) else (
  echo WARN: %AGENT% not found. Run `cargo build --release` in mr-agent\ if you need it.
)

REM --- Our resources (fonts, icons, templates, plugins) ---
xcopy /E /I /Y "%REPO%\resources" "%DEPLOY%\resources" >nul || exit /b 1

REM --- Qt runtime via windeployqt ---
REM --qmldir points at our QML sources so windeployqt knows which Qt QML
REM modules to pull in (QtQuick, QtQuick.Controls, QtQml, etc). Without
REM it, the platform plugin lands but QML imports fail at runtime.
"%QT_BIN%\windeployqt.exe" ^
  --release ^
  --compiler-runtime ^
  --no-translations ^
  --no-system-d3d-compiler ^
  --no-opengl-sw ^
  --qmldir "%REPO%\src\ui\qml" ^
  "%DEPLOY%\minRender.exe" || exit /b 1

REM windeployqt on minrender pulls Qt6Core/Network/Gui/etc. The headless
REM sidecar uses Qt6Core + Qt6Network only, which are already present
REM because minrender uses them — no separate windeployqt run needed.

echo.
echo Deploy folder ready: %DEPLOY%

if /I not "%~1"=="--iss" goto :done

REM Inno Setup compile step. Hoisted out of a nested if-block because
REM the ISCC path contains "(x86)" which breaks nested-parens parsing.
if not exist "%ISCC%" goto :no_iscc
echo.
echo Compiling installer...
"%ISCC%" "%REPO%\installer\minrender_installer.iss"
if errorlevel 1 exit /b %errorlevel%

:done
endlocal
exit /b 0

:no_iscc
echo ERROR: Inno Setup ISCC.exe not found at %ISCC%
endlocal
exit /b 1
