<#
Collect a real Windows AC/ACC run. Does not launch/configure the app or game.
PresentMon 2.x console executable is required unless -ResourcesOnly is explicit.
Run the same replay/scenario with GameOnly, Normal, and LowResource separately.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('AC','ACC')][string]$Game,
    [Parameter(Mandatory=$true)][ValidateSet('GameOnly','Normal','LowResource')][string]$Mode,
    [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$Scenario,
    [ValidateRange(30,1800)][int]$DurationSeconds = 120,
    [ValidateRange(0,2147483647)][int]$GameProcessId = 0,
    [ValidateRange(0,2147483647)][int]$AppProcessId = 0,
    [string]$PresentMonPath = '',
    [string]$OutputDirectory = '',
    [string]$BuildLabel = '',
    [switch]$ResourcesOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-ProfileProcess {
    param([int]$ProcessId, [string[]]$Names, [string]$Role)
    if ($ProcessId -gt 0) {
        $candidate = Get-Process -Id $ProcessId -ErrorAction Stop
        if ($Names -notcontains $candidate.ProcessName) { throw "$Role PID has an unexpected executable name." }
        return $candidate
    }
    $candidates = @(Get-Process -Name $Names -ErrorAction SilentlyContinue)
    if ($candidates.Count -ne 1) { throw "Expected exactly one $Role process; found $($candidates.Count). Specify its PID if multiple instances exist." }
    return $candidates[0]
}

function Get-ProfileProcessIdentity {
    param([System.Diagnostics.Process]$Process)
    $path = $Process.MainModule.FileName
    return [ordered]@{
        pid = $Process.Id; name = $Process.ProcessName
        started_utc = $Process.StartTime.ToUniversalTime().ToString('o')
        executable = [IO.Path]::GetFileName($path)
        image_sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Get-ProfileProcessSample {
    param([System.Diagnostics.Process]$Process)
    if ($null -eq $Process) { return $null }
    $Process.Refresh()
    if ($Process.HasExited) { throw "A measured process exited before capture completed." }
    return @{
        cpu_ms = $Process.TotalProcessorTime.TotalMilliseconds
        working_set_bytes = $Process.WorkingSet64
        peak_lifetime_working_set_bytes = $Process.PeakWorkingSet64
        private_commit_bytes = $Process.PrivateMemorySize64
    }
}

function Get-ProfileMemory {
    if (-not ('RaceEngineerProfiling.NativeMemory' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace RaceEngineerProfiling {
  public struct MemorySample {
    public ulong TotalPhysical, AvailablePhysical, CommitBytes, CommitLimit;
  }
  public static class NativeMemory {
    [StructLayout(LayoutKind.Sequential)] struct PerformanceInfo {
      public uint cb;
      public UIntPtr CommitTotal, CommitLimit, CommitPeak, PhysicalTotal, PhysicalAvailable;
      public UIntPtr SystemCache, KernelTotal, KernelPaged, KernelNonpaged, PageSize;
      public uint HandleCount, ProcessCount, ThreadCount;
    }
    [DllImport("psapi.dll", SetLastError=true)]
    static extern bool GetPerformanceInfo(out PerformanceInfo info, uint size);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetPhysicallyInstalledSystemMemory(out ulong kib);
    public static ulong InstalledBytes() {
      ulong kib;
      if (!GetPhysicallyInstalledSystemMemory(out kib)) throw new System.ComponentModel.Win32Exception();
      return checked(kib * 1024UL);
    }
    public static MemorySample Read() {
      PerformanceInfo p;
      if (!GetPerformanceInfo(out p, (uint)Marshal.SizeOf(typeof(PerformanceInfo))))
        throw new System.ComponentModel.Win32Exception();
      ulong page = p.PageSize.ToUInt64();
      return new MemorySample { TotalPhysical=p.PhysicalTotal.ToUInt64()*page,
        AvailablePhysical=p.PhysicalAvailable.ToUInt64()*page,
        CommitBytes=p.CommitTotal.ToUInt64()*page, CommitLimit=p.CommitLimit.ToUInt64()*page };
    }
  }
}
'@
    }
    return [RaceEngineerProfiling.NativeMemory]::Read()
}

function ConvertTo-ProfileNumber {
    param([double]$Value)
    return $Value.ToString('F6', [Globalization.CultureInfo]::InvariantCulture)
}

function Write-ProfileManifest {
    param([string]$Path, $Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 8
    $temporary = $Path + '.tmp'
    [IO.File]::WriteAllText($temporary, $json, (New-Object Text.UTF8Encoding($false)))
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This collector requires a real Windows machine running AC or ACC. It cannot validate a Linux simulation as game performance.'
}
if ([IntPtr]::Size -ne 8) { throw 'Run this collector with 64-bit Windows PowerShell.' }
if (-not [Diagnostics.Stopwatch]::IsHighResolution) { throw 'A QPC-backed Stopwatch is required to align PresentMon frames.' }
if (-not $ResourcesOnly -and [string]::IsNullOrWhiteSpace($PresentMonPath)) {
    throw 'Pass -PresentMonPath pointing to the official PresentMon 2.x console executable, or explicitly choose -ResourcesOnly (no frametime measurement).'
}
if (-not $ResourcesOnly) { $PresentMonPath = (Resolve-Path -LiteralPath $PresentMonPath).Path }
$gameNames = if ($Game -eq 'AC') { @('acs') } else { @('AC2-Win64-Shipping') }
$gameProcess = Get-ProfileProcess $GameProcessId $gameNames 'game'
$appProcess = $null
if ($Mode -eq 'GameOnly') {
    if (@(Get-Process -Name RaceEngineer -ErrorAction SilentlyContinue).Count -gt 0) {
        throw 'Close RaceEngineer before a GameOnly control run.'
    }
} else {
    $appProcess = Get-ProfileProcess $AppProcessId @('RaceEngineer') 'RaceEngineer'
}
$memory = Get-ProfileMemory
$machine = [ordered]@{
    logical_processors = [Environment]::ProcessorCount
    installed_physical_bytes = [RaceEngineerProfiling.NativeMemory]::InstalledBytes()
    usable_physical_bytes = $memory.TotalPhysical
    os_version = [Environment]::OSVersion.VersionString
    cpu = @((Get-CimInstance Win32_Processor).Name)
    gpu = @(Get-CimInstance Win32_VideoController | ForEach-Object {
        [ordered]@{ name=$_.Name; driver=$_.DriverVersion }
    })
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path (Get-Location).Path ('race-profile-' + $Game.ToLowerInvariant() + '-' + $Mode.ToLowerInvariant() + '-' + [Guid]::NewGuid().ToString('N'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory already exists; choose a new directory to avoid mixing captures.' }
[void](New-Item -ItemType Directory -Path $OutputDirectory)
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$manifestPath = Join-Path $OutputDirectory 'manifest.json'
$framesPath = Join-Path $OutputDirectory 'frames.csv'
$manifest = [ordered]@{
    schema = 1; kind = 'windows_live_capture'; status = 'running'
    game = $Game.ToLowerInvariant(); mode = $Mode; scenario = $Scenario; build_label = $BuildLabel
    profile_mode_is_user_declared = $true
    duration_requested_s = $DurationSeconds; actual_duration_s = 0
    clock_frequency = [Diagnostics.Stopwatch]::Frequency
    capture_start_qpc = 0; capture_end_qpc = 0
    machine = $machine; game_process = (Get-ProfileProcessIdentity $gameProcess)
    app_process = $(if ($null -ne $appProcess) { Get-ProfileProcessIdentity $appProcess } else { $null })
    files = @{resources='resources.csv'; frames=$(if ($ResourcesOnly) { $null } else { 'frames.csv' })}
    presentmon = $null; error = $null
    collector = @{power_shell=$PSVersionTable.PSVersion.ToString(); interval_ms=1000; process_id=$PID}
}
$writer = $null; $presentMon = $null; $failure = $null; $sampleCount = 0
try {
    $writer = New-Object IO.StreamWriter((Join-Path $OutputDirectory 'resources.csv'), $false, (New-Object Text.UTF8Encoding($false)))
    $writer.AutoFlush = $true
    if (-not $ResourcesOnly) {
        $sessionName = 'RaceEngineerProfile-' + [Guid]::NewGuid().ToString('N')
        $manifest.presentmon = @{ image_sha256=(Get-FileHash -LiteralPath $PresentMonPath -Algorithm SHA256).Hash.ToLowerInvariant(); exit_code=$null; clock='qpc_ticks'; metrics='v1'; session_name=$sessionName; cleanup_error=$null }
        $presentMonArgs = @('--process_id', [string]$gameProcess.Id, '--output_file', ('"' + $framesPath + '"'),
            '--v1_metrics', '--qpc_time', '--timed', [string]$DurationSeconds,
            '--terminate_after_timed', '--terminate_on_proc_exit', '--no_console_stats',
            '--session_name', $sessionName)
        $presentMon = Start-Process -FilePath $PresentMonPath -ArgumentList $presentMonArgs -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $OutputDirectory 'presentmon.stdout.txt') `
            -RedirectStandardError (Join-Path $OutputDirectory 'presentmon.stderr.txt')
    }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $manifest.capture_start_qpc = [Diagnostics.Stopwatch]::GetTimestamp()
    Write-ProfileManifest $manifestPath $manifest
    $collectorProcess = Get-Process -Id $PID
    while ($true) {
        if ($Mode -eq 'GameOnly' -and @(Get-Process -Name RaceEngineer -ErrorAction SilentlyContinue).Count -gt 0) {
            throw 'RaceEngineer started during a GameOnly control run.'
        }
        $elapsed = $watch.Elapsed.TotalSeconds
        $a = Get-ProfileProcessSample $appProcess
        $g = Get-ProfileProcessSample $gameProcess
        $c = Get-ProfileProcessSample $collectorProcess
        $m = Get-ProfileMemory
        $row = [ordered]@{
            elapsed_s = (ConvertTo-ProfileNumber $elapsed)
            app_cpu_ms = $(if ($null -eq $a) { '' } else { ConvertTo-ProfileNumber $a.cpu_ms })
            app_working_set_bytes = $(if ($null -eq $a) { '' } else { $a.working_set_bytes })
            app_private_commit_bytes = $(if ($null -eq $a) { '' } else { $a.private_commit_bytes })
            app_peak_lifetime_working_set_bytes = $(if ($null -eq $a) { '' } else { $a.peak_lifetime_working_set_bytes })
            game_cpu_ms = (ConvertTo-ProfileNumber $g.cpu_ms)
            game_working_set_bytes = $g.working_set_bytes; game_private_commit_bytes = $g.private_commit_bytes
            game_peak_lifetime_working_set_bytes = $g.peak_lifetime_working_set_bytes
            collector_cpu_ms = (ConvertTo-ProfileNumber $c.cpu_ms)
            collector_working_set_bytes = $c.working_set_bytes
            system_total_physical_bytes = $m.TotalPhysical; system_available_physical_bytes = $m.AvailablePhysical
            system_commit_bytes = $m.CommitBytes; system_commit_limit_bytes = $m.CommitLimit
        }
        $csv = @([pscustomobject]$row | ConvertTo-Csv -NoTypeInformation)
        if ($sampleCount -eq 0) { $writer.WriteLine($csv[0]) }
        $writer.WriteLine($csv[1]); ++$sampleCount
        if ($watch.Elapsed.TotalSeconds -ge $DurationSeconds) { break }
        $dueMs = [Math]::Min($DurationSeconds * 1000., $sampleCount * 1000.)
        $delayMs = [int][Math]::Max(1., $dueMs - $watch.Elapsed.TotalMilliseconds)
        [Threading.Thread]::Sleep($delayMs)
    }
    $manifest.capture_end_qpc = [Diagnostics.Stopwatch]::GetTimestamp()
    $manifest.actual_duration_s = $watch.Elapsed.TotalSeconds
    if ($null -ne $presentMon) {
        if (-not $presentMon.WaitForExit(15000)) { throw 'PresentMon did not finish its timed capture.' }
        $manifest.presentmon.exit_code = $presentMon.ExitCode
        if ($presentMon.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $framesPath)) {
            throw 'PresentMon failed or produced no CSV. See its stderr file; ETW access may require an elevated Windows terminal.'
        }
    }
    $manifest.status = $(if ($ResourcesOnly) { 'resources_only' } else { 'complete' })
} catch {
    $failure = $_
    $manifest.status = 'failed'; $manifest.error = $_.Exception.Message
} finally {
    if ($null -ne $writer) { $writer.Dispose() }
    if ($null -ne $presentMon -and $manifest.status -ne 'complete') {
        try {
            if (-not $presentMon.HasExited) { $presentMon.Kill(); [void]$presentMon.WaitForExit(5000) }
            # A killed collector can leave ETW active. Stop only this capture's unique session.
            $cleanup = Start-Process -FilePath $PresentMonPath -PassThru -WindowStyle Hidden `
                -ArgumentList @('--session_name', $manifest.presentmon.session_name, '--terminate_existing_session', '--no_console_stats') `
                -RedirectStandardOutput (Join-Path $OutputDirectory 'presentmon.cleanup.stdout.txt') `
                -RedirectStandardError (Join-Path $OutputDirectory 'presentmon.cleanup.stderr.txt')
            if (-not $cleanup.WaitForExit(5000)) {
                $cleanup.Kill()
                throw 'PresentMon session cleanup timed out.'
            }
            $manifest.presentmon['cleanup_exit_code'] = $cleanup.ExitCode
        } catch { $manifest.presentmon.cleanup_error = $_.Exception.Message }
    }
    $manifest['sample_count'] = $sampleCount
    Write-ProfileManifest $manifestPath $manifest
}
if ($null -ne $failure) { throw $failure }
Write-Host "Captured $Game / $Mode into $OutputDirectory. The analyzer must validate frame coverage before interpreting frametime."
