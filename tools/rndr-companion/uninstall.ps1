<#
.SYNOPSIS
  Remove the minRender RNDR companion task.

.DESCRIPTION
  Disables the companion first (Enabled = 0) and waits one tick so it stops
  the RNDR client itself - Stop-ScheduledTask kills PowerShell outright and
  would leave TCPSVCS running - then stops and unregisters the task and
  removes the installed script. The config and log are kept.
#>
[CmdletBinding()]
param([string]$TaskName = 'minRender RNDR companion')
$ErrorActionPreference = 'Continue'

$compKey = 'HKCU:\Software\MinRender\Companion'
if (Test-Path -LiteralPath $compKey) {
    New-ItemProperty -LiteralPath $compKey -Name Enabled -Value 0 -PropertyType DWord -Force | Out-Null
    Write-Host 'disabled companion; waiting for it to stop the client'
    Start-Sleep -Seconds 7
}
$t = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($t) {
    if ($t.State -eq 'Running') { Stop-ScheduledTask -TaskName $TaskName }
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Host "removed task '$TaskName'"
} else {
    Write-Host "task '$TaskName' not registered"
}
# Belt and braces: a companion orphaned by an older task definition.
Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
    Where-Object { $_.CommandLine -like '*rndr-companion.ps1*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue; Write-Host "stopped stray companion pid $($_.ProcessId)" }
# Both the current and the pre-0.5.8 data folder, whichever holds a copy.
foreach ($base in @((Join-Path $env:USERPROFILE 'minRender'), (Join-Path $env:LOCALAPPDATA 'MinRender'))) {
    $d = Join-Path $base 'companion'
    if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force; Write-Host "removed $d" }
}
$dest = Join-Path (Join-Path $env:USERPROFILE 'minRender') 'companion'
if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force; Write-Host "removed $dest" }
if (Test-Path -LiteralPath $compKey) { Remove-Item -LiteralPath $compKey -Recurse -Force }
Write-Host 'config and log left in %USERPROFILE%\minRender'
