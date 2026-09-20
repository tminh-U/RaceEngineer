$p = Start-Process -FilePath "build/RaceEngineer.exe" -PassThru
Start-Sleep -Seconds 3
if (!$p.HasExited) {
    Write-Host "RaceEngineer running cleanly with PID: $($p.Id)"
    Stop-Process -Id $p.Id -Force
    Write-Host "Stopped cleanly."
} else {
    Write-Host "Exited early with code: $($p.ExitCode)"
}
