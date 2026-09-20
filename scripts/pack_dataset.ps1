# Pack dataset directory into dataset.zip for uploading to Google Colab
param(
    [string]$SourceDir = "dataset",
    [string]$ZipFile = "dataset.zip"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$ResolvedSource = Join-Path $RepoRoot $SourceDir
$ResolvedZip = Join-Path $RepoRoot $ZipFile

if (!(Test-Path $ResolvedSource)) {
    Write-Error "Dataset directory not found: $ResolvedSource"
    exit 1
}

$audioDir = Join-Path $ResolvedSource "raw_audio"
$metaFile = Join-Path $ResolvedSource "metadata.csv"

if (!(Test-Path $audioDir)) {
    Write-Warning "Directory raw_audio/ not found in $ResolvedSource"
}
if (!(Test-Path $metaFile)) {
    Write-Warning "File metadata.csv not found in $ResolvedSource"
}

$audioCount = (Get-ChildItem -Path $audioDir -Filter "*.wav" -ErrorAction SilentlyContinue).Count
Write-Host "Packing dataset: $audioCount wav files found..." -ForegroundColor Cyan

if (Test-Path $ResolvedZip) {
    Remove-Item -Force $ResolvedZip
}

Compress-Archive -Path "$ResolvedSource\*" -DestinationPath $ResolvedZip -CompressionLevel Optimal

$sizeMb = (Get-Item $ResolvedZip).Length / 1MB
Write-Host "Success! Created $ResolvedZip ($([math]::Round($sizeMb, 2)) MB)" -ForegroundColor Green
Write-Host "You can now upload $ResolvedZip to Google Colab or Google Drive." -ForegroundColor Yellow
