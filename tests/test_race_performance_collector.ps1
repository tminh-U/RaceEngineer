# Cross-platform checks of the Windows collector's parser, formatting and helpers.
# Does not fabricate a Windows capture or execute Windows P/Invoke on Linux.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$source = Join-Path $PSScriptRoot '..\tools\measure-race-performance.ps1'
$source = (Resolve-Path -LiteralPath $source).Path
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw ($parseErrors | Out-String) }
foreach ($function in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]}, $true)) {
    . ([scriptblock]::Create($function.Extent.Text))
}
$directory = Join-Path ([IO.Path]::GetTempPath()) ('race-profiler-unit-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($directory)
try {
    [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('vi-VN')
    if ((ConvertTo-ProfileNumber 1.25) -ne '1.250000') { throw 'CSV number formatting depends on Windows locale.' }
    $self = Get-Process -Id $PID
    $sample = Get-ProfileProcessSample $self
    if ($sample.cpu_ms -lt 0 -or $sample.working_set_bytes -le 0) { throw 'Process resource helper failed.' }
    $identity = Get-ProfileProcessIdentity $self
    if ($identity.image_sha256.Length -ne 64 -or $identity.pid -ne $PID) { throw 'Process binary identity was not recorded.' }
    $failed = $false
    try { Get-ProfileProcess $PID @('not-this-process') 'test' | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw 'Collector accepted the wrong process name.' }
    $path = Join-Path $directory 'manifest.json'
    Write-ProfileManifest $path ([ordered]@{schema=1; status='unit_fixture'; value=1.25})
    $saved = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($saved.status -ne 'unit_fixture' -or $saved.value -ne 1.25) { throw 'Atomic manifest write failed.' }
    # Match the collector's constructor/CSV expression, not merely parser acceptance.
    $csvPath = Join-Path $directory 'resources.csv'
    $writer = New-Object IO.StreamWriter($csvPath, $false, (New-Object Text.UTF8Encoding($false)))
    try {
        $row = [ordered]@{elapsed_s=(ConvertTo-ProfileNumber 1.25); app_cpu_ms=(ConvertTo-ProfileNumber 20.)}
        $lines = @([pscustomobject]$row | ConvertTo-Csv -NoTypeInformation)
        foreach ($line in $lines) { $writer.WriteLine($line) }
    } finally { $writer.Dispose() }
    $csv = @(Import-Csv -LiteralPath $csvPath)
    if ($csv.Count -ne 1 -or $csv[0].elapsed_s -ne '1.250000') { throw 'Locale-independent CSV write failed.' }
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        $declarations = @($ast.FindAll({param($node)
            $node -is [Management.Automation.Language.StringConstantExpressionAst] -and
            $node.StringConstantType -eq [Management.Automation.Language.StringConstantType]::SingleQuotedHereString
        }, $true))
        if ($declarations.Count -ne 1) { throw 'Expected one native C# declaration block.' }
        Add-Type -TypeDefinition $declarations[0].Value
        $native = 'RaceEngineerProfiling.NativeMemory' -as [type]
        if ($null -eq $native) { throw 'Native memory C# declarations failed to compile.' }
        $layout = $native.GetNestedType('PerformanceInfo', [Reflection.BindingFlags]::NonPublic)
        $size = [Runtime.InteropServices.Marshal]::SizeOf([Activator]::CreateInstance($layout))
        if ([IntPtr]::Size -eq 8 -and $size -ne 104) { throw 'Incorrect 64-bit PERFORMANCE_INFORMATION layout.' }
        $rejected = $false
        try { & $source -Game AC -Mode GameOnly -Scenario 'unit fixture' -ResourcesOnly | Out-Null }
        catch { $rejected = $_.Exception.Message -match 'real Windows machine' }
        if (-not $rejected) { throw 'Collector accepted Linux as a real Windows/game capture.' }
    } else {
        $memory = Get-ProfileMemory
        $installed = [RaceEngineerProfiling.NativeMemory]::InstalledBytes()
        if ($memory.TotalPhysical -le 0 -or $memory.AvailablePhysical -gt $memory.TotalPhysical -or
            $memory.CommitLimit -le 0 -or $memory.CommitBytes -gt $memory.CommitLimit -or
            $installed -lt $memory.TotalPhysical) {
            throw 'Native Windows memory APIs returned invalid physical/commit counters.'
        }
    }
    Write-Output 'Collector helper checks passed; native memory APIs checked on Windows, declarations only on Linux. ETW/game capture is not validated by these tests.'
} finally { Remove-Item -LiteralPath $directory -Recurse -Force }
