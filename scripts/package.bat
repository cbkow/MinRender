@echo off
REM scripts\package.bat — stage everything the Microsoft Store package needs
REM under <build>\deploy\ and optionally pack the MSIX.
REM
REM Prerequisites (run these first):
REM   scripts\build_msvc.bat --build build --target minrender mr-restart minrender-headless --config Release
REM   cd mr-agent && cargo build --release && cd ..    (if agent needs rebuilding)
REM
REM Usage:
REM   scripts\package.bat          — just stage to <build>\deploy
REM   scripts\package.bat --msix   — stage and pack the Microsoft Store MSIX
REM                                  (identity env vars per installer\STORE.md)
REM
REM MR_BUILD_DIR overrides the build tree (default: build).

setlocal

set "REPO=%~dp0.."
set "BUILD=%REPO%\build"
if defined MR_BUILD_DIR set "BUILD=%MR_BUILD_DIR%"
set "DEPLOY=%BUILD%\deploy"
set "QT_BIN=C:\Qt\6.11.1\msvc2022_64\bin"
REM windeployqt needs VCINSTALLDIR to locate the VC++ runtime DLLs
REM (vcruntime140, msvcp140). Without it they're silently skipped and the
REM app won't launch on machines without the VC++ redistributable. Resolve
REM the VS install the same way build_msvc.bat does (vswhere, latest VS with
REM the C++ tools) so a VS version change on the build box doesn't silently
REM point at a path that no longer exists.
if defined VCINSTALLDIR goto :vc_done
set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" goto :vc_fallback
pushd "%VSWHERE_DIR%"
for /f "delims=" %%i in ('.\vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VCINSTALLDIR=%%i\VC\"
popd
if defined VCINSTALLDIR goto :vc_done
:vc_fallback
set "VCINSTALLDIR=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\"
:vc_done
if not exist "%VCINSTALLDIR%Redist" echo WARN: VCINSTALLDIR=%VCINSTALLDIR% has no Redist folder - VC++ runtime DLLs will be missing.
set "AGENT=%REPO%\mr-agent\target\release\mr-agent.exe"

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

if /I not "%~1"=="--msix" goto :done

REM Store packaging lives in PowerShell (token substitution + makeappx
REM discovery are miserable in batch). It re-validates the deploy folder.
powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO%\scripts\make_msix.ps1"
if errorlevel 1 exit /b %errorlevel%

:done
endlocal
exit /b 0
