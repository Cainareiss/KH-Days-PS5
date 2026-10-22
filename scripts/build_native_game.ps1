[CmdletBinding()]
param(
    [string]$SourceRoot = ".cache/khdays-native-v0.1.1",
    [string]$BuildDir = "build-native-game-ps5",
    [int]$WaitForPrepareProcessId = 0,
    [ValidateRange(1, 16)][int]$Jobs = 1,
    [switch]$Prepare,
    [ValidateRange(1, 1)][int]$PrepareJobs = 1
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
function ProjectPath([string]$value) {
    if ([IO.Path]::IsPathRooted($value)) { return [IO.Path]::GetFullPath($value) }
    return [IO.Path]::GetFullPath((Join-Path $project $value))
}
$source = ProjectPath $SourceRoot
$build = ProjectPath $BuildDir
$generated = Join-Path $source "build/ps5-gen"
$adapted = Join-Path $project "build-native-elf-sources"
$sdk = Join-Path $project "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk"
$llvm = Join-Path $project "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc"
$ninja = Join-Path $env:APPDATA "Python/Python314/Scripts/ninja.exe"
$statusFile = Join-Path $build "status.json"
New-Item -ItemType Directory -Path $build -Force | Out-Null
$status = [ordered]@{
    stage = "checking inputs"
    source = $source
    generated = $generated
    adapted = $adapted
    build = $build
    prepare_pid = $WaitForPrepareProcessId
    game_archive = $null
    final_eboot = "not linked"
    hardware = "not tested"
}
function Status([string]$stage) {
    $status.stage = $stage
    $status | ConvertTo-Json | Set-Content -LiteralPath $statusFile -Encoding UTF8
    Write-Output $stage
}

$oldPath = $env:PATH
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$oldSdk = $env:PS5_PAYLOAD_SDK
$oldLlvm = $env:PS5_LLVM_ROOT
$buildLock = $null
try {
    $buildLock = [IO.File]::Open((Join-Path $build "build.lock"), 'OpenOrCreate', 'ReadWrite', 'None')
    foreach ($required in @($ninja, (Join-Path $llvm "bin/clang.exe"),
                           (Join-Path $sdk "target/include/stdio.h"))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing input: $required" }
    }
    $lock = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $project "native/upstream-lock.json") |
        ConvertFrom-Json
    $safeSource = $source.Replace('\', '/')
    $revision = (& git -c "safe.directory=$safeSource" -C $source rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $revision -ne $lock.commit) { throw "Unpinned native upstream: $revision" }
    $decomp = Join-Path $source "external/khdays-decomp"
    $safeDecomp = $decomp.Replace('\', '/')
    $decompRevision = (& git -c "safe.directory=$safeDecomp" -C $decomp rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $decompRevision -ne $lock.decomp_commit) {
        throw "Unpinned decomp: $decompRevision"
    }

    if ($WaitForPrepareProcessId -gt 0) {
        $prepareProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$WaitForPrepareProcessId"
        if ($prepareProcess -and $prepareProcess.CommandLine -notmatch '(khdays-native-v0\.1\.1.*prepare\.py|prepare_native_bounded\.py)') {
            throw "PID $WaitForPrepareProcessId is not the expected source generator"
        }
        if ($prepareProcess) {
            $running = Get-Process -Id $WaitForPrepareProcessId -ErrorAction SilentlyContinue
            if ($running) {
                Status "waiting for the existing source generator"
                $running.WaitForExit()
                if ($running.ExitCode -ne 0) {
                    throw "Source generator failed: exit code $($running.ExitCode). See build-native-prepare.err.log."
                }
            }
        }
    }
    if ($Prepare) {
        $active = Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
            Where-Object { $_.CommandLine -match '(khdays-native-v0\.1\.1.*prepare\.py|prepare_native_bounded\.py)' }
        if ($active) { throw "A generator is already running (PID $($active.ProcessId -join ', '))" }
        Status "generating native sources with bounded memory and cache reuse"
        & python -u (Join-Path $project "scripts/prepare_native_bounded.py") `
            --source $source --out $generated --jobs $PrepareJobs
        if ($LASTEXITCODE -ne 0) { throw "Bounded source generation failed: $LASTEXITCODE. See generator-state.json." }
    }
    foreach ($filename in @("sources.cmake", "khdays_data_area.h", "khdays_ds_symbols.h", "ds_functions.c")) {
        if (-not (Test-Path -LiteralPath (Join-Path $generated $filename) -PathType Leaf)) {
            throw "Generator incomplete: $filename. Check build-native-prepare.err.log; no duplicate generation started."
        }
    }

    $env:PS5_PAYLOAD_SDK = $sdk
    $env:PS5_LLVM_ROOT = $llvm
    $env:PATH = "$llvm/bin;$oldPath"
    $env:TEMP = Join-Path $build "temp"
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null

    Status "adapting generated C to ELF and LP64"
    & python (Join-Path $project "scripts/prepare_native_elf_sources.py") --generated $generated --out $adapted
    if ($LASTEXITCODE -ne 0) { throw "ELF source adaptation failed: $LASTEXITCODE" }

    Status "configuring the complete generated-game archive"
    & cmake -S (Join-Path $project "native") -B $build -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_TOOLCHAIN_FILE=$project/native/ps5-toolchain.cmake" `
        "-DKHDAYS_NATIVE_UPSTREAM_ROOT=$source" `
        "-DKHDAYS_NATIVE_GENERATED_ROOT=$generated" `
        "-DKHDAYS_NATIVE_ELF_SOURCES=$adapted" `
        -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "Native game configuration failed: $LASTEXITCODE" }

    Status "compiling generated gameplay and platform archives"
    & cmake --build $build --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Native game compilation failed: $LASTEXITCODE" }
    $archive = Join-Path $build "libkhdays-native-ps5-game.a"
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw "Game archive not produced" }
    $status.game_archive = $archive
    $status["game_archive_sha256"] = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    $status["upstream_commit"] = $revision
    $status["decomp_commit"] = $decompRevision
    Status "game archive compiled; complete host runtime and final executable link pending"
} catch {
    if ($buildLock) {
        $status["error"] = $_.Exception.Message
        Status "failed"
    }
    throw
} finally {
    if ($buildLock) { $buildLock.Dispose() }
    $env:PATH = $oldPath
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $env:PS5_PAYLOAD_SDK = $oldSdk
    $env:PS5_LLVM_ROOT = $oldLlvm
}
