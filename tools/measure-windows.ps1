param(
    [string]$Exe = "$PSScriptRoot/../build/windows/imvim.exe",
    [string]$Output = "$PSScriptRoot/../build/windows/memory.json"
)
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path $Exe).Path
$stats = Join-Path (Split-Path $exePath) 'idle.json'
$process = Start-Process -FilePath $exePath -ArgumentList @('--smoke-seconds', '12', '--stats', "`"$stats`"") -PassThru
$records = @{}
$nvimId = $null
while (-not $process.HasExited) {
    if (-not $nvimId) {
        $child = Get-CimInstance Win32_Process -Filter "ParentProcessId = $($process.Id)" | Select-Object -First 1
        if ($child) { $nvimId = $child.ProcessId }
    }
    $targets = @{ gui = $process.Id }
    if ($nvimId) { $targets.nvim = $nvimId }
    foreach ($name in $targets.Keys) {
        $sample = Get-Process -Id $targets[$name] -ErrorAction SilentlyContinue
        if (-not $sample) { continue }
        if (-not $records.ContainsKey($name)) {
            $records[$name] = @{ pid = $sample.Id; peak_private_bytes = 0L; peak_working_set_bytes = 0L }
        }
        $record = $records[$name]
        $record.peak_private_bytes = [Math]::Max($record.peak_private_bytes, $sample.PrivateMemorySize64)
        $record.peak_working_set_bytes = [Math]::Max($record.peak_working_set_bytes, $sample.WorkingSet64)
        $record.last_private_bytes = $sample.PrivateMemorySize64
        $record.last_working_set_bytes = $sample.WorkingSet64
    }
    Start-Sleep -Milliseconds 100
    $process.Refresh()
}
$process.WaitForExit()
$result = @{ exit_code = $process.ExitCode; processes = $records; note = 'CPU process memory only; excludes GPU allocations. Clean Neovim configuration.' }
$result | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $Output
Get-Content $Output
if (Test-Path $stats) { Get-Content $stats }
if ($process.ExitCode -ne 0) { throw "imvim exited with code $($process.ExitCode)" }
