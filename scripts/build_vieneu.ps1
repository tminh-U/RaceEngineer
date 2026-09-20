$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$vsCmd = "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$vulkanSdk = if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { "C:\VulkanSDK\1.4.357.0" }
$sourceDir = Join-Path $RepoRoot "third_party\vieneu.cpp"
$buildDir = Join-Path $RepoRoot "build-vieneu-vk"
$ortRoot = Join-Path $RepoRoot "third_party\onnxruntime-dml"
$binTargetDir = Join-Path $RepoRoot "third_party\vieneu-bin\bin"
$libTargetDir = Join-Path $RepoRoot "third_party\vieneu-bin\lib"

New-Item -ItemType Directory -Force -Path $binTargetDir, $libTargetDir | Out-Null

$cmd = @"
call "$vsCmd"
set VULKAN_SDK=$vulkanSdk
set PATH=$vulkanSdk\bin;%PATH%
cmake -B "$buildDir" -S "$sourceDir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DVIENEU_NATIVE_BACKEND=vulkan -DONNXRUNTIME_ROOT="$ortRoot" -DGGML_VULKAN=ON
cmake --build "$buildDir" --config Release --target vieneu-tts
"@

$cmdPath = Join-Path $PSScriptRoot "build_tmp.bat"
try {
    Set-Content -Path $cmdPath -Value $cmd
    cmd /c $cmdPath
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

    # Copy output binaries and header to third_party/vieneu-bin
    $includeTargetDir = Join-Path $RepoRoot "third_party\vieneu-bin\include\vieneu"
    New-Item -ItemType Directory -Force -Path $includeTargetDir | Out-Null
    Copy-Item -Path (Join-Path $sourceDir "src\vieneu\vieneu_tts.h") -Destination (Join-Path $includeTargetDir "vieneu_tts.h") -Force
    Copy-Item -Path (Join-Path $buildDir "vieneu-tts.dll") -Destination $binTargetDir -Force
    Copy-Item -Path (Join-Path $buildDir "vieneu-tts.lib") -Destination $libTargetDir -Force
    Copy-Item -Path (Join-Path $buildDir "*.dll") -Destination $binTargetDir -Force
    Copy-Item -Path (Join-Path $ortRoot "bin\*.dll") -Destination $binTargetDir -Force

    $seaG2pSource = Join-Path $sourceDir "third_party\sea-g2p\python\sea_g2p\sea_g2p.bin"
    if (Test-Path $seaG2pSource) {
        Copy-Item -Path $seaG2pSource -Destination (Join-Path $binTargetDir "sea_g2p.bin") -Force
        if (Test-Path (Join-Path $RepoRoot "build")) {
            Copy-Item -Path $seaG2pSource -Destination (Join-Path $RepoRoot "build\sea_g2p.bin") -Force
        }
        Copy-Item -Path $seaG2pSource -Destination (Join-Path $buildDir "sea_g2p.bin") -Force
    }
    if (Test-Path (Join-Path $RepoRoot "build")) {
        Copy-Item -Path (Join-Path $buildDir "vieneu-tts.dll") -Destination (Join-Path $RepoRoot "build\vieneu-tts.dll") -Force
    }
    Write-Host "VieNeu-TTS native build completed and binaries staged to third_party/vieneu-bin." -ForegroundColor Green
} finally {
    if (Test-Path $cmdPath) {
        Remove-Item -Force $cmdPath -ErrorAction SilentlyContinue
    }
}
