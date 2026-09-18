param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [ValidateRange(1, 100)][int]$Variants = 15,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$OutputEncoding = [Text.Encoding]::UTF8

$python = Join-Path $ProjectRoot "runtime\piper\venv\Scripts\python.exe"
$model = Join-Path $ProjectRoot "models\piper\vi_VN-vais1000-medium.onnx"
$config = "$model.json"
$phrasesFile = Join-Path $PSScriptRoot "spotter_phrases.json"
$outputDirectory = Join-Path $ProjectRoot "assets\spotter"
$buildAudio = Join-Path $ProjectRoot "build\audio\spotter"

foreach ($required in @($python, $model, $config, $phrasesFile)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing required file: $required"
    }
}

$phrases = ConvertFrom-Json -InputObject (
    [IO.File]::ReadAllText($phrasesFile, [Text.Encoding]::UTF8))
foreach ($phrase in $phrases) {
    if ([string]::IsNullOrWhiteSpace([string]$phrase.id) -or
        [string]::IsNullOrWhiteSpace([string]$phrase.text)) {
        throw "Every spotter phrase needs non-empty id and text fields."
    }
}

if ($Clean) {
    if (Test-Path -LiteralPath $outputDirectory) {
        Get-ChildItem -LiteralPath $outputDirectory -Recurse -File | Remove-Item -Force
    }
    if (Test-Path -LiteralPath $buildAudio) {
        Get-ChildItem -LiteralPath $buildAudio -Recurse -File | Remove-Item -Force
    }
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$temporaryTranscript = Join-Path ([IO.Path]::GetTempPath()) (
    "raceengineer-piper-" + [guid]::NewGuid().ToString("N") + ".txt")
$completed = 0
$total = $phrases.Count * $Variants

try {
    foreach ($phrase in $phrases) {
        $phraseDirectory = Join-Path $outputDirectory ([string]$phrase.id)
        New-Item -ItemType Directory -Force -Path $phraseDirectory | Out-Null
        [IO.File]::WriteAllText($temporaryTranscript, ([string]$phrase.text) + [Environment]::NewLine,
            [Text.UTF8Encoding]::new($false))

        for ($variant = 1; $variant -le $Variants; $variant++) {
            $destination = Join-Path $phraseDirectory ("{0:D2}.wav" -f $variant)
            if (Test-Path -LiteralPath $destination -PathType Leaf) {
                $existing = [IO.File]::ReadAllBytes($destination)
                if ($existing.Length -ge 44 -and
                    [Text.Encoding]::ASCII.GetString($existing, 0, 4) -eq "RIFF") {
                    $completed++
                    continue
                }
            }
            $noiseScale = 0.55 + ((($variant - 1) % 5) * 0.035)
            $noiseWidth = 0.65 + ([Math]::Floor(($variant - 1) / 5) * 0.05)
            $lengthScale = 0.94 + ((($variant - 1) % 3) * 0.02)
            & $python -m piper -m $model -c $config -i $temporaryTranscript -f $destination `
                --length-scale $($lengthScale.ToString("0.00", [Globalization.CultureInfo]::InvariantCulture)) `
                --noise-scale $($noiseScale.ToString("0.000", [Globalization.CultureInfo]::InvariantCulture)) `
                --noise-w-scale $($noiseWidth.ToString("0.00", [Globalization.CultureInfo]::InvariantCulture)) `
                --sentence-silence 0.08
            if ($LASTEXITCODE -ne 0) { throw "Piper failed for $($phrase.id), variant $variant" }
            $audio = [IO.File]::ReadAllBytes($destination)
            if ($audio.Length -lt 44 -or [Text.Encoding]::ASCII.GetString($audio, 0, 4) -ne "RIFF") {
                throw "Invalid WAV for $($phrase.id), variant $variant"
            }
            $completed++
            Write-Host "[$completed/$total] $($phrase.id) variant $variant"
        }
    }

    $entries = foreach ($phrase in $phrases) {
        $files = for ($variant = 1; $variant -le $Variants; $variant++) {
            "$($phrase.id)/$("{0:D2}.wav" -f $variant)"
        }
        [ordered]@{ id = [string]$phrase.id; text = [string]$phrase.text; files = @($files) }
    }
    $manifest = [ordered]@{
        version = 2
        backend = "Piper 1.8.0"
        voice = "vi_VN-vais1000-medium"
        fine_tuned = $false
        variants_per_phrase = $Variants
        generated_utc = [DateTime]::UtcNow.ToString("o")
        entries = @($entries)
    } | ConvertTo-Json -Depth 6 -Compress
    [IO.File]::WriteAllText((Join-Path $outputDirectory "manifest.json"),
        $manifest + "`n", [Text.UTF8Encoding]::new($false))

    if (Test-Path -LiteralPath (Join-Path $ProjectRoot "build")) {
        New-Item -ItemType Directory -Force -Path $buildAudio | Out-Null
        Copy-Item -Path (Join-Path $outputDirectory "*") -Destination $buildAudio -Recurse -Force
    }
    Write-Host "Generated $total Piper spotter WAV files."
} finally {
    Remove-Item -LiteralPath $temporaryTranscript -Force -ErrorAction SilentlyContinue
}
