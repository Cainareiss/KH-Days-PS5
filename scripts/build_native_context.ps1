[CmdletBinding()]
param(
    [string]$SourceRoot,
    [string]$BuildDir = "build-native-context-ps5",
    [int]$Jobs = 1,
    [switch]$SkipPrepare
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$lock = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $project "native/upstream-lock.json") |
    ConvertFrom-Json
if (-not $SourceRoot) {
    $SourceRoot = Join-Path $project ".cache/khdays-native-v0.1.1"
}
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$build = if ([IO.Path]::IsPathRooted($BuildDir)) {
    [IO.Path]::GetFullPath($BuildDir)
} else {
    [IO.Path]::GetFullPath((Join-Path $project $BuildDir))
}
$sdk = Join-Path $project "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk"
$llvm = Join-Path $project "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc"
$toolchain = Join-Path $project "native/ps5-toolchain.cmake"
$ninja = Join-Path $env:APPDATA "Python/Python314/Scripts/ninja.exe"
$generated = Join-Path $SourceRoot "build/ps5-gen"

if (-not (Test-Path -LiteralPath (Join-Path $generated "khdays_data_area.h") -PathType Leaf)) {
    if ($SkipPrepare) {
        throw "Native sources are not generated yet. Wait for build-native-prepare.log before running this build."
    }
    $activePrepare = Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
        Where-Object { $_.CommandLine -match '(khdays-native-v0\.1\.1.*prepare\.py|prepare_native_bounded\.py)' }
    if ($activePrepare) {
        throw "A native source generator is already running (PID $($activePrepare.ProcessId -join ', ')); no second generation started."
    }
    & python -u (Join-Path $project "scripts/prepare_native_bounded.py") `
        --source $SourceRoot --out $generated --jobs 1
    if ($LASTEXITCODE -ne 0) { throw "Could not prepare the native game sources" }
}
foreach ($required in @(
    (Join-Path $SourceRoot "port/host/app/context.h"),
    (Join-Path $SourceRoot "port/tests/thread_test.c"),
    (Join-Path $generated "khdays_data_area.h"),
    (Join-Path $sdk "target/include/stdio.h"),
    (Join-Path $llvm "bin/clang.exe"),
    $ninja,
    $toolchain)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file missing: $required"
    }
}

$safeSource = $SourceRoot.Replace('\', '/')
$actual = (& git -c "safe.directory=$safeSource" -C $SourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $lock.commit) {
    throw "khdays-port must be pinned to $($lock.commit); found $actual"
}
$decomp = Join-Path $SourceRoot "external/khdays-decomp"
$safeDecomp = $decomp.Replace('\', '/')
$actualDecomp = (& git -c "safe.directory=$safeDecomp" -C $decomp rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualDecomp -ne $lock.decomp_commit) {
    throw "khdays-decomp must be pinned to $($lock.decomp_commit); found $actualDecomp"
}

$oldPath = $env:PATH
$oldSdk = $env:PS5_PAYLOAD_SDK
$oldLlvm = $env:PS5_LLVM_ROOT
try {
    $env:PS5_PAYLOAD_SDK = $sdk
    $env:PS5_LLVM_ROOT = $llvm
    $env:PATH = "$($llvm)\bin;$($sdk)\win;$oldPath"
    & cmake -S (Join-Path $project "native") -B $build -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DKHDAYS_NATIVE_UPSTREAM_ROOT=$SourceRoot" `
        "-DKHDAYS_NATIVE_GENERATED_ROOT=$generated" `
        -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "Native context configuration failed" }
    & cmake --build $build --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Native context build failed" }

    $archive = Join-Path $build "libkhdays-native-ps5-context.a"
    $hostArchive = Join-Path $build "libkhdays-native-ps5-host-contract.a"
    $nm = Join-Path $llvm "bin/llvm-nm.exe"
    $symbols = (& $nm --defined-only $archive | Out-String)
    foreach ($symbol in @("khdays_context_create", "khdays_context_jump", "khdays_context_switch")) {
        if ($symbols -notmatch "\b$symbol\b") {
            throw "Missing context symbol in Prospero archive: $symbol"
        }
    }
    $report = [ordered]@{
        upstream = $actual
        decomp = $actualDecomp
        target = "x86_64-sie-ps5"
        archive = $archive
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash
        host_archive = $hostArchive
        host_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $hostArchive).Hash
        symbols = @("khdays_context_create", "khdays_context_jump", "khdays_context_switch")
        memory_contract = "compiled"
        host_contract = "clock, atomic files and local time compiled"
        hardware = "pending"
    }
    $report | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $build "validation.json")
    $report | ConvertTo-Json -Compress
} finally {
    $env:PATH = $oldPath
    $env:PS5_PAYLOAD_SDK = $oldSdk
    $env:PS5_LLVM_ROOT = $oldLlvm
}

