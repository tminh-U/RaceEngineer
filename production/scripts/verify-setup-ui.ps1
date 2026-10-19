[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,
    [Parameter(Mandatory = $true)]
    [string]$LogPrefix
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$executable = (Resolve-Path $ExecutablePath).Path
$outputDirectory = Join-Path $projectRoot 'build'
New-Item $outputDirectory -ItemType Directory -Force | Out-Null
$stdout = Join-Path $outputDirectory "$LogPrefix-setup-stdout.txt"
$stderr = Join-Path $outputDirectory "$LogPrefix-setup-stderr.txt"
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_FORCE_STDERR_LOGGING = '1'
$check = Start-Process $executable -ArgumentList '--verify-setup-ui' -WorkingDirectory $projectRoot -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$null = $check.Handle
$passed = $false
$cleanup = 'exited'
try {
    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline) {
        $passed = [bool](Select-String -Path $stdout -SimpleMatch 'Setup UI 880x576: passed' -Quiet -ErrorAction SilentlyContinue)
        if ($passed -or $check.HasExited) { break }
        Start-Sleep -Milliseconds 250
        $check.Refresh()
    }
    if (-not $passed) { throw 'Native setup UI assertions did not pass (process exited or 90-second timeout).' }
    # The UI assertions finish before uninterruptible native model warmup/shutdown.
    if (-not $check.WaitForExit(15000)) {
        $cleanup = 'terminated after passed UI assertions; model warmup/shutdown not verified'
        Stop-Process -Id $check.Id -Force
    } elseif ($check.ExitCode -ne 0) {
        throw "Native setup process crashed after its UI assertions: $($check.ExitCode)"
    }
    [ordered]@{
        executable = $executable
        sha256 = (Get-FileHash $executable -Algorithm SHA256).Hash.ToLowerInvariant()
        ui_assertions_passed = $passed
        cleanup = $cleanup
        exit_code = if ($cleanup -eq 'exited') { $check.ExitCode } else { $null }
    } | ConvertTo-Json | Set-Content (Join-Path $outputDirectory "$LogPrefix-setup-result.json")
    Write-Host "Native setup UI passed; cleanup: $cleanup"
} finally {
    if (-not $check.HasExited) { Stop-Process -Id $check.Id -Force -ErrorAction SilentlyContinue }
    Get-Content $stdout, $stderr
    $check.Dispose()
}
