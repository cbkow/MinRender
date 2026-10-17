<#
.SYNOPSIS
  External RNDR supervisor for a minRender node (Windows only).

.DESCRIPTION
  Runs the OTOY Render Network client (rndrclient.exe -> TCPSVCS.EXE) while
  the local minRender node is idle and stops it the moment a render starts,
  then relaunches it after a cooldown. This is the state machine that lived
  inside minRender before 0.5.6, moved out of the app so the shipped binary
  contains none of it.

  It never talks to the farm. It reads two facts minRender publishes for
  same-machine tooling:
    HKCU\Software\MinRender\Node   RenderState / NodeState / Pid / UpdatedMs
  and blocks on a registry change notification instead of polling. For an
  app version that predates the key it falls back to polling
  GET http://127.0.0.1:<http_port>/api/status once a second.

  Operator controls live under HKCU\Software\MinRender\Companion:
    Enabled (DWORD)  0 = stop RNDR and stay idle; 1 = supervise. Checked on
                     every wake (at most 5 s latency).
    State, Rndr, LastSeen are written by this script for observability.

  Configuration: %LOCALAPPDATA%\MinRender\rndr-companion.json (see README).
  Log:           %LOCALAPPDATA%\MinRender\rndr-companion.log

  Policy when minRender is unreachable (not running, or the Pid in the key
  is dead and the HTTP fallback fails): hold steady. Nothing is launched and
  a running client is left alone. When the app comes back the startup
  settle applies before any launch, exactly as on a fresh start.
