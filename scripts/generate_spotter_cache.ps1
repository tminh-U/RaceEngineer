param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [ValidateRange(1, 100)][int]$Variants = 50,
    [ValidateRange(1024, 65535)][int]$Port = 8398
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http
$runtime = Join-Path $ProjectRoot "runtime\gwen-tts\crispasr-windows-x86_64-vulkan\crispasr.exe"
$model = Join-Path $ProjectRoot "models\gwen-tts\gwen-tts-0.6b-q8_0.gguf"
$codec = Join-Path $ProjectRoot "models\gwen-tts\qwen3-tts-tokenizer-12hz.gguf"
$voiceDirectory = Join-Path $ProjectRoot "voices\gwen-tts"
$referenceTextPath = Join-Path $voiceDirectory "khanh_toan.txt"
$outputDirectory = Join-Path $ProjectRoot "assets\spotter"

foreach ($required in @($runtime, $model, $codec, $referenceTextPath, (Join-Path $voiceDirectory "khanh_toan.wav"))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required file: $required" }
}

$phrases = @(
    [ordered]@{ Id = "session_started"; Text = "Đã kết nối với game." },
    [ordered]@{ Id = "session_ended"; Text = "Đã ngắt kết nối game." },
    [ordered]@{ Id = "fuel_low"; Text = "Nhiên liệu sắp hết." },
    [ordered]@{ Id = "fuel_critical"; Text = "Nhiên liệu nguy cấp." },
    [ordered]@{ Id = "engine_hot"; Text = "Nhiệt độ động cơ cao." },
    [ordered]@{ Id = "engine_critical"; Text = "Động cơ quá nhiệt." },
    [ordered]@{ Id = "yellow_flag"; Text = "Cờ vàng." },
    [ordered]@{ Id = "blue_flag"; Text = "Cờ xanh dương." },
    [ordered]@{ Id = "pit_limiter_on"; Text = "Đã bật giới hạn tốc độ pit." },
    [ordered]@{ Id = "pit_limiter_off"; Text = "Đã tắt giới hạn tốc độ pit." },
    [ordered]@{ Id = "new_best_lap"; Text = "Vòng chạy nhanh nhất mới." }
)

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$referenceText = [IO.File]::ReadAllText($referenceTextPath, [Text.Encoding]::UTF8).Trim()
$arguments = @(
    "--server", "--backend", "qwen3-tts", "--model", $model,
    "--codec-model", $codec, "--voice-dir", $voiceDirectory,
    "--i-have-rights", "--speaker-identity", "real_person",
    "--no-spoken-disclaimer", "--accept-marking-responsibility",
    "--no-punctuation", "--host", "127.0.0.1", "--port", "$Port",
    "--gpu-backend", "vulkan", "--threads", "8"
)

$server = $null
$client = [Net.Http.HttpClient]::new()
$client.Timeout = [TimeSpan]::FromMinutes(5)
try {
    $server = Start-Process -FilePath $runtime -ArgumentList $arguments -WorkingDirectory (Split-Path $runtime) -WindowStyle Hidden -PassThru
    $ready = $false
    for ($attempt = 0; $attempt -lt 240; $attempt++) {
        Start-Sleep -Milliseconds 250
        try {
            $response = $client.GetAsync("http://127.0.0.1:$Port/health").GetAwaiter().GetResult()
            if ($response.IsSuccessStatusCode) { $ready = $true; break }
        } catch { }
        if ($server.HasExited) { throw "Gwen-TTS server exited during startup with code $($server.ExitCode)." }
    }
    if (-not $ready) { throw "Gwen-TTS server did not become ready within 60 seconds." }

    $completed = 0
    $total = $phrases.Count * $Variants
    foreach ($phrase in $phrases) {
        $phraseDirectory = Join-Path $outputDirectory $phrase.Id
        New-Item -ItemType Directory -Force -Path $phraseDirectory | Out-Null
        for ($seed = 1; $seed -le $Variants; $seed++) {
            $fileName = "{0:D2}.wav" -f $seed
            $destination = Join-Path $phraseDirectory $fileName
            if (Test-Path -LiteralPath $destination) {
                $existing = [IO.File]::ReadAllBytes($destination)
                if ($existing.Length -ge 44 -and [Text.Encoding]::ASCII.GetString($existing, 0, 4) -eq "RIFF") {
                    $completed++
                    continue
                }
            }
            $payload = [ordered]@{
                model = "gwen-tts"
                input = $phrase.Text
                voice = "khanh_toan"
                ref_text = $referenceText
                language = "vi"
                source_lang = "vi"
                response_format = "wav"
                seed = $seed
                spoken_disclaimer = $false
                consent_attestation = "User requested the upstream Gwen-TTS khanh_toan preset published by G-Group AI Lab."
                marking_attestation = "RaceEngineer labels output as AI-generated local TTS; watermark and C2PA remain enabled."
            }
            $json = $payload | ConvertTo-Json -Compress
            $content = [Net.Http.ByteArrayContent]::new([Text.Encoding]::UTF8.GetBytes($json))
            $content.Headers.ContentType = [Net.Http.Headers.MediaTypeHeaderValue]::new("application/json")
            $response = $client.PostAsync("http://127.0.0.1:$Port/v1/audio/speech", $content).GetAwaiter().GetResult()
            $audio = $response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
            if (-not $response.IsSuccessStatusCode -or $audio.Length -lt 44 -or [Text.Encoding]::ASCII.GetString($audio, 0, 4) -ne "RIFF") {
                $detail = [Text.Encoding]::UTF8.GetString($audio)
                throw "Synthesis failed for $($phrase.Id) seed $seed (HTTP $([int]$response.StatusCode)): $detail"
            }
            [IO.File]::WriteAllBytes($destination, $audio)
            $completed++
            Write-Host "[$completed/$total] $($phrase.Id) seed $seed"
        }
    }

    $entries = foreach ($phrase in $phrases) {
        $files = for ($seed = 1; $seed -le $Variants; $seed++) {
            "$($phrase.Id)/$("{0:D2}.wav" -f $seed)"
        }
        [ordered]@{ id = $phrase.Id; text = $phrase.Text; files = @($files) }
    }
    $manifest = [ordered]@{
        version = 1
        voice = "khanh_toan"
        variants_per_phrase = $Variants
        generated_utc = [DateTime]::UtcNow.ToString("o")
        entries = @($entries)
    } | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText((Join-Path $outputDirectory "manifest.json"), $manifest + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))

    $buildAudio = Join-Path $ProjectRoot "build\audio\spotter"
    if (Test-Path -LiteralPath (Join-Path $ProjectRoot "build")) {
        New-Item -ItemType Directory -Force -Path $buildAudio | Out-Null
        Copy-Item -LiteralPath (Join-Path $outputDirectory "manifest.json") -Destination $buildAudio -Force
        foreach ($phrase in $phrases) {
            Copy-Item -LiteralPath (Join-Path $outputDirectory $phrase.Id) -Destination $buildAudio -Recurse -Force
        }
    }
    Write-Host "Generated $total cached spotter WAV files in $outputDirectory"
} finally {
    $client.Dispose()
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
        $server.WaitForExit()
    }
}
