[CmdletBinding()]
param(
    [string]$OutputRoot = "build-native-link/PPSA35820"
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$template = Join-Path $project "toolchains/sources/PS5_Vulkan"
$work = Join-Path $project "build-native-libc"
$output = if ([IO.Path]::IsPathRooted($OutputRoot)) {
    [IO.Path]::GetFullPath($OutputRoot)
} else { [IO.Path]::GetFullPath((Join-Path $project $OutputRoot)) }
$rawExpected = "8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb"
$signedExpected = "e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"
$builderSource = Join-Path $template "build/runtime-shim/libc-builder"
$signer = Join-Path $template "build/host/ps5-native-tool.exe"
$api = Join-Path $template "tooling/native/runtime/api-surface.txt"
$imports = Join-Path $template "tooling/native/runtime/imports.txt"
$manifest = Join-Path $template "runtime/libc.prx.sha256"
foreach ($path in @($builderSource, $signer, $api, $imports, $manifest)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing build input: $path" }
}
if ((Get-Content -Raw -LiteralPath $manifest).Trim() -ne "$signedExpected *libc.prx") {
    throw "Runtime manifest differs from the pinned version"
}
New-Item -ItemType Directory -Path $work -Force | Out-Null
$builder = Join-Path $work "libc-builder.exe"
Copy-Item -LiteralPath $builderSource -Destination $builder -Force
foreach ($copy in @("a", "b")) {
    $raw = Join-Path $work "libc-$copy.raw.elf"
    $signed = Join-Path $work "libc-$copy.prx"
    & $builder $api $imports $raw
    if ($LASTEXITCODE -ne 0) { throw "libc generation failed ($copy): $LASTEXITCODE" }
    if ((Get-FileHash -LiteralPath $raw -Algorithm SHA256).Hash -ne $rawExpected) {
        throw "Generated ELF does not match the pinned runtime ($copy)"
    }
    & $signer self --sign --in $raw --out $signed
    if ($LASTEXITCODE -ne 0) { throw "libc FSELF generation failed ($copy)" }
    if ((Get-FileHash -LiteralPath $signed -Algorithm SHA256).Hash -ne $signedExpected) {
        throw "Generated FSELF does not match the pinned runtime ($copy)"
    }
    $inspection = (& $signer self --inspect --file $signed | Out-String)
    if ($LASTEXITCODE -ne 0 -or $inspection -notmatch 'integrity:\s*valid') {
        throw "libc integrity validation failed ($copy)"
    }
}
$module = Join-Path $output "sce_module/libc.prx"
if ((Test-Path -LiteralPath $module) -and
    (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -ne $signedExpected) {
    throw "A different runtime already exists; preserving it: $module"
}
New-Item -ItemType Directory -Path (Split-Path $module) -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $work "libc-a.prx") -Destination $module -Force
$report = [ordered]@{
    module = $module
    bytes = (Get-Item -LiteralPath $module).Length
    sha256 = $signedExpected
    raw_sha256 = $rawExpected
    independent_builds = 2
    byte_identical = $true
    integrity = "valid"
    api_sha256 = (Get-FileHash -LiteralPath $api -Algorithm SHA256).Hash
    imports_sha256 = (Get-FileHash -LiteralPath $imports -Algorithm SHA256).Hash
    final_eboot_import_coverage = "pending final link"
    hardware = "not tested in this game"
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work "validation.json") -Encoding UTF8
$report | ConvertTo-Json
