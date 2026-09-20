# Setup script for VieNeu-TTS v3 Turbo native model assets
param(
    [string]$TargetDir = "models\vieneu-v3"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ResolvedTarget = Join-Path $RepoRoot $TargetDir

Write-Host "Setting up VieNeu-TTS v3 models at: $ResolvedTarget" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $ResolvedTarget | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $ResolvedTarget "codec") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $ResolvedTarget "acoustic") | Out-Null

$HfCache = "$env:USERPROFILE\.cache\huggingface\hub"
$V3Cache = Join-Path $HfCache "models--pnnbao-ump--VieNeu-TTS-v3-Turbo\snapshots\5f2a3e93092efaba9153253ff5f2e6a8e810e4f2"
$MossCache = Join-Path $HfCache "models--OpenMOSS-Team--MOSS-Audio-Tokenizer-Nano-ONNX\snapshots\ceff0d0749bfb3fa2d61149794ec6feef0d1e1ae"

function Copy-Or-Download($SourcePath, $DestPath, $DownloadUrl) {
    if (Test-Path $DestPath) {
        $size = (Get-Item $DestPath).Length
        if ($size -gt 0) {
            Write-Host "Already exists: $DestPath ($size bytes)" -ForegroundColor Green
            return
        }
    }
    if (![string]::IsNullOrEmpty($SourcePath) -and (Test-Path $SourcePath) -and (Get-Item $SourcePath).Length -gt 0) {
        Copy-Item -LiteralPath $SourcePath -Destination $DestPath -Force
        Write-Host "Copied from cache: $(Split-Path -Leaf $DestPath)" -ForegroundColor Green
        return
    }
    Write-Host "Downloading: $DownloadUrl ..." -ForegroundColor Yellow
    curl.exe -L --fail --retry 3 -o $DestPath $DownloadUrl
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download $DownloadUrl"
    }
    Write-Host "Downloaded: $(Split-Path -Leaf $DestPath)" -ForegroundColor Green
}

$CppBaseUrl = "https://huggingface.co/lastudio-community/VieNeu-TTS-v3-Turbo-CPP/resolve/main"
$PnnBaseUrl = "https://huggingface.co/pnnbao-ump/VieNeu-TTS-v3-Turbo/resolve/main"

# Root configs
Copy-Or-Download (Join-Path $V3Cache "config.json") (Join-Path $ResolvedTarget "config.json") "$CppBaseUrl/config.json"
Copy-Or-Download (Join-Path $V3Cache "onnx_update\tokenizer.json") (Join-Path $ResolvedTarget "tokenizer.json") "$CppBaseUrl/tokenizer.json"
Copy-Or-Download (Join-Path $V3Cache "onnx_update\vieneu_v3_heads.npz") (Join-Path $ResolvedTarget "vieneu_v3_heads.npz") "$CppBaseUrl/vieneu_v3_heads.npz"
Copy-Or-Download (Join-Path $V3Cache "speaker_encoder.onnx") (Join-Path $ResolvedTarget "speaker_encoder.onnx") "$CppBaseUrl/speaker_encoder.onnx"
Copy-Or-Download (Join-Path $V3Cache "denoiser.onnx") (Join-Path $ResolvedTarget "denoiser.onnx") "$CppBaseUrl/denoiser.onnx"
Copy-Or-Download "" (Join-Path $ResolvedTarget "voices_v3_turbo.json") "$CppBaseUrl/voices_v3_turbo.json"

# Acoustic weights
Copy-Or-Download "" (Join-Path $ResolvedTarget "acoustic\vieneu_acoustic_weights.npz") "$CppBaseUrl/acoustic/vieneu_acoustic_weights.npz"

# Codec assets
$CodecDir = Join-Path $ResolvedTarget "codec"
Copy-Or-Download (Join-Path $MossCache "moss_audio_tokenizer_decode_full.onnx") (Join-Path $CodecDir "moss_audio_tokenizer_decode_full.onnx") "$CppBaseUrl/codec/moss_audio_tokenizer_decode_full.onnx"
Copy-Or-Download (Join-Path $MossCache "moss_audio_tokenizer_decode_shared.data") (Join-Path $CodecDir "moss_audio_tokenizer_decode_shared.data") "$CppBaseUrl/codec/moss_audio_tokenizer_decode_shared.data"
Copy-Or-Download (Join-Path $MossCache "moss_audio_tokenizer_encode.onnx") (Join-Path $CodecDir "moss_audio_tokenizer_encode.onnx") "$CppBaseUrl/codec/moss_audio_tokenizer_encode.onnx"
Copy-Or-Download (Join-Path $MossCache "moss_audio_tokenizer_encode.data") (Join-Path $CodecDir "moss_audio_tokenizer_encode.data") "$CppBaseUrl/codec/moss_audio_tokenizer_encode.data"

# Backbone GGUF
Copy-Or-Download "" (Join-Path $ResolvedTarget "backbone.gguf") "$CppBaseUrl/backbone.gguf"

Write-Host "All VieNeu-TTS v3 native models are ready at $ResolvedTarget" -ForegroundColor Green
