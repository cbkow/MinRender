# scripts\make_msix.ps1 - stage build\deploy into an MSIX package for the
# Microsoft Store. Run via scripts\package.bat --msix (which produces
# build\deploy first), or directly once build\deploy exists.
#
# Identity comes from Partner Center (the app's "Product identity" page) via
# env vars or parameters:
#   MR_MSIX_IDENTITY_NAME       Package/Identity/Name
#   MR_MSIX_PUBLISHER           Package/Identity/Publisher  (CN=<GUID>)
#   MR_MSIX_PUBLISHER_DISPLAY   PublisherDisplayName
# See installer\STORE.md for the full runbook.
#
# -Sideload skips the identity requirement and stamps placeholder test
# identity, for local install testing with Developer Mode:
#   Add-AppxPackage -Register build\msix\AppxManifest.xml
# (registers the staged loose files as an installed package, no signing
# needed; uninstall from Start like any app).

param(
    [string]$IdentityName     = $env:MR_MSIX_IDENTITY_NAME,
    [string]$Publisher        = $env:MR_MSIX_PUBLISHER,
    [string]$PublisherDisplay = $env:MR_MSIX_PUBLISHER_DISPLAY,
    [switch]$Sideload,
    [switch]$SkipPack   # stage only (for Add-AppxPackage -Register testing)
)

$ErrorActionPreference = 'Stop'

$Repo   = Split-Path -Parent $PSScriptRoot
$Deploy = Join-Path $Repo 'build\deploy'
$Stage  = Join-Path $Repo 'build\msix'

# --- Identity -------------------------------------------------------------
if ($Sideload) {
    if (-not $IdentityName)     { $IdentityName     = 'minRender.SideloadTest' }
    if (-not $Publisher)        { $Publisher        = 'CN=minRender Dev' }
    if (-not $PublisherDisplay) { $PublisherDisplay = 'minRender Dev' }
} elseif (-not ($IdentityName -and $Publisher -and $PublisherDisplay)) {
    throw ("Store identity not set. Set MR_MSIX_IDENTITY_NAME, MR_MSIX_PUBLISHER, " +
           "MR_MSIX_PUBLISHER_DISPLAY from Partner Center's Product identity page " +
           "(see installer\STORE.md), or pass -Sideload for a local test package.")
}

# --- Version: single source of truth is project() in CMakeLists.txt -------
$cml = Get-Content (Join-Path $Repo 'CMakeLists.txt') -Raw
if ($cml -notmatch 'project\(MinRender VERSION (\d+)\.(\d+)\.(\d+)') {
    throw 'Could not parse project(MinRender VERSION x.y.z) from CMakeLists.txt'
}
$Version = '{0}.{1}.{2}.0' -f $Matches[1], $Matches[2], $Matches[3]

# --- Sanity checks --------------------------------------------------------
if (-not (Test-Path (Join-Path $Deploy 'minRender.exe'))) {
    throw "$Deploy\minRender.exe missing - run scripts\package.bat first."
}
# WinSparkle.lib is a load-time import: if the DLL was staged, the exe was
# built with the self-updater linked in. Store policy forbids self-updating,
# and deleting the DLL here would just make the exe fail to launch.
if (Test-Path (Join-Path $Deploy 'WinSparkle.dll')) {
    throw ("build\deploy contains WinSparkle.dll - this build links the self-updater. " +
           "Reconfigure with -DMINRENDER_STORE_BUILD=ON, rebuild, and re-run package.bat.")
}
foreach ($exe in 'minrender-headless.exe', 'mr-restart.exe', 'mr-agent.exe') {
    if (-not (Test-Path (Join-Path $Deploy $exe))) {
        throw "$Deploy\$exe missing - the MSIX declares it; rebuild before packaging."
    }
}

# --- Stage ----------------------------------------------------------------
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
Copy-Item $Deploy $Stage -Recurse
Copy-Item (Join-Path $Repo 'installer\msix\Assets') (Join-Path $Stage 'Assets') -Recurse

$manifest = Get-Content (Join-Path $Repo 'installer\msix\AppxManifest.xml.in') -Raw
$manifest = $manifest.
    Replace('$MRIDENTITYNAME$', $IdentityName).
    Replace('$MRPUBLISHER$', $Publisher).
    Replace('$MRPUBLISHERDISPLAY$', $PublisherDisplay).
    Replace('$MRVERSION$', $Version)
Set-Content -Path (Join-Path $Stage 'AppxManifest.xml') -Value $manifest -Encoding UTF8

Write-Host "Staged MSIX layout: $Stage (version $Version)"
if ($SkipPack) {
    Write-Host 'Skipping pack. Test with: Add-AppxPackage -Register build\msix\AppxManifest.xml'
    exit 0
}

# --- Pack -----------------------------------------------------------------
$kits = 'C:\Program Files (x86)\Windows Kits\10\bin'
$makeappx = Get-ChildItem -Path $kits -Recurse -Filter makeappx.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $makeappx) {
    throw "makeappx.exe not found under $kits - install the Windows 10/11 SDK."
}

$OutFile = Join-Path $Repo ("build\minRender-{0}-x64.msix" -f $Version)
& $makeappx.FullName pack /o /d $Stage /p $OutFile
if ($LASTEXITCODE -ne 0) { throw "makeappx failed ($LASTEXITCODE)" }

Write-Host ''
Write-Host "MSIX ready: $OutFile"
if ($Sideload) {
    Write-Host 'Test-identity package: not for Store upload. To install it locally,'
    Write-Host 'either sign it with a cert whose subject matches the Publisher, or use'
    Write-Host 'the unsigned loose-file route: make_msix.ps1 -Sideload -SkipPack, then'
    Write-Host 'Add-AppxPackage -Register build\msix\AppxManifest.xml  (Developer Mode).'
} else {
    Write-Host 'Upload this file in Partner Center - the Store signs it during ingestion.'
}