#>
[CmdletBinding()]
param(
    # Run the state machine once and print the decision, then exit.
    # Handy for checking config and reachability without the task.
    [switch]$Once
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

# ---------------------------------------------------------------- paths ---
$AppData      = Join-Path $env:LOCALAPPDATA 'MinRender'
$ConfigPath   = Join-Path $AppData 'rndr-companion.json'
$LogPath      = Join-Path $AppData 'rndr-companion.log'
$NodeKeyPath  = 'HKCU:\Software\MinRender\Node'
$CompKeyPath  = 'HKCU:\Software\MinRender\Companion'
$NodeSubKey   = 'Software\MinRender\Node'

# ------------------------------------------------------------- defaults ---
# Mirrors the constants of the retired in-process supervisor.
$Defaults = [ordered]@{
    rndr_path             = (Join-Path $AppData 'rn\rndrclient.exe')
    process_name          = 'TCPSVCS'      # what rndrclient.exe spawns
    startup_delay_seconds = 120            # let dispatch claim the GPU first
    warmup_seconds        = 15             # no watchdog right after a launch
    watchdog_seconds      = 5
    hang_grace_seconds    = 15
    cooldown_seconds      = 120            # after a render, before relaunch
    dcc_gate              = $false
    dcc_poll_seconds      = 5
    dcc_settle_seconds    = 600            # 10 min after a creative app closes
    block_processes       = @('blender.exe', 'AfterFX.exe')
    http_port             = 0              # 0 = read from config.json
    fallback_poll_seconds = 1              # only used without the registry key
}

# --------------------------------------------------------------- native ---
Add-Type -Namespace MR -Name Native -MemberDefinition @'
[DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
public static extern int RegOpenKeyExW(IntPtr hKey, string subKey, int options, int samDesired, out IntPtr phkResult);
[DllImport("advapi32.dll")]
public static extern int RegNotifyChangeKeyValue(IntPtr hKey, bool watchSubtree, int notifyFilter, IntPtr hEvent, bool asynchronous);
[DllImport("advapi32.dll")]
public static extern int RegCloseKey(IntPtr hKey);
[DllImport("kernel32.dll", SetLastError = true)]
public static extern IntPtr CreateEventW(IntPtr attrs, bool manualReset, bool initialState, string name);
[DllImport("kernel32.dll")]
public static extern int WaitForSingleObject(IntPtr handle, int milliseconds);
[DllImport("kernel32.dll")]
public static extern bool ResetEvent(IntPtr handle);
[DllImport("kernel32.dll")]
public static extern bool CloseHandle(IntPtr handle);
'@
$HKEY_CURRENT_USER = [IntPtr]::new(-2147483647)   # 0x80000001 sign-extended
$KEY_NOTIFY        = 0x0010
$KEY_READ          = 0x20019
$REG_NOTIFY_CHANGE_LAST_SET = 0x4
$WAIT_OBJECT_0     = 0
$WAIT_TIMEOUT      = 258

# ------------------------------------------------------------------ log ---
function Write-Log {
    param([string]$Message)
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Message
    try { Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8 } catch { }
    if ($Once -or $Host.Name -ne 'ConsoleHost') { Write-Host $line }
}

function Initialize-Log {
    try {
        if ((Test-Path -LiteralPath $LogPath) -and (Get-Item -LiteralPath $LogPath).Length -gt 2MB) {
            Remove-Item -LiteralPath $LogPath -Force
        }
    } catch { }
}

# --------------------------------------------------------------- config ---
function Read-Config {
    $cfg = [ordered]@{}
    foreach ($k in $Defaults.Keys) { $cfg[$k] = $Defaults[$k] }
    if (Test-Path -LiteralPath $ConfigPath) {
        try {
            $user = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
            foreach ($p in $user.PSObject.Properties) {
                if ($cfg.Contains($p.Name)) { $cfg[$p.Name] = $p.Value }
                else { Write-Log "config: ignoring unknown key '$($p.Name)'" }
            }
        } catch {
            Write-Log "config: failed to parse $ConfigPath ($($_.Exception.Message)); using defaults"
        }
    }
    if ([int]$cfg.http_port -le 0) {
        $cfg.http_port = 8420
        $appCfg = Join-Path $AppData 'config.json'
        if (Test-Path -LiteralPath $appCfg) {
            try {
                $j = Get-Content -LiteralPath $appCfg -Raw | ConvertFrom-Json
                if ($j.http_port) { $cfg.http_port = [int]$j.http_port }
            } catch { }
        }
    }
    # Normalise the block list to bare lower-case image names.
    $cfg.block_processes = @($cfg.block_processes | ForEach-Object { ([string]$_).ToLowerInvariant() })
    return $cfg
}

# ----------------------------------------------------- companion status ---
function Set-CompanionValue {
    param([string]$Name, $Value, [string]$Type = 'String')
    try {
        if (-not (Test-Path -LiteralPath $CompKeyPath)) { New-Item -Path $CompKeyPath -Force | Out-Null }
        New-ItemProperty -LiteralPath $CompKeyPath -Name $Name -Value $Value -PropertyType $Type -Force | Out-Null
    } catch { }
}

function Get-Enabled {
    try {
        $v = (Get-ItemProperty -LiteralPath $CompKeyPath -Name Enabled -ErrorAction Stop).Enabled
        return ([int]$v -ne 0)
    } catch { return $true }   # no key = enabled
}

# ------------------------------------------------------------ node state ---
# Returns @{ Reachable; Rendering; Stopped; Source }.
function Get-NodeState {
    param($Cfg)
    $k = $null
    try { $k = Get-ItemProperty -LiteralPath $NodeKeyPath -ErrorAction Stop } catch { }
    if ($k -and $k.PSObject.Properties['Pid']) {
        $pid_ = [int]$k.Pid
        $alive = $false
        if ($pid_ -ne 0) {
            $alive = $null -ne (Get-Process -Id $pid_ -ErrorAction SilentlyContinue)
        }
        if ($alive) {
            return @{
                Reachable = $true
                Rendering = ([string]$k.RenderState -eq 'rendering')
                Stopped   = ([string]$k.NodeState -eq 'stopped')
                Source    = 'registry'
            }
        }
        # Pid 0 = clean exit; dead Pid = crash. Either way the app is gone
        # unless an older build without the key is what is running, which
        # the HTTP fallback below detects.
    }
    try {
        $uri = 'http://127.0.0.1:{0}/api/status' -f [int]$Cfg.http_port
        $st = Invoke-RestMethod -Uri $uri -TimeoutSec 1 -ErrorAction Stop
        return @{
            Reachable = $true
            Rendering = ([string]$st.render_state -eq 'rendering')
            Stopped   = ([string]$st.node_state -eq 'stopped')
            Source    = 'http'
        }
    } catch {
        return @{ Reachable = $false; Rendering = $false; Stopped = $false; Source = 'none' }
    }
}

# ---------------------------------------------------------- RNDR client ---
function Get-RndrProcesses { param($Cfg) @(Get-Process -Name $Cfg.process_name -ErrorAction SilentlyContinue) }

function Test-RndrRunning { param($Cfg) @(Get-RndrProcesses $Cfg).Count -gt 0 }

# Mirrors the old .Responding check: only processes that own a visible
# window can be judged; a headless client is assumed responsive.
function Test-RndrResponsive {
    param($Cfg)
    $procs = @(Get-RndrProcesses $Cfg)
    $windowed = @($procs | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero })
    if ($windowed.Count -eq 0) { return $true }
    foreach ($p in $windowed) { if (-not $p.Responding) { return $false } }
    return $true
}

function Start-Rndr {
    param($Cfg)
    if (Test-RndrRunning $Cfg) { Write-Log 'launch: client already running, adopting'; return $true }
    if (-not (Test-Path -LiteralPath $Cfg.rndr_path)) {
        Write-Log "launch: client not found at $($Cfg.rndr_path)"
        return $false
    }
    try {
        # rndrclient.exe is a launcher: it spawns TCPSVCS and exits.
        Start-Process -FilePath $Cfg.rndr_path -WorkingDirectory (Split-Path $Cfg.rndr_path) -WindowStyle Hidden | Out-Null
        Write-Log 'launch: RNDR client launched'
        return $true
    } catch {
        Write-Log "launch: failed ($($_.Exception.Message))"
        return $false
    }
}

function Stop-Rndr {
    param($Cfg, [string]$Why)
    $procs = @(Get-RndrProcesses $Cfg)
    if ($procs.Count -eq 0) { return }
    try { $procs | Stop-Process -Force -ErrorAction Stop } catch { }
    Write-Log "kill: stopped $($procs.Count) $($Cfg.process_name) process(es) - $Why"
}

# -------------------------------------------------------------- DCC gate ---
# Exact image-name match, and only when the image lives under a Program
# Files root (an artist's install, not a per-user helper).
function Find-CreativeApp {
    param($Cfg)
    if (-not $Cfg.dcc_gate -or @($Cfg.block_processes).Count -eq 0) { return $null }
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) |
        Where-Object { $_ } | ForEach-Object { ($_.TrimEnd('\') + '\').ToLowerInvariant() }
    foreach ($p in Get-Process -ErrorAction SilentlyContinue) {
        $img = ($p.Name + '.exe').ToLowerInvariant()
        if ($Cfg.block_processes -notcontains $img) { continue }
        $path = $null
        try { $path = $p.Path } catch { }
        if (-not $path) { continue }    # another user's process: cannot verify
        $lp = $path.ToLowerInvariant()
        foreach ($r in $roots) { if ($lp.StartsWith($r)) { return $p.Name + '.exe' } }
    }
    return $null
}

# -------------------------------------------------------- registry wait ---
# Opens the Node key for notification. Returns $null if the key does not
# exist yet (app never ran on this machine); the caller retries each tick.
function Open-NodeKey {
    $h = [IntPtr]::Zero
    $rc = [MR.Native]::RegOpenKeyExW($HKEY_CURRENT_USER, $NodeSubKey, 0, ($KEY_READ -bor $KEY_NOTIFY), [ref]$h)
    if ($rc -ne 0) { return $null }
    return $h
}

function Arm-Notify {
    param([IntPtr]$Key, [IntPtr]$Event)
    [MR.Native]::ResetEvent($Event) | Out-Null
    $rc = [MR.Native]::RegNotifyChangeKeyValue($Key, $false, $REG_NOTIFY_CHANGE_LAST_SET, $Event, $true)
    return ($rc -eq 0)
}

# ------------------------------------------------------------------ main ---
Initialize-Log
$mutex = New-Object System.Threading.Mutex($false, 'Local\MinRenderRndrCompanion')
if (-not $mutex.WaitOne(0)) { Write-Log 'another companion instance is running; exiting'; exit 0 }

$cfg = Read-Config
Write-Log ("starting: client={0} port={1} startup={2}s cooldown={3}s dcc_gate={4}" -f
    $cfg.rndr_path, $cfg.http_port, $cfg.startup_delay_seconds, $cfg.cooldown_seconds, $cfg.dcc_gate)
if (-not (Test-Path -LiteralPath $cfg.rndr_path)) {
    Write-Log 'client binary absent; will keep waiting in case it is installed later'
}

$state          = 'Inactive'
$now            = Get-Date
$reachableSince = $null          # when the app was last seen coming up
$lastReachable  = $null
$launchTime     = $now
$cooldownStart  = $now
$lastWatchdog   = $now
$hangSince      = $null
$lastDccPoll    = $now.AddDays(-1)
$dccPresent     = $false
$dccName        = ''
$lastOccupied   = $now           # unknown history: serve a full settle first
$lastEnabled    = $true
$lastSource     = ''
$lastState      = ''

# Task Scheduler stops a task by terminating the process it launched. With
# the conhost --headless launcher that is conhost, not this PowerShell, so
# watch the parent and leave when it is gone; otherwise a restarted task
# finds the mutex held by an orphan running stale config.
$parentPid = 0
try { $parentPid = [int](Get-CimInstance Win32_Process -Filter "ProcessId=$PID").ParentProcessId } catch { }
function Test-ParentAlive {
    if ($script:parentPid -le 0) { return $true }
    return $null -ne (Get-Process -Id $script:parentPid -ErrorAction SilentlyContinue)
}

$event = [MR.Native]::CreateEventW([IntPtr]::Zero, $true, $false, $null)
$key   = Open-NodeKey
if ($key) { Arm-Notify $key $event | Out-Null } else { Write-Log 'node key absent; polling until minRender publishes it' }

function Set-State {
    param([string]$New, [string]$Why)
    if ($New -ne $script:state) {
        Write-Log ("state: {0} -> {1} ({2})" -f $script:state, $New, $Why)
        $script:state = $New
        Set-CompanionValue -Name State -Value $New
    }
}

try {
    while ($true) {
        if (-not (Test-ParentAlive)) { Write-Log "parent process $parentPid gone (task stopped); exiting, client left as is"; break }
        $now = Get-Date
        $enabled = Get-Enabled
        $ns = Get-NodeState $cfg

        # --- liveness bookkeeping -------------------------------------------
        if ($ns.Reachable -and -not $lastReachable) {
            $reachableSince = $now
            Write-Log ("minRender reachable via {0}; startup settle {1}s" -f $ns.Source, $cfg.startup_delay_seconds)
        } elseif (-not $ns.Reachable -and $lastReachable) {
            Write-Log 'minRender unreachable; holding steady (no launches)'
        }
        $lastReachable = $ns.Reachable
        if ($ns.Source -ne $lastSource) { $lastSource = $ns.Source }

        if ($enabled -ne $lastEnabled) {
            Write-Log ("Enabled -> {0}" -f [int]$enabled)
            $lastEnabled = $enabled
        }

        # --- DCC gate ----------------------------------------------------------
        if ($cfg.dcc_gate -and ($now - $lastDccPoll).TotalSeconds -ge $cfg.dcc_poll_seconds) {
            $lastDccPoll = $now
            $found = Find-CreativeApp $cfg
            if ($found) {
                $lastOccupied = $now
                if (-not $dccPresent -or $dccName -ne $found) { Write-Log "dcc: detected $found" }
                $dccName = $found
            } elseif ($dccPresent) {
                Write-Log ("dcc: {0} gone; settling {1}s" -f $dccName, $cfg.dcc_settle_seconds)
                $dccName = ''
            }
            $dccPresent = [bool]$found
        }
        $dccBlocked = $cfg.dcc_gate -and (($now - $lastOccupied).TotalSeconds -lt $cfg.dcc_settle_seconds)

        # --- state machine -------------------------------------------------
        if (-not $enabled) {
            if ($state -ne 'Inactive') {
                Stop-Rndr $cfg 'disabled by operator'
                Set-State 'Inactive' 'disabled'
            }
        } elseif (-not $ns.Reachable) {
            # Hold steady. A running client stays; nothing new is launched.
        } else {
            $rendering = $ns.Rendering
            switch ($state) {
                'Inactive' {
                    if ($rendering) {
                        Set-State 'RenderActive' 'render in progress'
                    } elseif (($now - $reachableSince).TotalSeconds -lt $cfg.startup_delay_seconds) {
                        # startup settle
                    } elseif ($dccBlocked) {
                        # artist present or settling
                    } elseif (Test-RndrRunning $cfg) {
                        Write-Log 'adopting running client'
                        $launchTime = $now; $hangSince = $null
                        Set-State 'Running' 'adopted'
                    } elseif (Start-Rndr $cfg) {
                        $launchTime = $now; $hangSince = $null
                        Set-State 'Running' 'launched'
                    }
                }
                'Running' {
                    if ($rendering) {
                        Stop-Rndr $cfg 'render started'
                        Set-State 'RenderActive' 'render started'
                    } elseif ($dccPresent) {
                        Stop-Rndr $cfg "creative app present ($dccName)"
                        Set-State 'Inactive' 'creative app'
                    } elseif (($now - $lastWatchdog).TotalSeconds -ge $cfg.watchdog_seconds) {
                        $lastWatchdog = $now
                        if (($now - $launchTime).TotalSeconds -ge $cfg.warmup_seconds) {
                            if (-not (Test-RndrRunning $cfg)) {
                                Write-Log 'watchdog: client gone, relaunching'
                                if (Start-Rndr $cfg) { $launchTime = $now }
                                $hangSince = $null
                            } elseif (-not (Test-RndrResponsive $cfg)) {
                                if (-not $hangSince) { $hangSince = $now; Write-Log 'watchdog: client not responding, grace period' }
                                elseif (($now - $hangSince).TotalSeconds -ge $cfg.hang_grace_seconds) {
                                    Stop-Rndr $cfg 'hung'
                                    if (Start-Rndr $cfg) { $launchTime = $now }
                                    $hangSince = $null
                                }
                            } elseif ($hangSince) {
                                Write-Log 'watchdog: client recovered'; $hangSince = $null
                            }
                        }
                    }
                }
                'RenderActive' {
                    if (-not $rendering) {
                        $cooldownStart = $now
                        Set-State 'Cooldown' ("render finished; {0}s cooldown" -f $cfg.cooldown_seconds)
                    }
                }
                'Cooldown' {
                    if ($rendering) {
                        Set-State 'RenderActive' 'new render'
                    } elseif ($dccBlocked) {
                        Set-State 'Inactive' 'creative app / settling'
                    } elseif (($now - $cooldownStart).TotalSeconds -ge $cfg.cooldown_seconds) {
                        if (Test-RndrRunning $cfg) { $launchTime = $now; Set-State 'Running' 'adopted after cooldown' }
                        elseif (Start-Rndr $cfg)   { $launchTime = $now; Set-State 'Running' 'relaunched after cooldown' }
                    }
                }
            }
        }

        Set-CompanionValue -Name LastSeen -Value ([int64]([DateTimeOffset]$now).ToUnixTimeMilliseconds()) -Type QWord
        Set-CompanionValue -Name Rndr -Value $(if (Test-RndrRunning $cfg) { 'running' } else { 'absent' })
        if ($state -ne $lastState) { $lastState = $state; Set-CompanionValue -Name State -Value $state }

        if ($Once) { Write-Log ("once: reachable={0} source={1} rendering={2} state={3} enabled={4}" -f $ns.Reachable, $ns.Source, $ns.Rendering, $state, [int]$enabled); break }

        # --- wait: registry notification or watchdog tick --------------------
        if (-not $key) {
            $key = Open-NodeKey
            if ($key) { Write-Log 'node key appeared; switching to notifications'; Arm-Notify $key $event | Out-Null }
        }
        $timeoutMs = if ($key -and $ns.Source -ne 'http') { [int]$cfg.watchdog_seconds * 1000 } else { [int]$cfg.fallback_poll_seconds * 1000 }
        $rc = [MR.Native]::WaitForSingleObject($event, $timeoutMs)
        if ($rc -eq $WAIT_OBJECT_0 -and $key) {
            # Re-arm first: the app writes several values per transition and
            # each one signals; the state machine above dedupes by re-reading.
            if (-not (Arm-Notify $key $event)) {
                # Key deleted or handle invalid: reopen next loop.
                [MR.Native]::RegCloseKey($key) | Out-Null; $key = $null
            }
        }
    }
} finally {
    if ($key) { [MR.Native]::RegCloseKey($key) | Out-Null }
    if ($event -ne [IntPtr]::Zero) { [MR.Native]::CloseHandle($event) | Out-Null }
    $mutex.ReleaseMutex() | Out-Null
    Write-Log 'exiting'
}
