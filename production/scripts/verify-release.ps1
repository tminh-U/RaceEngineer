[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
$packagePath = (Resolve-Path $PackageDirectory).Path
$binPath = Join-Path $packagePath 'bin'
$manifestPath = Join-Path $packagePath 'release-manifest.json'

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "release-manifest.json is missing."
}

$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$requiredFiles = @(
    'bin/RaceEngineer.exe',
    'bin/Qt6Core.dll',
    'bin/Qt6Quick.dll',
    'bin/sound.mp3',
    'bin/final_icon.ico',
    'bin/msvcp140.dll',
    'bin/vcruntime140.dll',
    'bin/vcruntime140_1.dll',
    'bin/audio/spotter/manifest.json',
    'vc_redist.x64.exe',
    'bin/sea_g2p.bin',
    'bin/models/ggml-phowhisper-small-q5_1.bin',
    'bin/models/vieneu-v3/backbone.gguf',
    'bin/models/vieneu-v3/config.json',
    'bin/models/vieneu-v3/tokenizer.json',
    'bin/models/vieneu-v3/vieneu_v3_heads.npz',
    'bin/models/vieneu-v3/acoustic/vieneu_acoustic_weights.npz',
    'bin/models/vieneu-v3/codec/moss_audio_tokenizer_decode_full.onnx',
    'extras/AssettoCorsa/apps/python/RaceEngineer/RaceEngineer.py',
    'extras/AssettoCorsa/apps/python/RaceEngineer/manifest.ini'
)

foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $packagePath ($relativePath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required package file is missing: $relativePath"
    }
}

foreach ($entry in $manifest.files) {
    $path = Join-Path $packagePath ($entry.path -replace '/', '\')
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Manifest file is missing: $($entry.path)"
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
    if ($hash -ne $entry.sha256) {
        throw "Checksum mismatch: $($entry.path)"
    }
}

$unexpectedSymbols = Get-ChildItem -LiteralPath $packagePath -File -Recurse |
    Where-Object { $_.Extension -in @('.pdb', '.ilk') -or $_.Name -eq 'settings.json' }
if ($unexpectedSymbols) {
    throw "Development or user-specific files found in package: $($unexpectedSymbols.FullName -join ', ')"
}

$obsoleteInstallerFiles = Get-ChildItem -LiteralPath $packagePath -File -Recurse |
    Where-Object { $_.Name -in @('install.ps1', 'setup.cmd', 'payload.zip', 'payload.7z', 'config.txt') }
if ($obsoleteInstallerFiles) {
    throw "Obsolete installer files found in package: $($obsoleteInstallerFiles.FullName -join ', ')"
}

$executablePath = Join-Path $binPath 'RaceEngineer.exe'
$env:PATH = "$binPath;$env:PATH"
$versionProcess = Start-Process -FilePath $executablePath `
    -ArgumentList '--version' `
    -WorkingDirectory $binPath `
    -Wait `
    -PassThru `
    -WindowStyle Hidden
if ($versionProcess.ExitCode -ne 0) {
    throw "Packaged executable did not start successfully."
}

Write-Host "Release verified: $($manifest.product) $($manifest.version) ($($manifest.platform))"
Write-Host "$($manifest.product) $($manifest.version)"
