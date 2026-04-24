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
set "QT_BIN=C:\Qt\6.11.0\msvc2022_64\bin"
set "AGENT=%REPO%\mr-agent\target\release\mr-agent.exe"
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

REM --- Sanity checks ---
if not exist "%BUILD%\minrender.exe" (
  echo ERROR: %BUILD%\minrender.exe missing. Run scripts\build_msvc.bat first.
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
copy /Y "%BUILD%\minrender.exe"          "%DEPLOY%\" >nul || exit /b 1
copy /Y "%BUILD%\minrender-headless.exe" "%DEPLOY%\" >nul || exit /b 1
copy /Y "%BUILD%\mr-restart.exe"         "%DEPLOY%\" >nul || exit /b 1

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
  --no-translations ^
  --no-system-d3d-compiler ^
  --no-opengl-sw ^
  --qmldir "%REPO%\src\ui\qml" ^
  "%DEPLOY%\minrender.exe" || exit /b 1

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
