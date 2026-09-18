param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [ValidateRange(1, 100)][int]$Variants = 15,
    [switch]$Clean
)

# Static spotter audio deliberately uses low-latency Piper. Gwen-TTS remains
# available in the app as the optional dynamic, higher-latency backend.
$generator = Join-Path $PSScriptRoot "generate_piper_spotter_cache.ps1"
& $generator -ProjectRoot $ProjectRoot -Variants $Variants -Clean:$Clean
exit $LASTEXITCODE
