[CmdletBinding()]
param(
    [string]$Destination = ".cache/khdays-native-v0.1.1"
)

$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$lock = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $project "native/upstream-lock.json") |
    ConvertFrom-Json
$destinationPath = if ([IO.Path]::IsPathRooted($Destination)) {
    [IO.Path]::GetFullPath($Destination)
} else {
    [IO.Path]::GetFullPath((Join-Path $project $Destination))
}

if (-not (Test-Path -LiteralPath (Join-Path $destinationPath ".git"))) {
    New-Item -ItemType Directory -Force -Path (Split-Path $destinationPath) | Out-Null
    & git clone --depth 1 --branch ("v" + $lock.version) --no-recurse-submodules `
        $lock.project $destinationPath
    if ($LASTEXITCODE -ne 0) { throw "Could not clone khdays-port" }
}

$safe = $destinationPath.Replace('\', '/')
$actual = (& git -c "safe.directory=$safe" -C $destinationPath rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $lock.commit) {
    throw "Existing source is not the pinned v$($lock.version) commit: $actual"
}
& git -c "safe.directory=$safe" -C $destinationPath submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw "Could not initialize khdays-decomp" }

$decomp = Join-Path $destinationPath "external/khdays-decomp"
$decompSafe = $decomp.Replace('\', '/')
$decompActual = (& git -c "safe.directory=$decompSafe" -C $decomp rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $decompActual -ne $lock.decomp_commit) {
    throw "khdays-decomp is not pinned to $($lock.decomp_commit): $decompActual"
}

[ordered]@{
    source = $destinationPath
    version = $lock.version
    commit = $actual
    decomp_commit = $decompActual
    game_data_downloaded = $false
} | ConvertTo-Json
