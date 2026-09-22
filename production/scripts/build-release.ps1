[CmdletBinding()]
param(
    [string]$BuildDirectory = '',
    [string]$QtPrefix = $env:QT_PREFIX,
    [switch]$EnableLtcg
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "MSVC cl.exe was not found. Run this script from an x64 Visual Studio developer shell."
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot 'build-production'
}
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
}
$ltcg = if ($EnableLtcg) { 'ON' } else { 'OFF' }

$configureArgs = @(
    '--fresh',
    '-S', $projectRoot,
    '-B', $buildPath,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DRACEENGINEER_ENABLE_LTCG=$ltcg"
)

if ($QtPrefix) {
    $qtPath = (Resolve-Path $QtPrefix).Path
    $configureArgs += "-DCMAKE_PREFIX_PATH=$qtPath"
    $env:PATH = "$qtPath\bin;$env:PATH"
}

$env:PATH = "$buildPath;$env:PATH"

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

& cmake --build $buildPath --parallel
if ($LASTEXITCODE -ne 0) { throw "Release build failed." }

if ($QtPrefix) {
    $windeployqt = Join-Path (Resolve-Path $QtPrefix).Path 'bin\windeployqt.exe'
} else {
    $windeployqt = (Get-Command windeployqt.exe -ErrorAction SilentlyContinue).Source
}
if (-not $windeployqt -or -not (Test-Path -LiteralPath $windeployqt)) {
    throw "windeployqt.exe was not found. Pass -QtPrefix or add it to PATH."
}

$executablePath = Join-Path $buildPath 'RaceEngineer.exe'
& $windeployqt --release --no-translations --compiler-runtime `
    --qmldir (Join-Path $projectRoot 'qml') $executablePath
if ($LASTEXITCODE -ne 0) { throw "Qt runtime deployment failed." }

& ctest --test-dir $buildPath --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Release tests failed." }

Write-Host "Production build ready: $buildPath"
