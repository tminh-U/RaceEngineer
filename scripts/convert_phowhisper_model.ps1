param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$PythonPath = "python.exe",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$modelRevision = "55a7e3eb6c906de891f8f06a107754427dd3be79"
$whisperRevision = "31243bad24cc746f07d4c8bfdd2d974872cb1803"
$workDirectory = Join-Path $ProjectRoot ".tooling\phowhisper"
$modelDirectory = Join-Path $workDirectory "vinai-PhoWhisper-medium"
$openAiWhisperDirectory = Join-Path $workDirectory "openai-whisper"
$venvDirectory = Join-Path $workDirectory "venv"
$venvPython = Join-Path $venvDirectory "Scripts\python.exe"
$converterBuildDirectory = Join-Path $workDirectory "whisper-cpp-build"
$outputDirectory = Join-Path $ProjectRoot "models"
$f16Model = Join-Path $workDirectory "ggml-phowhisper-medium-f16.bin"
$q5Model = Join-Path $outputDirectory "ggml-phowhisper-medium-q5_0.bin"

New-Item -ItemType Directory -Force -Path $workDirectory, $outputDirectory | Out-Null
if ((Test-Path -LiteralPath $q5Model -PathType Leaf) -and -not $Force) {
    Write-Host "Already converted: $q5Model"
    Get-FileHash -Algorithm SHA256 -LiteralPath $q5Model
    return
}
if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
    & $PythonPath -m venv $venvDirectory
    if ($LASTEXITCODE -ne 0) { throw "Could not create the PhoWhisper conversion virtual environment." }
}
& $venvPython -m pip install --disable-pip-version-check --upgrade pip
& $venvPython -m pip install --disable-pip-version-check torch transformers numpy
if ($LASTEXITCODE -ne 0) { throw "Could not install the one-time PhoWhisper conversion dependencies." }

if (-not (Test-Path -LiteralPath (Join-Path $modelDirectory ".git") -PathType Container)) {
    $env:GIT_LFS_SKIP_SMUDGE = "1"
    git clone https://huggingface.co/vinai/PhoWhisper-medium $modelDirectory
    $cloneExitCode = $LASTEXITCODE
    Remove-Item Env:GIT_LFS_SKIP_SMUDGE -ErrorAction SilentlyContinue
    if ($cloneExitCode -ne 0) { throw "Could not clone vinai/PhoWhisper-medium." }
}
git -C $modelDirectory fetch origin $modelRevision
if ($LASTEXITCODE -ne 0) { throw "Could not fetch the pinned vinai/PhoWhisper-medium revision." }
git -C $modelDirectory checkout --detach $modelRevision
if ($LASTEXITCODE -ne 0) { throw "Could not check out the pinned vinai/PhoWhisper-medium revision." }
git -C $modelDirectory lfs pull
if ($LASTEXITCODE -ne 0) { throw "Could not download vinai/PhoWhisper-medium weights." }

if (-not (Test-Path -LiteralPath (Join-Path $openAiWhisperDirectory ".git") -PathType Container)) {
    git clone https://github.com/openai/whisper.git $openAiWhisperDirectory
    if ($LASTEXITCODE -ne 0) { throw "Could not clone the OpenAI Whisper conversion assets." }
}
git -C $openAiWhisperDirectory fetch origin $whisperRevision
if ($LASTEXITCODE -ne 0) { throw "Could not fetch the pinned OpenAI Whisper conversion revision." }
git -C $openAiWhisperDirectory checkout --detach $whisperRevision
if ($LASTEXITCODE -ne 0) { throw "Could not check out the pinned OpenAI Whisper conversion revision." }

& $venvPython (Join-Path $ProjectRoot "third_party\whisper.cpp\models\convert-h5-to-ggml.py") `
    $modelDirectory $openAiWhisperDirectory $workDirectory
if ($LASTEXITCODE -ne 0) { throw "whisper.cpp could not convert vinai/PhoWhisper-medium." }
Move-Item -LiteralPath (Join-Path $workDirectory "ggml-model.bin") -Destination $f16Model -Force

cmake -S (Join-Path $ProjectRoot "third_party\whisper.cpp") -B $converterBuildDirectory `
    -DWHISPER_BUILD_EXAMPLES=ON -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF `
    -DGGML_VULKAN=OFF -DGGML_CUDA=OFF
if ($LASTEXITCODE -ne 0) { throw "Could not configure whisper.cpp quantize." }
cmake --build $converterBuildDirectory --target whisper-quantize --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Could not build whisper.cpp quantize." }
& (Join-Path $converterBuildDirectory "bin\whisper-quantize.exe") $f16Model $q5Model q5_0
if ($LASTEXITCODE -ne 0) { throw "Could not quantize PhoWhisper-medium to Q5_0." }

Write-Host "Created $q5Model"
Get-FileHash -Algorithm SHA256 -LiteralPath $q5Model
