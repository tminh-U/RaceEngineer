param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$downloadDirectory = Join-Path $ProjectRoot ".downloads\runtime"
$gwenRuntimeDirectory = Join-Path $ProjectRoot "runtime\gwen-tts"
$voiceDirectory = Join-Path $ProjectRoot "voices\gwen-tts"
$whisperDirectory = Join-Path $ProjectRoot "models"
$gwenModelDirectory = Join-Path $whisperDirectory "gwen-tts"

New-Item -ItemType Directory -Force -Path $downloadDirectory, $gwenRuntimeDirectory, $voiceDirectory, $whisperDirectory, $gwenModelDirectory | Out-Null

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

Get-VerifiedFile `
    -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin?download=true" `
    -OutFile (Join-Path $whisperDirectory "ggml-small-q5_1.bin") `
    -Sha256 "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb"

Write-Host "Whisper and Gwen-TTS (Khanh Toan) runtime assets are ready. Rebuild RaceEngineer to copy them beside the executable."
