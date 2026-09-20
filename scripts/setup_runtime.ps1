param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$downloadDirectory = Join-Path $ProjectRoot ".downloads\runtime"
$voiceDirectory = Join-Path $ProjectRoot "voices\vieneu"
$whisperDirectory = Join-Path $ProjectRoot "models"
$piperModelDirectory = Join-Path $whisperDirectory "piper"
$piperRuntimeDirectory = Join-Path $ProjectRoot "runtime\piper"

New-Item -ItemType Directory -Force -Path $downloadDirectory, $voiceDirectory, $whisperDirectory, $piperModelDirectory, $piperRuntimeDirectory | Out-Null

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

$python = Get-Command python.exe -ErrorAction SilentlyContinue

& (Join-Path $PSScriptRoot "convert_phowhisper_model.ps1") -ProjectRoot $ProjectRoot

Get-VerifiedFile `
    -Uri "https://huggingface.co/rhasspy/piper-voices/resolve/main/vi/vi_VN/vais1000/medium/vi_VN-vais1000-medium.onnx?download=true" `
    -OutFile (Join-Path $piperModelDirectory "vi_VN-vais1000-medium.onnx") `
    -Sha256 "ec7c89e2c85f4d1edc24b6120c18aaf1bda614f06b511567eb9c7c0de15e2dab"
Get-VerifiedFile `
    -Uri "https://huggingface.co/rhasspy/piper-voices/resolve/main/vi/vi_VN/vais1000/medium/vi_VN-vais1000-medium.onnx.json?download=true" `
    -OutFile (Join-Path $piperModelDirectory "vi_VN-vais1000-medium.onnx.json") `
    -Sha256 "fafb9da1354ed4b77c31af228ed41fb41cd825c14cffa105454b25e6ae751ee0"

if ($python) {
    $piperPython = Join-Path $piperRuntimeDirectory "venv\Scripts\python.exe"
    if (-not (Test-Path -LiteralPath $piperPython -PathType Leaf)) {
        & $python.Source -m venv (Join-Path $piperRuntimeDirectory "venv")
    }
    & $piperPython -m pip install --disable-pip-version-check "piper-tts==1.8.0"
}

# Setup native VieNeu-TTS v3 Turbo models
& (Join-Path $PSScriptRoot "setup_vieneu_models.ps1")

Write-Host "PhoWhisper-small Q5_1, Piper 1.8.0 and native VieNeu-TTS assets are ready. Rebuild RaceEngineer to copy them beside the executable."
