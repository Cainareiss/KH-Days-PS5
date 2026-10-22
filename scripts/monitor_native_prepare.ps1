[CmdletBinding()]
param(
    [int]$ProcessId = 0,
    [string]$SourceRoot = ".cache/khdays-native-v0.1.1"
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$root = if ([IO.Path]::IsPathRooted($SourceRoot)) {
    [IO.Path]::GetFullPath($SourceRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $project $SourceRoot))
}
if ($ProcessId -eq 0) {
    $active = Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
        Where-Object { $_.CommandLine -match '(khdays-native-v0\.1\.1.*prepare\.py|prepare_native_bounded\.py)' } |
        Select-Object -First 1
    if ($active) { $ProcessId = $active.ProcessId }
}
$proc = if ($ProcessId -gt 0) { Get-Process -Id $ProcessId -ErrorAction SilentlyContinue } else { $null }
$generated = Join-Path $root "build/ps5-gen"
$generatorState = Join-Path $generated "generator-state.json"
$generator = if (Test-Path -LiteralPath $generatorState) {
    Get-Content -Raw -LiteralPath $generatorState | ConvertFrom-Json
} else { $null }
$pipelineStatus = Join-Path $project "build-native-game-ps5/status.json"
$pipeline = if (Test-Path -LiteralPath $pipelineStatus) {
    Get-Content -Raw -LiteralPath $pipelineStatus | ConvertFrom-Json
} else { $null }
[ordered]@{
    process = if ($proc) { "running" } else { "exited" }
    pid = $ProcessId
    cpu_seconds = if ($proc) { [math]::Round($proc.CPU, 2) } else { $null }
    working_set_mb = if ($proc) { [math]::Round($proc.WorkingSet64 / 1MB, 1) } else { $null }
    generated_root = $generated
    generated_header = Test-Path -LiteralPath (Join-Path $generated "khdays_data_area.h")
    generated_sources = Test-Path -LiteralPath (Join-Path $generated "sources.cmake")
    rewritten_directory = Test-Path -LiteralPath (Join-Path $generated "rewritten")
    native_compile_stage = if ($pipeline) { $pipeline.stage } else { "not started" }
    generator_progress = $generator
    libc_ready = Test-Path -LiteralPath (Join-Path $project "build-native-link/PPSA35820/sce_module/libc.prx")
    cover_ready = Test-Path -LiteralPath (Join-Path $project "build-native-link/PPSA35820/sce_sys/icon0.png")
    background_ready = Test-Path -LiteralPath (Join-Path $project "build-native-link/PPSA35820/sce_sys/pic0.png")
} | ConvertTo-Json -Depth 5

$log = Join-Path $project "build-native-prepare.log"
$errorLog = Join-Path $project "build-native-prepare.err.log"
if ($generator) {
    $log = Join-Path $project "build-native-game.log"
    $errorLog = Join-Path $project "build-native-game.err.log"
}
if (Test-Path -LiteralPath $log) {
    "--- stdout (tail) ---"
    Get-Content -LiteralPath $log -Tail 5
}
if (Test-Path -LiteralPath $errorLog) {
    "--- stderr (tail) ---"
    Get-Content -LiteralPath $errorLog -Tail 5
}
