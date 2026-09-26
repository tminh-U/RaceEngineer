[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+([-.].+)?$')]
    [string]$Version,
    [string]$BuildDirectory = '',
    [string]$OutputDirectory = '',
    [string]$QtPrefix = $env:QT_PREFIX,

    [string]$VCRedistPath = $env:VC_REDIST_X64,

    [string]$ISCCPath = $env:ISCC_PATH
)

$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot 'build-production'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot 'production\dist'
}
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
}
$outputPath = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
}
$stagingPath = Join-Path $outputPath "RaceEngineer-$Version"
$archivePath = Join-Path $outputPath "RaceEngineer-$Version-windows-x64.zip"

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$outputPrefix = $outputPath.TrimEnd('\') + '\'
if (-not $stagingPath.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean a staging path outside the output directory."
}
if (Test-Path -LiteralPath $stagingPath) {
    Remove-Item -LiteralPath $stagingPath -Recurse -Force
}
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

& cmake --install $buildPath --prefix $stagingPath
if ($LASTEXITCODE -ne 0) { throw "CMake install failed." }

$licensePath = Join-Path $projectRoot 'LICENSE'
if (Test-Path -LiteralPath $licensePath) {
    Copy-Item -LiteralPath $licensePath -Destination (Join-Path $stagingPath 'LICENSE')
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'production\README.md') `
    -Destination (Join-Path $stagingPath 'README.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'production\config\settings.example.json') `
    -Destination (Join-Path $stagingPath 'settings.example.json')
Copy-Item -LiteralPath (Join-Path $projectRoot 'production\config\pit_strategy.profile.example.json') `
    -Destination (Join-Path $stagingPath 'pit_strategy.profile.example.json')

$spotterSource = Join-Path $projectRoot 'assets\spotter'
$spotterManifest = Join-Path $spotterSource 'manifest.json'
if (Test-Path -LiteralPath $spotterManifest) {
    $spotterTarget = Join-Path $stagingPath 'bin\audio\spotter'
    New-Item -ItemType Directory -Force -Path $spotterTarget | Out-Null
    Copy-Item -Path (Join-Path $spotterSource '*') -Destination $spotterTarget -Recurse -Force
}

$pythonAddonSource = Join-Path $projectRoot 'apps\python\RaceEngineer'
if (Test-Path -LiteralPath $pythonAddonSource) {
    $pythonAddonTarget = Join-Path $stagingPath 'extras\AssettoCorsa\apps\python\RaceEngineer'
    New-Item -ItemType Directory -Force -Path $pythonAddonTarget | Out-Null
    Copy-Item -Path (Join-Path $pythonAddonSource '*') -Destination $pythonAddonTarget -Recurse -Force
}

$executablePath = Join-Path $stagingPath 'bin\RaceEngineer.exe'
if (-not (Test-Path -LiteralPath $executablePath)) {
    throw "Installed executable not found: $executablePath"
}

$windeployqt = if ($QtPrefix) {
    Join-Path (Resolve-Path $QtPrefix).Path 'bin\windeployqt.exe'
} else {
    (Get-Command windeployqt.exe -ErrorAction SilentlyContinue).Source
}
if (-not $windeployqt -or -not (Test-Path -LiteralPath $windeployqt)) {
    throw "windeployqt.exe was not found. Pass -QtPrefix or add it to PATH."
}

& $windeployqt --release --no-translations --compiler-runtime `
    --qmldir (Join-Path $projectRoot 'qml') $executablePath
if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed." }

$vcRuntimeDirectory = $null
if ($env:VCToolsRedistDir) {
    $candidate = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
    if (Test-Path -LiteralPath $candidate) {
        $vcRuntimeDirectory = $candidate
    }
}
if (-not $vcRuntimeDirectory -and $env:VCToolsInstallDir) {
    $candidate = Join-Path $env:VCToolsInstallDir '..\Redist\MSVC'
    if (Test-Path -LiteralPath $candidate) {
        $vcRuntimeDirectory = Get-ChildItem -LiteralPath $candidate -Directory |
            ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
            Where-Object { Test-Path -LiteralPath $_ } |
            Select-Object -First 1
    }
}
if (-not $vcRuntimeDirectory) {
    throw "MSVC runtime directory was not found. Run from an x64 MSVC shell."
}
Copy-Item -Path (Join-Path $vcRuntimeDirectory '*.dll') `
    -Destination (Join-Path $stagingPath 'bin') -Force

if ([string]::IsNullOrWhiteSpace($VCRedistPath) -and $env:VCToolsRedistDir) {
    $VCRedistPath = Get-ChildItem -Path $env:VCToolsRedistDir -Recurse `
        -Filter 'vc_redist.x64.exe' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $VCRedistPath -or -not (Test-Path -LiteralPath $VCRedistPath)) {
    throw "vc_redist.x64.exe was not found. Pass -VCRedistPath."
}
Copy-Item -LiteralPath $VCRedistPath `
    -Destination (Join-Path $stagingPath 'vc_redist.x64.exe') -Force

$manifestEntries = @(
    Get-ChildItem -LiteralPath $stagingPath -File -Recurse |
        Where-Object { $_.Name -ne 'release-manifest.json' } |
        ForEach-Object {
            $relativePath = $_.FullName.Substring($stagingPath.Length).TrimStart('\', '/') -replace '\\', '/'
            [ordered]@{
                path = $relativePath
                bytes = $_.Length
                sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
            }
        }
)

$commit = (& git -C $projectRoot rev-parse --short HEAD 2>$null)
$manifest = [ordered]@{
    product = 'RaceEngineer'
    version = $Version
    platform = 'windows-x64'
    commit = if ($LASTEXITCODE -eq 0) { $commit.Trim() } else { 'unknown' }
    built_utc = [DateTime]::UtcNow.ToString('o')
    files = $manifestEntries
}
[IO.File]::WriteAllText(
    (Join-Path $stagingPath 'release-manifest.json'),
    ($manifest | ConvertTo-Json -Depth 5),
    [Text.UTF8Encoding]::new($false)
)

Compress-Archive -Path (Join-Path $stagingPath '*') `
    -DestinationPath $archivePath -CompressionLevel Optimal

$isccCandidates = @(
    $ISCCPath,
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path ${env:ProgramFiles} 'Inno Setup 6\ISCC.exe')
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

$iscc = $isccCandidates |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1

if (-not $iscc) {
    throw "ISCC.exe was not found. Install Inno Setup 6 or pass -ISCCPath."
}

$installerScript = Join-Path $projectRoot 'production\installer\RaceEngineer.iss'
$installerPath = Join-Path $outputPath "RaceEngineer-$Version-Setup.exe"

& $iscc `
    "/DAppVersion=$Version" `
    "/DStagingDir=$stagingPath" `
    "/DOutputDir=$outputPath" `
    "/DOutputBaseName=RaceEngineer-$Version-Setup" `
    $installerScript

if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compilation failed."
}

if (-not (Test-Path -LiteralPath $installerPath)) {
    throw "Inno Setup did not create the expected installer: $installerPath"
}

Write-Host "Portable package: $stagingPath"
Write-Host "Archive: $archivePath"
Write-Host "Installer: $installerPath"
