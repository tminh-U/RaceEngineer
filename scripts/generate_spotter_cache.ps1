param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

# Default: High-fidelity native VieNeu-TTS (Minh Quân preset, GPU Vulkan offload)
$exe = Join-Path $ProjectRoot "build\generate_spotter_cache.exe"
if (-not (Test-Path $exe)) {
    Write-Host "Compiling generate_spotter_cache.exe..."
    $vsCmd = "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    $cmd = "call `"$vsCmd`" && cl /nologo /O2 /MD /EHsc /utf-8 /std:c++17 /I`"$ProjectRoot\third_party\vieneu-bin\include`" `"$ProjectRoot\tools\generate_spotter_cache.cpp`" /Fe:`"$exe`" /link /LIBPATH:`"$ProjectRoot\third_party\vieneu-bin\lib`" vieneu-tts.lib"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "Failed to compile generate_spotter_cache.exe" }
}

$prev = Get-Location
try {
    Set-Location (Join-Path $ProjectRoot "build")
    & $exe
    exit $LASTEXITCODE
} finally {
    Set-Location $prev
}
