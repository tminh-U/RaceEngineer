param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$downloadDirectory = Join-Path $ProjectRoot ".downloads\runtime"
$gwenRuntimeDirectory = Join-Path $ProjectRoot "runtime\gwen-tts"
$voiceDirectory = Join-Path $ProjectRoot "voices\gwen-tts"
$whisperDirectory = Join-Path $ProjectRoot "models"
$gwenModelDirectory = Join-Path $whisperDirectory "gwen-tts"
$piperModelDirectory = Join-Path $whisperDirectory "piper"
$piperRuntimeDirectory = Join-Path $ProjectRoot "runtime\piper"

New-Item -ItemType Directory -Force -Path $downloadDirectory, $gwenRuntimeDirectory, $voiceDirectory, $whisperDirectory, $gwenModelDirectory, $piperModelDirectory, $piperRuntimeDirectory | Out-Null

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

$crispArchive = Join-Path $downloadDirectory "crispasr-windows-x86_64-vulkan-v0.8.33.zip"
Get-VerifiedFile `
    -Uri "https://github.com/CrispStrobe/CrispASR/releases/download/v0.8.33/crispasr-windows-x86_64-vulkan.zip" `
    -OutFile $crispArchive `
    -Sha256 "a774ff3cc205e34a87f2ddd94817f634e6379f1b72ba9e25b73f740c1b146ea1"
Expand-Archive -LiteralPath $crispArchive -DestinationPath $gwenRuntimeDirectory -Force

Get-VerifiedFile `
    -Uri "https://huggingface.co/cstr/gwen-tts-0.6b-GGUF/resolve/main/gwen-tts-0.6b-q8_0.gguf?download=true" `
    -OutFile (Join-Path $gwenModelDirectory "gwen-tts-0.6b-q8_0.gguf") `
    -Sha256 "53fac479a337af6ac09664dcc7248a471324cf187df77f6d0d4fe76ac10bfc96"
Get-VerifiedFile `
    -Uri "https://huggingface.co/cstr/qwen3-tts-tokenizer-12hz-GGUF/resolve/main/qwen3-tts-tokenizer-12hz.gguf?download=true" `
    -OutFile (Join-Path $gwenModelDirectory "qwen3-tts-tokenizer-12hz.gguf") `
    -Sha256 "70dc95dbfdd9aa5d9d406236ff771d061bf17b0cda02a72513953355606e719b"

$reference16k = Join-Path $downloadDirectory "khanh_toan-16khz.wav"
Get-VerifiedFile `
    -Uri "https://raw.githubusercontent.com/ggroup-ai-lab/gwen-tts/main/data/khanh_toan.wav" `
    -OutFile $reference16k `
    -Sha256 "07827ee2a1b9d96591167d2065e1557af0c1f85a57c7c5b1d3611b67e5dd6062"

$python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $python) {
    throw "Python 3 is required once to resample the official khanh_toan reference from 16 kHz to 24 kHz."
}
& $python.Source (Join-Path $PSScriptRoot "resample_wav.py") `
    $reference16k (Join-Path $voiceDirectory "khanh_toan.wav") --rate 24000

$referenceText = "việt nam đang kiêu hãnh bước vào kỷ nguyên vươn mình rực rỡ với khát vọng mãnh liệt, trí tuệ đổi mới, tinh thần đoàn kết."
[System.IO.File]::WriteAllText(
    (Join-Path $voiceDirectory "khanh_toan.txt"),
    $referenceText + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))

& (Join-Path $PSScriptRoot "convert_phowhisper_model.ps1") -ProjectRoot $ProjectRoot

Get-VerifiedFile `
    -Uri "https://huggingface.co/rhasspy/piper-voices/resolve/main/vi/vi_VN/vais1000/medium/vi_VN-vais1000-medium.onnx?download=true" `
    -OutFile (Join-Path $piperModelDirectory "vi_VN-vais1000-medium.onnx") `
    -Sha256 "ec7c89e2c85f4d1edc24b6120c18aaf1bda614f06b511567eb9c7c0de15e2dab"
Get-VerifiedFile `
    -Uri "https://huggingface.co/rhasspy/piper-voices/resolve/main/vi/vi_VN/vais1000/medium/vi_VN-vais1000-medium.onnx.json?download=true" `
    -OutFile (Join-Path $piperModelDirectory "vi_VN-vais1000-medium.onnx.json") `
    -Sha256 "fafb9da1354ed4b77c31af228ed41fb41cd825c14cffa105454b25e6ae751ee0"

$piperPython = Join-Path $piperRuntimeDirectory "venv\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $piperPython -PathType Leaf)) {
    & $python.Source -m venv (Join-Path $piperRuntimeDirectory "venv")
}
& $piperPython -m pip install --disable-pip-version-check "piper-tts==1.8.0"
if ($LASTEXITCODE -ne 0) { throw "Could not install Piper 1.8.0 runtime." }

Write-Host "PhoWhisper-medium Q5, Piper 1.8.0 and Gwen-TTS (Khanh Toan) runtime assets are ready. Rebuild RaceEngineer to copy them beside the executable."
