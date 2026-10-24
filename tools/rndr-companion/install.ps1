<#
.SYNOPSIS
  Install the minRender RNDR companion as a per-user logon task.

.DESCRIPTION
  - Copies rndr-companion.ps1 to %USERPROFILE%\minRender\companion\
  - Writes a default rndr-companion.json next to minRender's config.json if
    none exists (edit it afterwards; the task re-reads it on restart)
  - Sets HKCU\Software\MinRender\Companion\Enabled = 1
  - Registers the scheduled task "minRender RNDR companion": runs at logon
    of this user, in the interactive session (the RNDR client needs the
    desktop and the GPU), hidden (conhost --headless on Windows 11, hidden
    PowerShell window on older builds), no execution time limit, restarted
    every minute if it fails, single instance.
  - Starts it.

  Run as the user who runs minRender. No elevation needed.
#>
[CmdletBinding()]
param(
    [string]$TaskName = 'minRender RNDR companion',
    # Register without starting (e.g. to edit the config first).
    [switch]$NoStart
)
$ErrorActionPreference = 'Stop'

$src     = Join-Path $PSScriptRoot 'rndr-companion.ps1'
# minRender's data folder: %USERPROFILE%\minRender since 0.5.8, with the
# pre-0.5.8 %LOCALAPPDATA%\MinRender as the fallback while an older app
# version is installed (same rule as the DCC plug-ins).
function Get-MinRenderDataDir {
    $new = Join-Path $env:USERPROFILE 'minRender'
    $old = Join-Path $env:LOCALAPPDATA 'MinRender'
    if ((Test-Path -LiteralPath (Join-Path $new 'config.json')) -or -not (Test-Path -LiteralPath $old)) { return $new }
    return $old
}
$appData = Get-MinRenderDataDir
$dest    = Join-Path $appData 'companion'
$script  = Join-Path $dest 'rndr-companion.ps1'
$config  = Join-Path $appData 'rndr-companion.json'

if (-not (Test-Path -LiteralPath $src)) { throw "rndr-companion.ps1 not found next to install.ps1" }
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -LiteralPath $src -Destination $script -Force
Write-Host "installed  $script"

# rndr_path is left out of a fresh config: the companion then defaults it
# to <data folder>\rn\rndrclient.exe, so it follows the data folder.
if (-not (Test-Path -LiteralPath $config)) {
    [ordered]@{
        startup_delay_seconds = 120
        cooldown_seconds      = 120
        dcc_gate              = $false
        dcc_settle_seconds    = 600
        block_processes       = @('blender.exe', 'AfterFX.exe')
    } | ConvertTo-Json | Set-Content -LiteralPath $config -Encoding ASCII
    Write-Host "wrote      $config (defaults)"
} else {
    Write-Host "kept       $config"
    # A config the 0.5.8 migration copied over may still point the client
    # into the old %LOCALAPPDATA%\MinRender. Repoint it to the same place in
    # the new folder when the client was copied there, so deleting the old
    # folder later does not break the companion.
    $oldDir = Join-Path $env:LOCALAPPDATA 'MinRender'
    $j = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
    $p = [string]$j.rndr_path
    if ($p -and $appData -ne $oldDir -and $p.StartsWith($oldDir + '\', [StringComparison]::OrdinalIgnoreCase)) {
        $moved = Join-Path $appData $p.Substring($oldDir.Length + 1)
        if (Test-Path -LiteralPath $moved) {
            $j.rndr_path = $moved
            $j | ConvertTo-Json | Set-Content -LiteralPath $config -Encoding ASCII
            Write-Host "repointed  rndr_path -> $moved"
        } else {
            Write-Host "note       rndr_path still under $oldDir (no copy at $moved); keep that folder or fix rndr_path"
        }
    }
}

$compKey = 'HKCU:\Software\MinRender\Companion'
if (-not (Test-Path -LiteralPath $compKey)) { New-Item -Path $compKey -Force | Out-Null }
New-ItemProperty -LiteralPath $compKey -Name Enabled -Value 1 -PropertyType DWord -Force | Out-Null

# Hidden launch. conhost --headless allocates no console at all; the
# -WindowStyle Hidden fallback still flashes a window at logon.
$psArgs = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$script`""
if ([Environment]::OSVersion.Version.Build -ge 22000) {
    $action = New-ScheduledTaskAction -Execute 'conhost.exe' -Argument "--headless powershell.exe $psArgs"
} else {
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-WindowStyle Hidden $psArgs"
}

# Resolve the account from the token, not USERDOMAIN/USERNAME: inside an
# SSH session those can name an account the scheduler cannot map to a SID.
$account = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

$trigger = New-ScheduledTaskTrigger -AtLogOn -User $account
$trigger.Delay = 'PT30S'

$principal = New-ScheduledTaskPrincipal -UserId $account -LogonType Interactive -RunLevel Limited

$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1)

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
Write-Host "registered task '$TaskName'"

if (-not $NoStart) {
    Start-ScheduledTask -TaskName $TaskName
    Start-Sleep -Seconds 3
    $t = Get-ScheduledTask -TaskName $TaskName
    Write-Host ("task state: {0}" -f $t.State)
    $log = Join-Path $appData 'rndr-companion.log'
    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 3 | ForEach-Object { Write-Host "  $_" } }
}
Write-Host ""
Write-Host "pause:   reg add HKCU\Software\MinRender\Companion /v Enabled /t REG_DWORD /d 0 /f"
Write-Host "resume:  reg add HKCU\Software\MinRender\Companion /v Enabled /t REG_DWORD /d 1 /f"
Write-Host "log:     $(Join-Path $appData 'rndr-companion.log')"
