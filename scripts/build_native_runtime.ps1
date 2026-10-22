[CmdletBinding()]
param(
    [string]$SourceRoot = '.cache/khdays-native-v0.1.1',
    [string]$BuildDir = 'build-native-game-ps5',
    [ValidateRange(1, 16)][int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function ProjectPath([string]$value) {
    if ([IO.Path]::IsPathRooted($value)) { return [IO.Path]::GetFullPath($value) }
    return [IO.Path]::GetFullPath((Join-Path $project $value))
}
$source = ProjectPath $SourceRoot
$build = ProjectPath $BuildDir
$sdk = Join-Path $project 'toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk'
$llvm = Join-Path $project 'toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc'
$ninja = Join-Path $env:APPDATA 'Python/Python314/Scripts/ninja.exe'
$generated = Join-Path $source 'build/ps5-gen'
$adapted = Join-Path $project 'build-native-elf-sources'
foreach ($required in @($ninja, "$generated/khdays_data_area.h", "$adapted/sources.cmake",
                        "$sdk/target/include/stdio.h", "$llvm/bin/clang.exe")) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing runtime input: $required" }
}
& cmake -S (Join-Path $project 'native') -B $build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$project/native/ps5-toolchain.cmake" `
    "-DPS5_PAYLOAD_SDK=$sdk" "-DPS5_LLVM_ROOT=$llvm" `
    "-DKHDAYS_NATIVE_UPSTREAM_ROOT=$source" "-DKHDAYS_NATIVE_GENERATED_ROOT=$generated" `
    "-DKHDAYS_NATIVE_ELF_SOURCES=$adapted" -DCMAKE_BUILD_TYPE=Release -DKHDAYS_NATIVE_BUILD_RUNTIME=ON
if ($LASTEXITCODE -ne 0) { throw 'Native runtime configuration failed' }
& cmake --build $build --target khdays-native-ps5-runtime --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Native runtime compilation failed' }
& python (Join-Path $project 'scripts/validate_native_runtime.py') `
    --build $build --llvm "$llvm/bin" --out (Join-Path $build 'runtime-validation')
if ($LASTEXITCODE -ne 0) { throw 'Native runtime integration audit failed' }
Write-Output "Native runtime compiled. Link audit: $build/runtime-validation/validation.json"
