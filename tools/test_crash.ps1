$proc = Start-Process -FilePath "build\RaceEngineer.exe" -PassThru -RedirectStandardOutput "crash_out.log" -RedirectStandardError "crash_err.log"
Write-Host "Started PID $($proc.Id), waiting up to 45 seconds..."
$exited = $proc.WaitForExit(45000)
if ($exited) {
    Write-Host "CRASHED! Process exited with code: $($proc.ExitCode)"
    Write-Host "--- STDOUT ---"
    Get-Content "crash_out.log" -ErrorAction SilentlyContinue
    Write-Host "--- STDERR ---"
    Get-Content "crash_err.log" -ErrorAction SilentlyContinue
} else {
    Write-Host "Still running after 45 seconds! PID: $($proc.Id)"
    Stop-Process -Id $proc.Id -Force
}
