param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$downloadDirectory = Join-Path $ProjectRoot ".downloads\runtime"
$voiceDirectory = Join-Path $ProjectRoot "voices\vieneu"
$whisperDirectory = Join-Path $ProjectRoot "models"

New-Item -ItemType Directory -Force -Path $downloadDirectory, $voiceDirectory, $whisperDirectory | Out-Null

function Get-VerifiedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$OutFile,
        [Parameter(Mandatory = $true)][string]$Sha256
    )

    if (Test-Path -LiteralPath $OutFile) {
        $existingHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutFile).Hash.ToLowerInvariant()
        if ($existingHash -eq $Sha256) {
            Write-Host "Already verified: $OutFile"
            return
        }
    }

    $partialFile = "$OutFile.part"
    curl.exe -L --fail --retry 3 $Uri -o $partialFile
    if ($LASTEXITCODE -ne 0) {
        throw "Download failed: $Uri"
    }
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $partialFile).Hash.ToLowerInvariant()
    if ($actualHash -ne $Sha256) {
        Remove-Item -LiteralPath $partialFile -Force
        throw "SHA256 mismatch for $OutFile (expected $Sha256, got $actualHash)"
    }
    Move-Item -LiteralPath $partialFile -Destination $OutFile -Force
}


& (Join-Path $PSScriptRoot "convert_phowhisper_model.ps1") -ProjectRoot $ProjectRoot

# Setup native VieNeu-TTS v3 Turbo models
& (Join-Path $PSScriptRoot "setup_vieneu_models.ps1")

Write-Host "PhoWhisper-small Q5_1 and native VieNeu-TTS assets are ready. Rebuild RaceEngineer to copy them beside the executable."
